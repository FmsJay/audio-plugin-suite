#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
inline float clampFinite (float x, float lo = -4.0f, float hi = 4.0f) noexcept
{
    if (! std::isfinite (x))
        return 0.0f;
    return std::clamp (x, lo, hi);
}

inline float softLimit (float x) noexcept
{
    if (! std::isfinite (x))
        return 0.0f;
    constexpr float knee = 0.92f;
    const float ax = std::abs (x);
    if (ax <= knee)
        return x;
    const float excess = ax - knee;
    const float shaped = knee + (1.0f - knee) * std::tanh (excess / (1.0f - knee));
    return x >= 0.0f ? shaped : -shaped;
}

inline void setNormalizedParam (juce::AudioProcessorValueTreeState& apvts,
                                const char* paramId,
                                float plainValue)
{
    if (auto* p = apvts.getParameter (paramId))
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
}

template <size_t N>
inline void copyStringToAtomicChars (const char* src, std::array<std::atomic<char>, N>& dst) noexcept
{
    size_t i = 0;
    if (src != nullptr)
    {
        for (; i + 1 < N && src[i] != '\0'; ++i)
            dst[i].store (src[i], std::memory_order_relaxed);
    }
    for (; i < N; ++i)
        dst[i].store ('\0', std::memory_order_relaxed);
}
} // namespace

FossilAudioProcessor::FossilAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_ (*this, nullptr, "FossilState", createParameterLayout())
{
    using namespace fossil::param_ids;

    pHpssSep_        = apvts_.getRawParameterValue (hpssSep);
    pNoiseThresh_    = apvts_.getRawParameterValue (noiseThresh);
    pDryT_           = apvts_.getRawParameterValue (dryT);
    pDryH_           = apvts_.getRawParameterValue (dryH);
    pDryN_           = apvts_.getRawParameterValue (dryN);
    pStrataBlend_    = apvts_.getRawParameterValue (strataBlend);
    pShimmerSmear_   = apvts_.getRawParameterValue (shimmerSmear);
    pShimmerPitch_   = apvts_.getRawParameterValue (shimmerPitch);
    pOutputGain_     = apvts_.getRawParameterValue (outputGain);
    pCrossfadePos_   = apvts_.getRawParameterValue (crossfadePos);
    pCrossfadeDepth_ = apvts_.getRawParameterValue (crossfadeDepth);
    pFreezeMode_     = apvts_.getRawParameterValue (freezeMode);
    pMidiFreeze_     = apvts_.getRawParameterValue (midiFreeze);

    pEuclidEnable_   = apvts_.getRawParameterValue (euclidEnable);
    pEuclidSteps_    = apvts_.getRawParameterValue (euclidSteps);
    pEuclidHits_     = apvts_.getRawParameterValue (euclidHits);
    pEuclidRot_      = apvts_.getRawParameterValue (euclidRot);
    pEuclidSmooth_   = apvts_.getRawParameterValue (euclidSmooth);
    pEuclidDepth_    = apvts_.getRawParameterValue (euclidDepth);
    pEuclidRate_     = apvts_.getRawParameterValue (euclidRate);
    pInternalBpm_    = apvts_.getRawParameterValue (internalBpm);

    pTexLinkBus_     = apvts_.getRawParameterValue (textureLinkBus);
    pVowelModDepth_  = apvts_.getRawParameterValue (vowelModDepth);
    pHarmLinkBus_    = apvts_.getRawParameterValue (harmonyLinkBus);
    pHarmQuantize_   = apvts_.getRawParameterValue (harmQuantize);

    for (int i = 0; i < fossil::kNumStrata; ++i)
    {
        auto& lp  = pLayers_[static_cast<size_t> (i)];
        lp.source = apvts_.getRawParameterValue (layerSource[static_cast<size_t> (i)]);
        lp.gain   = apvts_.getRawParameterValue (layerGain[static_cast<size_t> (i)]);
        lp.pan    = apvts_.getRawParameterValue (layerPan[static_cast<size_t> (i)]);
        lp.width  = apvts_.getRawParameterValue (layerWidth[static_cast<size_t> (i)]);
        lp.pitch  = apvts_.getRawParameterValue (layerPitch[static_cast<size_t> (i)]);
        lp.grain  = apvts_.getRawParameterValue (layerGrain[static_cast<size_t> (i)]);
        lp.drift  = apvts_.getRawParameterValue (layerDrift[static_cast<size_t> (i)]);
        lp.tilt   = apvts_.getRawParameterValue (layerTilt[static_cast<size_t> (i)]);
        lp.mute   = apvts_.getRawParameterValue (layerMute[static_cast<size_t> (i)]);
        lp.solo   = apvts_.getRawParameterValue (layerSolo[static_cast<size_t> (i)]);
        lp.gate   = apvts_.getRawParameterValue (layerGate[static_cast<size_t> (i)]);
    }

    apvts_.addParameterListener (textureLinkBus, this);
    apvts_.addParameterListener (harmonyLinkBus, this);

    loadFactoryPreset (0);
}

FossilAudioProcessor::~FossilAudioProcessor()
{
    apvts_.removeParameterListener (fossil::param_ids::textureLinkBus, this);
    apvts_.removeParameterListener (fossil::param_ids::harmonyLinkBus, this);
    textureLink_.disconnect();
    harmonyLink_.disconnect();
}

