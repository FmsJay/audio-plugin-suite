#pragma once

#include "../SessionMateTypes.h"
#include "../SessionMateUiBridge.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sessionmate {

class VstSubHost {
public:
    VstSubHost() {
#if JUCE_PLUGINHOST_VST3
        formatManager.addFormat(new juce::VST3PluginFormat());
#endif
        loadPersistedCacheAndBlacklist();
    }

    ~VstSubHost() {
        cancelScan.store(true, std::memory_order_release);
        if (scanThread.joinable())
            scanThread.join();

        closeAllEditors();

        for (int i = 0; i < kNumSlots; ++i) {
            HostedSlotNode* old = slots[static_cast<size_t>(i)].activeNode.exchange(nullptr, std::memory_order_acq_rel);
            if (old != nullptr)
                retireNodeLockedOrDirect(old);
        }

        // Wait briefly for any in-flight audio callback to finish before freeing nodes
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
        while (std::chrono::steady_clock::now() < deadline) {
            bool anyInFlight = false;
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                for (const auto& node : retiredNodes) {
                    if (node && node->inFlight.load(std::memory_order_acquire) > 0) {
                        anyInFlight = true;
                        break;
                    }
                }
            }
            if (!anyInFlight)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        std::lock_guard<std::mutex> lock(stateMutex);
        for (auto& node : retiredNodes) {
            if (node && node->instance)
                node->instance->releaseResources();
        }
        retiredNodes.clear();
    }

    VstSubHost(const VstSubHost&) = delete;
    VstSubHost& operator=(const VstSubHost&) = delete;

    void prepare(double sampleRate, int blockSize) {
        currentSampleRate = (sampleRate > 0.0) ? sampleRate : 44100.0;
        currentBlockSize = (blockSize > 0) ? blockSize : 512;

        for (int i = 0; i < kNumSlots; ++i) {
            HostedSlotNode* node = slots[static_cast<size_t>(i)].activeNode.load(std::memory_order_acquire);
            if (node != nullptr && node->instance != nullptr) {
                const int requiredSamples = std::max(currentBlockSize, 4096);
                const int totalCh = std::max(2, std::max(node->numInChannels, node->numOutChannels));
                if (node->scratchBuffer.getNumChannels() < totalCh ||
                    node->scratchBuffer.getNumSamples() < requiredSamples) {
                    node->scratchBuffer.setSize(totalCh, requiredSamples, false, true, false);
                }
                node->instance->prepareToPlay(currentSampleRate, currentBlockSize);
            }
        }
    }

    void addCustomScanDirectory(const juce::File& dir) {
        if (!dir.isDirectory())
            return;
        std::lock_guard<std::mutex> lock(stateMutex);
        for (const auto& existing : customScanDirs) {
            if (existing == dir)
                return;
        }
        customScanDirs.push_back(dir);
    }

    bool isScanning() const noexcept {
        return scanning.load(std::memory_order_acquire);
    }

    std::string getScanStatus() const {
        std::lock_guard<std::mutex> lock(stateMutex);
        return scanStatus;
    }

    void scanPluginsAsync(std::function<void()> onFinished = {}) {
        bool expected = false;
        if (!scanning.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return;

        if (scanThread.joinable())
            scanThread.join();

        cancelScan.store(false, std::memory_order_release);
        setScanStatus("Scanning VST3 instruments...");

        std::vector<juce::File> dirs = getDefaultScanDirectories();
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            for (const auto& d : customScanDirs)
                dirs.push_back(d);
        }

        scanThread = std::thread([this, dirs = std::move(dirs), cb = std::move(onFinished)]() {
            int totalScanned = 0;
            for (const auto& dir : dirs) {
                if (cancelScan.load(std::memory_order_acquire))
                    break;
                if (dir.isDirectory())
                    totalScanned += scanDirectorySafe(dir, 256);
            }

            if (cancelScan.load(std::memory_order_acquire)) {
                setScanStatus("Scan cancelled");
            } else {
                const auto instruments = getAvailableInstruments();
                setScanStatus("Ready (" + std::to_string(instruments.size()) + " VST3 instruments, " +
                              std::to_string(totalScanned) + " scanned)");
            }

            scanning.store(false, std::memory_order_release);
            if (cb)
                cb();
        });
    }

