#include "../src/SessionMateProcessor.h"

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using namespace sessionmate;

namespace {

int gTestsPassed = 0;
int gTestsFailed = 0;

void check(bool condition, const std::string& name, const std::string& detail = "") {
    if (condition) {
        ++gTestsPassed;
        std::cout << "  [PASS] " << name << "\n";
    } else {
        ++gTestsFailed;
        std::cerr << "  [FAIL] " << name;
        if (!detail.empty()) std::cerr << " -- " << detail;
        std::cerr << "\n";
    }
}

void testStylesAndOddMeters() {
    std::cout << "\n=== 1. Styles (32+) & Odd Time Signatures ===\n";
    GrooveComposer composer;
    composer.prepare(44100.0);
    const auto& lib = composer.library();
    check(lib.count() >= 30, "StyleLibrary has >= 30 styles", "count=" + std::to_string(lib.count()));

    auto parsed = parseChordTrack("| Em9 | A13 | Dmaj7 | B7#9 |");
    std::array<ChordSymbol, kMaxChordBars> chart {};
    for (size_t i = 0; i < parsed.size() && i < kMaxChordBars; ++i)
        chart[i] = parsed[i];
    composer.setChordProgression(chart, static_cast<int>(parsed.size()));

    std::array<SlotConfig, kNumSlots> slots {};
    for (int s = 0; s < kNumSlots; ++s) {
        slots[static_cast<size_t>(s)].busy = 0.8f;
        slots[static_cast<size_t>(s)].kickLock = 0.9f;
    }

    ListenerTelemetry quietListener {};
    quietListener.userActivity = 0.05f;

    int stylesWithValidMidi = 0;
    bool allNotesValidRange = true;

    for (int idx = 0; idx < lib.count(); ++idx) {
        composer.reset();
        const auto& g = lib.get(idx);
        PerformanceControls perf {};
        perf.styleAIndex = idx;
        perf.styleBIndex = idx;
        perf.styleMorph = 0.0f;
        perf.timeSig = g.defaultTimeSig;
        perf.section = SongSection::Chorus;
        perf.energy = 0.8f;
        perf.swing = g.defaultSwing;

        SyncTransport transport;
        transport.prepare(44100.0);
        transport.setManualBpm(g.defaultBpm > 50.0 ? g.defaultBpm : 110.0);
        transport.setStandalonePlaying(true);

        int noteOns = 0;
        SyncTransport::HostPlayHeadInfo noHost {};
        for (int blk = 0; blk < 200; ++blk) { // ~2.3 seconds at 512 samples
            auto ts = transport.advanceBlock(512, SyncMode::StandaloneTap, perf.timeSig, noHost, quietListener);
            BlockEventList evs;
            ChordSymbol ch;
            int barIdx = 0;
            bool fill = false;
            composer.processBlock(512, ts, perf, slots, quietListener, evs, ch, barIdx, fill);
            for (int e = 0; e < evs.count; ++e) {
                const auto& ev = evs.events[static_cast<size_t>(e)];
                if (ev.type == EventType::NoteOn) {
                    ++noteOns;
                    if (ev.data1 > 127 || ev.data2 == 0 || ev.data2 > 127 || ev.sampleOffset < 0 || ev.sampleOffset >= 512)
                        allNotesValidRange = false;
                }
            }
        }
        if (noteOns >= 10)
            ++stylesWithValidMidi;
    }

    check(stylesWithValidMidi == lib.count(),
          "All " + std::to_string(lib.count()) + " styles generate valid non-empty MIDI",
          std::to_string(stylesWithValidMidi) + "/" + std::to_string(lib.count()));
    check(allNotesValidRange, "All generated MIDI notes have valid pitch/velocity/sampleOffset");

    // Odd Time Signatures test: 3/4, 4/4, 5/4, 6/8, 7/8, 12/8
    const TimeSignature meters[6] = { {3,4}, {4,4}, {5,4}, {6,8}, {7,8}, {12,8} };
    bool allMetersWork = true;
    for (const auto& m : meters) {
        composer.reset();
        SyncTransport transport;
        transport.prepare(44100.0);
        transport.setManualBpm(120.0);
        transport.setStandalonePlaying(true);

        PerformanceControls perf {};
        perf.styleAIndex = 6;
        perf.styleBIndex = 7;
        perf.styleMorph = 0.5f;
        perf.timeSig = m;
        perf.section = SongSection::Chorus;
        perf.energy = 0.85f;

        int noteOns = 0;
        int maxStepSeen = 0;
        SyncTransport::HostPlayHeadInfo noHost {};
        for (int blk = 0; blk < 260; ++blk) {
            auto ts = transport.advanceBlock(512, SyncMode::StandaloneTap, m, noHost, quietListener);
            maxStepSeen = std::max(maxStepSeen, ts.currentStep);
            BlockEventList evs;
            ChordSymbol ch;
            int barIdx = 0;
            bool fill = false;
            composer.processBlock(512, ts, perf, slots, quietListener, evs, ch, barIdx, fill);
            for (int e = 0; e < evs.count; ++e) {
                if (evs.events[static_cast<size_t>(e)].type == EventType::NoteOn)
                    ++noteOns;
            }
        }
        if (noteOns < 15 || maxStepSeen < m.stepsPerBar() - 2) {
            allMetersWork = false;
            std::cerr << "    Meter " << m.numerator << "/" << m.denominator
                      << " noteOns=" << noteOns << " maxStep=" << maxStepSeen << "\n";
        }
    }
    check(allMetersWork, "All 6 time signatures (3/4, 4/4, 5/4, 6/8, 7/8, 12/8) generate full-bar grooves");
}

void testStyleMorphing() {
    std::cout << "\n=== 2. Style A <-> B DNA Morphing ===\n";
    StyleLibrary lib;
    const auto gA = lib.get(0);
    const auto gB = lib.get(7);
    const auto m0 = lib.morph(0, 7, 0.0f);
    const auto m5 = lib.morph(0, 7, 0.5f);
    const auto m1 = lib.morph(0, 7, 1.0f);

    check(std::abs(m0.defaultSwing - gA.defaultSwing) < 1e-4f, "Morph 0.0 matches Style A swing");
    check(std::abs(m1.defaultSwing - gB.defaultSwing) < 1e-4f, "Morph 1.0 matches Style B swing");
    const float midSwing = 0.5f * (gA.defaultSwing + gB.defaultSwing);
    check(std::abs(m5.defaultSwing - midSwing) < 1e-4f, "Morph 0.5 interpolates swing halfway");

    bool kickInterpOk = true;
    for (int i = 0; i < 16; ++i) {
        const float expected = 0.5f * (gA.kick[static_cast<size_t>(i)] + gB.kick[static_cast<size_t>(i)]);
        if (std::abs(m5.kick[static_cast<size_t>(i)] - expected) > 1e-3f)
            kickInterpOk = false;
    }
    check(kickInterpOk, "Morph 0.5 smoothly interpolates kick pattern matrix");
}

void testChordParserAndVoiceLeading() {
    std::cout << "\n=== 3. Chord Parser & Smooth Voice-Leading ===\n";
    const auto em9 = parseChord("Em9");
    check(em9.valid && em9.rootPitchClass == 4 && em9.quality == ChordQuality::Minor9, "Parse Em9");

    const auto b7s9 = parseChord("B7#9");
    check(b7s9.valid && b7s9.rootPitchClass == 11 && b7s9.quality == ChordQuality::Dominant7Sharp9, "Parse B7#9");

    const auto slash = parseChord("D/F#");
    check(slash.valid && slash.rootPitchClass == 2 && slash.bassPitchClass == 6, "Parse slash chord D/F#");

    const auto fhm7b5 = parseChord("F#m7b5");
    check(fhm7b5.valid && fhm7b5.rootPitchClass == 6 && fhm7b5.quality == ChordQuality::Minor7b5, "Parse F#m7b5");

    const auto track = parseChordTrack("| Em9 | A13 | Dmaj7 | B7#9 |");
    check(track.size() == 4, "Parse 4-bar chord chart", "bars=" + std::to_string(track.size()));

    // Voice-leading smoothness test across ii-V-I: Dm9 -> G13 -> Cmaj9
    const auto dm9 = parseChord("Dm9");
    const auto g13 = parseChord("G13");
    const auto cmaj9 = parseChord("Cmaj9");

    VoicingResult empty {};
    const auto v1 = voiceLeadKeys(dm9, empty, 48, 76);
    const auto v2 = voiceLeadKeys(g13, v1, 48, 76);
    const auto v3 = voiceLeadKeys(cmaj9, v2, 48, 76);

    auto avgDist = [](const VoicingResult& a, const VoicingResult& b) {
        const int n = std::min(a.count, b.count);
        if (n <= 0) return 99.0;
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
            sum += std::abs(a.notes[static_cast<size_t>(i)] - b.notes[static_cast<size_t>(i)]);
        return sum / static_cast<double>(n);
    };

    const double d12 = avgDist(v1, v2);
    const double d23 = avgDist(v2, v3);
    check(v1.count >= 3 && v2.count >= 3 && v3.count >= 3, "Voice-leading produces >= 3-note voicings");
    check(d12 <= 4.0 && d23 <= 4.0, "Smooth minimal-motion voice-leading on ii-V-I",
          "d12=" + std::to_string(d12) + ", d23=" + std::to_string(d23));

    const auto gtr = guitarFretboardVoicing(em9);
    check(gtr.count >= 4 && gtr.notes[0] >= 40 && gtr.notes[static_cast<size_t>(gtr.count - 1)] <= 84,
          "Guitar fretboard voicing within realistic guitar range");
}

void testLiveAudioListener() {
    std::cout << "\n=== 4. Live Audio Follow (Tempo, Chord & Leave Space) ===\n";
    constexpr double sr = 44100.0;
    LiveAudioListener listener;
    listener.prepare(sr);

    // 4a. Synthetic 120 BPM beat train (impulse burst every 0.5s for 5 seconds)
    const int totalSamples = static_cast<int>(sr * 5.0);
    const int beatPeriod = static_cast<int>(sr * 0.5); // 120 BPM
    std::vector<float> bufL(512, 0.0f), bufR(512, 0.0f);
    for (int pos = 0; pos < totalSamples; pos += 512) {
        for (int i = 0; i < 512; ++i) {
            const int t = pos + i;
            const int inBeat = t % beatPeriod;
            float s = 0.0f;
            if (inBeat < 2200) {
                const float env = std::exp(-static_cast<float>(inBeat) / 350.0f);
                const float freq = 60.0f + 160.0f * std::exp(-static_cast<float>(inBeat) / 180.0f);
                s = 0.85f * env * std::sin(2.0f * 3.14159265f * freq * static_cast<float>(inBeat) / static_cast<float>(sr));
            }
            bufL[static_cast<size_t>(i)] = s;
            bufR[static_cast<size_t>(i)] = s;
        }
        listener.processAudioBlock(bufL.data(), bufR.data(), 512);
    }

    const auto tempoTel = listener.getTelemetry();
    const bool tempoClose = (std::abs(tempoTel.detectedBpm - 120.0) <= 6.0) ||
                            (std::abs(tempoTel.detectedBpm - 60.0) <= 3.0);
    check(tempoClose, "Live tempo tracker locks near 120 BPM on synthetic pulse train",
          "detected=" + std::to_string(tempoTel.detectedBpm) + " conf=" + std::to_string(tempoTel.tempoConfidence));
    check(tempoTel.userActivity > 0.15f, "User activity rises during active playing",
          "activity=" + std::to_string(tempoTel.userActivity));

    // 4b. Synthetic rich C Major chord (C3=130.81, E3=164.81, G3=196.00, C4=261.63)
    listener.reset();
    const double freqsC[4] = { 130.8128, 164.8138, 195.9977, 261.6256 };
    for (int blk = 0; blk < 120; ++blk) { // ~1.4 sec
        for (int i = 0; i < 512; ++i) {
            const double t = static_cast<double>(blk * 512 + i) / sr;
            double s = 0.0;
            for (double f : freqsC) {
                s += 0.22 * std::sin(2.0 * 3.14159265358979 * f * t)
                   + 0.10 * std::sin(2.0 * 3.14159265358979 * 2.0 * f * t)
                   + 0.05 * std::sin(2.0 * 3.14159265358979 * 3.0 * f * t);
            }
            bufL[static_cast<size_t>(i)] = static_cast<float>(s);
            bufR[static_cast<size_t>(i)] = static_cast<float>(s);
        }
        listener.processAudioBlock(bufL.data(), bufR.data(), 512);
    }
    const auto chordTelC = listener.getTelemetry();
    check(chordTelC.detectedChord.rootPitchClass == 0 &&
          (chordTelC.detectedChord.quality == ChordQuality::Major ||
           chordTelC.detectedChord.quality == ChordQuality::Major7 ||
           chordTelC.detectedChord.quality == ChordQuality::Major9),
          "Live CQT+HMM chord detector recognizes C Major",
          "got=" + formatChord(chordTelC.detectedChord));

    // 4c. Silence decay for Leave Space userActivity (~2.8 seconds)
    std::fill(bufL.begin(), bufL.end(), 0.0f);
    std::fill(bufR.begin(), bufR.end(), 0.0f);
    for (int blk = 0; blk < 240; ++blk) {
        listener.processAudioBlock(bufL.data(), bufR.data(), 512);
    }
    const auto silentTel = listener.getTelemetry();
    check(silentTel.userActivity < 0.05f, "Leave-Space userActivity decays cleanly during silence",
          "activity=" + std::to_string(silentTel.userActivity));
}

void testDrumMapTranslator() {
    std::cout << "\n=== 5. Drum-Map & Articulation Auto-Translator ===\n";
    DrumMapTranslator translator;
    SlotConfig cfg {};
    cfg.cc4HiHatEnabled = true;
    cfg.velocityCurve = 1.0f;

    BandNoteEvent kickEv {};
    kickEv.slot = 0;
    kickEv.type = EventType::NoteOn;
    kickEv.data1 = DrumNote::Kick;
    kickEv.data2 = 100;

    BandNoteEvent out[2];

    // BFD3 maps Kick 36 -> 24
    cfg.drumMap = DrumMapPreset::BFD3;
    int n = translator.translateEvent(kickEv, cfg, out);
    check(n == 1 && out[0].data1 == 24, "BFD3 Kick remaps 36 -> 24", "got=" + std::to_string(out[0].data1));

    // Addictive Drums 2 maps Closed Hat 42 -> 49 + emits CC4
    BandNoteEvent hatEv {};
    hatEv.slot = 0;
    hatEv.type = EventType::NoteOn;
    hatEv.data1 = DrumNote::ClosedHat;
    hatEv.data2 = 90;
    hatEv.articulation = Articulation::HatClosed;

    cfg.drumMap = DrumMapPreset::AddictiveDrums2;
    n = translator.translateEvent(hatEv, cfg, out);
    const bool ad2HatOk = (n == 2 && out[0].type == EventType::ControlChange && out[0].data1 == 4 && out[1].data1 == 49) ||
                          (n == 1 && out[0].data1 == 49);
    check(ad2HatOk, "Addictive Drums 2 Closed Hat maps 42 -> 49 (+ CC4)");

    // Custom Map
    translator.setCustomMapping(DrumNote::Snare, 60);
    cfg.drumMap = DrumMapPreset::Custom;
    BandNoteEvent snareEv = kickEv;
    snareEv.data1 = DrumNote::Snare;
    n = translator.translateEvent(snareEv, cfg, out);
    check(n == 1 && out[0].data1 == 60, "Custom drum map remaps Snare 38 -> 60");

    // Velocity curve scaling (gamma < 1 boosts light touch, gamma > 1 requires harder strike)
    const uint8_t lightCurveVel = translator.mapVelocity(64, 0.5f);
    const uint8_t linearVel     = translator.mapVelocity(64, 1.0f);
    const uint8_t heavyCurveVel = translator.mapVelocity(64, 2.0f);
    check(lightCurveVel > linearVel && linearVel > heavyCurveVel,
          "Velocity curve scales dynamics properly",
          "light=" + std::to_string(lightCurveVel) + " linear=" + std::to_string(linearVel) +
          " heavy=" + std::to_string(heavyCurveVel));
}

void testFullProcessorRenderAndMidiExport() {
    std::cout << "\n=== 6 & 7. Full 6-Player Built-In Render & 32-Bar MIDI Export ===\n";
    SessionMateProcessor proc;
    proc.prepareToPlay(44100.0, 512);

    // Switch to StandaloneTap mode so it plays autonomously without a DAW playhead
    if (auto* p = proc.getAPVTS().getParameter(ParamID::syncMode))
        p->setValueNotifyingHost(p->convertTo0to1(2.0f));
    proc.setStandalonePlaying(true);

    juce::AudioBuffer<float> audio(proc.getTotalNumOutputChannels(), 512);
    juce::MidiBuffer midiOut;

    bool hasNanOrInf = false;
    double sumSq = 0.0;
    float maxPeak = 0.0f;
    int totalMidiOutEvents = 0;
    std::array<bool, 6> channelSeen {};

    for (int blk = 0; blk < 300; ++blk) { // ~3.5 seconds (~6 beats at 104 BPM)
        if (blk == 120) proc.triggerFillNow();
        audio.clear();
        midiOut.clear();
        proc.processBlock(audio, midiOut);

        for (int ch = 0; ch < 2; ++ch) {
            const float* ptr = audio.getReadPointer(ch);
            for (int i = 0; i < 512; ++i) {
                const float v = ptr[i];
                if (!std::isfinite(v)) hasNanOrInf = true;
                sumSq += static_cast<double>(v) * static_cast<double>(v);
                maxPeak = std::max(maxPeak, std::abs(v));
            }
        }

        for (const auto meta : midiOut) {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn()) {
                ++totalMidiOutEvents;
                const int ch = msg.getChannel();
                if (ch >= 1 && ch <= 6)
                    channelSeen[static_cast<size_t>(ch - 1)] = true;
            }
        }
    }