juce::AudioProcessorValueTreeState::ParameterLayout FossilAudioProcessor::createParameterLayout()
{
    using namespace fossil::param_ids;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Decomposition & Dry Pass
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { hpssSep, 1 }, "HPSS Separation",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.72f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { noiseThresh, 1 }, "Noise Sensitivity",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.45f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { dryT, 1 }, "Dry Transient Pass",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.90f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { dryH, 1 }, "Dry Tonal Pass",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.50f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { dryN, 1 }, "Dry Noise Pass",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.30f));

    // Master Strata & Shimmer
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { strataBlend, 1 }, "Strata Blend",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.62f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { shimmerSmear, 1 }, "Shimmer Smear",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.44f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { shimmerPitch, 1 }, "Shimmer Octave",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.35f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { outputGain, 1 }, "Output Gain",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { crossfadePos, 1 }, "Strata Crossfade",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.50f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { crossfadeDepth, 1 }, "Crossfade Focus",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { freezeMode, 1 }, "Freeze Mode",
        juce::StringArray { "Latch", "Momentary" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { midiFreeze, 1 }, "MIDI Freeze", true));

    // Euclidean Gate
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { euclidEnable, 1 }, "Euclidean Gate", true));
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { euclidSteps, 1 }, "Euclid Steps", 1, 16, 16));
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { euclidHits, 1 }, "Euclid Hits", 0, 16, 5));
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { euclidRot, 1 }, "Euclid Rotation", 0, 15, 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { euclidSmooth, 1 }, "Euclid Smoothness",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.35f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { euclidDepth, 1 }, "Euclid Depth",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.85f));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { euclidRate, 1 }, "Euclid Rate",
        juce::StringArray { "1/4", "1/8", "1/16", "1/8T", "1/32" }, 2));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { internalBpm, 1 }, "Internal BPM",
        juce::NormalisableRange<float> (40.0f, 240.0f, 0.1f), 120.0f));

    // SuiteTextureLink & SuiteHarmonyLink
    const juce::StringArray busChoices { "Off", "Bus A", "Bus B", "Bus C", "Bus D", "Bus E", "Bus F", "Bus G", "Bus H" };
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { textureLinkBus, 1 }, "Texture Link Bus", busChoices, 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { vowelModDepth, 1 }, "Vowel Mod Depth",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.65f));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { harmonyLinkBus, 1 }, "Harmony Link Bus", busChoices, 0));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { harmQuantize, 1 }, "Harmonic Quantize", true));

    // Per-Stratum Parameters (L1..L4)
    const juce::StringArray sourceChoices { "Transient (T)", "Tonal (H)", "Air/Noise (N)" };
    const std::array<int, fossil::kNumStrata> defaultSources { 1, 1, 2, 0 };
    const std::array<float, fossil::kNumStrata> defaultPitches { 0.0f, 12.0f, 0.0f, 0.0f };
    const std::array<bool, fossil::kNumStrata> defaultGates { false, false, true, true };

    for (int i = 0; i < fossil::kNumStrata; ++i)
    {
        const auto idx = static_cast<size_t> (i);
        const juce::String prefix = "L" + juce::String (i + 1) + " ";

        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { layerSource[idx], 1 }, prefix + "Source", sourceChoices, defaultSources[idx]));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { layerGain[idx], 1 }, prefix + "Gain",
            juce::NormalisableRange<float> (0.0f, 1.5f, 0.001f), 0.80f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { layerPan[idx], 1 }, prefix + "Pan",
            juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { layerWidth[idx], 1 }, prefix + "Width",
            juce::NormalisableRange<float> (0.0f, 2.0f, 0.001f), 1.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { layerPitch[idx], 1 }, prefix + "Pitch",
            juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), defaultPitches[idx]));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { layerGrain[idx], 1 }, prefix + "Grain Size",
            juce::NormalisableRange<float> (20.0f, 500.0f, 0.5f, 0.6f), 180.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { layerDrift[idx], 1 }, prefix + "Drift",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.30f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { layerTilt[idx], 1 }, prefix + "Tilt",
            juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { layerMute[idx], 1 }, prefix + "Mute", false));
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { layerSolo[idx], 1 }, prefix + "Solo", false));
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { layerGate[idx], 1 }, prefix + "Euclid Gate", defaultGates[idx]));
    }

    return { params.begin(), params.end() };
}

void FossilAudioProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    const int busIdx = static_cast<int> (std::lround (newValue)) - 1; // 0="Off" -> -1, 1="Bus A" -> 0
    if (parameterID == fossil::param_ids::textureLinkBus)
    {
        std::lock_guard<std::mutex> lock (linkControlMutex_);
        const char* suf = (texCustomOverride_ && ! texCustomSuffix_.empty()) ? texCustomSuffix_.c_str() : nullptr;
        if (busIdx >= 0 && busIdx < suite::texlink::kNumBuses)
            textureLink_.connect (busIdx, suf);
        else
            textureLink_.disconnect();
    }
    else if (parameterID == fossil::param_ids::harmonyLinkBus)
    {
        std::lock_guard<std::mutex> lock (linkControlMutex_);
        const char* suf = (harmCustomOverride_ && ! harmCustomSuffix_.empty()) ? harmCustomSuffix_.c_str() : nullptr;
        if (busIdx >= 0 && busIdx < suite::harmony_link::kNumBuses)
            harmonyLink_.connect (busIdx, suf);
        else
            harmonyLink_.disconnect();
    }
}

void FossilAudioProcessor::connectTextureLinkBus (int busIndex, const char* customSuffix)
{
    {
        std::lock_guard<std::mutex> lock (linkControlMutex_);
        if (customSuffix != nullptr && customSuffix[0] != '\0')
        {
            texCustomSuffix_   = customSuffix;
            texCustomOverride_ = true;
        }
        else
        {
            texCustomSuffix_.clear();
            texCustomOverride_ = false;
        }

        if (busIndex >= 0 && busIndex < suite::texlink::kNumBuses)
            textureLink_.connect (busIndex, texCustomOverride_ ? texCustomSuffix_.c_str() : nullptr);
        else
            textureLink_.disconnect();
    }

    const int paramChoice = (busIndex >= 0 && busIndex < suite::texlink::kNumBuses) ? (busIndex + 1) : 0;
    setNormalizedParam (apvts_, fossil::param_ids::textureLinkBus, static_cast<float> (paramChoice));
}

void FossilAudioProcessor::connectHarmonyLinkBus (int busIndex, const char* customSuffix)
{
    {
        std::lock_guard<std::mutex> lock (linkControlMutex_);
        if (customSuffix != nullptr && customSuffix[0] != '\0')
        {
            harmCustomSuffix_   = customSuffix;
            harmCustomOverride_ = true;
        }
        else
        {
            harmCustomSuffix_.clear();
            harmCustomOverride_ = false;
        }

        if (busIndex >= 0 && busIndex < suite::harmony_link::kNumBuses)
            harmonyLink_.connect (busIndex, harmCustomOverride_ ? harmCustomSuffix_.c_str() : nullptr);
        else
            harmonyLink_.disconnect();
    }

    const int paramChoice = (busIndex >= 0 && busIndex < suite::harmony_link::kNumBuses) ? (busIndex + 1) : 0;
    setNormalizedParam (apvts_, fossil::param_ids::harmonyLinkBus, static_cast<float> (paramChoice));
}

void FossilAudioProcessor::syncLinkBusesFromParams()
{
    std::lock_guard<std::mutex> lock (linkControlMutex_);

    if (pTexLinkBus_ != nullptr)
    {
        const int busIdx = static_cast<int> (std::lround (pTexLinkBus_->load (std::memory_order_relaxed))) - 1;
        const char* suf  = (texCustomOverride_ && ! texCustomSuffix_.empty()) ? texCustomSuffix_.c_str() : nullptr;
        if (busIdx >= 0 && busIdx < suite::texlink::kNumBuses)
            textureLink_.connect (busIdx, suf);
        else
            textureLink_.disconnect();
    }

    if (pHarmLinkBus_ != nullptr)
    {
        const int busIdx = static_cast<int> (std::lround (pHarmLinkBus_->load (std::memory_order_relaxed))) - 1;
        const char* suf  = (harmCustomOverride_ && ! harmCustomSuffix_.empty()) ? harmCustomSuffix_.c_str() : nullptr;
        if (busIdx >= 0 && busIdx < suite::harmony_link::kNumBuses)
            harmonyLink_.connect (busIdx, suf);
        else
            harmonyLink_.disconnect();
    }
}

