#pragma once

#include "SessionMateTypes.h"
#include <juce_core/juce_core.h>
#include <string>
#include <vector>

namespace sessionmate {

struct AvailablePluginItem {
    std::string name;
    std::string manufacturer;
    std::string identifier;
    bool isInstrument = true;
};

// Clean decoupling interface between SessionMateProcessor and SessionMateEditor UI
class ISessionMateUiBridge {
public:
    virtual ~ISessionMateUiBridge() = default;

    // Real-time snapshot for 30Hz UI repaint
    virtual UiSnapshot getUiSnapshot() const = 0;

    // Style catalog metadata
    virtual int getStyleCount() const = 0;
    virtual std::string getStyleName(int index) const = 0;
    virtual std::string getStyleFamilyName(int index) const = 0;

    // Transport & Jam controls
    virtual void setStandalonePlaying(bool shouldPlay) = 0;
    virtual bool isStandalonePlaying() const = 0;
    virtual void triggerTapTempo() = 0;
    virtual void triggerFillNow() = 0;

    // Chord Track editing & Quick-Start Presets
    virtual std::string getChordChartText() const = 0;
    virtual void setChordChartText(const std::string& chartText) = 0;
    virtual std::vector<std::string> getChordBarLabels() const = 0;
    virtual std::string formatChordSymbol(const ChordSymbol& chord) const = 0;
    virtual std::vector<std::pair<std::string, std::string>> getChordProgressionPresets() const = 0;
    virtual void clearActiveReharm() = 0;
    virtual std::string getModeName(ScaleMode mode) const = 0;

    // Per-Slot VST3 Sub-Host & SFZ controls
    virtual std::vector<AvailablePluginItem> getAvailableVst3Plugins() const = 0;
    virtual void startVst3ScanAsync() = 0;
    virtual bool isVst3Scanning() const = 0;
    virtual std::string getVst3ScanStatus() const = 0;

    virtual SlotSourceMode getSlotSourceMode(int slot) const = 0;
    virtual std::string getSlotSourceLabel(int slot) const = 0;
    virtual bool isSlotFallbackActive(int slot) const = 0;
    virtual void selectBuiltInForSlot(int slot) = 0;
    virtual bool selectVst3ForSlot(int slot, const std::string& pluginIdentifier, std::string& outError) = 0;
    virtual bool loadSfzForSlot(int slot, const juce::File& sfzOrWavFile, std::string& outError) = 0;
    virtual void openSlotPluginGui(int slot) = 0;
    virtual bool hasHostedPluginInSlot(int slot) const = 0;

    // MIDI History Export & Drag-to-DAW
    virtual juce::File createDragMidiFile(int slotFilter) const = 0;
    virtual bool exportMidiToFile(const juce::File& file, int slotFilter) const = 0;
    virtual void clearMidiHistory() = 0;

    // Portable .sessionmate Preset Save/Load
    virtual bool exportSessionMatePreset(const juce::File& file) const = 0;
    virtual bool importSessionMatePreset(const juce::File& file, std::string& outStatus) = 0;
};

} // namespace sessionmate