    int scanDirectorySafe(const juce::File& dir, int maxPlugins = 64) {
        if (!dir.isDirectory() || maxPlugins <= 0)
            return 0;

#if JUCE_PLUGINHOST_VST3
        juce::AudioPluginFormat* vst3Format = getVst3Format();
        if (vst3Format == nullptr)
            return 0;

        const juce::File pedalFile = getDeadMansPedalFile();
        pedalFile.getParentDirectory().createDirectory();

        juce::FileSearchPath searchPath;
        searchPath.add(dir);

        // Local list synced with knownPluginList under mutex so scanning never blocks readers
        juce::KnownPluginList workingList;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            if (auto xml = knownPluginList.createXml())
                workingList.recreateFromXml(*xml);
            for (const auto& bl : knownPluginList.getBlacklistedFiles())
                workingList.addToBlacklist(bl);
        }

        juce::PluginDirectoryScanner scanner(
            workingList,
            *vst3Format,
            searchPath,
            true,
            pedalFile,
            false);

        int scannedCount = 0;
        juce::String pluginName;
        while (scannedCount < maxPlugins && !cancelScan.load(std::memory_order_acquire)) {
            const juce::String nextFile = scanner.getNextPluginFileThatWillBeScanned();
            if (nextFile.isEmpty())
                break;

            if (isSelfPluginString(nextFile)) {
                scanner.skipNextFile();
                continue;
            }

            setScanStatus("Scanning: " + juce::File(nextFile).getFileNameWithoutExtension().toStdString());

            const bool hasMore = scanner.scanNextFile(true, pluginName);
            ++scannedCount;

            if (!hasMore)
                break;
        }

        if (pedalFile.existsAsFile())
            pedalFile.deleteFile();

        // Remove any self-referencing entries and non-instruments before persisting
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            for (const auto& desc : workingList.getTypes()) {
                if (isSelfPluginDescription(desc))
                    continue;
                if (!desc.isInstrument)
                    continue;
                knownPluginList.addType(desc);
            }
            for (const auto& bl : workingList.getBlacklistedFiles())
                knownPluginList.addToBlacklist(bl);
            for (const auto& failed : scanner.getFailedFiles())
                knownPluginList.addToBlacklist(failed);

            savePersistedCacheAndBlacklistLocked();
        }

        return scannedCount;
#else
        juce::ignoreUnused(dir, maxPlugins);
        return 0;
