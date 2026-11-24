#pragma once

#include <SuiteHarmonyLink.h>
#include "MusicTheory.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace harmonyhud {

inline suite::link::LinkChordQuality toLinkQuality(ChordQuality q) noexcept {
    using LQ = suite::link::LinkChordQuality;
    switch (q) {
        case ChordQuality::Major:      return LQ::Major;
        case ChordQuality::Minor:      return LQ::Minor;
        case ChordQuality::Dom7:       return LQ::Dom7;
        case ChordQuality::Maj7:       return LQ::Maj7;
        case ChordQuality::Min7:       return LQ::Min7;
        case ChordQuality::Min7b5:     return LQ::Min7b5;
        case ChordQuality::Dim:        return LQ::Dim;
        case ChordQuality::Dim7:       return LQ::Dim7;
        case ChordQuality::Aug:        return LQ::Aug;
        case ChordQuality::Sus2:       return LQ::Sus2;
        case ChordQuality::Sus4:       return LQ::Sus4;
        case ChordQuality::Six:        return LQ::Maj6;
        case ChordQuality::Min6:       return LQ::Min6;
        case ChordQuality::Dom9:       return LQ::Dom9;
        case ChordQuality::Maj9:       return LQ::Maj9;
        case ChordQuality::Min9:       return LQ::Min9;
        case ChordQuality::Dom11:      return LQ::Dom11;
        case ChordQuality::Min11:      return LQ::Min11;
        case ChordQuality::Dom13:      return LQ::Dom13;
        case ChordQuality::Maj13:      return LQ::Maj13;
        case ChordQuality::Min13:      return LQ::Min13;
        case ChordQuality::Dom7Sharp9: return LQ::Dom7Sharp9;
        case ChordQuality::Dom7Flat9:  return LQ::Dom7Flat9;
        case ChordQuality::Power5:     return LQ::Power5;
        case ChordQuality::Add9:       return LQ::Add9;
        case ChordQuality::None:
        case ChordQuality::Count:
        default:                       return LQ::None;
    }
}

inline ChordQuality fromLinkQuality(suite::link::LinkChordQuality q) noexcept {
    using LQ = suite::link::LinkChordQuality;
    switch (q) {
        case LQ::Major:      return ChordQuality::Major;
        case LQ::Minor:      return ChordQuality::Minor;
        case LQ::Dom7:       return ChordQuality::Dom7;
        case LQ::Maj7:       return ChordQuality::Maj7;
        case LQ::Min7:       return ChordQuality::Min7;
        case LQ::Min7b5:     return ChordQuality::Min7b5;
        case LQ::Dim:        return ChordQuality::Dim;
        case LQ::Dim7:       return ChordQuality::Dim7;
        case LQ::Aug:        return ChordQuality::Aug;
        case LQ::Sus2:       return ChordQuality::Sus2;
        case LQ::Sus4:       return ChordQuality::Sus4;
        case LQ::Maj6:       return ChordQuality::Six;
        case LQ::Min6:       return ChordQuality::Min6;
        case LQ::Dom9:       return ChordQuality::Dom9;
        case LQ::Maj9:       return ChordQuality::Maj9;
        case LQ::Min9:       return ChordQuality::Min9;
        case LQ::Dom11:      return ChordQuality::Dom11;
        case LQ::Min11:      return ChordQuality::Min11;
        case LQ::Dom13:      return ChordQuality::Dom13;
        case LQ::Maj13:      return ChordQuality::Maj13;
        case LQ::Min13:      return ChordQuality::Min13;
        case LQ::Dom7Sharp9: return ChordQuality::Dom7Sharp9;
        case LQ::Dom7Flat9:  return ChordQuality::Dom7Flat9;
        case LQ::Power5:     return ChordQuality::Power5;
        case LQ::Add9:       return ChordQuality::Add9;
        case LQ::None:
        default:             return ChordQuality::None;
    }
}

inline suite::link::LinkMode toLinkMode(ScaleMode m) noexcept {
    return static_cast<suite::link::LinkMode>(std::clamp(static_cast<int>(m), 0, 12));
}

