#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace sessionmate {

constexpr int kNumSlots = 6;
constexpr int kMaxStepsPerBar = 24;
constexpr int kMaxBarsHistory = 32;
constexpr int kMaxChordBars = 32;
constexpr int kMaxEventsPerBlock = 256;

enum class SlotId : int {
    Drums = 0,
    Bass = 1,
    Keys = 2,
    Guitar = 3,
    Percussion = 4,
    HornsStrings = 5
};

inline const char* slotName(SlotId id) noexcept {
    switch (id) {
        case SlotId::Drums:        return "Drums";
        case SlotId::Bass:         return "Bass";
        case SlotId::Keys:         return "Keys";
        case SlotId::Guitar:       return "Guitar";
        case SlotId::Percussion:   return "Percussion";
        case SlotId::HornsStrings: return "Horns/Strings";
    }
    return "Slot";
}

inline const char* slotShortName(SlotId id) noexcept {
    switch (id) {
        case SlotId::Drums:        return "1. DRUMS";
        case SlotId::Bass:         return "2. BASS";
        case SlotId::Keys:         return "3. KEYS";
        case SlotId::Guitar:       return "4. GUITAR";
        case SlotId::Percussion:   return "5. PERC";
        case SlotId::HornsStrings: return "6. HORNS";
    }
    return "SLOT";
}

// 1-based MIDI channel for multi-channel MIDI output (Ch 1..6)
inline int slotMidiChannel(int slotIndex) noexcept {
    return (slotIndex >= 0 && slotIndex < kNumSlots) ? (slotIndex + 1) : 1;
}

enum class SyncMode : int {
    DawHost = 0,
    LiveFollow = 1,
    StandaloneTap = 2
};

inline const char* syncModeName(SyncMode m) noexcept {
    switch (m) {
        case SyncMode::DawHost:       return "DAW Sync";
        case SyncMode::LiveFollow:    return "Live Follow";
        case SyncMode::StandaloneTap: return "Tap / Jam";
    }
    return "DAW Sync";
}

enum class SongSection : int {
    Intro = 0,
    Verse = 1,
    PreChorus = 2,
    Chorus = 3,
    Bridge = 4,
    Solo = 5,
    Outro = 6
};

constexpr int kNumSections = 7;

inline const char* sectionName(SongSection s) noexcept {
    switch (s) {
        case SongSection::Intro:     return "Intro";
        case SongSection::Verse:     return "Verse";
        case SongSection::PreChorus: return "Pre-Ch";
        case SongSection::Chorus:    return "Chorus";
        case SongSection::Bridge:    return "Bridge";
        case SongSection::Solo:      return "Solo";
        case SongSection::Outro:     return "Outro";
    }
    return "Chorus";
}

struct TimeSignature {
    int numerator = 4;
    int denominator = 4;

    // 16th-note grid steps per bar (3/4=12, 4/4=16, 5/4=20, 6/8=12, 7/8=14, 12/8=24)
    int stepsPerBar() const noexcept {
        const int den = denominator > 0 ? denominator : 4;
        const int steps = (numerator * 16) / den;
        if (steps < 4) return 4;
        if (steps > kMaxStepsPerBar) return kMaxStepsPerBar;
        return steps;
    }

    // Length of 1 bar in quarter-note PPQ units
    double beatsPerBarQuarter() const noexcept {
        const double den = denominator > 0 ? static_cast<double>(denominator) : 4.0;
        return static_cast<double>(numerator) * (4.0 / den);
    }

    bool isCompound() const noexcept {
        return denominator == 8 && (numerator % 3 == 0);
    }

    bool operator==(const TimeSignature& o) const noexcept {
        return numerator == o.numerator && denominator == o.denominator;
    }
    bool operator!=(const TimeSignature& o) const noexcept { return !(*this == o); }
};

enum class TimeSigPreset : int {
    TS_4_4 = 0,
    TS_3_4 = 1,
    TS_5_4 = 2,
    TS_6_8 = 3,
    TS_7_8 = 4,
    TS_12_8 = 5
};

inline TimeSignature timeSigFromPreset(int idx) noexcept {
    switch (idx) {
        case 1: return { 3, 4 };
        case 2: return { 5, 4 };
        case 3: return { 6, 8 };
        case 4: return { 7, 8 };
        case 5: return { 12, 8 };
        default: return { 4, 4 };
    }
}

