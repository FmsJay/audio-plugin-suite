#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

inline float loadParam(const std::atomic<float>* p, float fallback) noexcept {
    return (p != nullptr) ? p->load(std::memory_order_relaxed) : fallback;
}

inline void copyFixedString(char* dst, size_t dstSize, const char* src) noexcept {
    if (dst == nullptr || dstSize == 0) return;
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    std::snprintf(dst, dstSize, "%s", src);
}

inline void setChoiceOrFloatParam(juce::AudioProcessorValueTreeState& apvts,
                                  const char* paramId,
                                  float plainValue) {
    if (auto* param = apvts.getParameter(paramId)) {
        param->setValueNotifyingHost(param->convertTo0to1(plainValue));
    }
}

} // namespace

VowelMorphAudioProcessor::VowelMorphAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "VowelMorphState", vowelmorph::createParameterLayout()) {
    cacheParameterPointers();
    apvts_.addParameterListener(vowelmorph::params::textureBus, this);
    apvts_.addParameterListener(vowelmorph::params::harmonyBus, this);
    syncLinkConnectionsFromParams();
}

VowelMorphAudioProcessor::~VowelMorphAudioProcessor() {
    apvts_.removeParameterListener(vowelmorph::params::textureBus, this);
    apvts_.removeParameterListener(vowelmorph::params::harmonyBus, this);
    textureLink_.disconnect();
    harmonyLink_.disconnect();
}

void VowelMorphAudioProcessor::cacheParameterPointers() noexcept {
    using namespace vowelmorph::params;
    pVowelX_             = apvts_.getRawParameterValue(vowelX);
    pVowelY_             = apvts_.getRawParameterValue(vowelY);
    pTractProfile_       = apvts_.getRawParameterValue(tractProfile);
    pThroatSize_         = apvts_.getRawParameterValue(throatSize);
    pJawOpen_            = apvts_.getRawParameterValue(jawOpen);
    pResonance_          = apvts_.getRawParameterValue(resonance);
    pBreathAir_          = apvts_.getRawParameterValue(breathAir);
    pChoirWidth_         = apvts_.getRawParameterValue(choirWidth);
    pDrive_              = apvts_.getRawParameterValue(drive);
    pMix_                = apvts_.getRawParameterValue(mix);
    pOutputDb_           = apvts_.getRawParameterValue(outputDb);

    pModMode_            = apvts_.getRawParameterValue(modMode);
    pModDepth_           = apvts_.getRawParameterValue(modDepth);
    pEnvAttackMs_        = apvts_.getRawParameterValue(envAttackMs);
    pEnvReleaseMs_       = apvts_.getRawParameterValue(envReleaseMs);
    pLfoRateHz_          = apvts_.getRawParameterValue(lfoRateHz);
    pLfoSync_            = apvts_.getRawParameterValue(lfoSync);
    pLfoDiv_             = apvts_.getRawParameterValue(lfoDiv);
    pLfoShape_           = apvts_.getRawParameterValue(lfoShape);

    pSeqRate_            = apvts_.getRawParameterValue(seqRate);
    pSeqGlide_           = apvts_.getRawParameterValue(seqGlide);
    pConsonantDip_       = apvts_.getRawParameterValue(consonantDip);
    for (int i = 0; i < vowelmorph::kNumSeqSteps; ++i) {
        pSeqSteps_[static_cast<size_t>(i)]   = apvts_.getRawParameterValue(seqStepIds[i]);
        pSeqAccents_[static_cast<size_t>(i)] = apvts_.getRawParameterValue(seqAccentIds[i]);
    }

    pTextureBus_         = apvts_.getRawParameterValue(textureBus);
    pLinkEuclidSync_     = apvts_.getRawParameterValue(linkEuclidSync);
    pLinkNoiseBreath_    = apvts_.getRawParameterValue(linkNoiseBreath);
    pLinkTransientJaw_   = apvts_.getRawParameterValue(linkTransientJaw);
    pLinkFreezeSweep_    = apvts_.getRawParameterValue(linkFreezeSweep);

    pHarmonyBus_         = apvts_.getRawParameterValue(harmonyBus);
    pHarmonyChoirDrone_  = apvts_.getRawParameterValue(harmonyChoirDrone);
    pHarmonySectionLift_ = apvts_.getRawParameterValue(harmonySectionLift);
}

void VowelMorphAudioProcessor::setCustomLinkSuffix(const std::string& suffix) {
    customLinkSuffix_ = suffix;
    syncLinkConnectionsFromParams();
}