float FossilAudioProcessor::quantizeSemitoneToMask (float semi, uint16_t relativeIntervalMask) noexcept
{
    if (! std::isfinite (semi) || std::abs (semi) < 0.05f)
        return 0.0f;

    const uint16_t mask = static_cast<uint16_t> ((relativeIntervalMask & 0x0FFFu) | 1u);
    int bestQ = 0;
    float bestDist = 1.0e9f;

    for (int q = -24; q <= 24; ++q)
    {
        const unsigned pc = static_cast<unsigned> (((q % 12) + 12) % 12);
        if ((mask & (1u << pc)) == 0u)
            continue;

        const float dist = std::abs (static_cast<float> (q) - semi);
        if (dist < bestDist - 1.0e-5f || (std::abs (dist - bestDist) <= 1.0e-5f && std::abs (q) < std::abs (bestQ)))
        {
            bestDist = dist;
            bestQ = q;
        }
    }

    return static_cast<float> (bestQ);
}

void FossilAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate_ = std::max (8000.0, sampleRate);
    const int safeBlock = std::max (256, samplesPerBlock);

    hpss_.prepare (currentSampleRate_, safeBlock);
    strata_.prepare (currentSampleRate_, safeBlock);
    euclidGate_.prepare (currentSampleRate_);
    shimmer_.prepare (currentSampleRate_, safeBlock);

    setLatencySamples (hpss_.getLatencySamples());

    const auto allocSize = static_cast<size_t> (std::max (4096, safeBlock));
    bufTL_.assign (allocSize, 0.0f);
    bufTR_.assign (allocSize, 0.0f);
    bufHL_.assign (allocSize, 0.0f);
    bufHR_.assign (allocSize, 0.0f);
    bufNL_.assign (allocSize, 0.0f);
    bufNR_.assign (allocSize, 0.0f);
    wetL_.assign  (allocSize, 0.0f);
    wetR_.assign  (allocSize, 0.0f);

    momentarySlot_    = -1;
    sustainPedalDown_ = false;
    prepared_         = true;

    syncLinkBusesFromParams();

    // Restore any frozen snapshots loaded from state prior to prepareToPlay
    {
        const juce::ScopedLock sl (snapshotLock_);
        for (const auto& snap : pendingSnapshots_)
            strata_.restoreSnapshot (snap.slot, snap.left, snap.right, snap.source, &telemetry_);
        pendingSnapshots_.clear();
    }
}

void FossilAudioProcessor::releaseResources()
{
    hpss_.reset();
    euclidGate_.reset();
    shimmer_.reset();
}

bool FossilAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& outSet = layouts.getMainOutputChannelSet();
    const auto& inSet  = layouts.getMainInputChannelSet();
    if (outSet != juce::AudioChannelSet::stereo() && outSet != juce::AudioChannelSet::mono())
        return false;
    if (inSet != juce::AudioChannelSet::stereo() && inSet != juce::AudioChannelSet::mono())
        return false;
    return true;
}

void FossilAudioProcessor::readParameters (fossil::MasterParams& out) const noexcept
{
    out.hpssSep        = pHpssSep_->load (std::memory_order_relaxed);
    out.noiseThresh    = pNoiseThresh_->load (std::memory_order_relaxed);
    out.dryT           = pDryT_->load (std::memory_order_relaxed);
    out.dryH           = pDryH_->load (std::memory_order_relaxed);
    out.dryN           = pDryN_->load (std::memory_order_relaxed);
    out.strataBlend    = pStrataBlend_->load (std::memory_order_relaxed);
    out.shimmerSmear   = pShimmerSmear_->load (std::memory_order_relaxed);
    out.shimmerPitch   = pShimmerPitch_->load (std::memory_order_relaxed);
    out.outputGainDb   = pOutputGain_->load (std::memory_order_relaxed);
    out.crossfadePos   = pCrossfadePos_->load (std::memory_order_relaxed);
    out.crossfadeDepth = pCrossfadeDepth_->load (std::memory_order_relaxed);
    out.freezeMode     = static_cast<int> (std::lround (pFreezeMode_->load (std::memory_order_relaxed)));
    out.midiFreeze     = pMidiFreeze_->load (std::memory_order_relaxed) >= 0.5f;

    out.euclidEnable   = pEuclidEnable_->load (std::memory_order_relaxed) >= 0.5f;
    out.euclidSteps    = std::clamp (static_cast<int> (std::lround (pEuclidSteps_->load (std::memory_order_relaxed))), 1, 16);
    out.euclidHits     = std::clamp (static_cast<int> (std::lround (pEuclidHits_->load (std::memory_order_relaxed))), 0, out.euclidSteps);
    out.euclidRot      = std::clamp (static_cast<int> (std::lround (pEuclidRot_->load (std::memory_order_relaxed))), 0, std::max (0, out.euclidSteps - 1));
    out.euclidSmooth   = pEuclidSmooth_->load (std::memory_order_relaxed);
    out.euclidDepth    = pEuclidDepth_->load (std::memory_order_relaxed);
    out.euclidRate     = std::clamp (static_cast<int> (std::lround (pEuclidRate_->load (std::memory_order_relaxed))), 0, 4);
    out.internalBpm    = pInternalBpm_->load (std::memory_order_relaxed);

    out.textureLinkBus = std::clamp (static_cast<int> (std::lround (pTexLinkBus_->load (std::memory_order_relaxed))), 0, 8);
    out.vowelModDepth  = pVowelModDepth_->load (std::memory_order_relaxed);
    out.harmonyLinkBus = std::clamp (static_cast<int> (std::lround (pHarmLinkBus_->load (std::memory_order_relaxed))), 0, 8);
    out.harmQuantize   = pHarmQuantize_->load (std::memory_order_relaxed) >= 0.5f;

    for (int i = 0; i < fossil::kNumStrata; ++i)
    {
        const auto& lp = pLayers_[static_cast<size_t> (i)];
        auto& sp       = out.strata[static_cast<size_t> (i)];
        const int src  = std::clamp (static_cast<int> (std::lround (lp.source->load (std::memory_order_relaxed))), 0, 2);
        sp.source      = static_cast<fossil::StreamSource> (src);
        sp.gain        = lp.gain->load (std::memory_order_relaxed);
        sp.pan         = lp.pan->load (std::memory_order_relaxed);
        sp.width       = lp.width->load (std::memory_order_relaxed);
        sp.pitchSemi   = lp.pitch->load (std::memory_order_relaxed);
        sp.grainMs     = lp.grain->load (std::memory_order_relaxed);
        sp.drift       = lp.drift->load (std::memory_order_relaxed);
        sp.tilt        = lp.tilt->load (std::memory_order_relaxed);
        sp.mute        = lp.mute->load (std::memory_order_relaxed) >= 0.5f;
        sp.solo        = lp.solo->load (std::memory_order_relaxed) >= 0.5f;
        sp.euclidGated = lp.gate->load (std::memory_order_relaxed) >= 0.5f;
    }
}

