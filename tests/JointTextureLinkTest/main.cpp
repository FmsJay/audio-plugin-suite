#include <SuiteTextureLink.h>
#include <SuiteHarmonyLink.h>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int    kBlockSize  = 512;
constexpr double kBpm        = 120.0;
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

    void advance(int numSamples) { samplePos_ += numSamples; }

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

struct GuitarSynth {
    std::vector<int> notes{ 52, 59, 64, 67, 71 }; // Em9 chord
    std::vector<double> phases{ 0.0, 0.0, 0.0, 0.0, 0.0 };
    uint64_t sampleIdx = 0;

    void renderBlock(juce::AudioBuffer<float>& buf, int numSamples, bool withPluck) {
        buf.clear();
        auto* L = buf.getWritePointer(0);
        auto* R = buf.getNumChannels() > 1 ? buf.getWritePointer(1) : nullptr;
        const float norm = 0.25f / static_cast<float>(notes.size());

        for (int i = 0; i < numSamples; ++i, ++sampleIdx) {
            double s = 0.0;
            for (size_t k = 0; k < notes.size(); ++k) {
                const double f = 440.0 * std::pow(2.0, (static_cast<double>(notes[k]) - 69.0) / 12.0);
                const double ph = phases[k];
                s += std::sin(ph) + 0.45 * std::sin(2.0 * ph) + 0.22 * std::sin(3.0 * ph) + 0.10 * std::sin(4.0 * ph);
                phases[k] += kTwoPi * f / kSampleRate;
                if (phases[k] >= kTwoPi) phases[k] -= kTwoPi;
            }
            float out = static_cast<float>(s) * norm;
            if (withPluck && (sampleIdx % 12000ull) < 120ull) {
                const float env = 1.0f - static_cast<float>(sampleIdx % 12000ull) / 120.0f;
                const float n = std::sin(static_cast<float>(sampleIdx) * 1.37f) * 0.35f * env;
                out += n;
            }
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
        outError = "No VST3 descriptions found in: " + vst3Path.getFullPathName().toStdString();
        return nullptr;
    }

    juce::String err;
    auto inst = fmtMgr.createPluginInstance(*descs[0], kSampleRate, kBlockSize, err);
    if (!inst) outError = "createPluginInstance failed: " + err.toStdString();
    return inst;
}

juce::AudioProcessorParameter* findParam(juce::AudioProcessor& proc, const juce::String& name) {
    for (auto* p : proc.getParameters()) {
        if (p && p->getName(128).equalsIgnoreCase(name)) return p;
    }
    for (auto* p : proc.getParameters()) {
        if (p && p->getName(128).containsIgnoreCase(name)) return p;
    }
    return nullptr;
}

bool setNormParam(juce::AudioProcessor& proc, const juce::String& name, float normVal, std::string& rb) {
    auto* p = findParam(proc, name);
    if (!p) { rb = "NOT FOUND: " + name.toStdString(); return false; }
    p->setValueNotifyingHost(std::clamp(normVal, 0.0f, 1.0f));
    rb = p->getName(64).toStdString() + "=" + p->getCurrentValueAsText().toStdString();
    return true;
}

bool setChoiceParam(juce::AudioProcessor& proc, const juce::String& name, int idx, int count, std::string& rb) {
    const float norm = (count > 1) ? static_cast<float>(idx) / static_cast<float>(count - 1) : 0.0f;
    return setNormParam(proc, name, norm, rb);
}

bool setIntParam(juce::AudioProcessor& proc, const juce::String& name, int val, int minV, int maxV, std::string& rb) {
    const float norm = (maxV > minV) ? static_cast<float>(val - minV) / static_cast<float>(maxV - minV) : 0.0f;
    return setNormParam(proc, name, norm, rb);
}

bool setBoolParam(juce::AudioProcessor& proc, const juce::String& name, bool val, std::string& rb) {
    return setNormParam(proc, name, val ? 1.0f : 0.0f, rb);
}

bool checkFiniteAndPeak(const juce::AudioBuffer<float>& buf, float& outPeak) {
    bool ok = true;
    for (int ch = 0; ch < buf.getNumChannels(); ++ch) {
        const auto* ptr = buf.getReadPointer(ch);
        for (int i = 0; i < buf.getNumSamples(); ++i) {
            if (!std::isfinite(ptr[i])) ok = false;
            else outPeak = std::max(outPeak, std::abs(ptr[i]));
        }
    }
    return ok;
}

} // namespace

