#pragma once

#include "SessionMateTypes.h"
#include <SuiteHarmonyLink.h>
#include <algorithm>
#include <cstring>

namespace sessionmate {

inline suite::link::LinkChordQuality toLinkQuality(ChordQuality q) noexcept {
    switch (q) {
        case ChordQuality::Major:           return suite::link::LinkChordQuality::Major;
        case ChordQuality::Minor:           return suite::link::LinkChordQuality::Minor;
        case ChordQuality::Dominant7:       return suite::link::LinkChordQuality::Dom7;
        case ChordQuality::Major7:          return suite::link::LinkChordQuality::Maj7;
        case ChordQuality::Minor7:          return suite::link::LinkChordQuality::Min7;
        case ChordQuality::Minor7b5:        return suite::link::LinkChordQuality::Min7b5;
        case ChordQuality::Diminished:      return suite::link::LinkChordQuality::Dim;
        case ChordQuality::Diminished7:     return suite::link::LinkChordQuality::Dim7;
        case ChordQuality::Augmented:       return suite::link::LinkChordQuality::Aug;
        case ChordQuality::Sus2:            return suite::link::LinkChordQuality::Sus2;
        case ChordQuality::Sus4:            return suite::link::LinkChordQuality::Sus4;
        case ChordQuality::Major6:          return suite::link::LinkChordQuality::Maj6;
        case ChordQuality::Minor6:          return suite::link::LinkChordQuality::Min6;
        case ChordQuality::Dominant9:       return suite::link::LinkChordQuality::Dom9;
        case ChordQuality::Major9:          return suite::link::LinkChordQuality::Maj9;
        case ChordQuality::Minor9:          return suite::link::LinkChordQuality::Min9;
        case ChordQuality::Dominant11:      return suite::link::LinkChordQuality::Dom11;
        case ChordQuality::Minor11:         return suite::link::LinkChordQuality::Min11;
        case ChordQuality::Dominant13:      return suite::link::LinkChordQuality::Dom13;
        case ChordQuality::Major13:         return suite::link::LinkChordQuality::Maj13;
        case ChordQuality::Minor13:         return suite::link::LinkChordQuality::Min13;
        case ChordQuality::Dominant7Sharp9: return suite::link::LinkChordQuality::Dom7Sharp9;
        case ChordQuality::Dominant7Flat9:  return suite::link::LinkChordQuality::Dom7Flat9;
        case ChordQuality::Power5:          return suite::link::LinkChordQuality::Power5;
        case ChordQuality::Add9:            return suite::link::LinkChordQuality::Add9;
    }
    return suite::link::LinkChordQuality::Major;
}

inline void populateChordIntervals(ChordSymbol& c) noexcept {
    c.intervals = { -1, -1, -1, -1, -1, -1, -1 };
    auto setTones = [&](std::initializer_list<int> list) noexcept {
        int idx = 0;
        for (int v : list) {
            if (idx < 7) c.intervals[static_cast<size_t>(idx++)] = v;
        }
        c.numTones = idx;
    };

    switch (c.quality) {
        case ChordQuality::Major:           setTones({ 0, 4, 7 }); break;
        case ChordQuality::Minor:           setTones({ 0, 3, 7 }); break;
        case ChordQuality::Dominant7:       setTones({ 0, 4, 7, 10 }); break;
        case ChordQuality::Major7:          setTones({ 0, 4, 7, 11 }); break;
        case ChordQuality::Minor7:          setTones({ 0, 3, 7, 10 }); break;
        case ChordQuality::Minor7b5:        setTones({ 0, 3, 6, 10 }); break;
        case ChordQuality::Diminished:      setTones({ 0, 3, 6 }); break;
        case ChordQuality::Diminished7:     setTones({ 0, 3, 6, 9 }); break;
        case ChordQuality::Augmented:       setTones({ 0, 4, 8 }); break;
        case ChordQuality::Sus2:            setTones({ 0, 2, 7 }); break;
        case ChordQuality::Sus4:            setTones({ 0, 5, 7 }); break;
        case ChordQuality::Major6:          setTones({ 0, 4, 7, 9 }); break;
        case ChordQuality::Minor6:          setTones({ 0, 3, 7, 9 }); break;
        case ChordQuality::Dominant9:       setTones({ 0, 4, 7, 10, 14 }); break;
        case ChordQuality::Major9:          setTones({ 0, 4, 7, 11, 14 }); break;
        case ChordQuality::Minor9:          setTones({ 0, 3, 7, 10, 14 }); break;
        case ChordQuality::Dominant11:      setTones({ 0, 4, 7, 10, 14, 17 }); break;
        case ChordQuality::Minor11:         setTones({ 0, 3, 7, 10, 14, 17 }); break;
        case ChordQuality::Dominant13:      setTones({ 0, 4, 7, 10, 14, 21 }); break;
        case ChordQuality::Major13:         setTones({ 0, 4, 7, 11, 14, 21 }); break;
        case ChordQuality::Minor13:         setTones({ 0, 3, 7, 10, 14, 21 }); break;
        case ChordQuality::Dominant7Sharp9: setTones({ 0, 4, 7, 10, 15 }); break;
        case ChordQuality::Dominant7Flat9:  setTones({ 0, 4, 7, 10, 13 }); break;
        case ChordQuality::Power5:          setTones({ 0, 7 }); break;
        case ChordQuality::Add9:            setTones({ 0, 4, 7, 14 }); break;
    }
}

inline suite::link::LinkChord toLinkChord(const ChordSymbol& ch,
                                          uint8_t confidence = 100,
                                          uint16_t durationBeatsQ8 = 1024) noexcept {
    if (!ch.valid || ch.rootPitchClass < 0 || ch.rootPitchClass >= 12)
        return suite::link::LinkChord{};
    return suite::link::makeLinkChord(
        static_cast<int8_t>(ch.rootPitchClass),
        toLinkQuality(ch.quality),
        static_cast<int8_t>(ch.bassPitchClass),
        confidence,
        durationBeatsQ8);
}

inline ChordSymbol fromLinkChord(const suite::link::LinkChord& lc) noexcept {
    ChordSymbol out{};
    if (!lc.isValid()) {
        out.valid = false;
        out.rootPitchClass = -1;
        out.bassPitchClass = -1;
        out.numTones = 0;
        return out;
    }

    out.valid = true;
    out.rootPitchClass = static_cast<int>(lc.rootPc);
    out.bassPitchClass = (lc.bassPc >= 0 && lc.bassPc < 12) ? static_cast<int>(lc.bassPc) : -1;

    switch (lc.quality) {
        case suite::link::LinkChordQuality::Major:      out.quality = ChordQuality::Major; break;
        case suite::link::LinkChordQuality::Minor:      out.quality = ChordQuality::Minor; break;
        case suite::link::LinkChordQuality::Dom7:       out.quality = ChordQuality::Dominant7; break;
        case suite::link::LinkChordQuality::Maj7:       out.quality = ChordQuality::Major7; break;
        case suite::link::LinkChordQuality::Min7:       out.quality = ChordQuality::Minor7; break;
        case suite::link::LinkChordQuality::Min7b5:     out.quality = ChordQuality::Minor7b5; break;
        case suite::link::LinkChordQuality::Dim:        out.quality = ChordQuality::Diminished; break;
        case suite::link::LinkChordQuality::Dim7:       out.quality = ChordQuality::Diminished7; break;
        case suite::link::LinkChordQuality::Aug:        out.quality = ChordQuality::Augmented; break;
        case suite::link::LinkChordQuality::Sus2:       out.quality = ChordQuality::Sus2; break;
        case suite::link::LinkChordQuality::Sus4:       out.quality = ChordQuality::Sus4; break;
        case suite::link::LinkChordQuality::Maj6:       out.quality = ChordQuality::Major6; break;
        case suite::link::LinkChordQuality::Min6:       out.quality = ChordQuality::Minor6; break;
        case suite::link::LinkChordQuality::Dom9:       out.quality = ChordQuality::Dominant9; break;
        case suite::link::LinkChordQuality::Maj9:       out.quality = ChordQuality::Major9; break;
        case suite::link::LinkChordQuality::Min9:       out.quality = ChordQuality::Minor9; break;
        case suite::link::LinkChordQuality::Dom11:      out.quality = ChordQuality::Dominant11; break;
        case suite::link::LinkChordQuality::Min11:      out.quality = ChordQuality::Minor11; break;
        case suite::link::LinkChordQuality::Dom13:      out.quality = ChordQuality::Dominant13; break;
        case suite::link::LinkChordQuality::Maj13:      out.quality = ChordQuality::Major13; break;
        case suite::link::LinkChordQuality::Min13:      out.quality = ChordQuality::Minor13; break;
        case suite::link::LinkChordQuality::Dom7Sharp9: out.quality = ChordQuality::Dominant7Sharp9; break;
        case suite::link::LinkChordQuality::Dom7Flat9:  out.quality = ChordQuality::Dominant7Flat9; break;
        case suite::link::LinkChordQuality::Power5:     out.quality = ChordQuality::Power5; break;
        case suite::link::LinkChordQuality::Add9:       out.quality = ChordQuality::Add9; break;
        default:                                        out.quality = ChordQuality::Major; break;
    }

    populateChordIntervals(out);
    return out;
}

inline ScaleMode fromLinkMode(suite::link::LinkMode m) noexcept {
    const int idx = std::clamp(static_cast<int>(m), 0, static_cast<int>(ScaleMode::Blues));
    return static_cast<ScaleMode>(idx);
}

inline suite::link::HarmonicSourceMode toLinkHarmonicSource(HarmonicSourceMode m) noexcept {
    switch (m) {
        case HarmonicSourceMode::Chart:          return suite::link::HarmonicSourceMode::Chart;
        case HarmonicSourceMode::LiveAudio:      return suite::link::HarmonicSourceMode::LiveAudio;
        case HarmonicSourceMode::HarmonyHudLink: return suite::link::HarmonicSourceMode::HarmonyHudLink;
    }
    return suite::link::HarmonicSourceMode::Chart;
}

inline void copyFixedString(char* dst, size_t dstSize, const char* src) noexcept {
    if (!dst || dstSize == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    while (i + 1 < dstSize && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

} // namespace sessionmate
