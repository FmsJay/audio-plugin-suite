#pragma once

#include "../SessionMateTypes.h"
#include "DrumSynthEngine.h"
#include "InstrumentEngines.h"
#include "SfzSamplePlayer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace sessionmate {

class StudioRoomReverb {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = (sampleRate > 8000.0 && sampleRate < 384000.0) ? sampleRate : 48000.0;
        const float scale = static_cast<float>(sampleRate_ / 44100.0);

        // Warm studio room delay lengths (prime-ish, stereo decorrelated)
        static constexpr int kBaseCombL[4] = { 1116, 1277, 1422, 1617 };
        static constexpr int kBaseCombR[4] = { 1139, 1301, 1447, 1641 };
        static constexpr int kBaseApL[2]   = { 225, 556 };
        static constexpr int kBaseApR[2]   = { 248, 579 };

        for (int i = 0; i < 4; ++i) {
            combLenL_[i] = std::clamp(static_cast<int>(kBaseCombL[i] * scale), 64, kMaxComb - 1);
            combLenR_[i] = std::clamp(static_cast<int>(kBaseCombR[i] * scale), 64, kMaxComb - 1);
        }
        for (int i = 0; i < 2; ++i) {
            apLenL_[i] = std::clamp(static_cast<int>(kBaseApL[i] * scale), 32, kMaxAp - 1);
            apLenR_[i] = std::clamp(static_cast<int>(kBaseApR[i] * scale), 32, kMaxAp - 1);
        }

        reset();
    }

    void reset() noexcept {
        for (auto& b : combBufL_) b.fill(0.0f);
        for (auto& b : combBufR_) b.fill(0.0f);
        for (auto& b : apBufL_) b.fill(0.0f);
        for (auto& b : apBufR_) b.fill(0.0f);
        combIdxL_.fill(0);
        combIdxR_.fill(0);
        combLpL_.fill(0.0f);
        combLpR_.fill(0.0f);
        apIdxL_.fill(0);
        apIdxR_.fill(0);
    }

    void processSample(float inL, float inR, float& outL, float& outR) noexcept {
        // Gentle stereo cross-feed into the room
        const float xL = inL * 0.72f + inR * 0.28f;
        const float xR = inR * 0.72f + inL * 0.28f;

        constexpr float kFeedback = 0.78f;
        constexpr float kDamp = 0.34f;
        constexpr float kDampInv = 1.0f - kDamp;

        float accL = 0.0f;
        float accR = 0.0f;

        for (int i = 0; i < 4; ++i) {
            const size_t si = static_cast<size_t>(i);
            const int idxL = combIdxL_[si];
            const float yL = combBufL_[si][static_cast<size_t>(idxL)];
            combLpL_[si] = flushDenormal(yL * kDampInv + combLpL_[si] * kDamp);
            combBufL_[si][static_cast<size_t>(idxL)] = flushDenormal(xL + combLpL_[si] * kFeedback);
            combIdxL_[si] = (idxL + 1 >= combLenL_[si]) ? 0 : (idxL + 1);
            accL += yL;

            const int idxR = combIdxR_[si];
            const float yR = combBufR_[si][static_cast<size_t>(idxR)];
            combLpR_[si] = flushDenormal(yR * kDampInv + combLpR_[si] * kDamp);
            combBufR_[si][static_cast<size_t>(idxR)] = flushDenormal(xR + combLpR_[si] * kFeedback);
            combIdxR_[si] = (idxR + 1 >= combLenR_[si]) ? 0 : (idxR + 1);
            accR += yR;
        }

        accL *= 0.25f;
        accR *= 0.25f;

        // 2-stage allpass diffusion per channel
        constexpr float kApGain = 0.5f;
        for (int i = 0; i < 2; ++i) {
            const size_t si = static_cast<size_t>(i);
            const int idxL = apIdxL_[si];
            const float bufOutL = apBufL_[si][static_cast<size_t>(idxL)];
            const float vL = flushDenormal(accL + bufOutL * kApGain);
            apBufL_[si][static_cast<size_t>(idxL)] = vL;
            accL = bufOutL - vL * kApGain;
            apIdxL_[si] = (idxL + 1 >= apLenL_[si]) ? 0 : (idxL + 1);

            const int idxR = apIdxR_[si];
            const float bufOutR = apBufR_[si][static_cast<size_t>(idxR)];
            const float vR = flushDenormal(accR + bufOutR * kApGain);
            apBufR_[si][static_cast<size_t>(idxR)] = vR;
            accR = bufOutR - vR * kApGain;
            apIdxR_[si] = (idxR + 1 >= apLenR_[si]) ? 0 : (idxR + 1);
        }

        outL = std::isfinite(accL) ? flushDenormal(accL) : 0.0f;
        outR = std::isfinite(accR) ? flushDenormal(accR) : 0.0f;
    }

