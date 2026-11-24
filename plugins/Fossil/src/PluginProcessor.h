#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <SuiteHarmonyLink.h>
#include <SuiteTextureLink.h>

#include "FossilParams.h"
#include "dsp/EuclideanGate.h"
#include "dsp/HpssDecomposer.h"
#include "dsp/ShimmerDiffusion.h"
#include "dsp/StrataEngine.h"

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

class FossilAudioProcessor : public juce::AudioProcessor,
                             private juce::AudioProcessorValueTreeState::Listener
{
public:
    FossilAudioProcessor();
    ~FossilAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Fossil"; }

    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Public UI / Host / Test API
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts_; }
    const fossil::FossilTelemetry& getTelemetry() const noexcept { return telemetry_; }

    void triggerFreeze (fossil::StreamSource source, int targetSlot = -1);
    void releaseMomentaryFreeze (int targetSlot = -1);
    void clearLayer (int slot);
    void clearAllLayers();
    void swapLayers (int slotA, int slotB);
    void loadFactoryPreset (int index);

    // Link Bus API (busIndex: -1 = Off, 0..7 = Bus A..H; customSuffix optional for isolated tests)
    void connectTextureLinkBus (int busIndex, const char* customSuffix = nullptr);
    void connectHarmonyLinkBus (int busIndex, const char* customSuffix = nullptr);
    int  getTextureLinkBusIndex() const noexcept { return textureLink_.currentBus(); }
    int  getHarmonyLinkBusIndex() const noexcept { return harmonyLink_.currentBus(); }

    static float quantizeSemitoneToMask (float semi, uint16_t relativeIntervalMask) noexcept;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Direct DSP access for diagnostics / testing
    int getDecompositionLatencySamples() const noexcept { return hpss_.getLatencySamples(); }

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void syncLinkBusesFromParams();
    void readParameters (fossil::MasterParams& out) const noexcept;
    void handleMidiMessages (const juce::MidiBuffer& midiMessages, const fossil::MasterParams& params) noexcept;
    void drainCommands() noexcept;

    struct PendingSnapshot
    {
        int slot = -1;
        fossil::StreamSource source = fossil::StreamSource::Tonal;
        std::vector<float> left;
        std::vector<float> right;
    };

    juce::AudioProcessorValueTreeState apvts_;

    // Raw atomic parameter pointers for zero-lookup audio-thread reads
    std::atomic<float>* pHpssSep_        = nullptr;
    std::atomic<float>* pNoiseThresh_    = nullptr;
    std::atomic<float>* pDryT_           = nullptr;
    std::atomic<float>* pDryH_           = nullptr;
    std::atomic<float>* pDryN_           = nullptr;
    std::atomic<float>* pStrataBlend_    = nullptr;
    std::atomic<float>* pShimmerSmear_   = nullptr;
    std::atomic<float>* pShimmerPitch_   = nullptr;
    std::atomic<float>* pOutputGain_     = nullptr;
    std::atomic<float>* pCrossfadePos_   = nullptr;
    std::atomic<float>* pCrossfadeDepth_ = nullptr;
    std::atomic<float>* pFreezeMode_     = nullptr;
    std::atomic<float>* pMidiFreeze_     = nullptr;

    std::atomic<float>* pEuclidEnable_   = nullptr;
    std::atomic<float>* pEuclidSteps_    = nullptr;
    std::atomic<float>* pEuclidHits_     = nullptr;
    std::atomic<float>* pEuclidRot_      = nullptr;
    std::atomic<float>* pEuclidSmooth_   = nullptr;
    std::atomic<float>* pEuclidDepth_    = nullptr;
    std::atomic<float>* pEuclidRate_     = nullptr;
    std::atomic<float>* pInternalBpm_    = nullptr;

    std::atomic<float>* pTexLinkBus_     = nullptr;
    std::atomic<float>* pVowelModDepth_  = nullptr;
    std::atomic<float>* pHarmLinkBus_    = nullptr;
    std::atomic<float>* pHarmQuantize_   = nullptr;

    struct LayerParamPtrs
    {
        std::atomic<float>* source = nullptr;
        std::atomic<float>* gain   = nullptr;
        std::atomic<float>* pan    = nullptr;
        std::atomic<float>* width  = nullptr;
        std::atomic<float>* pitch  = nullptr;
        std::atomic<float>* grain  = nullptr;
        std::atomic<float>* drift  = nullptr;
        std::atomic<float>* tilt   = nullptr;
        std::atomic<float>* mute   = nullptr;
        std::atomic<float>* solo   = nullptr;
        std::atomic<float>* gate   = nullptr;
    };
    std::array<LayerParamPtrs, fossil::kNumStrata> pLayers_ {};

    // Core DSP engines
    fossil::dsp::HpssDecomposer    hpss_;
    fossil::dsp::StrataEngine      strata_;
    fossil::dsp::EuclideanGate     euclidGate_;
    fossil::dsp::ShimmerDiffusion  shimmer_;

    // Shared-memory IPC endpoints (lock-free audio-thread read/publish)
    suite::texlink::TextureLinkEndpoint      textureLink_;
    suite::harmony_link::HarmonyLinkEndpoint harmonyLink_;
    mutable std::mutex linkControlMutex_;
    std::string texCustomSuffix_;
    std::string harmCustomSuffix_;
    bool texCustomOverride_  = false;
    bool harmCustomOverride_ = false;

    // Pre-allocated audio scratch buffers (zero alloc in processBlock)
    std::vector<float> bufTL_, bufTR_;
    std::vector<float> bufHL_, bufHR_;
    std::vector<float> bufNL_, bufNR_;
    std::vector<float> wetL_,  wetR_;

    // Lock-free queues & telemetry
    fossil::LockFreeQueue<fossil::FossilCommand, 64> commandQueue_;
    fossil::FossilTelemetry telemetry_;

    // Smoothed master gains & link modulators
    float smoothedDryT_        = 0.90f;
    float smoothedDryH_        = 0.50f;
    float smoothedDryN_        = 0.35f;
    float smoothedStrataBlend_ = 0.65f;
    float smoothedOutGain_     = 1.0f;

    float smoothedVowelX_      = 0.5f;
    float smoothedVowelY_      = 0.5f;
    float smoothedVowelBright_ = 0.55f;
    float smoothedVowelModEnv_ = 0.5f;
    float prevEnergyT_         = 0.0f;
    float transientTrigEnv_    = 0.0f;

    uint32_t freezeCaptureSeq_ = 0u;
    uint8_t  lastCaptureSrc_   = 1u;

    int  currentProgram_      = 0;
    int  momentarySlot_       = -1;
    bool sustainPedalDown_    = false;
    double currentSampleRate_ = 48000.0;
    bool prepared_            = false;

    mutable juce::CriticalSection snapshotLock_;
    std::vector<PendingSnapshot> pendingSnapshots_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FossilAudioProcessor)
};
