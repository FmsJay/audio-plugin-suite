#pragma once

#include "../SessionMateTypes.h"
#include "ChordParser.h"
#include "StyleLibrary.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace sessionmate {

struct ComposerExtendedResult {
    ChordSymbol activeChord {};
    ChordSymbol nextChord {};
    float beatsUntilNextChord = 4.0f;
    int activeChordBarIndex = 0;
    bool fillActive = false;
    bool reharmActive = false;
    ChordSymbol reharmChord {};
    bool clashAvoided = false;
};

class GrooveComposer {
public:
    GrooveComposer() {
        // Default musical 4-bar jazz-funk progression: | Em9 | A13 | Dmaj7 | B7#9 |
        chords_[0] = parseChord("Em9");
        chords_[1] = parseChord("A13");
        chords_[2] = parseChord("Dmaj7");
        chords_[3] = parseChord("B7#9");
        numChordBars_ = 4;
    }

    void prepare(double sampleRate) noexcept {
        sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 48000.0;
        reset();
    }

    void reset() noexcept {
        activeNotes_.fill({});
        activeNoteMaskLo_.fill(0);
        activeNoteMaskHi_.fill(0);
        prevKeysVoicing_ = {};
        wasPlaying_ = false;
        lastBlockEndPpq_ = -1.0;
        lastBarIndex_ = -1;
        lastSection_ = SongSection::Chorus;
        manualFillRequested_ = false;
        manualFillBar_ = -1;
        keysSustainDown_ = false;
        clearReharm();
        for (int s = 0; s < kNumSlots; ++s) {
            wasPatternLocked_[static_cast<size_t>(s)] = false;
            lockAnchorBar_[static_cast<size_t>(s)] = 0;
            lockBarsCount_[static_cast<size_t>(s)] = 2;
        }
        clearLockBuffers();
    }

    void setChordProgression(const std::array<ChordSymbol, kMaxChordBars>& chords, int numBars) noexcept {
        const int clamped = std::clamp(numBars, 1, kMaxChordBars);
        for (int i = 0; i < clamped; ++i) {
            chords_[static_cast<size_t>(i)] = chords[static_cast<size_t>(i)];
        }
        numChordBars_ = clamped;
    }

    void triggerManualFill() noexcept {
        manualFillRequested_ = true;
    }

    void queueReharmChord(const ChordSymbol& chord, int timingMode /*0=Immediate, 1=NextBeat, 2=NextBar*/, int durationBars = 1) noexcept {
        if (!chord.valid || chord.rootPitchClass < 0) {
            clearReharm();
            return;
        }
        pendingReharmChord_ = chord;
        pendingReharmTiming_ = std::clamp(timingMode, 0, 2);
        pendingReharmDurationBars_ = std::max(1, durationBars);
        reharmPending_ = true;
        reharmStartPpq_ = -1.0;
        reharmEndPpq_ = -1.0;
        if (pendingReharmTiming_ == 0) {
            reharmActive_ = true;
            activeReharmChord_ = chord;
        }
    }

    void clearReharm() noexcept {
        reharmPending_ = false;
        reharmActive_ = false;
        pendingReharmChord_ = {};
        activeReharmChord_ = {};
        pendingReharmTiming_ = 0;
        pendingReharmDurationBars_ = 1;
        reharmStartPpq_ = -1.0;
        reharmEndPpq_ = -1.0;
    }

    void getSlotActiveNoteMasks(std::array<uint64_t, kNumSlots>& outLo,
                                std::array<uint64_t, kNumSlots>& outHi) const noexcept {
        outLo = activeNoteMaskLo_;
        outHi = activeNoteMaskHi_;
    }

    const StyleLibrary& library() const noexcept {
        return library_;
    }

    void processBlock(int numSamples,
                      const TransportState& transport,
                      const PerformanceControls& perf,
                      const std::array<SlotConfig, kNumSlots>& slots,
                      const ListenerTelemetry& listener,
                      BlockEventList& outEvents,
                      ChordSymbol& outActiveChord,
                      int& outActiveChordBar,
                      bool& outFillActive) noexcept {
        HarmonyLinkHints emptyHints {};
        ComposerExtendedResult ext {};
        processBlockEx(numSamples, transport, perf, slots, listener, emptyHints, outEvents, ext);
        outActiveChord = ext.activeChord;
        outActiveChordBar = ext.activeChordBarIndex;
        outFillActive = ext.fillActive;
    }

