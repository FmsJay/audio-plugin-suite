#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <SuiteDSP.h>
#include <SuiteTextureLink.h>
#include <SuiteHarmonyLink.h>

#include "VowelMorphParams.h"
#include "dsp/PhoneticTables.h"
#include "dsp/FormantBank.h"
#include "dsp/ModulationEngine.h"

#include <array>
#include <atomic>
#include <string>

class VowelMorphAudioProcessor : public juce::AudioProcessor,
                                 private juce::AudioProcessorValueTreeState::Listener {
public:
    VowelMorphAudioProcessor();
    ~VowelMorphAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "VowelMorph"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.15; }

    int getNumPrograms() override { return vowelmorph::kNumPresets; }
    int getCurrentProgram() override { return currentPresetIndex_.load(std::memory_order_relaxed); }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts_; }
    const juce::AudioProcessorValueTreeState& getAPVTS() const noexcept { return apvts_; }

    // Apply one of the 18 factory presets
    void applyFactoryPreset(int index);

    // Lock-free telemetry snapshot for UI (double-buffered seqlock-like index swap)
    vowelmorph::TelemetrySnapshot getTelemetrySnapshot() const noexcept;

    // Explicit Link bus control (useful for tests & isolated custom suffixes)
    void setCustomLinkSuffix(const std::string& suffix);
    bool connectTextureBus(int busIndex, const char* customSuffix = nullptr);
    bool connectHarmonyBus(int busIndex, const char* customSuffix = nullptr);
    int  getConnectedTextureBus() const noexcept { return textureLink_.currentBus(); }
    int  getConnectedHarmonyBus() const noexcept { return harmonyLink_.currentBus(); }

private:
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void syncLinkConnectionsFromParams();
    void cacheParameterPointers() noexcept;

    juce::AudioProcessorValueTreeState apvts_;

    // Cached atomic parameter pointers (zero string lookup on audio thread)
    std::atomic<float>* pVowelX_             = nullptr;
    std::atomic<float>* pVowelY_             = nullptr;
    std::atomic<float>* pTractProfile_       = nullptr;
    std::atomic<float>* pThroatSize_         = nullptr;
    std::atomic<float>* pJawOpen_            = nullptr;
    std::atomic<float>* pResonance_          = nullptr;
    std::atomic<float>* pBreathAir_          = nullptr;
    std::atomic<float>* pChoirWidth_         = nullptr;
    std::atomic<float>* pDrive_              = nullptr;
    std::atomic<float>* pMix_                = nullptr;
    std::atomic<float>* pOutputDb_           = nullptr;

    std::atomic<float>* pModMode_            = nullptr;
    std::atomic<float>* pModDepth_           = nullptr;
    std::atomic<float>* pEnvAttackMs_        = nullptr;
    std::atomic<float>* pEnvReleaseMs_       = nullptr;
    std::atomic<float>* pLfoRateHz_          = nullptr;
    std::atomic<float>* pLfoSync_            = nullptr;
    std::atomic<float>* pLfoDiv_             = nullptr;
    std::atomic<float>* pLfoShape_           = nullptr;

    std::atomic<float>* pSeqRate_            = nullptr;
    std::atomic<float>* pSeqGlide_           = nullptr;
    std::atomic<float>* pConsonantDip_       = nullptr;
    std::array<std::atomic<float>*, vowelmorph::kNumSeqSteps> pSeqSteps_ {};
    std::array<std::atomic<float>*, vowelmorph::kNumSeqSteps> pSeqAccents_ {};

    std::atomic<float>* pTextureBus_         = nullptr;
    std::atomic<float>* pLinkEuclidSync_     = nullptr;
    std::atomic<float>* pLinkNoiseBreath_    = nullptr;
    std::atomic<float>* pLinkTransientJaw_   = nullptr;
    std::atomic<float>* pLinkFreezeSweep_    = nullptr;

    std::atomic<float>* pHarmonyBus_         = nullptr;
    std::atomic<float>* pHarmonyChoirDrone_  = nullptr;
    std::atomic<float>* pHarmonySectionLift_ = nullptr;

    // DSP engines
    double sampleRate_ = 48000.0;
    vowelmorph::dsp::FormantBank      formantBank_;
    vowelmorph::dsp::ModulationEngine modEngine_;

    // IPC Link Endpoints
    suite::texlink::TextureLinkEndpoint      textureLink_;
    suite::harmony_link::HarmonyLinkEndpoint harmonyLink_;
    std::string customLinkSuffix_;

    // Double-buffered lock-free UI telemetry
    std::array<vowelmorph::TelemetrySnapshot, 2> telemetryBuffers_ {};
    std::atomic<int> telemetryReadIdx_ { 0 };
    std::atomic<int> currentPresetIndex_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VowelMorphAudioProcessor)
};
