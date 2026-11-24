#include <SuiteHarmonyLink.h>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int    kBlockSize  = 512;
constexpr double kBpm        = 110.0;
constexpr double kTwoPi      = 6.283185307179586476925286766559;

struct TestStats {
    int passed = 0;
    int failed = 0;

    void check(bool cond, const std::string& label, const std::string& detail = {}) {
        if (cond) {
            ++passed;
            std::cout << "  [PASS] " << label;
            if (!detail.empty()) std::cout << " (" << detail << ")";
            std::cout << "\n";
        } else {
            ++failed;
            std::cerr << "  [FAIL] " << label;
            if (!detail.empty()) std::cerr << " (" << detail << ")";
            std::cerr << "\n";
        }
    }
};

class MockPlayHead final : public juce::AudioPlayHead {
public:
    void reset(double bpm, int num = 4, int den = 4) {
        bpm_ = bpm;
        timeSigNum_ = num;
        timeSigDen_ = den;
        samplePos_ = 0;
        playing_ = true;
    }

    void advance(int numSamples) {
        samplePos_ += numSamples;
    }

    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo info;
        info.setIsPlaying(playing_);
        info.setBpm(bpm_);
        info.setTimeSignature(TimeSignature{ timeSigNum_, timeSigDen_ });
        info.setTimeInSamples(samplePos_);
        const double sec = static_cast<double>(samplePos_) / kSampleRate;
        info.setTimeInSeconds(sec);
        const double ppq = sec * (bpm_ / 60.0);
        info.setPpqPosition(ppq);
        const double beatsPerBar = static_cast<double>(timeSigNum_) * (4.0 / static_cast<double>(timeSigDen_));
        const int64_t bar = static_cast<int64_t>(std::floor(ppq / beatsPerBar));
        info.setBarCount(bar);
        info.setPpqPositionOfLastBarStart(static_cast<double>(bar) * beatsPerBar);
        return info;
    }

private:
    double  bpm_        = kBpm;
    int     timeSigNum_ = 4;
    int     timeSigDen_ = 4;
    int64_t samplePos_  = 0;
    bool    playing_    = true;
};

struct ChordSynth {
    std::vector<int> midiNotes;
    std::vector<double> phases;

    void setChord(const std::vector<int>& notes) {
        midiNotes = notes;
        phases.assign(notes.size(), 0.0);
    }

    void renderStereo(juce::AudioBuffer<float>& buf, int numSamples, float gain = 0.22f) {
        buf.clear();
        if (midiNotes.empty()) return;
        const float norm = gain / static_cast<float>(midiNotes.size());
        auto* L = buf.getWritePointer(0);
        auto* R = buf.getNumChannels() > 1 ? buf.getWritePointer(1) : nullptr;
        for (int i = 0; i < numSamples; ++i) {
            double s = 0.0;
            for (size_t k = 0; k < midiNotes.size(); ++k) {
                const double freq = 440.0 * std::pow(2.0, (static_cast<double>(midiNotes[k]) - 69.0) / 12.0);
                const double ph = phases[k];
                // Rich warm Rhodes/piano-like harmonic spectrum
                s += std::sin(ph)
                   + 0.42 * std::sin(2.0 * ph)
                   + 0.18 * std::sin(3.0 * ph)
                   + 0.07 * std::sin(4.0 * ph);
                phases[k] += kTwoPi * freq / kSampleRate;
                if (phases[k] >= kTwoPi) phases[k] -= kTwoPi;
            }
            const float out = static_cast<float>(s) * norm;
            L[i] = out;
            if (R) R[i] = out;
        }
    }
};

std::unique_ptr<juce::AudioPluginInstance> loadVst3Binary(
    juce::AudioPluginFormatManager& fmtMgr,
    const juce::File& vst3Path,
    std::string& outError)
{
    if (!vst3Path.exists()) {
        outError = "Path does not exist: " + vst3Path.getFullPathName().toStdString();
        return nullptr;
    }

    juce::OwnedArray<juce::PluginDescription> descs;
    for (int i = 0; i < fmtMgr.getNumFormats(); ++i) {
        auto* fmt = fmtMgr.getFormat(i);
        if (fmt && fmt->fileMightContainThisPluginType(vst3Path.getFullPathName())) {
            fmt->findAllTypesForFile(descs, vst3Path.getFullPathName());
        }
    }

    if (descs.isEmpty()) {
        outError = "No VST3 plugin descriptions found in: " + vst3Path.getFullPathName().toStdString();
        return nullptr;
    }

    juce::String errStr;
    auto inst = fmtMgr.createPluginInstance(*descs[0], kSampleRate, kBlockSize, errStr);
    if (!inst) {
        outError = "createPluginInstance failed: " + errStr.toStdString();
    }
    return inst;
}