bool VowelMorphAudioProcessor::connectTextureBus(int busIndex, const char* customSuffix) {
    if (busIndex < 0 || busIndex >= suite::texlink::kNumBuses) {
        textureLink_.disconnect();
        return false;
    }
    const char* sfx = (customSuffix != nullptr && customSuffix[0] != '\0')
                          ? customSuffix
                          : (customLinkSuffix_.empty() ? nullptr : customLinkSuffix_.c_str());
    return textureLink_.connect(busIndex, sfx);
}

bool VowelMorphAudioProcessor::connectHarmonyBus(int busIndex, const char* customSuffix) {
    if (busIndex < 0 || busIndex >= suite::harmony_link::kNumBuses) {
        harmonyLink_.disconnect();
        return false;
    }
    const char* sfx = (customSuffix != nullptr && customSuffix[0] != '\0')
                          ? customSuffix
                          : (customLinkSuffix_.empty() ? nullptr : customLinkSuffix_.c_str());
    return harmonyLink_.connect(busIndex, sfx);
}

void VowelMorphAudioProcessor::syncLinkConnectionsFromParams() {
    const int texChoice  = static_cast<int>(std::round(loadParam(pTextureBus_, 1.0f)));
    const int harmChoice = static_cast<int>(std::round(loadParam(pHarmonyBus_, 0.0f)));
    connectTextureBus(texChoice - 1);
    connectHarmonyBus(harmChoice - 1);
}

void VowelMorphAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue) {
    const int busIndex = static_cast<int>(std::round(newValue)) - 1;
    if (parameterID == vowelmorph::params::textureBus) {
        connectTextureBus(busIndex);
    } else if (parameterID == vowelmorph::params::harmonyBus) {
        connectHarmonyBus(busIndex);
    }
}

void VowelMorphAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    formantBank_.prepare(sampleRate_, samplesPerBlock);
    modEngine_.prepare(sampleRate_);
    syncLinkConnectionsFromParams();
}

void VowelMorphAudioProcessor::releaseResources() {
    formantBank_.reset();
    modEngine_.reset();
}

bool VowelMorphAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto mainOut = layouts.getMainOutputChannelSet();
    const auto mainIn  = layouts.getMainInputChannelSet();
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;
    if (mainIn != mainOut)
        return false;
    return true;
}

void VowelMorphAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer& midiMessages) {
    suite::dsp::ScopedDenormalGuard denormalGuard;

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0)
        return;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch) {
        buffer.clear(ch, 0, numSamples);
    }

    // 1. Handle incoming MIDI note vowel triggers
    for (const auto metadata : midiMessages) {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn()) {
            modEngine_.handleMidiNoteOn(msg.getNoteNumber(), msg.getFloatVelocity());
        } else if (msg.isNoteOff()) {
            modEngine_.handleMidiNoteOff(msg.getNoteNumber());
        } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
            modEngine_.handleMidiNoteOff(-1);
        }
    }

    // 2. Measure block input peak for meters & envelope follower
    const float* inL = buffer.getReadPointer(0);
    const float* inR = (numChannels > 1) ? buffer.getReadPointer(1) : inL;

    float peakInL = 0.0f;
    float peakInR = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        peakInL = std::max(peakInL, std::abs(inL[i]));
        peakInR = std::max(peakInR, std::abs(inR[i]));
    }
    const float blockPeak = std::max(peakInL, peakInR);

    // 3. Query host transport (BPM & PPQ)
    double bpm = 120.0;
    double ppq = 0.0;
    bool hostPlaying = false;
    if (auto* ph = getPlayHead()) {
        if (auto pos = ph->getPosition()) {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto q = pos->getPpqPosition()) ppq = *q;
            hostPlaying = pos->getIsPlaying();
        }
    }

    // 4. Lock-free read from SuiteTextureLink (Fossil peer) & SuiteHarmonyLink (Band/HUD peer)
    vowelmorph::dsp::ModulationEngine::ExternalLinkInputs ext {};
    ext.linkEuclidSync     = loadParam(pLinkEuclidSync_, 1.0f) >= 0.5f;
    ext.linkNoiseBreath    = loadParam(pLinkNoiseBreath_, 1.0f) >= 0.5f;
    ext.linkTransientJaw   = loadParam(pLinkTransientJaw_, 1.0f) >= 0.5f;
    ext.linkFreezeSweep    = loadParam(pLinkFreezeSweep_, 1.0f) >= 0.5f;
    ext.harmonySectionLift = loadParam(pHarmonySectionLift_, 1.0f) >= 0.5f;

    suite::texlink::FossilToVowelPayload fossilIn {};
    bool fossilAlive = false;
    if (textureLink_.isConnected()) {
        textureLink_.readFossil(fossilIn, fossilAlive);
    }

    if (fossilAlive) {
        ext.fossilAlive            = true;
        ext.fossilTransient        = std::max(fossilIn.energyT, fossilIn.transientTrigger);
        ext.fossilTonal            = fossilIn.energyH;
        ext.fossilNoise            = fossilIn.energyN;
        ext.fossilGateEnv          = fossilIn.euclidEnvelope;
        ext.fossilEuclidPulseSeq   = fossilIn.euclidHitSeq;
        ext.fossilEuclidStep       = fossilIn.euclidStep;
        ext.fossilFreezeCaptureSeq = fossilIn.freezeCaptureSeq;
        if (!hostPlaying && fossilIn.bpm >= 30.0 && fossilIn.bpm <= 300.0) {
            bpm = fossilIn.bpm;
        }
    }

    suite::harmony_link::BandToHudPayload bandIn {};
    suite::harmony_link::HudToBandPayload hudIn {};
    bool bandAlive = false;
    bool hudAlive  = false;
    int harmonyRootPc = -1;
    char harmonyChordText[20] { "N.C." };
    char harmonySectionText[16] { "" };

    if (harmonyLink_.isConnected()) {
        harmonyLink_.readBand(bandIn, bandAlive);
        harmonyLink_.readHud(hudIn, hudAlive);
    }

    if (bandAlive || hudAlive) {
        ext.harmonyAlive = true;
        if (bandAlive && bandIn.activeChord.isValid()) {
            harmonyRootPc = bandIn.activeChord.rootPc;
            suite::harmony_link::formatLinkChord(bandIn.activeChord, harmonyChordText, sizeof(harmonyChordText));
            ext.harmonySection = bandIn.songSection;
            copyFixedString(harmonySectionText, sizeof(harmonySectionText), bandIn.sectionLabel);
        } else if (hudAlive && hudIn.liveChord.isValid()) {
            harmonyRootPc = hudIn.liveChord.rootPc;
            suite::harmony_link::formatLinkChord(hudIn.liveChord, harmonyChordText, sizeof(harmonyChordText));
        } else if (bandAlive) {
            harmonyRootPc = bandIn.keyRootPc;
            ext.harmonySection = bandIn.songSection;
            copyFixedString(harmonySectionText, sizeof(harmonySectionText), bandIn.sectionLabel);
            copyFixedString(harmonyChordText, sizeof(harmonyChordText),
                            suite::harmony_link::pitchClassName(bandIn.keyRootPc));
        } else if (hudAlive) {
            harmonyRootPc = hudIn.keyRootPc;
            copyFixedString(harmonyChordText, sizeof(harmonyChordText),
                            suite::harmony_link::pitchClassName(hudIn.keyRootPc));
        }
    }

    // 5. Build ModulationEngine config & step modulation
    const float baseX      = loadParam(pVowelX_, 0.42f);
    const float baseY      = loadParam(pVowelY_, 0.35f);
    const int   tractIdx   = std::clamp(static_cast<int>(std::round(loadParam(pTractProfile_, 2.0f))),
                                        0, vowelmorph::kNumTracts - 1);
    const float throatNorm = loadParam(pThroatSize_, 2.0f) * 0.01f; // -0.5..+0.5
    const float jaw01      = loadParam(pJawOpen_, 68.0f) * 0.01f;
    const float res01      = loadParam(pResonance_, 54.0f) * 0.01f;
    const float breath01   = loadParam(pBreathAir_, 22.0f) * 0.01f;
    const float width01    = loadParam(pChoirWidth_, 65.0f) * 0.01f;
    const float drive01    = loadParam(pDrive_, 14.0f) * 0.01f;
    const float mix01      = loadParam(pMix_, 72.0f) * 0.01f;
    const float outGain    = suite::dsp::dbToGain(loadParam(pOutputDb_, 0.0f));
    const float harmDrone  = loadParam(pHarmonyChoirDrone_, 35.0f) * 0.01f;

    vowelmorph::dsp::ModulationEngine::ModConfig modCfg {};
    modCfg.modMode        = std::clamp(static_cast<int>(std::round(loadParam(pModMode_, 0.0f))), 0, 3);
    modCfg.modDepth       = loadParam(pModDepth_, 48.0f) * 0.01f;
    modCfg.baseX          = baseX;
    modCfg.baseY          = baseY;
    modCfg.baseJaw        = jaw01;
    modCfg.baseThroat     = throatNorm;
    modCfg.envAttackMs    = loadParam(pEnvAttackMs_, 8.0f);
    modCfg.envReleaseMs   = loadParam(pEnvReleaseMs_, 140.0f);
    modCfg.lfoRateHz      = loadParam(pLfoRateHz_, 1.2f);
    modCfg.lfoSync        = loadParam(pLfoSync_, 1.0f) >= 0.5f;
    modCfg.lfoDiv         = std::clamp(static_cast<int>(std::round(loadParam(pLfoDiv_, 6.0f))), 0, 6);
    modCfg.lfoShape       = std::clamp(static_cast<int>(std::round(loadParam(pLfoShape_, 0.0f))), 0, 3);
    modCfg.seqRate        = std::clamp(static_cast<int>(std::round(loadParam(pSeqRate_, 1.0f))), 0, 6);
    modCfg.seqGlide01     = loadParam(pSeqGlide_, 45.0f) * 0.01f;
    modCfg.consonantDip01 = loadParam(pConsonantDip_, 32.0f) * 0.01f;
    for (int i = 0; i < vowelmorph::kNumSeqSteps; ++i) {
        modCfg.seqSteps[static_cast<size_t>(i)]   = std::clamp(static_cast<int>(std::round(loadParam(pSeqSteps_[static_cast<size_t>(i)], 4.0f))), 0, vowelmorph::kStepRest);
        modCfg.seqAccents[static_cast<size_t>(i)] = loadParam(pSeqAccents_[static_cast<size_t>(i)], 0.0f) >= 0.5f;
    }
    modCfg.bpm         = bpm;
    modCfg.ppqPosition = ppq;
    modCfg.hostPlaying = hostPlaying;

    const auto modOut = modEngine_.processBlock(numSamples, blockPeak, modCfg, ext);

    // 6. Compute 5-band formant target & process audio sample-by-sample
    const auto target = vowelmorph::dsp::computeFormants(
        modOut.vowelX,
        modOut.vowelY,
        modOut.jawOpen,
        modOut.throatSize,
        tractIdx,
        res01,
        sampleRate_);

    vowelmorph::dsp::FormantBank::ProcessParams bankParams {};
    bankParams.target             = target;
    bankParams.breathAir01        = breath01;
    bankParams.fossilNoiseBoost01 = modOut.breathBoost;
    bankParams.choirWidth01       = width01;
    bankParams.drive01            = drive01;
    bankParams.mix01              = mix01;
    bankParams.outputGain         = outGain;
    bankParams.consonantDip01     = modOut.consonantDip;
    bankParams.inputEnv01         = modOut.inputEnv;
    bankParams.harmonyRootPc      = harmonyRootPc;
    bankParams.harmonyDrone01     = ext.harmonyAlive ? harmDrone : 0.0f;

    float* outL = buffer.getWritePointer(0);
    float* outR = (numChannels > 1) ? buffer.getWritePointer(1) : nullptr;

    float peakOutL = 0.0f;
    float peakOutR = 0.0f;

    for (int i = 0; i < numSamples; ++i) {
        const float sL = outL[i];
        const float sR = (outR != nullptr) ? outR[i] : sL;
        float yL = 0.0f;
        float yR = 0.0f;
        formantBank_.processSample(sL, sR, yL, yR, bankParams);

        outL[i] = yL;
        peakOutL = std::max(peakOutL, std::abs(yL));
        if (outR != nullptr) {
            outR[i] = yR;
            peakOutR = std::max(peakOutR, std::abs(yR));
        } else {
            peakOutR = peakOutL;
        }
    }

    // 7. Lock-free publish VowelToFossilPayload to SuiteTextureLink
    if (textureLink_.isConnected()) {
        suite::texlink::VowelToFossilPayload vowelOut {};
        uint32_t vFlags = 0u;
        if (modCfg.modMode == static_cast<int>(vowelmorph::ModMode::StepSeq))
            vFlags |= suite::texlink::kVowelSeqActive;
        if (modOut.consonantDip > 0.05f)
            vFlags |= suite::texlink::kVowelConsonantClosure;
        if (breath01 + modOut.breathBoost > 0.05f)
            vFlags |= suite::texlink::kVowelBreathActive;
        if (modCfg.modMode == static_cast<int>(vowelmorph::ModMode::MidiTrigger))
            vFlags |= suite::texlink::kVowelMidiControlled;
        if (ext.harmonyAlive)
            vFlags |= suite::texlink::kVowelHarmonyLinked;

        vowelOut.flags             = vFlags;
        vowelOut.vowelX            = modOut.vowelX;
        vowelOut.vowelY            = modOut.vowelY;
        vowelOut.jawOpen           = modOut.jawOpen;
        vowelOut.throatSize        = std::clamp(modOut.throatSize * 2.0f, -1.0f, 1.0f);
        vowelOut.breathAir         = std::clamp(breath01 + modOut.breathBoost * 0.5f, 0.0f, 1.0f);
        vowelOut.modEnvelope       = std::clamp(0.5f + 0.5f * modOut.modEnvelope, 0.0f, 1.0f);
        vowelOut.formantBrightness = target.brightness;
        vowelOut.resonanceQ        = res01;

        for (int b = 0; b < vowelmorph::kNumFormants; ++b) {
            vowelOut.formantFreqHz[b] = target.freqHz[static_cast<size_t>(b)];
            vowelOut.formantGain[b]   = target.gain[static_cast<size_t>(b)];
        }

        vowelOut.syllableStepSeq    = modOut.stepChangeSeq;
        vowelOut.activeSyllableStep = static_cast<uint8_t>(std::clamp(modOut.activeStep, 0, 7));
        vowelOut.activePhonemeId    = static_cast<uint8_t>(std::clamp(target.nearestPhoneme, 0, 9));
        vowelOut.activeTractProfile = static_cast<uint8_t>(tractIdx);
        vowelOut.modMode            = static_cast<uint8_t>(modCfg.modMode);

        copyFixedString(vowelOut.syllableLabel, sizeof(vowelOut.syllableLabel),
                        vowelmorph::phonemeCode(target.nearestPhoneme));
        copyFixedString(vowelOut.tractLabel, sizeof(vowelOut.tractLabel),
                        vowelmorph::tractName(tractIdx));

        textureLink_.publishVowel(vowelOut);
    }

    // 8. Lock-free publish double-buffered TelemetrySnapshot for UI
    const int writeIdx = 1 - telemetryReadIdx_.load(std::memory_order_relaxed);
    auto& snap = telemetryBuffers_[static_cast<size_t>(writeIdx)];
    snap.inputLevelL   = peakInL;
    snap.inputLevelR   = peakInR;
    snap.outputLevelL  = peakOutL;
    snap.outputLevelR  = peakOutR;
    snap.baseVowelX    = baseX;
    snap.baseVowelY    = baseY;
    snap.liveVowelX    = modOut.vowelX;
    snap.liveVowelY    = modOut.vowelY;
    snap.liveJawOpen   = modOut.jawOpen;
    snap.liveThroat    = modOut.throatSize;
    snap.liveResonance = res01;
    snap.liveBreath    = std::clamp(breath01 + modOut.breathBoost * 0.5f, 0.0f, 1.0f);
    snap.modValue      = modOut.modEnvelope;
    snap.envValue      = modOut.inputEnv;
    snap.dipAmount     = modOut.consonantDip;
    snap.formantFreqHz = target.freqHz;
    snap.formantQ      = target.q;
    snap.formantGain   = target.gain;
    snap.activeStep    = modOut.activeStep;
    snap.activePhoneme = target.nearestPhoneme;
    snap.tractProfile  = tractIdx;
    snap.modMode       = modCfg.modMode;
    snap.euclidLocked  = modOut.euclidLocked;

    snap.fossilConnected   = fossilAlive;
    snap.fossilTransient   = fossilAlive ? std::max(fossilIn.energyT, fossilIn.transientTrigger) : 0.0f;
    snap.fossilTonal       = fossilAlive ? fossilIn.energyH : 0.0f;
    snap.fossilNoise       = fossilAlive ? fossilIn.energyN : 0.0f;
    snap.fossilGateEnv     = fossilAlive ? fossilIn.euclidEnvelope : 0.0f;
    snap.fossilEuclidStep  = fossilAlive ? static_cast<int>(fossilIn.euclidStep) : 0;
    snap.fossilEuclidSteps = fossilAlive ? std::max(1, static_cast<int>(fossilIn.euclidSteps)) : 16;
    snap.fossilEuclidHits  = fossilAlive ? static_cast<int>(fossilIn.euclidHits) : 0;
    snap.fossilStrataCount = fossilAlive ? static_cast<int>(fossilIn.frozenStrataCount) : 0;

    snap.harmonyConnected = ext.harmonyAlive;
    snap.harmonyRootPc    = harmonyRootPc;
    copyFixedString(snap.harmonyChordLabel, sizeof(snap.harmonyChordLabel), harmonyChordText);
    copyFixedString(snap.harmonySectionLabel, sizeof(snap.harmonySectionLabel), harmonySectionText);

    telemetryReadIdx_.store(writeIdx, std::memory_order_release);
}