private:
    static constexpr int kMaxComb = 8192;
    static constexpr int kMaxAp = 4096;

    static inline float flushDenormal(float x) noexcept {
        return (std::abs(x) < 1.0e-20f) ? 0.0f : x;
    }

    double sampleRate_ = 48000.0;
    std::array<std::array<float, kMaxComb>, 4> combBufL_ {};
    std::array<std::array<float, kMaxComb>, 4> combBufR_ {};
    std::array<int, 4> combLenL_ { 1116, 1277, 1422, 1617 };
    std::array<int, 4> combLenR_ { 1139, 1301, 1447, 1641 };
    std::array<int, 4> combIdxL_ {};
    std::array<int, 4> combIdxR_ {};
    std::array<float, 4> combLpL_ {};
    std::array<float, 4> combLpR_ {};

    std::array<std::array<float, kMaxAp>, 2> apBufL_ {};
    std::array<std::array<float, kMaxAp>, 2> apBufR_ {};
    std::array<int, 2> apLenL_ { 225, 556 };
    std::array<int, 2> apLenR_ { 248, 579 };
    std::array<int, 2> apIdxL_ {};
    std::array<int, 2> apIdxR_ {};
};

class BuiltInBandSynth {
public:
    void prepare(double sampleRate, int /*maxBlockSize*/) noexcept {
        sampleRate_ = (sampleRate > 8000.0 && sampleRate < 384000.0) ? sampleRate : 48000.0;
        // 1-pole tilt pivot at ~950 Hz
        const float pivotHz = 950.0f;
        tiltCoeff_ = 1.0f - std::exp(-6.28318530717958647692f * pivotHz / static_cast<float>(sampleRate_));
        gainSmoothCoeff_ = 1.0f - std::exp(-1.0f / (0.015f * static_cast<float>(sampleRate_)));

        drums_.prepare(sampleRate_);
        bass_.prepare(sampleRate_);
        keys_.prepare(sampleRate_);
        guitar_.prepare(sampleRate_);
        perc_.prepare(sampleRate_);
        horns_.prepare(sampleRate_);
        for (auto& sfz : sfzPlayers_) {
            sfz.prepare(sampleRate_);
        }
        reverb_.prepare(sampleRate_);
        reset();
    }

    void reset() noexcept {
        drums_.reset();
        bass_.reset();
        keys_.reset();
        guitar_.reset();
        perc_.reset();
        horns_.reset();
        for (auto& sfz : sfzPlayers_) {
            sfz.reset();
        }
        reverb_.reset();
        reverbBusL_.fill(0.0f);
        reverbBusR_.fill(0.0f);
        for (auto& s : stripState_) {
            s = {};
        }
        masterGainSmoothed_ = 0.7943282f; // -2 dB default
    }

    void setSlotPatch(int slot, int patchIndex) noexcept {
        switch (slot) {
            case 0: drums_.setPatch(patchIndex); break;
            case 1: bass_.setPatch(patchIndex); break;
            case 2: keys_.setPatch(patchIndex); break;
            case 3: guitar_.setPatch(patchIndex); break;
            case 4: perc_.setPatch(patchIndex); break;
            case 5: horns_.setPatch(patchIndex); break;
            default: break;
        }
    }

    bool loadSfzForSlot(int slot, const std::string& path, std::string& outError) {
        if (slot < 0 || slot >= kNumSlots) {
            outError = "Invalid slot index";
            return false;
        }
        return sfzPlayers_[static_cast<size_t>(slot)].loadFile(path, outError);
    }

    void clearSfzForSlot(int slot) {
        if (slot < 0 || slot >= kNumSlots) return;
        sfzPlayers_[static_cast<size_t>(slot)].clear();
    }

    bool hasSfzLoaded(int slot) const noexcept {
        if (slot < 0 || slot >= kNumSlots) return false;
        return sfzPlayers_[static_cast<size_t>(slot)].isLoaded();
    }

    std::string getSfzName(int slot) const {
        if (slot < 0 || slot >= kNumSlots) return {};
        return sfzPlayers_[static_cast<size_t>(slot)].loadedName();
    }