inline int presetFromTimeSig(TimeSignature ts) noexcept {
    if (ts.numerator == 3 && ts.denominator == 4) return 1;
    if (ts.numerator == 5 && ts.denominator == 4) return 2;
    if (ts.numerator == 6 && ts.denominator == 8) return 3;
    if (ts.numerator == 7 && ts.denominator == 8) return 4;
    if (ts.numerator == 12 && ts.denominator == 8) return 5;
    return 0;
}

inline const char* pitchClassName(int pc, bool preferFlats = false) noexcept {
    static constexpr const char* kSharps[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    static constexpr const char* kFlats[12] = {
        "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"
    };
    if (pc < 0) return "N.C.";
    const int wrapped = ((pc % 12) + 12) % 12;
    return preferFlats ? kFlats[wrapped] : kSharps[wrapped];
}

enum class ChordQuality : int {
    Major = 0,
    Minor,
    Dominant7,
    Major7,
    Minor7,
    Minor7b5,
    Diminished,
    Diminished7,
    Augmented,
    Sus2,
    Sus4,
    Major6,
    Minor6,
    Dominant9,
    Major9,
    Minor9,
    Dominant11,
    Minor11,
    Dominant13,
    Major13,
    Minor13,
    Dominant7Sharp9,
    Dominant7Flat9,
    Power5,
    Add9
};

struct ChordSymbol {
    int rootPitchClass = 4;      // 0=C..11=B, -1=N.C.
    int bassPitchClass = -1;     // -1=root, else 0..11
    ChordQuality quality = ChordQuality::Minor9;
    bool valid = true;
    std::array<int, 7> intervals { 0, 3, 7, 10, 14, -1, -1 };
    int numTones = 5;

    int effectiveBass() const noexcept {
        return (bassPitchClass >= 0 && bassPitchClass < 12) ? bassPitchClass : rootPitchClass;
    }

    bool operator==(const ChordSymbol& o) const noexcept {
        return rootPitchClass == o.rootPitchClass &&
               bassPitchClass == o.bassPitchClass &&
               quality == o.quality &&
               valid == o.valid;
    }
    bool operator!=(const ChordSymbol& o) const noexcept { return !(*this == o); }
};

namespace DrumNote {
    constexpr int Kick         = 36;
    constexpr int SideStick    = 37;
    constexpr int Snare        = 38;
    constexpr int Clap         = 39;
    constexpr int SnareRim     = 40;
    constexpr int LowFloorTom  = 41;
    constexpr int ClosedHat    = 42;
    constexpr int HighFloorTom = 43;
    constexpr int PedalHat     = 44;
    constexpr int LowTom       = 45;
    constexpr int OpenHat      = 46;
    constexpr int MidTom       = 47;
    constexpr int HighTom      = 48;
    constexpr int Crash1       = 49;
    constexpr int Ride         = 51;
    constexpr int RideBell     = 53;
    constexpr int Tambourine   = 54;
    constexpr int Splash       = 55;
    constexpr int Cowbell      = 56;
    constexpr int Crash2       = 57;
}

namespace PercNote {
    constexpr int Tambourine = 54;
    constexpr int Cowbell    = 56;
    constexpr int HighBongo  = 60;
    constexpr int LowBongo   = 61;
    constexpr int MuteConga  = 62;
    constexpr int OpenConga  = 63;
    constexpr int LowConga   = 64;
    constexpr int Cabasa     = 69;
    constexpr int Shaker     = 70;
    constexpr int Clave      = 75;
    constexpr int Woodblock  = 76;
}

enum class Articulation : uint8_t {
    Normal = 0,
    Ghost,
    Rimshot,
    SideStick,
    HatOpen,
    HatHalfOpen,
    HatClosed,
    HatPedal,
    RideBell,
    Slap,
    PalmMute,
    StrumUp,
    StrumDown,
    Stab,
    Swell
};

enum class EventType : uint8_t {
    NoteOn = 0,
    NoteOff = 1,
    ControlChange = 2,
    PitchBend = 3
};

struct BandNoteEvent {
    int sampleOffset = 0;
    uint8_t slot = 0;
    EventType type = EventType::NoteOn;
    uint8_t data1 = 0;          // note or CC number
    uint8_t data2 = 0;          // velocity or CC value
    Articulation articulation = Articulation::Normal;
    double ppqPosition = 0.0;
};

enum class DrumPatch : int { AcousticKit = 0, VintageDry, BrushJazz, ModernPunch, LoFiCrisp, Electronic808 };
enum class BassPatch : int { FingerBass = 0, PickedRock, SlapFunk, UprightJazz, SynthSub, Acid303 };
enum class KeysPatch : int { RhodesMk1 = 0, Wurlitzer, AcousticPiano, B3Organ, Clavinet, WarmPad };
enum class GuitarPatch : int { CleanElectric = 0, FunkMute, AcousticStrum, ReggaeSkank, CrunchRock, NylonArp };
enum class PercPatch : int { StudioShakerTamb = 0, LatinCongaBongo, AfrobeatPerc, ElectronicMicro };
enum class HornsPatch : int { BrassStabs = 0, WarmStrings, RhodesFills, SynthLeadBrass };

inline int numPatchesForSlot(int slotIndex) noexcept {
    switch (slotIndex) {
        case 0: return 6;
        case 1: return 6;
        case 2: return 6;
        case 3: return 6;
        case 4: return 4;
        case 5: return 4;
        default: return 1;
    }
}

inline const char* patchNameForSlot(int slotIndex, int patchIndex) noexcept {
    static constexpr const char* kDrum[6]  = { "Acoustic Kit", "Vintage Dry", "Brush Jazz", "Modern Punch", "Lo-Fi Crisp", "808 Electronic" };
    static constexpr const char* kBass[6]  = { "Finger Bass", "Picked Rock", "Slap Funk", "Upright Jazz", "Synth Sub", "Acid 303" };
    static constexpr const char* kKeys[6]  = { "Rhodes Mk1", "Wurlitzer", "Acoustic Piano", "B3 Organ", "Clavinet", "Warm Pad" };
    static constexpr const char* kGtr[6]   = { "Clean Electric", "Funk Mute", "Acoustic Strum", "Reggae Skank", "Crunch Rock", "Nylon Arp" };
    static constexpr const char* kPerc[4]  = { "Shaker & Tamb", "Latin Congas", "Afro Perc", "Micro Electro" };
    static constexpr const char* kHorns[4] = { "Brass Stabs", "Warm Strings", "Rhodes Fills", "Synth Brass" };
    if (patchIndex < 0) patchIndex = 0;
    switch (slotIndex) {
        case 0: return kDrum[patchIndex % 6];
        case 1: return kBass[patchIndex % 6];
        case 2: return kKeys[patchIndex % 6];
        case 3: return kGtr[patchIndex % 6];
        case 4: return kPerc[patchIndex % 4];
        case 5: return kHorns[patchIndex % 4];
        default: return "Default";
    }
}

enum class DrumMapPreset : int {
    GeneralMIDI = 0,
    ToontrackSD3,
    AddictiveDrums2,
    StevenSlate5,
    GetGoodDrums,
    BFD3,
    Custom
};

constexpr int kNumDrumMaps = 7;

inline const char* drumMapName(DrumMapPreset p) noexcept {
    switch (p) {
        case DrumMapPreset::GeneralMIDI:     return "General MIDI";
        case DrumMapPreset::ToontrackSD3:    return "Toontrack SD3/EZD";
        case DrumMapPreset::AddictiveDrums2: return "Addictive Drums 2";
        case DrumMapPreset::StevenSlate5:    return "SSD 5";
        case DrumMapPreset::GetGoodDrums:    return "GetGood Drums";
        case DrumMapPreset::BFD3:            return "BFD3";
        case DrumMapPreset::Custom:          return "Custom Map";
    }
    return "General MIDI";
}

enum class SlotSourceMode : int {
    BuiltIn = 0,
    HostedVST3 = 1,
    CustomSFZ = 2
};

enum class HarmonicSourceMode : int {
    Chart = 0,
    LiveAudio = 1,
    HarmonyHudLink = 2
};

inline const char* harmonicSourceName(HarmonicSourceMode m) noexcept {
    switch (m) {
        case HarmonicSourceMode::Chart:          return "Chart";
        case HarmonicSourceMode::LiveAudio:      return "Live Audio";
        case HarmonicSourceMode::HarmonyHudLink: return "HUD Link";
    }
    return "Chart";
}

enum class KeysVoicingStyle : int {
    Close = 0,
    Drop2 = 1,
    RootlessJazz = 2,
    Shell37 = 3,
    OpenSpread = 4
};

constexpr int kNumKeysVoicings = 5;

inline const char* keysVoicingName(KeysVoicingStyle v) noexcept {
    switch (v) {
        case KeysVoicingStyle::Close:        return "Close";
        case KeysVoicingStyle::Drop2:        return "Drop-2";
        case KeysVoicingStyle::RootlessJazz: return "Rootless";
        case KeysVoicingStyle::Shell37:      return "Shell 3-7";
        case KeysVoicingStyle::OpenSpread:   return "Open Spread";
    }
    return "Close";
}

// 1:1 mirror of suite::harmony_link::LinkMode
enum class ScaleMode : int {
    Ionian = 0,
    Dorian,
    Phrygian,
    Lydian,
    Mixolydian,
    Aeolian,
    Locrian,
    HarmonicMinor,
    MelodicMinor,
    Altered,
    PentatonicMajor,
    PentatonicMinor,
    Blues
};

inline const char* scaleModeName(ScaleMode m) noexcept {
    switch (m) {
        case ScaleMode::Ionian:          return "Ionian";
        case ScaleMode::Dorian:          return "Dorian";
        case ScaleMode::Phrygian:        return "Phrygian";
        case ScaleMode::Lydian:          return "Lydian";
        case ScaleMode::Mixolydian:      return "Mixolydian";
        case ScaleMode::Aeolian:         return "Aeolian";
        case ScaleMode::Locrian:         return "Locrian";
        case ScaleMode::HarmonicMinor:   return "Harm. Min";
        case ScaleMode::MelodicMinor:    return "Mel. Min";
        case ScaleMode::Altered:         return "Altered";
        case ScaleMode::PentatonicMajor: return "Maj Pent";
        case ScaleMode::PentatonicMinor: return "Min Pent";
        case ScaleMode::Blues:           return "Blues";
    }
    return "Ionian";
}

struct HarmonyLinkHints {
    bool peerAlive = false;
    ChordSymbol liveChord {};
    float chordConfidence = 0.0f;
    int keyRootPc = 4;
    bool keyIsMinor = true;
    ScaleMode activeMode = ScaleMode::Aeolian;
    uint16_t scalePitchClassMask = 0;
    uint16_t avoidPitchClassMask = 0;
    uint16_t userPitchClassMask = 0;
    float userActivity = 0.0f;
    float userEnergy = 0.0f;
    bool userHasSignal = false;
    int clashCount = 0;
};

struct SlotConfig {
    SlotSourceMode sourceMode = SlotSourceMode::BuiltIn;
    int builtInPatch = 0;
    DrumMapPreset drumMap = DrumMapPreset::GeneralMIDI;
    float busy = 0.65f;
    float kickLock = 0.80f;
    float volumeDb = 0.0f;
    float pan = 0.0f;
    float reverbSend = 0.16f;
    float tiltEq = 0.0f;
    bool mute = false;
    bool solo = false;
    bool patternLock = false;
    int patternLockBars = 2;
    bool callAndResponse = true;
    float velocityCurve = 1.0f;
    bool cc4HiHatEnabled = true;
    bool cc64SustainEnabled = true;
};

struct PerformanceControls {
    int styleAIndex = 6;              // Default: 70s Dry Pocket Funk
    int styleBIndex = 7;              // Default: Neo-Soul (J-Dilla)
    float styleMorph = 0.25f;
    TimeSignature timeSig { 4, 4 };
    SongSection section = SongSection::Chorus;
    float energy = 0.68f;
    float swing = 0.56f;
    float pushPullMs = 2.0f;
    float leaveSpace = 0.65f;
    bool autoDetectChords = false;
    bool autoSectionFromEnergy = false;
    int keyRoot = 4;                  // E
    bool keyMinor = true;             // E minor
    bool halfTime = false;
    bool doubleTime = false;
    float masterVolumeDb = -2.0f;
    HarmonicSourceMode harmonicSource = HarmonicSourceMode::Chart;
    KeysVoicingStyle keysVoicing = KeysVoicingStyle::Close;
    bool clashAvoidance = true;
    int linkBus = 0;                  // 0=Off, 1..8 = Bus A..H
};

struct ListenerTelemetry {
    double detectedBpm = 104.0;
    float tempoConfidence = 0.0f;
    double beatPhase = 0.0;
    ChordSymbol detectedChord {};
    float chordConfidence = 0.0f;
    int detectedKeyRoot = 4;
    bool detectedKeyMinor = true;
    float inputRms = 0.0f;
    float userActivity = 0.0f;
    float energyEstimate = 0.5f;
    bool onsetDetected = false;
};

struct TransportState {
    bool isPlaying = false;
    double bpm = 104.0;
    double ppqPosition = 0.0;
    int currentBar = 0;
    int currentBeat = 0;
    int currentStep = 0;
    double barStartPpq = 0.0;
    TimeSignature timeSig { 4, 4 };
    SyncMode syncMode = SyncMode::DawHost;
};

struct UiSnapshot {
    TransportState transport {};
    ListenerTelemetry listener {};
    ChordSymbol activeChord {};
    ChordSymbol nextChord {};
    float beatsUntilNextChord = 4.0f;
    int activeChordBarIndex = 0;
    SongSection activeSection = SongSection::Chorus;
    bool fillActive = false;
    bool hudLinkConnected = false;
    ChordSymbol hudDetectedChord {};
    float hudChordConfidence = 0.0f;
    ScaleMode hudActiveMode = ScaleMode::Aeolian;
    int hudClashCount = 0;
    bool reharmActive = false;
    ChordSymbol reharmChord {};
    bool clashAvoidedThisBlock = false;
    std::array<float, kNumSlots> slotPeakL {};
    std::array<float, kNumSlots> slotPeakR {};
    std::array<bool, kNumSlots> slotNoteActivity {};
    std::array<bool, kNumSlots> slotFallbackActive {};
    float masterPeakL = 0.0f;
    float masterPeakR = 0.0f;
    int recordedMidiEvents = 0;
    int recordedBars = 0;
};

// Fixed-capacity lock-free event buffer for a single audio block (zero heap allocs)
struct BlockEventList {
    std::array<BandNoteEvent, kMaxEventsPerBlock> events {};
    int count = 0;

    void clear() noexcept { count = 0; }

    bool push(const BandNoteEvent& ev) noexcept {
        if (count >= kMaxEventsPerBlock) return false;
        events[static_cast<size_t>(count++)] = ev;
        return true;
    }
};

// Parameter ID constants for APVTS
namespace ParamID {
    inline constexpr const char* syncMode       = "sync_mode";
    inline constexpr const char* manualBpm      = "manual_bpm";
    inline constexpr const char* styleA         = "style_a";
    inline constexpr const char* styleB         = "style_b";
    inline constexpr const char* styleMorph     = "style_morph";
    inline constexpr const char* timeSig        = "time_sig";
    inline constexpr const char* section        = "section";
    inline constexpr const char* energy         = "energy";
    inline constexpr const char* swing          = "swing";
    inline constexpr const char* pushPullMs     = "push_pull_ms";
    inline constexpr const char* leaveSpace     = "leave_space";
    inline constexpr const char* autoChord      = "auto_chord";
    inline constexpr const char* autoSection    = "auto_section";
    inline constexpr const char* keyRoot        = "key_root";
    inline constexpr const char* keyMinor       = "key_minor";
    inline constexpr const char* halfTime       = "half_time";
    inline constexpr const char* doubleTime     = "double_time";
    inline constexpr const char* masterVolDb    = "master_vol_db";
    inline constexpr const char* harmonicSource = "harmonic_source";
    inline constexpr const char* linkBus        = "link_bus";
    inline constexpr const char* clashAvoid     = "clash_avoid";
    inline constexpr const char* keysVoicing    = "keys_voicing";

    inline std::string slotParam(int slot, const char* suffix) {
        return "slot" + std::to_string(slot) + "_" + suffix;
    }
}

} // namespace sessionmate
