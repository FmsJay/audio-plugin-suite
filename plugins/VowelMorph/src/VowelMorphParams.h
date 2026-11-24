#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <cstdint>

namespace vowelmorph {

// ============================================================================
// Core Constants & Enumerations
// ============================================================================
inline constexpr int kNumFormants = 5;
inline constexpr int kNumPhonemes = 10;
inline constexpr int kNumTracts   = 8;
inline constexpr int kNumSeqSteps = 8;
inline constexpr int kNumPresets  = 18;

enum class PhonemeId : int {
    EE = 0, // /i/  "beet"
    IH = 1, // /ɪ/  "bit"
    EH = 2, // /ɛ/  "bet"
    AE = 3, // /æ/  "bat"
    AH = 4, // /ɑ/  "father"
    AW = 5, // /ɔ/  "bought"
    OH = 6, // /o/  "boat"
    OO = 7, // /u/  "boot"
    UH = 8, // /ʌ/  "but"
    MM = 9  // /m/  closed nasal hum
};

enum class TractProfile : int {
    Bass          = 0,
    Tenor         = 1,
    AltoChoir     = 2,
    Soprano       = 3,
    Child         = 4,
    WhisperAir    = 5,
    TalkBoxSynth  = 6,
    AlienGiant    = 7
};

enum class ModMode : int {
    EnvFollower = 0,
    Lfo         = 1,
    StepSeq     = 2,
    MidiTrigger = 3
};

enum class LfoShape : int {
    Sine     = 0,
    Triangle = 1,
    Saw      = 2,
    SampleHold = 3
};

enum class SeqRate : int {
    Quarter      = 0, // 1/4
    Eighth       = 1, // 1/8
    EighthTrip   = 2, // 1/8T
    Sixteenth    = 3, // 1/16
    SixteenthTrp = 4, // 1/16T
    ThirtySecond = 5, // 1/32
    Half         = 6  // 1/2
};

// Step value: 0..9 = PhonemeId, 10 = Rest (closed/attenuated hold)
inline constexpr int kStepRest = 10;

inline constexpr const char* phonemeCode(int idx) noexcept {
    constexpr const char* kCodes[kNumPhonemes + 1] = {
        "EE", "IH", "EH", "AE", "AH", "AW", "OH", "OO", "UH", "MM", ".."
    };
    if (idx < 0 || idx > kStepRest) return "AH";
    return kCodes[idx];
}

inline constexpr const char* phonemeIpa(int idx) noexcept {
    constexpr const char* kIpa[kNumPhonemes] = {
        "/i/", "/I/", "/E/", "/ae/", "/a/", "/c/", "/o/", "/u/", "/^/", "/m/"
    };
    if (idx < 0 || idx >= kNumPhonemes) return "/a/";
    return kIpa[idx];
}

inline constexpr const char* tractName(int idx) noexcept {
    constexpr const char* kNames[kNumTracts] = {
        "Bass",
        "Tenor",
        "Alto Choir",
        "Soprano",
        "Child",
        "Whisper Air",
        "Talk-Box Synth",
        "Alien / Giant"
    };
    if (idx < 0 || idx >= kNumTracts) return "Alto Choir";
    return kNames[idx];
}

inline constexpr const char* modModeName(int idx) noexcept {
    constexpr const char* kNames[4] = { "Env", "LFO", "StepSeq", "MIDI" };
    if (idx < 0 || idx >= 4) return "Env";
    return kNames[idx];
}

inline constexpr const char* seqRateName(int idx) noexcept {
    constexpr const char* kNames[7] = { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/2" };
    if (idx < 0 || idx >= 7) return "1/8";
    return kNames[idx];
}

inline constexpr double seqRateBeatsPerStep(int idx) noexcept {
    constexpr double kBeats[7] = {
        1.0,        // 1/4
        0.5,        // 1/8
        1.0 / 3.0,  // 1/8T
        0.25,       // 1/16
        1.0 / 6.0,  // 1/16T
        0.125,      // 1/32
        2.0         // 1/2
    };
    if (idx < 0 || idx >= 7) return 0.5;
    return kBeats[idx];
}

// ============================================================================
// APVTS Parameter IDs
// ============================================================================
namespace params {
    // Core Vocal Tract & Vowel XY
    inline constexpr const char* vowelX          = "vowel_x";          // 0..1 (Front -> Back)
    inline constexpr const char* vowelY          = "vowel_y";          // 0..1 (Open -> Close)
    inline constexpr const char* tractProfile    = "tract_profile";    // 0..7
    inline constexpr const char* throatSize      = "throat_size";      // -50..+50 %
    inline constexpr const char* jawOpen         = "jaw_open";         // 0..100 %
    inline constexpr const char* resonance       = "resonance";        // 0..100 %
    inline constexpr const char* breathAir       = "breath_air";       // 0..100 %
    inline constexpr const char* choirWidth      = "choir_width";      // 0..100 %
    inline constexpr const char* drive           = "drive";            // 0..100 %
    inline constexpr const char* mix             = "mix";              // 0..100 %
    inline constexpr const char* outputDb        = "output_db";        // -18..+12 dB

    // Modulation Engine
    inline constexpr const char* modMode         = "mod_mode";         // 0=Env, 1=LFO, 2=StepSeq, 3=MIDI
    inline constexpr const char* modDepth        = "mod_depth";        // -100..+100 %
    inline constexpr const char* envAttackMs     = "env_attack_ms";    // 1..150 ms
    inline constexpr const char* envReleaseMs    = "env_release_ms";   // 15..800 ms
    inline constexpr const char* lfoRateHz       = "lfo_rate_hz";      // 0.05..16 Hz (or synced)
    inline constexpr const char* lfoSync         = "lfo_sync";         // bool
    inline constexpr const char* lfoDiv          = "lfo_div";          // 0..6 (same as SeqRate)
    inline constexpr const char* lfoShape        = "lfo_shape";        // 0..3

    // 8-Step Syllable Sequencer
    inline constexpr const char* seqRate         = "seq_rate";         // 0..6
    inline constexpr const char* seqGlide        = "seq_glide";        // 0..100 %
    inline constexpr const char* consonantDip    = "consonant_dip";    // 0..100 %
    inline constexpr const char* seqStepIds[kNumSeqSteps] = {
        "seq_step_0", "seq_step_1", "seq_step_2", "seq_step_3",
        "seq_step_4", "seq_step_5", "seq_step_6", "seq_step_7"
    };
    inline constexpr const char* seqAccentIds[kNumSeqSteps] = {
        "seq_accent_0", "seq_accent_1", "seq_accent_2", "seq_accent_3",
        "seq_accent_4", "seq_accent_5", "seq_accent_6", "seq_accent_7"
    };

    // SuiteTextureLink (Fossil <-> VowelMorph)
    inline constexpr const char* textureBus         = "texture_bus";          // 0=Off, 1..8=A..H
    inline constexpr const char* linkEuclidSync     = "link_euclid_sync";     // bool
    inline constexpr const char* linkNoiseBreath    = "link_noise_breath";    // bool
    inline constexpr const char* linkTransientJaw   = "link_transient_jaw";   // bool
    inline constexpr const char* linkFreezeSweep    = "link_freeze_sweep";    // bool

    // SuiteHarmonyLink (Read-Only SessionMate / HarmonyHUD)
    inline constexpr const char* harmonyBus         = "harmony_bus";          // 0=Off, 1..8=A..H
    inline constexpr const char* harmonyChoirDrone  = "harmony_choir_drone";  // 0..100 %
    inline constexpr const char* harmonySectionLift = "harmony_section_lift"; // bool
} // namespace params

// ============================================================================
// Live Audio -> UI Telemetry Snapshot (Lock-Free Copyable POD)
// ============================================================================
struct TelemetrySnapshot {
    float inputLevelL  = 0.0f;
    float inputLevelR  = 0.0f;
    float outputLevelL = 0.0f;
    float outputLevelR = 0.0f;

    float baseVowelX   = 0.5f;
    float baseVowelY   = 0.5f;
    float liveVowelX   = 0.5f;
    float liveVowelY   = 0.5f;
    float liveJawOpen  = 0.5f;
    float liveThroat   = 0.0f;
    float liveResonance = 0.5f;
    float liveBreath   = 0.0f;
    float modValue     = 0.0f;
    float envValue     = 0.0f;
    float dipAmount    = 0.0f;

    std::array<float, kNumFormants> formantFreqHz { 500.0f, 1400.0f, 2500.0f, 3500.0f, 4500.0f };
    std::array<float, kNumFormants> formantQ      { 6.0f, 8.0f, 10.0f, 12.0f, 12.0f };
    std::array<float, kNumFormants> formantGain   { 1.0f, 0.7f, 0.45f, 0.25f, 0.15f };

    int   activeStep      = 0;
    int   activePhoneme   = static_cast<int>(PhonemeId::AH);
    int   tractProfile    = static_cast<int>(TractProfile::AltoChoir);
    int   modMode         = static_cast<int>(ModMode::EnvFollower);
    bool  euclidLocked    = false;

    // TextureLink (Fossil peer) telemetry
    bool  fossilConnected = false;
    float fossilTransient = 0.0f;
    float fossilTonal     = 0.0f;
    float fossilNoise     = 0.0f;
    float fossilGateEnv   = 0.0f;
    int   fossilEuclidStep  = 0;
    int   fossilEuclidSteps = 16;
    int   fossilEuclidHits  = 5;
    int   fossilStrataCount = 0;

    // HarmonyLink (SessionMate / HarmonyHUD peer) telemetry
    bool  harmonyConnected = false;
    int   harmonyRootPc    = -1;
    char  harmonyChordLabel[20]   { "N.C." };
    char  harmonySectionLabel[16] { "" };
};

// ============================================================================
// Factory Presets (18 Curated Musical Presets)
// ============================================================================
struct FactoryPreset {
    const char* name;
    const char* category;
    int   tractProfile;
    float vowelX;
    float vowelY;
    float throatSize;
    float jawOpen;
    float resonance;
    float breathAir;
    float choirWidth;
    float drive;
    float mix;
    int   modMode;
    float modDepth;
    int   seqRate;
    float seqGlide;
    float consonantDip;
    std::array<int, kNumSeqSteps> steps;
    std::array<int, kNumSeqSteps> accents;
    bool  euclidSync;
    float harmonyDrone;
};

inline const std::array<FactoryPreset, kNumPresets>& getFactoryPresets() noexcept {
    static const std::array<FactoryPreset, kNumPresets> kPresets {{
        { "Warm Alto Choir Pad", "Choir",
          2, 0.42f, 0.35f,  2.0f, 68.0f, 54.0f, 22.0f, 65.0f, 14.0f, 72.0f,
          1, 32.0f, 6, 65.0f, 15.0f,
          { 7, 4, 0, 6, 4, 7, 9, 6 }, { 1, 0, 0, 0, 1, 0, 0, 0 }, false, 35.0f },

        { "Talking Neuro Bass", "Bass",
          0, 0.25f, 0.52f, -14.0f, 75.0f, 82.0f, 8.0f, 25.0f, 58.0f, 88.0f,
          0, 72.0f, 3, 28.0f, 42.0f,
          { 7, 3, 0, 4, 7, 0, 3, 9 }, { 1, 0, 1, 0, 1, 0, 1, 0 }, false, 0.0f },

        { "Funky Clav Auto-Wah", "Funk",
          6, 0.32f, 0.44f,  6.0f, 62.0f, 76.0f, 5.0f, 18.0f, 36.0f, 80.0f,
          0, 82.0f, 3, 20.0f, 30.0f,
          { 4, 0, 2, 7, 4, 0, 3, 6 }, { 1, 0, 0, 1, 0, 1, 0, 0 }, false, 0.0f },

        { "Whispering Ghost", "Ambient",
          5, 0.60f, 0.72f, 12.0f, 45.0f, 48.0f, 78.0f, 85.0f,  8.0f, 84.0f,
          1, 45.0f, 0, 80.0f, 12.0f,
          { 7, 6, 4, 9, 0, 1, 7, 10 }, { 0, 0, 1, 0, 0, 0, 1, 0 }, false, 25.0f },

        { "Euclidean Cyber Choir", "Linked",
          2, 0.50f, 0.50f,  0.0f, 70.0f, 68.0f, 28.0f, 74.0f, 22.0f, 85.0f,
          2, 85.0f, 3, 38.0f, 52.0f,
          { 7, 4, 0, 10, 6, 4, 9, 0 }, { 1, 0, 1, 0, 1, 0, 0, 1 }, true, 40.0f },

        { "Fossil Shimmer Companion", "Linked",
          3, 0.58f, 0.40f,  8.0f, 66.0f, 58.0f, 42.0f, 88.0f, 10.0f, 78.0f,
          2, 65.0f, 1, 72.0f, 24.0f,
          { 7, 6, 4, 0, 1, 4, 7, 9 }, { 1, 0, 0, 1, 0, 0, 1, 0 }, true, 48.0f },

        { "Cathedral Soprano Halo", "Choir",
          3, 0.36f, 0.65f, 14.0f, 74.0f, 62.0f, 30.0f, 80.0f, 10.0f, 75.0f,
          1, 28.0f, 6, 78.0f, 10.0f,
          { 0, 1, 4, 6, 7, 6, 4, 0 }, { 1, 0, 0, 0, 1, 0, 0, 0 }, false, 45.0f },

        { "Deep Gregorian Bass", "Choir",
          0, 0.72f, 0.42f, -22.0f, 58.0f, 64.0f, 18.0f, 55.0f, 20.0f, 82.0f,
          2, 55.0f, 6, 74.0f, 22.0f,
          { 7, 6, 5, 4, 6, 7, 9, 9 }, { 1, 0, 0, 1, 0, 0, 0, 0 }, false, 30.0f },

        { "Daft Talk-Box Lead", "Synth",
          6, 0.28f, 0.58f,  0.0f, 76.0f, 86.0f, 6.0f, 22.0f, 64.0f, 92.0f,
          2, 90.0f, 1, 24.0f, 58.0f,
          { 4, 0, 7, 4, 2, 0, 6, 9 }, { 1, 1, 0, 1, 0, 1, 0, 0 }, false, 0.0f },

        { "Tenor Soul Swell", "Vocal",
          1, 0.46f, 0.34f, -4.0f, 72.0f, 56.0f, 20.0f, 45.0f, 18.0f, 68.0f,
          0, 58.0f, 1, 48.0f, 20.0f,
          { 4, 6, 7, 4, 2, 0, 6, 4 }, { 1, 0, 0, 0, 1, 0, 0, 0 }, false, 20.0f },

        { "Child Music-Box Choir", "Choir",
          4, 0.30f, 0.62f, 20.0f, 64.0f, 58.0f, 26.0f, 70.0f,  6.0f, 74.0f,
          2, 70.0f, 1, 40.0f, 34.0f,
          { 0, 2, 4, 7, 0, 1, 6, 9 }, { 1, 0, 1, 0, 1, 0, 0, 0 }, false, 32.0f },

        { "Alien Throat Drone", "FX",
          7, 0.78f, 0.66f, -34.0f, 48.0f, 88.0f, 36.0f, 92.0f, 46.0f, 90.0f,
          1, 68.0f, 0, 64.0f, 44.0f,
          { 9, 7, 8, 5, 3, 0, 9, 7 }, { 1, 0, 0, 1, 0, 0, 1, 0 }, false, 15.0f },

        { "Rhythmic 16th Stutter Vox", "Sequencer",
          1, 0.40f, 0.50f,  4.0f, 70.0f, 72.0f, 16.0f, 40.0f, 30.0f, 86.0f,
          2, 92.0f, 3, 18.0f, 68.0f,
          { 4, 10, 0, 7, 10, 2, 6, 9 }, { 1, 0, 1, 0, 0, 1, 0, 1 }, false, 0.0f },

        { "Subtle Vocal Polish", "Mix",
          2, 0.45f, 0.42f,  0.0f, 60.0f, 34.0f, 14.0f, 30.0f,  8.0f, 32.0f,
          0, 28.0f, 1, 50.0f, 10.0f,
          { 4, 2, 0, 6, 4, 2, 0, 6 }, { 0, 0, 0, 0, 0, 0, 0, 0 }, false, 12.0f },

        { "Breathy Flute Pad", "Ambient",
          5, 0.74f, 0.76f, 10.0f, 38.0f, 64.0f, 66.0f, 76.0f,  6.0f, 80.0f,
          1, 36.0f, 0, 75.0f, 12.0f,
          { 7, 6, 7, 9, 0, 7, 6, 7 }, { 0, 0, 0, 0, 0, 0, 0, 0 }, false, 38.0f },

        { "Acid Vowel Squelch", "Bass",
          6, 0.20f, 0.30f, -10.0f, 82.0f, 92.0f,  4.0f, 20.0f, 70.0f, 94.0f,
          2, 95.0f, 3, 30.0f, 46.0f,
          { 3, 0, 4, 7, 3, 2, 0, 6 }, { 1, 0, 1, 1, 0, 1, 0, 1 }, false, 0.0f },

        { "MIDI Choir Keyboard", "MIDI",
          2, 0.50f, 0.50f,  0.0f, 70.0f, 62.0f, 24.0f, 72.0f, 14.0f, 86.0f,
          3, 80.0f, 1, 35.0f, 25.0f,
          { 0, 1, 2, 3, 4, 5, 6, 7 }, { 1, 0, 0, 0, 1, 0, 0, 0 }, false, 28.0f },

        { "Closed Nasal Hum", "Vocal",
          1, 0.85f, 0.88f, -8.0f, 22.0f, 60.0f, 16.0f, 42.0f, 16.0f, 76.0f,
          0, 42.0f, 1, 60.0f, 18.0f,
          { 9, 7, 9, 6, 9, 7, 9, 8 }, { 1, 0, 0, 0, 1, 0, 0, 0 }, false, 22.0f }
    }};
    return kPresets;
}

// ============================================================================
// APVTS Parameter Layout Factory
// ============================================================================
inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    auto pctRange = juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f);
    auto biPctRange = juce::NormalisableRange<float>(-100.0f, 100.0f, 0.1f);

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ params::vowelX, 1 }, "Vowel X",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.42f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ params::vowelY, 1 }, "Vowel Y",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.35f));

    p.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ params::tractProfile, 1 }, "Tract Profile",
        juce::StringArray{ "Bass", "Tenor", "Alto Choir", "Soprano", "Child", "Whisper Air", "Talk-Box Synth", "Alien / Giant" },
        2));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ params::throatSize, 1 }, "Throat Size",
        juce::NormalisableRange<float>(-50.0f, 50.0f, 0.1f), 2.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ params::jawOpen, 1 }, "Jaw Open", pctRange, 68.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ params::resonance, 1 }, "Resonance", pctRange, 54.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ params::breathAir, 1 }, "Breath Air", pctRange, 22.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ params::choirWidth, 1 }, "Choir Width", pctRange, 65.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ params::drive, 1 }, "Drive", pctRange, 14.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ params::mix, 1 }, "Mix", pctRange, 72.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ params::outputDb, 1 }, "Output",
        juce::NormalisableRange<float>(-18.0f, 12.0f, 0.1f), 0.0f));

    // Modulation
    p.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ params::modMode, 1 }, "Mod Mode",
        juce::StringArray{ "Env", "LFO", "StepSeq", "MIDI" }, 0));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ params::modDepth, 1 }, "Mod Depth", biPctRange, 48.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ params::envAttackMs, 1 }, "Env Attack",
        juce::NormalisableRange<float>(1.0f, 150.0f, 0.1f, 0.45f), 8.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ params::envReleaseMs, 1 }, "Env Release",
        juce::NormalisableRange<float>(15.0f, 800.0f, 0.5f, 0.45f), 140.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ params::lfoRateHz, 1 }, "LFO Rate",
        juce::NormalisableRange<float>(0.05f, 16.0f, 0.01f, 0.35f), 1.2f));

    p.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ params::lfoSync, 1 }, "LFO Sync", true));

    p.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ params::lfoDiv, 1 }, "LFO Division",
        juce::StringArray{ "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/2" }, 6));

    p.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ params::lfoShape, 1 }, "LFO Shape",
        juce::StringArray{ "Sine", "Triangle", "Saw", "S&H" }, 0));

    // 8-Step Syllable Sequencer
    p.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ params::seqRate, 1 }, "Seq Rate",
        juce::StringArray{ "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/2" }, 1));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ params::seqGlide, 1 }, "Seq Glide", pctRange, 45.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ params::consonantDip, 1 }, "Consonant Dip", pctRange, 32.0f));

    const juce::StringArray stepChoices { "EE", "IH", "EH", "AE", "AH", "AW", "OH", "OO", "UH", "MM", "Rest" };
    constexpr std::array<int, kNumSeqSteps> kDefaultSteps { 7, 4, 0, 10, 6, 4, 9, 10 };
    constexpr std::array<bool, kNumSeqSteps> kDefaultAccents { true, false, true, false, true, false, false, false };
    for (int i = 0; i < kNumSeqSteps; ++i) {
        p.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{ params::seqStepIds[i], 1 },
            "Seq Step " + juce::String(i + 1),
            stepChoices,
            kDefaultSteps[static_cast<size_t>(i)]));

        p.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{ params::seqAccentIds[i], 1 },
            "Seq Accent " + juce::String(i + 1),
            kDefaultAccents[static_cast<size_t>(i)]));
    }

    // TextureLink (Fossil <-> VowelMorph)
    const juce::StringArray busChoices { "Off", "A", "B", "C", "D", "E", "F", "G", "H" };
    p.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ params::textureBus, 1 }, "Texture Link Bus", busChoices, 1)); // Default Bus A

    p.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ params::linkEuclidSync, 1 }, "Link Euclid Sync", true));

    p.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ params::linkNoiseBreath, 1 }, "Link Air->Breath", true));

    p.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ params::linkTransientJaw, 1 }, "Link Trans->Jaw", true));

    p.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ params::linkFreezeSweep, 1 }, "Link Freeze Sweep", true));

    // HarmonyLink (Read-Only SessionMate / HarmonyHUD)
    p.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ params::harmonyBus, 1 }, "Harmony Link Bus", busChoices, 0)); // Default Off

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ params::harmonyChoirDrone, 1 }, "Chord Choir", pctRange, 35.0f));

    p.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ params::harmonySectionLift, 1 }, "Section Lift", true));

    return { p.begin(), p.end() };
}

} // namespace vowelmorph