inline suite::link::ReharmTiming toLinkTiming(ReharmPushTiming t) noexcept {
    switch (t) {
        case ReharmPushTiming::Immediate: return suite::link::ReharmTiming::Immediate;
        case ReharmPushTiming::NextBeat:  return suite::link::ReharmTiming::NextBeat;
        case ReharmPushTiming::NextBar:
        default:                          return suite::link::ReharmTiming::NextBar;
    }
}

inline suite::link::LinkChord toLinkChord(const ChordSymbol& c, double durationBeats = 4.0) noexcept {
    if (!c.isValid())
        return suite::link::LinkChord{};
    const uint8_t conf = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(c.confidence * 100.0f)), 0, 100));
    const double clampedDur = std::clamp(durationBeats, 0.25, 64.0);
    const uint16_t durQ8 = static_cast<uint16_t>(std::lround(clampedDur * 256.0));
    return suite::link::makeLinkChord(static_cast<int8_t>(c.root),
                                      toLinkQuality(c.quality),
                                      c.isSlashChord() ? static_cast<int8_t>(c.bass) : int8_t{-1},
                                      conf,
                                      durQ8);
}

inline ChordSymbol fromLinkChord(const suite::link::LinkChord& lc) noexcept {
    ChordSymbol out;
    if (!lc.isValid())
        return out;
    out.root = lc.rootPc;
    out.quality = fromLinkQuality(lc.quality);
    out.bass = (lc.bassPc >= 0 && lc.bassPc < 12 && lc.bassPc != lc.rootPc) ? lc.bassPc : -1;
    out.confidence = std::clamp(static_cast<float>(lc.confidence) * 0.01f, 0.0f, 1.0f);
    return out;
}

inline suite::link::NoteMask128 toNoteMask128(const std::array<bool, 128>& active) noexcept {
    suite::link::NoteMask128 mask;
    for (int n = 0; n < 128; ++n) {
        if (active[static_cast<size_t>(n)])
            mask.set(n);
    }
    return mask;
}

inline void fromNoteMask128(const suite::link::NoteMask128& mask, std::array<bool, 128>& out) noexcept {
    out.fill(false);
    for (int n = 0; n < 128; ++n) {
        if (mask.test(n))
            out[static_cast<size_t>(n)] = true;
    }
}

inline BandLinkState fromBandPayload(const suite::link::BandToHudPayload& p,
                                     bool peerAlive,
                                     int busIndex,
                                     const KeyEstimate& keyForRoman) {
    BandLinkState st;
    st.connected = peerAlive;
    st.busIndex = busIndex;
    if (!peerAlive)
        return st;

    st.isPlaying = (p.isPlaying != 0);
    st.bpm = (p.bpm > 20.0 && p.bpm < 350.0) ? p.bpm : 120.0;
    st.timeSigNum = (p.timeSigNum > 0) ? static_cast<int>(p.timeSigNum) : 4;
    st.timeSigDen = (p.timeSigDen > 0) ? static_cast<int>(p.timeSigDen) : 4;
    st.currentBar = static_cast<int>(p.currentBar);
    st.beatInBar = p.beatInBar;
    st.activeChartBarIndex = static_cast<int>(p.activeChartBarIndex);

    // Safe bounded null-terminated string extraction
    {
        size_t len = 0;
        while (len < sizeof(p.styleName) && p.styleName[len] != '\0') ++len;
        st.styleName.assign(p.styleName, len);
    }
    {
        size_t len = 0;
        while (len < sizeof(p.sectionLabel) && p.sectionLabel[len] != '\0') ++len;
        st.sectionLabel.assign(p.sectionLabel, len);
    }

    st.clashAvoidanceActive = (p.clashAvoidanceActive != 0);
    st.reharmPendingActive = (p.reharmPendingActive != 0);
    st.activeChord = fromLinkChord(p.activeChord);
    st.nextChord = fromLinkChord(p.nextChord);
    st.beatsUntilNextChord = std::max(0.0f, p.beatsUntilNextChord);

    for (int s = 0; s < suite::link::kNumBandSlots; ++s) {
        fromNoteMask128(p.slotNotes[s], st.slotNotes[static_cast<size_t>(s)]);
        st.slotActive[static_cast<size_t>(s)] = (p.slotActive[s] != 0) || p.slotNotes[s].any();
    }

    const int numBars = std::clamp(static_cast<int>(p.bandChartNumBars), 0, suite::link::kMaxChartBars);
    st.bandChart.reserve(static_cast<size_t>(numBars));
    double barCursor = 1.0;
    for (int i = 0; i < numBars; ++i) {
        ChordSymbol cs = fromLinkChord(p.bandChartBars[i]);
        if (!cs.isValid()) continue;
        ChordHistoryEntry entry;
        entry.chord = cs;
        entry.roman = keyForRoman.romanNumeral(cs);
        entry.barPosition = barCursor;
        entry.durationBeats = (p.bandChartBars[i].durationBeatsQ8 > 0)
                                  ? (static_cast<double>(p.bandChartBars[i].durationBeatsQ8) / 256.0)
                                  : 4.0;
        entry.midiNotes = cs.voicingMidiNotes(3);
        st.bandChart.push_back(std::move(entry));
        barCursor += std::max(0.25, entry.durationBeats / 4.0);
    }

    return st;
}