#endif
    }

    std::vector<AvailablePluginItem> getAvailableInstruments() const {
        std::vector<AvailablePluginItem> items;
        std::lock_guard<std::mutex> lock(stateMutex);

        for (const auto& desc : knownPluginList.getTypes()) {
            if (!desc.isInstrument)
                continue;
            if (isSelfPluginDescription(desc))
                continue;

            AvailablePluginItem item;
            item.name = desc.name.toStdString();
            item.manufacturer = desc.manufacturerName.toStdString();
            item.identifier = desc.createIdentifierString().toStdString();
            item.isInstrument = desc.isInstrument;
            items.push_back(std::move(item));
        }

        std::sort(items.begin(), items.end(), [](const AvailablePluginItem& a, const AvailablePluginItem& b) {
            return a.name < b.name;
        });

        return items;
    }

    bool loadPluginByIdentifier(int slot,
                                const std::string& identifier,
                                const std::string& displayName,
                                const juce::MemoryBlock& savedState,
                                double sampleRate,
                                int blockSize,
                                bool& outFallbackUsed,
                                std::string& outError) {
        outFallbackUsed = false;
        outError.clear();

        if (!isValidSlot(slot)) {
            outError = "Invalid slot index";
            return false;
        }

        closePluginEditor(slot);

        SlotHostState& slotState = slots[static_cast<size_t>(slot)];
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            slotState.desiredPluginId = identifier;
            slotState.desiredPluginName = displayName;
            slotState.desiredPluginState = savedState;
        }

        if (identifier.empty()) {
            HostedSlotNode* old = slotState.activeNode.exchange(nullptr, std::memory_order_acq_rel);
            if (old != nullptr)
                retireNodeLockedOrDirect(old);
            slotState.fallbackActive.store(false, std::memory_order_release);
            return false;
        }

        if (isSelfPluginString(juce::String(identifier)) || isSelfPluginString(juce::String(displayName))) {
            outError = "Cannot self-host SessionMate";
            activateFallback(slot);
            outFallbackUsed = true;
            return false;
        }

        juce::PluginDescription desc {};
        if (!findPluginDescription(identifier, desc)) {
            outError = "VST3 plugin not installed on this machine: " +
                       (displayName.empty() ? identifier : displayName);
            activateFallback(slot);
            outFallbackUsed = true;
            return false;
        }

        if (isSelfPluginDescription(desc)) {
            outError = "Cannot self-host SessionMate";
            activateFallback(slot);
            outFallbackUsed = true;
            return false;
        }

        const double sr = (sampleRate > 0.0) ? sampleRate : currentSampleRate;
        const int bs = (blockSize > 0) ? blockSize : currentBlockSize;

        juce::String juceError;
        std::unique_ptr<juce::AudioPluginInstance> instance =
            formatManager.createPluginInstance(desc, sr, bs, juceError);

        if (instance == nullptr) {
            outError = juceError.isNotEmpty()
                ? juceError.toStdString()
                : ("Failed to instantiate VST3 plugin: " + desc.name.toStdString());
            activateFallback(slot);
            outFallbackUsed = true;
            return false;
        }

        instance->enableAllBuses();
        instance->prepareToPlay(sr, bs);

        if (savedState.getSize() > 0) {
            instance->setStateInformation(savedState.getData(), static_cast<int>(savedState.getSize()));
        }

        auto newNode = std::make_unique<HostedSlotNode>();
        newNode->numInChannels = std::max(0, instance->getTotalNumInputChannels());
        newNode->numOutChannels = std::max(2, instance->getTotalNumOutputChannels());
        const int totalCh = std::max(2, std::max(newNode->numInChannels, newNode->numOutChannels));
        const int scratchSamples = std::max(bs, 4096);
        newNode->scratchBuffer.setSize(totalCh, scratchSamples, false, true, false);
        newNode->instance = std::move(instance);

        HostedSlotNode* rawNew = newNode.release();
        HostedSlotNode* old = slotState.activeNode.exchange(rawNew, std::memory_order_acq_rel);
        if (old != nullptr)
            retireNodeLockedOrDirect(old);

        slotState.fallbackActive.store(false, std::memory_order_release);

        {
            std::lock_guard<std::mutex> lock(stateMutex);
            if (slotState.desiredPluginName.empty())
                slotState.desiredPluginName = desc.name.toStdString();
            slotState.activePluginName = desc.name.toStdString();
        }

        return true;
    }

    bool loadPluginByIdSimple(int slot, const std::string& identifier, std::string& outError) {
        bool fallbackUsed = false;
        return loadPluginByIdentifier(
            slot,
            identifier,
            "",
            juce::MemoryBlock(),
            currentSampleRate,
            currentBlockSize,
            fallbackUsed,
            outError);
    }

    void unloadSlot(int slot) {
        if (!isValidSlot(slot))
            return;

        closePluginEditor(slot);

        SlotHostState& slotState = slots[static_cast<size_t>(slot)];
        HostedSlotNode* old = slotState.activeNode.exchange(nullptr, std::memory_order_acq_rel);
        if (old != nullptr)
            retireNodeLockedOrDirect(old);

        slotState.fallbackActive.store(false, std::memory_order_release);

        std::lock_guard<std::mutex> lock(stateMutex);
        slotState.desiredPluginId.clear();
        slotState.desiredPluginName.clear();
        slotState.activePluginName.clear();
        slotState.desiredPluginState.reset();
    }

    // LOCK-FREE, ALLOCATION-FREE AUDIO-THREAD RENDER
    bool processSlotAudio(int slot, juce::AudioBuffer<float>& stereoOut, juce::MidiBuffer& midi) noexcept {
        if (!isValidSlot(slot))
            return false;

        HostedSlotNode* node = slots[static_cast<size_t>(slot)].activeNode.load(std::memory_order_acquire);
        if (node == nullptr || node->instance == nullptr)
            return false;

        node->inFlight.fetch_add(1, std::memory_order_acq_rel);

        const int numSamples = stereoOut.getNumSamples();
        const int maxCh = std::min(node->scratchBuffer.getNumChannels(), kMaxStackChannels);
        const int maxSamples = node->scratchBuffer.getNumSamples();

        if (numSamples > 0 && numSamples <= maxSamples && maxCh >= 2) {
            float* channelPtrs[kMaxStackChannels] {};
            for (int ch = 0; ch < maxCh; ++ch) {
                channelPtrs[ch] = node->scratchBuffer.getWritePointer(ch);
                juce::FloatVectorOperations::clear(channelPtrs[ch], numSamples);
            }

            juce::AudioBuffer<float> aliasBuffer(channelPtrs, maxCh, numSamples);
            node->instance->processBlock(aliasBuffer, midi);

            const int outCh = stereoOut.getNumChannels();
            if (outCh >= 2) {
                juce::FloatVectorOperations::copy(stereoOut.getWritePointer(0), channelPtrs[0], numSamples);
                if (node->numOutChannels >= 2)
                    juce::FloatVectorOperations::copy(stereoOut.getWritePointer(1), channelPtrs[1], numSamples);
                else
                    juce::FloatVectorOperations::copy(stereoOut.getWritePointer(1), channelPtrs[0], numSamples);
            } else if (outCh == 1) {
                juce::FloatVectorOperations::copy(stereoOut.getWritePointer(0), channelPtrs[0], numSamples);
            }
        }

        node->inFlight.fetch_sub(1, std::memory_order_release);
        return true;
    }

    void openPluginEditor(int slot) {
        if (!isValidSlot(slot))
            return;

        SlotHostState& slotState = slots[static_cast<size_t>(slot)];
        if (slotState.editorWindow != nullptr) {
            slotState.editorWindow->setVisible(true);
            slotState.editorWindow->toFront(true);
            return;
        }

        HostedSlotNode* node = slotState.activeNode.load(std::memory_order_acquire);
        if (node == nullptr || node->instance == nullptr)
            return;

        if (juce::AudioProcessorEditor* editor = node->instance->createEditorIfNeeded()) {
            const juce::String title = "SessionMate - " + juce::String(getSlotDisplayName(slot));
            slotState.editorWindow = std::make_unique<PluginEditorWindow>(
                title,
                [this, slot]() { closePluginEditor(slot); });
            slotState.editorWindow->setContentOwned(editor, true);
            slotState.editorWindow->centreWithSize(
                std::max(200, editor->getWidth()),
                std::max(150, editor->getHeight()));
            slotState.editorWindow->setVisible(true);
            slotState.editorWindow->toFront(true);
        }
    }

    void closePluginEditor(int slot) {
        if (!isValidSlot(slot))
            return;
        slots[static_cast<size_t>(slot)].editorWindow.reset();
    }

    void closeAllEditors() {
        for (int i = 0; i < kNumSlots; ++i)
            closePluginEditor(i);
    }

    bool hasActivePlugin(int slot) const noexcept {
        if (!isValidSlot(slot))
            return false;
        return slots[static_cast<size_t>(slot)].activeNode.load(std::memory_order_acquire) != nullptr;
    }

    bool isSlotFallbackActive(int slot) const noexcept {
        if (!isValidSlot(slot))
            return false;
        return slots[static_cast<size_t>(slot)].fallbackActive.load(std::memory_order_acquire);
    }

    std::string getSlotDisplayName(int slot) const {
        if (!isValidSlot(slot))
            return "Built-In";

        std::lock_guard<std::mutex> lock(stateMutex);
        const SlotHostState& slotState = slots[static_cast<size_t>(slot)];
        if (slotState.activeNode.load(std::memory_order_acquire) != nullptr && !slotState.activePluginName.empty())
            return slotState.activePluginName;
        if (!slotState.desiredPluginName.empty())
            return slotState.desiredPluginName;
        return "Built-In";
    }

    std::string getSlotDesiredId(int slot) const {
        if (!isValidSlot(slot))
            return {};
        std::lock_guard<std::mutex> lock(stateMutex);
        return slots[static_cast<size_t>(slot)].desiredPluginId;
    }

    juce::MemoryBlock getSlotSavedState(int slot) const {
        if (!isValidSlot(slot))
            return {};

        const SlotHostState& slotState = slots[static_cast<size_t>(slot)];
        HostedSlotNode* node = slotState.activeNode.load(std::memory_order_acquire);
        if (node != nullptr && node->instance != nullptr) {
            juce::MemoryBlock state;
            node->instance->getStateInformation(state);
            return state;
        }

        std::lock_guard<std::mutex> lock(stateMutex);
        return slotState.desiredPluginState;
    }

    // Safe to call periodically on the UI/message thread to reclaim retired plugin nodes
    void collectGarbage() {
        const auto now = std::chrono::steady_clock::now();
        std::vector<std::unique_ptr<HostedSlotNode>> toDestroy;

        {
            std::lock_guard<std::mutex> lock(stateMutex);
            auto it = retiredNodes.begin();
            while (it != retiredNodes.end()) {
                HostedSlotNode* node = it->get();
                const bool ready = (node == nullptr) ||
                                   (node->inFlight.load(std::memory_order_acquire) == 0 &&
                                    (now - node->retiredAt) >= std::chrono::milliseconds(250));
                if (ready) {
                    toDestroy.push_back(std::move(*it));
                    it = retiredNodes.erase(it);
                } else {
                    ++it;
                }
            }
        }

        for (auto& node : toDestroy) {
            if (node && node->instance)
                node->instance->releaseResources();
        }
    }

