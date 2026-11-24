#pragma once

#include "SessionMateTypes.h"
#include "SessionMateParams.h"
#include "SessionMateUiBridge.h"
#include "HarmonyLinkAdapter.h"
#include "styles/ChordParser.h"
#include "styles/StyleLibrary.h"
#include "styles/GrooveComposer.h"
#include "synth/BuiltInBandSynth.h"
#include "host/DrumMapTranslator.h"
#include "host/VstSubHost.h"
#include "host/MidiHistoryExporter.h"
#include "listener/LiveAudioListener.h"
#include "listener/SyncTransport.h"

#include <SuiteHarmonyLink.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace sessionmate {

class SessionMateProcessor : public juce::AudioProcessor,
                             public ISessionMateUiBridge,
                             private juce::AudioProcessorValueTreeState::Listener {
public:
    SessionMateProcessor();
    ~SessionMateProcessor() override;

    // juce::AudioProcessor overrides
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SessionMate"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Direct accessors for tests & editor
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    const StyleLibrary& getStyleLibrary() const noexcept { return grooveComposer->library(); }
    VstSubHost& getVstSubHost() noexcept { return *vstHost; }
    DrumMapTranslator& getDrumMapTranslator() noexcept { return drumMapTranslator; }
    MidiHistoryExporter& getMidiHistoryExporter() noexcept { return *midiHistory; }
    suite::link::HarmonyLinkEndpoint& getHarmonyLink() noexcept { return *harmonyLink; }

    // For isolated unit / joint integration testing on custom named shared-memory suffix
    void setLinkTestSuffix(const std::string& suffix);
    void syncLinkBusConnection();

    // ISessionMateUiBridge implementation
    UiSnapshot getUiSnapshot() const override;

    int getStyleCount() const override;
    std::string getStyleName(int index) const override;
    std::string getStyleFamilyName(int index) const override;

    void setStandalonePlaying(bool shouldPlay) override;
    bool isStandalonePlaying() const override;
    void triggerTapTempo() override;
    void triggerFillNow() override;

    std::string getChordChartText() const override;
    void setChordChartText(const std::string& chartText) override;
    std::vector<std::string> getChordBarLabels() const override;
    std::string formatChordSymbol(const ChordSymbol& chord) const override;
    std::vector<std::pair<std::string, std::string>> getChordProgressionPresets() const override;
    void clearActiveReharm() override;
    std::string getModeName(ScaleMode mode) const override;

    std::vector<AvailablePluginItem> getAvailableVst3Plugins() const override;
    void startVst3ScanAsync() override;
    bool isVst3Scanning() const override;
    std::string getVst3ScanStatus() const override;

    SlotSourceMode getSlotSourceMode(int slot) const override;
    std::string getSlotSourceLabel(int slot) const override;
    bool isSlotFallbackActive(int slot) const override;
    void selectBuiltInForSlot(int slot) override;
    bool selectVst3ForSlot(int slot, const std::string& pluginIdentifier, std::string& outError) override;
    bool loadSfzForSlot(int slot, const juce::File& sfzOrWavFile, std::string& outError) override;
    void openSlotPluginGui(int slot) override;
    bool hasHostedPluginInSlot(int slot) const override;

    juce::File createDragMidiFile(int slotFilter) const override;
    bool exportMidiToFile(const juce::File& file, int slotFilter) const override;
    void clearMidiHistory() override;

    bool exportSessionMatePreset(const juce::File& file) const override;
    bool importSessionMatePreset(const juce::File& file, std::string& outStatus) override;

    // Portable JSON preset helpers (also testable without disk I/O)
    juce::String serializePresetToJson() const;
    bool deserializePresetFromJson(const juce::String& jsonText, std::string& outStatus);

private:
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    static BusesProperties makeBusesLayout();
    void cacheParameterPointers();
    void readParametersToControls(PerformanceControls& perf,
                                  SyncMode& outSyncMode,
                                  double& outManualBpm,
                                  std::array<SlotConfig, kNumSlots>& outSlots) const noexcept;
    void applyPendingChordProgressionOnAudioThread() noexcept;
    void syncIncomingLinkChartToUi() const;

    juce::AudioProcessorValueTreeState apvts;

    // Cached raw atomic parameter pointers (zero lookup on audio thread)
    struct GlobalParamPtrs {
        std::atomic<float>* syncMode = nullptr;
        std::atomic<float>* manualBpm = nullptr;
        std::atomic<float>* styleA = nullptr;
        std::atomic<float>* styleB = nullptr;
        std::atomic<float>* styleMorph = nullptr;
        std::atomic<float>* timeSig = nullptr;
        std::atomic<float>* section = nullptr;
        std::atomic<float>* energy = nullptr;
        std::atomic<float>* swing = nullptr;
        std::atomic<float>* pushPullMs = nullptr;
        std::atomic<float>* leaveSpace = nullptr;
        std::atomic<float>* autoChord = nullptr;
        std::atomic<float>* autoSection = nullptr;
        std::atomic<float>* keyRoot = nullptr;
        std::atomic<float>* keyMinor = nullptr;
        std::atomic<float>* halfTime = nullptr;
        std::atomic<float>* doubleTime = nullptr;
        std::atomic<float>* masterVolDb = nullptr;
        std::atomic<float>* harmonicSource = nullptr;
        std::atomic<float>* linkBus = nullptr;
        std::atomic<float>* clashAvoid = nullptr;
        std::atomic<float>* keysVoicing = nullptr;
    } globalParams;

    struct SlotParamPtrs {
        std::atomic<float>* patch = nullptr;
        std::atomic<float>* drumMap = nullptr;
        std::atomic<float>* busy = nullptr;
        std::atomic<float>* kickLock = nullptr;
        std::atomic<float>* vol = nullptr;
        std::atomic<float>* pan = nullptr;
        std::atomic<float>* reverb = nullptr;
        std::atomic<float>* tilt = nullptr;
        std::atomic<float>* mute = nullptr;
        std::atomic<float>* solo = nullptr;
        std::atomic<float>* lock = nullptr;
        std::atomic<float>* callResp = nullptr;
    };
    std::array<SlotParamPtrs, kNumSlots> slotParams {};

    // Core Subsystems (heap-owned once at construction -> tiny stack footprint, zero audio-thread allocs)
    std::unique_ptr<LiveAudioListener> liveListener;
    SyncTransport syncTransport;
    std::unique_ptr<GrooveComposer> grooveComposer;
    std::unique_ptr<BuiltInBandSynth> bandSynth;
    DrumMapTranslator drumMapTranslator;
    std::unique_ptr<VstSubHost> vstHost;
    std::unique_ptr<MidiHistoryExporter> midiHistory;
    std::unique_ptr<suite::link::HarmonyLinkEndpoint> harmonyLink;

    // HarmonyLink state (audio thread & connection management)
    std::string linkTestSuffix;
    uint32_t lastSeenHudReharmSeq = 0;
    uint32_t lastSeenHudChartSeq = 0;
    uint32_t publishedBandChartSeq = 1;
    std::atomic<bool> pendingClearReharm { false };

    // Lock-free double-buffered incoming chart from HarmonyHUD (Audio -> UI sync)
    struct IncomingChartSlot {
        std::array<ChordSymbol, kMaxChordBars> chords {};
        int numBars = 0;
    };
    mutable std::array<IncomingChartSlot, 2> incomingLinkCharts {};
    mutable std::atomic<int> incomingLinkChartIdx { 0 };
    mutable std::atomic<uint32_t> incomingLinkChartVersion { 0 };
    mutable std::atomic<uint32_t> appliedIncomingLinkChartVersion { 0 };

    // Pre-allocated audio scratch buffers (zero allocations in processBlock)
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
    std::array<juce::AudioBuffer<float>, kNumSlots> slotAudioBuffers;
    std::array<juce::MidiBuffer, kNumSlots> slotMidiBuffers;
    std::array<BlockEventList, kNumSlots> slotEventLists;
    BlockEventList masterBlockEvents;

    // Per-slot source mode & SFZ metadata
    std::array<std::atomic<int>, kNumSlots> slotSourceModes {};
    std::array<std::string, kNumSlots> slotSfzPaths {};
    mutable std::mutex metadataMutex;

    // Lock-free double-buffered Chord Track (UI -> Audio & Audio -> BandToHud)
    struct ChordChartSlot {
        std::array<ChordSymbol, kMaxChordBars> chords {};
        int numBars = 4;
    };
    mutable std::array<ChordChartSlot, 2> chordBuffers {};
    mutable std::atomic<int> activeChordBufferIdx { 0 };
    mutable std::atomic<uint32_t> chordChartVersion { 1 };
    uint32_t lastAppliedChordVersion = 0;
    mutable std::string chordChartText { "| Em9 | A13 | Dmaj7 | B7#9 |" };

    // Audio-thread local copy of current chord chart (for lock-free BandToHud broadcast)
    std::array<ChordSymbol, kMaxChordBars> audioThreadChart {};
    int audioThreadChartBars = 4;

    // Manual fill request (UI -> Audio)
    std::atomic<bool> pendingFillRequest { false };

    // Lock-free double-buffered UiSnapshot (Audio -> UI)
    std::array<UiSnapshot, 2> uiSnapshots {};
    std::atomic<int> publishedSnapshotIdx { 0 };
    std::atomic<bool> standalonePlayingFlag { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SessionMateProcessor)
};

} // namespace sessionmate