void FossilAudioProcessor::handleMidiMessages (const juce::MidiBuffer& midiMessages,
                                               const fossil::MasterParams& params) noexcept
{
    if (! params.midiFreeze)
        return;

    const bool isMomentary = (params.freezeMode == 1);

    auto recordCapture = [this, isMomentary] (fossil::StreamSource src) noexcept
    {
        const int slot = strata_.captureStream (-1, src, &telemetry_);
        ++freezeCaptureSeq_;
        lastCaptureSrc_ = static_cast<uint8_t> (static_cast<int> (src));
        telemetry_.freezeCaptureSeq.store (freezeCaptureSeq_, std::memory_order_relaxed);
        telemetry_.lastCapturedSlot.store (slot, std::memory_order_relaxed);
        telemetry_.activeFreezeSource.store (static_cast<int> (src), std::memory_order_relaxed);
        if (isMomentary)
            momentarySlot_ = slot;
    };

    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();

        if (msg.isNoteOn())
        {
            const int note = msg.getNoteNumber();
            if (note == 48 || note == 60)      recordCapture (fossil::StreamSource::Transient);
            else if (note == 50 || note == 62) recordCapture (fossil::StreamSource::Tonal);
            else if (note == 52 || note == 64) recordCapture (fossil::StreamSource::Noise);
            else if (note == 53 || note == 65)
            {
                strata_.clearAll (&telemetry_);
                telemetry_.activeFreezeSource.store (-1, std::memory_order_relaxed);
                momentarySlot_ = -1;
            }
        }
        else if (msg.isNoteOff() && isMomentary)
        {
            const int note = msg.getNoteNumber();
            if ((note == 48 || note == 50 || note == 52 || note == 60 || note == 62 || note == 64) && momentarySlot_ >= 0)
            {
                strata_.clearSlot (momentarySlot_, &telemetry_);
                momentarySlot_ = -1;
                telemetry_.activeFreezeSource.store (-1, std::memory_order_relaxed);
            }
        }
        else if (msg.isController() && msg.getControllerNumber() == 64) // Sustain pedal
        {
            const bool down = msg.getControllerValue() >= 64;
            if (down && ! sustainPedalDown_)
            {
                sustainPedalDown_ = true;
                recordCapture (fossil::StreamSource::Tonal);
            }
            else if (! down && sustainPedalDown_)
            {
                sustainPedalDown_ = false;
                if (isMomentary && momentarySlot_ >= 0)
                {
                    strata_.clearSlot (momentarySlot_, &telemetry_);
                    momentarySlot_ = -1;
                    telemetry_.activeFreezeSource.store (-1, std::memory_order_relaxed);
                }
            }
        }
    }
}

void FossilAudioProcessor::drainCommands() noexcept
{
    fossil::FossilCommand cmd;
    while (commandQueue_.pop (cmd))
    {
        switch (cmd.type)
        {
            case fossil::CommandType::CaptureStream:
            {
                const int slot = strata_.captureStream (cmd.slotA, cmd.source, &telemetry_);
                ++freezeCaptureSeq_;
                lastCaptureSrc_ = static_cast<uint8_t> (static_cast<int> (cmd.source));
                telemetry_.freezeCaptureSeq.store (freezeCaptureSeq_, std::memory_order_relaxed);
                telemetry_.lastCapturedSlot.store (slot, std::memory_order_relaxed);
                telemetry_.activeFreezeSource.store (static_cast<int> (cmd.source), std::memory_order_relaxed);
                momentarySlot_ = slot;
                break;
            }
            case fossil::CommandType::ReleaseMomentary:
            {
                const int slot = (cmd.slotA >= 0) ? cmd.slotA : momentarySlot_;
                if (slot >= 0 && slot < fossil::kNumStrata)
                    strata_.clearSlot (slot, &telemetry_);
                momentarySlot_ = -1;
                telemetry_.activeFreezeSource.store (-1, std::memory_order_relaxed);
                break;
            }
            case fossil::CommandType::ClearSlot:
            {
                if (cmd.slotA >= 0 && cmd.slotA < fossil::kNumStrata)
                    strata_.clearSlot (cmd.slotA, &telemetry_);
                break;
            }
            case fossil::CommandType::ClearAll:
            {
                strata_.clearAll (&telemetry_);
                momentarySlot_ = -1;
                telemetry_.activeFreezeSource.store (-1, std::memory_order_relaxed);
                break;
            }
            case fossil::CommandType::SwapSlots:
            {
                strata_.swapSlots (cmd.slotA, cmd.slotB, &telemetry_);
                break;
            }
            case fossil::CommandType::None:
                break;
        }
    }
}

void FossilAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalSamples = buffer.getNumSamples();
    if (totalSamples <= 0)
        return;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, totalSamples);

    fossil::MasterParams params;
    readParameters (params);

    // Sync Euclidean gate with DAW host playhead
    bool   hostPlaying = false;
    double hostBpm     = static_cast<double> (params.internalBpm);
    double hostPpq     = 0.0;
    if (auto* ph = getPlayHead())
    {
        if (const auto pos = ph->getPosition())
        {
            hostPlaying = pos->getIsPlaying();
            if (const auto bpm = pos->getBpm(); bpm.hasValue() && *bpm > 1.0)
                hostBpm = *bpm;
            if (const auto ppq = pos->getPpqPosition(); ppq.hasValue())
                hostPpq = *ppq;
        }
    }

    euclidGate_.configure (params.euclidEnable, params.euclidSteps, params.euclidHits,
                           params.euclidRot, params.euclidSmooth, params.euclidDepth,
                           params.euclidRate, params.internalBpm);
    euclidGate_.syncBlock (hostPlaying, hostBpm, hostPpq);

    hpss_.setParameters (params.hpssSep, params.noiseThresh);

    // Process UI commands & MIDI triggers
    drainCommands();
    handleMidiMessages (midiMessages, params);

    // ========================================================================
    // Read lock-free SuiteTextureLink (VowelMorph -> Fossil)
    // ========================================================================
    fossil::dsp::StrataEngine::ExternalModulation extMod {};
    float shimmerBrightness   = 0.55f;
    float shimmerIntervalSemi = 12.0f;
    bool  vowelPeerAlive      = false;

    if (textureLink_.isConnected())
    {
        suite::texlink::VowelToFossilPayload v2f {};
        if (textureLink_.readVowel (v2f, vowelPeerAlive) && vowelPeerAlive)
        {
            const float slew = std::clamp (static_cast<float> (totalSamples) / static_cast<float> (std::max (1.0, currentSampleRate_ * 0.025)), 0.05f, 0.85f);
            const float vx   = std::isfinite (v2f.vowelX)            ? std::clamp (v2f.vowelX, 0.0f, 1.0f)            : 0.5f;
            const float vy   = std::isfinite (v2f.vowelY)            ? std::clamp (v2f.vowelY, 0.0f, 1.0f)            : 0.5f;
            const float vb   = std::isfinite (v2f.formantBrightness) ? std::clamp (v2f.formantBrightness, 0.0f, 1.0f) : 0.55f;
            const float vme  = std::isfinite (v2f.modEnvelope)       ? std::clamp (v2f.modEnvelope, 0.0f, 1.0f)       : 0.5f;

            smoothedVowelX_      += slew * (vx  - smoothedVowelX_);
            smoothedVowelY_      += slew * (vy  - smoothedVowelY_);
            smoothedVowelBright_ += slew * (vb  - smoothedVowelBright_);
            smoothedVowelModEnv_ += slew * (vme - smoothedVowelModEnv_);

            const float modD = std::clamp (params.vowelModDepth, 0.0f, 1.0f);
            if (modD > 0.001f)
            {
                // Vowel-driven Strata Crossfade & Tilt + Formant-Colored Shimmer Diffusion
                extMod.crossfadePosOffset  = ((smoothedVowelX_ - 0.5f) * 0.90f + (smoothedVowelModEnv_ - 0.5f) * 0.22f) * modD;
                extMod.crossfadeDepthBoost = 0.72f * modD;
                extMod.tiltOffset          = ((smoothedVowelBright_ - 0.5f) * 0.75f + (smoothedVowelY_ - 0.5f) * 0.30f) * modD;
                shimmerBrightness          = std::clamp (0.55f + (smoothedVowelBright_ - 0.5f) * 0.95f * modD, 0.05f, 0.98f);
            }

            telemetry_.vowelX.store          (smoothedVowelX_, std::memory_order_relaxed);
            telemetry_.vowelY.store          (smoothedVowelY_, std::memory_order_relaxed);
            telemetry_.vowelBrightness.store (smoothedVowelBright_, std::memory_order_relaxed);
            telemetry_.vowelModEnv.store     (smoothedVowelModEnv_, std::memory_order_relaxed);
            copyStringToAtomicChars (v2f.syllableLabel, telemetry_.vowelSyllableLabel);
            copyStringToAtomicChars (v2f.tractLabel,    telemetry_.vowelTractLabel);
        }
    }
    telemetry_.vowelPeerAlive.store (vowelPeerAlive, std::memory_order_relaxed);
    const float effCrossfade = std::clamp (params.crossfadePos + extMod.crossfadePosOffset, 0.0f, 1.0f);
    telemetry_.effectiveCrossfade.store (effCrossfade, std::memory_order_relaxed);

    // ========================================================================
    // Read lock-free SuiteHarmonyLink (SessionMate / HarmonyHUD -> Fossil)
    // ========================================================================
    bool harmonyAlive = false;
    if (harmonyLink_.isConnected())
    {
        suite::harmony_link::BandToHudPayload b2h {};
        suite::harmony_link::HudToBandPayload h2b {};
        bool bandAlive = false;
        bool hudAlive  = false;
        harmonyLink_.readBand (b2h, bandAlive);
        harmonyLink_.readHud  (h2b, hudAlive);
        harmonyAlive = bandAlive || hudAlive;

        if (harmonyAlive)
        {
            suite::harmony_link::LinkChord activeChord {};
            int8_t keyRootPc = 0;
            uint16_t scaleMask = 0u;

            if (bandAlive && b2h.activeChord.isValid())
            {
                activeChord = b2h.activeChord;
                keyRootPc   = b2h.keyRootPc;
                scaleMask   = suite::harmony_link::modePitchClassMask (
                    keyRootPc, b2h.keyIsMinor ? suite::harmony_link::LinkMode::Aeolian
                                              : suite::harmony_link::LinkMode::Ionian);
            }
            if (! activeChord.isValid() && hudAlive && h2b.liveChord.isValid())
            {
                activeChord = h2b.liveChord;
                keyRootPc   = h2b.keyRootPc;
                scaleMask   = (h2b.scalePitchClassMask != 0u)
                                  ? h2b.scalePitchClassMask
                                  : suite::harmony_link::modePitchClassMask (keyRootPc, h2b.activeMode);
            }
            else if (hudAlive && h2b.scalePitchClassMask != 0u)
            {
                scaleMask = h2b.scalePitchClassMask;
            }

            const uint16_t rawMask = activeChord.isValid() ? activeChord.pitchClassMask : scaleMask;
            const int8_t   rootPc  = activeChord.isValid() ? activeChord.rootPc : keyRootPc;

            telemetry_.activeHarmonyMask.store (rawMask, std::memory_order_relaxed);
            telemetry_.activeChordRootPc.store (static_cast<int> (rootPc), std::memory_order_relaxed);

            char chordStr[16] {};
            if (activeChord.isValid())
                suite::harmony_link::formatLinkChord (activeChord, chordStr, sizeof (chordStr));
            else if (rootPc >= 0 && rootPc < 12)
                std::snprintf (chordStr, sizeof (chordStr), "%s", suite::harmony_link::pitchClassName (rootPc));
            copyStringToAtomicChars (chordStr, telemetry_.harmonyChordLabel);

            if (params.harmQuantize && rawMask != 0u)
            {
                const int8_t invShift = static_cast<int8_t> ((12 - (static_cast<int> (rootPc) % 12 + 12) % 12) % 12);
                const uint16_t relMask = (rootPc >= 0 && rootPc < 12)
                                             ? suite::harmony_link::rotatePitchClassMask (rawMask, invShift)
                                             : rawMask;

                extMod.useQuantizedPitch = true;
                for (int s = 0; s < fossil::kNumStrata; ++s)
                {
                    extMod.quantizedPitchSemi[static_cast<size_t> (s)] =
                        quantizeSemitoneToMask (params.strata[static_cast<size_t> (s)].pitchSemi, relMask);
                }

                // Harmonically aware shimmer interval: +12st octave (or +7st fifth if P5 in chord & shimmerPitch moderate)
                const bool hasFifth = (relMask & (1u << 7u)) != 0u;
                shimmerIntervalSemi = (hasFifth && params.shimmerPitch > 0.15f && params.shimmerPitch < 0.45f) ? 7.0f : 12.0f;
            }
        }
    }
    telemetry_.harmonyPeerAlive.store (harmonyAlive, std::memory_order_relaxed);
    if (! harmonyAlive)
    {
        telemetry_.activeHarmonyMask.store (0u, std::memory_order_relaxed);
        telemetry_.activeChordRootPc.store (-1, std::memory_order_relaxed);
        copyStringToAtomicChars ("", telemetry_.harmonyChordLabel);
    }

    // ========================================================================
    // Core Audio Processing
    // ========================================================================
    const float targetOutGain = std::pow (10.0f, params.outputGainDb / 20.0f);
    const float smoothCoeff   = 1.0f - std::exp (-1.0f / static_cast<float> (std::max (1.0, currentSampleRate_ * 0.015)));

    float inPeakL  = 0.0f;
    float inPeakR  = 0.0f;
    float outPeakL = 0.0f;
    float outPeakR = 0.0f;

    const int maxChunk = static_cast<int> (bufTL_.size());
    if (maxChunk <= 0)
        return;

    float* ch0 = buffer.getWritePointer (0);
    float* ch1 = (numOut > 1) ? buffer.getWritePointer (1) : nullptr;
    const float* inCh0 = (numIn > 0) ? buffer.getReadPointer (0) : nullptr;
    const float* inCh1 = (numIn > 1) ? buffer.getReadPointer (1) : inCh0;

    int offset = 0;
    while (offset < totalSamples)
    {
        const int chunk = std::min (totalSamples - offset, maxChunk);
        const float* srcL = inCh0 ? (inCh0 + offset) : bufTL_.data();
        const float* srcR = inCh1 ? (inCh1 + offset) : srcL;

        for (int i = 0; i < chunk; ++i)
        {
            inPeakL = std::max (inPeakL, std::abs (clampFinite (srcL[i])));
            inPeakR = std::max (inPeakR, std::abs (clampFinite (srcR[i])));
        }

        // 1. Real-Time 3-Way STFT HPSS + Spectral-Flatness Decomposition
        hpss_.process (srcL, srcR,
                       bufTL_.data(), bufTR_.data(),
                       bufHL_.data(), bufHR_.data(),
                       bufNL_.data(), bufNR_.data(),
                       chunk);

        // 2. 4-Strata Granular Freeze + Euclidean Gate (+ Vowel/Harmony Mod)
        strata_.processBlock (bufTL_.data(), bufTR_.data(),
                              bufHL_.data(), bufHR_.data(),
                              bufNL_.data(), bufNR_.data(),
                              wetL_.data(),  wetR_.data(),
                              chunk, params, euclidGate_, &telemetry_, &extMod);

        // 3. Formant-Colored Shimmer Smear Feedback Diffusion on Strata Output
        shimmer_.processBlock (wetL_.data(), wetR_.data(), chunk,
                               params.shimmerSmear, params.shimmerPitch,
                               shimmerBrightness, shimmerIntervalSemi);

        // 4. Mix Independent Dry Pass (T / H / N) + Strata Blend + Safety Limiter
        for (int i = 0; i < chunk; ++i)
        {
            smoothedDryT_        += smoothCoeff * (params.dryT        - smoothedDryT_);
            smoothedDryH_        += smoothCoeff * (params.dryH        - smoothedDryH_);
            smoothedDryN_        += smoothCoeff * (params.dryN        - smoothedDryN_);
            smoothedStrataBlend_ += smoothCoeff * (params.strataBlend - smoothedStrataBlend_);
            smoothedOutGain_     += smoothCoeff * (targetOutGain      - smoothedOutGain_);

            const float dryL = bufTL_[static_cast<size_t> (i)] * smoothedDryT_
                             + bufHL_[static_cast<size_t> (i)] * smoothedDryH_
                             + bufNL_[static_cast<size_t> (i)] * smoothedDryN_;
            const float dryR = bufTR_[static_cast<size_t> (i)] * smoothedDryT_
                             + bufHR_[static_cast<size_t> (i)] * smoothedDryH_
                             + bufNR_[static_cast<size_t> (i)] * smoothedDryN_;

            const float mixL = softLimit ((dryL + wetL_[static_cast<size_t> (i)] * smoothedStrataBlend_) * smoothedOutGain_);
            const float mixR = softLimit ((dryR + wetR_[static_cast<size_t> (i)] * smoothedStrataBlend_) * smoothedOutGain_);

            if (ch1 != nullptr)
            {
                ch0[offset + i] = mixL;
                ch1[offset + i] = mixR;
            }
            else
            {
                ch0[offset + i] = 0.5f * (mixL + mixR);
            }

            outPeakL = std::max (outPeakL, std::abs (mixL));
            outPeakR = std::max (outPeakR, std::abs (mixR));
        }

        offset += chunk;
    }

    // ========================================================================
    // Update Lock-Free UI Telemetry & Publish Fossil -> VowelMorph Payload
    // ========================================================================
    float eT = 0.0f, eH = 0.0f, eN = 0.0f;
    hpss_.getLastEnergies (eT, eH, eN);
    telemetry_.liveEnergyT.store (eT, std::memory_order_relaxed);
    telemetry_.liveEnergyH.store (eH, std::memory_order_relaxed);
    telemetry_.liveEnergyN.store (eN, std::memory_order_relaxed);
    telemetry_.inputPeakL.store  (inPeakL, std::memory_order_relaxed);
    telemetry_.inputPeakR.store  (inPeakR, std::memory_order_relaxed);
    telemetry_.outputPeakL.store (outPeakL, std::memory_order_relaxed);
    telemetry_.outputPeakR.store (outPeakR, std::memory_order_relaxed);
    telemetry_.currentBpm.store  (euclidGate_.getActiveBpm(), std::memory_order_relaxed);
    telemetry_.hostSynced.store  (euclidGate_.isHostSynced(), std::memory_order_relaxed);
    telemetry_.currentEuclidStep.store (euclidGate_.getCurrentStep(), std::memory_order_relaxed);
    telemetry_.currentEuclidEnv.store  (euclidGate_.getCurrentEnvelope(), std::memory_order_relaxed);
    telemetry_.euclidPatternMask.store (euclidGate_.getPatternMask(), std::memory_order_relaxed);

    // Fast transient trigger spike envelope for VowelMorph consonant accent
    const float riseT = std::max (0.0f, eT - prevEnergyT_);
    prevEnergyT_ = prevEnergyT_ * 0.72f + eT * 0.28f;
    const float trigSample = std::clamp (riseT * 4.5f + eT * 0.5f, 0.0f, 1.0f);
    transientTrigEnv_ = (trigSample > transientTrigEnv_) ? trigSample : (transientTrigEnv_ * 0.78f);

    if (textureLink_.isConnected())
    {
        suite::texlink::FossilToVowelPayload f2v {};
        const uint8_t frozenMask  = strata_.getFrozenMask();
        const uint8_t frozenCount = strata_.getFrozenCount();

        uint32_t flags = 0u;
        if (params.euclidEnable)       flags |= suite::texlink::kFossilEuclidOn;
        if (euclidGate_.isHostSynced()) flags |= suite::texlink::kFossilHostSynced;
        if (frozenCount > 0u)          flags |= suite::texlink::kFossilAnyFrozen;
        if (params.freezeMode == 1)    flags |= suite::texlink::kFossilMomentary;
        if (harmonyAlive)              flags |= suite::texlink::kFossilHarmonyLinked;

        f2v.flags             = flags;
        f2v.energyT           = std::clamp (eT, 0.0f, 1.0f);
        f2v.energyH           = std::clamp (eH, 0.0f, 1.0f);
        f2v.energyN           = std::clamp (eN, 0.0f, 1.0f);
        f2v.transientTrigger  = std::clamp (transientTrigEnv_, 0.0f, 1.0f);

        f2v.bpm               = euclidGate_.getActiveBpm();
        f2v.ppqPosition       = hostPpq;

        f2v.euclidPatternMask = euclidGate_.getPatternMask();
        f2v.euclidStepSeq     = euclidGate_.getStepSeq();
        f2v.euclidHitSeq      = euclidGate_.getHitSeq();
        f2v.euclidEnvelope    = euclidGate_.getCurrentEnvelope();

        f2v.euclidStep        = static_cast<uint8_t> (std::clamp (euclidGate_.getCurrentStep(), 0, 15));
        f2v.euclidSteps       = static_cast<uint8_t> (std::clamp (params.euclidSteps, 1, 16));
        f2v.euclidHits        = static_cast<uint8_t> (std::clamp (params.euclidHits, 0, 16));
        f2v.euclidActiveHit   = euclidGate_.isCurrentStepHit() ? 1u : 0u;

        f2v.freezeCaptureSeq  = freezeCaptureSeq_;
        f2v.lastCaptureSource = lastCaptureSrc_;
        f2v.frozenStrataMask  = frozenMask;
        f2v.frozenStrataCount = frozenCount;

        f2v.strataEnergy      = std::clamp (strata_.getLastCombinedEnergy(), 0.0f, 1.0f);
        f2v.crossfadePos      = effCrossfade;
        f2v.shimmerSmear      = params.shimmerSmear;
        f2v.dominantPitchSemi = strata_.getDominantPitchSemi (params);

        const auto& levels = strata_.getLastSlotLevels();
        for (int s = 0; s < fossil::kNumStrata; ++s)
            f2v.strataLevels[s] = std::clamp (levels[static_cast<size_t> (s)], 0.0f, 1.0f);

        textureLink_.publishFossil (f2v);
    }
}

