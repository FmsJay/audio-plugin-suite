#pragma once

#include "MusicTheory.h"
#include "PolyphonicAnalyzer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace harmonyhud {

class HarmonyTracker {
public:
    static constexpr int kNumQualitiesActive = kNumChordQualities - 1; // 25 active qualities (Major..Add9)
    static constexpr int kNumStates = 1 + 12 * kNumQualitiesActive;    // 301 states (0 = None)
    static constexpr int kDefaultHopSize = 1024;
    static constexpr size_t kMaxHistoryEntries = 32;                   // Matches SuiteHarmonyLink 32-bar chart

    HarmonyTracker() {
        precomputeTemplates();
        prepare(48000.0);
    }

    void prepare(double sampleRate) {
        sampleRate_ = (std::isfinite(sampleRate) && sampleRate >= 8000.0) ? sampleRate : 48000.0;
        mainAnalyzer_.prepare(sampleRate_, a4Hz_);
        sidechainAnalyzer_.prepare(sampleRate_, a4Hz_);
        reset();
    }

    void reset() {
        mainAnalyzer_.reset();
        sidechainAnalyzer_.reset();
        hasSidechainStream_ = false;
        wasSignalActive_ = false;

        resetHmmUniform();
        keyChromaHist_.fill(0.0f);
        keyFramesAccum_ = 0;

        currentChord_ = ChordSymbol{};
        currentKey_ = KeyEstimate{0, false, 0.0f};
        currentRoman_ = "-";
        activeMode_ = ScaleMode::Ionian;
        scalePitchClassMask_ = scaleModePitchClassMask(0, ScaleMode::Ionian);
        avoidPitchClassMask_ = 0;
        chroma_.fill(0.0f);
        noteSalience_.fill(0.0f);
        activeNotes_.fill(false);
        sidechainNotes_.fill(false);
        clashingNotes_.fill(false);
        clashes_.clear();
        clashSeverity_ = 0.0f;
        sidechainActive_ = false;
        clashSourceIsBand_ = false;
        userActivityLevel_ = 0.0f;
        inputLevelDb_ = -100.0f;
        sidechainLevelDb_ = -100.0f;
        history_.clear();
        bandLink_ = BandLinkState{};
        runningBeats_ = 0.0;
    }

    void setReferenceA4(float a4Hz) {
        const float clamped = (std::isfinite(a4Hz) && a4Hz >= 400.0f && a4Hz <= 480.0f) ? a4Hz : 440.0f;
        a4Hz_ = clamped;
        mainAnalyzer_.setReferenceA4(clamped);
        sidechainAnalyzer_.setReferenceA4(clamped);
    }

    void setSensitivity(float sensitivity01) {
        sensitivity01_ = std::isfinite(sensitivity01) ? std::clamp(sensitivity01, 0.0f, 1.0f) : 0.55f;
    }

    void setSmoothing(float smoothing01) {
        smoothing01_ = std::isfinite(smoothing01) ? std::clamp(smoothing01, 0.0f, 1.0f) : 0.50f;
    }

    void setClashSensitivity(float clashSens01) {
        clashSensitivity01_ = std::isfinite(clashSens01) ? std::clamp(clashSens01, 0.0f, 1.0f) : 0.60f;
    }

    void setFreezeHistory(bool freeze) {
        freezeHistory_ = freeze;
    }

    void setMidiNoteState(int midiNote, bool isOn) {
        if (midiNote >= 0 && midiNote < 128) {
            midiHeld_[static_cast<size_t>(midiNote)] = isOn;
        }
    }

    void clearMidiNotes() {
        midiHeld_.fill(false);
    }

    void setTransportInfo(double bpm, double ppqPosition, bool isPlaying) {
        if (std::isfinite(bpm) && bpm >= 20.0 && bpm <= 320.0)
            bpm_ = bpm;
        if (std::isfinite(ppqPosition) && ppqPosition >= 0.0)
            ppqPosition_ = ppqPosition;
        isPlaying_ = isPlaying;
    }

    void updateBandLinkState(const BandLinkState& bandState) {
        bandLink_ = bandState;
        // If audio sidechain is not currently active, immediately refresh Player-vs-Band Clash Radar
        if (!sidechainActive_) {
            evaluateBandLinkClashes();
        }
    }