vowelmorph::TelemetrySnapshot VowelMorphAudioProcessor::getTelemetrySnapshot() const noexcept {
    const int idx = telemetryReadIdx_.load(std::memory_order_acquire);
    return telemetryBuffers_[static_cast<size_t>(idx)];
}

void VowelMorphAudioProcessor::applyFactoryPreset(int index) {
    const int clamped = std::clamp(index, 0, vowelmorph::kNumPresets - 1);
    const auto& preset = vowelmorph::getFactoryPresets()[static_cast<size_t>(clamped)];
    currentPresetIndex_.store(clamped, std::memory_order_relaxed);

    using namespace vowelmorph::params;
    setChoiceOrFloatParam(apvts_, tractProfile,      static_cast<float>(preset.tractProfile));
    setChoiceOrFloatParam(apvts_, vowelX,            preset.vowelX);
    setChoiceOrFloatParam(apvts_, vowelY,            preset.vowelY);
    setChoiceOrFloatParam(apvts_, throatSize,        preset.throatSize);
    setChoiceOrFloatParam(apvts_, jawOpen,           preset.jawOpen);
    setChoiceOrFloatParam(apvts_, resonance,         preset.resonance);
    setChoiceOrFloatParam(apvts_, breathAir,         preset.breathAir);
    setChoiceOrFloatParam(apvts_, choirWidth,        preset.choirWidth);
    setChoiceOrFloatParam(apvts_, drive,             preset.drive);
    setChoiceOrFloatParam(apvts_, mix,               preset.mix);
    setChoiceOrFloatParam(apvts_, modMode,           static_cast<float>(preset.modMode));
    setChoiceOrFloatParam(apvts_, modDepth,          preset.modDepth);
    setChoiceOrFloatParam(apvts_, seqRate,           static_cast<float>(preset.seqRate));
    setChoiceOrFloatParam(apvts_, seqGlide,          preset.seqGlide);
    setChoiceOrFloatParam(apvts_, consonantDip,      preset.consonantDip);
    setChoiceOrFloatParam(apvts_, linkEuclidSync,    preset.euclidSync ? 1.0f : 0.0f);
    setChoiceOrFloatParam(apvts_, harmonyChoirDrone, preset.harmonyDrone);

    for (int i = 0; i < vowelmorph::kNumSeqSteps; ++i) {
        setChoiceOrFloatParam(apvts_, seqStepIds[i],   static_cast<float>(preset.steps[static_cast<size_t>(i)]));
        setChoiceOrFloatParam(apvts_, seqAccentIds[i], preset.accents[static_cast<size_t>(i)] != 0 ? 1.0f : 0.0f);
    }
}

