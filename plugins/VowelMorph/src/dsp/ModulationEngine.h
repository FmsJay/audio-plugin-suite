#pragma once

#include "PhoneticTables.h"
#include "SuiteDSP.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace vowelmorph::dsp {

class ModulationEngine {
public:
    static constexpr int kStepRestValue = 10;

    struct ExternalLinkInputs {
        bool     fossilAlive            = false;
        float    fossilTransient        = 0.0f;
        float    fossilTonal            = 0.0f;
        float    fossilNoise            = 0.0f;
        float    fossilGateEnv          = 0.0f;
        uint32_t fossilEuclidPulseSeq   = 0;
        uint8_t  fossilEuclidStep       = 0;
        uint32_t fossilFreezeCaptureSeq = 0;
        bool     linkEuclidSync         = false;
        bool     linkNoiseBreath        = false;
        bool     linkTransientJaw       = false;
        bool     linkFreezeSweep        = false;
        bool     harmonyAlive           = false;
        uint8_t  harmonySection         = 0;
        bool     harmonySectionLift     = false;
    };

    struct ModConfig {
        int   modMode        = 0;
        float modDepth       = 0.5f;   // -1..+1
        float baseX          = 0.5f;
        float baseY          = 0.5f;
        float baseJaw        = 0.6f;
        float baseThroat     = 0.0f;   // -0.5..+0.5
        float envAttackMs    = 8.0f;
        float envReleaseMs   = 140.0f;
        float lfoRateHz      = 1.2f;
        bool  lfoSync        = true;
        int   lfoDiv         = 6;
        int   lfoShape       = 0;
        int   seqRate        = 1;
        float seqGlide01     = 0.45f;
        float consonantDip01 = 0.32f;
        std::array<int, 8>  seqSteps   { 7, 4, 0, 10, 6, 4, 9, 10 };
        std::array<bool, 8> seqAccents { true, false, true, false, true, false, false, false };
        double bpm           = 120.0;
        double ppqPosition   = 0.0;
        bool   hostPlaying   = false;
    };

    struct ModOutput {
        float    vowelX        = 0.5f;
        float    vowelY        = 0.5f;
        float    jawOpen       = 0.6f;
        float    throatSize    = 0.0f;
        float    consonantDip  = 0.0f;
        float    breathBoost   = 0.0f;
        float    modEnvelope   = 0.0f;
        float    inputEnv      = 0.0f;
        int      activeStep    = 0;
        uint32_t stepChangeSeq = 0;
        bool     euclidLocked  = false;
    };

    void prepare(double sampleRate) noexcept {
        sampleRate_ = std::max(8000.0, std::isfinite(sampleRate) ? sampleRate : 48000.0);
        reset();
    }

    void reset() noexcept {
        env_                  = 0.0f;
        lfoPhase_             = 0.0;
        shValX_               = 0.0f;
        shValY_               = 0.0f;
        rngState_             = 0xA511E9B3u;

        seqPhaseBeats_        = 0.0;
        activeStep_           = 0;
        stepChangeSeq_        = 0;
        glideInitialized_     = false;
        glideX_               = 0.5f;
        glideY_               = 0.5f;
        glideJaw_             = 0.6f;

        dipActive_            = false;
        dipTimerSec_          = 0.0f;

        euclidInit_           = false;
        lastEuclidPulseSeq_   = 0;
        freezeInit_           = false;
        lastFreezeCaptureSeq_ = 0;
        freezeSweepActive_    = false;
        freezeSweepSec_       = 0.0f;

        midiActive_           = false;
        midiLastNote_         = -1;
        midiPhoneme_          = 4;
        midiVelocity_         = 0.0f;
        midiThroatShift_      = 0.0f;
    }