    void setHistory(const std::vector<ChordHistoryEntry>& entries) {
        history_.clear();
        const size_t n = std::min(entries.size(), kMaxHistoryEntries);
        history_.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            ChordHistoryEntry e = entries[i];
            if (!e.chord.isValid())
                continue;
            if (e.roman.empty() || e.roman == "-")
                e.roman = currentKey_.romanNumeral(e.chord);
            if (e.midiNotes.empty())
                e.midiNotes = e.chord.voicingMidiNotes(3);
            history_.push_back(std::move(e));
        }
    }

    void replaceHistoryEntry(size_t index, const ChordSymbol& chord) {
        if (index >= history_.size() || !chord.isValid())
            return;
        history_[index].chord = chord;
        history_[index].roman = currentKey_.romanNumeral(chord);
        history_[index].midiNotes = chord.voicingMidiNotes(3);
    }

    void appendHistoryEntry(const ChordSymbol& chord) {
        if (!chord.isValid())
            return;
        ChordHistoryEntry entry;
        entry.chord = chord;
        entry.roman = currentKey_.romanNumeral(chord);
        entry.barPosition = history_.empty() ? 1.0 : (history_.back().barPosition + 1.0);
        entry.durationBeats = 4.0;
        entry.midiNotes = chord.voicingMidiNotes(3);
        history_.push_back(std::move(entry));
        if (history_.size() > kMaxHistoryEntries)
            history_.erase(history_.begin());
    }

    void removeHistoryEntry(size_t index) {
        if (index < history_.size()) {
            history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(index));
        }
    }

    void clearHistory() {
        history_.clear();
        runningBeats_ = 0.0;
    }

    int processAudioBlocks(const float* mainMono, const float* sidechainMono, int numSamples) {
        if (numSamples <= 0)
            return 0;

        if (mainMono != nullptr)
            mainAnalyzer_.pushAudio(mainMono, numSamples);

        if (sidechainMono != nullptr) {
            sidechainAnalyzer_.pushAudio(sidechainMono, numSamples);
            hasSidechainStream_ = true;
        }

        int framesProcessed = 0;
        PolyphonicCqtNmf::FrameFeatures mainFeat{};
        while (mainAnalyzer_.analyzeIfReady(mainFeat, kDefaultHopSize)) {
            PolyphonicCqtNmf::FrameFeatures scFeat{};
            const PolyphonicCqtNmf::FrameFeatures* scPtr = nullptr;
            if (sidechainMono != nullptr || hasSidechainStream_) {
                if (sidechainAnalyzer_.analyzeIfReady(scFeat, kDefaultHopSize)) {
                    scPtr = &scFeat;
                }
            }
            processMainFrame(mainFeat, scPtr);
            ++framesProcessed;
        }
        return framesProcessed;
    }

    void processMainFrame(const PolyphonicCqtNmf::FrameFeatures& mainFeat,
                          const PolyphonicCqtNmf::FrameFeatures* sidechainFeat = nullptr) {
        const double dtSec = static_cast<double>(kDefaultHopSize) / sampleRate_;
        const double dtBeats = (dtSec * bpm_) / 60.0;

        // Sensitivity controls noise gate (-42 dB at 0.0 .. -72 dB at 1.0) and note activation threshold
        const float gateDb = -42.0f - 30.0f * sensitivity01_;
        const float noteThresh = 0.36f - 0.18f * sensitivity01_; // ~0.26 at default 0.55

        const bool audioActive = mainFeat.hasSignal && std::isfinite(mainFeat.rmsDb) && (mainFeat.rmsDb >= gateDb);
        inputLevelDb_ = std::isfinite(mainFeat.rmsDb) ? mainFeat.rmsDb : -100.0f;

        // Count held MIDI notes
        int midiCount = 0;
        int lowestMidiHeld = -1;
        for (int m = 0; m < 128; ++m) {
            if (midiHeld_[static_cast<size_t>(m)]) {
                ++midiCount;
                if (lowestMidiHeld < 0)
                    lowestMidiHeld = m;
            }
        }

        const bool hasSignal = audioActive || (midiCount > 0);

        // Smoothed userActivityLevel (0..1)
        float instantAct = 0.0f;
        if (audioActive) {
            instantAct = std::clamp((inputLevelDb_ + 60.0f) / 52.0f, 0.0f, 1.0f);
        }
        if (midiCount > 0) {
            instantAct = std::max(instantAct, std::clamp(0.45f + 0.12f * static_cast<float>(midiCount), 0.0f, 1.0f));
        }
        const float actCoef = (instantAct > userActivityLevel_) ? 0.45f : 0.12f;
        userActivityLevel_ = std::clamp(userActivityLevel_ + actCoef * (instantAct - userActivityLevel_), 0.0f, 1.0f);

        // Build fused chroma, noteSalience, bassChroma, activeNotes, lowestActiveMidi
        std::array<float, 12> effChroma{};
        std::array<float, 12> effBassChroma{};
        std::array<float, 72> effSalience{};
        std::array<bool, 128> effActiveNotes{};
        int effLowestMidi = -1;

        if (audioActive) {
            effChroma = mainFeat.chroma;
            effBassChroma = mainFeat.bassChroma;
            effSalience = mainFeat.noteSalience;
            effLowestMidi = mainFeat.lowestActiveMidi;

            for (int n = 0; n < PolyphonicCqtNmf::kNumNotes; ++n) {
                const float s = effSalience[static_cast<size_t>(n)];
                if (s >= noteThresh) {
                    const float prevS = (n > 0) ? effSalience[static_cast<size_t>(n - 1)] : 0.0f;
                    const float nextS = (n + 1 < PolyphonicCqtNmf::kNumNotes) ? effSalience[static_cast<size_t>(n + 1)] : 0.0f;
                    if (s >= prevS * 0.90f && s >= nextS * 0.90f) {
                        effActiveNotes[static_cast<size_t>(PolyphonicCqtNmf::kMinMidi + n)] = true;
                    }
                }
            }
        }

        if (midiCount > 0) {
            for (int m = 0; m < 128; ++m) {
                if (midiHeld_[static_cast<size_t>(m)]) {
                    effActiveNotes[static_cast<size_t>(m)] = true;
                    effChroma[static_cast<size_t>(m % 12)] = 1.0f;
                    if (m >= PolyphonicCqtNmf::kMinMidi && m <= PolyphonicCqtNmf::kMaxMidi) {
                        effSalience[static_cast<size_t>(m - PolyphonicCqtNmf::kMinMidi)] = 1.0f;
                    }
                }
            }
            if (effLowestMidi < 0 || lowestMidiHeld < effLowestMidi) {
                effLowestMidi = lowestMidiHeld;
            }
            effBassChroma.fill(0.0f);
            if (effLowestMidi >= 0) {
                effBassChroma[static_cast<size_t>(effLowestMidi % 12)] = 1.0f;
            }
            if (!audioActive) {
                inputLevelDb_ = -12.0f;
            }
        }

        // Normalize fused chroma
        float maxC = 0.0f;
        for (float v : effChroma)
            if (std::isfinite(v) && v > maxC) maxC = v;
        if (maxC > 1e-6f) {
            const float inv = 1.0f / maxC;
            for (float& v : effChroma)
                v = std::isfinite(v) ? (v * inv) : 0.0f;
        } else {
            effChroma.fill(0.0f);
        }

        chroma_ = effChroma;
        noteSalience_ = effSalience;
        activeNotes_ = effActiveNotes;

        // 1. Update Krumhansl-Schmuckler Key Estimator (chroma + bass harmonic gravity)
        if (hasSignal) {
            updateKeyEstimate(effChroma, effBassChroma, dtSec);
        }

        // 2. Compute chord emission log-likelihoods & run HMM forward filter
        if (hasSignal && !wasSignalActive_) {
            resetHmmUniform();
        }
        wasSignalActive_ = hasSignal;

        std::array<float, kNumStates> logEmit{};
        computeEmissions(hasSignal, effChroma, effBassChroma, effLowestMidi, logEmit);

        const float stayBonus = 0.25f + smoothing01_ * 2.55f; // [0.25 .. 2.80]
        const float logOffDiag = logSumExp(logAlpha_);

        std::array<float, kNumStates> nextAlpha{};
        for (int s = 0; s < kNumStates; ++s) {
            const float stayTerm = logAlpha_[static_cast<size_t>(s)] + stayBonus;
            const float pred = logAddExp(stayTerm, logOffDiag);
            nextAlpha[static_cast<size_t>(s)] = pred + logEmit[static_cast<size_t>(s)];
        }

        const float normZ = logSumExp(nextAlpha);
        int bestState = 0;
        float bestLogP = -1e30f;
        float secondLogP = -1e30f;

        for (int s = 0; s < kNumStates; ++s) {
            const float lp = nextAlpha[static_cast<size_t>(s)] - normZ;
            logAlpha_[static_cast<size_t>(s)] = std::isfinite(lp) ? lp : -50.0f;
            if (logAlpha_[static_cast<size_t>(s)] > bestLogP) {
                secondLogP = bestLogP;
                bestLogP = logAlpha_[static_cast<size_t>(s)];
                bestState = s;
            } else if (logAlpha_[static_cast<size_t>(s)] > secondLogP) {
                secondLogP = logAlpha_[static_cast<size_t>(s)];
            }
        }

        // Decode winning chord symbol + slash bass detection
        ChordSymbol detected{};
        if (hasSignal && bestState > 0) {
            const int idx = bestState - 1;
            const int root = idx / kNumQualitiesActive;
            const auto qual = static_cast<ChordQuality>(1 + (idx % kNumQualitiesActive));
            detected.root = root;
            detected.quality = qual;
            detected.bass = -1;

            const float postProb = std::exp(std::clamp(bestLogP, -20.0f, 0.0f));
            const float margin = std::clamp((bestLogP - secondLogP) * 0.25f, 0.0f, 1.0f);
            detected.confidence = std::clamp(0.55f * postProb + 0.45f * margin, 0.05f, 1.0f);

            // Slash-chord bass detection
            if (effLowestMidi >= 0) {
                const int bassPC = effLowestMidi % 12;
                const uint16_t chordMask = detected.pitchClassMask();
                if (bassPC != root && (chordMask & (1u << bassPC)) != 0) {
                    bool rootBelowBass = false;
                    for (int m = PolyphonicCqtNmf::kMinMidi; m < effLowestMidi; ++m) {
                        if ((m % 12) == root) {
                            const int nIdx = m - PolyphonicCqtNmf::kMinMidi;
                            if (nIdx >= 0 && nIdx < PolyphonicCqtNmf::kNumNotes &&
                                effSalience[static_cast<size_t>(nIdx)] >= 0.18f) {
                                rootBelowBass = true;
                                break;
                            }
                        }
                    }
                    for (int m = 0; m < effLowestMidi && !rootBelowBass; ++m) {
                        if (midiHeld_[static_cast<size_t>(m)] && (m % 12) == root) {
                            rootBelowBass = true;
                        }
                    }
                    if (!rootBelowBass) {
                        detected.bass = bassPC;
                    }
                }
            }
        }

        currentChord_ = detected;
        currentRoman_ = currentKey_.romanNumeral(currentChord_);

        // 3. Mode & Avoid-Note Mask Generator
        activeMode_ = inferModeForChordAndKey(currentChord_, currentKey_);
        const int refRoot = currentChord_.isValid() ? currentChord_.root : currentKey_.tonic;
        scalePitchClassMask_ = scaleModePitchClassMask(refRoot, activeMode_);
        avoidPitchClassMask_ = computeAvoidPitchClassMask(hasSignal, effActiveNotes, effChroma);

        // 4. Update bar-synced chord progression history
        runningBeats_ += dtBeats;
        if (!freezeHistory_ && currentChord_.isValid()) {
            updateHistory(currentChord_, dtBeats);
        }

        // 5. Inter-Track Sidechain or Band-Link Clash Detection
        const bool audioScReady = (sidechainFeat != nullptr && sidechainFeat->hasSignal &&
                                   std::isfinite(sidechainFeat->rmsDb) && sidechainFeat->rmsDb >= gateDb);
        if (audioScReady) {
            updateSidechainClashes(hasSignal, effChroma, effSalience, effActiveNotes,
                                   sidechainFeat, gateDb, noteThresh);
        } else {
            sidechainActive_ = false;
            sidechainNotes_.fill(false);
            sidechainLevelDb_ = (sidechainFeat != nullptr && std::isfinite(sidechainFeat->rmsDb))
                                    ? sidechainFeat->rmsDb
                                    : -100.0f;
            if (bandLink_.connected) {
                evaluateBandLinkClashes();
            } else {
                clashes_.clear();
                clashingNotes_.fill(false);
                clashSeverity_ = 0.0f;
                clashSourceIsBand_ = false;
            }
        }
    }

    AnalysisSnapshot getSnapshot() const {
        AnalysisSnapshot snap;
        snap.currentChord = currentChord_;
        snap.currentKey = currentKey_;
        snap.currentRoman = currentRoman_;
        snap.activeMode = activeMode_;
        snap.scalePitchClassMask = scalePitchClassMask_;
        snap.avoidPitchClassMask = avoidPitchClassMask_;
        snap.chroma = chroma_;
        snap.noteSalience = noteSalience_;
        snap.activeNotes = activeNotes_;
        snap.sidechainNotes = sidechainNotes_;
        snap.clashingNotes = clashingNotes_;
        snap.clashes = clashes_;
        snap.clashSeverity = clashSeverity_;
        snap.sidechainActive = sidechainActive_;
        snap.clashSourceIsBand = clashSourceIsBand_;
        snap.userActivityLevel = userActivityLevel_;
        snap.inputLevelDb = inputLevelDb_;
        snap.sidechainLevelDb = sidechainLevelDb_;
        snap.substitutions = generateSubstitutions(currentChord_, currentKey_);
        snap.history = history_;
        snap.bandLink = bandLink_;
        return snap;
    }