void VowelMorphAudioProcessor::setCurrentProgram(int index) {
    applyFactoryPreset(index);
}

const juce::String VowelMorphAudioProcessor::getProgramName(int index) {
    if (index < 0 || index >= vowelmorph::kNumPresets)
        return {};
    return vowelmorph::getFactoryPresets()[static_cast<size_t>(index)].name;
}

void VowelMorphAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts_.copyState();
    state.setProperty("currentPresetIndex", currentPresetIndex_.load(std::memory_order_relaxed), nullptr);
    if (auto xml = state.createXml()) {
        copyXmlToBinary(*xml, destData);
    }
}

void VowelMorphAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (data == nullptr || sizeInBytes <= 0)
        return;
    if (auto xml = getXmlFromBinary(data, sizeInBytes)) {
        if (xml->hasTagName(apvts_.state.getType())) {
            auto tree = juce::ValueTree::fromXml(*xml);
            const int savedPreset = static_cast<int>(tree.getProperty("currentPresetIndex", 0));
            currentPresetIndex_.store(std::clamp(savedPreset, 0, vowelmorph::kNumPresets - 1),
                                      std::memory_order_relaxed);
            apvts_.replaceState(tree);
            syncLinkConnectionsFromParams();
        }
    }
}

juce::AudioProcessorEditor* VowelMorphAudioProcessor::createEditor() {
    return new VowelMorphAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VowelMorphAudioProcessor();
}