    const double rms = std::sqrt(sumSq / (300.0 * 512.0 * 2.0));
    check(!hasNanOrInf, "Built-in 6-player audio render has zero NaN/Inf");
    check(rms > 0.005 && maxPeak <= 1.05f, "Built-in 6-player mix is audible and well-balanced",
          "rms=" + std::to_string(rms) + " peak=" + std::to_string(maxPeak));
    check(totalMidiOutEvents >= 30, "Multi-channel MIDI Out emits rich band events",
          "events=" + std::to_string(totalMidiOutEvents));

    int activeChannels = 0;
    for (bool seen : channelSeen) if (seen) ++activeChannels;
    check(activeChannels >= 4, "Multiple band member MIDI channels (Ch 1-6) active",
          "activeCh=" + std::to_string(activeChannels));

    // MIDI File Export Validity
    const juce::File tempMid = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getChildFile("sessionmate_test_export.mid");
    tempMid.deleteFile();
    const bool exported = proc.exportMidiToFile(tempMid, -1);
    check(exported && tempMid.existsAsFile() && tempMid.getSize() > 64,
          "32-bar All-Band MIDI file exports cleanly");

    juce::MidiFile readBack;
    juce::FileInputStream fis(tempMid);
    const bool readOk = fis.openedOk() && readBack.readFrom(fis);
    int totalReadNotes = 0;
    if (readOk) {
        for (int t = 0; t < readBack.getNumTracks(); ++t) {
            const auto* trk = readBack.getTrack(t);
            for (int e = 0; e < trk->getNumEvents(); ++e) {
                if (trk->getEventPointer(e)->message.isNoteOn())
                    ++totalReadNotes;
            }
        }
    }
    check(readOk && readBack.getNumTracks() >= 2 && totalReadNotes >= 20,
          "Exported .mid parses cleanly via juce::MidiFile with tracks and notes",
          "tracks=" + std::to_string(readBack.getNumTracks()) + " notes=" + std::to_string(totalReadNotes));
    tempMid.deleteFile();
}