private:
    static constexpr int kMaxStackChannels = 32;

    struct HostedSlotNode {
        std::unique_ptr<juce::AudioPluginInstance> instance;
        int numInChannels = 0;
        int numOutChannels = 2;
        juce::AudioBuffer<float> scratchBuffer;
        std::atomic<int> inFlight { 0 };
        std::chrono::steady_clock::time_point retiredAt {};
    };

    class PluginEditorWindow : public juce::DocumentWindow {
    public:
        PluginEditorWindow(const juce::String& title, std::function<void()> onCloseCb)
            : juce::DocumentWindow(
                  title,
                  juce::Colour(0xff181b20),
                  juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
              onClose(std::move(onCloseCb)) {
            setUsingNativeTitleBar(true);
            setResizable(true, true);
        }

        void closeButtonPressed() override {
            if (onClose)
                onClose();
        }

    private:
        std::function<void()> onClose;
    };

    struct SlotHostState {
        std::atomic<HostedSlotNode*> activeNode { nullptr };
        std::atomic<bool> fallbackActive { false };
        std::string desiredPluginId;
        std::string desiredPluginName;
        std::string activePluginName;
        juce::MemoryBlock desiredPluginState;
        std::unique_ptr<PluginEditorWindow> editorWindow;
    };

    static bool isValidSlot(int slot) noexcept {
        return slot >= 0 && slot < kNumSlots;
    }

    static bool isSelfPluginString(const juce::String& str) {
        return str.containsIgnoreCase("SessionMate");
    }

    static bool isSelfPluginDescription(const juce::PluginDescription& desc) {
        return isSelfPluginString(desc.name) ||
               isSelfPluginString(desc.descriptiveName) ||
               isSelfPluginString(desc.fileOrIdentifier);
    }

    static juce::File getAppDataDir() {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("SessionMate");
    }

    static juce::File getDeadMansPedalFile() {
        return getAppDataDir().getChildFile("vst3_deadmanspedal.txt");
    }

    static juce::File getBlacklistFile() {
        return getAppDataDir().getChildFile("vst3_blacklist.txt");
    }

    static juce::File getKnownPluginsFile() {
        return getAppDataDir().getChildFile("known_vst3.xml");
    }

    static std::vector<juce::File> getDefaultScanDirectories() {
        std::vector<juce::File> dirs;
        dirs.emplace_back("C:\\Program Files\\Common Files\\VST3");
        dirs.emplace_back("D:\\Portable Workstation\\DAW\\VST\\VST3");
        return dirs;
    }

    juce::AudioPluginFormat* getVst3Format() const {
        for (int i = 0; i < formatManager.getNumFormats(); ++i) {
            if (auto* fmt = formatManager.getFormat(i)) {
                if (fmt->getName().equalsIgnoreCase("VST3"))
                    return fmt;
            }
        }
        return nullptr;
    }

    void setScanStatus(const std::string& status) {
        std::lock_guard<std::mutex> lock(stateMutex);
        scanStatus = status;
    }

    void activateFallback(int slot) {
        SlotHostState& slotState = slots[static_cast<size_t>(slot)];
        HostedSlotNode* old = slotState.activeNode.exchange(nullptr, std::memory_order_acq_rel);
        if (old != nullptr)
            retireNodeLockedOrDirect(old);
        slotState.fallbackActive.store(true, std::memory_order_release);
    }

    void retireNodeLockedOrDirect(HostedSlotNode* oldNode) {
        if (oldNode == nullptr)
            return;
        oldNode->retiredAt = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(stateMutex);
        retiredNodes.emplace_back(oldNode);
    }

    bool findPluginDescription(const std::string& identifier, juce::PluginDescription& outDesc) {
        const juce::String idStr(identifier);
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            for (const auto& desc : knownPluginList.getTypes()) {
                if (desc.createIdentifierString() == idStr ||
                    desc.fileOrIdentifier.equalsIgnoreCase(idStr) ||
                    desc.name.equalsIgnoreCase(idStr)) {
                    outDesc = desc;
                    return true;
                }
            }
        }

#if JUCE_PLUGINHOST_VST3
        // If identifier is a direct path to a .vst3 bundle/file, scan it on demand
        const juce::File candidate(idStr);
        if (candidate.exists() && candidate.hasFileExtension(".vst3") && !isSelfPluginString(idStr)) {
            if (auto* vst3 = getVst3Format()) {
                juce::OwnedArray<juce::PluginDescription> found;
                vst3->findAllTypesForFile(found, candidate.getFullPathName());
                for (auto* d : found) {
                    if (d != nullptr && d->isInstrument && !isSelfPluginDescription(*d)) {
                        outDesc = *d;
                        std::lock_guard<std::mutex> lock(stateMutex);
                        knownPluginList.addType(*d);
                        savePersistedCacheAndBlacklistLocked();
                        return true;
                    }
                }
            }
        }
#endif
        return false;
    }

    void loadPersistedCacheAndBlacklist() {
        std::lock_guard<std::mutex> lock(stateMutex);

        const juce::File knownFile = getKnownPluginsFile();
        if (knownFile.existsAsFile()) {
            if (auto xml = juce::parseXML(knownFile))
                knownPluginList.recreateFromXml(*xml);
        }

        const juce::File blacklistFile = getBlacklistFile();
        if (blacklistFile.existsAsFile()) {
            juce::StringArray lines;
            blacklistFile.readLines(lines);
            for (const auto& line : lines) {
                const juce::String trimmed = line.trim();
                if (trimmed.isNotEmpty())
                    knownPluginList.addToBlacklist(trimmed);
            }
        }

        // Recover from any previous crash recorded by dead-man's-pedal
        const juce::File pedalFile = getDeadMansPedalFile();
        if (pedalFile.existsAsFile()) {
            const juce::String crashed = pedalFile.loadFileAsString().trim();
            if (crashed.isNotEmpty()) {
                knownPluginList.addToBlacklist(crashed);
                savePersistedCacheAndBlacklistLocked();
            }
            pedalFile.deleteFile();
        }
    }

    void savePersistedCacheAndBlacklistLocked() const {
        const juce::File appDir = getAppDataDir();
        appDir.createDirectory();

        if (auto xml = knownPluginList.createXml())
            xml->writeTo(getKnownPluginsFile());

        const auto blacklisted = knownPluginList.getBlacklistedFiles();
        getBlacklistFile().replaceWithText(blacklisted.joinIntoString("\n"));
    }

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPluginList;

    std::array<SlotHostState, kNumSlots> slots {};
    std::vector<std::unique_ptr<HostedSlotNode>> retiredNodes;
    std::vector<juce::File> customScanDirs;

    mutable std::mutex stateMutex;
    std::thread scanThread;
    std::atomic<bool> scanning { false };
    std::atomic<bool> cancelScan { false };
    std::string scanStatus { "Idle" };

    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
};

} // namespace sessionmate