    void handleMidiNoteOn(int noteNumber, float velocity) noexcept {
        // Map C3 (48)..A3 (57) directly to phonemes 0..9; wrap modulo 10 elsewhere
        const int rel = noteNumber - 48;
        const int phoneme = ((rel % 10) + 10) % 10;

        midiPhoneme_     = phoneme;
        midiLastNote_    = noteNumber;
        midiVelocity_    = std::clamp(velocity, 0.0f, 1.0f);
        midiActive_      = true;

        // Higher notes -> smaller throat (-), lower notes -> larger throat (+)
        const float octShift = static_cast<float>(noteNumber - 53) / 24.0f;
        midiThroatShift_ = std::clamp(octShift, -0.35f, 0.35f);

        triggerConsonantDip();
    }

    void handleMidiNoteOff(int noteNumber) noexcept {
        if (noteNumber == midiLastNote_) {
            midiActive_ = false;
        }
    }

    ModOutput processBlock(int numSamples,
                           float blockPeakIn,
                           const ModConfig& cfg,
                           const ExternalLinkInputs& ext) noexcept {
        const double dtSec = static_cast<double>(std::max(1, numSamples)) / sampleRate_;
        const double bpm = std::clamp(std::isfinite(cfg.bpm) ? cfg.bpm : 120.0, 30.0, 300.0);
        const float depth = suite::dsp::clampFinite(cfg.modDepth, -1.0f, 1.0f);
        const float absDepth = std::abs(depth);

        // 1. Block-accurate envelope follower
        const float peakIn = suite::dsp::clampFinite(std::abs(blockPeakIn), 0.0f, 4.0f);
        const double attSec = std::max(0.001, static_cast<double>(std::clamp(cfg.envAttackMs, 1.0f, 150.0f)) * 0.001);
        const double relSec = std::max(0.005, static_cast<double>(std::clamp(cfg.envReleaseMs, 15.0f, 800.0f)) * 0.001);
        const float envCoeff = static_cast<float>(std::exp(-dtSec / ((peakIn > env_) ? attSec : relSec)));
        env_ = suite::dsp::flushDenormal(peakIn + envCoeff * (env_ - peakIn));

        float outX      = suite::dsp::clampFinite(cfg.baseX, 0.0f, 1.0f);
        float outY      = suite::dsp::clampFinite(cfg.baseY, 0.0f, 1.0f);
        float outJaw    = suite::dsp::clampFinite(cfg.baseJaw, 0.0f, 1.0f);
        float outThroat = suite::dsp::clampFinite(cfg.baseThroat, -0.5f, 0.5f);
        float modVal    = 0.0f;
        float restDip   = 0.0f;
        bool  euclidLocked = false;

        if (!glideInitialized_) {
            glideX_           = outX;
            glideY_           = outY;
            glideJaw_         = outJaw;
            glideInitialized_ = true;
        }

        // 2. Modulation mode dispatch
        const int mode = std::clamp(cfg.modMode, 0, 3);
        if (mode == 0) {
            // Mode 0: Envelope Follower
            const float envScaled = std::clamp(env_ * 2.2f, 0.0f, 1.0f);
            modVal = envScaled * depth;
            outX   += -0.42f * modVal;
            outY   += -0.36f * modVal;
            outJaw +=  0.35f * modVal;
        } else if (mode == 1) {
            // Mode 1: LFO (Sine, Triangle, Saw, S&H; free or tempo-synced)
            const double beatsPerCycle = beatsPerStepForRate(cfg.lfoDiv);
            const double lfoHz = cfg.lfoSync
                ? ((bpm / 60.0) / beatsPerCycle)
                : static_cast<double>(std::clamp(cfg.lfoRateHz, 0.05f, 16.0f));

            const double prevPhase = lfoPhase_;
            if (cfg.lfoSync && cfg.hostPlaying && std::isfinite(cfg.ppqPosition)) {
                const double cycles = std::max(0.0, cfg.ppqPosition) / beatsPerCycle;
                lfoPhase_ = cycles - std::floor(cycles);
            } else {
                lfoPhase_ += lfoHz * dtSec;
                lfoPhase_ -= std::floor(lfoPhase_);
            }

            if (lfoPhase_ < prevPhase) {
                shValX_ = nextBipolarNoise();
                shValY_ = nextBipolarNoise();
            }

            float lfoX = 0.0f;
            float lfoY = 0.0f;
            const double p = lfoPhase_;
            const double pQuad = p + 0.25 - std::floor(p + 0.25);

            switch (std::clamp(cfg.lfoShape, 0, 3)) {
                case 0: // Sine (smooth 2D orbit)
                    lfoX = static_cast<float>(std::sin(suite::dsp::kTwoPi * p));
                    lfoY = static_cast<float>(std::cos(suite::dsp::kTwoPi * p));
                    break;
                case 1: // Triangle
                    lfoX = static_cast<float>(1.0 - 4.0 * std::abs(p - 0.5));
                    lfoY = static_cast<float>(1.0 - 4.0 * std::abs(pQuad - 0.5));
                    break;
                case 2: // Saw
                    lfoX = static_cast<float>(2.0 * p - 1.0);
                    lfoY = static_cast<float>(1.0 - 2.0 * p);
                    break;
                case 3: // Sample & Hold
                    lfoX = shValX_;
                    lfoY = shValY_;
                    break;
                default:
                    break;
            }

            modVal = lfoX * depth;
            outX   += 0.38f * depth * lfoX;
            outY   += 0.34f * depth * lfoY;
            outJaw += 0.18f * depth * lfoX;
        } else if (mode == 2) {
            // Mode 2: 8-Step Tempo-Synced / Fossil-Euclidean Syllable Sequencer
            const bool useEuclid = ext.fossilAlive && ext.linkEuclidSync;
            euclidLocked = useEuclid;

            if (useEuclid) {
                if (!euclidInit_) {
                    lastEuclidPulseSeq_ = ext.fossilEuclidPulseSeq;
                    euclidInit_ = true;
                } else if (ext.fossilEuclidPulseSeq != lastEuclidPulseSeq_) {
                    lastEuclidPulseSeq_ = ext.fossilEuclidPulseSeq;
                    activeStep_ = (activeStep_ + 1) & 7;
                    ++stepChangeSeq_;
                    triggerConsonantDip();
                }
            } else {
                euclidInit_ = false;
                const double beatsPerStep = beatsPerStepForRate(cfg.seqRate);
                int newStep = activeStep_;
                if (cfg.hostPlaying && std::isfinite(cfg.ppqPosition)) {
                    const double pos = std::max(0.0, cfg.ppqPosition);
                    newStep = static_cast<int>(std::floor(pos / beatsPerStep)) & 7;
                } else {
                    seqPhaseBeats_ += (bpm / 60.0) * dtSec;
                    if (seqPhaseBeats_ >= 64.0) {
                        seqPhaseBeats_ = std::fmod(seqPhaseBeats_, 64.0);
                    }
                    newStep = static_cast<int>(std::floor(seqPhaseBeats_ / beatsPerStep)) & 7;
                }
                if (newStep != activeStep_) {
                    activeStep_ = newStep;
                    ++stepChangeSeq_;
                    triggerConsonantDip();
                }
            }

            const size_t stepIdx = static_cast<size_t>(activeStep_ & 7);
            const int stepVal = cfg.seqSteps[stepIdx];
            const bool accent = cfg.seqAccents[stepIdx];

            float targetX = outX;
            float targetY = outY;
            float targetJaw = outJaw;

            if (stepVal == kStepRestValue) {
                const auto [mmX, mmY] = getPhonemeXY(9);
                targetX   = outX + absDepth * (mmX - outX);
                targetY   = outY + absDepth * (mmY - outY);
                targetJaw = outJaw * (1.0f - 0.65f * absDepth);
                restDip   = 0.45f * absDepth;
            } else {
                const auto [px, py] = getPhonemeXY(std::clamp(stepVal, 0, 9));
                targetX = outX + absDepth * (px - outX);
                targetY = outY + absDepth * (py - outY);
                if (accent) {
                    targetJaw += 0.20f * absDepth;
                }
            }

            // Exponential glide (5 ms .. 220 ms)
            const double glideSec = 0.005 + 0.215 * static_cast<double>(std::clamp(cfg.seqGlide01, 0.0f, 1.0f));
            const float alpha = static_cast<float>(1.0 - std::exp(-dtSec / glideSec));
            glideX_   = suite::dsp::flushDenormal(glideX_   + alpha * (targetX   - glideX_));
            glideY_   = suite::dsp::flushDenormal(glideY_   + alpha * (targetY   - glideY_));
            glideJaw_ = suite::dsp::flushDenormal(glideJaw_ + alpha * (targetJaw - glideJaw_));

            outX   = glideX_;
            outY   = glideY_;
            outJaw = glideJaw_;
            modVal = accent ? absDepth : (stepVal == kStepRestValue ? -0.5f * absDepth : 0.4f * absDepth);
        } else {
            // Mode 3: MIDI Trigger
            const auto [px, py] = getPhonemeXY(midiPhoneme_);
            const float targetX = outX + absDepth * (px - outX);
            const float targetY = outY + absDepth * (py - outY);
            const float velJaw  = midiActive_ ? (0.25f * midiVelocity_ * absDepth) : 0.0f;
            const float targetJaw = outJaw + velJaw;

            const double glideSec = 0.006 + 0.140 * static_cast<double>(std::clamp(cfg.seqGlide01, 0.0f, 1.0f));
            const float alpha = static_cast<float>(1.0 - std::exp(-dtSec / glideSec));
            glideX_   = suite::dsp::flushDenormal(glideX_   + alpha * (targetX   - glideX_));
            glideY_   = suite::dsp::flushDenormal(glideY_   + alpha * (targetY   - glideY_));
            glideJaw_ = suite::dsp::flushDenormal(glideJaw_ + alpha * (targetJaw - glideJaw_));

            outX      = glideX_;
            outY      = glideY_;
            outJaw    = glideJaw_;
            outThroat -= midiThroatShift_ * absDepth;
            modVal    = midiActive_ ? (midiVelocity_ * absDepth) : 0.0f;
        }

        // 3. Consonant Closure Dip envelope (18..55 ms smooth raised-sine pulse)
        float dipEnv = 0.0f;
        const float dipCfg = std::clamp(cfg.consonantDip01, 0.0f, 1.0f);
        if (dipActive_) {
            const float dipDurSec = 0.018f + 0.037f * dipCfg;
            dipTimerSec_ += static_cast<float>(dtSec);
            if (dipTimerSec_ >= dipDurSec) {
                dipActive_ = false;
                dipTimerSec_ = 0.0f;
            } else {
                const float u = dipTimerSec_ / dipDurSec;
                dipEnv = std::sin(suite::dsp::kPiF * u) * dipCfg;
            }
        }

        // 4. Fossil Freeze Sweep (~700 ms expressive OO -> AH -> EE bloom)
        if (ext.fossilAlive && ext.linkFreezeSweep) {
            if (!freezeInit_) {
                lastFreezeCaptureSeq_ = ext.fossilFreezeCaptureSeq;
                freezeInit_ = true;
            } else if (ext.fossilFreezeCaptureSeq != lastFreezeCaptureSeq_) {
                lastFreezeCaptureSeq_ = ext.fossilFreezeCaptureSeq;
                freezeSweepActive_ = true;
                freezeSweepSec_ = 0.0f;
            }
        } else {
            freezeInit_ = false;
        }

        if (freezeSweepActive_) {
            constexpr float kFreezeDurSec = 0.70f;
            freezeSweepSec_ += static_cast<float>(dtSec);
            if (freezeSweepSec_ >= kFreezeDurSec) {
                freezeSweepActive_ = false;
                freezeSweepSec_ = 0.0f;
            } else {
                const float u = freezeSweepSec_ / kFreezeDurSec;
                const auto [ooX, ooY] = getPhonemeXY(7); // OO
                const auto [ahX, ahY] = getPhonemeXY(4); // AH
                const auto [eeX, eeY] = getPhonemeXY(0); // EE

                float sweepX = ahX;
                float sweepY = ahY;
                if (u < 0.5f) {
                    const float t = u * 2.0f;
                    sweepX = ooX + t * (ahX - ooX);
                    sweepY = ooY + t * (ahY - ooY);
                } else {
                    const float t = (u - 0.5f) * 2.0f;
                    sweepX = ahX + t * (eeX - ahX);
                    sweepY = ahY + t * (eeY - ahY);
                }

                const float blend = std::sin(suite::dsp::kPiF * u) * 0.78f;
                outX   += blend * (sweepX - outX);
                outY   += blend * (sweepY - outY);
                outJaw += blend * 0.18f;
            }
        }

        // 5. Fossil Transient -> Jaw articulation & Noise -> Breath
        float breathBoost = 0.0f;
        if (ext.fossilAlive) {
            if (ext.linkTransientJaw) {
                outJaw += std::clamp(ext.fossilTransient, 0.0f, 1.0f) * 0.28f;
            }
            if (ext.linkNoiseBreath) {
                breathBoost = std::clamp(ext.fossilNoise, 0.0f, 1.0f);
            }
        }

        // 6. HarmonyLink Section Lift (Chorus/Solo >= 2 lifts jaw +0.10)
        if (ext.harmonyAlive && ext.harmonySectionLift && ext.harmonySection >= 2) {
            outJaw += 0.10f;
        }

        ModOutput out;
        out.vowelX        = suite::dsp::clampFinite(outX, 0.0f, 1.0f);
        out.vowelY        = suite::dsp::clampFinite(outY, 0.0f, 1.0f);
        out.jawOpen       = suite::dsp::clampFinite(outJaw, 0.0f, 1.0f);
        out.throatSize    = suite::dsp::clampFinite(outThroat, -0.5f, 0.5f);
        out.consonantDip  = suite::dsp::clampFinite(dipEnv + restDip, 0.0f, 1.0f);
        out.breathBoost   = suite::dsp::clampFinite(breathBoost, 0.0f, 1.0f);
        out.modEnvelope   = suite::dsp::clampFinite(modVal, -1.0f, 1.0f);
        out.inputEnv      = suite::dsp::clampFinite(env_, 0.0f, 1.0f);
        out.activeStep    = activeStep_;
        out.stepChangeSeq = stepChangeSeq_;
        out.euclidLocked  = euclidLocked;
        return out;
    }

private:
    static constexpr double beatsPerStepForRate(int idx) noexcept {
        constexpr double kBeats[7] = {
            1.0,        // 1/4
            0.5,        // 1/8
            1.0 / 3.0,  // 1/8T
            0.25,       // 1/16
            1.0 / 6.0,  // 1/16T
            0.125,      // 1/32
            2.0         // 1/2
        };
        if (idx < 0 || idx >= 7) return 0.5;
        return kBeats[idx];
    }