juce::AudioProcessorParameter* findParam(juce::AudioProcessor& proc, const juce::String& nameSubstr) {
    for (auto* p : proc.getParameters()) {
        if (p && p->getName(128).containsIgnoreCase(nameSubstr)) {
            return p;
        }
    }
    return nullptr;
}

bool setChoiceParam(juce::AudioProcessor& proc, const juce::String& nameSubstr, int choiceIdx, int numChoices, std::string& readback) {
    auto* p = findParam(proc, nameSubstr);
    if (!p) return false;
    const float norm = (numChoices > 1) ? static_cast<float>(choiceIdx) / static_cast<float>(numChoices - 1) : 0.0f;
    p->setValueNotifyingHost(norm);
    readback = p->getName(64).toStdString() + "=" + p->getCurrentValueAsText().toStdString();
    return true;
}

bool setBoolParam(juce::AudioProcessor& proc, const juce::String& nameSubstr, bool val, std::string& readback) {
    auto* p = findParam(proc, nameSubstr);
    if (!p) return false;
    p->setValueNotifyingHost(val ? 1.0f : 0.0f);
    readback = p->getName(64).toStdString() + "=" + p->getCurrentValueAsText().toStdString();
    return true;
}

std::string chordStr(const suite::link::LinkChord& c) {
    char buf[32]{};
    suite::link::formatLinkChord(c, buf, sizeof(buf));
    return std::string(buf);
}

} // namespace