void FossilAudioProcessor::triggerFreeze (fossil::StreamSource source, int targetSlot)
{
    fossil::FossilCommand cmd;
    cmd.type   = fossil::CommandType::CaptureStream;
    cmd.slotA  = targetSlot;
    cmd.source = source;
    commandQueue_.push (cmd);
}

void FossilAudioProcessor::releaseMomentaryFreeze (int targetSlot)
{
    fossil::FossilCommand cmd;
    cmd.type  = fossil::CommandType::ReleaseMomentary;
    cmd.slotA = targetSlot;
    commandQueue_.push (cmd);
}

void FossilAudioProcessor::clearLayer (int slot)
{
    fossil::FossilCommand cmd;
    cmd.type  = fossil::CommandType::ClearSlot;
    cmd.slotA = slot;
    commandQueue_.push (cmd);
}

void FossilAudioProcessor::clearAllLayers()
{
    fossil::FossilCommand cmd;
    cmd.type = fossil::CommandType::ClearAll;
    commandQueue_.push (cmd);
}

void FossilAudioProcessor::swapLayers (int slotA, int slotB)
{
    if (slotA < 0 || slotA >= fossil::kNumStrata || slotB < 0 || slotB >= fossil::kNumStrata || slotA == slotB)
        return;

    auto swapParam = [this] (const char* idA, const char* idB)
    {
        auto* pA = apvts_.getParameter (idA);
        auto* pB = apvts_.getParameter (idB);
        if (pA && pB)
        {
            const float vA = pA->getValue();
            const float vB = pB->getValue();
            pA->setValueNotifyingHost (vB);
            pB->setValueNotifyingHost (vA);
        }
    };

    using namespace fossil::param_ids;
    const auto a = static_cast<size_t> (slotA);
    const auto b = static_cast<size_t> (slotB);
    swapParam (layerSource[a], layerSource[b]);
    swapParam (layerGain[a],   layerGain[b]);
    swapParam (layerPan[a],    layerPan[b]);
    swapParam (layerWidth[a],  layerWidth[b]);
    swapParam (layerPitch[a],  layerPitch[b]);
    swapParam (layerGrain[a],  layerGrain[b]);
    swapParam (layerDrift[a],  layerDrift[b]);
    swapParam (layerTilt[a],   layerTilt[b]);
    swapParam (layerMute[a],   layerMute[b]);
    swapParam (layerSolo[a],   layerSolo[b]);
    swapParam (layerGate[a],   layerGate[b]);

    fossil::FossilCommand cmd;
    cmd.type  = fossil::CommandType::SwapSlots;
    cmd.slotA = slotA;
    cmd.slotB = slotB;
    commandQueue_.push (cmd);
}