    void triggerConsonantDip() noexcept {
        dipActive_   = true;
        dipTimerSec_ = 0.0f;
    }

    float nextBipolarNoise() noexcept {
        uint32_t x = rngState_;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        rngState_ = x;
        return static_cast<float>(static_cast<int32_t>(x)) * 4.6566128730773926e-10f;
    }

    double   sampleRate_           = 48000.0;
    float    env_                  = 0.0f;
    double   lfoPhase_             = 0.0;
    float    shValX_               = 0.0f;
    float    shValY_               = 0.0f;
    uint32_t rngState_             = 0xA511E9B3u;

    double   seqPhaseBeats_        = 0.0;
    int      activeStep_           = 0;
    uint32_t stepChangeSeq_        = 0;
    bool     glideInitialized_     = false;
    float    glideX_               = 0.5f;
    float    glideY_               = 0.5f;
    float    glideJaw_             = 0.6f;

    bool     dipActive_            = false;
    float    dipTimerSec_          = 0.0f;

    bool     euclidInit_           = false;
    uint32_t lastEuclidPulseSeq_   = 0;
    bool     freezeInit_           = false;
    uint32_t lastFreezeCaptureSeq_ = 0;
    bool     freezeSweepActive_    = false;
    float    freezeSweepSec_       = 0.0f;

    bool     midiActive_           = false;
    int      midiLastNote_         = -1;
    int      midiPhoneme_          = 4;
    float    midiVelocity_         = 0.0f;
    float    midiThroatShift_      = 0.0f;
};

} // namespace vowelmorph::dsp