    SM_NOINLINE void processBlockEx(int numSamples,
                                    const TransportState& transport,
                                    const PerformanceControls& perf,
                                    const std::array<SlotConfig, kNumSlots>& slots,
                                    const ListenerTelemetry& listener,
                                    const HarmonyLinkHints& linkHints,
                                    BlockEventList& outEvents,
                                    ComposerExtendedResult& outResult) noexcept {
        outEvents.clear();
        outResult = {};
        if (numSamples <= 0) {
            outResult.fillActive = false;
            return;
        }

        const TimeSignature ts = (perf.timeSig.numerator > 0 && perf.timeSig.denominator > 0)
                                     ? perf.timeSig
                                     : transport.timeSig;
        const int stepsPerBar = ts.stepsPerBar();
        const double barPpqLen = ts.beatsPerBarQuarter();
        const double stepPpqLen = barPpqLen / static_cast<double>(stepsPerBar);

        // Handle stop: flush all active notes cleanly
        if (!transport.isPlaying) {
            if (wasPlaying_) {
                flushAllNotes(outEvents, 0, transport.ppqPosition);
                wasPlaying_ = false;
            }
            lastBlockEndPpq_ = -1.0;
            outResult.fillActive = false;
            const int safeBar = std::max(0, transport.currentBar);
            const int chordBar = (numChordBars_ > 0) ? (safeBar % numChordBars_) : 0;
            const int nextChordBar = (numChordBars_ > 0) ? ((chordBar + 1) % numChordBars_) : 0;
            outResult.activeChordBarIndex = chordBar;
            outResult.activeChord = resolveActiveChordEx(chordBar, perf, listener, linkHints);
            outResult.nextChord = reharmPending_ ? pendingReharmChord_ : chords_[static_cast<size_t>(nextChordBar)];
            outResult.beatsUntilNextChord = static_cast<float>(barPpqLen);
            outResult.reharmActive = reharmActive_;
            outResult.reharmChord = activeReharmChord_;
            outResult.clashAvoided = false;
            return;
        }

        double effectiveBpm = std::clamp(transport.bpm, 30.0, 300.0);
        if (perf.halfTime && !perf.doubleTime) effectiveBpm *= 0.5;
        else if (perf.doubleTime && !perf.halfTime) effectiveBpm *= 2.0;
        effectiveBpm = std::clamp(effectiveBpm, 20.0, 360.0);

        const double beatsPerSecond = effectiveBpm / 60.0;
        const double beatsPerSample = beatsPerSecond / sampleRate_;
        const double blockStartPpq = std::max(0.0, transport.ppqPosition);
        const double blockEndPpq = blockStartPpq + static_cast<double>(numSamples) * beatsPerSample;

        // Detect seek / loop jump
        if (wasPlaying_ && lastBlockEndPpq_ >= 0.0 && std::fabs(blockStartPpq - lastBlockEndPpq_) > 0.25) {
            flushAllNotes(outEvents, 0, blockStartPpq);
            lastBarIndex_ = -1;
            manualFillBar_ = -1;
            if (reharmActive_ && (blockStartPpq < reharmStartPpq_ - 0.25 || (reharmEndPpq_ >= 0.0 && blockStartPpq >= reharmEndPpq_))) {
                clearReharm();
            }
        }
        wasPlaying_ = true;
        lastBlockEndPpq_ = blockEndPpq;

        // Anchor any newly queued reharm chord to transport PPQ
        anchorPendingReharm(blockStartPpq, barPpqLen);
        updateReharmAtPpq(blockStartPpq);

        // Determine active bar & chord at block start
        const int currentBar = static_cast<int>(std::floor(blockStartPpq / barPpqLen + 1.0e-9));
        const int startChordBar = (numChordBars_ > 0) ? ((currentBar % numChordBars_ + numChordBars_) % numChordBars_) : 0;
        const int startNextChordBar = (numChordBars_ > 0) ? ((startChordBar + 1) % numChordBars_) : 0;
        outResult.activeChordBarIndex = startChordBar;
        outResult.activeChord = resolveActiveChordEx(startChordBar, perf, listener, linkHints);
        outResult.nextChord = reharmPending_ ? pendingReharmChord_ : chords_[static_cast<size_t>(startNextChordBar)];
        outResult.beatsUntilNextChord = static_cast<float>(std::max(0.0, static_cast<double>(currentBar + 1) * barPpqLen - blockStartPpq));
        outResult.reharmActive = reharmActive_;
        outResult.reharmChord = activeReharmChord_;

        // Process due NoteOffs in [blockStartPpq, blockEndPpq)
        processPendingNoteOffs(blockStartPpq, blockEndPpq, beatsPerSample, numSamples, outEvents);

        // Morph Style A <-> Style B (real-time safe, zero heap allocs)
        const StyleGenome genome = library_.morph(perf.styleAIndex, perf.styleBIndex, perf.styleMorph);
        const int nativeSteps = genome.defaultTimeSig.stepsPerBar();

        // Effective song section
        const SongSection section = resolveSection(perf, listener);
        const float secEnergyMul = sectionEnergyMul(section);
        const float secDensityMul = sectionDensityMul(section);

        // Zero-Sidechain Leave-Space Integration: combine local audio listener + HarmonyHUD link
        const float effectiveUserActivity = std::max(
            listener.userActivity,
            (linkHints.peerAlive && linkHints.userHasSignal) ? linkHints.userActivity : 0.0f);
        const float userDuck = std::clamp(effectiveUserActivity * perf.leaveSpace, 0.0f, 0.85f);

        // Auto-Clash Avoidance mask & active scale mode
        const uint16_t avoidMask = (perf.clashAvoidance && linkHints.peerAlive) ? linkHints.avoidPitchClassMask : 0;
        const ScaleMode activeMode = linkHints.peerAlive
                                         ? linkHints.activeMode
                                         : (perf.keyMinor ? ScaleMode::Aeolian : ScaleMode::Ionian);

        // Check if any slot is soloed
        bool anySolo = false;
        for (int s = 0; s < kNumSlots; ++s) {
            if (slots[static_cast<size_t>(s)].solo) {
                anySolo = true;
                break;
            }
        }
        auto slotAudible = [&](int s) noexcept -> bool {
            const auto& cfg = slots[static_cast<size_t>(s)];
            if (cfg.mute) return false;
            if (anySolo && !cfg.solo) return false;
            return true;
        };

        // Update pattern-lock state transitions
        for (int s = 0; s < kNumSlots; ++s) {
            const bool locked = slots[static_cast<size_t>(s)].patternLock;
            if (locked && !wasPatternLocked_[static_cast<size_t>(s)]) {
                const int bars = (slots[static_cast<size_t>(s)].patternLockBars >= 4) ? 4 : 2;
                lockBarsCount_[static_cast<size_t>(s)] = bars;
                lockAnchorBar_[static_cast<size_t>(s)] = std::max(0, currentBar - bars);
            }
            wasPatternLocked_[static_cast<size_t>(s)] = locked;
        }

        // Micro-timing parameters in PPQ
        const float swingAmt = std::clamp(perf.swing, 0.50f, 0.72f);
        const double pushPullPpq = (static_cast<double>(perf.pushPullMs) +
                                    static_cast<double>(genome.defaultPushPullMs) * 0.25) *
                                   0.001 * beatsPerSecond;
        const double maxMarginPpq = stepPpqLen * 0.6 + std::fabs(pushPullPpq) + 0.05;

        const int64_t firstStepIdx = std::max<int64_t>(0, static_cast<int64_t>(std::floor((blockStartPpq - maxMarginPpq) / stepPpqLen)));
        const int64_t lastStepIdx  = static_cast<int64_t>(std::ceil((blockEndPpq + maxMarginPpq) / stepPpqLen));

        bool fillActiveThisBlock = false;
        bool clashAvoidedThisBlock = false;

        for (int64_t globalStep = firstStepIdx; globalStep <= lastStepIdx; ++globalStep) {
            const int barIdx = static_cast<int>(globalStep / stepsPerBar);
            const int stepInBar = static_cast<int>(globalStep % stepsPerBar);

            // Nominal step PPQ + swing + push/pull
            double stepPpq = static_cast<double>(globalStep) * stepPpqLen;

            // Swing delay
            if (!ts.isCompound() && swingAmt > 0.501f) {
                if (genome.isSwingTriplet) {
                    if ((stepInBar % 4) == 2) {
                        stepPpq += static_cast<double>((swingAmt - 0.50f) * 2.0f) * 0.65 * stepPpqLen;
                    }
                } else {
                    if ((stepInBar % 2) == 1) {
                        stepPpq += static_cast<double>((swingAmt - 0.50f) * 2.0f) * 0.48 * stepPpqLen;
                    }
                }
            }

            stepPpq += pushPullPpq;

            // Drunk micro-jitter (per step, bounded)
            if (genome.feelDrunkJitterMs > 0.05f) {
                const float r = hashRand01(static_cast<uint32_t>(barIdx), static_cast<uint32_t>(stepInBar), 99u, 17u) * 2.0f - 1.0f;
                const double jitterPpq = static_cast<double>(r * genome.feelDrunkJitterMs) * 0.001 * beatsPerSecond;
                stepPpq += jitterPpq;
            }

            if (stepPpq < blockStartPpq || stepPpq >= blockEndPpq) continue;

            const int sampleOffset = std::clamp(
                static_cast<int>(std::llround((stepPpq - blockStartPpq) / beatsPerSample)),
                0, numSamples - 1);

            // Update quantized reharm activation/expiry at exact step PPQ
            updateReharmAtPpq(stepPpq);

            // Detect bar/section transitions
            const bool isNewBar = (barIdx != lastBarIndex_);
            const bool sectionChanged = (section != lastSection_);
            if (isNewBar) {
                if (manualFillRequested_) {
                    manualFillBar_ = barIdx;
                    manualFillRequested_ = false;
                } else if (manualFillBar_ >= 0 && barIdx > manualFillBar_) {
                    manualFillBar_ = -1;
                }
                lastBarIndex_ = barIdx;
            }
            if (sectionChanged) {
                lastSection_ = section;
            }

            // If manual fill was triggered mid-bar, start immediately in current bar
            if (manualFillRequested_) {
                manualFillBar_ = barIdx;
                manualFillRequested_ = false;
            }

            // Chord for this bar and next bar
            const int chordBar = (numChordBars_ > 0) ? ((barIdx % numChordBars_ + numChordBars_) % numChordBars_) : 0;
            const int nextChordBar = (numChordBars_ > 0) ? ((chordBar + 1) % numChordBars_) : 0;
            const ChordSymbol activeChord = resolveActiveChordEx(chordBar, perf, listener, linkHints);
            const ChordSymbol nextChord = reharmPending_ ? pendingReharmChord_ : chords_[static_cast<size_t>(nextChordBar)];
            outResult.activeChord = activeChord;
            outResult.nextChord = nextChord;
            outResult.activeChordBarIndex = chordBar;
            outResult.beatsUntilNextChord = static_cast<float>(std::max(0.0, static_cast<double>(barIdx + 1) * barPpqLen - stepPpq));
            outResult.reharmActive = reharmActive_;
            outResult.reharmChord = activeReharmChord_;

            // Determine fill state (manual fill OR automatic 1-bar turnaround every 4th bar)
            const bool isManualFill = (manualFillBar_ == barIdx);
            const bool isAutoTurnaround = ((barIdx % 4) == 3) &&
                                          (stepInBar >= stepsPerBar / 2) &&
                                          (genome.drumsFillDensity * perf.energy * secDensityMul > 0.25f);
            const bool isFillStep = isManualFill || isAutoTurnaround;
            if (isFillStep) fillActiveThisBlock = true;

            const bool crashBar = (stepInBar == 0) &&
                                  (barIdx == 0 || (barIdx % 8) == 0 || (barIdx % 4) == 0 || sectionChanged);

            // Map step into native genome step index
            const int mappedStep = mapStep(stepInBar, stepsPerBar, nativeSteps);

            // -----------------------------------------------------------------
            // SLOT 0: DRUMS
            // -----------------------------------------------------------------
            bool kickTriggered = false;
            if (slotAudible(0)) {
                if (slots[0].patternLock) {
                    kickTriggered = replayLockedStep(0, barIdx, stepInBar, activeChord, sampleOffset, stepPpq, stepPpqLen,
                                                     beatsPerSample, numSamples, slots[0].velocityCurve, outEvents);
                } else {
                    clearLockStep(0, barIdx, stepInBar);
                    kickTriggered = generateDrumStep(barIdx, stepInBar, stepsPerBar, mappedStep, genome,
                                                     perf, slots[0], secEnergyMul, secDensityMul,
                                                     isFillStep, crashBar, sampleOffset, stepPpq, stepPpqLen,
                                                     outEvents);
                }
            }

            // -----------------------------------------------------------------
            // SLOT 1: BASS (with Kick-Lock % + Mode-aware walk + Clash Avoidance)
            // -----------------------------------------------------------------
            if (slotAudible(1) && activeChord.valid) {
                if (slots[1].patternLock) {
                    replayLockedStep(1, barIdx, stepInBar, activeChord, sampleOffset, stepPpq, stepPpqLen,
                                     beatsPerSample, numSamples, slots[1].velocityCurve, outEvents);
                } else {
                    clearLockStep(1, barIdx, stepInBar);
                    generateBassStep(barIdx, stepInBar, stepsPerBar, mappedStep, genome, activeChord, nextChord,
                                     perf, slots[1], activeMode, avoidMask, secEnergyMul, secDensityMul, kickTriggered,
                                     sampleOffset, stepPpq, stepPpqLen, outEvents, clashAvoidedThisBlock);
                }
            }

            // -----------------------------------------------------------------
            // SLOT 2: KEYS (voiceLeadKeys + Voicing Styles + Clash Avoidance + CC64 Sustain)
            // -----------------------------------------------------------------
            if (slotAudible(2) && activeChord.valid) {
                if (slots[2].patternLock) {
                    replayLockedStep(2, barIdx, stepInBar, activeChord, sampleOffset, stepPpq, stepPpqLen,
                                     beatsPerSample, numSamples, slots[2].velocityCurve, outEvents);
                } else {
                    clearLockStep(2, barIdx, stepInBar);
                    generateKeysStep(barIdx, stepInBar, mappedStep, genome, activeChord,
                                     perf, slots[2], avoidMask, secEnergyMul, secDensityMul, userDuck,
                                     sampleOffset, stepPpq, stepPpqLen, outEvents, clashAvoidedThisBlock);
                }
            }

            // -----------------------------------------------------------------
            // SLOT 3: GUITAR (guitarFretboardVoicing + Clash Avoidance + biomechanical strum)
            // -----------------------------------------------------------------
            if (slotAudible(3) && activeChord.valid) {
                if (slots[3].patternLock) {
                    replayLockedStep(3, barIdx, stepInBar, activeChord, sampleOffset, stepPpq, stepPpqLen,
                                     beatsPerSample, numSamples, slots[3].velocityCurve, outEvents);
                } else {
                    clearLockStep(3, barIdx, stepInBar);
                    generateGuitarStep(barIdx, stepInBar, mappedStep, genome, activeChord,
                                       perf, slots[3], avoidMask, secEnergyMul, secDensityMul, userDuck,
                                       sampleOffset, stepPpq, stepPpqLen, beatsPerSample, numSamples, outEvents,
                                       clashAvoidedThisBlock);
                }
            }

            // -----------------------------------------------------------------
            // SLOT 4: PERCUSSION
            // -----------------------------------------------------------------
            if (slotAudible(4)) {
                if (slots[4].patternLock) {
                    replayLockedStep(4, barIdx, stepInBar, activeChord, sampleOffset, stepPpq, stepPpqLen,
                                     beatsPerSample, numSamples, slots[4].velocityCurve, outEvents);
                } else {
                    clearLockStep(4, barIdx, stepInBar);
                    generatePercStep(barIdx, stepInBar, mappedStep, genome,
                                     perf, slots[4], secEnergyMul, secDensityMul,
                                     sampleOffset, stepPpq, stepPpqLen, outEvents);
                }
            }

            // -----------------------------------------------------------------
            // SLOT 5: HORNS / STRINGS (Call-and-Response + Mode Fills + Clash Avoidance)
            // -----------------------------------------------------------------
            if (slotAudible(5) && activeChord.valid) {
                if (slots[5].patternLock) {
                    replayLockedStep(5, barIdx, stepInBar, activeChord, sampleOffset, stepPpq, stepPpqLen,
                                     beatsPerSample, numSamples, slots[5].velocityCurve, outEvents);
                } else {
                    clearLockStep(5, barIdx, stepInBar);
                    generateHornsStep(barIdx, stepInBar, stepsPerBar, mappedStep, genome, activeChord,
                                      perf, slots[5], effectiveUserActivity, activeMode, avoidMask,
                                      secEnergyMul, secDensityMul, userDuck,
                                      sampleOffset, stepPpq, stepPpqLen, outEvents, clashAvoidedThisBlock);
                }
            }
        }

        outResult.fillActive = fillActiveThisBlock || (manualFillBar_ == currentBar);
        outResult.clashAvoided = clashAvoidedThisBlock;
        sortEventsBySampleOffset(outEvents);
    }

private:
    static constexpr int kMaxActiveNotes = 128;
    static constexpr int kMaxLockBars = 4;
    static constexpr int kMaxLockNotesPerStep = 8;

