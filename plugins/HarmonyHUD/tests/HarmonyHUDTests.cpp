#include <juce_audio_processors/juce_audio_processors.h>
#include "../src/PluginProcessor.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace harmonyhud;

namespace {

int gPassCount = 0;
int gFailCount = 0;

void check(bool cond, const std::string& label, const std::string& detail = {}) {
    if (cond) {
        ++gPassCount;
        std::cout << "[PASS] " << label;
        if (!detail.empty()) std::cout << " (" << detail << ")";
        std::cout << "\n";
    } else {
        ++gFailCount;
        std::cerr << "[FAIL] " << label;
        if (!detail.empty()) std::cerr << " (" << detail << ")";
        std::cerr << "\n";
    }
}

// Synthesize rich multi-harmonic polyphonic instrument tone (piano/guitar-like harmonics 1..6)
std::vector<float> synthPolyphonicChord(const std::vector<int>& midiNotes,
                                        double sampleRate,
                                        double durationSec,
                                        float a4Hz = 440.0f,
                                        float amplitude = 0.28f) {
    const int nSamples = static_cast<int>(sampleRate * durationSec);
    std::vector<float> buf(static_cast<size_t>(nSamples), 0.0f);
    if (midiNotes.empty()) return buf;

    constexpr double kTwoPi = 6.28318530717958647692;
    const float harmonicWeights[6] = {1.0f, 0.52f, 0.34f, 0.22f, 0.14f, 0.09f};
    const float perNoteGain = amplitude / static_cast<float>(midiNotes.size());

    for (size_t idx = 0; idx < midiNotes.size(); ++idx) {
        int midi = midiNotes[idx];
        double f0 = static_cast<double>(a4Hz) * std::pow(2.0, (midi - 69) / 12.0);
        double basePhase = 0.37 * static_cast<double>(idx);

        for (int i = 0; i < nSamples; ++i) {
            double t = static_cast<double>(i) / sampleRate;
            float env = 1.0f;
            if (t < 0.015) env = static_cast<float>(t / 0.015);
            else if (t > durationSec - 0.02) env = std::max(0.0f, static_cast<float>((durationSec - t) / 0.02));
            env *= (0.78f + 0.22f * std::exp(static_cast<float>(-t * 1.1)));

            double sample = 0.0;
            for (int h = 1; h <= 6; ++h) {
                double fh = f0 * h;
                if (fh >= sampleRate * 0.45) break;
                sample += harmonicWeights[h - 1] * std::sin(kTwoPi * fh * t + basePhase * h);
            }
            buf[static_cast<size_t>(i)] += static_cast<float>(sample) * perNoteGain * env;
        }
    }
    return buf;
}

ChordSymbol recognizeChordFromAudio(const std::vector<int>& midiNotes,
                                    double sampleRate = 48000.0,
                                    float a4Hz = 440.0f,
                                    double durationSec = 0.9) {
    HarmonyTracker tracker;
    tracker.prepare(sampleRate);
    tracker.setReferenceA4(a4Hz);
    tracker.setSensitivity(0.55f);
    tracker.setSmoothing(0.35f);

    auto audio = synthPolyphonicChord(midiNotes, sampleRate, durationSec, a4Hz);
    constexpr int kBlock = 512;
    for (size_t pos = 0; pos < audio.size(); pos += kBlock) {
        int n = static_cast<int>(std::min<size_t>(kBlock, audio.size() - pos));
        tracker.processAudioBlocks(audio.data() + pos, nullptr, n);
    }
    return tracker.getSnapshot().currentChord;
}

void testSingleChordRecognition() {
    std::cout << "\n--- Test 1: Multi-Harmonic Polyphonic Chord Recognition ---\n";

    struct ChordTestCase {
        std::string expectedName;
        int expectedRoot;
        ChordQuality expectedQuality;
        int expectedBass; // -1 if root position
        std::vector<int> midiNotes;
    };

    const std::vector<ChordTestCase> cases = {
        {"Cmaj7", 0,  ChordQuality::Maj7,  -1, {48, 52, 55, 59}},        // C3 E3 G3 B3
        {"Am9",   9,  ChordQuality::Min9,  -1, {45, 52, 55, 59, 60}},    // A2 E3 G3 B3 C4
        {"Fmaj9", 5,  ChordQuality::Maj9,  -1, {41, 48, 52, 55, 57}},    // F2 C3 E3 G3 A3
        {"G13",   7,  ChordQuality::Dom13, -1, {43, 53, 57, 59, 64}},    // G2 F3 A3 B3 E4
        {"Dm7",   2,  ChordQuality::Min7,  -1, {50, 53, 57, 60}},        // D3 F3 A3 C4
        {"Bdim",  11, ChordQuality::Dim,   -1, {47, 50, 53, 59}},        // B2 D3 F3 B3
        {"E7",    4,  ChordQuality::Dom7,  -1, {40, 47, 50, 56}},        // E2 B2 D3 G#3
        {"D/F#",  2,  ChordQuality::Major,  6, {42, 50, 57, 62}},        // F#2 D3 A3 D4
        {"C/G",   0,  ChordQuality::Major,  7, {43, 48, 52, 55}},        // G2 C3 E3 G3
    };

    for (const auto& tc : cases) {
        auto det = recognizeChordFromAudio(tc.midiNotes, 48000.0, 440.0f, 0.95);
        bool rootOk = (det.root == tc.expectedRoot);
        bool qualOk = (det.quality == tc.expectedQuality);
        bool bassOk = (tc.expectedBass < 0)
                          ? (!det.isSlashChord())
                          : (det.isSlashChord() && det.bass == tc.expectedBass);
        check(rootOk && qualOk && bassOk,
              "Chord " + tc.expectedName,
              "detected=" + det.name(false) + " conf=" + std::to_string(static_cast<int>(det.confidence * 100)) + "%");
    }
}

void testProgressionKeyAndHistory() {
    std::cout << "\n--- Test 2: Progression, Key Estimation, Mode, Roman Numerals & 5-Chip Substitutions ---\n";

    HarmonyTracker tracker;
    tracker.prepare(48000.0);
    tracker.setSensitivity(0.55f);
    tracker.setSmoothing(0.45f);
    tracker.setTransportInfo(120.0, 0.0, true);

    const std::vector<std::vector<int>> prog = {
        {48, 52, 55, 59},     // Cmaj7
        {45, 52, 55, 59, 60}, // Am9
        {41, 48, 52, 55, 57}, // Fmaj9
        {43, 53, 57, 59, 64}, // G13
    };

    double ppq = 0.0;
    for (size_t i = 0; i < prog.size(); ++i) {
        auto audio = synthPolyphonicChord(prog[i], 48000.0, 1.1);
        for (size_t pos = 0; pos < audio.size(); pos += 512) {
            int n = static_cast<int>(std::min<size_t>(512, audio.size() - pos));
            tracker.setTransportInfo(120.0, ppq, true);
            tracker.processAudioBlocks(audio.data() + pos, nullptr, n);
            ppq += (static_cast<double>(n) / 48000.0) * 2.0;
        }
    }

    auto snap = tracker.getSnapshot();
    bool keyIsCMajor = (snap.currentKey.tonic == 0 && !snap.currentKey.isMinor);
    check(keyIsCMajor && snap.currentKey.confidence >= 0.50f,
          "Key estimation C Major",
          snap.currentKey.name() + " (" + std::to_string(static_cast<int>(snap.currentKey.confidence * 100)) + "%)");

    check(snap.history.size() >= 4,
          "Chord progression history captured >= 4 entries",
          "count=" + std::to_string(snap.history.size()));

    // Verify 5 categorized substitutions & mode inference for Fmaj9 in C Major
    ChordSymbol fmaj9{5, ChordQuality::Maj9, -1, 0.95f};
    KeyEstimate cMaj{0, false, 0.95f};
    auto subs = generateSubstitutions(fmaj9, cMaj);
    bool subsValid = true;
    for (const auto& s : subs) {
        if (!s.chord.isValid() || s.midiNotes.empty() || s.category.empty())
            subsValid = false;
    }
    check(subsValid,
          "5 categorized smart substitutions generated for Fmaj9 in C Major",
          subs[0].label + ", " + subs[1].label + ", " + subs[2].label + ", " + subs[3].label + ", " + subs[4].label);
    check(cMaj.romanNumeral(fmaj9) == "IVmaj9",
          "Roman numeral function for Fmaj9 in C Major",
          cMaj.romanNumeral(fmaj9));
    check(inferModeForChordAndKey(fmaj9, cMaj) == ScaleMode::Lydian,
          "Mode inference for IVmaj9 in C Major -> Lydian",
          scaleModeName(inferModeForChordAndKey(fmaj9, cMaj)));
    check(computeOverlayPitchClassMask(OverlayMode::GuideTones, fmaj9, cMaj, ScaleMode::Lydian) == ((1u << 9) | (1u << 4)),
          "Guide-tone overlay mask for Fmaj9 -> A (3rd) & E (7th)");
}

void testSidechainClashDetection() {
    std::cout << "\n--- Test 3: Inter-Track Sidechain Clash Detection ---\n";

    // 3a. Minor-2nd clash: Cmaj7 (C3, E3, G3, B3) vs F3 on sidechain (E3 vs F3 = m2!)
    {
        HarmonyTracker tracker;
        tracker.prepare(48000.0);
        auto mainBuf = synthPolyphonicChord({48, 52, 55, 59}, 48000.0, 0.8);
        auto scBuf   = synthPolyphonicChord({53}, 48000.0, 0.8);

        for (size_t pos = 0; pos < mainBuf.size(); pos += 512) {
            int n = static_cast<int>(std::min<size_t>(512, mainBuf.size() - pos));
            tracker.processAudioBlocks(mainBuf.data() + pos, scBuf.data() + pos, n);
        }
        auto snap = tracker.getSnapshot();
        bool foundM2 = false;
        for (const auto& c : snap.clashes)
            if (c.isMinorSecond()) foundM2 = true;
        check(snap.sidechainActive && foundM2 && snap.clashSeverity > 0.2f,
              "Sidechain minor-2nd clash detected (Cmaj7 vs F3)",
              snap.clashes.empty() ? "none" : snap.clashes.front().description());
    }

    // 3b. Tritone clash: C3+G3 main vs F#3 sidechain
    {
        HarmonyTracker tracker;
        tracker.prepare(48000.0);
        auto mainBuf = synthPolyphonicChord({48, 55}, 48000.0, 0.8);
        auto scBuf   = synthPolyphonicChord({54}, 48000.0, 0.8);

        for (size_t pos = 0; pos < mainBuf.size(); pos += 512) {
            int n = static_cast<int>(std::min<size_t>(512, mainBuf.size() - pos));
            tracker.processAudioBlocks(mainBuf.data() + pos, scBuf.data() + pos, n);
        }
        auto snap = tracker.getSnapshot();
        bool foundTritone = false;
        for (const auto& c : snap.clashes)
            if (c.isTritone()) foundTritone = true;
        check(snap.sidechainActive && foundTritone,
              "Sidechain tritone clash detected (C+G vs F#)",
              snap.clashes.empty() ? "none" : snap.clashes.front().description());
    }

    // 3c. Clean consonant sidechain: Cmaj7 main vs C2+G2 bass sidechain -> zero clashes!
    {
        HarmonyTracker tracker;
        tracker.prepare(48000.0);
        auto mainBuf = synthPolyphonicChord({48, 52, 55, 59}, 48000.0, 0.8);
        auto scBuf   = synthPolyphonicChord({36, 43}, 48000.0, 0.8);

        for (size_t pos = 0; pos < mainBuf.size(); pos += 512) {
            int n = static_cast<int>(std::min<size_t>(512, mainBuf.size() - pos));
            tracker.processAudioBlocks(mainBuf.data() + pos, scBuf.data() + pos, n);
        }
        auto snap = tracker.getSnapshot();
        check(snap.sidechainActive && snap.clashes.empty(),
              "Consonant sidechain produces 0 false clashes",
              "clashes=" + std::to_string(snap.clashes.size()));
    }
}

void testProcessorBitTransparentAndMidiAndState() {
    std::cout << "\n--- Test 4: Bit-Transparent Pass-Through, Audition, MIDI Input & State Round-Trip ---\n";

    HarmonyHUDProcessor proc;
    proc.prepareToPlay(48000.0, 512);

    // 4a. Bit-transparent pass-through test
    juce::AudioBuffer<float> buf(2, 512);
    juce::AudioBuffer<float> ref(2, 512);
    for (int ch = 0; ch < 2; ++ch) {
        for (int i = 0; i < 512; ++i) {
            float v = 0.35f * std::sin(0.05f * i + ch * 1.1f) + 0.12f * std::cos(0.19f * i);
            buf.setSample(ch, i, v);
            ref.setSample(ch, i, v);
        }
    }
    juce::MidiBuffer midi;
    proc.processBlock(buf, midi);

    bool bitExact = true;
    for (int ch = 0; ch < 2; ++ch) {
        for (int i = 0; i < 512; ++i) {
            if (buf.getSample(ch, i) != ref.getSample(ch, i)) {
                bitExact = false;
                break;
            }
        }
    }
    check(bitExact, "Audio pass-through is 100% bit-transparent when preview synth idle");

    // 4b. Audition preview synth outputs clean finite audio when clicked
    proc.auditionChord({60, 64, 67, 71}, 0.5f);
    juce::AudioBuffer<float> silent(2, 512);
    silent.clear();
    proc.processBlock(silent, midi);
    float peak = 0.0f;
    bool allFinite = true;
    for (int ch = 0; ch < 2; ++ch) {
        for (int i = 0; i < 512; ++i) {
            float s = silent.getSample(ch, i);
            if (!std::isfinite(s)) allFinite = false;
            peak = std::max(peak, std::abs(s));
        }
    }
    check(allFinite && peak > 0.01f && peak <= 1.0f,
          "Preview synth auditions cleanly with finite bounded output",
          "peak=" + std::to_string(peak));

    // 4c. Live MIDI chord input detection (Dm7: D3, F3, A3, C4)
    proc.resetAnalysis();
    proc.flushAnalysisNow();
    juce::MidiBuffer chordMidi;
    chordMidi.addEvent(juce::MidiMessage::noteOn(1, 50, (juce::uint8)100), 0);
    chordMidi.addEvent(juce::MidiMessage::noteOn(1, 53, (juce::uint8)100), 0);
    chordMidi.addEvent(juce::MidiMessage::noteOn(1, 57, (juce::uint8)100), 0);
    chordMidi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    silent.clear();
    proc.processBlock(silent, chordMidi);
    proc.flushAnalysisNow();
    auto midiSnap = proc.getSnapshot();
    check(midiSnap.currentChord.root == 2 && midiSnap.currentChord.quality == ChordQuality::Min7,
          "Live MIDI chord detection (Dm7)",
          "detected=" + midiSnap.currentChord.name());

    // 4d. State save / restore round-trip (including Phase 2 params)
    auto setParamNorm = [&](const char* id, float plainVal) {
        if (auto* p = proc.getAPVTS().getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1(plainVal));
    };
    setParamNorm(param::kTuningA4, 432.0f);
    setParamNorm(param::kSensitivity, 72.5f);
    setParamNorm(param::kSmoothing, 35.0f);
    setParamNorm(param::kGuitarTuning, 2.0f);  // DADGAD
    setParamNorm(param::kAuditionGain, -6.0f);
    setParamNorm(param::kClashSensitivity, 80.0f);
    setParamNorm(param::kFreezeHistory, 1.0f);
    setParamNorm(param::kLinkBus, 3.0f);       // Bus C
    setParamNorm(param::kViewMode, 1.0f);      // Band
    setParamNorm(param::kOverlayMode, 4.0f);   // Active Mode
    setParamNorm(param::kFretPosition, 2.0f);  // Pos 5
    setParamNorm(param::kAutoPushLive, 0.0f);
    setParamNorm(param::kClashAssist, 1.0f);

    juce::MemoryBlock stateBlock;
    proc.getStateInformation(stateBlock);

    HarmonyHUDProcessor restored;
    restored.setStateInformation(stateBlock.getData(), static_cast<int>(stateBlock.getSize()));

    auto getVal = [](HarmonyHUDProcessor& p, const char* id) {
        return p.getAPVTS().getRawParameterValue(id)->load();
    };
    bool stateOk =
        std::abs(getVal(restored, param::kTuningA4) - 432.0f) < 0.2f &&
        std::abs(getVal(restored, param::kSensitivity) - 72.5f) < 0.6f &&
        std::abs(getVal(restored, param::kSmoothing) - 35.0f) < 0.6f &&
        restored.getGuitarTuning() == GuitarTuning::DADGAD &&
        std::abs(getVal(restored, param::kAuditionGain) - (-6.0f)) < 0.6f &&
        std::abs(getVal(restored, param::kClashSensitivity) - 80.0f) < 0.6f &&
        getVal(restored, param::kFreezeHistory) >= 0.5f &&
        restored.getLinkBusIndex() == 2 &&
        restored.getViewMode() == ViewMode::Band &&
        restored.getOverlayMode() == OverlayMode::ActiveMode &&
        restored.getFretPosition() == FretPositionFilter::Pos5 &&
        getVal(restored, param::kAutoPushLive) < 0.5f &&
        getVal(restored, param::kClashAssist) >= 0.5f;
    check(stateOk, "APVTS state save/restore round-trip (all 13 parameters)");
}

void testGuitarVoicingAndMidiExport() {
    std::cout << "\n--- Test 5: Guitar Voicing Solver (Tunings & CAGED Pos) & .mid Export ---\n";

    const std::vector<ChordSymbol> testChords = {
        {0, ChordQuality::Maj7, -1, 1.0f},
        {5, ChordQuality::Maj9, -1, 1.0f},
        {7, ChordQuality::Dom13, -1, 1.0f},
        {2, ChordQuality::Major, 6, 1.0f}, // D/F#
    };

    for (auto tuning : {GuitarTuning::Standard, GuitarTuning::DropD, GuitarTuning::DADGAD}) {
        bool allPlayable = true;
        for (const auto& c : testChords) {
            for (auto pos : {FretPositionFilter::Auto, FretPositionFilter::Open, FretPositionFilter::Pos5}) {
                auto v = solveGuitarVoicing(c, tuning, pos);
                int sounding = 0, minF = 99, maxF = 0;
                for (int f : v.frets) {
                    if (f >= 0) {
                        ++sounding;
                        if (f > 0) {
                            minF = std::min(minF, f);
                            maxF = std::max(maxF, f);
                        }
                    }
                }
                int span = (maxF > 0 && minF < 99) ? (maxF - minF) : 0;
                if (!v.valid || sounding < 3 || span > 4)
                    allPlayable = false;
            }
        }
        check(allPlayable, std::string("Playable guitar voicings across CAGED positions in ") + guitarTuningName(tuning));
    }

    std::vector<ChordHistoryEntry> hist;
    for (size_t i = 0; i < testChords.size(); ++i) {
        ChordHistoryEntry e;
        e.chord = testChords[i];
        e.roman = "I";
        e.barPosition = static_cast<double>(i + 1);
        e.durationBeats = 4.0;
        e.midiNotes = testChords[i].voicingMidiNotes(3);
        hist.push_back(e);
    }

    auto bytes = buildMidiFileBytes(hist, 120.0, false);
    bool headerOk = bytes.size() > 30 &&
                    bytes[0] == 'M' && bytes[1] == 'T' && bytes[2] == 'h' && bytes[3] == 'd' &&
                    bytes[14] == 'M' && bytes[15] == 'T' && bytes[16] == 'r' && bytes[17] == 'k';
    uint32_t declaredTrackLen = 0;
    if (bytes.size() >= 22) {
        declaredTrackLen = (static_cast<uint32_t>(bytes[18]) << 24) |
                           (static_cast<uint32_t>(bytes[19]) << 16) |
                           (static_cast<uint32_t>(bytes[20]) << 8) |
                           static_cast<uint32_t>(bytes[21]);
    }
    bool lenMatches = (22u + declaredTrackLen == bytes.size());
    bool eotOk = bytes.size() >= 3 &&
                 bytes[bytes.size() - 3] == 0xFF &&
                 bytes[bytes.size() - 2] == 0x2F &&
                 bytes[bytes.size() - 1] == 0x00;

    juce::MemoryInputStream mis(bytes.data(), bytes.size(), false);
    juce::MidiFile juceMidi;
    bool juceReadOk = juceMidi.readFrom(mis);
    int noteOnCount = 0;
    if (juceReadOk && juceMidi.getNumTracks() == 1) {
        const auto* trk = juceMidi.getTrack(0);
        for (int i = 0; i < trk->getNumEvents(); ++i)
            if (trk->getEventPointer(i)->message.isNoteOn())
                ++noteOnCount;
    }

    check(headerOk && lenMatches && eotOk && juceReadOk && noteOnCount >= 12,
          "Valid Standard MIDI File (.mid) export verified by JUCE MidiFile parser",
          "bytes=" + std::to_string(bytes.size()) + " noteOns=" + std::to_string(noteOnCount));
}

void testMultiSampleRateAndA4Tuning() {
    std::cout << "\n--- Test 6: Multi-Sample-Rate (44.1/48/96 kHz) & A4=432Hz Tuning ---\n";

    for (double sr : {44100.0, 48000.0, 96000.0}) {
        auto det = recognizeChordFromAudio({48, 52, 55, 59}, sr, 440.0f, 0.9);
        check(det.root == 0 && det.quality == ChordQuality::Maj7,
              "Cmaj7 recognized at " + std::to_string(static_cast<int>(sr)) + " Hz",
              det.name());
    }

    auto det432 = recognizeChordFromAudio({48, 52, 55, 59}, 48000.0, 432.0f, 0.9);
    check(det432.root == 0 && det432.quality == ChordQuality::Maj7,
          "Cmaj7 recognized with custom tuning A4 = 432 Hz",
          det432.name());
}

void testSuiteHarmonyLinkBidirectionalLoopback() {
    std::cout << "\n--- Test 7: SuiteHarmonyLink Bidirectional IPC & Band Clash Radar ---\n";

    const char* kTestSuffix = "hhud_unit_test_v1";

    HarmonyHUDProcessor hudProc;
    hudProc.prepareToPlay(48000.0, 512);
    hudProc.connectLinkBus(0, kTestSuffix);

    suite::link::HarmonyLinkEndpoint mockBand;
    bool bandConnected = mockBand.connect(0, kTestSuffix);
    check(bandConnected, "Mock Band endpoint connected on isolated HarmonyLink bus A");

    // 7a. Feed Cmaj7 MIDI into HarmonyHUD -> verify HudToBandPayload published accurately
    juce::AudioBuffer<float> silent(2, 512);
    silent.clear();
    juce::MidiBuffer midiCmaj7;
    midiCmaj7.addEvent(juce::MidiMessage::noteOn(1, 48, (juce::uint8)100), 0); // C3
    midiCmaj7.addEvent(juce::MidiMessage::noteOn(1, 52, (juce::uint8)100), 0); // E3
    midiCmaj7.addEvent(juce::MidiMessage::noteOn(1, 55, (juce::uint8)100), 0); // G3
    midiCmaj7.addEvent(juce::MidiMessage::noteOn(1, 59, (juce::uint8)100), 0); // B3
    hudProc.processBlock(silent, midiCmaj7);
    hudProc.flushAnalysisNow();

    suite::link::HudToBandPayload hudRx{};
    bool hudAlive = false;
    bool readOk = mockBand.readHud(hudRx, hudAlive);
    check(readOk && hudAlive &&
              hudRx.liveChord.rootPc == 0 &&
              hudRx.liveChord.quality == suite::link::LinkChordQuality::Maj7 &&
              hudRx.userActiveNotes.test(52) &&
              hudRx.avoidPitchClassMask != 0,
          "HarmonyHUD -> Band live chord, note mask & avoidPitchClassMask published over SuiteHarmonyLink",
          "avoidMask=0x" + std::to_string(hudRx.avoidPitchClassMask));

    // 7b. Push one-click Reharm (Db7, NextBeat) and Chart -> Band reads both cleanly
    ChordSymbol db7{1, ChordQuality::Dom7, -1, 1.0f};
    hudProc.pushReharmToBand(db7, ReharmPushTiming::NextBeat, 2);
    hudProc.sendChartToBand();
    hudProc.flushAnalysisNow();

    readOk = mockBand.readHud(hudRx, hudAlive);
    check(readOk && hudAlive &&
              hudRx.reharmSeq == 1 &&
              hudRx.reharmChord.rootPc == 1 &&
              hudRx.reharmChord.quality == suite::link::LinkChordQuality::Dom7 &&
              hudRx.reharmTiming == suite::link::ReharmTiming::NextBeat &&
              hudRx.reharmLockBars == 2 &&
              hudRx.chartSeq == 1 &&
              hudRx.chartNumBars >= 1,
          "One-click Reharm push & Send Chart -> Band received with exact sequence & timing");

    // 7c. Mock SessionMate publishes BandToHudPayload with Next-Chord Lookahead, Band Chart & clashing Keys note F3!
    suite::link::BandToHudPayload bandTx{};
    bandTx.isPlaying = 1;
    bandTx.bpm = 114.0;
    bandTx.timeSigNum = 4;
    bandTx.timeSigDen = 4;
    bandTx.currentBar = 4;
    bandTx.beatInBar = 2.0f;
    bandTx.activeChartBarIndex = 1;
    bandTx.clashAvoidanceActive = 1;
    bandTx.reharmPendingActive = 1;
    std::strncpy(bandTx.styleName, "Neo-Soul (J-Dilla)", sizeof(bandTx.styleName) - 1);
    std::strncpy(bandTx.sectionLabel, "Chorus", sizeof(bandTx.sectionLabel) - 1);
    bandTx.activeChord = suite::link::makeLinkChord(4, suite::link::LinkChordQuality::Min9);  // Em9
    bandTx.nextChord   = suite::link::makeLinkChord(9, suite::link::LinkChordQuality::Dom13); // A13
    bandTx.beatsUntilNextChord = 2.0f;

    // Band Bass plays C2 (36), Band Keys plays F3 (53 -> minor-2nd clash with user's E3!)
    bandTx.slotNotes[1].set(36);
    bandTx.slotActive[1] = 1;
    bandTx.slotNotes[2].set(53);
    bandTx.slotActive[2] = 1;

    // 4-bar Band Chart
    bandTx.bandChartSeq = 1;
    bandTx.bandChartNumBars = 4;
    bandTx.bandChartBars[0] = suite::link::makeLinkChord(4, suite::link::LinkChordQuality::Min9);
    bandTx.bandChartBars[1] = suite::link::makeLinkChord(9, suite::link::LinkChordQuality::Dom13);
    bandTx.bandChartBars[2] = suite::link::makeLinkChord(2, suite::link::LinkChordQuality::Maj7);
    bandTx.bandChartBars[3] = suite::link::makeLinkChord(11, suite::link::LinkChordQuality::Dom7Sharp9);

    mockBand.publishBand(bandTx);
    hudProc.flushAnalysisNow();

    auto snap = hudProc.getSnapshot();
    bool foundBandClash = false;
    for (const auto& c : snap.clashes) {
        if (c.isMinorSecond())
            foundBandClash = true;
    }

    check(snap.bandLink.connected &&
              snap.bandLink.styleName == "Neo-Soul (J-Dilla)" &&
              snap.bandLink.sectionLabel == "Chorus" &&
              snap.bandLink.nextChord.root == 9 &&
              snap.bandLink.nextChord.quality == ChordQuality::Dom13 &&
              std::abs(snap.bandLink.beatsUntilNextChord - 2.0f) < 0.01f &&
              snap.bandLink.slotNotes[2][53],
          "Band -> HarmonyHUD X-Ray state, Next-Chord Lookahead & per-slot notes received cleanly",
          "next=" + snap.bandLink.nextChord.name() + " in " + std::to_string(snap.bandLink.beatsUntilNextChord) + " beats");

    check(foundBandClash && snap.clashSourceIsBand && snap.clashingNotes[52] && snap.clashingNotes[53],
          "Zero-Sidechain Player-vs-Band Clash Radar flagged user E3 vs Band Keys F3",
          snap.clashes.empty() ? "none" : snap.clashes.front().description());

    // 7d. Pull Band Chart into HarmonyHUD & edit history
    hudProc.pullChartFromBand();
    auto pulled = hudProc.getSnapshot();
    check(pulled.history.size() == 4 &&
              pulled.history[0].chord.root == 4 &&
              pulled.history[1].chord.quality == ChordQuality::Dom13 &&
              pulled.history[3].chord.quality == ChordQuality::Dom7Sharp9,
          "Pull Band Chart imported all 4 SessionMate bars into HarmonyHUD progression editor");

    hudProc.editHistoryChord(0, ChordSymbol{0, ChordQuality::Maj9, -1, 1.0f});
    auto edited = hudProc.getSnapshot();
    check(edited.history.size() == 4 && edited.history[0].chord.root == 0 &&
              edited.history[0].chord.quality == ChordQuality::Maj9,
          "Interactive Progression Editor replaced bar 0 with Cmaj9");

    hudProc.connectLinkBus(-1);
    mockBand.disconnect();
}

} // namespace

int main() {
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "==============================================\n";
    std::cout << "  HarmonyHUD Automated Verification Suite\n";
    std::cout << "==============================================\n";

    testSingleChordRecognition();
    testProgressionKeyAndHistory();
    testSidechainClashDetection();
    testProcessorBitTransparentAndMidiAndState();
    testGuitarVoicingAndMidiExport();
    testMultiSampleRateAndA4Tuning();
    testSuiteHarmonyLinkBidirectionalLoopback();

    std::cout << "\n==============================================\n";
    std::cout << "  SUMMARY: " << gPassCount << " passed, " << gFailCount << " failed\n";
    std::cout << "==============================================\n";

    return (gFailCount == 0) ? 0 : 1;
}
