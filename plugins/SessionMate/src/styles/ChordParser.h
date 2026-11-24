#pragma once

#include "../SessionMateTypes.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace sessionmate {

struct VoicingResult {
    std::array<int, 6> notes {};
    int count = 0;
};

namespace detail {

inline void fillChordIntervals(ChordSymbol& chord) noexcept {
    chord.intervals.fill(-1);
    auto set = [&](std::initializer_list<int> list) noexcept {
        int i = 0;
        for (int v : list) {
            if (i < 7) chord.intervals[static_cast<size_t>(i++)] = v;
        }
        chord.numTones = i;
    };

    switch (chord.quality) {
        case ChordQuality::Major:           set({ 0, 4, 7 }); break;
        case ChordQuality::Minor:           set({ 0, 3, 7 }); break;
        case ChordQuality::Dominant7:       set({ 0, 4, 7, 10 }); break;
        case ChordQuality::Major7:          set({ 0, 4, 7, 11 }); break;
        case ChordQuality::Minor7:          set({ 0, 3, 7, 10 }); break;
        case ChordQuality::Minor7b5:        set({ 0, 3, 6, 10 }); break;
        case ChordQuality::Diminished:      set({ 0, 3, 6 }); break;
        case ChordQuality::Diminished7:     set({ 0, 3, 6, 9 }); break;
        case ChordQuality::Augmented:       set({ 0, 4, 8 }); break;
        case ChordQuality::Sus2:            set({ 0, 2, 7 }); break;
        case ChordQuality::Sus4:            set({ 0, 5, 7 }); break;
        case ChordQuality::Major6:          set({ 0, 4, 7, 9 }); break;
        case ChordQuality::Minor6:          set({ 0, 3, 7, 9 }); break;
        case ChordQuality::Dominant9:       set({ 0, 4, 7, 10, 14 }); break;
        case ChordQuality::Major9:          set({ 0, 4, 7, 11, 14 }); break;
        case ChordQuality::Minor9:          set({ 0, 3, 7, 10, 14 }); break;
        case ChordQuality::Dominant11:      set({ 0, 4, 7, 10, 14, 17 }); break;
        case ChordQuality::Minor11:         set({ 0, 3, 7, 10, 14, 17 }); break;
        case ChordQuality::Dominant13:      set({ 0, 4, 7, 10, 14, 21 }); break;
        case ChordQuality::Major13:         set({ 0, 4, 7, 11, 14, 21 }); break;
        case ChordQuality::Minor13:         set({ 0, 3, 7, 10, 14, 21 }); break;
        case ChordQuality::Dominant7Sharp9: set({ 0, 4, 7, 10, 15 }); break;
        case ChordQuality::Dominant7Flat9:  set({ 0, 4, 7, 10, 13 }); break;
        case ChordQuality::Power5:          set({ 0, 7 }); break;
        case ChordQuality::Add9:            set({ 0, 4, 7, 14 }); break;
    }
}

inline int parsePitchClass(const std::string& s, size_t& pos) noexcept {
    if (pos >= s.size()) return -1;
    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(s[pos])));
    int pc = -1;
    switch (c) {
        case 'C': pc = 0; break;
        case 'D': pc = 2; break;
        case 'E': pc = 4; break;
        case 'F': pc = 5; break;
        case 'G': pc = 7; break;
        case 'A': pc = 9; break;
        case 'B': pc = 11; break;
        default: return -1;
    }
    ++pos;
    while (pos < s.size()) {
        if (s[pos] == '#') {
            pc = (pc + 1) % 12;
            ++pos;
        } else if (s[pos] == 'b') {
            pc = (pc + 11) % 12;
            ++pos;
        } else {
            break;
        }
    }
    return pc;
}

inline std::string trimWhitespace(const std::string& in) {
    size_t start = 0;
    while (start < in.size() && std::isspace(static_cast<unsigned char>(in[start]))) ++start;
    size_t end = in.size();
    while (end > start && std::isspace(static_cast<unsigned char>(in[end - 1]))) --end;
    return in.substr(start, end - start);
}

inline const char* qualitySuffix(ChordQuality q) noexcept {
    switch (q) {
        case ChordQuality::Major:           return "";
        case ChordQuality::Minor:           return "m";
        case ChordQuality::Dominant7:       return "7";
        case ChordQuality::Major7:          return "maj7";
        case ChordQuality::Minor7:          return "m7";
        case ChordQuality::Minor7b5:        return "m7b5";
        case ChordQuality::Diminished:      return "dim";
        case ChordQuality::Diminished7:     return "dim7";
        case ChordQuality::Augmented:       return "aug";
        case ChordQuality::Sus2:            return "sus2";
        case ChordQuality::Sus4:            return "sus4";
        case ChordQuality::Major6:          return "6";
        case ChordQuality::Minor6:          return "m6";
        case ChordQuality::Dominant9:       return "9";
        case ChordQuality::Major9:          return "maj9";
        case ChordQuality::Minor9:          return "m9";
        case ChordQuality::Dominant11:      return "11";
        case ChordQuality::Minor11:         return "m11";
        case ChordQuality::Dominant13:      return "13";
        case ChordQuality::Major13:         return "maj13";
        case ChordQuality::Minor13:         return "m13";
        case ChordQuality::Dominant7Sharp9: return "7#9";
        case ChordQuality::Dominant7Flat9:  return "7b9";
        case ChordQuality::Power5:          return "5";
        case ChordQuality::Add9:            return "add9";
    }
    return "";
}