    struct ActiveNote {
        bool active = false;
        uint8_t slot = 0;
        uint8_t note = 0;
        double offPpq = 0.0;
    };

    struct LockedNote {
        uint8_t note = 0;
        uint8_t velocity = 0;
        uint8_t durSteps = 1;
        Articulation articulation = Articulation::Normal;
        int8_t capturedRoot = -1;
        float strumDelayMs = 0.0f;
    };

    struct LockedStep {
        std::array<LockedNote, kMaxLockNotesPerStep> notes {};
        uint8_t count = 0;
    };

    StyleLibrary library_ {};
    double sampleRate_ = 48000.0;

    std::array<ChordSymbol, kMaxChordBars> chords_ {};
    int numChordBars_ = 4;

    std::array<ActiveNote, kMaxActiveNotes> activeNotes_ {};
    std::array<uint64_t, kNumSlots> activeNoteMaskLo_ {};
    std::array<uint64_t, kNumSlots> activeNoteMaskHi_ {};
    VoicingResult prevKeysVoicing_ {};

    bool wasPlaying_ = false;
    double lastBlockEndPpq_ = -1.0;
    int lastBarIndex_ = -1;
    SongSection lastSection_ = SongSection::Chorus;
    bool manualFillRequested_ = false;
    int manualFillBar_ = -1;
    bool keysSustainDown_ = false;

    // Quantized Live Reharm Queue state
    bool reharmPending_ = false;
    bool reharmActive_ = false;
    ChordSymbol pendingReharmChord_ {};
    ChordSymbol activeReharmChord_ {};
    int pendingReharmTiming_ = 0;       // 0=Immediate, 1=NextBeat, 2=NextBar
    int pendingReharmDurationBars_ = 1;
    double reharmStartPpq_ = -1.0;
    double reharmEndPpq_ = -1.0;

    std::array<bool, kNumSlots> wasPatternLocked_ {};
    std::array<int, kNumSlots> lockAnchorBar_ {};
    std::array<int, kNumSlots> lockBarsCount_ { 2, 2, 2, 2, 2, 2 };
    std::array<std::array<std::array<LockedStep, kMaxStepsPerBar>, kMaxLockBars>, kNumSlots> lockBuffers_ {};