    void processSlotBlock(int slot, float* left, float* right, int numSamples,
                          const BandNoteEvent* events, int numEvents, bool useSfz) noexcept {
        if (!left || !right || numSamples <= 0) return;
        if (slot < 0 || slot >= kNumSlots) {
            for (int i = 0; i < numSamples; ++i) {
                left[i] = 0.0f;
                right[i] = 0.0f;
            }
            return;
        }

        const bool sfzActive = useSfz && sfzPlayers_[static_cast<size_t>(slot)].isLoaded();
        int evIdx = 0;

        for (int i = 0; i < numSamples; ++i) {
            if (events && numEvents > 0) {
                while (evIdx < numEvents && events[evIdx].sampleOffset <= i) {
                    dispatchEvent(slot, events[evIdx], sfzActive);
                    ++evIdx;
                }
            }

            float sL = 0.0f;
            float sR = 0.0f;
            renderSlotSample(slot, sfzActive, sL, sR);
            left[i] = sL;
            right[i] = sR;
        }

        // Flush any events with sampleOffset >= numSamples at end of block
        if (events && numEvents > 0) {
            while (evIdx < numEvents) {
                dispatchEvent(slot, events[evIdx], sfzActive);
                ++evIdx;
            }
        }
    }

    void applyChannelStripAndMix(int slot, float* slotL, float* slotR,
                                 float* masterL, float* masterR, int numSamples,
                                 const SlotConfig& cfg, bool anySolo,
                                 float& outPeakL, float& outPeakR) noexcept {
        outPeakL = 0.0f;
        outPeakR = 0.0f;
        if (!slotL || !slotR || !masterL || !masterR || numSamples <= 0) return;
        if (slot < 0 || slot >= kNumSlots) return;

        auto& st = stripState_[static_cast<size_t>(slot)];
        const bool audible = !cfg.mute && (!anySolo || cfg.solo);
        const float targetGain = audible
            ? dbToGain(std::clamp(cfg.volumeDb, -60.0f, 12.0f))
            : 0.0f;

        // Tilt EQ: cfg.tiltEq in [-1, +1] -> +/- 6 dB opposite low/high shelf
        const float tilt = std::clamp(cfg.tiltEq, -1.0f, 1.0f);
        const float loGain = dbToGain(-tilt * 6.0f);
        const float hiGain = dbToGain(+tilt * 6.0f);

        // Constant-power stereo balance/pan (center = 1.0, 1.0)
        const float pan = std::clamp(cfg.pan, -1.0f, 1.0f);
        constexpr float kHalfPi = 1.57079632679489661923f;
        const float panAngle = (pan + 1.0f) * 0.5f * kHalfPi;
        const float panL = std::cos(panAngle) * 1.41421356f;
        const float panR = std::sin(panAngle) * 1.41421356f;

        const float revSend = std::clamp(cfg.reverbSend, 0.0f, 1.0f);
        const int revLimit = std::min(numSamples, kMaxReverbBlock);

        float peakL = 0.0f;
        float peakR = 0.0f;

        for (int i = 0; i < numSamples; ++i) {
            st.gainSmoothed += (targetGain - st.gainSmoothed) * gainSmoothCoeff_;
            if (std::abs(st.gainSmoothed) < 1.0e-20f) st.gainSmoothed = 0.0f;

            float xL = std::isfinite(slotL[i]) ? slotL[i] : 0.0f;
            float xR = std::isfinite(slotR[i]) ? slotR[i] : 0.0f;

            // 1-pole tilt EQ
            st.tiltLpL += (xL - st.tiltLpL) * tiltCoeff_;
            st.tiltLpR += (xR - st.tiltLpR) * tiltCoeff_;
            if (std::abs(st.tiltLpL) < 1.0e-20f) st.tiltLpL = 0.0f;
            if (std::abs(st.tiltLpR) < 1.0e-20f) st.tiltLpR = 0.0f;

            const float hpL = xL - st.tiltLpL;
            const float hpR = xR - st.tiltLpR;
            const float eqL = st.tiltLpL * loGain + hpL * hiGain;
            const float eqR = st.tiltLpR * loGain + hpR * hiGain;

            const float outL = eqL * st.gainSmoothed * panL;
            const float outR = eqR * st.gainSmoothed * panR;

            slotL[i] = outL;
            slotR[i] = outR;

            const float aL = std::abs(outL);
            const float aR = std::abs(outR);
            if (aL > peakL) peakL = aL;
            if (aR > peakR) peakR = aR;

            masterL[i] += outL;
            masterR[i] += outR;

            if (i < revLimit && revSend > 0.0001f) {
                reverbBusL_[static_cast<size_t>(i)] += outL * revSend;
                reverbBusR_[static_cast<size_t>(i)] += outR * revSend;
            }
        }

        outPeakL = std::clamp(peakL, 0.0f, 2.0f);
        outPeakR = std::clamp(peakR, 0.0f, 2.0f);
    }