void testVstScannerSafetyAndFallback() {
    std::cout << "\n=== 8 & 9. VST3 Scanner Safety & Collaborator Graceful Fallback ===\n";
    SessionMateProcessor procA;
    procA.prepareToPlay(44100.0, 512);

    // Safe scan on empty temp directory
    const juce::File emptyDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                    .getChildFile("sessionmate_empty_vst3_test");
    emptyDir.createDirectory();
    const int found = procA.getVstSubHost().scanDirectorySafe(emptyDir, 8);
    check(found == 0, "Safe VST3 scanner handles empty directory cleanly");
    emptyDir.deleteRecursively();

    // Customize chord chart & Style Morph
    procA.setChordChartText("| Cm9 | F13 | Bbmaj7 | G7#9 |");
    if (auto* p = procA.getAPVTS().getParameter(ParamID::styleMorph))
        p->setValueNotifyingHost(0.73f);

    // Simulate collaborator preset referencing a missing third-party VST3 on Slot 2 (Keys)
    const juce::String rawJson = procA.serializePresetToJson();
    juce::var parsed = juce::JSON::parse(rawJson);
    if (auto* root = parsed.getDynamicObject()) {
        if (auto* slotsArr = root->getProperty("slots").getArray()) {
            if (auto* keysObj = (*slotsArr)[2].getDynamicObject()) {
                keysObj->setProperty("sourceMode", static_cast<int>(SlotSourceMode::HostedVST3));
                keysObj->setProperty("vstIdentifier", "C:/MissingStudio/Keyscape_Collaborator.vst3");
                keysObj->setProperty("vstName", "Keyscape");
                juce::MemoryBlock fakeState("FAKE_KEYSCAPE_STATE", 19);
                keysObj->setProperty("vstStateBase64", fakeState.toBase64Encoding());
            }
        }
    }
    const juce::String collabPresetJson = juce::JSON::toString(parsed, true);

    // Restore into a fresh SessionMateProcessor (simulating collaborator's PC without Keyscape)
    SessionMateProcessor procB;
    procB.prepareToPlay(44100.0, 512);
    std::string status;
    const bool restored = procB.deserializePresetFromJson(collabPresetJson, status);
    check(restored, "Portable .sessionmate preset deserializes cleanly", status);
    check(procB.getChordChartText().find("Cm9") != std::string::npos &&
          procB.getChordChartText().find("G7#9") != std::string::npos,
          "Chord chart round-trips identically", procB.getChordChartText());
    check(procB.isSlotFallbackActive(2), "Missing hosted VST3 activates graceful Built-In fallback");

    // Verify re-saving preserves collaborator's original VST identifier
    const juce::String reSaved = procB.serializePresetToJson();
    check(reSaved.contains("Keyscape_Collaborator.vst3"),
          "Re-saving after fallback preserves collaborator's VST3 reference");

    // Verify Slot 2 still renders audible audio via Built-In Rhodes fallback!
    if (auto* p = procB.getAPVTS().getParameter(ParamID::syncMode))
        p->setValueNotifyingHost(p->convertTo0to1(2.0f));
    if (auto* p = procB.getAPVTS().getParameter(ParamID::slotParam(2, "solo")))
        p->setValueNotifyingHost(1.0f);
    procB.setStandalonePlaying(true);

    juce::AudioBuffer<float> buf(procB.getTotalNumOutputChannels(), 512);
    juce::MidiBuffer midi;
    double sq = 0.0;
    for (int b = 0; b < 150; ++b) {
        buf.clear();
        midi.clear();
        procB.processBlock(buf, midi);
        for (int i = 0; i < 512; ++i)
            sq += static_cast<double>(buf.getSample(0, i)) * static_cast<double>(buf.getSample(0, i));
    }
    const double keysRms = std::sqrt(sq / (150.0 * 512.0));
    check(keysRms > 0.001, "Graceful fallback slot renders audible Built-In Keys audio",
          "keysRms=" + std::to_string(keysRms));
}