inline void sortVoicingAscending(VoicingResult& v) noexcept {
    for (int i = 1; i < v.count; ++i) {
        int key = v.notes[static_cast<size_t>(i)];
        int j = i - 1;
        while (j >= 0 && v.notes[static_cast<size_t>(j)] > key) {
            v.notes[static_cast<size_t>(j + 1)] = v.notes[static_cast<size_t>(j)];
            --j;
        }
        v.notes[static_cast<size_t>(j + 1)] = key;
    }
}

struct ScaleModeDef {
    std::array<int, 7> intervals {};
    int length = 7;
};

inline ScaleModeDef scaleModeDef(ScaleMode mode) noexcept {
    switch (mode) {
        case ScaleMode::Ionian:          return { { 0, 2, 4, 5, 7, 9, 11 }, 7 };
        case ScaleMode::Dorian:          return { { 0, 2, 3, 5, 7, 9, 10 }, 7 };
        case ScaleMode::Phrygian:        return { { 0, 1, 3, 5, 7, 8, 10 }, 7 };
        case ScaleMode::Lydian:          return { { 0, 2, 4, 6, 7, 9, 11 }, 7 };
        case ScaleMode::Mixolydian:      return { { 0, 2, 4, 5, 7, 9, 10 }, 7 };
        case ScaleMode::Aeolian:         return { { 0, 2, 3, 5, 7, 8, 10 }, 7 };
        case ScaleMode::Locrian:         return { { 0, 1, 3, 5, 6, 8, 10 }, 7 };
        case ScaleMode::HarmonicMinor:   return { { 0, 2, 3, 5, 7, 8, 11 }, 7 };
        case ScaleMode::MelodicMinor:    return { { 0, 2, 3, 5, 7, 9, 11 }, 7 };
        case ScaleMode::Altered:         return { { 0, 1, 3, 4, 6, 8, 10 }, 7 };
        case ScaleMode::PentatonicMajor: return { { 0, 2, 4, 7, 9, 0,  0  }, 5 };
        case ScaleMode::PentatonicMinor: return { { 0, 3, 5, 7, 10, 0, 0  }, 5 };
        case ScaleMode::Blues:           return { { 0, 3, 5, 6, 7, 10, 0  }, 6 };
    }
    return { { 0, 2, 4, 5, 7, 9, 11 }, 7 };
}

inline void deduplicateVoicing(VoicingResult& v) noexcept {
    if (v.count <= 1) return;
    sortVoicingAscending(v);
    int writeIdx = 1;
    for (int i = 1; i < v.count; ++i) {
        if (v.notes[static_cast<size_t>(i)] != v.notes[static_cast<size_t>(writeIdx - 1)]) {
            v.notes[static_cast<size_t>(writeIdx++)] = v.notes[static_cast<size_t>(i)];
        }
    }
    v.count = writeIdx;
}

} // namespace detail