    // Deterministic fast hash PRNG in [0, 1)
    static float hashRand01(uint32_t bar, uint32_t step, uint32_t slot, uint32_t salt) noexcept {
        uint32_t x = bar * 0x9E3779B1u ^ step * 0x85EBCA77u ^ slot * 0xC2B2AE3Du ^ salt * 0x27D4EB2Fu;
        x ^= x >> 16;
        x *= 0x7FEB352Du;
        x ^= x >> 15;
        x *= 0x846CA68Bu;
        x ^= x >> 16;
        return static_cast<float>(x & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
    }

    static int mapStep(int stepInBar, int activeSteps, int nativeSteps) noexcept {
        if (activeSteps == nativeSteps) return std::clamp(stepInBar, 0, kMaxStepsPerBar - 1);
        if (stepInBar == 0) return 0;
        const int idx = (stepInBar * nativeSteps) / std::max(1, activeSteps);
        return std::clamp(idx, 0, std::max(0, nativeSteps - 1));
    }

    static SongSection resolveSection(const PerformanceControls& perf, const ListenerTelemetry& listener) noexcept {
        if (!perf.autoSectionFromEnergy) return perf.section;
        const float e = std::clamp(listener.energyEstimate, 0.0f, 1.0f);
        if (e < 0.22f) return SongSection::Intro;
        if (e < 0.45f) return SongSection::Verse;
        if (e < 0.65f) return SongSection::PreChorus;
        if (e < 0.85f) return SongSection::Chorus;
        return SongSection::Solo;
    }

    static float sectionEnergyMul(SongSection s) noexcept {
        switch (s) {
            case SongSection::Intro:     return 0.65f;
            case SongSection::Verse:     return 0.82f;
            case SongSection::PreChorus: return 0.94f;
            case SongSection::Chorus:    return 1.08f;
            case SongSection::Bridge:    return 0.88f;
            case SongSection::Solo:      return 1.12f;
            case SongSection::Outro:     return 0.72f;
        }
        return 1.0f;
    }

    static float sectionDensityMul(SongSection s) noexcept {
        switch (s) {
            case SongSection::Intro:     return 0.55f;
            case SongSection::Verse:     return 0.78f;
            case SongSection::PreChorus: return 0.92f;
            case SongSection::Chorus:    return 1.08f;
            case SongSection::Bridge:    return 0.85f;
            case SongSection::Solo:      return 1.12f;
            case SongSection::Outro:     return 0.62f;
        }
        return 1.0f;
    }

    void anchorPendingReharm(double blockStartPpq, double barPpqLen) noexcept {
        if (!reharmPending_ || reharmStartPpq_ >= 0.0) return;
        const double safeBarLen = std::max(0.25, barPpqLen);
        if (pendingReharmTiming_ == 0) {
            reharmStartPpq_ = blockStartPpq;
        } else if (pendingReharmTiming_ == 1) {
            const double curBeat = std::floor(blockStartPpq + 1.0e-9);
            reharmStartPpq_ = curBeat + 1.0;
        } else {
            const double curBar = std::floor(blockStartPpq / safeBarLen + 1.0e-9);
            reharmStartPpq_ = (curBar + 1.0) * safeBarLen;
        }
        reharmEndPpq_ = reharmStartPpq_ + static_cast<double>(std::max(1, pendingReharmDurationBars_)) * safeBarLen;
    }

    void updateReharmAtPpq(double ppq) noexcept {
        if (reharmPending_ && reharmStartPpq_ >= 0.0 && ppq + 1.0e-6 >= reharmStartPpq_) {
            reharmActive_ = true;
            activeReharmChord_ = pendingReharmChord_;
            reharmPending_ = false;
        }
        if (reharmActive_ && reharmEndPpq_ >= 0.0 && ppq + 1.0e-6 >= reharmEndPpq_) {
            reharmActive_ = false;
            activeReharmChord_ = {};
            reharmStartPpq_ = -1.0;
            reharmEndPpq_ = -1.0;
        }
    }

    ChordSymbol resolveActiveChordEx(int chordBar, const PerformanceControls& perf,
                                     const ListenerTelemetry& listener,
                                     const HarmonyLinkHints& linkHints) const noexcept {
        if (reharmActive_ && activeReharmChord_.valid) {
            return activeReharmChord_;
        }
        if (perf.harmonicSource == HarmonicSourceMode::HarmonyHudLink &&
            linkHints.peerAlive && linkHints.liveChord.valid) {
            return linkHints.liveChord;
        }
        if ((perf.harmonicSource == HarmonicSourceMode::LiveAudio || perf.autoDetectChords) &&
            listener.chordConfidence >= 0.45f && listener.detectedChord.valid) {
            return listener.detectedChord;
        }
        const int idx = std::clamp(chordBar, 0, std::max(0, numChordBars_ - 1));
        return chords_[static_cast<size_t>(idx)];
    }

    void setActiveNoteBit(uint8_t slot, uint8_t note, bool on) noexcept {
        if (slot >= static_cast<uint8_t>(kNumSlots) || note >= 128) return;
        const size_t s = static_cast<size_t>(slot);
        if (note < 64) {
            const uint64_t bit = static_cast<uint64_t>(1ull) << note;
            if (on) activeNoteMaskLo_[s] |= bit;
            else    activeNoteMaskLo_[s] &= ~bit;
        } else {
            const uint64_t bit = static_cast<uint64_t>(1ull) << (note - 64);
            if (on) activeNoteMaskHi_[s] |= bit;
            else    activeNoteMaskHi_[s] &= ~bit;
        }
    }

    static uint8_t scaleVelocity(float baseVel, float energy, float secEnergyMul,
                                 float velCurve, float jitter) noexcept {
        float v = baseVel * (0.55f + 0.55f * energy) * secEnergyMul + jitter;
        v = std::clamp(v, 1.0f, 127.0f);
        if (std::fabs(velCurve - 1.0f) > 0.01f) {
            const float norm = v / 127.0f;
            const float exp = std::clamp(velCurve, 0.35f, 2.5f);
            v = std::pow(norm, exp) * 127.0f;
        }
        return static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(v)), 1, 127));
    }

    void emitCC(BlockEventList& out, int sampleOffset, uint8_t slot, uint8_t ccNum, uint8_t val, double ppq) noexcept {
        BandNoteEvent ev {};
        ev.sampleOffset = sampleOffset;
        ev.slot = slot;
        ev.type = EventType::ControlChange;
        ev.data1 = ccNum;
        ev.data2 = val;
        ev.ppqPosition = ppq;
        out.push(ev);
    }

    void emitNote(BlockEventList& out, int sampleOffset, uint8_t slot, int noteMidi, uint8_t vel,
                  Articulation art, double noteOnPpq, double durPpq) noexcept {
        const uint8_t note = static_cast<uint8_t>(std::clamp(noteMidi, 0, 127));
        const double offPpq = noteOnPpq + std::max(0.02, durPpq);

        // If this (slot, note) is already sounding, emit a paired NoteOff first
        int freeIdx = -1;
        for (int i = 0; i < kMaxActiveNotes; ++i) {
            auto& an = activeNotes_[static_cast<size_t>(i)];
            if (an.active && an.slot == slot && an.note == note) {
                BandNoteEvent offEv {};
                offEv.sampleOffset = sampleOffset;
                offEv.slot = slot;
                offEv.type = EventType::NoteOff;
                offEv.data1 = note;
                offEv.data2 = 0;
                offEv.ppqPosition = noteOnPpq;
                out.push(offEv);
                setActiveNoteBit(slot, note, false);
                an.active = false;
                freeIdx = i;
                break;
            }
            if (!an.active && freeIdx < 0) freeIdx = i;
        }

        // Steal oldest slot if full
        if (freeIdx < 0) {
            int oldestIdx = 0;
            double oldestPpq = activeNotes_[0].offPpq;
            for (int i = 1; i < kMaxActiveNotes; ++i) {
                if (activeNotes_[static_cast<size_t>(i)].offPpq < oldestPpq) {
                    oldestPpq = activeNotes_[static_cast<size_t>(i)].offPpq;
                    oldestIdx = i;
                }
            }
            auto& stolen = activeNotes_[static_cast<size_t>(oldestIdx)];
            BandNoteEvent offEv {};
            offEv.sampleOffset = sampleOffset;
            offEv.slot = stolen.slot;
            offEv.type = EventType::NoteOff;
            offEv.data1 = stolen.note;
            offEv.data2 = 0;
            offEv.ppqPosition = noteOnPpq;
            out.push(offEv);
            setActiveNoteBit(stolen.slot, stolen.note, false);
            stolen.active = false;
            freeIdx = oldestIdx;
        }

        BandNoteEvent onEv {};
        onEv.sampleOffset = sampleOffset;
        onEv.slot = slot;
        onEv.type = EventType::NoteOn;
        onEv.data1 = note;
        onEv.data2 = vel;
        onEv.articulation = art;
        onEv.ppqPosition = noteOnPpq;
        out.push(onEv);
        setActiveNoteBit(slot, note, true);

        auto& entry = activeNotes_[static_cast<size_t>(freeIdx)];
        entry.active = true;
        entry.slot = slot;
        entry.note = note;
        entry.offPpq = offPpq;
    }

    void processPendingNoteOffs(double blockStartPpq, double blockEndPpq,
                                double beatsPerSample, int numSamples,
                                BlockEventList& out) noexcept {
        for (auto& an : activeNotes_) {
            if (!an.active) continue;
            if (an.offPpq < blockEndPpq) {
                const int offSample = std::clamp(
                    static_cast<int>(std::llround((an.offPpq - blockStartPpq) / beatsPerSample)),
                    0, numSamples - 1);
                BandNoteEvent offEv {};
                offEv.sampleOffset = offSample;
                offEv.slot = an.slot;
                offEv.type = EventType::NoteOff;
                offEv.data1 = an.note;
                offEv.data2 = 0;
                offEv.ppqPosition = std::max(blockStartPpq, an.offPpq);
                out.push(offEv);
                setActiveNoteBit(an.slot, an.note, false);
                an.active = false;
            }
        }
    }

    void flushAllNotes(BlockEventList& out, int sampleOffset, double ppq) noexcept {
        for (auto& an : activeNotes_) {
            if (!an.active) continue;
            BandNoteEvent offEv {};
            offEv.sampleOffset = sampleOffset;
            offEv.slot = an.slot;
            offEv.type = EventType::NoteOff;
            offEv.data1 = an.note;
            offEv.data2 = 0;
            offEv.ppqPosition = ppq;
            out.push(offEv);
            setActiveNoteBit(an.slot, an.note, false);
            an.active = false;
        }
        activeNoteMaskLo_.fill(0);
        activeNoteMaskHi_.fill(0);
        if (keysSustainDown_) {
            emitCC(out, sampleOffset, 2, 64, 0, ppq);
            keysSustainDown_ = false;
        }
    }

    void clearLockBuffers() noexcept {
        for (auto& slotBuf : lockBuffers_) {
            for (auto& barBuf : slotBuf) {
                for (auto& st : barBuf) st.count = 0;
            }
        }
    }

    void clearLockStep(int slot, int barIdx, int stepInBar) noexcept {
        const int b = ((barIdx % kMaxLockBars) + kMaxLockBars) % kMaxLockBars;
        lockBuffers_[static_cast<size_t>(slot)][static_cast<size_t>(b)][static_cast<size_t>(stepInBar)].count = 0;
    }

    void recordLockNote(int slot, int barIdx, int stepInBar, int note, uint8_t vel,
                        int durSteps, Articulation art, int capturedRoot, float strumDelayMs = 0.0f) noexcept {
        const int b = ((barIdx % kMaxLockBars) + kMaxLockBars) % kMaxLockBars;
        auto& ls = lockBuffers_[static_cast<size_t>(slot)][static_cast<size_t>(b)][static_cast<size_t>(stepInBar)];
        if (ls.count >= kMaxLockNotesPerStep) return;
        auto& ln = ls.notes[static_cast<size_t>(ls.count++)];
        ln.note = static_cast<uint8_t>(std::clamp(note, 0, 127));
        ln.velocity = vel;
        ln.durSteps = static_cast<uint8_t>(std::clamp(durSteps, 1, 24));
        ln.articulation = art;
        ln.capturedRoot = static_cast<int8_t>(capturedRoot);
        ln.strumDelayMs = strumDelayMs;
    }

    bool replayLockedStep(int slot, int barIdx, int stepInBar, const ChordSymbol& activeChord,
                          int sampleOffset, double stepPpq, double stepPpqLen,
                          double beatsPerSample, int numSamples, float /*velCurve*/,
                          BlockEventList& out) noexcept {
        const int lockLen = std::max(1, lockBarsCount_[static_cast<size_t>(slot)]);
        const int srcBar = (lockAnchorBar_[static_cast<size_t>(slot)] + (barIdx % lockLen)) % kMaxLockBars;
        const auto& ls = lockBuffers_[static_cast<size_t>(slot)][static_cast<size_t>(srcBar)][static_cast<size_t>(stepInBar)];
        bool kickHit = false;
        for (uint8_t i = 0; i < ls.count; ++i) {
            const auto& ln = ls.notes[static_cast<size_t>(i)];
            int note = static_cast<int>(ln.note);
            // Transpose melodic/harmonic slots cleanly if chord root changed while locked
            if (slot != 0 && slot != 4 && ln.capturedRoot >= 0 && activeChord.valid && activeChord.rootPitchClass >= 0) {
                int delta = activeChord.rootPitchClass - static_cast<int>(ln.capturedRoot);
                if (delta > 6) delta -= 12;
                if (delta < -6) delta += 12;
                note = std::clamp(note + delta, 24, 96);
            }
            const int delaySamples = static_cast<int>(std::llround(static_cast<double>(ln.strumDelayMs) * 0.001 * sampleRate_));
            const int sOff = std::clamp(sampleOffset + delaySamples, 0, numSamples - 1);
            const double evPpq = stepPpq + static_cast<double>(delaySamples) * beatsPerSample;
            emitNote(out, sOff, static_cast<uint8_t>(slot), note, ln.velocity, ln.articulation,
                     evPpq, static_cast<double>(ln.durSteps) * stepPpqLen * 0.90);
            if (slot == 0 && note == DrumNote::Kick) kickHit = true;
        }
        return kickHit;
    }

    // -------------------------------------------------------------------------
    // SLOT 0: DRUMMER
    // -------------------------------------------------------------------------
    bool generateDrumStep(int barIdx, int stepInBar, int stepsPerBar, int mappedStep,
                          const StyleGenome& genome, const PerformanceControls& perf,
                          const SlotConfig& cfg, float secEnergyMul, float secDensityMul,
                          bool isFillStep, bool crashBar,
                          int sampleOffset, double stepPpq, double stepPpqLen,
                          BlockEventList& out) noexcept {
        bool kickHit = false;
        const float busy = std::clamp(cfg.busy * secDensityMul, 0.05f, 1.35f);
        const double drumDur = stepPpqLen * 0.45;

        auto emitDrum = [&](int note, float baseVel, Articulation art, uint32_t salt) noexcept {
            const float jit = (hashRand01(static_cast<uint32_t>(barIdx), static_cast<uint32_t>(stepInBar), 0u, salt) - 0.5f) * 14.0f;
            const uint8_t vel = scaleVelocity(baseVel, perf.energy, secEnergyMul, cfg.velocityCurve, jit);
            emitNote(out, sampleOffset, 0, note, vel, art, stepPpq, drumDur);
            recordLockNote(0, barIdx, stepInBar, note, vel, 1, art, -1);
        };

        // Crash on bar 1 / section changes
        if (crashBar && perf.energy * secEnergyMul > 0.40f) {
            emitDrum(DrumNote::Crash1, 102.0f, Articulation::Normal, 101u);
        }

        // Turnaround / Manual Fill generator
        if (isFillStep) {
            const int fillStart = (manualFillBar_ == barIdx) ? 0 : (stepsPerBar / 2);
            const int fillPos = stepInBar - fillStart;
            const int fillLen = std::max(1, stepsPerBar - fillStart);
            const float progress = static_cast<float>(fillPos) / static_cast<float>(fillLen);
            const float rFill = hashRand01(static_cast<uint32_t>(barIdx), static_cast<uint32_t>(stepInBar), 0u, 77u);

            if (rFill < std::clamp(0.55f + 0.40f * busy, 0.25f, 0.96f)) {
                int fillNote = DrumNote::Snare;
                if (progress < 0.30f)      fillNote = DrumNote::Snare;
                else if (progress < 0.55f) fillNote = DrumNote::HighTom;
                else if (progress < 0.80f) fillNote = DrumNote::MidTom;
                else                       fillNote = DrumNote::LowFloorTom;

                const float baseVel = 82.0f + progress * 32.0f;
                emitDrum(fillNote, baseVel, Articulation::Normal, 78u);
            }
            if ((stepInBar % 4) == 0 || stepInBar == stepsPerBar - 1) {
                emitDrum(DrumNote::Kick, 96.0f, Articulation::Normal, 79u);
                kickHit = true;
            }
            return kickHit;
        }

        // Kick
        const float pKick = genome.kick[static_cast<size_t>(mappedStep)];
        if (pKick > 0.01f) {
            const float thresh = (pKick >= 0.85f) ? 1.0f : std::clamp(pKick * busy, 0.0f, 1.0f);
            if (hashRand01(static_cast<uint32_t>(barIdx), static_cast<uint32_t>(stepInBar), 0u, 1u) < thresh) {
                emitDrum(DrumNote::Kick, 88.0f + pKick * 32.0f, Articulation::Normal, 2u);
                kickHit = true;
            }
        }

        // Snare / SideStick / Rimshot
        const float pSnare = genome.snare[static_cast<size_t>(mappedStep)];
        bool snareHit = false;
        if (pSnare > 0.01f) {
            const float thresh = (pSnare >= 0.85f) ? 1.0f : std::clamp(pSnare * busy, 0.0f, 1.0f);
            if (hashRand01(static_cast<uint32_t>(barIdx), static_cast<uint32_t>(stepInBar), 0u, 3u) < thresh) {
                int sNote = DrumNote::Snare;
                Articulation sArt = Articulation::Normal;
                if (genome.useSideStick && perf.energy < 0.72f) {
                    sNote = DrumNote::SideStick;
                    sArt = Articulation::SideStick;
                } else if (perf.energy * secEnergyMul > 0.92f && pSnare >= 0.95f) {
                    sNote = DrumNote::SnareRim;
                    sArt = Articulation::Rimshot;
                }
                emitDrum(sNote, 84.0f + pSnare * 34.0f, sArt, 4u);
                snareHit = true;
            }
        }

        // Ghost Snare
        const float pGhost = genome.ghostSnare[static_cast<size_t>(mappedStep)];
        if (!snareHit && pGhost > 0.01f && busy > 0.30f) {
            if (hashRand01(static_cast<uint32_t>(barIdx), static_cast<uint32_t>(stepInBar), 0u, 5u) < pGhost * busy) {
                const float velBase = genome.useBrushFeel ? 38.0f : (28.0f + pGhost * 24.0f);
                emitDrum(DrumNote::Snare, velBase, Articulation::Ghost, 6u);
            }
        }

        // Ride vs Hi-Hat selection (Chorus/Solo or Jazz prefers Ride when ride pattern is strong)
        const float pRide = genome.ride[static_cast<size_t>(mappedStep)];
        const float pHatC = genome.hatClosed[static_cast<size_t>(mappedStep)];
        const float pHatO = genome.hatOpen[static_cast<size_t>(mappedStep)];
        const bool preferRide = (genome.isSwingTriplet || genome.useBrushFeel ||
                                 (pRide > 0.60f && (perf.section == SongSection::Chorus || perf.section == SongSection::Solo)));

        if (preferRide && pRide > 0.01f) {
            if (hashRand01(static_cast<uint32_t>(barIdx), static_cast<uint32_t>(stepInBar), 0u, 7u) < pRide * busy) {
                const bool bell = (stepInBar % 4 == 0) && (perf.energy > 0.80f) && !genome.useBrushFeel;
                emitDrum(bell ? DrumNote::RideBell : DrumNote::Ride, 68.0f + pRide * 30.0f,
                         bell ? Articulation::RideBell : Articulation::Normal, 8u);
            }
            // Pedal hat on 2 & 4 in jazz
            if (pHatC > 0.65f && (stepInBar == 4 || stepInBar == 12)) {
                emitDrum(DrumNote::PedalHat, 58.0f, Articulation::HatPedal, 9u);
            }
        } else {
            // Hi-Hat (open takes precedence over closed)
            bool didOpen = false;
            if (pHatO > 0.01f &&
                hashRand01(static_cast<uint32_t>(barIdx), static_cast<uint32_t>(stepInBar), 0u, 10u) < pHatO * busy) {
                if (cfg.cc4HiHatEnabled) {
                    emitCC(out, sampleOffset, 0, 4, 110, stepPpq);
                }
                emitDrum(DrumNote::OpenHat, 74.0f + pHatO * 28.0f, Articulation::HatOpen, 11u);
                didOpen = true;
            }
            if (!didOpen && pHatC > 0.01f &&
                hashRand01(static_cast<uint32_t>(barIdx), static_cast<uint32_t>(stepInBar), 0u, 12u) < pHatC * busy) {
                if (cfg.cc4HiHatEnabled) {
                    const uint8_t cc4Val = static_cast<uint8_t>(std::clamp(
                        static_cast<int>(18.0f + perf.energy * 36.0f), 0, 127));
                    emitCC(out, sampleOffset, 0, 4, cc4Val, stepPpq);
                }
                const Articulation hArt = (perf.energy > 0.78f) ? Articulation::HatHalfOpen : Articulation::HatClosed;
                emitDrum(DrumNote::ClosedHat, 56.0f + pHatC * 34.0f, hArt, 13u);
            }
        }

        return kickHit;
    }

    // -------------------------------------------------------------------------
    // SLOT 1: BASSIST (with Kick-Lock % + Mode-aware walk + Clash Avoidance)
    // -------------------------------------------------------------------------
    void generateBassStep(int barIdx, int stepInBar, int stepsPerBar, int mappedStep,
                          const StyleGenome& genome, const ChordSymbol& activeChord,
                          const ChordSymbol& nextChord, const PerformanceControls& perf,
                          const SlotConfig& cfg, ScaleMode activeMode, uint16_t avoidMask,
                          float secEnergyMul, float secDensityMul,
                          bool kickTriggered,
                          int sampleOffset, double stepPpq, double stepPpqLen,
                          BlockEventList& out, bool& clashAvoided) noexcept {
        const float busy = std::clamp(cfg.busy * secDensityMul, 0.05f, 1.35f);
        float prob = genome.bassSync[static_cast<size_t>(mappedStep)] * busy;

        // Kick-Lock %: align bass hits directly to the drummer's kick!
        if (kickTriggered) {
            prob = std::max(prob, cfg.kickLock);
        } else if (cfg.kickLock > 0.70f && stepInBar != 0) {
            prob *= (1.0f - (cfg.kickLock - 0.70f) * 0.8f);
        }

        if (prob <= 0.01f) return;
        const float r = hashRand01(static_cast<uint32_t>(barIdx), static_cast<uint32_t>(stepInBar), 1u, 1u);
        if (r >= std::clamp(prob, 0.0f, 1.0f)) return;

        // Base bass MIDI note in E1..D#3 (MIDI 28..47)
        const int bassPc = activeChord.effectiveBass();
        int baseMidi = 36 + bassPc; // C2..B2
        if (baseMidi > 44) baseMidi -= 12; // keep low register tight (E1..G#2)
        if (baseMidi < 28) baseMidi += 12;

        int note = baseMidi;
        bool isPassingTone = false;
        int walkDeg = 0;
        Articulation art = Articulation::Normal;

        const float rPitch = hashRand01(static_cast<uint32_t>(barIdx), static_cast<uint32_t>(stepInBar), 1u, 2u);
        const bool isApproachStep = (stepInBar >= stepsPerBar - 2) && nextChord.valid &&
                                    (nextChord.effectiveBass() != bassPc);

        if (stepInBar == 0) {
            note = baseMidi; // Anchor beat 1 on chord bass
        } else if (isApproachStep && rPitch < genome.bassWalkProb) {
            // Chromatic or scalar leading tone into next bar's bass
            int targetMidi = 36 + nextChord.effectiveBass();
            if (targetMidi > 44) targetMidi -= 12;
            note = (rPitch < genome.bassWalkProb * 0.5f) ? (targetMidi - 1) : (targetMidi + 1);
            isPassingTone = true;
        } else if (rPitch < genome.bassOctaveProb) {
            note = baseMidi + 12;
            if (cfg.builtInPatch == static_cast<int>(BassPatch::SlapFunk)) art = Articulation::Slap;
        } else if (rPitch < genome.bassOctaveProb + genome.bassWalkProb * 0.55f) {
            // Mode-aware scale walk tone
            walkDeg = static_cast<int>(hashRand01(static_cast<uint32_t>(barIdx), static_cast<uint32_t>(stepInBar), 1u, 3u) * 5.0f);
            note = scaleNoteInMode(activeChord.rootPitchClass, activeMode, walkDeg, 36);
            while (note > 50) note -= 12;
            while (note < 28) note += 12;
            isPassingTone = true;
        } else if ((stepInBar % 4) != 0 && rPitch > 0.78f) {
            note = baseMidi + 7; // 5th
            isPassingTone = true;
        }

        // Auto-Clash Avoidance for bass walk/passing tones
        if (isPassingTone && avoidMask != 0 && ((avoidMask & static_cast<uint16_t>(1u << (note % 12))) != 0)) {
            int nudged = scaleNoteInMode(activeChord.rootPitchClass, activeMode, walkDeg + 1, 36);
            while (nudged > 50) nudged -= 12;
            while (nudged < 28) nudged += 12;
            if ((avoidMask & static_cast<uint16_t>(1u << (nudged % 12))) == 0) {
                note = nudged;
            } else {
                note = baseMidi;
            }
            clashAvoided = true;
        }

        const int durSteps = (genome.bassSync[static_cast<size_t>(mappedStep)] > 0.8f && (stepInBar % 4) == 0) ? 3 : 2;
        const float jit = (hashRand01(static_cast<uint32_t>(barIdx), static_cast<uint32_t>(stepInBar), 1u, 4u) - 0.5f) * 10.0f;
        const uint8_t vel = scaleVelocity(kickTriggered ? 104.0f : 90.0f, perf.energy, secEnergyMul, cfg.velocityCurve, jit);

        emitNote(out, sampleOffset, 1, note, vel, art, stepPpq, static_cast<double>(durSteps) * stepPpqLen * 0.88);
        recordLockNote(1, barIdx, stepInBar, note, vel, durSteps, art, activeChord.rootPitchClass);
    }

    // -------------------------------------------------------------------------
    // SLOT 2: KEYS (voiceLeadKeys + Voicing Styles + Clash Avoidance + CC64)
    // -------------------------------------------------------------------------
    void generateKeysStep(int barIdx, int stepInBar, int mappedStep,
                          const StyleGenome& genome, const ChordSymbol& activeChord,
                          const PerformanceControls& perf, const SlotConfig& cfg,
                          uint16_t avoidMask,
                          float secEnergyMul, float secDensityMul, float userDuck,
                          int sampleOffset, double stepPpq, double stepPpqLen,
                          BlockEventList& out, bool& clashAvoided) noexcept {
        const float busy = std::clamp(cfg.busy * secDensityMul * (1.0f - userDuck * 0.45f), 0.05f, 1.35f);
        const float pComp = genome.keysComp[static_cast<size_t>(mappedStep)];
        if (pComp <= 0.01f) return;

        const float thresh = (stepInBar == 0 && pComp >= 0.85f) ? 1.0f : std::clamp(pComp * busy, 0.0f, 1.0f);
        if (hashRand01(static_cast<uint32_t>(barIdx), static_cast<uint32_t>(stepInBar), 2u, 1u) >= thresh) return;

        // Smooth minimal-L1-movement voice leading with style & clash avoidance
        const VoicingResult voicing = voiceLeadKeys(activeChord, prevKeysVoicing_, 48, 76, perf.keysVoicing, avoidMask);
        if (voicing.count <= 0) return;

        if (avoidMask != 0) {
            const VoicingResult rawVoicing = voiceLeadKeys(activeChord, prevKeysVoicing_, 48, 76, perf.keysVoicing, 0);
            if (rawVoicing.count != voicing.count || rawVoicing.notes != voicing.notes) {
                clashAvoided = true;
            }
        }

        prevKeysVoicing_ = voicing;

        // CC64 sustain pedal re-pedaling
        if (cfg.cc64SustainEnabled) {
            if (keysSustainDown_) {
                emitCC(out, sampleOffset, 2, 64, 0, stepPpq);
            }
            const bool holdPedal = (genome.keysSustainSteps >= 4);
            emitCC(out, sampleOffset, 2, 64, holdPedal ? 127 : 0, stepPpq);
            keysSustainDown_ = holdPedal;
        }

        const int durSteps = std::clamp(genome.keysSustainSteps, 1, 12);
        const double durPpq = static_cast<double>(durSteps) * stepPpqLen * 0.92;
        const float duckVel = 1.0f - userDuck * 0.25f;

        for (int i = 0; i < voicing.count; ++i) {
            const float jit = (hashRand01(static_cast<uint32_t>(barIdx), static_cast<uint32_t>(stepInBar), 2u, 10u + static_cast<uint32_t>(i)) - 0.5f) * 10.0f;
            const uint8_t vel = scaleVelocity((76.0f + pComp * 24.0f) * duckVel, perf.energy, secEnergyMul, cfg.velocityCurve, jit);
            emitNote(out, sampleOffset, 2, voicing.notes[static_cast<size_t>(i)], vel, Articulation::Normal, stepPpq, durPpq);
            recordLockNote(2, barIdx, stepInBar, voicing.notes[static_cast<size_t>(i)], vel, durSteps, Articulation::Normal, activeChord.rootPitchClass);
        }
    }

    // -------------------------------------------------------------------------
    // SLOT 3: GUITAR (guitarFretboardVoicing + Clash Avoidance + strum)
    // -------------------------------------------------------------------------
    void generateGuitarStep(int barIdx, int stepInBar, int mappedStep,
                            const StyleGenome& genome, const ChordSymbol& activeChord,
                            const PerformanceControls& perf, const SlotConfig& cfg,
                            uint16_t avoidMask,
                            float secEnergyMul, float secDensityMul, float userDuck,
                            int sampleOffset, double stepPpq, double stepPpqLen,
                            double beatsPerSample, int numSamples,
                            BlockEventList& out, bool& clashAvoided) noexcept {
        const float busy = std::clamp(cfg.busy * secDensityMul * (1.0f - userDuck * 0.45f), 0.05f, 1.35f);
        const float pStrum = genome.guitarStrum[static_cast<size_t>(mappedStep)];
        if (pStrum <= 0.01f) return;

        if (hashRand01(static_cast<uint32_t>(barIdx), static_cast<uint32_t>(stepInBar), 3u, 1u) >= std::clamp(pStrum * busy, 0.0f, 1.0f)) {
            return;
        }

        const VoicingResult rawVoicing = guitarFretboardVoicing(activeChord);
        const VoicingResult voicing = (avoidMask != 0) ? filterAvoidGuitarVoicing(rawVoicing, avoidMask) : rawVoicing;
        if (voicing.count <= 0) return;
        if (avoidMask != 0 && voicing.count != rawVoicing.count) {
            clashAvoided = true;
        }

        const float pUp = genome.guitarUpstroke[static_cast<size_t>(mappedStep)];
        const bool isUpstroke = (pUp > 0.35f) &&
                                (hashRand01(static_cast<uint32_t>(barIdx), static_cast<uint32_t>(stepInBar), 3u, 2u) < pUp);
        const bool isMute = genome.reggaeSkank ||
                            (hashRand01(static_cast<uint32_t>(barIdx), static_cast<uint32_t>(stepInBar), 3u, 3u) < genome.guitarMuteProb);

        const Articulation art = isMute ? Articulation::PalmMute
                                        : (isUpstroke ? Articulation::StrumUp : Articulation::StrumDown);
        const int durSteps = isMute ? 1 : (isUpstroke ? 2 : 3);
        const double durPpq = static_cast<double>(durSteps) * stepPpqLen * (isMute ? 0.45 : 0.88);
        const float strumSpreadMs = genome.reggaeSkank ? 3.0f : (isUpstroke ? 5.0f : 7.5f);
        const float duckVel = 1.0f - userDuck * 0.25f;

        // Upstrokes/skanks focus on top 3-4 strings; downstrokes strum full voicing
        const int startIdx = (isUpstroke && voicing.count > 3) ? (voicing.count - 4) : 0;
        const int numStrings = voicing.count - startIdx;

        for (int k = 0; k < numStrings; ++k) {
            const int idx = isUpstroke ? (voicing.count - 1 - k) : (startIdx + k);
            const float delayMs = static_cast<float>(k) * strumSpreadMs;
            const int delaySamples = static_cast<int>(std::llround(static_cast<double>(delayMs) * 0.001 * sampleRate_));
            const int sOff = std::clamp(sampleOffset + delaySamples, 0, numSamples - 1);
            const double evPpq = stepPpq + static_cast<double>(delaySamples) * beatsPerSample;

            const float jit = (hashRand01(static_cast<uint32_t>(barIdx), static_cast<uint32_t>(stepInBar), 3u, 20u + static_cast<uint32_t>(k)) - 0.5f) * 10.0f;
            const float baseV = (isMute ? 74.0f : 86.0f) * duckVel;
            const uint8_t vel = scaleVelocity(baseV, perf.energy, secEnergyMul, cfg.velocityCurve, jit);

            emitNote(out, sOff, 3, voicing.notes[static_cast<size_t>(idx)], vel, art, evPpq, durPpq);
            recordLockNote(3, barIdx, stepInBar, voicing.notes[static_cast<size_t>(idx)], vel, durSteps, art, activeChord.rootPitchClass, delayMs);
        }
    }

    // -------------------------------------------------------------------------
    // SLOT 4: PERCUSSION
    // -------------------------------------------------------------------------
    void generatePercStep(int barIdx, int stepInBar, int mappedStep,
                          const StyleGenome& genome, const PerformanceControls& perf,
                          const SlotConfig& cfg, float secEnergyMul, float secDensityMul,
                          int sampleOffset, double stepPpq, double stepPpqLen,
                          BlockEventList& out) noexcept {
        const float busy = std::clamp(cfg.busy * secDensityMul, 0.05f, 1.35f);
        const double durPpq = stepPpqLen * 0.45;

        int priNote = PercNote::Shaker;
        int secNote = PercNote::Tambourine;
        switch (cfg.builtInPatch % 4) {
            case 1: // LatinCongaBongo
                priNote = (stepInBar % 4 == 0) ? PercNote::OpenConga : PercNote::MuteConga;
                secNote = (stepInBar % 2 == 0) ? PercNote::HighBongo : PercNote::LowConga;
                break;
            case 2: // AfrobeatPerc
                priNote = PercNote::Cabasa;
                secNote = (stepInBar % 4 == 0) ? PercNote::Cowbell : PercNote::Clave;
                break;
            case 3: // ElectronicMicro
                priNote = PercNote::Shaker;
                secNote = PercNote::Woodblock;
                break;
            default: // StudioShakerTamb
                priNote = PercNote::Shaker;
                secNote = PercNote::Tambourine;
                break;
        }

        const float pPri = genome.percPrimary[static_cast<size_t>(mappedStep)];
        if (pPri > 0.01f &&
            hashRand01(static_cast<uint32_t>(barIdx), static_cast<uint32_t>(stepInBar), 4u, 1u) < pPri * busy) {
            const float jit = (hashRand01(static_cast<uint32_t>(barIdx), static_cast<uint32_t>(stepInBar), 4u, 2u) - 0.5f) * 12.0f;
            const uint8_t vel = scaleVelocity(58.0f + pPri * 32.0f, perf.energy, secEnergyMul, cfg.velocityCurve, jit);
            emitNote(out, sampleOffset, 4, priNote, vel, Articulation::Normal, stepPpq, durPpq);
            recordLockNote(4, barIdx, stepInBar, priNote, vel, 1, Articulation::Normal, -1);
        }

        const float pSec = genome.percSecondary[static_cast<size_t>(mappedStep)];
        if (pSec > 0.01f &&
            hashRand01(static_cast<uint32_t>(barIdx), static_cast<uint32_t>(stepInBar), 4u, 3u) < pSec * busy) {
            const float jit = (hashRand01(static_cast<uint32_t>(barIdx), static_cast<uint32_t>(stepInBar), 4u, 4u) - 0.5f) * 12.0f;
            const uint8_t vel = scaleVelocity(66.0f + pSec * 34.0f, perf.energy, secEnergyMul, cfg.velocityCurve, jit);
            emitNote(out, sampleOffset, 4, secNote, vel, Articulation::Normal, stepPpq, durPpq);
            recordLockNote(4, barIdx, stepInBar, secNote, vel, 1, Articulation::Normal, -1);
        }
    }

    // -------------------------------------------------------------------------
    // SLOT 5: HORNS / STRINGS (Call-and-Response + Mode Fills + Clash Avoidance)
    // -------------------------------------------------------------------------
    void generateHornsStep(int barIdx, int stepInBar, int stepsPerBar, int mappedStep,
                           const StyleGenome& genome, const ChordSymbol& activeChord,
                           const PerformanceControls& perf, const SlotConfig& cfg,
                           float effectiveUserActivity, ScaleMode activeMode, uint16_t avoidMask,
                           float secEnergyMul, float secDensityMul, float userDuck,
                           int sampleOffset, double stepPpq, double stepPpqLen,
                           BlockEventList& out, bool& clashAvoided) noexcept {
        // Call-and-response: duck heavily when the user is actively playing
        const float spaceGate = cfg.callAndResponse ? (1.0f - userDuck) : 1.0f;
        if (spaceGate < 0.12f) return;

        const float busy = std::clamp(cfg.busy * secDensityMul * spaceGate, 0.0f, 1.35f);
        const float pStab = genome.hornsStab[static_cast<size_t>(mappedStep)];

        // In low-activity gaps (effectiveUserActivity < 0.25), allow short melodic answer fills in second half of bar
        const bool isGapAnswer = cfg.callAndResponse &&
                                 (effectiveUserActivity < 0.25f) &&
                                 (stepInBar >= stepsPerBar / 2) &&
                                 ((stepInBar % 2) == 0);

        float prob = pStab * busy;
        if (isGapAnswer) {
            prob = std::max(prob, genome.hornsFillDensity * busy * 0.55f);
        }
        if (prob <= 0.01f) return;

        if (hashRand01(static_cast<uint32_t>(barIdx), static_cast<uint32_t>(stepInBar), 5u, 1u) >= std::clamp(prob, 0.0f, 1.0f)) {
            return;
        }

        const bool isStrings = (cfg.builtInPatch == static_cast<int>(HornsPatch::WarmStrings));
        const Articulation art = isStrings ? Articulation::Swell : Articulation::Stab;
        const int durSteps = isStrings ? 6 : (pStab >= 0.70f ? 2 : 1);
        const double durPpq = static_cast<double>(durSteps) * stepPpqLen * (isStrings ? 0.95 : 0.70);

        // Melodic counter-melody fill in gap answers when not a primary stab step
        if (isGapAnswer && !isStrings && pStab < 0.35f) {
            const int deg = static_cast<int>(hashRand01(static_cast<uint32_t>(barIdx), static_cast<uint32_t>(stepInBar), 5u, 7u) * 7.0f);
            int note = scaleNoteInMode(activeChord.rootPitchClass, activeMode, deg, 60);
            while (note > 84) note -= 12;
            while (note < 60) note += 12;

            if (avoidMask != 0 && ((avoidMask & static_cast<uint16_t>(1u << (note % 12))) != 0)) {
                clashAvoided = true;
                int nudged = scaleNoteInMode(activeChord.rootPitchClass, activeMode, deg + 1, 60);
                while (nudged > 84) nudged -= 12;
                while (nudged < 60) nudged += 12;
                if ((avoidMask & static_cast<uint16_t>(1u << (nudged % 12))) != 0) {
                    return; // skip clashing fill note cleanly
                }
                note = nudged;
            }

            const float jit = (hashRand01(static_cast<uint32_t>(barIdx), static_cast<uint32_t>(stepInBar), 5u, 10u) - 0.5f) * 10.0f;
            const uint8_t vel = scaleVelocity(86.0f * spaceGate, perf.energy, secEnergyMul, cfg.velocityCurve, jit);
            emitNote(out, sampleOffset, 5, note, vel, art, stepPpq, durPpq);
            recordLockNote(5, barIdx, stepInBar, note, vel, durSteps, art, activeChord.rootPitchClass);
            return;
        }

        // Voice upper chord tones (2 or 3 voices in MIDI 60..84) with clash avoidance
        const VoicingResult voicing = voiceLeadKeys(activeChord, {}, 60, 84, KeysVoicingStyle::Close, avoidMask);
        if (avoidMask != 0) {
            const VoicingResult rawVoicing = voiceLeadKeys(activeChord, {}, 60, 84, KeysVoicingStyle::Close, 0);
            if (rawVoicing.count != voicing.count || rawVoicing.notes != voicing.notes) {
                clashAvoided = true;
            }
        }
        const int numVoices = std::min(voicing.count, isStrings ? 3 : 2);
        const int startV = std::max(0, voicing.count - numVoices);

        for (int i = startV; i < voicing.count; ++i) {
            const float jit = (hashRand01(static_cast<uint32_t>(barIdx), static_cast<uint32_t>(stepInBar), 5u, 10u + static_cast<uint32_t>(i)) - 0.5f) * 10.0f;
            const uint8_t vel = scaleVelocity((isStrings ? 74.0f : 92.0f) * spaceGate,
                                              perf.energy, secEnergyMul, cfg.velocityCurve, jit);
            emitNote(out, sampleOffset, 5, voicing.notes[static_cast<size_t>(i)], vel, art, stepPpq, durPpq);
            recordLockNote(5, barIdx, stepInBar, voicing.notes[static_cast<size_t>(i)], vel, durSteps, art, activeChord.rootPitchClass);
        }
    }

    static void sortEventsBySampleOffset(BlockEventList& list) noexcept {
        for (int i = 1; i < list.count; ++i) {
            const BandNoteEvent key = list.events[static_cast<size_t>(i)];
            int j = i - 1;
            while (j >= 0 && list.events[static_cast<size_t>(j)].sampleOffset > key.sampleOffset) {
                list.events[static_cast<size_t>(j + 1)] = list.events[static_cast<size_t>(j)];
                --j;
            }
            list.events[static_cast<size_t>(j + 1)] = key;
        }
    }
};

} // namespace sessionmate