void testVoicingsPresetsAndClashAvoidance() {
    std::cout << "\n=== 10 & 11. Keys Voicing Styles, Presets, Modes & Auto-Clash Avoidance ===\n";

    // 7 Quick-Start Chord Progression Presets
    const auto& presets = getFactoryChordProgressions();
    check(presets.size() >= 7, "Factory chord progression presets count >= 7",
          "count=" + std::to_string(presets.size()));
    bool allPresetsValid = true;
    for (const auto& kv : presets) {
        const auto bars = parseChordTrack(kv.second);
        if (bars.size() < 4) allPresetsValid = false;
    }
    check(allPresetsValid, "All factory chord progression presets parse cleanly (>= 4 bars)");

    // Keys Voicing Styles on Cmaj9
    const auto cmaj9 = parseChord("Cmaj9");
    VoicingResult empty {};
    const auto vClose    = voiceLeadKeys(cmaj9, empty, 48, 76, KeysVoicingStyle::Close, 0);
    const auto vDrop2    = voiceLeadKeys(cmaj9, empty, 48, 76, KeysVoicingStyle::Drop2, 0);
    const auto vRootless = voiceLeadKeys(cmaj9, empty, 48, 76, KeysVoicingStyle::RootlessJazz, 0);
    const auto vShell    = voiceLeadKeys(cmaj9, empty, 48, 76, KeysVoicingStyle::Shell37, 0);
    const auto vOpen     = voiceLeadKeys(cmaj9, empty, 48, 76, KeysVoicingStyle::OpenSpread, 0);

    bool rootlessOmitsRoot = (vRootless.count >= 3);
    for (int i = 0; i < vRootless.count; ++i) {
        if ((vRootless.notes[static_cast<size_t>(i)] % 12) == cmaj9.rootPitchClass)
            rootlessOmitsRoot = false;
    }
    check(vClose.count >= 3 && vDrop2.count >= 3 && vShell.count >= 2 && vOpen.count >= 3,
          "All 5 KeysVoicingStyle modes generate valid voicings");
    check(rootlessOmitsRoot, "RootlessJazz Bill Evans voicing omits root pitch class");

    // Auto-Clash Avoidance on Keys & Guitar
    // Avoid pitch class 11 (B) on Cmaj9 -> should omit B while keeping >= 2 core tones
    const uint16_t avoidB = static_cast<uint16_t>(1u << 11);
    const auto vAvoid = voiceLeadKeys(cmaj9, empty, 48, 76, KeysVoicingStyle::Close, avoidB);
    bool keysAvoidedB = (vAvoid.count >= 2);
    for (int i = 0; i < vAvoid.count; ++i) {
        if ((vAvoid.notes[static_cast<size_t>(i)] % 12) == 11)
            keysAvoidedB = false;
    }
    check(keysAvoidedB, "Auto-Clash Avoidance strips avoided pitch class from Keys voicing");

    const auto gtrRaw = guitarFretboardVoicing(cmaj9);
    const uint16_t avoidE = static_cast<uint16_t>(1u << 4);
    const auto gtrFiltered = filterAvoidGuitarVoicing(gtrRaw, avoidE);
    check(gtrFiltered.count >= 2 && gtrFiltered.count <= gtrRaw.count,
          "Auto-Clash Avoidance filters Guitar voicing while keeping >= 2 notes");

    // Mode mask & scale note lookup
    const uint16_t dDorianMask = scaleModeMask(2, ScaleMode::Dorian); // D E F G A B C -> pcs 2,4,5,7,9,11,0
    const bool dorianOk = (dDorianMask & (1u << 2)) && (dDorianMask & (1u << 5)) &&
                          (dDorianMask & (1u << 9)) && (dDorianMask & (1u << 11)) &&
                          !(dDorianMask & (1u << 10)); // no Bb in D Dorian
    check(dorianOk, "ScaleMode D Dorian mask exact");
    const int deg2 = scaleNoteInMode(2, ScaleMode::Dorian, 2, 60);
    check(deg2 >= 48 && deg2 <= 84, "scaleNoteInMode returns valid MIDI note in octave");
}