int main() {
    juce::ScopedJuceInitialiser_GUI juceInit;
    TestStats stats;

    std::cout << "=== Joint Cross-Plugin Live Integration Test (HarmonyHUD.vst3 <-> SessionMate.vst3) ===\n\n";

    // 1. Load real compiled Release VST3 binaries
    juce::AudioPluginFormatManager fmtMgr;
    fmtMgr.addFormat(new juce::VST3PluginFormat());

    const juce::File hudFile("D:\\Portable Workstation\\DAW\\PluginSuite\\dist\\HarmonyHUD\\HarmonyHUD.vst3");
    const juce::File smFile ("D:\\Portable Workstation\\DAW\\PluginSuite\\dist\\SessionMate\\SessionMate.vst3");

    std::string errHud, errSm;
    auto hud = loadVst3Binary(fmtMgr, hudFile, errHud);
    auto sm  = loadVst3Binary(fmtMgr, smFile,  errSm);

    stats.check(hud != nullptr, "Load HarmonyHUD.vst3 Release binary",
                hud ? (hud->getName().toStdString() + ", params=" + std::to_string(hud->getParameters().size())) : errHud);
    stats.check(sm != nullptr, "Load SessionMate.vst3 Release binary",
                sm ? (sm->getName().toStdString() + ", params=" + std::to_string(sm->getParameters().size())) : errSm);

    if (!hud || !sm) {
        std::cerr << "\nAborting: failed to load one or both VST3 binaries.\n";
        return 1;
    }

    // Configure clean main stereo buses (disabling HarmonyHUD sidechain bus so Band Clash Radar activates!)
    hud->disableNonMainBuses();
    sm->disableNonMainBuses();

    MockPlayHead playHead;
    playHead.reset(kBpm, 4, 4);
    hud->setPlayHead(&playHead);
    sm->setPlayHead(&playHead);

    hud->prepareToPlay(kSampleRate, kBlockSize);
    sm->prepareToPlay(kSampleRate, kBlockSize);

    stats.check(hud->getTotalNumInputChannels() >= 2 && hud->getTotalNumOutputChannels() >= 2,
                "HarmonyHUD stereo bus layout valid",
                "in=" + std::to_string(hud->getTotalNumInputChannels()) + " out=" + std::to_string(hud->getTotalNumOutputChannels()));
    stats.check(sm->getTotalNumOutputChannels() >= 2,
                "SessionMate stereo bus layout valid",
                "in=" + std::to_string(sm->getTotalNumInputChannels()) + " out=" + std::to_string(sm->getTotalNumOutputChannels()));

    // 2. Wire VST3 parameters for Bus A live linking
    std::string rb;
    stats.check(setChoiceParam(*hud, "HarmonyLink Bus", 1, 9, rb), "HarmonyHUD -> Bus A", rb);
    stats.check(setBoolParam(*hud, "Auto-Push Live", true, rb), "HarmonyHUD -> Auto-Push Live ON", rb);
    stats.check(setBoolParam(*hud, "Clash Assist", true, rb), "HarmonyHUD -> Clash Assist ON", rb);

    stats.check(setChoiceParam(*sm, "Sync Mode", 0, 3, rb), "SessionMate -> DAW Sync", rb);
    stats.check(setChoiceParam(*sm, "HarmonyHUD Link Bus", 1, 9, rb), "SessionMate -> Bus A", rb);
    stats.check(setChoiceParam(*sm, "Harmonic Source", 2, 3, rb), "SessionMate -> HUD Link", rb);
    stats.check(setBoolParam(*sm, "Auto Clash Avoidance", true, rb), "SessionMate -> Clash Avoidance ON", rb);

    // Observer endpoint on Bus A (0)
    suite::link::HarmonyLinkEndpoint observer;
    stats.check(observer.connect(0), "SuiteHarmonyLink observer connected to Bus A");

    const int hudCh = std::max({ 2, hud->getTotalNumInputChannels(), hud->getTotalNumOutputChannels() });
    const int smCh  = std::max({ 2, sm->getTotalNumInputChannels(),  sm->getTotalNumOutputChannels() });
    juce::AudioBuffer<float> hudBuf(hudCh, kBlockSize);
    juce::AudioBuffer<float> hudRef(2, kBlockSize);
    juce::AudioBuffer<float> smBuf(smCh, kBlockSize);

    ChordSynth synth;

    auto stepLockstep = [&](int numBlocks,
                            bool injectMidiNote,
                            int injectedMidiNote,
                            float& maxPassThroughErr,
                            float& smPeak,
                            bool& smAllFinite,
                            uint32_t& smMidiChannelsMask,
                            int& smTotalMidiNotes,
                            std::array<suite::link::NoteMask128, 6>& unionSlotNotes,
                            suite::link::HudToBandPayload& lastHud,
                            bool& lastHudAlive,
                            suite::link::BandToHudPayload& lastBand,
                            bool& lastBandAlive,
                            uint8_t& maxClashCountSeen,
                            uint16_t& unionAvoidMask) {
        juce::MidiBuffer hudMidi;
        juce::MidiBuffer smMidi;

        for (int b = 0; b < numBlocks; ++b) {
            hudMidi.clear();
            smMidi.clear();

            synth.renderStereo(hudBuf, kBlockSize, 0.25f);
            hudRef.copyFrom(0, 0, hudBuf, 0, 0, kBlockSize);
            hudRef.copyFrom(1, 0, hudBuf, 1, 0, kBlockSize);

            if (injectMidiNote && b == 0) {
                hudMidi.addEvent(juce::MidiMessage::noteOn(1, injectedMidiNote, 0.9f), 0);
            }

            hud->processBlock(hudBuf, hudMidi);

            // Check HarmonyHUD bit-transparent pass-through
            for (int ch = 0; ch < 2; ++ch) {
                const auto* a = hudRef.getReadPointer(ch);
                const auto* c = hudBuf.getReadPointer(ch);
                for (int i = 0; i < kBlockSize; ++i) {
                    maxPassThroughErr = std::max(maxPassThroughErr, std::abs(a[i] - c[i]));
                }
            }

            smBuf.clear();
            sm->processBlock(smBuf, smMidi);

            for (int ch = 0; ch < 2; ++ch) {
                const auto* ptr = smBuf.getReadPointer(ch);
                for (int i = 0; i < kBlockSize; ++i) {
                    if (!std::isfinite(ptr[i])) smAllFinite = false;
                    else smPeak = std::max(smPeak, std::abs(ptr[i]));
                }
            }

            for (const auto meta : smMidi) {
                const auto msg = meta.getMessage();
                if (msg.isNoteOn()) {
                    const int ch = msg.getChannel();
                    if (ch >= 1 && ch <= 16) smMidiChannelsMask |= (1u << (ch - 1));
                    ++smTotalMidiNotes;
                }
            }

            playHead.advance(kBlockSize);

            // Pace gently so HarmonyHUD's background analyzer thread drains audio FIFO & publishes live IPC
            if ((b & 1) == 1) {
                std::this_thread::sleep_for(std::chrono::milliseconds(6));
            }

            suite::link::HudToBandPayload hp{};
            bool ha = false;
            if (observer.readHud(hp, ha) && ha) {
                lastHud = hp;
                lastHudAlive = true;
                maxClashCountSeen = std::max(maxClashCountSeen, hp.clashCount);
                unionAvoidMask |= hp.avoidPitchClassMask;
            }

            suite::link::BandToHudPayload bp{};
            bool ba = false;
            if (observer.readBand(bp, ba) && ba) {
                lastBand = bp;
                lastBandAlive = true;
                for (int s = 0; s < 6; ++s) {
                    unionSlotNotes[static_cast<size_t>(s)].orWith(bp.slotNotes[s]);
                }
            }
        }

        if (injectMidiNote) {
            hudMidi.clear();
            hudMidi.addEvent(juce::MidiMessage::noteOff(1, injectedMidiNote), 0);
            hud->processBlock(hudBuf, hudMidi);
        }
    };

    // ========================================================================
    // Phase A: Feed Fmaj9 (F3, A3, C4, E4, G4) for ~1.8s
    // ========================================================================
    std::cout << "\n--- Phase A: Live Polyphonic Fmaj9 (HarmonyHUD.vst3 -> SessionMate.vst3) ---\n";
    synth.setChord({ 53, 57, 60, 64, 67 }); // F3, A3, C4, E4, G4

    float maxPassErr = 0.0f;
    float smPeak = 0.0f;
    bool smFinite = true;
    uint32_t smMidiMask = 0;
    int smMidiNotes = 0;
    std::array<suite::link::NoteMask128, 6> slotUnion{};
    suite::link::HudToBandPayload hudSnap{};
    bool hudAlive = false;
    suite::link::BandToHudPayload bandSnap{};
    bool bandAlive = false;
    uint8_t maxClashes = 0;
    uint16_t avoidUnion = 0;

    // 170 blocks * 512 / 48000 = 1.81s
    stepLockstep(170, false, 0, maxPassErr, smPeak, smFinite, smMidiMask, smMidiNotes,
                 slotUnion, hudSnap, hudAlive, bandSnap, bandAlive, maxClashes, avoidUnion);

    stats.check(maxPassErr < 1e-6f, "HarmonyHUD audio pass-through bit-transparent",
                "maxErr=" + std::to_string(maxPassErr));
    stats.check(hudAlive && hudSnap.userHasSignal == 1, "Bus A live HudToBandPayload received",
                "chord=" + chordStr(hudSnap.liveChord) + ", conf=" + std::to_string(hudSnap.liveChord.confidence) +
                "%, mode=" + suite::link::modeName(hudSnap.activeMode));
    stats.check(hudSnap.liveChord.isValid() && hudSnap.liveChord.rootPc == 5 &&
                hudSnap.liveChord.quality == suite::link::LinkChordQuality::Maj9,
                "HarmonyHUD.vst3 recognized Fmaj9 over live audio",
                "detected=" + chordStr(hudSnap.liveChord));

    const bool bassKeysGuitarActive = slotUnion[1].any() && slotUnion[2].any() && slotUnion[3].any();
    stats.check(bandAlive && bandSnap.activeChord.isValid() && bandSnap.activeChord.rootPc == 5,
                "SessionMate.vst3 tracked Fmaj9 live over Bus A",
                "bandActive=" + chordStr(bandSnap.activeChord) +
                ", next=" + chordStr(bandSnap.nextChord) +
                ", style=" + std::string(bandSnap.styleName));
    stats.check(bandSnap.nextChord.isValid(), "SessionMate broadcast valid nextChord lookahead",
                "next=" + chordStr(bandSnap.nextChord) + ", beats=" + std::to_string(bandSnap.beatsUntilNextChord));
    stats.check(bassKeysGuitarActive, "SessionMate broadcast live per-slot NoteMask128 (Bass/Keys/Guitar)",
                "Bass=" + std::to_string(slotUnion[1].any()) +
                " Keys=" + std::to_string(slotUnion[2].any()) +
                " Guitar=" + std::to_string(slotUnion[3].any()) +
                " Horns=" + std::to_string(slotUnion[5].any()));
    stats.check(smFinite && smPeak > 0.01f && smPeak <= 1.2f,
                "SessionMate.vst3 produced finite, audible stereo audio",
                "peak=" + std::to_string(smPeak));

    int midiChCount = 0;
    for (int ch = 0; ch < 6; ++ch) if (smMidiMask & (1u << ch)) ++midiChCount;
    stats.check(smMidiNotes > 10 && midiChCount >= 3,
                "SessionMate.vst3 produced multi-channel MIDI Out (Ch 1..6)",
                "notes=" + std::to_string(smMidiNotes) + ", activeChMask=0x" + juce::String::toHexString(static_cast<int>(smMidiMask)).toStdString());

    // ========================================================================
    // Phase B: Live Chord Change to Dm7 (D3, F3, A3, C4) for ~1.8s
    // ========================================================================
    std::cout << "\n--- Phase B: Live Chord Transition Fmaj9 -> Dm7 ---\n";
    synth.setChord({ 50, 53, 57, 60 }); // D3, F3, A3, C4
    stepLockstep(170, false, 0, maxPassErr, smPeak, smFinite, smMidiMask, smMidiNotes,
                 slotUnion, hudSnap, hudAlive, bandSnap, bandAlive, maxClashes, avoidUnion);

    stats.check(hudAlive && hudSnap.liveChord.isValid() && hudSnap.liveChord.rootPc == 2,
                "HarmonyHUD.vst3 tracked chord change to Dm7",
                "detected=" + chordStr(hudSnap.liveChord));
    stats.check(bandAlive && bandSnap.activeChord.isValid() && bandSnap.activeChord.rootPc == 2,
                "SessionMate.vst3 followed chord change to D root live over Bus A",
                "bandActive=" + chordStr(bandSnap.activeChord));

    // ========================================================================
    // Phase C: Zero-Sidechain Player-vs-Band Clash Radar + Auto-Clash Avoidance
    // ========================================================================
    std::cout << "\n--- Phase C: Zero-Sidechain Player-vs-Band Clash Radar & Avoidance ---\n";
    // Inject MIDI Eb4 (MIDI 63, PC 3) — minor-2nd clash against D (PC 2) and E/C# against SessionMate's Dm7 band!
    maxClashes = 0;
    avoidUnion = 0;
    stepLockstep(100, true, 63, maxPassErr, smPeak, smFinite, smMidiMask, smMidiNotes,
                 slotUnion, hudSnap, hudAlive, bandSnap, bandAlive, maxClashes, avoidUnion);

    stats.check(avoidUnion != 0u,
                "HarmonyHUD.vst3 published non-zero avoidPitchClassMask",
                "avoidMask=0x" + juce::String::toHexString(static_cast<int>(avoidUnion)).toStdString());
    stats.check(maxClashes > 0,
                "HarmonyHUD.vst3 zero-sidechain Band Clash Radar detected live clashes",
                "maxClashCount=" + std::to_string(static_cast<int>(maxClashes)) +
                ", severity=" + std::to_string(static_cast<int>(hudSnap.clashSeverity)));
    stats.check(smFinite && bandAlive && bandSnap.clashAvoidanceActive == 1,
                "SessionMate.vst3 remained finite with clashAvoidanceActive == 1",
                "clashAvoidanceActive=" + std::to_string(static_cast<int>(bandSnap.clashAvoidanceActive)));

    // ========================================================================
    // Phase D: State Save/Restore Round-Trip & Clean Teardown
    // ========================================================================
    std::cout << "\n--- Phase D: State Save/Restore & Clean Teardown ---\n";
    juce::MemoryBlock hudState, smState;
    hud->getStateInformation(hudState);
    sm->getStateInformation(smState);
    stats.check(hudState.getSize() > 0, "HarmonyHUD.vst3 getStateInformation non-empty",
                std::to_string(hudState.getSize()) + " bytes");
    stats.check(smState.getSize() > 0, "SessionMate.vst3 getStateInformation non-empty",
                std::to_string(smState.getSize()) + " bytes");

    hud->setStateInformation(hudState.getData(), static_cast<int>(hudState.getSize()));
    sm->setStateInformation(smState.getData(), static_cast<int>(smState.getSize()));
    stats.check(true, "Both VST3 plugins completed setStateInformation cleanly");

    observer.disconnect();
    hud->releaseResources();
    sm->releaseResources();
    hud.reset();
    sm.reset();
    stats.check(true, "Clean teardown with zero crashes");

    std::cout << "\n========================================================================\n";
    std::cout << "JointHarmonyLinkTest Summary: Passed: " << stats.passed
              << " | Failed: " << stats.failed << "\n";
    std::cout << "========================================================================\n";

    return stats.failed == 0 ? 0 : 1;
}