inline ChordSymbol parseChord(const std::string& text) {
    ChordSymbol result {};
    result.rootPitchClass = -1;
    result.bassPitchClass = -1;
    result.quality = ChordQuality::Major;
    result.valid = false;
    result.intervals.fill(-1);
    result.numTones = 0;

    std::string s = detail::trimWhitespace(text);
    if (s.empty()) return result;

    // Check for N.C. (No Chord)
    std::string upper;
    upper.reserve(s.size());
    for (char c : s) upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    if (upper == "N.C." || upper == "NC" || upper == "N.C" || upper == "NOCHORD" || upper == "-" || upper == "X") {
        return result;
    }

    // Split optional slash bass
    std::string body = s;
    std::string bassPart;
    const size_t slashPos = s.find('/');
    if (slashPos != std::string::npos) {
        body = s.substr(0, slashPos);
        bassPart = detail::trimWhitespace(s.substr(slashPos + 1));
    }

    size_t pos = 0;
    const int root = detail::parsePitchClass(body, pos);
    if (root < 0) return result;

    result.rootPitchClass = root;
    result.valid = true;

    std::string qual = body.substr(pos);
    // Keep case-sensitive check for 'M' (major) vs 'm' (minor), plus lowercase version
    std::string qualLower;
    qualLower.reserve(qual.size());
    for (char c : qual) {
        if (!std::isspace(static_cast<unsigned char>(c)) && c != '(' && c != ')') {
            qualLower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }

    // Also strip whitespace/parens from case-sensitive qual
    std::string qualClean;
    qualClean.reserve(qual.size());
    for (char c : qual) {
        if (!std::isspace(static_cast<unsigned char>(c)) && c != '(' && c != ')') {
            qualClean.push_back(c);
        }
    }

    if (qualClean.empty()) {
        result.quality = ChordQuality::Major;
    } else if (qualClean == "M7" || qualLower == "maj7" || qualLower == "ma7") {
        result.quality = ChordQuality::Major7;
    } else if (qualClean == "M9" || qualLower == "maj9" || qualLower == "ma9") {
        result.quality = ChordQuality::Major9;
    } else if (qualClean == "M13" || qualLower == "maj13" || qualLower == "ma13") {
        result.quality = ChordQuality::Major13;
    } else if (qualClean == "M" || qualLower == "maj") {
        result.quality = ChordQuality::Major;
    } else if (qualLower == "m7b5" || qualLower == "min7b5" || qualLower == "-7b5") {
        result.quality = ChordQuality::Minor7b5;
    } else if (qualLower == "m13" || qualLower == "min13" || qualLower == "-13") {
        result.quality = ChordQuality::Minor13;
    } else if (qualLower == "m11" || qualLower == "min11" || qualLower == "-11") {
        result.quality = ChordQuality::Minor11;
    } else if (qualLower == "m9" || qualLower == "min9" || qualLower == "-9") {
        result.quality = ChordQuality::Minor9;
    } else if (qualLower == "m7" || qualLower == "min7" || qualLower == "-7") {
        result.quality = ChordQuality::Minor7;
    } else if (qualLower == "m6" || qualLower == "min6" || qualLower == "-6") {
        result.quality = ChordQuality::Minor6;
    } else if (qualLower == "m" || qualLower == "min" || qualLower == "-") {
        result.quality = ChordQuality::Minor;
    } else if (qualLower == "7#9" || qualLower == "7+9" || qualLower == "7alt") {
        result.quality = ChordQuality::Dominant7Sharp9;
    } else if (qualLower == "7b9" || qualLower == "7-9") {
        result.quality = ChordQuality::Dominant7Flat9;
    } else if (qualLower == "dim7" || qualLower == "o7") {
        result.quality = ChordQuality::Diminished7;
    } else if (qualLower == "dim" || qualLower == "o") {
        result.quality = ChordQuality::Diminished;
    } else if (qualLower == "aug" || qualLower == "+") {
        result.quality = ChordQuality::Augmented;
    } else if (qualLower == "sus2" || qualLower == "2") {
        result.quality = ChordQuality::Sus2;
    } else if (qualLower == "sus4" || qualLower == "sus" || qualLower == "7sus4") {
        result.quality = ChordQuality::Sus4;
    } else if (qualLower == "add9" || qualLower == "add2") {
        result.quality = ChordQuality::Add9;
    } else if (qualLower == "13") {
        result.quality = ChordQuality::Dominant13;
    } else if (qualLower == "11") {
        result.quality = ChordQuality::Dominant11;
    } else if (qualLower == "9") {
        result.quality = ChordQuality::Dominant9;
    } else if (qualLower == "7" || qualLower == "dom7") {
        result.quality = ChordQuality::Dominant7;
    } else if (qualLower == "6") {
        result.quality = ChordQuality::Major6;
    } else if (qualLower == "5") {
        result.quality = ChordQuality::Power5;
    } else {
        // Fallback heuristic for compound extensions
        if (qualLower.find("m7b5") != std::string::npos) result.quality = ChordQuality::Minor7b5;
        else if (qualLower.find("maj7") != std::string::npos) result.quality = ChordQuality::Major7;
        else if (qualLower.find("m7") != std::string::npos) result.quality = ChordQuality::Minor7;
        else if (qualLower[0] == 'm' || qualLower[0] == '-') result.quality = ChordQuality::Minor;
        else if (qualLower.find('7') != std::string::npos) result.quality = ChordQuality::Dominant7;
        else result.quality = ChordQuality::Major;
    }

    if (!bassPart.empty()) {
        size_t bpos = 0;
        const int bassPc = detail::parsePitchClass(bassPart, bpos);
        if (bassPc >= 0) result.bassPitchClass = bassPc;
    }

    detail::fillChordIntervals(result);
    return result;
}

inline std::string formatChord(const ChordSymbol& chord, bool preferFlats = false) {
    if (!chord.valid || chord.rootPitchClass < 0) return "N.C.";
    std::string out = pitchClassName(chord.rootPitchClass, preferFlats);
    out += detail::qualitySuffix(chord.quality);
    if (chord.bassPitchClass >= 0 && chord.bassPitchClass != chord.rootPitchClass) {
        out += "/";
        out += pitchClassName(chord.bassPitchClass, preferFlats);
    }
    return out;
}

inline std::vector<ChordSymbol> parseChordTrack(const std::string& chartText) {
    std::vector<ChordSymbol> chords;
    chords.reserve(16);

    std::string token;
    auto flushToken = [&]() {
        std::string trimmed = detail::trimWhitespace(token);
        token.clear();
        if (trimmed.empty()) return;
        if (static_cast<int>(chords.size()) >= kMaxChordBars) return;

        if (trimmed == "%" && !chords.empty()) {
            chords.push_back(chords.back());
            return;
        }
        ChordSymbol c = parseChord(trimmed);
        // Accept valid chords or explicit N.C.
        std::string up;
        for (char ch : trimmed) up.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        if (c.valid || up == "N.C." || up == "NC" || up == "N.C") {
            chords.push_back(c);
        }
    };

    for (char c : chartText) {
        if (c == '|' || c == ',' || c == ';' || c == '\n' || c == '\r' || c == '\t' || c == ' ') {
            flushToken();
        } else {
            token.push_back(c);
        }
    }
    flushToken();
    return chords;
}

inline std::string formatChordTrack(const std::vector<ChordSymbol>& chords) {
    if (chords.empty()) return "";
    std::string out = "|";
    for (const auto& c : chords) {
        out += " ";
        out += formatChord(c, false);
        out += " |";
    }
    return out;
}

inline int scaleNoteForChord(const ChordSymbol& chord, int degree, int octaveBase) noexcept {
    if (!chord.valid || chord.rootPitchClass < 0) {
        return std::clamp(octaveBase, 0, 127);
    }

    static constexpr std::array<int, 7> kIonian     = { 0, 2, 4, 5, 7, 9, 11 };
    static constexpr std::array<int, 7> kDorian     = { 0, 2, 3, 5, 7, 9, 10 };
    static constexpr std::array<int, 7> kMixolydian = { 0, 2, 4, 5, 7, 9, 10 };
    static constexpr std::array<int, 7> kLocrian    = { 0, 1, 3, 5, 6, 8, 10 };
    static constexpr std::array<int, 7> kDiminished = { 0, 2, 3, 5, 6, 8, 9 };
    static constexpr std::array<int, 7> kLydianAug  = { 0, 2, 4, 6, 8, 9, 11 };
    static constexpr std::array<int, 7> kAltered    = { 0, 1, 3, 4, 6, 8, 10 };
    static constexpr std::array<int, 7> kAeolian    = { 0, 2, 3, 5, 7, 8, 10 };

    const std::array<int, 7>* scale = &kIonian;
    switch (chord.quality) {
        case ChordQuality::Major:
        case ChordQuality::Major7:
        case ChordQuality::Major9:
        case ChordQuality::Major13:
        case ChordQuality::Major6:
        case ChordQuality::Add9:
            scale = &kIonian;
            break;
        case ChordQuality::Minor:
        case ChordQuality::Minor7:
        case ChordQuality::Minor9:
        case ChordQuality::Minor11:
        case ChordQuality::Minor13:
        case ChordQuality::Minor6:
            scale = &kDorian;
            break;
        case ChordQuality::Dominant7:
        case ChordQuality::Dominant9:
        case ChordQuality::Dominant11:
        case ChordQuality::Dominant13:
        case ChordQuality::Sus2:
        case ChordQuality::Sus4:
            scale = &kMixolydian;
            break;
        case ChordQuality::Minor7b5:
            scale = &kLocrian;
            break;
        case ChordQuality::Diminished:
        case ChordQuality::Diminished7:
            scale = &kDiminished;
            break;
        case ChordQuality::Augmented:
            scale = &kLydianAug;
            break;
        case ChordQuality::Dominant7Sharp9:
        case ChordQuality::Dominant7Flat9:
            scale = &kAltered;
            break;
        case ChordQuality::Power5:
            scale = &kAeolian;
            break;
    }

    int octShift = 0;
    int idx = degree;
    while (idx < 0) { idx += 7; --octShift; }
    while (idx >= 7) { idx -= 7; ++octShift; }

    const int midi = octaveBase + chord.rootPitchClass + (*scale)[static_cast<size_t>(idx)] + octShift * 12;
    return std::clamp(midi, 0, 127);
}

inline uint16_t scaleModeMask(int rootPc, ScaleMode mode) noexcept {
    if (rootPc < 0) return 0;
    const int r = ((rootPc % 12) + 12) % 12;
    const detail::ScaleModeDef def = detail::scaleModeDef(mode);
    uint16_t mask = 0;
    for (int i = 0; i < def.length; ++i) {
        const int pc = (r + def.intervals[static_cast<size_t>(i)]) % 12;
        mask |= static_cast<uint16_t>(1u << pc);
    }
    return mask;
}

inline int scaleNoteInMode(int rootPc, ScaleMode mode, int degree, int octaveBase) noexcept {
    if (rootPc < 0) return std::clamp(octaveBase, 0, 127);
    const int r = ((rootPc % 12) + 12) % 12;
    const detail::ScaleModeDef def = detail::scaleModeDef(mode);
    const int len = std::max(1, def.length);

    int octShift = 0;
    int idx = degree;
    while (idx < 0) { idx += len; --octShift; }
    while (idx >= len) { idx -= len; ++octShift; }

    const int midi = octaveBase + r + def.intervals[static_cast<size_t>(idx)] + octShift * 12;
    return std::clamp(midi, 0, 127);
}

#ifdef _MSC_VER
#define SM_NOINLINE __declspec(noinline)
#else
#define SM_NOINLINE __attribute__((noinline))
#endif

SM_NOINLINE inline VoicingResult voiceLeadKeys(const ChordSymbol& chord, const VoicingResult& prevVoicing,
                                               int lowMidi = 48, int highMidi = 76,
                                               KeysVoicingStyle style = KeysVoicingStyle::Close,
                                               uint16_t avoidPcMask = 0) noexcept {
    VoicingResult best {};
    if (!chord.valid || chord.rootPitchClass < 0 || chord.numTones <= 0) return best;

    if (lowMidi > highMidi) std::swap(lowMidi, highMidi);

    const int effLow = (style == KeysVoicingStyle::OpenSpread) ? std::max(24, lowMidi - 5) : lowMidi;
    const int effHigh = (style == KeysVoicingStyle::OpenSpread) ? std::min(108, highMidi + 5) : highMidi;
    const int rootPc = ((chord.rootPitchClass % 12) + 12) % 12;

    std::array<int, 5> pcs {};
    int numPcs = 0;
    auto pushPc = [&](int pc) noexcept {
        pc = ((pc % 12) + 12) % 12;
        if (numPcs >= 5) return;
        for (int k = 0; k < numPcs; ++k) {
            if (pcs[static_cast<size_t>(k)] == pc) return;
        }
        pcs[static_cast<size_t>(numPcs++)] = pc;
    };

    if (style == KeysVoicingStyle::RootlessJazz) {
        // Bill Evans rootless voicing: omit root pitch class, build 3rd/5th/7th(or 6th)/9th
        auto pushNonRoot = [&](int semitones) noexcept {
            const int pc = (rootPc + semitones) % 12;
            if (pc != rootPc) pushPc(pc);
        };
        switch (chord.quality) {
            case ChordQuality::Major:
            case ChordQuality::Add9:
                pushNonRoot(4); pushNonRoot(7); pushNonRoot(9); pushNonRoot(2);
                break;
            case ChordQuality::Minor:
                pushNonRoot(3); pushNonRoot(7); pushNonRoot(9); pushNonRoot(2);
                break;
            case ChordQuality::Sus2:
                pushNonRoot(2); pushNonRoot(7); pushNonRoot(9); pushNonRoot(10);
                break;
            case ChordQuality::Sus4:
                pushNonRoot(5); pushNonRoot(7); pushNonRoot(9); pushNonRoot(2);
                break;
            case ChordQuality::Diminished:
                pushNonRoot(3); pushNonRoot(6); pushNonRoot(9);
                break;
            case ChordQuality::Augmented:
                pushNonRoot(4); pushNonRoot(8); pushNonRoot(2);
                break;
            case ChordQuality::Power5:
                pushNonRoot(7); pushNonRoot(2);
                break;
            case ChordQuality::Dominant11:
            case ChordQuality::Minor11:
            case ChordQuality::Dominant13:
            case ChordQuality::Major13:
            case ChordQuality::Minor13:
                // 3rd, 7th, 9th, 11th/13th
                pushNonRoot(chord.intervals[1]);
                pushNonRoot(chord.intervals[3]);
                pushNonRoot(chord.intervals[4]);
                pushNonRoot(chord.intervals[5]);
                break;
            default: {
                for (int i = 1; i < chord.numTones && numPcs < 4; ++i) {
                    pushNonRoot(chord.intervals[static_cast<size_t>(i)]);
                }
                if (numPcs < 4) pushNonRoot(2); // add 9th for lush 4-note rootless
                break;
            }
        }
    } else if (style == KeysVoicingStyle::Shell37) {
        // Minimalist 3-note shell: root + 3rd + 7th/6th (or 5th for triads)
        pushPc(rootPc);
        if (chord.numTones >= 2) pushPc(rootPc + chord.intervals[1]);
        if (chord.numTones >= 4) pushPc(rootPc + chord.intervals[3]);
        else if (chord.numTones >= 3) pushPc(rootPc + chord.intervals[2]);
    } else {
        // Close, Drop2, OpenSpread: select up to 5 essential pitch classes (4 max for Drop2)
        const int maxPcs = (style == KeysVoicingStyle::Drop2) ? 4 : 5;
        if (chord.numTones <= maxPcs) {
            for (int i = 0; i < chord.numTones && numPcs < maxPcs; ++i) {
                pushPc(rootPc + chord.intervals[static_cast<size_t>(i)]);
            }
        } else if (maxPcs == 4) {
            // Root, 3rd, 7th, 9th/13th (omit 5th)
            static constexpr int kPick4[4] = { 0, 1, 3, 4 };
            for (int idx : kPick4) {
                if (idx < chord.numTones) pushPc(rootPc + chord.intervals[static_cast<size_t>(idx)]);
            }
        } else {
            // 6-tone chords: keep root, 3rd, 7th, 9th, 11th/13th (skip 5th at index 2)
            static constexpr int kPick5[5] = { 0, 1, 3, 4, 5 };
            for (int idx : kPick5) {
                if (idx < chord.numTones) pushPc(rootPc + chord.intervals[static_cast<size_t>(idx)]);
            }
        }
    }

    if (numPcs <= 0) return best;

    const int centerMidi = (effLow + effHigh) / 2;
    float bestScore = 1.0e9f;

    auto scoreCandidate = [&](const VoicingResult& cand, float extraPenalty) noexcept -> float {
        float penalty = extraPenalty;
        for (int i = 1; i < cand.count; ++i) {
            const int gap = cand.notes[static_cast<size_t>(i)] - cand.notes[static_cast<size_t>(i - 1)];
            if (gap <= 1 && cand.notes[static_cast<size_t>(i - 1)] < 55) penalty += 12.0f;
        }
        const int span = cand.notes[static_cast<size_t>(cand.count - 1)] - cand.notes[0];
        const int maxSpan = (style == KeysVoicingStyle::OpenSpread) ? 28
                          : (style == KeysVoicingStyle::Drop2 ? 21 : 17);
        if (span > maxSpan) penalty += static_cast<float>(span - maxSpan) * 1.5f;

        float score = penalty;
        if (prevVoicing.count > 0) {
            float l1 = 0.0f;
            const int paired = std::min(cand.count, prevVoicing.count);
            for (int i = 0; i < paired; ++i) {
                l1 += static_cast<float>(std::abs(cand.notes[static_cast<size_t>(i)] - prevVoicing.notes[static_cast<size_t>(i)]));
            }
            // Extra nearest-neighbor term when voice counts differ
            if (cand.count != prevVoicing.count) {
                for (int i = paired; i < cand.count; ++i) {
                    int minDist = 127;
                    for (int j = 0; j < prevVoicing.count; ++j) {
                        const int d = std::abs(cand.notes[static_cast<size_t>(i)] - prevVoicing.notes[static_cast<size_t>(j)]);
                        if (d < minDist) minDist = d;
                    }
                    l1 += static_cast<float>(minDist);
                }
            }
            score += l1;
        } else {
            float avg = 0.0f;
            for (int i = 0; i < cand.count; ++i) avg += static_cast<float>(cand.notes[static_cast<size_t>(i)]);
            avg /= static_cast<float>(cand.count);
            score += std::fabs(avg - static_cast<float>(centerMidi));
        }
        return score;
    };

    if (style == KeysVoicingStyle::OpenSpread) {
        // Wide open voicing across effLow..effHigh: root+5th low, 3rd+7th/9th upper
        const int fifthInterval = (chord.numTones >= 3) ? chord.intervals[2] : 7;
        const int fifthPc = (rootPc + fifthInterval) % 12;

        std::array<int, 4> upperPcs {};
        int numUpper = 0;
        for (int i = 0; i < numPcs && numUpper < 4; ++i) {
            if (pcs[static_cast<size_t>(i)] != rootPc && pcs[static_cast<size_t>(i)] != fifthPc) {
                upperPcs[static_cast<size_t>(numUpper++)] = pcs[static_cast<size_t>(i)];
            }
        }
        if (numUpper == 0 && numPcs >= 2) {
            upperPcs[static_cast<size_t>(numUpper++)] = pcs[1];
        }

        for (int baseOct = 2; baseOct <= 5; ++baseOct) {
            const int rootMidi = baseOct * 12 + rootPc;
            if (rootMidi < effLow || rootMidi > effHigh - 12) continue;
            int fifthMidi = rootMidi + ((fifthPc - rootPc + 12) % 12);
            if (fifthMidi == rootMidi) fifthMidi += 7;

            const int upperInvCount = std::max(1, numUpper);
            for (int uInv = 0; uInv < upperInvCount; ++uInv) {
                VoicingResult cand {};
                cand.notes[static_cast<size_t>(cand.count++)] = rootMidi;
                if (fifthMidi <= effHigh && fifthMidi != rootMidi && cand.count < 6) {
                    cand.notes[static_cast<size_t>(cand.count++)] = fifthMidi;
                }

                int prevNote = std::max(fifthMidi + 2, rootMidi + 11);
                for (int u = 0; u < numUpper && cand.count < 6; ++u) {
                    const int pc = upperPcs[static_cast<size_t>((uInv + u) % numUpper)];
                    int note = (prevNote / 12) * 12 + pc;
                    while (note <= prevNote) note += 12;
                    cand.notes[static_cast<size_t>(cand.count++)] = note;
                    prevNote = note;
                }

                detail::sortVoicingAscending(cand);
                if (cand.notes[0] < effLow || cand.notes[static_cast<size_t>(cand.count - 1)] > effHigh) {
                    continue;
                }

                const float score = scoreCandidate(cand, 0.0f);
                if (score < bestScore) {
                    bestScore = score;
                    best = cand;
                }
            }
        }
    } else {
        // Generate inversions for Close (allows close + subtle drop-2 for smooth voice-leading), Drop2, RootlessJazz, Shell37
        const int dropStart = (style == KeysVoicingStyle::Drop2 && numPcs >= 4) ? 1 : 0;
        const int dropEnd   = ((style == KeysVoicingStyle::Drop2 || style == KeysVoicingStyle::Close) && numPcs >= 4) ? 1 : 0;

        for (int inv = 0; inv < numPcs; ++inv) {
            for (int baseOct = 2; baseOct <= 6; ++baseOct) {
                for (int dropMode = dropStart; dropMode <= dropEnd; ++dropMode) {
                    VoicingResult cand {};
                    cand.count = numPcs;
                    int prevNote = baseOct * 12 + pcs[static_cast<size_t>(inv)];
                    cand.notes[0] = prevNote;

                    for (int i = 1; i < numPcs; ++i) {
                        const int pc = pcs[static_cast<size_t>((inv + i) % numPcs)];
                        int note = (prevNote / 12) * 12 + pc;
                        while (note <= prevNote) note += 12;
                        cand.notes[static_cast<size_t>(i)] = note;
                        prevNote = note;
                    }

                    // Drop-2 voicing: drop second-highest note down an octave
                    if (dropMode == 1 && cand.count >= 4) {
                        cand.notes[static_cast<size_t>(cand.count - 2)] -= 12;
                        detail::sortVoicingAscending(cand);
                    }

                    if (cand.notes[0] < effLow || cand.notes[static_cast<size_t>(cand.count - 1)] > effHigh) {
                        continue;
                    }

                    const float score = scoreCandidate(cand, 0.0f);
                    if (score < bestScore) {
                        bestScore = score;
                        best = cand;
                    }
                }
            }
        }
    }

    // Fallback if range was too narrow
    if (best.count == 0) {
        int base = effLow + ((pcs[0] - (effLow % 12) + 12) % 12);
        for (int i = 0; i < numPcs && best.count < 6; ++i) {
            int n = (base / 12) * 12 + pcs[static_cast<size_t>(i)];
            while (n < effLow) n += 12;
            while (n > effHigh) n -= 12;
            best.notes[static_cast<size_t>(best.count++)] = std::clamp(n, 0, 127);
        }
        detail::deduplicateVoicing(best);
    }

    // Auto-Clash Avoidance: remove or shift non-root upper voices hitting avoidPcMask,
    // while keeping at least 2 core chord tones so Keys never goes silent.
    if (avoidPcMask != 0 && best.count > 0) {
        VoicingResult kept {};
        VoicingResult clashing {};
        for (int i = 0; i < best.count; ++i) {
            const int n = best.notes[static_cast<size_t>(i)];
            const int pc = n % 12;
            const bool isRoot = (pc == rootPc) && (style != KeysVoicingStyle::RootlessJazz);
            const bool hitsAvoid = ((avoidPcMask & static_cast<uint16_t>(1u << pc)) != 0);
            if (!hitsAvoid || isRoot) {
                if (kept.count < 6) kept.notes[static_cast<size_t>(kept.count++)] = n;
            } else {
                if (clashing.count < 6) clashing.notes[static_cast<size_t>(clashing.count++)] = n;
            }
        }

        // If removing clashing tones dropped below 2 voices, try shifting them to a safe chord tone
        const int minKeep = std::min(2, best.count);
        if (kept.count < minKeep) {
            for (int c = 0; c < clashing.count && kept.count < minKeep; ++c) {
                const int orig = clashing.notes[static_cast<size_t>(c)];
                int bestReplacement = -1;
                int bestDist = 127;
                for (int t = 0; t < chord.numTones; ++t) {
                    const int pc = (rootPc + chord.intervals[static_cast<size_t>(t)]) % 12;
                    if (style == KeysVoicingStyle::RootlessJazz && pc == rootPc) continue;
                    if ((avoidPcMask & static_cast<uint16_t>(1u << pc)) != 0) continue;
                    // Find nearest octave of pc to orig within [effLow, effHigh]
                    for (int oct = 2; oct <= 7; ++oct) {
                        const int candN = oct * 12 + pc;
                        if (candN < effLow || candN > effHigh) continue;
                        bool dup = false;
                        for (int k = 0; k < kept.count; ++k) {
                            if (kept.notes[static_cast<size_t>(k)] == candN) { dup = true; break; }
                        }
                        if (dup) continue;
                        const int d = std::abs(candN - orig);
                        if (d < bestDist) {
                            bestDist = d;
                            bestReplacement = candN;
                        }
                    }
                }
                if (bestReplacement >= 0 && kept.count < 6) {
                    kept.notes[static_cast<size_t>(kept.count++)] = bestReplacement;
                }
            }
            // Still < minKeep? Restore lowest clashing note(s) so Keys never goes silent
            for (int c = 0; c < clashing.count && kept.count < minKeep; ++c) {
                if (kept.count < 6) kept.notes[static_cast<size_t>(kept.count++)] = clashing.notes[static_cast<size_t>(c)];
            }
        }

        detail::deduplicateVoicing(kept);
        best = kept;
    }

    return best;
}

inline VoicingResult filterAvoidGuitarVoicing(const VoicingResult& in, uint16_t avoidPcMask) noexcept {
    if (avoidPcMask == 0 || in.count <= 2) return in;

    VoicingResult out {};
    // Always preserve lowest bass/root string
    out.notes[static_cast<size_t>(out.count++)] = in.notes[0];

    for (int i = 1; i < in.count; ++i) {
        const int n = in.notes[static_cast<size_t>(i)];
        const int pc = n % 12;
        if ((avoidPcMask & static_cast<uint16_t>(1u << pc)) == 0) {
            if (out.count < 6) out.notes[static_cast<size_t>(out.count++)] = n;
        }
    }

    // Preserve at least 2 notes (lowest bass/root + nearest upper tone)
    if (out.count < 2 && in.count >= 2) {
        out.notes[static_cast<size_t>(out.count++)] = in.notes[1];
    }

    detail::sortVoicingAscending(out);
    return out;
}

inline const std::vector<std::pair<std::string, std::string>>& getFactoryChordProgressions() {
    static const std::vector<std::pair<std::string, std::string>> kProgressions = {
        { "Neo-Soul Pocket",  "| Em9 | A13 | Dmaj7 | B7#9 |" },
        { "ii-V-I Jazz",      "| Dm9 | G13 | Cmaj9 | A7#9 |" },
        { "Pop Anthem",       "| C | G | Am7 | Fmaj7 |" },
        { "12-Bar Blues (E)", "| E7 | A7 | E7 | E7 | A7 | A7 | E7 | E7 | B7 | A7 | E7 | B7#9 |" },
        { "Andalusian",       "| Am | G | Fmaj7 | E7 |" },
        { "Modal Dorian",     "| Dm9 | G13 | Dm9 | Em7 |" },
        { "Funk Vamp",        "| E9 | A13 | E9 | B7#9 |" }
    };
    return kProgressions;
}

SM_NOINLINE inline VoicingResult guitarFretboardVoicing(const ChordSymbol& chord) noexcept {
    VoicingResult best {};
    if (!chord.valid || chord.rootPitchClass < 0 || chord.numTones <= 0) return best;

    // Standard 6-string tuning: E2, A2, D3, G3, B3, E4
    static constexpr std::array<int, 6> kOpenStrings = { 40, 45, 50, 55, 59, 64 };

    // Build chord pitch-class bitmask and identify essential tones (3rd/sus, 7th/6th, extension)
    uint16_t chordMask = 0;
    for (int i = 0; i < chord.numTones; ++i) {
        const int pc = (chord.rootPitchClass + chord.intervals[static_cast<size_t>(i)]) % 12;
        chordMask |= static_cast<uint16_t>(1u << pc);
    }
    const int bassPc = chord.effectiveBass();
    chordMask |= static_cast<uint16_t>(1u << bassPc);

    int thirdPc = -1;
    int seventhPc = -1;
    if (chord.numTones >= 2) thirdPc = (chord.rootPitchClass + chord.intervals[1]) % 12;
    if (chord.numTones >= 4) seventhPc = (chord.rootPitchClass + chord.intervals[3]) % 12;

    float bestScore = -1.0e9f;

    // Evaluate realistic fretboard windows (open position + movable 4-fret spans up to fret 10)
    for (int minFret = 0; minFret <= 9; ++minFret) {
        const int maxFret = minFret + 4;
        // Try bass on string 0 (E), string 1 (A), or string 2 (D)
        for (int bassStr = 0; bassStr <= 2; ++bassStr) {
            int bassFret = -1;
            for (int f = minFret; f <= maxFret; ++f) {
                if (((kOpenStrings[static_cast<size_t>(bassStr)] + f) % 12) == bassPc) {
                    bassFret = f;
                    break;
                }
            }
            if (bassFret < 0) continue;

            VoicingResult cand {};
            cand.notes[static_cast<size_t>(cand.count++)] = kOpenStrings[static_cast<size_t>(bassStr)] + bassFret;

            uint16_t coveredMask = static_cast<uint16_t>(1u << bassPc);
            int frettedMin = (bassFret > 0) ? bassFret : 99;
            int frettedMax = (bassFret > 0) ? bassFret : 0;

            for (int s = bassStr + 1; s < 6; ++s) {
                int bestNote = -1;
                int bestFret = -1;
                float toneScore = -1.0e6f;

                // Allow open string if minFret == 0, plus frets in [minFret, maxFret]
                for (int f = (minFret == 0 ? 0 : minFret); f <= maxFret; ++f) {
                    const int midi = kOpenStrings[static_cast<size_t>(s)] + f;
                    if (midi < 40 || midi > 76) continue;
                    const int pc = midi % 12;
                    if ((chordMask & (1u << pc)) == 0) continue;

                    float sc = 0.0f;
                    // Strongly prefer uncovered chord tones
                    if ((coveredMask & (1u << pc)) == 0) sc += 10.0f;
                    if (pc == thirdPc && (coveredMask & (1u << pc)) == 0) sc += 6.0f;
                    if (pc == seventhPc && (coveredMask & (1u << pc)) == 0) sc += 5.0f;
                    // Prefer compact hand stretch
                    if (f > 0) {
                        const int span = std::max(frettedMax, f) - std::min(frettedMin, f);
                        if (span > 4) continue;
                        sc -= static_cast<float>(span) * 0.8f;
                    }
                    // Prefer staying closer to minFret
                    sc -= static_cast<float>(std::abs(f - minFret)) * 0.3f;

                    if (sc > toneScore) {
                        toneScore = sc;
                        bestNote = midi;
                        bestFret = f;
                    }
                }

                if (bestNote >= 40 && bestNote <= 76 && cand.count < 6) {
                    cand.notes[static_cast<size_t>(cand.count++)] = bestNote;
                    coveredMask |= static_cast<uint16_t>(1u << (bestNote % 12));
                    if (bestFret > 0) {
                        frettedMin = std::min(frettedMin, bestFret);
                        frettedMax = std::max(frettedMax, bestFret);
                    }
                }
            }

            if (cand.count < 3 && chord.quality != ChordQuality::Power5) continue;

            // Score overall guitar voicing quality
            float score = static_cast<float>(cand.count) * 3.0f;
            int uniqueTones = 0;
            for (int pc = 0; pc < 12; ++pc) {
                if ((coveredMask & (1u << pc)) != 0) ++uniqueTones;
            }
            score += static_cast<float>(uniqueTones) * 8.0f;
            if (thirdPc >= 0 && (coveredMask & (1u << thirdPc)) != 0) score += 10.0f;
            if (seventhPc >= 0 && (coveredMask & (1u << seventhPc)) != 0) score += 8.0f;
            // Slight preference for mid-neck / open voicings (frets 0..7)
            score -= static_cast<float>(minFret) * 0.45f;
            // Power5 prefers 3-string power chord
            if (chord.quality == ChordQuality::Power5 && cand.count > 3) {
                cand.count = 3;
            }

            if (score > bestScore) {
                bestScore = score;
                best = cand;
            }
        }
    }

    if (best.count == 0) {
        // Fallback triad in guitar range
        int rootMidi = 40 + ((chord.effectiveBass() - (40 % 12) + 12) % 12);
        for (int i = 0; i < chord.numTones && best.count < 4; ++i) {
            int n = rootMidi + chord.intervals[static_cast<size_t>(i)];
            if (n >= 40 && n <= 76) best.notes[static_cast<size_t>(best.count++)] = n;
        }
    }

    detail::sortVoicingAscending(best);
    return best;
}

} // namespace sessionmate