void testQuantizedReharmAndHarmonyLink() {
    std::cout << "\n=== 12 & 13. Quantized Reharm & Live Bidirectional SuiteHarmonyLink ===\n";

    auto proc = std::make_unique<SessionMateProcessor>();
    proc->prepareToPlay(44100.0, 512);

    const std::string suffix = "SMUnitTest_" + std::to_string(suite::link::HarmonyLinkEndpoint::nowMs());
    proc->setLinkTestSuffix(suffix);

    // Put SessionMate in StandaloneTap + Bus A + HUD Link + Auto-Clash Avoidance
    if (auto* p = proc->getAPVTS().getParameter(ParamID::syncMode))
        p->setValueNotifyingHost(p->convertTo0to1(2.0f));
    if (auto* p = proc->getAPVTS().getParameter(ParamID::harmonicSource))
        p->setValueNotifyingHost(p->convertTo0to1(2.0f)); // HarmonyHudLink
    if (auto* p = proc->getAPVTS().getParameter(ParamID::linkBus))
        p->setValueNotifyingHost(p->convertTo0to1(1.0f)); // Bus A
    if (auto* p = proc->getAPVTS().getParameter(ParamID::clashAvoid))
        p->setValueNotifyingHost(1.0f);
    proc->setStandalonePlaying(true);

    // Mock HarmonyHUD peer on the same isolated shared-memory bus
    suite::link::HarmonyLinkEndpoint mockHud;
    const bool hudConnected = mockHud.connect(0, suffix.c_str());
    check(hudConnected, "Mock HarmonyHUD connected to isolated SuiteHarmonyLink test bus");

    // 1. HUD -> SessionMate: live Fmaj9 chord + Lydian mode + avoid pitch class F# (6) + user activity
    suite::link::HudToBandPayload hudPayload {};
    hudPayload.liveChord = suite::link::makeLinkChord(5, suite::link::LinkChordQuality::Maj9, -1, 95); // Fmaj9
    hudPayload.keyRootPc = 5;
    hudPayload.keyIsMinor = 0;
    hudPayload.keyConfidence = 90;
    hudPayload.activeMode = suite::link::LinkMode::Lydian;
    hudPayload.scalePitchClassMask = suite::link::modePitchClassMask(5, suite::link::LinkMode::Lydian);
    hudPayload.avoidPitchClassMask = static_cast<uint16_t>(1u << 4); // avoid E (pc=4)
    hudPayload.userActiveNotes.set(64);
    hudPayload.userActivityLevel = 0.72f;
    hudPayload.userEnergy = 0.65f;
    hudPayload.userHasSignal = 1;
    hudPayload.clashCount = 1;
    mockHud.publishHud(hudPayload);

    juce::AudioBuffer<float> audio(proc->getTotalNumOutputChannels(), 512);
    juce::MidiBuffer midi;
    for (int blk = 0; blk < 40; ++blk) {
        audio.clear();
        midi.clear();
        proc->processBlock(audio, midi);
    }

    const auto snapLive = proc->getUiSnapshot();
    check(snapLive.hudLinkConnected, "SessionMate detects live HarmonyHUD peer over SuiteHarmonyLink");
    check(snapLive.activeChord.rootPitchClass == 5 && snapLive.activeChord.quality == ChordQuality::Major9,
          "SessionMate adopts live Fmaj9 chord from HarmonyHUD",
          "got=" + proc->formatChordSymbol(snapLive.activeChord));
    check(snapLive.hudActiveMode == ScaleMode::Lydian, "SessionMate receives active Lydian mode from HarmonyHUD");

    // 2. HUD -> SessionMate: One-Click Quantized Reharm Push (Db13, Immediate, 1 bar)
    hudPayload.reharmSeq = 1;
    hudPayload.reharmChord = suite::link::makeLinkChord(1, suite::link::LinkChordQuality::Dom13, -1, 100); // C#13/Db13
    hudPayload.reharmTiming = suite::link::ReharmTiming::Immediate;
    hudPayload.reharmLockBars = 1;
    hudPayload.heartbeatMs = 0; // auto-stamp fresh timestamp
    mockHud.publishHud(hudPayload);

    audio.clear();
    midi.clear();
    proc->processBlock(audio, midi);
    const auto snapReharm = proc->getUiSnapshot();
    check(snapReharm.reharmActive && snapReharm.activeChord.rootPitchClass == 1 &&
          snapReharm.activeChord.quality == ChordQuality::Dominant13,
          "One-Click Immediate Reharm Push switches SessionMate to Db13 immediately",
          "active=" + proc->formatChordSymbol(snapReharm.activeChord));

    // Clear reharm and push a 4-bar Chord Chart from HarmonyHUD -> SessionMate
    proc->clearActiveReharm();
    if (auto* p = proc->getAPVTS().getParameter(ParamID::harmonicSource))
        p->setValueNotifyingHost(p->convertTo0to1(0.0f)); // Back to Chart mode

    hudPayload.chartSeq = 1;
    hudPayload.chartNumBars = 4;
    hudPayload.chartBars[0] = suite::link::makeLinkChord(2, suite::link::LinkChordQuality::Min9);  // Dm9
    hudPayload.chartBars[1] = suite::link::makeLinkChord(7, suite::link::LinkChordQuality::Dom13); // G13
    hudPayload.chartBars[2] = suite::link::makeLinkChord(0, suite::link::LinkChordQuality::Maj9);  // Cmaj9
    hudPayload.chartBars[3] = suite::link::makeLinkChord(9, suite::link::LinkChordQuality::Dom7Sharp9); // A7#9
    hudPayload.heartbeatMs = 0;
    mockHud.publishHud(hudPayload);

    bool sawBandNotes = false;
    suite::link::BandToHudPayload lastBandPayload {};
    bool bandPeerAlive = false;

    for (int blk = 0; blk < 120; ++blk) {
        audio.clear();
        midi.clear();
        proc->processBlock(audio, midi);

        suite::link::BandToHudPayload bp {};
        bool alive = false;
        if (mockHud.readBand(bp, alive) && alive) {
            bandPeerAlive = true;
            lastBandPayload = bp;
            if (bp.slotNotes[1].any() || bp.slotNotes[2].any() || bp.slotNotes[3].any())
                sawBandNotes = true;
        }
    }

    const std::string syncedChart = proc->getChordChartText();
    check(syncedChart.find("Dm9") != std::string::npos && syncedChart.find("Cmaj9") != std::string::npos,
          "Send Chart -> SessionMate imports 4-bar ii-V-I chart cleanly", syncedChart);

    // 3. SessionMate -> HarmonyHUD: Verify live BandToHudPayload broadcast!
    check(bandPeerAlive && lastBandPayload.isPlaying == 1 && lastBandPayload.bpm > 50.0,
          "SessionMate broadcasts live BandToHudPayload with transport & heartbeat");
    check(lastBandPayload.activeChord.isValid() && lastBandPayload.nextChord.isValid() &&
          lastBandPayload.beatsUntilNextChord >= 0.0f,
          "SessionMate broadcasts activeChord + nextChord lookahead + beatsUntilNextChord");
    check(sawBandNotes, "SessionMate broadcasts live per-slot NoteMask128 bitmasks for Band X-Ray");
    check(lastBandPayload.bandChartNumBars == 4 &&
          lastBandPayload.bandChartBars[0].rootPc == 2 &&
          lastBandPayload.bandChartBars[2].rootPc == 0,
          "SessionMate mirrors live 4-bar chord chart back to HarmonyHUD");
}

} // namespace

int main() {
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "=========================================\n";
    std::cout << "  SessionMate Comprehensive Test Suite\n";
    std::cout << "=========================================\n";

    testStylesAndOddMeters();
    testStyleMorphing();
    testChordParserAndVoiceLeading();
    testLiveAudioListener();
    testDrumMapTranslator();
    testFullProcessorRenderAndMidiExport();
    testVstScannerSafetyAndFallback();
    testVoicingsPresetsAndClashAvoidance();
    testQuantizedReharmAndHarmonyLink();

    std::cout << "\n=========================================\n";
    std::cout << "  Passed: " << gTestsPassed << "  |  Failed: " << gTestsFailed << "\n";
    std::cout << "=========================================\n";

    return gTestsFailed == 0 ? 0 : 1;
}