inline suite::link::HudToBandPayload buildHudPayload(const AnalysisSnapshot& snap,
                                                     bool autoPushLiveChords,
                                                     bool clashAssistEnabled,
                                                     uint32_t reharmSeq,
                                                     const ChordSymbol& reharmChord,
                                                     ReharmPushTiming reharmTiming,
                                                     uint8_t reharmLockBars,
                                                     uint32_t chartSeq,
                                                     const std::vector<ChordHistoryEntry>& chartToSend) {
    suite::link::HudToBandPayload out{};
    out.liveChord = toLinkChord(snap.currentChord, 4.0);
    out.keyRootPc = static_cast<int8_t>(std::clamp(snap.currentKey.tonic, 0, 11));
    out.keyIsMinor = snap.currentKey.isMinor ? 1u : 0u;
    out.keyConfidence = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(snap.currentKey.confidence * 100.0f)), 0, 100));
    out.activeMode = toLinkMode(snap.activeMode);

    out.scalePitchClassMask = snap.scalePitchClassMask;
    out.avoidPitchClassMask = clashAssistEnabled ? snap.avoidPitchClassMask : 0u;

    uint16_t clashMask = 0u;
    for (int n = 0; n < 128; ++n) {
        if (snap.clashingNotes[static_cast<size_t>(n)])
            clashMask = static_cast<uint16_t>(clashMask | (1u << (n % 12)));
    }
    out.clashPitchClassMask = clashMask;

    for (int i = 0; i < 12; ++i)
        out.chroma[i] = snap.chroma[static_cast<size_t>(i)];

    out.userActiveNotes = toNoteMask128(snap.activeNotes);
    out.userActivityLevel = std::clamp(snap.userActivityLevel, 0.0f, 1.0f);
    out.userEnergy = std::clamp((snap.inputLevelDb + 60.0f) / 60.0f, 0.0f, 1.0f);
    out.userHasSignal = (snap.inputLevelDb > -58.0f || out.userActiveNotes.any()) ? 1u : 0u;
    out.clashCount = static_cast<uint8_t>(std::min<size_t>(255u, snap.clashes.size()));
    out.clashSeverity = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(snap.clashSeverity * 100.0f)), 0, 100));
    out.autoPushLiveChords = autoPushLiveChords ? 1u : 0u;

    out.reharmSeq = reharmSeq;
    out.reharmChord = toLinkChord(reharmChord, 4.0);
    out.reharmTiming = toLinkTiming(reharmTiming);
    out.reharmLockBars = std::max<uint8_t>(1u, reharmLockBars);

    out.chartSeq = chartSeq;
    const int nBars = std::min<int>(static_cast<int>(chartToSend.size()), suite::link::kMaxChartBars);
    out.chartNumBars = static_cast<uint8_t>(nBars);
    for (int i = 0; i < nBars; ++i) {
        out.chartBars[i] = toLinkChord(chartToSend[static_cast<size_t>(i)].chord,
                                       chartToSend[static_cast<size_t>(i)].durationBeats);
    }

    return out;
}

} // namespace harmonyhud