void FossilAudioProcessor::loadFactoryPreset (int index)
{
    const auto& presets = fossil::getFactoryPresets();
    if (presets.empty())
        return;

    const int safeIdx = std::clamp (index, 0, static_cast<int> (presets.size()) - 1);
    currentProgram_ = safeIdx;
    const auto& p = presets[static_cast<size_t> (safeIdx)].params;

    using namespace fossil::param_ids;
    setNormalizedParam (apvts_, hpssSep,        p.hpssSep);
    setNormalizedParam (apvts_, noiseThresh,    p.noiseThresh);
    setNormalizedParam (apvts_, dryT,           p.dryT);
    setNormalizedParam (apvts_, dryH,           p.dryH);
    setNormalizedParam (apvts_, dryN,           p.dryN);
    setNormalizedParam (apvts_, strataBlend,    p.strataBlend);
    setNormalizedParam (apvts_, shimmerSmear,   p.shimmerSmear);
    setNormalizedParam (apvts_, shimmerPitch,   p.shimmerPitch);
    setNormalizedParam (apvts_, outputGain,     p.outputGainDb);
    setNormalizedParam (apvts_, crossfadePos,   p.crossfadePos);
    setNormalizedParam (apvts_, crossfadeDepth, p.crossfadeDepth);
    setNormalizedParam (apvts_, freezeMode,     static_cast<float> (p.freezeMode));
    setNormalizedParam (apvts_, midiFreeze,     p.midiFreeze ? 1.0f : 0.0f);

    setNormalizedParam (apvts_, euclidEnable,   p.euclidEnable ? 1.0f : 0.0f);
    setNormalizedParam (apvts_, euclidSteps,    static_cast<float> (p.euclidSteps));
    setNormalizedParam (apvts_, euclidHits,     static_cast<float> (p.euclidHits));
    setNormalizedParam (apvts_, euclidRot,      static_cast<float> (p.euclidRot));
    setNormalizedParam (apvts_, euclidSmooth,   p.euclidSmooth);
    setNormalizedParam (apvts_, euclidDepth,    p.euclidDepth);
    setNormalizedParam (apvts_, euclidRate,     static_cast<float> (p.euclidRate));
    setNormalizedParam (apvts_, internalBpm,    p.internalBpm);

    for (int i = 0; i < fossil::kNumStrata; ++i)
    {
        const auto idx = static_cast<size_t> (i);
        const auto& sp = p.strata[idx];
        setNormalizedParam (apvts_, layerSource[idx], static_cast<float> (static_cast<int> (sp.source)));
        setNormalizedParam (apvts_, layerGain[idx],   sp.gain);
        setNormalizedParam (apvts_, layerPan[idx],    sp.pan);
        setNormalizedParam (apvts_, layerWidth[idx],  sp.width);
        setNormalizedParam (apvts_, layerPitch[idx],  sp.pitchSemi);
        setNormalizedParam (apvts_, layerGrain[idx],  sp.grainMs);
        setNormalizedParam (apvts_, layerDrift[idx],  sp.drift);
        setNormalizedParam (apvts_, layerTilt[idx],   sp.tilt);
        setNormalizedParam (apvts_, layerMute[idx],   sp.mute ? 1.0f : 0.0f);
        setNormalizedParam (apvts_, layerSolo[idx],   sp.solo ? 1.0f : 0.0f);
        setNormalizedParam (apvts_, layerGate[idx],   sp.euclidGated ? 1.0f : 0.0f);
    }
}