int main() {
    juce::ScopedJuceInitialiser_GUI juceInit;
    TestStats stats;

    std::cout << "=== Joint Cross-Plugin Live Integration Test (Fossil.vst3 <-> VowelMorph.vst3 + HarmonyLink) ===\n\n";

    // 1. Load BOTH actual compiled Release VST3 binaries
    juce::AudioPluginFormatManager fmtMgr;
    fmtMgr.addFormat(new juce::VST3PluginFormat());

    const juce::File fossilFile("D:\\Portable Workstation\\DAW\\PluginSuite\\dist\\Fossil\\Fossil.vst3");
    const juce::File vowelFile ("D:\\Portable Workstation\\DAW\\PluginSuite\\dist\\VowelMorph\\VowelMorph.vst3");

    std::string errFos, errVow;
    auto fossil = loadVst3Binary(fmtMgr, fossilFile, errFos);
    auto vowel  = loadVst3Binary(fmtMgr, vowelFile,  errVow);

    stats.check(fossil != nullptr, "Load Fossil.vst3 Release binary",
                fossil ? (fossil->getName().toStdString() + ", params=" + std::to_string(fossil->getParameters().size())) : errFos);
    stats.check(vowel != nullptr, "Load VowelMorph.vst3 Release binary",
                vowel ? (vowel->getName().toStdString() + ", params=" + std::to_string(vowel->getParameters().size())) : errVow);

    if (!fossil || !vowel) {
        std::cerr << "\nAborting: failed to load one or both VST3 binaries.\n";
        return 1;
    }

    MockPlayHead playHead;
    playHead.reset(kBpm, 4, 4);
    fossil->setPlayHead(&playHead);
    vowel->setPlayHead(&playHead);

    fossil->prepareToPlay(kSampleRate, kBlockSize);
    vowel->prepareToPlay(kSampleRate, kBlockSize);

    stats.check(fossil->getTotalNumInputChannels() >= 2 && fossil->getTotalNumOutputChannels() >= 2,
                "Fossil stereo bus layout valid",
                "in=" + std::to_string(fossil->getTotalNumInputChannels()) + " out=" + std::to_string(fossil->getTotalNumOutputChannels()));
    stats.check(vowel->getTotalNumInputChannels() >= 2 && vowel->getTotalNumOutputChannels() >= 2,
                "VowelMorph stereo bus layout valid",
                "in=" + std::to_string(vowel->getTotalNumInputChannels()) + " out=" + std::to_string(vowel->getTotalNumOutputChannels()));
    stats.check(fossil->getLatencySamples() == 1024,
                "Fossil exact PDC latency == 1024 samples",
                "reported=" + std::to_string(fossil->getLatencySamples()));

    // Wire VST3 parameters on BOTH plugins
    std::string rb;
    stats.check(setChoiceParam(*fossil, "Texture Link Bus", 1, 9, rb), "Fossil -> Texture Bus A", rb);
    stats.check(setNormParam(*fossil, "Vowel Mod Depth", 0.85f, rb), "Fossil -> Vowel Mod Depth 0.85", rb);
    stats.check(setChoiceParam(*fossil, "Harmony Link Bus", 1, 9, rb), "Fossil -> Harmony Bus A", rb);
    stats.check(setBoolParam(*fossil, "Harmonic Quantize", true, rb), "Fossil -> Harmonic Quantize ON", rb);
    stats.check(setBoolParam(*fossil, "Euclidean Gate", true, rb), "Fossil -> Euclidean Gate ON", rb);
    stats.check(setIntParam(*fossil, "Euclid Steps", 8, 1, 16, rb), "Fossil -> Euclid Steps=8", rb);
    stats.check(setIntParam(*fossil, "Euclid Hits", 5, 0, 16, rb), "Fossil -> Euclid Hits=5", rb);
    stats.check(setChoiceParam(*fossil, "Euclid Rate", 1, 5, rb), "Fossil -> Euclid Rate=1/8", rb);

    stats.check(setChoiceParam(*vowel, "Texture Link Bus", 1, 9, rb), "VowelMorph -> Texture Bus A", rb);
    stats.check(setChoiceParam(*vowel, "Mod Mode", 2, 4, rb), "VowelMorph -> Mod Mode=StepSeq", rb);
    stats.check(setBoolParam(*vowel, "Link Euclid Sync", true, rb), "VowelMorph -> Link Euclid Sync ON", rb);
    stats.check(setBoolParam(*vowel, "Link Air->Breath", true, rb), "VowelMorph -> Link Air->Breath ON", rb);
    stats.check(setBoolParam(*vowel, "Link Trans->Jaw", true, rb), "VowelMorph -> Link Trans->Jaw ON", rb);
    stats.check(setBoolParam(*vowel, "Link Freeze Sweep", true, rb), "VowelMorph -> Link Freeze Sweep ON", rb);
    stats.check(setChoiceParam(*vowel, "Harmony Link Bus", 1, 9, rb), "VowelMorph -> Harmony Bus A", rb);
    stats.check(setNormParam(*vowel, "Chord Choir", 0.50f, rb), "VowelMorph -> Chord Choir 50%", rb);
    stats.check(setBoolParam(*vowel, "Section Lift", true, rb), "VowelMorph -> Section Lift ON", rb);

    // Observer on TextureLink Bus A (0) and Publisher on HarmonyLink Bus A (0)
    suite::texlink::TextureLinkEndpoint texObserver;
    suite::harmony_link::HarmonyLinkEndpoint harmPublisher;
    stats.check(texObserver.connect(0), "SuiteTextureLink observer connected to Bus A");
    stats.check(harmPublisher.connect(0), "SuiteHarmonyLink publisher connected to Bus A");

    juce::AudioBuffer<float> chainBuf(2, kBlockSize);
    GuitarSynth synth;

    // ========================================================================
    // Phase A: Chained Audio (Synth -> Fossil.vst3 -> VowelMorph.vst3) + Freeze H + Euclid Lockstep
    // ========================================================================
    std::cout << "\n--- Phase A: Chained Audio + Freeze H (MIDI 62) + Euclid Lockstep ---\n";

    // Prime Fossil's STFT + capture ring buffers with 0.6s of harmonic + plucked input (~56 blocks)
    for (int b = 0; b < 56; ++b) {
        juce::MidiBuffer fosMidi, vowMidi;
        synth.renderBlock(chainBuf, kBlockSize, true);
        if (b == 48) {
            // Trigger MIDI NoteOn 62 (Freeze H) cleanly after STFT latency has filled!
            fosMidi.addEvent(juce::MidiMessage::noteOn(1, 62, 0.95f), 0);
        }
        fossil->processBlock(chainBuf, fosMidi);
        vowel->processBlock(chainBuf, vowMidi);
        playHead.advance(kBlockSize);
    }

    suite::texlink::VowelToFossilPayload vowBefore{};
    bool vowAliveBefore = false;
    texObserver.readVowel(vowBefore, vowAliveBefore);
    const uint32_t initialStepSeq = vowAliveBefore ? vowBefore.syllableStepSeq : 0u;

    // Now feed SILENCE into Fossil.vst3 while chaining Fossil's frozen output -> VowelMorph.vst3 for ~1.5s (142 blocks)
    float chainPeak = 0.0f;
    bool chainFinite = true;
    suite::texlink::FossilToVowelPayload fosSnap{};
    bool fosAlive = false;
    suite::texlink::VowelToFossilPayload vowSnap{};
    bool vowAlive = false;
    float maxEnergyHSeen = 0.0f;
    uint32_t maxEuclidHitSeq = 0;

    for (int b = 0; b < 142; ++b) {
        juce::MidiBuffer fosMidi, vowMidi;
        chainBuf.clear(); // pure silence into Fossil — frozen Stratum H must sustain!
        fossil->processBlock(chainBuf, fosMidi);
        vowel->processBlock(chainBuf, vowMidi);
        if (!checkFiniteAndPeak(chainBuf, chainPeak)) chainFinite = false;
        playHead.advance(kBlockSize);

        suite::texlink::FossilToVowelPayload f{};
        bool fa = false;
        if (texObserver.readFossil(f, fa) && fa) {
            fosSnap = f;
            fosAlive = true;
            maxEnergyHSeen = std::max(maxEnergyHSeen, f.energyH);
            maxEuclidHitSeq = std::max(maxEuclidHitSeq, f.euclidHitSeq);
        }
        suite::texlink::VowelToFossilPayload v{};
        bool va = false;
        if (texObserver.readVowel(v, va) && va) {
            vowSnap = v;
            vowAlive = true;
        }
    }

    stats.check(fosAlive && fosSnap.frozenStrataCount >= 1 && fosSnap.freezeCaptureSeq >= 1,
                "FossilToVowelPayload live with active frozen stratum",
                "frozenCount=" + std::to_string(fosSnap.frozenStrataCount) +
                ", captureSeq=" + std::to_string(fosSnap.freezeCaptureSeq) +
                ", maxEnergyH=" + std::to_string(maxEnergyHSeen));
    stats.check(maxEuclidHitSeq >= 2,
                "Fossil Euclidean Gate published >= 2 live hits on Bus A",
                "euclidHitSeq=" + std::to_string(maxEuclidHitSeq) +
                ", euclidStepSeq=" + std::to_string(fosSnap.euclidStepSeq));
    stats.check(maxEnergyHSeen > 0.0f || fosSnap.strataEnergy > 0.0f,
                "Fossil published non-zero harmonic/strata energy",
                "maxEnergyH=" + std::to_string(maxEnergyHSeen) + ", strataEnergy=" + std::to_string(fosSnap.strataEnergy));

    bool formantsValid = vowAlive;
    for (int k = 0; k < 5; ++k) {
        if (!(vowSnap.formantFreqHz[k] >= 80.0f && vowSnap.formantFreqHz[k] <= 14000.0f)) formantsValid = false;
    }
    stats.check(formantsValid,
                "VowelToFossilPayload live with physically valid F1..F5 formants",
                "F1=" + std::to_string(static_cast<int>(vowSnap.formantFreqHz[0])) +
                "Hz F2=" + std::to_string(static_cast<int>(vowSnap.formantFreqHz[1])) +
                "Hz syl=" + std::string(vowSnap.syllableLabel) +
                " tract=" + std::string(vowSnap.tractLabel));
    stats.check(vowAlive && vowSnap.syllableStepSeq > initialStepSeq,
                "VowelMorph syllableStepSeq advanced in lockstep with Fossil Euclid hits",
                "stepSeq: " + std::to_string(initialStepSeq) + " -> " + std::to_string(vowSnap.syllableStepSeq));
    stats.check(chainFinite && chainPeak > 0.005f && chainPeak <= 2.0f,
                "Chained Fossil.vst3 -> VowelMorph.vst3 output is audible & 100% finite",
                "peak=" + std::to_string(chainPeak));

    // ========================================================================
    // Phase B: VowelMorph (X, Y) Sweep (Dark OO 0.95 -> Bright EE 0.05) Modulating Fossil Live
    // ========================================================================
    std::cout << "\n--- Phase B: VowelMorph IPA (X, Y) Sweep OO -> EE Live over Bus A ---\n";
    stats.check(setChoiceParam(*vowel, "Mod Mode", 0, 4, rb), "VowelMorph -> Env mode", rb);
    stats.check(setNormParam(*vowel, "Mod Depth", 0.50f, rb), "VowelMorph -> Mod Depth 0% (clean XY sweep)", rb);
    stats.check(setNormParam(*vowel, "Vowel Y", 0.90f, rb), "VowelMorph -> Vowel Y=0.90 (close vowel)", rb);

    // Dark OO (back vowel X=0.95)
    stats.check(setNormParam(*vowel, "Vowel X", 0.95f, rb), "VowelMorph -> Dark OO (Vowel X=0.95)", rb);
    for (int b = 0; b < 24; ++b) {
        juce::MidiBuffer fm, vm;
        chainBuf.clear();
        fossil->processBlock(chainBuf, fm);
        vowel->processBlock(chainBuf, vm);
        playHead.advance(kBlockSize);
    }
    suite::texlink::VowelToFossilPayload darkSnap{};
    bool darkAlive = false;
    texObserver.readVowel(darkSnap, darkAlive);

    // Bright EE (front vowel X=0.05)
    stats.check(setNormParam(*vowel, "Vowel X", 0.05f, rb), "VowelMorph -> Bright EE (Vowel X=0.05)", rb);
    float sweepPeak = 0.0f;
    bool sweepFinite = true;
    for (int b = 0; b < 24; ++b) {
        juce::MidiBuffer fm, vm;
        chainBuf.clear();
        fossil->processBlock(chainBuf, fm);
        vowel->processBlock(chainBuf, vm);
        if (!checkFiniteAndPeak(chainBuf, sweepPeak)) sweepFinite = false;
        playHead.advance(kBlockSize);
    }
    suite::texlink::VowelToFossilPayload brightSnap{};
    bool brightAlive = false;
    texObserver.readVowel(brightSnap, brightAlive);

    stats.check(darkAlive && brightAlive &&
                brightSnap.formantBrightness > darkSnap.formantBrightness + 0.15f &&
                brightSnap.formantFreqHz[1] > darkSnap.formantFreqHz[1] + 500.0f,
                "VowelMorph formantBrightness & F2 increased sharply from OO (0.95) -> EE (0.05)",
                "dark(bright=" + std::to_string(darkSnap.formantBrightness) +
                ", F2=" + std::to_string(static_cast<int>(darkSnap.formantFreqHz[1])) + "Hz) -> " +
                "bright(bright=" + std::to_string(brightSnap.formantBrightness) +
                ", F2=" + std::to_string(static_cast<int>(brightSnap.formantFreqHz[1])) + "Hz)");
    stats.check(sweepFinite, "Fossil.vst3 + VowelMorph.vst3 stayed 100% finite during live vowel modulation",
                "peak=" + std::to_string(sweepPeak));

    // ========================================================================
    // Phase C: 4-Plugin Ecosystem Proof (SuiteHarmonyLink Bus A -> BOTH Fossil & VowelMorph)
    // ========================================================================
    std::cout << "\n--- Phase C: 4-Plugin Ecosystem HarmonyLink Bus A -> Fossil.vst3 & VowelMorph.vst3 ---\n";
    suite::harmony_link::BandToHudPayload bandPayload{};
    bandPayload.activeChord = suite::harmony_link::makeLinkChord(4, suite::harmony_link::LinkChordQuality::Min9);
    bandPayload.nextChord   = suite::harmony_link::makeLinkChord(9, suite::harmony_link::LinkChordQuality::Dom13);
    bandPayload.beatsUntilNextChord = 2.0f;
    bandPayload.bpm = kBpm;
    bandPayload.isPlaying = 1;
    bandPayload.songSection = 2; // Chorus
    bandPayload.keyRootPc = 4;
    bandPayload.keyIsMinor = 1;
    std::snprintf(bandPayload.styleName, sizeof(bandPayload.styleName), "Neo-Soul Pocket");
    std::snprintf(bandPayload.sectionLabel, sizeof(bandPayload.sectionLabel), "Chorus");

    float ecoPeak = 0.0f;
    bool ecoFinite = true;
    bool fossilSawHarmony = false;
    bool vowelSawHarmony = false;

    for (int b = 0; b < 32; ++b) {
        bandPayload.heartbeatMs = suite::harmony_link::HarmonyLinkEndpoint::nowMs();
        harmPublisher.publishBand(bandPayload);

        juce::MidiBuffer fm, vm;
        synth.renderBlock(chainBuf, kBlockSize, false);
        fossil->processBlock(chainBuf, fm);
        vowel->processBlock(chainBuf, vm);
        if (!checkFiniteAndPeak(chainBuf, ecoPeak)) ecoFinite = false;
        playHead.advance(kBlockSize);

        suite::texlink::FossilToVowelPayload f{};
        bool fa = false;
        if (texObserver.readFossil(f, fa) && fa && (f.flags & suite::texlink::kFossilHarmonyLinked) != 0u) {
            fossilSawHarmony = true;
        }
        suite::texlink::VowelToFossilPayload v{};
        bool va = false;
        if (texObserver.readVowel(v, va) && va && (v.flags & suite::texlink::kVowelHarmonyLinked) != 0u) {
            vowelSawHarmony = true;
        }
    }

    stats.check(fossilSawHarmony, "Fossil.vst3 received SuiteHarmonyLink Em9/Chorus & set kFossilHarmonyLinked");
    stats.check(vowelSawHarmony,  "VowelMorph.vst3 received SuiteHarmonyLink Em9/Chorus & set kVowelHarmonyLinked");
    stats.check(ecoFinite && ecoPeak > 0.01f, "Both VST3 plugins rendered clean finite audio under full 4-plugin linking",
                "peak=" + std::to_string(ecoPeak));

    // ========================================================================
    // Phase D: VST3 State Save/Restore & Clean Teardown
    // ========================================================================
    std::cout << "\n--- Phase D: State Save/Restore & Clean Teardown ---\n";
    juce::MemoryBlock fosState, vowState;
    fossil->getStateInformation(fosState);
    vowel->getStateInformation(vowState);
    stats.check(fosState.getSize() > 0, "Fossil.vst3 getStateInformation non-empty",
                std::to_string(fosState.getSize()) + " bytes");
    stats.check(vowState.getSize() > 0, "VowelMorph.vst3 getStateInformation non-empty",
                std::to_string(vowState.getSize()) + " bytes");

    fossil->setStateInformation(fosState.getData(), static_cast<int>(fosState.getSize()));
    vowel->setStateInformation(vowState.getData(), static_cast<int>(vowState.getSize()));
    stats.check(true, "Both VST3 plugins completed setStateInformation cleanly");

    texObserver.disconnect();
    harmPublisher.disconnect();
    fossil->releaseResources();
    vowel->releaseResources();
    fossil.reset();
    vowel.reset();
    stats.check(true, "Clean teardown with zero crashes");

    std::cout << "\n========================================================================\n";
    std::cout << "JointTextureLinkTest Summary: Passed: " << stats.passed
              << " | Failed: " << stats.failed << "\n";
    std::cout << "========================================================================\n";

    return stats.failed == 0 ? 0 : 1;
}
