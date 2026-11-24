#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/MusicTheory.h"
#include "dsp/PolyphonicAnalyzer.h"
#include "dsp/HarmonyTracker.h"
#include "dsp/HarmonyLinkBridge.h"
#include "dsp/PreviewSynth.h"

#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace harmonyhud {

namespace param {
inline constexpr const char* kTuningA4         = "tuningA4";
inline constexpr const char* kSensitivity      = "sensitivity";
inline constexpr const char* kSmoothing        = "smoothing";
inline constexpr const char* kGuitarTuning     = "guitarTuning";
inline constexpr const char* kAuditionGain     = "auditionGain";
inline constexpr const char* kClashSensitivity = "clashSensitivity";
inline constexpr const char* kFreezeHistory    = "freezeHistory";
inline constexpr const char* kLinkBus          = "linkBus";
inline constexpr const char* kViewMode         = "viewMode";
inline constexpr const char* kOverlayMode      = "overlayMode";
inline constexpr const char* kFretPosition     = "fretPosition";
inline constexpr const char* kAutoPushLive     = "autoPushLive";
inline constexpr const char* kClashAssist      = "clashAssist";
} // namespace param

struct MidiNoteEvent {
    uint8_t note = 0;
    uint8_t isOn = 0;
};

template <size_t Capacity>
class SpscEventFifo {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of two");
public:
    void reset() {
        readIdx_.store(0, std::memory_order_relaxed);
        writeIdx_.store(0, std::memory_order_relaxed);
    }

    bool push(MidiNoteEvent ev) noexcept {
        const size_t w = writeIdx_.load(std::memory_order_relaxed);
        const size_t nextW = (w + 1u) & (Capacity - 1u);
        if (nextW == readIdx_.load(std::memory_order_acquire))
            return false;
        buf_[w] = ev;
        writeIdx_.store(nextW, std::memory_order_release);
        return true;
    }

    bool pop(MidiNoteEvent& out) noexcept {
        const size_t r = readIdx_.load(std::memory_order_relaxed);
        if (r == writeIdx_.load(std::memory_order_acquire))
            return false;
        out = buf_[r];
        readIdx_.store((r + 1u) & (Capacity - 1u), std::memory_order_release);
        return true;
    }

private:
    std::array<MidiNoteEvent, Capacity> buf_{};
    std::atomic<size_t> readIdx_{0};
    std::atomic<size_t> writeIdx_{0};
};

class HarmonyHUDProcessor : public juce::AudioProcessor,
                            private juce::Thread {
public:
    HarmonyHUDProcessor();
    ~HarmonyHUDProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts_; }

    // UI / Test Thread API
    AnalysisSnapshot getSnapshot() const;
    double getCurrentBpm() const { return currentBpm_.load(std::memory_order_relaxed); }
    GuitarTuning getGuitarTuning() const;
    ViewMode getViewMode() const;
    OverlayMode getOverlayMode() const;
    FretPositionFilter getFretPosition() const;
    int getLinkBusIndex() const; // -1 = Off, 0..7 = Bus A..H

    // Link Bus API (busIndex: -1 = Off, 0..7 = Bus A..H; customSuffix optional for isolated tests)
    void connectLinkBus(int busIndex, const char* customSuffix = nullptr);
    void pushReharmToBand(const ChordSymbol& chord, ReharmPushTiming timing, uint8_t lockBars = 1);
    void sendChartToBand();
    void pullChartFromBand();

    // Interactive Chord Progression Editor API
    void editHistoryChord(size_t index, const ChordSymbol& chord);
    void deleteHistoryChord(size_t index);
    void appendHistoryChord(const ChordSymbol& chord);

    void auditionChord(const std::vector<int>& midiNotes, float durationSec = 1.4f);
    void auditionSingleNote(int midiNote, float durationSec = 0.7f);
    void clearHistory();
    void resetAnalysis();

    // Deterministic synchronous drain + link sync for unit tests & offline evaluation
    void flushAnalysisNow();

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    void run() override;
    void drainFifosAndAnalyze();
    void syncLinkBusConnectionIfNeeded();

    juce::AudioProcessorValueTreeState apvts_;

    // Cached atomic parameter pointers (read lock-free)
    std::atomic<float>* pTuningA4_         = nullptr;
    std::atomic<float>* pSensitivity_      = nullptr;
    std::atomic<float>* pSmoothing_        = nullptr;
    std::atomic<float>* pGuitarTuning_     = nullptr;
    std::atomic<float>* pAuditionGain_     = nullptr;
    std::atomic<float>* pClashSensitivity_ = nullptr;
    std::atomic<float>* pFreezeHistory_    = nullptr;
    std::atomic<float>* pLinkBus_          = nullptr;
    std::atomic<float>* pViewMode_         = nullptr;
    std::atomic<float>* pOverlayMode_      = nullptr;
    std::atomic<float>* pFretPosition_     = nullptr;
    std::atomic<float>* pAutoPushLive_     = nullptr;
    std::atomic<float>* pClashAssist_      = nullptr;

    // Lock-free Audio -> Analysis thread FIFOs
    static constexpr size_t kAudioFifoSize = 32768;
    SpscAudioFifo<kAudioFifoSize> mainAudioFifo_;
    SpscAudioFifo<kAudioFifoSize> sidechainAudioFifo_;
    SpscEventFifo<1024> midiEventFifo_;

    std::atomic<bool> sidechainPresent_{false};
    std::atomic<double> currentBpm_{120.0};
    std::atomic<double> currentPpq_{0.0};
    std::atomic<bool> isPlaying_{false};
    std::atomic<bool> clearHistoryRequested_{false};
    std::atomic<bool> resetAnalysisRequested_{false};
    std::atomic<double> sampleRate_{48000.0};

    // Audio-thread audition synth (zero allocations, silent when idle)
    PreviewSynth previewSynth_;

    // SuiteHarmonyLink bidirectional endpoint
    suite::link::HarmonyLinkEndpoint linkEndpoint_;
    mutable std::mutex linkControlMutex_;
    std::string customLinkSuffix_;
    int connectedBusIndex_ = -1;
    bool customSuffixOverride_ = false;
    uint32_t reharmSeq_ = 0;
    ChordSymbol lastReharmChord_;
    ReharmPushTiming lastReharmTiming_ = ReharmPushTiming::NextBar;
    uint8_t lastReharmLockBars_ = 1;
    uint32_t chartSeq_ = 0;
    std::vector<ChordHistoryEntry> lastSentChart_;

    // Background analysis thread objects (guarded by analysisMutex_)
    mutable std::mutex analysisMutex_;
    HarmonyTracker tracker_;
    std::array<float, 2048> drainMainBuf_{};
    std::array<float, 2048> drainSideBuf_{};

    mutable std::mutex snapshotMutex_;
    AnalysisSnapshot latestSnapshot_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HarmonyHUDProcessor)
};

} // namespace harmonyhud