    void finalizeMasterReverbAndLimit(float* masterL, float* masterR, int numSamples,
                                      float masterVolDb,
                                      float& outMasterPeakL, float& outMasterPeakR) noexcept {
        outMasterPeakL = 0.0f;
        outMasterPeakR = 0.0f;
        if (!masterL || !masterR || numSamples <= 0) return;

        const float targetMasterGain = dbToGain(std::clamp(masterVolDb, -60.0f, 12.0f));
        const int revLimit = std::min(numSamples, kMaxReverbBlock);

        float peakL = 0.0f;
        float peakR = 0.0f;

        for (int i = 0; i < numSamples; ++i) {
            masterGainSmoothed_ += (targetMasterGain - masterGainSmoothed_) * gainSmoothCoeff_;
            if (std::abs(masterGainSmoothed_) < 1.0e-20f) masterGainSmoothed_ = 0.0f;

            float wetL = 0.0f;
            float wetR = 0.0f;
            if (i < revLimit) {
                const size_t si = static_cast<size_t>(i);
                reverb_.processSample(reverbBusL_[si], reverbBusR_[si], wetL, wetR);
                reverbBusL_[si] = 0.0f;
                reverbBusR_[si] = 0.0f;
            }

            float mL = (std::isfinite(masterL[i]) ? masterL[i] : 0.0f) + wetL * 0.42f;
            float mR = (std::isfinite(masterR[i]) ? masterR[i] : 0.0f) + wetR * 0.42f;

            mL *= masterGainSmoothed_;
            mR *= masterGainSmoothed_;

            // Musical transparent soft-knee limiter (linear up to 0.75, soft clip ceiling ~0.98)
            mL = softLimit(mL);
            mR = softLimit(mR);

            masterL[i] = mL;
            masterR[i] = mR;

            const float aL = std::abs(mL);
            const float aR = std::abs(mR);
            if (aL > peakL) peakL = aL;
            if (aR > peakR) peakR = aR;
        }

        outMasterPeakL = std::clamp(peakL, 0.0f, 1.0f);
        outMasterPeakR = std::clamp(peakR, 0.0f, 1.0f);
    }

private:
    static constexpr int kMaxReverbBlock = 8192;

    struct SlotStripState {
        float gainSmoothed = 1.0f;
        float tiltLpL = 0.0f;
        float tiltLpR = 0.0f;
    };

    static inline float dbToGain(float db) noexcept {
        if (db <= -59.9f) return 0.0f;
        return std::pow(10.0f, db * 0.05f);
    }

    static inline float softLimit(float x) noexcept {
        if (!std::isfinite(x)) return 0.0f;
        if (std::abs(x) < 1.0e-20f) return 0.0f;
        constexpr float kKnee = 0.75f;
        constexpr float kCeiling = 0.98f;
        const float ax = std::abs(x);
        if (ax <= kKnee) return x;
        const float over = ax - kKnee;
        const float span = kCeiling - kKnee;
        const float shaped = kKnee + span * std::tanh(over / span);
        return (x < 0.0f) ? -shaped : shaped;
    }

    void dispatchEvent(int slot, const BandNoteEvent& ev, bool useSfz) noexcept {
        if (useSfz) {
            sfzPlayers_[static_cast<size_t>(slot)].handleEvent(ev);
            return;
        }
        switch (slot) {
            case 0: drums_.handleEvent(ev); break;
            case 1: bass_.handleEvent(ev); break;
            case 2: keys_.handleEvent(ev); break;
            case 3: guitar_.handleEvent(ev); break;
            case 4: perc_.handleEvent(ev); break;
            case 5: horns_.handleEvent(ev); break;
            default: break;
        }
    }

    void renderSlotSample(int slot, bool useSfz, float& outL, float& outR) noexcept {
        if (useSfz) {
            sfzPlayers_[static_cast<size_t>(slot)].renderSample(outL, outR);
            return;
        }
        switch (slot) {
            case 0: drums_.renderSample(outL, outR); break;
            case 1: bass_.renderSample(outL, outR); break;
            case 2: keys_.renderSample(outL, outR); break;
            case 3: guitar_.renderSample(outL, outR); break;
            case 4: perc_.renderSample(outL, outR); break;
            case 5: horns_.renderSample(outL, outR); break;
            default: outL = 0.0f; outR = 0.0f; break;
        }
    }

    double sampleRate_ = 48000.0;
    float tiltCoeff_ = 0.12f;
    float gainSmoothCoeff_ = 0.0014f;
    float masterGainSmoothed_ = 0.7943282f;

    DrumSynthEngine drums_ {};
    BassSynthEngine bass_ {};
    KeysSynthEngine keys_ {};
    GuitarSynthEngine guitar_ {};
    PercSynthEngine perc_ {};
    HornsStringsEngine horns_ {};
    std::array<SfzSamplePlayer, kNumSlots> sfzPlayers_ {};

    StudioRoomReverb reverb_ {};
    std::array<float, kMaxReverbBlock> reverbBusL_ {};
    std::array<float, kMaxReverbBlock> reverbBusR_ {};
    std::array<SlotStripState, kNumSlots> stripState_ {};
};

} // namespace sessionmate