int FossilAudioProcessor::getNumPrograms()
{
    return static_cast<int> (fossil::getFactoryPresets().size());
}

int FossilAudioProcessor::getCurrentProgram()
{
    return currentProgram_;
}

void FossilAudioProcessor::setCurrentProgram (int index)
{
    loadFactoryPreset (index);
}

const juce::String FossilAudioProcessor::getProgramName (int index)
{
    const auto& presets = fossil::getFactoryPresets();
    if (index >= 0 && index < static_cast<int> (presets.size()))
        return presets[static_cast<size_t> (index)].name;
    return {};
}

void FossilAudioProcessor::changeProgramName (int, const juce::String&) {}

void FossilAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts_.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (! xml)
        return;

    xml->setAttribute ("currentProgram", currentProgram_);

    // Persist any frozen strata snapshots compactly as Base64 int16 PCM
    auto* strataXml = xml->createNewChildElement ("FrozenStrata");
    for (int i = 0; i < fossil::kNumStrata; ++i)
    {
        std::vector<float> snapL, snapR;
        fossil::StreamSource src = fossil::StreamSource::Tonal;
        if (strata_.copySnapshotOut (i, snapL, snapR, src) && ! snapL.empty())
        {
            const size_t n = std::min ({ snapL.size(), snapR.size(), static_cast<size_t> (96000) });
            std::vector<int16_t> pcm (n * 2);
            for (size_t k = 0; k < n; ++k)
            {
                pcm[2 * k]     = static_cast<int16_t> (std::clamp (snapL[k], -1.0f, 1.0f) * 32767.0f);
                pcm[2 * k + 1] = static_cast<int16_t> (std::clamp (snapR[k], -1.0f, 1.0f) * 32767.0f);
            }
            juce::MemoryBlock mb (pcm.data(), pcm.size() * sizeof (int16_t));
            auto* layerXml = strataXml->createNewChildElement ("Layer");
            layerXml->setAttribute ("slot", i);
            layerXml->setAttribute ("source", static_cast<int> (src));
            layerXml->setAttribute ("samples", static_cast<int> (n));
            layerXml->setAttribute ("pcm16", mb.toBase64Encoding());
        }
    }

    copyXmlToBinary (*xml, destData);
}

void FossilAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (! xmlState)
        return;

    if (xmlState->hasTagName (apvts_.state.getType()))
    {
        currentProgram_ = xmlState->getIntAttribute ("currentProgram", 0);

        std::vector<PendingSnapshot> loaded;
        if (auto* strataXml = xmlState->getChildByName ("FrozenStrata"))
        {
            for (auto* layerXml : strataXml->getChildWithTagNameIterator ("Layer"))
            {
                const int slot = layerXml->getIntAttribute ("slot", -1);
                const int srcInt = std::clamp (layerXml->getIntAttribute ("source", 1), 0, 2);
                const int nSamples = layerXml->getIntAttribute ("samples", 0);
                const juce::String b64 = layerXml->getStringAttribute ("pcm16");

                if (slot >= 0 && slot < fossil::kNumStrata && nSamples > 0 && b64.isNotEmpty())
                {
                    juce::MemoryBlock mb;
                    if (mb.fromBase64Encoding (b64) && mb.getSize() >= static_cast<size_t> (nSamples) * 2 * sizeof (int16_t))
                    {
                        const auto* pcm = static_cast<const int16_t*> (mb.getData());
                        PendingSnapshot snap;
                        snap.slot = slot;
                        snap.source = static_cast<fossil::StreamSource> (srcInt);
                        snap.left.resize (static_cast<size_t> (nSamples));
                        snap.right.resize (static_cast<size_t> (nSamples));
                        constexpr float inv = 1.0f / 32767.0f;
                        for (int k = 0; k < nSamples; ++k)
                        {
                            snap.left[static_cast<size_t> (k)]  = static_cast<float> (pcm[2 * k]) * inv;
                            snap.right[static_cast<size_t> (k)] = static_cast<float> (pcm[2 * k + 1]) * inv;
                        }
                        loaded.push_back (std::move (snap));
                    }
                }
            }
            xmlState->removeChildElement (strataXml, true);
        }

        apvts_.replaceState (juce::ValueTree::fromXml (*xmlState));
        syncLinkBusesFromParams();

        if (prepared_)
        {
            strata_.clearAll (&telemetry_);
            for (const auto& snap : loaded)
                strata_.restoreSnapshot (snap.slot, snap.left, snap.right, snap.source, &telemetry_);
        }
        else
        {
            const juce::ScopedLock sl (snapshotLock_);
            pendingSnapshots_ = std::move (loaded);
        }
    }
}

juce::AudioProcessorEditor* FossilAudioProcessor::createEditor()
{
    return new FossilAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FossilAudioProcessor();
}
