#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace harmonyhud {

enum class ChordQuality : uint8_t {
    None = 0,
    Major,
    Minor,
    Dim,
    Aug,
    Sus2,
    Sus4,
    Six,
    Min6,
    Dom7,
    Maj7,
    Min7,
    Min7b5,
    Dim7,
    Dom9,
    Maj9,
    Min9,
    Dom11,
    Min11,
    Dom13,
    Maj13,
    Min13,
    Dom7Sharp9,
    Dom7Flat9,
    Power5,
    Add9,
    Count
};

inline constexpr int kNumChordQualities = static_cast<int>(ChordQuality::Count);

enum class ScaleMode : uint8_t {
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

enum class OverlayMode : int {
    Off = 0,
    ChordTones = 1,
    GuideTones = 2,
    Pentatonic = 3,
    ActiveMode = 4,
    Tensions = 5
};

enum class ViewMode : int {
    Input = 0,
    Band = 1,
    BothAndClash = 2
};

enum class FretPositionFilter : int {
    Auto = 0,
    Open = 1,
    Pos5 = 2,
    Pos7 = 3,
    Pos9 = 4
};

enum class ReharmPushTiming : int {
    Immediate = 0,
    NextBeat = 1,
    NextBar = 2
};

inline const char* scaleModeName(ScaleMode m) {
    switch (m) {
        case ScaleMode::Ionian:          return "Ionian (Major)";
        case ScaleMode::Dorian:          return "Dorian";
        case ScaleMode::Phrygian:        return "Phrygian";
        case ScaleMode::Lydian:          return "Lydian";
        case ScaleMode::Mixolydian:      return "Mixolydian";
        case ScaleMode::Aeolian:         return "Aeolian (Minor)";
        case ScaleMode::Locrian:         return "Locrian";
        case ScaleMode::HarmonicMinor:   return "Harmonic Minor";
        case ScaleMode::MelodicMinor:    return "Melodic Minor";
        case ScaleMode::Altered:         return "Altered (Super Locrian)";
        case ScaleMode::PentatonicMajor: return "Major Pentatonic";
        case ScaleMode::PentatonicMinor: return "Minor Pentatonic";
        case ScaleMode::Blues:           return "Blues";
        default:                         return "Ionian";
    }
}

inline uint16_t scaleModePitchClassMask(int rootPc, ScaleMode m) {
    static constexpr uint16_t kMasks[13] = {
        0b101010110101, // Ionian:        0,2,4,5,7,9,11
        0b011010101101, // Dorian:        0,2,3,5,7,9,10
        0b010110101011, // Phrygian:      0,1,3,5,7,8,10
        0b101011010101, // Lydian:        0,2,4,6,7,9,11
        0b011010110101, // Mixolydian:    0,2,4,5,7,9,10
        0b010110101101, // Aeolian:       0,2,3,5,7,8,10
        0b010101101011, // Locrian:       0,1,3,5,6,8,10
        0b100110101101, // HarmonicMinor: 0,2,3,5,7,8,11
        0b101010101101, // MelodicMinor:  0,2,3,5,7,9,11
        0b010101011011, // Altered:       0,1,3,4,6,8,10
        0b001010010101, // PentMajor:     0,2,4,7,9
        0b010010101001, // PentMinor:     0,3,5,7,10
        0b010011101001  // Blues:         0,3,5,6,7,10
    };
    const int idx = std::clamp(static_cast<int>(m), 0, 12);
    const int rot = ((rootPc % 12) + 12) % 12;
    const uint16_t base = kMasks[idx];
    uint16_t out = 0;
    for (int i = 0; i < 12; ++i) {
        if (base & (1u << i))
            out |= static_cast<uint16_t>(1u << ((i + rot) % 12));
    }
    return out;
}

inline const char* pitchClassName(int pc, bool preferFlats = false) {
    static constexpr const char* kSharps[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    static constexpr const char* kFlats[12] = {
        "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"
    };
    int norm = ((pc % 12) + 12) % 12;
    return preferFlats ? kFlats[norm] : kSharps[norm];
}

inline std::string midiToNoteName(int midiNote, bool preferFlats = false) {
    if (midiNote < 0 || midiNote > 127)
        return "--";
    int pc = midiNote % 12;
    int octave = (midiNote / 12) - 1;
    return std::string(pitchClassName(pc, preferFlats)) + std::to_string(octave);
}

inline const char* chordQualitySuffix(ChordQuality q) {
    switch (q) {
        case ChordQuality::Major:       return "";
        case ChordQuality::Minor:       return "m";
        case ChordQuality::Dim:         return "dim";
        case ChordQuality::Aug:         return "aug";
        case ChordQuality::Sus2:        return "sus2";
        case ChordQuality::Sus4:        return "sus4";
        case ChordQuality::Six:         return "6";
        case ChordQuality::Min6:        return "m6";
        case ChordQuality::Dom7:        return "7";
        case ChordQuality::Maj7:        return "maj7";
        case ChordQuality::Min7:        return "m7";
        case ChordQuality::Min7b5:      return "m7b5";
        case ChordQuality::Dim7:        return "dim7";
        case ChordQuality::Dom9:        return "9";
        case ChordQuality::Maj9:        return "maj9";
        case ChordQuality::Min9:        return "m9";
        case ChordQuality::Dom11:       return "11";
        case ChordQuality::Min11:       return "m11";
        case ChordQuality::Dom13:       return "13";
        case ChordQuality::Maj13:       return "maj13";
        case ChordQuality::Min13:       return "m13";
        case ChordQuality::Dom7Sharp9:  return "7#9";
        case ChordQuality::Dom7Flat9:   return "7b9";
        case ChordQuality::Power5:      return "5";
        case ChordQuality::Add9:        return "add9";
        case ChordQuality::None:
        case ChordQuality::Count:
        default:                        return "";
    }
}

struct ChordTemplateSpec {
    ChordQuality quality;
    std::array<float, 12> weights; // Relative to root = 0
    uint16_t coreMask;             // Required/primary pitch classes
    uint16_t extMask;              // Extension pitch classes
    float complexityPenalty;       // Mild Occam prior favoring simpler match when extensions absent
};

inline const std::array<ChordTemplateSpec, kNumChordQualities>& getChordTemplateSpecs() {
    static const std::array<ChordTemplateSpec, kNumChordQualities> kSpecs = {{
        // None
        { ChordQuality::None,       {0,0,0,0,0,0,0,0,0,0,0,0}, 0x000, 0x000, 0.0f },
        // Major: 0, 4, 7
        { ChordQuality::Major,      {1.0f,0,0,0,0.88f,0,0,0.82f,0,0,0,0}, (1<<0)|(1<<4)|(1<<7), 0, 0.00f },
        // Minor: 0, 3, 7
        { ChordQuality::Minor,      {1.0f,0,0,0.88f,0,0,0,0.82f,0,0,0,0}, (1<<0)|(1<<3)|(1<<7), 0, 0.00f },
        // Dim: 0, 3, 6
        { ChordQuality::Dim,        {1.0f,0,0,0.90f,0,0,0.92f,0,0,0,0,0}, (1<<0)|(1<<3)|(1<<6), 0, 0.03f },
        // Aug: 0, 4, 8
        { ChordQuality::Aug,        {1.0f,0,0,0,0.90f,0,0,0,0.92f,0,0,0}, (1<<0)|(1<<4)|(1<<8), 0, 0.04f },
        // Sus2: 0, 2, 7
        { ChordQuality::Sus2,       {1.0f,0,0.88f,0,0,0,0,0.84f,0,0,0,0}, (1<<0)|(1<<2)|(1<<7), 0, 0.03f },
        // Sus4: 0, 5, 7
        { ChordQuality::Sus4,       {1.0f,0,0,0,0,0.88f,0,0.84f,0,0,0,0}, (1<<0)|(1<<5)|(1<<7), 0, 0.03f },
        // Six: 0, 4, 7, 9
        { ChordQuality::Six,        {1.0f,0,0,0,0.85f,0,0,0.75f,0,0.82f,0,0}, (1<<0)|(1<<4)|(1<<7), (1<<9), 0.045f },
        // Min6: 0, 3, 7, 9
        { ChordQuality::Min6,       {1.0f,0,0,0.85f,0,0,0,0.75f,0,0.84f,0,0}, (1<<0)|(1<<3)|(1<<7), (1<<9), 0.045f },
        // Dom7: 0, 4, 7, 10
        { ChordQuality::Dom7,       {1.0f,0,0,0,0.86f,0,0,0.72f,0,0,0.86f,0}, (1<<0)|(1<<4)|(1<<10), (1<<7), 0.025f },
        // Maj7: 0, 4, 7, 11
        { ChordQuality::Maj7,       {1.0f,0,0,0,0.86f,0,0,0.74f,0,0,0,0.88f}, (1<<0)|(1<<4)|(1<<11), (1<<7), 0.025f },
        // Min7: 0, 3, 7, 10
        { ChordQuality::Min7,       {1.0f,0,0,0.86f,0,0,0,0.74f,0,0,0.86f,0}, (1<<0)|(1<<3)|(1<<10), (1<<7), 0.025f },
        // Min7b5: 0, 3, 6, 10
        { ChordQuality::Min7b5,     {1.0f,0,0,0.86f,0,0,0.88f,0,0,0,0.84f,0}, (1<<0)|(1<<3)|(1<<6)|(1<<10), 0, 0.04f },
        // Dim7: 0, 3, 6, 9
        { ChordQuality::Dim7,       {1.0f,0,0,0.88f,0,0,0.88f,0,0,0.88f,0,0}, (1<<0)|(1<<3)|(1<<6)|(1<<9), 0, 0.04f },
        // Dom9: 0, 2, 4, 7, 10
        { ChordQuality::Dom9,       {1.0f,0,0.80f,0,0.85f,0,0,0.65f,0,0,0.84f,0}, (1<<0)|(1<<4)|(1<<10), (1<<2)|(1<<7), 0.05f },
        // Maj9: 0, 2, 4, 7, 11
        { ChordQuality::Maj9,       {1.0f,0,0.82f,0,0.85f,0,0,0.66f,0,0,0,0.85f}, (1<<0)|(1<<4)|(1<<11), (1<<2)|(1<<7), 0.05f },
        // Min9: 0, 2, 3, 7, 10
        { ChordQuality::Min9,       {1.0f,0,0.82f,0.85f,0,0,0,0.66f,0,0,0.84f,0}, (1<<0)|(1<<3)|(1<<10), (1<<2)|(1<<7), 0.05f },
        // Dom11: 0, 2, 5, 7, 10
        { ChordQuality::Dom11,      {1.0f,0,0.68f,0,0.40f,0.82f,0,0.72f,0,0,0.84f,0}, (1<<0)|(1<<5)|(1<<10), (1<<2)|(1<<7), 0.065f },
        // Min11: 0, 2, 3, 5, 7, 10
        { ChordQuality::Min11,      {1.0f,0,0.65f,0.84f,0,0.82f,0,0.65f,0,0,0.82f,0}, (1<<0)|(1<<3)|(1<<5)|(1<<10), (1<<2)|(1<<7), 0.065f },
        // Dom13: 0, 2, 4, 7, 9, 10
        { ChordQuality::Dom13,      {1.0f,0,0.62f,0,0.84f,0,0,0.58f,0,0.84f,0.84f,0}, (1<<0)|(1<<4)|(1<<9)|(1<<10), (1<<2)|(1<<7), 0.06f },
        // Maj13: 0, 2, 4, 7, 9, 11
        { ChordQuality::Maj13,      {1.0f,0,0.60f,0,0.84f,0,0,0.55f,0,0.84f,0,0.84f}, (1<<0)|(1<<4)|(1<<9)|(1<<11), (1<<2)|(1<<7), 0.07f },
        // Min13: 0, 2, 3, 7, 9, 10
        { ChordQuality::Min13,      {1.0f,0,0.60f,0.84f,0,0,0,0.55f,0,0.84f,0.84f,0}, (1<<0)|(1<<3)|(1<<9)|(1<<10), (1<<2)|(1<<7), 0.07f },
        // Dom7Sharp9: 0, 3, 4, 7, 10
        { ChordQuality::Dom7Sharp9, {1.0f,0,0,0.82f,0.84f,0,0,0.60f,0,0,0.84f,0}, (1<<0)|(1<<3)|(1<<4)|(1<<10), (1<<7), 0.065f },
        // Dom7Flat9: 0, 1, 4, 7, 10
        { ChordQuality::Dom7Flat9,  {1.0f,0.82f,0,0,0.84f,0,0,0.60f,0,0,0.84f,0}, (1<<0)|(1<<1)|(1<<4)|(1<<10), (1<<7), 0.065f },
        // Power5: 0, 7
        { ChordQuality::Power5,     {1.0f,0,0,0,0,0,0,0.92f,0,0,0,0}, (1<<0)|(1<<7), 0, 0.055f },
        // Add9: 0, 2, 4, 7
        { ChordQuality::Add9,       {1.0f,0,0.82f,0,0.86f,0,0,0.78f,0,0,0,0}, (1<<0)|(1<<2)|(1<<4)|(1<<7), 0, 0.045f },
    }};
    return kSpecs;
}

inline std::vector<int> qualityIntervals(ChordQuality q) {
    switch (q) {
        case ChordQuality::Major:       return {0, 4, 7};
        case ChordQuality::Minor:       return {0, 3, 7};
        case ChordQuality::Dim:         return {0, 3, 6};
        case ChordQuality::Aug:         return {0, 4, 8};
        case ChordQuality::Sus2:        return {0, 2, 7};
        case ChordQuality::Sus4:        return {0, 5, 7};
        case ChordQuality::Six:         return {0, 4, 7, 9};
        case ChordQuality::Min6:        return {0, 3, 7, 9};
        case ChordQuality::Dom7:        return {0, 4, 7, 10};
        case ChordQuality::Maj7:        return {0, 4, 7, 11};
        case ChordQuality::Min7:        return {0, 3, 7, 10};
        case ChordQuality::Min7b5:      return {0, 3, 6, 10};
        case ChordQuality::Dim7:        return {0, 3, 6, 9};
        case ChordQuality::Dom9:        return {0, 4, 7, 10, 14};
        case ChordQuality::Maj9:        return {0, 4, 7, 11, 14};
        case ChordQuality::Min9:        return {0, 3, 7, 10, 14};
        case ChordQuality::Dom11:       return {0, 7, 10, 14, 17};
        case ChordQuality::Min11:       return {0, 3, 7, 10, 14, 17};
        case ChordQuality::Dom13:       return {0, 4, 10, 14, 21};
        case ChordQuality::Maj13:       return {0, 4, 11, 14, 21};
        case ChordQuality::Min13:       return {0, 3, 10, 14, 21};
        case ChordQuality::Dom7Sharp9:  return {0, 4, 10, 15};
        case ChordQuality::Dom7Flat9:   return {0, 4, 10, 13};
        case ChordQuality::Power5:      return {0, 7, 12};
        case ChordQuality::Add9:        return {0, 4, 7, 14};
        case ChordQuality::None:
        case ChordQuality::Count:
        default:                        return {};
    }
}

struct ChordSymbol {
    int root = -1;
    ChordQuality quality = ChordQuality::None;
    int bass = -1;
    float confidence = 0.0f;

    bool isValid() const {
        return root >= 0 && root < 12 && quality != ChordQuality::None;
    }

    bool sameHarmony(const ChordSymbol& other) const {
        return root == other.root && quality == other.quality && effectiveBass() == other.effectiveBass();
    }

    int effectiveBass() const {
        return (bass >= 0 && bass < 12) ? bass : root;
    }

    bool isSlashChord() const {
        return isValid() && bass >= 0 && bass < 12 && bass != root;
    }

    uint16_t pitchClassMask() const {
        if (!isValid()) return 0;
        uint16_t mask = 0;
        for (int iv : qualityIntervals(quality))
            mask |= static_cast<uint16_t>(1u << ((root + iv) % 12));
        if (isSlashChord())
            mask |= static_cast<uint16_t>(1u << bass);
        return mask;
    }

    // 3rd & 7th guide-tone pitch class mask (the harmonic skeleton for voice leading & soloing!)
    uint16_t guideToneMask() const {
        if (!isValid()) return 0;
        uint16_t mask = 0;
        for (int iv : qualityIntervals(quality)) {
            int rel = iv % 12;
            if (rel == 3 || rel == 4 || rel == 10 || rel == 11)
                mask |= static_cast<uint16_t>(1u << ((root + rel) % 12));
        }
        if (mask == 0) {
            // Sus/Power fallback: include 2/5/7
            for (int iv : qualityIntervals(quality)) {
                int rel = iv % 12;
                if (rel == 2 || rel == 5 || rel == 7)
                    mask |= static_cast<uint16_t>(1u << ((root + rel) % 12));
            }
        }
        return mask;
    }

    std::string name(bool preferFlats = false) const {
        if (!isValid())
            return "N.C.";
        std::string s = std::string(pitchClassName(root, preferFlats)) + chordQualitySuffix(quality);
        if (isSlashChord())
            s += "/" + std::string(pitchClassName(bass, preferFlats));
        return s;
    }

    std::vector<int> voicingMidiNotes(int baseOctave = 3) const {
        std::vector<int> notes;
        if (!isValid()) return notes;
        int rootMidi = (baseOctave + 1) * 12 + root;
        if (rootMidi > 55) rootMidi -= 12;

        if (isSlashChord()) {
            int bassMidi = (baseOctave) * 12 + bass;
            while (bassMidi >= rootMidi) bassMidi -= 12;
            while (bassMidi < 36) bassMidi += 12;
            notes.push_back(bassMidi);
        }

        for (int iv : qualityIntervals(quality)) {
            int n = rootMidi + iv;
            if (n <= 84) notes.push_back(n);
        }
        std::sort(notes.begin(), notes.end());
        notes.erase(std::unique(notes.begin(), notes.end()), notes.end());
        return notes;
    }
};

struct KeyEstimate {
    int tonic = 0;
    bool isMinor = false;
    float confidence = 0.0f;

    bool preferFlats() const {
        if (!isMinor)
            return tonic == 5 || tonic == 10 || tonic == 3 || tonic == 8 || tonic == 1 || tonic == 6;
        return tonic == 2 || tonic == 7 || tonic == 0 || tonic == 5 || tonic == 10 || tonic == 3;
    }

    std::string name() const {
        return std::string(pitchClassName(tonic, preferFlats())) + (isMinor ? " Minor" : " Major");
    }

    std::string romanNumeral(const ChordSymbol& chord) const {
        if (!chord.isValid())
            return "-";

        int degree = ((chord.root - tonic) % 12 + 12) % 12;
        bool isMinorQual = (chord.quality == ChordQuality::Minor ||
                            chord.quality == ChordQuality::Min6 ||
                            chord.quality == ChordQuality::Min7 ||
                            chord.quality == ChordQuality::Min9 ||
                            chord.quality == ChordQuality::Min11 ||
                            chord.quality == ChordQuality::Min13 ||
                            chord.quality == ChordQuality::Dim ||
                            chord.quality == ChordQuality::Dim7 ||
                            chord.quality == ChordQuality::Min7b5);

        static constexpr const char* kMajorUpper[12] = {
            "I", "bII", "II", "bIII", "III", "IV", "#IV", "V", "bVI", "VI", "bVII", "VII"
        };
        static constexpr const char* kMajorLower[12] = {
            "i", "bii", "ii", "biii", "iii", "iv", "#iv", "v", "bvi", "vi", "bvii", "vii"
        };
        static constexpr const char* kMinorUpper[12] = {
            "I", "bII", "II", "III", "#III", "IV", "#IV", "V", "VI", "#VI", "VII", "#VII"
        };
        static constexpr const char* kMinorLower[12] = {
            "i", "bii", "ii", "iii", "#iii", "iv", "#iv", "v", "vi", "#vi", "vii", "#vii"
        };

        std::string base;
        if (!isMinor)
            base = isMinorQual ? kMajorLower[degree] : kMajorUpper[degree];
        else
            base = isMinorQual ? kMinorLower[degree] : kMinorUpper[degree];

        switch (chord.quality) {
            case ChordQuality::Major:
            case ChordQuality::Minor:      break;
            case ChordQuality::Dim:        base += "o"; break;
            case ChordQuality::Aug:        base += "+"; break;
            case ChordQuality::Sus2:       base += "sus2"; break;
            case ChordQuality::Sus4:       base += "sus4"; break;
            case ChordQuality::Six:
            case ChordQuality::Min6:       base += "6"; break;
            case ChordQuality::Dom7:
            case ChordQuality::Min7:       base += "7"; break;
            case ChordQuality::Maj7:       base += "maj7"; break;
            case ChordQuality::Min7b5:     base += "ø7"; break;
            case ChordQuality::Dim7:       base += "o7"; break;
            case ChordQuality::Dom9:
            case ChordQuality::Min9:       base += "9"; break;
            case ChordQuality::Maj9:       base += "maj9"; break;
            case ChordQuality::Dom11:
            case ChordQuality::Min11:      base += "11"; break;
            case ChordQuality::Dom13:
            case ChordQuality::Min13:      base += "13"; break;
            case ChordQuality::Maj13:      base += "maj13"; break;
            case ChordQuality::Dom7Sharp9: base += "7#9"; break;
            case ChordQuality::Dom7Flat9:  base += "7b9"; break;
            case ChordQuality::Power5:     base += "5"; break;
            case ChordQuality::Add9:       base += "add9"; break;
            default: break;
        }
        return base;
    }
};

// Infer the most musically idiomatic mode for a chord inside the detected key
inline ScaleMode inferModeForChordAndKey(const ChordSymbol& chord, const KeyEstimate& key) {
    if (!chord.isValid())
        return key.isMinor ? ScaleMode::Aeolian : ScaleMode::Ionian;

    if (chord.quality == ChordQuality::Dom7Sharp9 || chord.quality == ChordQuality::Dom7Flat9)
        return ScaleMode::Altered;
    if (chord.quality == ChordQuality::Min7b5)
        return ScaleMode::Locrian;
    if (chord.quality == ChordQuality::Dim || chord.quality == ChordQuality::Dim7)
        return ScaleMode::HarmonicMinor;

    const int deg = ((chord.root - key.tonic) % 12 + 12) % 12;
    const bool isDom = (chord.quality == ChordQuality::Dom7 || chord.quality == ChordQuality::Dom9 ||
                        chord.quality == ChordQuality::Dom11 || chord.quality == ChordQuality::Dom13);
    const bool isMin = (chord.quality == ChordQuality::Minor || chord.quality == ChordQuality::Min7 ||
                        chord.quality == ChordQuality::Min9 || chord.quality == ChordQuality::Min11 ||
                        chord.quality == ChordQuality::Min13);
    const bool isMaj = (chord.quality == ChordQuality::Major || chord.quality == ChordQuality::Maj7 ||
                        chord.quality == ChordQuality::Maj9 || chord.quality == ChordQuality::Maj13 ||
                        chord.quality == ChordQuality::Six || chord.quality == ChordQuality::Add9);

    if (chord.quality == ChordQuality::Min6)
        return ScaleMode::MelodicMinor;
    if (isDom)
        return ScaleMode::Mixolydian;

    if (!key.isMinor) {
        if (deg == 0 && isMaj) return ScaleMode::Ionian;
        if (deg == 2 && isMin) return ScaleMode::Dorian;
        if (deg == 4 && isMin) return ScaleMode::Phrygian;
        if (deg == 5 && isMaj) return ScaleMode::Lydian;
        if (deg == 7)          return ScaleMode::Mixolydian;
        if (deg == 9 && isMin) return ScaleMode::Aeolian;
        if (deg == 11)         return ScaleMode::Locrian;
    } else {
        if (deg == 0 && isMin) return ScaleMode::Aeolian;
        if (deg == 2)          return ScaleMode::Locrian;
        if (deg == 3 && isMaj) return ScaleMode::Ionian;
        if (deg == 5 && isMin) return ScaleMode::Dorian;
        if (deg == 7 && isMin) return ScaleMode::Phrygian;
        if (deg == 7 && isDom) return ScaleMode::HarmonicMinor;
        if (deg == 8 && isMaj) return ScaleMode::Lydian;
        if (deg == 10 && isMaj) return ScaleMode::Mixolydian;
    }

    return isMin ? ScaleMode::Dorian : ScaleMode::Ionian;
}

inline uint16_t computeOverlayPitchClassMask(OverlayMode mode,
                                             const ChordSymbol& chord,
                                             const KeyEstimate& key,
                                             ScaleMode activeMode) {
    const int refRoot = chord.isValid() ? chord.root : key.tonic;
    switch (mode) {
        case OverlayMode::Off:
            return 0;
        case OverlayMode::ChordTones:
            return chord.pitchClassMask();
        case OverlayMode::GuideTones:
            return chord.guideToneMask();
        case OverlayMode::Pentatonic: {
            bool minorPent = key.isMinor;
            if (chord.isValid()) {
                minorPent = (chord.quality == ChordQuality::Minor || chord.quality == ChordQuality::Min7 ||
                             chord.quality == ChordQuality::Min9 || chord.quality == ChordQuality::Min11 ||
                             chord.quality == ChordQuality::Min6 || chord.quality == ChordQuality::Min7b5);
            }
            return scaleModePitchClassMask(refRoot, minorPent ? ScaleMode::PentatonicMinor
                                                              : ScaleMode::PentatonicMajor);
        }
        case OverlayMode::ActiveMode:
            return scaleModePitchClassMask(refRoot, activeMode);
        case OverlayMode::Tensions: {
            // Color tensions: 9th, 11th/#11th, 13th present in activeMode but outside triad!
            uint16_t modeMask = scaleModePitchClassMask(refRoot, activeMode);
            uint16_t chordMask = chord.pitchClassMask();
            uint16_t tensions = static_cast<uint16_t>(modeMask & ~chordMask);
            // Also include explicit 9/11/13 chord extensions
            if (chord.isValid()) {
                for (int iv : qualityIntervals(chord.quality)) {
                    if (iv >= 13 || iv == 2 || iv == 9)
                        tensions |= static_cast<uint16_t>(1u << ((chord.root + iv) % 12));
                }
            }
            return tensions;
        }
        default:
            return 0;
    }
}

struct SubstitutionSuggestion {
    ChordSymbol chord;
    std::string category; // "Functional", "Tritone Sub", "Sec. Dom", "Modal Borrow", "Color / Slash"
    std::string label;
    std::string reason;
    std::vector<int> midiNotes;
};

// Expanded 5-chip categorized reharmonization palette!
inline std::array<SubstitutionSuggestion, 5> generateSubstitutions(const ChordSymbol& current,
                                                                   const KeyEstimate& key) {
    std::array<SubstitutionSuggestion, 5> out;
    bool flats = key.preferFlats();

    auto makeSub = [&](int root, ChordQuality q, const std::string& category,
                       const std::string& reason, int bass = -1) {
        SubstitutionSuggestion s;
        s.chord.root = ((root % 12) + 12) % 12;
        s.chord.quality = q;
        s.chord.bass = (bass >= 0) ? (((bass % 12) + 12) % 12) : -1;
        s.chord.confidence = 1.0f;
        s.category = category;
        s.label = s.chord.name(flats);
        s.reason = reason;
        s.midiNotes = s.chord.voicingMidiNotes(3);
        return s;
    };

    const int t = key.tonic;
    if (!current.isValid()) {
        if (!key.isMinor) {
            out[0] = makeSub(t,     ChordQuality::Maj9,  "Functional",   "Tonic Imaj9");
            out[1] = makeSub(t + 1, ChordQuality::Dom7,  "Tritone Sub",  "Tritone bII7");
            out[2] = makeSub(t + 9, ChordQuality::Dom7,  "Sec. Dom",     "Sec. Dom V/ii");
            out[3] = makeSub(t + 5, ChordQuality::Min6,  "Modal Borrow", "Borrowed iv6");
            out[4] = makeSub(t + 5, ChordQuality::Maj9,  "Color / Slash","Pedal IV/I", t);
        } else {
            out[0] = makeSub(t,     ChordQuality::Min9,  "Functional",   "Tonic i9");
            out[1] = makeSub(t + 1, ChordQuality::Dom7,  "Tritone Sub",  "Tritone bII7");
            out[2] = makeSub(t + 7, ChordQuality::Dom7Flat9, "Sec. Dom", "Harmonic V7b9");
            out[3] = makeSub(t + 5, ChordQuality::Maj7,  "Modal Borrow", "Dorian IVmaj7");
            out[4] = makeSub(t + 8, ChordQuality::Maj9,  "Color / Slash","Lush bVImaj9");
        }
        return out;
    }

    const int deg = ((current.root - t) % 12 + 12) % 12;

    if (!key.isMinor) {
        switch (deg) {
            case 0: // I
                out[0] = makeSub(t + 9, ChordQuality::Min9,  "Functional",   "Relative vi9");
                out[1] = makeSub(t + 1, ChordQuality::Dom7,  "Tritone Sub",  "Lead-in bII7");
                out[2] = makeSub(t + 9, ChordQuality::Dom7,  "Sec. Dom",     "Sec. Dom V/ii");
                out[3] = makeSub(t + 5, ChordQuality::Min6,  "Modal Borrow", "Borrowed iv6");
                out[4] = makeSub(t,     ChordQuality::Maj7,  "Color / Slash","1st Inv I/3", t + 4);
                break;
            case 2: // ii
                out[0] = makeSub(t + 7, ChordQuality::Dom13, "Functional",   "Dominant V13");
                out[1] = makeSub(t + 1, ChordQuality::Dom7,  "Tritone Sub",  "Tritone bII7");
                out[2] = makeSub(t + 2, ChordQuality::Dom9,  "Sec. Dom",     "Sec. Dom V/V");
                out[3] = makeSub(t + 5, ChordQuality::Min6,  "Modal Borrow", "Borrowed iv6");
                out[4] = makeSub(t + 5, ChordQuality::Maj9,  "Color / Slash","IVmaj9/V", t + 7);
                break;
            case 4: // iii
                out[0] = makeSub(t + 9, ChordQuality::Min9,  "Functional",   "Circle vi9");
                out[1] = makeSub(t + 3, ChordQuality::Dom7,  "Tritone Sub",  "Tritone bIII7");
                out[2] = makeSub(t + 9, ChordQuality::Dom7,  "Sec. Dom",     "Sec. Dom V/ii");
                out[3] = makeSub(t + 10,ChordQuality::Maj9,  "Modal Borrow", "Backdoor bVII");
                out[4] = makeSub(t,     ChordQuality::Maj7,  "Color / Slash","Step I/3", t + 4);
                break;
            case 5: // IV (matches wireframe G13, Fm6, E7 + tritone & slash!)
                out[0] = makeSub(t + 7, ChordQuality::Dom13, "Functional",   "Dominant V13");
                out[1] = makeSub(t + 1, ChordQuality::Dom7,  "Tritone Sub",  "Tritone bII7");
                out[2] = makeSub(t + 4, ChordQuality::Dom7,  "Sec. Dom",     "Sec. Dom V/vi");
                out[3] = makeSub(t + 5, ChordQuality::Min6,  "Modal Borrow", "Borrowed iv6");
                out[4] = makeSub(t + 5, ChordQuality::Maj9,  "Color / Slash","Gospel IV/V", t + 7);
                break;
            case 7: // V
                out[0] = makeSub(t,     ChordQuality::Maj9,  "Functional",   "Resolve Imaj9");
                out[1] = makeSub(t + 1, ChordQuality::Dom7,  "Tritone Sub",  "Tritone bII7");
                out[2] = makeSub(t + 4, ChordQuality::Dom7,  "Sec. Dom",     "Deceptive V/vi");
                out[3] = makeSub(t + 8, ChordQuality::Maj7,  "Modal Borrow", "Epic bVImaj7");
                out[4] = makeSub(t + 9, ChordQuality::Min11, "Color / Slash","Deceptive vi11");
                break;
            case 9: // vi
                out[0] = makeSub(t + 2, ChordQuality::Min9,  "Functional",   "Circle ii9");
                out[1] = makeSub(t + 8, ChordQuality::Dom7,  "Tritone Sub",  "Tritone bVI7");
                out[2] = makeSub(t + 2, ChordQuality::Dom9,  "Sec. Dom",     "Sec. Dom V/V");
                out[3] = makeSub(t + 5, ChordQuality::Min6,  "Modal Borrow", "Borrowed iv6");
                out[4] = makeSub(t + 5, ChordQuality::Maj9,  "Color / Slash","Lift IVmaj9");
                break;
            default:
                out[0] = makeSub(t + 7, ChordQuality::Dom13, "Functional",   "Dominant V13");
                out[1] = makeSub(current.root + 6, ChordQuality::Dom7, "Tritone Sub", "Tritone Sub");
                out[2] = makeSub(t + 2, ChordQuality::Dom9,  "Sec. Dom",     "Sec. Dom V/V");
                out[3] = makeSub(t + 5, ChordQuality::Min6,  "Modal Borrow", "Borrowed iv6");
                out[4] = makeSub(t,     ChordQuality::Maj9,  "Color / Slash","Resolve Imaj9");
                break;
        }
    } else {
        switch (deg) {
            case 0: // i
                out[0] = makeSub(t + 5, ChordQuality::Min9,  "Functional",   "Subdom iv9");
                out[1] = makeSub(t + 1, ChordQuality::Dom7,  "Tritone Sub",  "Tritone bII7");
                out[2] = makeSub(t + 7, ChordQuality::Dom7Sharp9, "Sec. Dom","Altered V7#9");
                out[3] = makeSub(t + 5, ChordQuality::Dom9,  "Modal Borrow", "Dorian IV9");
                out[4] = makeSub(t + 8, ChordQuality::Maj9,  "Color / Slash","Lush bVImaj9");
                break;
            case 5: // iv
                out[0] = makeSub(t + 7, ChordQuality::Dom7,  "Functional",   "Harmonic V7");
                out[1] = makeSub(t + 1, ChordQuality::Dom7,  "Tritone Sub",  "Tritone bII7");
                out[2] = makeSub(t + 10,ChordQuality::Dom9,  "Sec. Dom",     "Sec. Dom V/III");
                out[3] = makeSub(t + 3, ChordQuality::Maj9,  "Modal Borrow", "Relative bIIImaj9");
                out[4] = makeSub(t,     ChordQuality::Min7,  "Color / Slash","i7/5 Pedal", t + 7);
                break;
            case 7: // V
                out[0] = makeSub(t,     ChordQuality::Min9,  "Functional",   "Resolve i9");
                out[1] = makeSub(t + 1, ChordQuality::Dom7,  "Tritone Sub",  "Tritone bII7");
                out[2] = makeSub(t + 2, ChordQuality::Dom7Flat9, "Sec. Dom", "Sec. Dom V/V");
                out[3] = makeSub(t,     ChordQuality::Maj7,  "Modal Borrow", "Picardy Imaj7");
                out[4] = makeSub(t + 8, ChordQuality::Maj7,  "Color / Slash","Deceptive bVI");
                break;
            default:
                out[0] = makeSub(t + 7, ChordQuality::Dom7,  "Functional",   "Harmonic V7");
                out[1] = makeSub(current.root + 6, ChordQuality::Dom7, "Tritone Sub", "Tritone Sub");
                out[2] = makeSub(t + 10,ChordQuality::Dom9,  "Sec. Dom",     "Backdoor bVII9");
                out[3] = makeSub(t + 5, ChordQuality::Min6,  "Modal Borrow", "Dorian iv6");
                out[4] = makeSub(t,     ChordQuality::Min9,  "Color / Slash","Resolve i9");
                break;
        }
    }
    return out;
}

struct ClashInfo {
    int mainMidi = -1;
    int sidechainMidi = -1;
    int intervalSemitones = 0; // 1 = m2/M7/m9, 6 = tritone
    float severity = 0.0f;     // 0..1
    std::string sourceLabel;   // "Sidechain" or "Band Keys" / "Band Bass" etc.

    bool isMinorSecond() const { return intervalSemitones == 1; }
    bool isTritone() const { return intervalSemitones == 6; }

    std::string description(bool preferFlats = false) const {
        if (mainMidi < 0 || sidechainMidi < 0) return "None";
        std::string kind = isMinorSecond() ? "m2" : (isTritone() ? "Tritone" : "Clash");
        std::string src = sourceLabel.empty() ? "" : (" [" + sourceLabel + "]");
        return midiToNoteName(mainMidi, preferFlats) + " vs " +
               midiToNoteName(sidechainMidi, preferFlats) + " (" + kind + ")" + src;
    }
};

enum class GuitarTuning : int {
    Standard = 0,
    DropD = 1,
    DADGAD = 2
};

inline const char* guitarTuningName(GuitarTuning t) {
    switch (t) {
        case GuitarTuning::Standard: return "Standard (EADGBE)";
        case GuitarTuning::DropD:    return "Drop D (DADGBE)";
        case GuitarTuning::DADGAD:   return "DADGAD";
        default:                     return "Standard (EADGBE)";
    }
}

inline std::array<int, 6> guitarOpenStrings(GuitarTuning t) {
    switch (t) {
        case GuitarTuning::Standard: return {40, 45, 50, 55, 59, 64}; // E2 A2 D3 G3 B3 E4
        case GuitarTuning::DropD:    return {38, 45, 50, 55, 59, 64}; // D2 A2 D3 G3 B3 E4
        case GuitarTuning::DADGAD:   return {38, 45, 50, 55, 57, 62}; // D2 A2 D3 G3 A3 D4
        default:                     return {40, 45, 50, 55, 59, 64};
    }
}

inline std::array<const char*, 6> guitarStringLabels(GuitarTuning t) {
    switch (t) {
        case GuitarTuning::Standard: return {"E", "A", "D", "G", "B", "e"};
        case GuitarTuning::DropD:    return {"D", "A", "D", "G", "B", "e"};
        case GuitarTuning::DADGAD:   return {"D", "A", "D", "G", "A", "d"};
        default:                     return {"E", "A", "D", "G", "B", "e"};
    }
}

struct GuitarVoicing {
    std::array<int, 6> frets = {-1, -1, -1, -1, -1, -1}; // -1 = muted, 0 = open, 1..15 = fret
    int baseFret = 1;
    bool valid = false;
};

inline GuitarVoicing solveGuitarVoicing(const ChordSymbol& chord,
                                        GuitarTuning tuning,
                                        FretPositionFilter posFilter = FretPositionFilter::Auto) {
    GuitarVoicing best;
    if (!chord.isValid())
        return best;

    const auto openMidi = guitarOpenStrings(tuning);
    const uint16_t mask = chord.pitchClassMask();
    const int targetBass = chord.effectiveBass();
    const auto& spec = getChordTemplateSpecs()[static_cast<size_t>(chord.quality)];
    const uint16_t coreMask = spec.coreMask;

    int startMin = 1, startMax = 10;
    bool forceNoOpen = false;
    switch (posFilter) {
        case FretPositionFilter::Open: startMin = 1; startMax = 3; break;
        case FretPositionFilter::Pos5: startMin = 4; startMax = 7; forceNoOpen = true; break;
        case FretPositionFilter::Pos7: startMin = 6; startMax = 9; forceNoOpen = true; break;
        case FretPositionFilter::Pos9: startMin = 8; startMax = 11; forceNoOpen = true; break;
        case FretPositionFilter::Auto:
        default: break;
    }

    auto searchRange = [&](int sMin, int sMax, bool noOpen) {
        float bestScore = -1e9f;
        GuitarVoicing result;

        for (int winStart = sMin; winStart <= sMax; ++winStart) {
            int winEnd = winStart + 3;
            bool allowOpen = (!noOpen && winStart <= 2);

            std::array<std::vector<int>, 6> cands;
            for (int s = 0; s < 6; ++s) {
                cands[s].push_back(-1);
                if (allowOpen) {
                    int pc = openMidi[s] % 12;
                    if (mask & (1u << pc)) cands[s].push_back(0);
                }
                for (int f = winStart; f <= winEnd; ++f) {
                    int pc = (openMidi[s] + f) % 12;
                    if (mask & (1u << pc)) cands[s].push_back(f);
                }
            }

            for (int bassStr = 0; bassStr <= 2; ++bassStr) {
                for (int bf : cands[bassStr]) {
                    if (bf < 0) continue;
                    int bpc = (openMidi[bassStr] + bf) % 12;
                    if (bpc != targetBass) continue;

                    std::array<int, 6> cur = {-1, -1, -1, -1, -1, -1};
                    cur[bassStr] = bf;
                    uint16_t covered = static_cast<uint16_t>(1u << bpc);

                    for (int s = bassStr + 1; s < 6; ++s) {
                        int bestF = -1;
                        float bestLocal = -1e9f;
                        for (int f : cands[s]) {
                            if (f < 0) continue;
                            int pc = (openMidi[s] + f) % 12;
                            float loc = 0.0f;
                            bool isNew = !(covered & (1u << pc));
                            bool isCore = (coreMask & (1u << ((pc - chord.root + 12) % 12))) != 0;
                            if (isNew && isCore) loc += 12.0f;
                            else if (isNew) loc += 8.0f;
                            else loc += 2.0f;
                            if (f == 0) loc += 1.2f;
                            loc -= 0.25f * std::abs(f - bf);
                            if (loc > bestLocal) {
                                bestLocal = loc;
                                bestF = f;
                            }
                        }
                        if (bestF >= 0) {
                            cur[s] = bestF;
                            covered |= static_cast<uint16_t>(1u << ((openMidi[s] + bestF) % 12));
                        }
                    }

                    int sounding = 0;
                    int minFret = 99, maxFret = 0;
                    int internalMutes = 0;
                    bool seenSounding = false;
                    int lastSounding = -1;
                    for (int s = 0; s < 6; ++s) {
                        if (cur[s] >= 0) {
                            ++sounding;
                            seenSounding = true;
                            lastSounding = s;
                            if (cur[s] > 0) {
                                minFret = std::min(minFret, cur[s]);
                                maxFret = std::max(maxFret, cur[s]);
                            }
                        } else if (seenSounding) {
                            ++internalMutes;
                        }
                    }
                    for (int s = lastSounding + 1; s < 6; ++s)
                        if (cur[s] < 0) --internalMutes;

                    if (sounding < 3) continue;
                    int span = (maxFret > 0 && minFret < 99) ? (maxFret - minFret) : 0;
                    if (span > 4) continue;

                    int uniqueCount = 0;
                    int coreCovered = 0;
                    int totalCore = 0;
                    for (int pc = 0; pc < 12; ++pc) {
                        if (covered & (1u << pc)) ++uniqueCount;
                        int rel = (pc - chord.root + 12) % 12;
                        if (coreMask & (1u << rel)) {
                            ++totalCore;
                            if (covered & (1u << pc)) ++coreCovered;
                        }
                    }

                    float score = 0.0f;
                    score += coreCovered * 15.0f;
                    score += uniqueCount * 6.0f;
                    score += sounding * 2.5f;
                    score -= internalMutes * 4.0f;
                    score -= span * 1.5f;
                    score -= (winStart - sMin) * 1.1f;
                    if (coreCovered == totalCore) score += 10.0f;
                    if (bassStr == 0 && sounding >= 5) score += 2.0f;

                    if (score > bestScore) {
                        bestScore = score;
                        result.frets = cur;
                        result.baseFret = (maxFret <= 4) ? 1 : minFret;
                        result.valid = true;
                    }
                }
            }
        }
        return result;
    };

    best = searchRange(startMin, startMax, forceNoOpen);
    if (!best.valid && posFilter != FretPositionFilter::Auto)
        best = searchRange(1, 10, false);
    return best;
}

struct ChordHistoryEntry {
    ChordSymbol chord;
    std::string roman;
    double barPosition = 1.0;
    double durationBeats = 4.0;
    std::vector<int> midiNotes;
};

struct BandLinkState {
    bool connected = false;
    int busIndex = -1; // -1=Off, 0..7 = A..H
    bool isPlaying = false;
    double bpm = 120.0;
    int timeSigNum = 4;
    int timeSigDen = 4;
    int currentBar = 1;
    float beatInBar = 0.0f;
    int activeChartBarIndex = -1;
    std::string styleName;
    std::string sectionLabel;
    bool clashAvoidanceActive = false;
    bool reharmPendingActive = false;
    ChordSymbol activeChord;
    ChordSymbol nextChord;
    float beatsUntilNextChord = 0.0f;
    std::array<std::array<bool, 128>, 6> slotNotes{};
    std::array<bool, 6> slotActive{};
    std::vector<ChordHistoryEntry> bandChart;
};

struct AnalysisSnapshot {
    ChordSymbol currentChord;
    KeyEstimate currentKey;
    std::string currentRoman = "-";
    ScaleMode activeMode = ScaleMode::Ionian;
    uint16_t scalePitchClassMask = 0;
    uint16_t avoidPitchClassMask = 0;
    std::array<float, 12> chroma{};
    std::array<float, 72> noteSalience{}; // MIDI 24 (C1) .. MIDI 95 (B6)
    std::array<bool, 128> activeNotes{};
    std::array<bool, 128> sidechainNotes{};
    std::array<bool, 128> clashingNotes{};
    std::vector<ClashInfo> clashes;
    float clashSeverity = 0.0f;
    bool sidechainActive = false;
    bool clashSourceIsBand = false;
    float userActivityLevel = 0.0f;
    float inputLevelDb = -100.0f;
    float sidechainLevelDb = -100.0f;
    std::array<SubstitutionSuggestion, 5> substitutions;
    std::vector<ChordHistoryEntry> history;
    BandLinkState bandLink;
};

// Standard MIDI File (SMF Format 0) builder — zero external dependencies, 100% DAW compatible
inline std::vector<uint8_t> buildMidiFileBytes(const std::vector<ChordHistoryEntry>& history,
                                               double bpm = 120.0,
                                               bool preferFlats = false) {
    auto writeU16BE = [](std::vector<uint8_t>& buf, uint16_t v) {
        buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        buf.push_back(static_cast<uint8_t>(v & 0xFF));
    };
    auto writeU32BE = [](std::vector<uint8_t>& buf, uint32_t v) {
        buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
        buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        buf.push_back(static_cast<uint8_t>(v & 0xFF));
    };
    auto writeVLQ = [](std::vector<uint8_t>& buf, uint32_t value) {
        uint8_t tmp[5];
        int n = 0;
        tmp[n++] = static_cast<uint8_t>(value & 0x7F);
        while ((value >>= 7) != 0)
            tmp[n++] = static_cast<uint8_t>((value & 0x7F) | 0x80);
        for (int i = n - 1; i >= 0; --i)
            buf.push_back(tmp[i]);
    };

    constexpr uint16_t kTicksPerQuarter = 480;
    std::vector<uint8_t> track;

    const std::string trackName = "HarmonyHUD Chords";
    writeVLQ(track, 0);
    track.push_back(0xFF);
    track.push_back(0x03);
    writeVLQ(track, static_cast<uint32_t>(trackName.size()));
    track.insert(track.end(), trackName.begin(), trackName.end());

    double clampedBpm = std::clamp(bpm, 30.0, 300.0);
    uint32_t usPerQuarter = static_cast<uint32_t>(std::llround(60000000.0 / clampedBpm));
    writeVLQ(track, 0);
    track.push_back(0xFF);
    track.push_back(0x51);
    track.push_back(0x03);
    track.push_back(static_cast<uint8_t>((usPerQuarter >> 16) & 0xFF));
    track.push_back(static_cast<uint8_t>((usPerQuarter >> 8) & 0xFF));
    track.push_back(static_cast<uint8_t>(usPerQuarter & 0xFF));

    writeVLQ(track, 0);
    track.push_back(0xFF);
    track.push_back(0x58);
    track.push_back(0x04);
    track.push_back(0x04);
    track.push_back(0x02);
    track.push_back(0x18);
    track.push_back(0x08);

    for (const auto& entry : history) {
        if (!entry.chord.isValid())
            continue;
        auto notes = entry.midiNotes.empty() ? entry.chord.voicingMidiNotes(3) : entry.midiNotes;
        if (notes.empty())
            continue;

        std::string label = entry.chord.name(preferFlats);
        if (!entry.roman.empty() && entry.roman != "-")
            label += " (" + entry.roman + ")";
        writeVLQ(track, 0);
        track.push_back(0xFF);
        track.push_back(0x06);
        writeVLQ(track, static_cast<uint32_t>(label.size()));
        track.insert(track.end(), label.begin(), label.end());

        for (int n : notes) {
            if (n < 0 || n > 127) continue;
            writeVLQ(track, 0);
            track.push_back(0x90);
            track.push_back(static_cast<uint8_t>(n));
            track.push_back(0x5A);
        }

        double beats = std::clamp(entry.durationBeats, 0.25, 16.0);
        uint32_t durTicks = static_cast<uint32_t>(std::llround(beats * kTicksPerQuarter));

        bool firstOff = true;
        for (int n : notes) {
            if (n < 0 || n > 127) continue;
            writeVLQ(track, firstOff ? durTicks : 0);
            firstOff = false;
            track.push_back(0x80);
            track.push_back(static_cast<uint8_t>(n));
            track.push_back(0x00);
        }
    }

    writeVLQ(track, 0);
    track.push_back(0xFF);
    track.push_back(0x2F);
    track.push_back(0x00);

    std::vector<uint8_t> smf;
    smf.reserve(14 + 8 + track.size());
    smf.insert(smf.end(), {'M', 'T', 'h', 'd'});
    writeU32BE(smf, 6);
    writeU16BE(smf, 0);
    writeU16BE(smf, 1);
    writeU16BE(smf, kTicksPerQuarter);
    smf.insert(smf.end(), {'M', 'T', 'r', 'k'});
    writeU32BE(smf, static_cast<uint32_t>(track.size()));
    smf.insert(smf.end(), track.begin(), track.end());

    return smf;
}

inline std::string buildChordChartText(const std::vector<ChordHistoryEntry>& history,
                                       const KeyEstimate& key) {
    std::ostringstream oss;
    oss << "HarmonyHUD Chord Chart\n";
    oss << "Key: " << key.name() << " (" << static_cast<int>(std::round(key.confidence * 100.0f)) << "%)\n";
    if (history.empty()) {
        oss << "| (No chords captured yet) |\n";
        return oss.str();
    }
    bool flats = key.preferFlats();
    int col = 0;
    for (const auto& e : history) {
        if (col == 0) oss << "| ";
        oss << e.chord.name(flats);
        if (!e.roman.empty() && e.roman != "-")
            oss << " (" << e.roman << ")";
        oss << " | ";
        if (++col >= 4) {
            oss << "\n";
            col = 0;
        }
    }
    if (col != 0) oss << "\n";
    return oss.str();
}

} // namespace harmonyhud