private:
    struct PrecomputedTemplate {
        std::array<float, 12> weights{};
        float norm = 1.0f;
        uint16_t requiredMask = 0;
        uint16_t fullMask = 0;
        float complexityPenalty = 0.0f;
    };

    void precomputeTemplates() {
        const auto& specs = getChordTemplateSpecs();
        for (int q = 1; q < kNumChordQualities; ++q) {
            const auto& sp = specs[static_cast<size_t>(q)];
            PrecomputedTemplate pt;
            pt.weights = sp.weights;
            pt.complexityPenalty = sp.complexityPenalty;

            float sumSq = 0.0f;
            uint16_t fullM = 0;
            for (int p = 0; p < 12; ++p) {
                sumSq += sp.weights[static_cast<size_t>(p)] * sp.weights[static_cast<size_t>(p)];
                if (sp.weights[static_cast<size_t>(p)] > 0.0f)
                    fullM |= static_cast<uint16_t>(1u << p);
            }
            pt.norm = std::max(std::sqrt(sumSq), 1e-6f);
            pt.fullMask = fullM;

            // Core tones are always required. For 6 / m6 / 9 / maj9 / m9 chords,
            // the characteristic extension (6th or 9th) is ALSO required so triads/7ths
            // are never misclassified as extended chords. (11th, 13th, #9, b9, add9 are
            // already in coreMask in MusicTheory.h!)
            uint16_t req = sp.coreMask;
            const auto qual = static_cast<ChordQuality>(q);
            if (qual == ChordQuality::Six || qual == ChordQuality::Min6) {
                req |= static_cast<uint16_t>(1u << 9);
            } else if (qual == ChordQuality::Dom9 || qual == ChordQuality::Maj9 || qual == ChordQuality::Min9) {
                req |= static_cast<uint16_t>(1u << 2);
            }
            pt.requiredMask = req;

            templates_[static_cast<size_t>(q)] = pt;
        }
    }

    void resetHmmUniform() {
        const float u = -std::log(static_cast<float>(kNumStates));
        logAlpha_.fill(u);
    }

    static float logAddExp(float a, float b) noexcept {
        if (a < b) std::swap(a, b);
        if (b <= -1e20f) return a;
        return a + std::log1p(std::exp(b - a));
    }

    static float logSumExp(const std::array<float, kNumStates>& arr) noexcept {
        float maxVal = arr[0];
        for (int i = 1; i < kNumStates; ++i)
            if (arr[static_cast<size_t>(i)] > maxVal) maxVal = arr[static_cast<size_t>(i)];
        double sum = 0.0;
        for (int i = 0; i < kNumStates; ++i)
            sum += std::exp(static_cast<double>(arr[static_cast<size_t>(i)] - maxVal));
        return maxVal + static_cast<float>(std::log(std::max(sum, 1e-30)));
    }

    static uint16_t scaleMaskForKey(const KeyEstimate& key) noexcept {
        static constexpr uint16_t kMajorRelMask =
            (1u << 0) | (1u << 2) | (1u << 4) | (1u << 5) | (1u << 7) | (1u << 9) | (1u << 11);
        static constexpr uint16_t kMinorRelMask =
            (1u << 0) | (1u << 2) | (1u << 3) | (1u << 5) | (1u << 7) | (1u << 8) | (1u << 10) | (1u << 11);
        const uint16_t base = key.isMinor ? kMinorRelMask : kMajorRelMask;
        uint16_t rot = 0;
        for (int i = 0; i < 12; ++i) {
            if (base & (1u << i))
                rot |= static_cast<uint16_t>(1u << ((i + key.tonic) % 12));
        }
        return rot;
    }

    static uint16_t computeAvoidPitchClassMask(bool hasSignal,
                                               const std::array<bool, 128>& activeNotes,
                                               const std::array<float, 12>& chroma) noexcept {
        if (!hasSignal)
            return 0u;

        uint16_t userMask = 0u;
        for (int m = 0; m < 128; ++m) {
            if (activeNotes[static_cast<size_t>(m)])
                userMask |= static_cast<uint16_t>(1u << (m % 12));
        }
        for (int pc = 0; pc < 12; ++pc) {
            if (chroma[static_cast<size_t>(pc)] >= 0.45f)
                userMask |= static_cast<uint16_t>(1u << pc);
        }

        uint16_t avoid = 0u;
        for (int pc = 0; pc < 12; ++pc) {
            if ((userMask & (1u << pc)) != 0) {
                avoid |= static_cast<uint16_t>(1u << ((pc + 1) % 12));
                avoid |= static_cast<uint16_t>(1u << ((pc + 11) % 12));
            }
        }
        // Never mark an active user note itself as an avoid note
        avoid &= static_cast<uint16_t>(~userMask);
        return avoid;
    }

    void computeEmissions(bool hasSignal,
                          const std::array<float, 12>& chroma,
                          const std::array<float, 12>& bassChroma,
                          int lowestActiveMidi,
                          std::array<float, kNumStates>& logEmit) const noexcept {
        constexpr float kEmitScale = 6.5f;

        if (!hasSignal) {
            logEmit.fill(-12.0f);
            logEmit[0] = 6.0f;
            return;
        }

        float cNormSq = 0.0f;
        int activePitchClasses = 0;
        for (int p = 0; p < 12; ++p) {
            cNormSq += chroma[static_cast<size_t>(p)] * chroma[static_cast<size_t>(p)];
            if (chroma[static_cast<size_t>(p)] >= 0.25f)
                ++activePitchClasses;
        }
        const float cNorm = std::max(std::sqrt(cNormSq), 1e-6f);

        // Single note -> None; 2+ pitch classes -> chord candidates compete
        logEmit[0] = (activePitchClasses < 2) ? (0.85f * kEmitScale) : (0.28f * kEmitScale);

        const int bassPC = (lowestActiveMidi >= 0) ? (lowestActiveMidi % 12) : -1;
        const bool useKeyBias = (currentKey_.confidence >= 0.30f);
        const uint16_t keyScaleMask = useKeyBias ? scaleMaskForKey(currentKey_) : 0x0FFFu;

        for (int root = 0; root < 12; ++root) {
            for (int q = 1; q < kNumChordQualities; ++q) {
                const auto& pt = templates_[static_cast<size_t>(q)];

                float dot = 0.0f;
                float missingReq = 0.0f;
                float unexplained = 0.0f;

                for (int rel = 0; rel < 12; ++rel) {
                    const int pc = (root + rel) % 12;
                    const float cVal = chroma[static_cast<size_t>(pc)];
                    const float wVal = pt.weights[static_cast<size_t>(rel)];

                    if (wVal > 0.0f) {
                        dot += cVal * wVal;
                    } else if (cVal > 0.16f) {
                        unexplained += (cVal - 0.16f);
                    }

                    if ((pt.requiredMask & (1u << rel)) != 0) {
                        if (cVal < 0.28f) {
                            missingReq += (0.28f - cVal) / 0.28f;
                        }
                    }
                }

                const float cosine = dot / (pt.norm * cNorm);

                float bassBonus = 0.0f;
                if (bassPC >= 0) {
                    const int bassRel = (bassPC - root + 12) % 12;
                    if (bassPC == root) {
                        bassBonus = 0.22f;
                    } else if (pt.weights[static_cast<size_t>(bassRel)] > 0.0f) {
                        bassBonus = 0.05f;
                    } else {
                        bassBonus = -0.10f;
                    }
                }
                bassBonus += 0.05f * bassChroma[static_cast<size_t>(root)];

                float keyBias = 0.0f;
                if (useKeyBias) {
                    uint16_t absMask = 0;
                    for (int rel = 0; rel < 12; ++rel) {
                        if (pt.fullMask & (1u << rel))
                            absMask |= static_cast<uint16_t>(1u << ((root + rel) % 12));
                    }
                    if ((absMask & ~keyScaleMask) == 0) {
                        keyBias = 0.03f;
                    }
                }

                const float rawScore = cosine
                                     - 0.58f * missingReq
                                     - 0.62f * unexplained
                                     + bassBonus
                                     + keyBias
                                     - pt.complexityPenalty;

                const int stateIdx = 1 + root * kNumQualitiesActive + (q - 1);
                logEmit[static_cast<size_t>(stateIdx)] = rawScore * kEmitScale;
            }
        }
    }

    void updateKeyEstimate(const std::array<float, 12>& chroma,
                           const std::array<float, 12>& bassChroma,
                           double dtSec) noexcept {
        static constexpr std::array<float, 12> kMajorProfile = {
            6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f
        };
        static constexpr std::array<float, 12> kMinorProfile = {
            6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f
        };

        ++keyFramesAccum_;
        // Equal-weight accumulation during the initial phrase, transitioning smoothly to a ~12s leaky integrator
        const float decay = static_cast<float>(std::exp(-dtSec / 12.0));
        const float minAlpha = 1.0f - decay;
        const float alpha = std::max(minAlpha, 1.0f / static_cast<float>(keyFramesAccum_));

        for (int p = 0; p < 12; ++p) {
            const float samplePc = chroma[static_cast<size_t>(p)] + 0.45f * bassChroma[static_cast<size_t>(p)];
            keyChromaHist_[static_cast<size_t>(p)] =
                keyChromaHist_[static_cast<size_t>(p)] * (1.0f - alpha) + samplePc * alpha;
        }

        float meanX = 0.0f;
        for (float v : keyChromaHist_) meanX += v;
        meanX /= 12.0f;

        float varX = 0.0f;
        for (float v : keyChromaHist_) {
            const float d = v - meanX;
            varX += d * d;
        }
        if (varX < 1e-8f) {
            currentKey_.confidence = 0.0f;
            return;
        }
        const float stdX = std::sqrt(varX);

        auto pearson = [&](const std::array<float, 12>& prof, int tonic) -> float {
            float meanY = 0.0f;
            for (float v : prof) meanY += v;
            meanY /= 12.0f;

            float cov = 0.0f;
            float varY = 0.0f;
            for (int i = 0; i < 12; ++i) {
                const float dx = keyChromaHist_[static_cast<size_t>((tonic + i) % 12)] - meanX;
                const float dy = prof[static_cast<size_t>(i)] - meanY;
                cov += dx * dy;
                varY += dy * dy;
            }
            return cov / (stdX * std::sqrt(varY) + 1e-8f);
        };

        int bestTonic = 0;
        bool bestMinor = false;
        float bestR = -2.0f;
        float secondR = -2.0f;

        for (int t = 0; t < 12; ++t) {
            for (int m = 0; m < 2; ++m) {
                const float r = pearson(m == 0 ? kMajorProfile : kMinorProfile, t);
                if (r > bestR) {
                    secondR = bestR;
                    bestR = r;
                    bestTonic = t;
                    bestMinor = (m == 1);
                } else if (r > secondR) {
                    secondR = r;
                }
            }
        }

        currentKey_.tonic = bestTonic;
        currentKey_.isMinor = bestMinor;
        const float posR = std::clamp(bestR, 0.0f, 1.0f);
        const float marginBoost = std::clamp((bestR - secondR) * 2.5f, 0.0f, 1.0f);
        currentKey_.confidence = std::clamp(0.75f * posR + 0.25f * marginBoost, 0.0f, 1.0f);
    }

    void updateHistory(const ChordSymbol& chord, double dtBeats) {
        if (!history_.empty() && history_.back().chord.sameHarmony(chord)) {
            history_.back().durationBeats += dtBeats;
            history_.back().roman = currentKey_.romanNumeral(chord);
            if (chord.confidence > history_.back().chord.confidence)
                history_.back().chord.confidence = chord.confidence;
            return;
        }

        ChordHistoryEntry entry;
        entry.chord = chord;
        entry.roman = currentKey_.romanNumeral(chord);
        entry.barPosition = isPlaying_ ? (1.0 + ppqPosition_ / 4.0) : (1.0 + runningBeats_ / 4.0);
        entry.durationBeats = std::max(0.25, dtBeats);
        entry.midiNotes = chord.voicingMidiNotes(3);

        history_.push_back(std::move(entry));
        if (history_.size() > kMaxHistoryEntries) {
            history_.erase(history_.begin());
        }
    }

    void updateSidechainClashes(bool mainHasSignal,
                                const std::array<float, 12>& mainChroma,
                                const std::array<float, 72>& mainSalience,
                                const std::array<bool, 128>& mainActiveNotes,
                                const PolyphonicCqtNmf::FrameFeatures* sidechainFeat,
                                float gateDb,
                                float noteThresh) {
        clashes_.clear();
        sidechainNotes_.fill(false);
        clashingNotes_.fill(false);
        clashSeverity_ = 0.0f;
        clashSourceIsBand_ = false;

        if (sidechainFeat == nullptr || !sidechainFeat->hasSignal ||
            !std::isfinite(sidechainFeat->rmsDb) || sidechainFeat->rmsDb < gateDb) {
            sidechainActive_ = false;
            return;
        }

        sidechainActive_ = true;
        sidechainLevelDb_ = sidechainFeat->rmsDb;

        const float scThresh = std::clamp(noteThresh * (1.25f - 0.55f * clashSensitivity01_), 0.12f, 0.45f);
        for (int n = 0; n < PolyphonicCqtNmf::kNumNotes; ++n) {
            const float s = sidechainFeat->noteSalience[static_cast<size_t>(n)];
            if (s >= scThresh) {
                const float prevS = (n > 0) ? sidechainFeat->noteSalience[static_cast<size_t>(n - 1)] : 0.0f;
                const float nextS = (n + 1 < PolyphonicCqtNmf::kNumNotes) ? sidechainFeat->noteSalience[static_cast<size_t>(n + 1)] : 0.0f;
                if (s >= prevS * 0.90f && s >= nextS * 0.90f) {
                    sidechainNotes_[static_cast<size_t>(PolyphonicCqtNmf::kMinMidi + n)] = true;
                }
            }
        }

        if (!mainHasSignal)
            return;

        const uint16_t mainChordMask = currentChord_.isValid() ? currentChord_.pitchClassMask() : 0u;
        const float sensScale = 0.65f + 0.65f * clashSensitivity01_;

        for (int sMidi = PolyphonicCqtNmf::kMinMidi; sMidi <= PolyphonicCqtNmf::kMaxMidi; ++sMidi) {
            if (!sidechainNotes_[static_cast<size_t>(sMidi)])
                continue;

            const int scPC = sMidi % 12;
            const float scSal = sidechainFeat->noteSalience[static_cast<size_t>(sMidi - PolyphonicCqtNmf::kMinMidi)];

            // Consonant doubling/chord-tone check: skip if sidechain pitch class is already
            // part of the main chord or strongly sounding in main.
            if ((mainChordMask & (1u << scPC)) != 0 || mainChroma[static_cast<size_t>(scPC)] >= 0.45f) {
                continue;
            }

            for (int mMidi = PolyphonicCqtNmf::kMinMidi; mMidi <= PolyphonicCqtNmf::kMaxMidi; ++mMidi) {
                if (!mainActiveNotes[static_cast<size_t>(mMidi)])
                    continue;

                const int diff = std::abs(mMidi - sMidi);
                const int pcDiff = diff % 12;

                if (pcDiff == 1 || pcDiff == 11) {
                    const float mSal = mainSalience[static_cast<size_t>(mMidi - PolyphonicCqtNmf::kMinMidi)];
                    float baseSev = 0.72f;
                    if (diff == 1) baseSev = 0.98f;
                    else if (diff == 11 || diff == 13) baseSev = 0.86f;
                    const float sev = std::clamp(baseSev * sensScale * std::sqrt(std::max(0.15f, mSal * scSal)), 0.1f, 1.0f);

                    ClashInfo ci;
                    ci.mainMidi = mMidi;
                    ci.sidechainMidi = sMidi;
                    ci.intervalSemitones = 1;
                    ci.severity = sev;
                    ci.sourceLabel = "Sidechain";
                    clashes_.push_back(ci);
                    clashingNotes_[static_cast<size_t>(mMidi)] = true;
                    clashingNotes_[static_cast<size_t>(sMidi)] = true;
                    if (sev > clashSeverity_) clashSeverity_ = sev;
                } else if (pcDiff == 6) {
                    const float mSal = mainSalience[static_cast<size_t>(mMidi - PolyphonicCqtNmf::kMinMidi)];
                    const float baseSev = (diff == 6) ? 0.68f : 0.54f;
                    const float sev = std::clamp(baseSev * sensScale * std::sqrt(std::max(0.15f, mSal * scSal)), 0.1f, 1.0f);

                    ClashInfo ci;
                    ci.mainMidi = mMidi;
                    ci.sidechainMidi = sMidi;
                    ci.intervalSemitones = 6;
                    ci.severity = sev;
                    ci.sourceLabel = "Sidechain";
                    clashes_.push_back(ci);
                    clashingNotes_[static_cast<size_t>(mMidi)] = true;
                    clashingNotes_[static_cast<size_t>(sMidi)] = true;
                    if (sev > clashSeverity_) clashSeverity_ = sev;
                }
            }
        }

        std::sort(clashes_.begin(), clashes_.end(),
                  [](const ClashInfo& a, const ClashInfo& b) { return a.severity > b.severity; });
        if (clashes_.size() > 8)
            clashes_.resize(8);
    }

    // Zero-sidechain Player-vs-Band Clash Radar against SessionMate slotNotes[1..5]
    void evaluateBandLinkClashes() {
        clashes_.clear();
        clashingNotes_.fill(false);
        clashSeverity_ = 0.0f;
        clashSourceIsBand_ = false;

        if (!bandLink_.connected)
            return;

        static constexpr const char* kSlotLabels[6] = {
            "Band Drums", "Band Bass", "Band Keys", "Band Guitar", "Band Perc", "Band Horns"
        };

        const uint16_t mainChordMask = currentChord_.isValid() ? currentChord_.pitchClassMask() : 0u;
        const float sensScale = 0.65f + 0.65f * clashSensitivity01_;

        // Check pitched band slots 1..5 (Bass, Keys, Guitar, Perc, Horns — skip slot 0 Drums)
        for (int slot = 1; slot < 6; ++slot) {
            for (int bMidi = 24; bMidi <= 108; ++bMidi) {
                if (!bandLink_.slotNotes[static_cast<size_t>(slot)][static_cast<size_t>(bMidi)])
                    continue;

                const int bPC = bMidi % 12;
                if ((mainChordMask & (1u << bPC)) != 0 || chroma_[static_cast<size_t>(bPC)] >= 0.45f) {
                    continue;
                }

                for (int mMidi = 0; mMidi < 128; ++mMidi) {
                    if (!activeNotes_[static_cast<size_t>(mMidi)])
                        continue;

                    const int diff = std::abs(mMidi - bMidi);
                    const int pcDiff = diff % 12;
                    if (pcDiff == 1 || pcDiff == 11) {
                        float baseSev = 0.70f;
                        if (diff == 1) baseSev = 0.96f;
                        else if (diff == 11 || diff == 13) baseSev = 0.84f;
                        const float sev = std::clamp(baseSev * sensScale, 0.1f, 1.0f);

                        ClashInfo ci;
                        ci.mainMidi = mMidi;
                        ci.sidechainMidi = bMidi;
                        ci.intervalSemitones = 1;
                        ci.severity = sev;
                        ci.sourceLabel = kSlotLabels[slot];
                        clashes_.push_back(ci);
                        clashingNotes_[static_cast<size_t>(mMidi)] = true;
                        clashingNotes_[static_cast<size_t>(bMidi)] = true;
                        if (sev > clashSeverity_) clashSeverity_ = sev;
                        clashSourceIsBand_ = true;
                    } else if (pcDiff == 6) {
                        const float baseSev = (diff == 6) ? 0.66f : 0.52f;
                        const float sev = std::clamp(baseSev * sensScale, 0.1f, 1.0f);

                        ClashInfo ci;
                        ci.mainMidi = mMidi;
                        ci.sidechainMidi = bMidi;
                        ci.intervalSemitones = 6;
                        ci.severity = sev;
                        ci.sourceLabel = kSlotLabels[slot];
                        clashes_.push_back(ci);
                        clashingNotes_[static_cast<size_t>(mMidi)] = true;
                        clashingNotes_[static_cast<size_t>(bMidi)] = true;
                        if (sev > clashSeverity_) clashSeverity_ = sev;
                        clashSourceIsBand_ = true;
                    }
                }
            }
        }

        std::sort(clashes_.begin(), clashes_.end(),
                  [](const ClashInfo& a, const ClashInfo& b) { return a.severity > b.severity; });
        if (clashes_.size() > 8)
            clashes_.resize(8);
    }

    double sampleRate_ = 48000.0;
    float a4Hz_ = 440.0f;
    float sensitivity01_ = 0.55f;
    float smoothing01_ = 0.50f;
    float clashSensitivity01_ = 0.60f;
    bool freezeHistory_ = false;

    double bpm_ = 120.0;
    double ppqPosition_ = 0.0;
    bool isPlaying_ = false;
    double runningBeats_ = 0.0;

    PolyphonicCqtNmf mainAnalyzer_;
    PolyphonicCqtNmf sidechainAnalyzer_;
    bool hasSidechainStream_ = false;
    bool wasSignalActive_ = false;

    std::array<PrecomputedTemplate, kNumChordQualities> templates_{};
    std::array<float, kNumStates> logAlpha_{};

    std::array<float, 12> keyChromaHist_{};
    int keyFramesAccum_ = 0;

    std::array<bool, 128> midiHeld_{};

    ChordSymbol currentChord_{};
    KeyEstimate currentKey_{0, false, 0.0f};
    std::string currentRoman_ = "-";
    ScaleMode activeMode_ = ScaleMode::Ionian;
    uint16_t scalePitchClassMask_ = 0;
    uint16_t avoidPitchClassMask_ = 0;
    std::array<float, 12> chroma_{};
    std::array<float, 72> noteSalience_{};
    std::array<bool, 128> activeNotes_{};
    std::array<bool, 128> sidechainNotes_{};
    std::array<bool, 128> clashingNotes_{};
    std::vector<ClashInfo> clashes_;
    float clashSeverity_ = 0.0f;
    bool sidechainActive_ = false;
    bool clashSourceIsBand_ = false;
    float userActivityLevel_ = 0.0f;
    float inputLevelDb_ = -100.0f;
    float sidechainLevelDb_ = -100.0f;
    std::vector<ChordHistoryEntry> history_;
    BandLinkState bandLink_;
};

} // namespace harmonyhud
