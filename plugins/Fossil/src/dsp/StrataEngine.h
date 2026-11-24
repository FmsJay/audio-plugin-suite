#pragma once

#include "../FossilParams.h"
#include "EuclideanGate.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace fossil::dsp
{

class StrataEngine
{
public:
    static constexpr int   kNumStreams = 3;
    static constexpr int   kNumGrains  = 4;
    static constexpr float kPi         = 3.14159265358979323846f;
    static constexpr float kTwoPi      = 6.28318530717958647692f;
    static constexpr float kHalfPi     = 1.57079632679489661923f;
    static constexpr float kSqrt2      = 1.41421356237309504880f;

    StrataEngine() = default;

    void prepare (double sampleRate, int /*maxBlockSize*/)
    {
        sampleRate_ = (std::isfinite (sampleRate) && sampleRate >= 8000.0) ? sampleRate : 44100.0;
        historyLen_ = std::max (1024, static_cast<int> (std::llround (sampleRate_ * 2.0)));
        captureLen_ = std::max (512, std::min (historyLen_, static_cast<int> (std::llround (sampleRate_ * 1.5))));

        for (auto& h : history_)
        {
            h.bufL.assign (static_cast<size_t> (historyLen_), 0.0f);
            h.bufR.assign (static_cast<size_t> (historyLen_), 0.0f);
            h.writeIdx = 0;
        }

        // Time constants
        fadeStep_       = 1.0f / std::max (1.0f, static_cast<float> (sampleRate_ * 0.015)); // ~15 ms linear crossfade
        gainSmoothCoeff_ = static_cast<float> (1.0 - std::exp (-1.0 / std::max (1.0, sampleRate_ * 0.010))); // ~10 ms
        tailDecay_      = static_cast<float> (std::exp (-1.0 / std::max (1.0, sampleRate_ * 0.004))); // ~4 ms click-guard
        tiltLpCoeff_    = static_cast<float> (1.0 - std::exp (-static_cast<double> (kTwoPi) * 900.0 / sampleRate_));
        tiltLpCoeff_    = std::clamp (tiltLpCoeff_, 0.001f, 0.99f);

        for (int s = 0; s < kNumStrata; ++s)
        {
            auto& st = strata_[static_cast<size_t> (s)];
            st.snapL.assign (static_cast<size_t> (historyLen_), 0.0f);
            st.snapR.assign (static_cast<size_t> (historyLen_), 0.0f);
            st.validLength = captureLen_;
            st.rngState    = 0x9E3779B9u ^ (static_cast<uint32_t> (s + 1) * 0x85EBCA6Bu);
            if (st.rngState == 0u)
                st.rngState = 0x12345678u;
        }

        reset();
    }

    void reset() noexcept
    {
        for (auto& h : history_)
        {
            std::fill (h.bufL.begin(), h.bufL.end(), 0.0f);
            std::fill (h.bufR.begin(), h.bufR.end(), 0.0f);
            h.writeIdx = 0;
        }

        for (int s = 0; s < kNumStrata; ++s)
        {
            auto& st = strata_[static_cast<size_t> (s)];
            std::fill (st.snapL.begin(), st.snapL.end(), 0.0f);
            std::fill (st.snapR.begin(), st.snapR.end(), 0.0f);
            st.validLength  = captureLen_;
            st.frozen       = false;
            st.clearing     = false;
            st.source       = StreamSource::Tonal;
            st.fadeEnv      = 0.0f;
            st.fadeTarget   = 0.0f;
            st.smoothedGain = 0.0f;
            st.lpStateL     = 0.0f;
            st.lpStateR     = 0.0f;
            st.lastOutL     = 0.0f;
            st.lastOutR     = 0.0f;
            st.tailL        = 0.0f;
            st.tailR        = 0.0f;
            st.scanPos      = 0.0f;
            resetGrainHeads (st, 0.0f);
        }

        roundRobinSlot_ = 0;
    }

    int captureStream (int targetSlot, StreamSource source, FossilTelemetry* telemetry = nullptr) noexcept
    {
        if (historyLen_ <= 0 || captureLen_ <= 0)
            return 0;

        int slot = targetSlot;
        if (slot < 0 || slot >= kNumStrata)
        {
            slot = -1;
            for (int i = 0; i < kNumStrata; ++i)
            {
                if (! isSlotFrozen (i))
                {
                    slot = i;
                    break;
                }
            }
            if (slot < 0)
            {
                slot = roundRobinSlot_;
                roundRobinSlot_ = (roundRobinSlot_ + 1) % kNumStrata;
            }
            else
            {
                roundRobinSlot_ = (slot + 1) % kNumStrata;
            }
        }
        else
        {
            roundRobinSlot_ = (slot + 1) % kNumStrata;
        }

        const int streamIdx = std::clamp (static_cast<int> (source), 0, kNumStreams - 1);
        const auto& hist = history_[static_cast<size_t> (streamIdx)];
        auto& st = strata_[static_cast<size_t> (slot)];

        // Preserve continuity if overwriting an active slot.
        st.tailL = st.lastOutL;
        st.tailR = st.lastOutR;

        const int len = std::min (captureLen_, static_cast<int> (st.snapL.size()));
        int readIdx = hist.writeIdx - len;
        if (readIdx < 0)
            readIdx += historyLen_;

        for (int i = 0; i < len; ++i)
        {
            st.snapL[static_cast<size_t> (i)] = hist.bufL[static_cast<size_t> (readIdx)];
            st.snapR[static_cast<size_t> (i)] = hist.bufR[static_cast<size_t> (readIdx)];
            if (++readIdx >= historyLen_)
                readIdx = 0;
        }

        // Trim leading silence when capturing short phrases so frozen playback starts immediately in active audio.
        int firstActive = 0;
        const int minKeep = std::min (len, std::max (2048, static_cast<int> (sampleRate_ * 0.12)));
        while (firstActive < len - minKeep)
        {
            const float a = std::max (std::abs (st.snapL[static_cast<size_t> (firstActive)]),
                                      std::abs (st.snapR[static_cast<size_t> (firstActive)]));
            if (a > 1.0e-4f)
                break;
            ++firstActive;
        }
        int effectiveLen = len;
        if (firstActive > 256)
        {
            effectiveLen = len - firstActive;
            for (int i = 0; i < effectiveLen; ++i)
            {
                st.snapL[static_cast<size_t> (i)] = st.snapL[static_cast<size_t> (i + firstActive)];
                st.snapR[static_cast<size_t> (i)] = st.snapR[static_cast<size_t> (i + firstActive)];
            }
        }

        applyEdgeTaper (st.snapL, st.snapR, effectiveLen);

        st.validLength = effectiveLen;
        st.source      = source;
        st.frozen      = true;
        st.clearing    = false;
        st.fadeEnv     = 0.0f;
        st.fadeTarget  = 1.0f;
        st.scanPos     = findActiveCenter (st.snapL, st.snapR, effectiveLen);
        resetGrainHeads (st, st.scanPos);

        if (telemetry != nullptr)
        {
            telemetry->lastCapturedSlot.store (slot, std::memory_order_relaxed);
            publishStratumTelemetry (slot, st, telemetry);
        }

        return slot;
    }

    void clearSlot (int slot, FossilTelemetry* telemetry = nullptr) noexcept
    {
        if (slot < 0 || slot >= kNumStrata)
            return;

        auto& st = strata_[static_cast<size_t> (slot)];
        st.clearing   = true;
        st.fadeTarget = 0.0f;
        if (! st.frozen && st.fadeEnv <= 1.0e-5f)
        {
            st.fadeEnv  = 0.0f;
            st.clearing = false;
        }

        if (telemetry != nullptr)
        {
            auto& t = telemetry->strata[static_cast<size_t> (slot)];
            t.frozen.store (false, std::memory_order_relaxed);
            t.outputLevel.store (0.0f, std::memory_order_relaxed);
            for (auto& bin : t.wavePreview)
                bin.store (0.0f, std::memory_order_relaxed);
        }
    }

    void clearAll (FossilTelemetry* telemetry = nullptr) noexcept
    {
        for (int i = 0; i < kNumStrata; ++i)
            clearSlot (i, telemetry);
    }

    void swapSlots (int slotA, int slotB, FossilTelemetry* telemetry = nullptr) noexcept
    {
        if (slotA < 0 || slotA >= kNumStrata || slotB < 0 || slotB >= kNumStrata || slotA == slotB)
            return;

        // std::vector::swap is O(1) pointer swap, zero heap allocation.
        std::swap (strata_[static_cast<size_t> (slotA)], strata_[static_cast<size_t> (slotB)]);

        if (telemetry != nullptr)
        {
            publishStratumTelemetry (slotA, strata_[static_cast<size_t> (slotA)], telemetry);
            publishStratumTelemetry (slotB, strata_[static_cast<size_t> (slotB)], telemetry);
        }
    }

    bool isSlotFrozen (int slot) const noexcept
    {
        if (slot < 0 || slot >= kNumStrata)
            return false;
        const auto& st = strata_[static_cast<size_t> (slot)];
        return st.frozen && ! st.clearing;
    }

    int getSnapshotLength() const noexcept { return captureLen_; }

    bool copySnapshotOut (int slot, std::vector<float>& outL, std::vector<float>& outR, StreamSource& outSource) const
    {
        if (slot < 0 || slot >= kNumStrata || ! isSlotFrozen (slot))
            return false;

        const auto& st = strata_[static_cast<size_t> (slot)];
        const int len = std::min (st.validLength, static_cast<int> (st.snapL.size()));
        if (len <= 0)
            return false;

        outL.assign (st.snapL.begin(), st.snapL.begin() + len);
        outR.assign (st.snapR.begin(), st.snapR.begin() + len);
        outSource = st.source;
        return true;
    }

    void restoreSnapshot (int slot,
                          const std::vector<float>& inL,
                          const std::vector<float>& inR,
                          StreamSource source,
                          FossilTelemetry* telemetry = nullptr)
    {
        if (slot < 0 || slot >= kNumStrata)
            return;

        if (inL.empty() || inR.empty() || historyLen_ <= 0)
        {
            clearSlot (slot, telemetry);
            return;
        }

        auto& st = strata_[static_cast<size_t> (slot)];
        const int maxCap = static_cast<int> (st.snapL.size());
        const int len    = std::min ({ static_cast<int> (inL.size()), static_cast<int> (inR.size()), maxCap });
        if (len < 16)
        {
            clearSlot (slot, telemetry);
            return;
        }

        for (int i = 0; i < len; ++i)
        {
            st.snapL[static_cast<size_t> (i)] = sanitize (inL[static_cast<size_t> (i)]);
            st.snapR[static_cast<size_t> (i)] = sanitize (inR[static_cast<size_t> (i)]);
        }

        st.validLength = len;
        st.source      = source;
        st.frozen      = true;
        st.clearing    = false;
        st.fadeEnv     = 0.0f;
        st.fadeTarget  = 1.0f;
        st.tailL       = 0.0f;
        st.tailR       = 0.0f;
        st.scanPos     = findActiveCenter (st.snapL, st.snapR, len);
        resetGrainHeads (st, st.scanPos);

        if (telemetry != nullptr)
            publishStratumTelemetry (slot, st, telemetry);
    }

    struct ExternalModulation
    {
        float crossfadePosOffset   = 0.0f;  // additive shift on crossfadePos (-1..+1)
        float crossfadeDepthBoost  = 0.0f;  // minimum/boosted crossfade focus when VowelMorph linked (0..1)
        float tiltOffset           = 0.0f;  // additive tilt shift from VowelMorph brightness/vowelY (-1..+1)
        bool  useQuantizedPitch    = false;
        std::array<float, kNumStrata> quantizedPitchSemi {};
    };

    void processBlock (const float* inTL, const float* inTR,
                       const float* inHL, const float* inHR,
                       const float* inNL, const float* inNR,
                       float* outL, float* outR,
                       int numSamples,
                       const MasterParams& params,
                       EuclideanGate& gate,
                       FossilTelemetry* telemetry = nullptr,
                       const ExternalModulation* extMod = nullptr) noexcept
    {
        if (numSamples <= 0)
            return;

        if (outL != nullptr)
            std::fill (outL, outL + numSamples, 0.0f);
        if (outR != nullptr)
            std::fill (outR, outR + numSamples, 0.0f);

        if (historyLen_ <= 0 || outL == nullptr || outR == nullptr)
            return;

        bool anySolo = false;
        for (int s = 0; s < kNumStrata; ++s)
        {
            if (params.strata[static_cast<size_t> (s)].solo)
            {
                anySolo = true;
                break;
            }
        }

        const float baseCfDepth = std::isfinite (params.crossfadeDepth) ? std::clamp (params.crossfadeDepth, 0.0f, 1.0f) : 0.0f;
        const float baseCfPos   = std::isfinite (params.crossfadePos)   ? std::clamp (params.crossfadePos, 0.0f, 1.0f)   : 0.5f;
        const float cfDepth     = (extMod != nullptr) ? std::clamp (std::max (baseCfDepth, extMod->crossfadeDepthBoost), 0.0f, 1.0f) : baseCfDepth;
        const float cfPos       = (extMod != nullptr) ? std::clamp (baseCfPos + extMod->crossfadePosOffset, 0.0f, 1.0f) : baseCfPos;
        const float cfCenter    = cfPos * static_cast<float> (kNumStrata - 1);
        const float extTilt     = (extMod != nullptr && std::isfinite (extMod->tiltOffset)) ? std::clamp (extMod->tiltOffset, -1.0f, 1.0f) : 0.0f;

        std::array<float, kNumStrata> effectiveWeights {};
        std::array<float, kNumStrata> slotPeaks {};

        for (int s = 0; s < kNumStrata; ++s)
        {
            if (cfDepth <= 1.0e-6f)
            {
                effectiveWeights[static_cast<size_t> (s)] = 1.0f;
            }
            else
            {
                constexpr float kFocusRadius = 1.5f;
                const float dist = std::abs (static_cast<float> (s) - cfCenter);
                const float win  = (dist < kFocusRadius) ? (0.5f * (1.0f + std::cos (kPi * dist / kFocusRadius))) : 0.0f;
                effectiveWeights[static_cast<size_t> (s)] = (1.0f - cfDepth) + cfDepth * win;
            }
        }

        constexpr int kChunkSize = 256;
        std::array<float, kChunkSize> gateBuf {};

        int offset = 0;
        while (offset < numSamples)
        {
            const int chunkLen = std::min (kChunkSize, numSamples - offset);

            // 1. Record incoming T/H/N streams into circular history buffers & advance Euclidean gate once per sample.
            auto& histT = history_[0];
            auto& histH = history_[1];
            auto& histN = history_[2];

            for (int n = 0; n < chunkLen; ++n)
            {
                const int idx = offset + n;

                histT.bufL[static_cast<size_t> (histT.writeIdx)] = (inTL != nullptr) ? sanitize (inTL[idx]) : 0.0f;
                histT.bufR[static_cast<size_t> (histT.writeIdx)] = (inTR != nullptr) ? sanitize (inTR[idx]) : 0.0f;
                if (++histT.writeIdx >= historyLen_) histT.writeIdx = 0;

                histH.bufL[static_cast<size_t> (histH.writeIdx)] = (inHL != nullptr) ? sanitize (inHL[idx]) : 0.0f;
                histH.bufR[static_cast<size_t> (histH.writeIdx)] = (inHR != nullptr) ? sanitize (inHR[idx]) : 0.0f;
                if (++histH.writeIdx >= historyLen_) histH.writeIdx = 0;

                histN.bufL[static_cast<size_t> (histN.writeIdx)] = (inNL != nullptr) ? sanitize (inNL[idx]) : 0.0f;
                histN.bufR[static_cast<size_t> (histN.writeIdx)] = (inNR != nullptr) ? sanitize (inNR[idx]) : 0.0f;
                if (++histN.writeIdx >= historyLen_) histN.writeIdx = 0;

                gateBuf[static_cast<size_t> (n)] = gate.processSample();
            }

            // 2. Render each active stratum for this chunk.
            for (int s = 0; s < kNumStrata; ++s)
            {
                auto& st = strata_[static_cast<size_t> (s)];
                const bool hasTail = (std::abs (st.tailL) > 1.0e-6f || std::abs (st.tailR) > 1.0e-6f);
                if (! st.frozen && st.fadeEnv <= 1.0e-6f && ! hasTail)
                    continue;

                const auto& sp = params.strata[static_cast<size_t> (s)];
                const int   vLen = st.validLength;
                if (vLen < 16)
                    continue;

                const float fLen = static_cast<float> (vLen);

                const float grainMs    = std::isfinite (sp.grainMs)   ? std::clamp (sp.grainMs, 20.0f, 500.0f) : 180.0f;
                const float rawPitch   = (extMod != nullptr && extMod->useQuantizedPitch)
                                             ? extMod->quantizedPitchSemi[static_cast<size_t> (s)]
                                             : sp.pitchSemi;
                const float pitchSemi  = std::isfinite (rawPitch)     ? std::clamp (rawPitch, -24.0f, 24.0f) : 0.0f;
                const float drift      = std::isfinite (sp.drift)     ? std::clamp (sp.drift, 0.0f, 1.0f) : 0.30f;
                const float tilt       = std::isfinite (sp.tilt)      ? std::clamp (sp.tilt + extTilt, -1.0f, 1.0f) : 0.0f;
                const float width      = std::isfinite (sp.width)     ? std::clamp (sp.width, 0.0f, 2.0f) : 1.0f;
                const float pan        = std::isfinite (sp.pan)       ? std::clamp (sp.pan, -1.0f, 1.0f) : 0.0f;
                const float userGain   = std::isfinite (sp.gain)      ? std::clamp (sp.gain, 0.0f, 1.5f) : 0.80f;

                const float grainSamples = std::clamp (grainMs * 0.001f * static_cast<float> (sampleRate_), 32.0f, fLen - 4.0f);
                const float phaseInc     = 1.0f / grainSamples;
                const float baseSpeed    = std::exp2 (pitchSemi / 12.0f);

                // Smoothly blend normalization between coherent (0.5) and uncorrelated drifted grains (~0.72).
                const float grainNorm = 0.5f + 0.22f * drift;

                // Complementary 1-pole tilt gains (~ +-5.1 dB).
                const float lowGain  = std::exp2 (-tilt * 0.85f);
                const float highGain = std::exp2 ( tilt * 0.85f);

                // Constant-power pan (pan=0 -> 1.0, 1.0).
                const float panAngle = (pan + 1.0f) * 0.25f * kPi;
                const float panL     = std::cos (panAngle) * kSqrt2;
                const float panR     = std::sin (panAngle) * kSqrt2;

                const bool  muted      = sp.mute || (anySolo && ! sp.solo);
                const float targetGain = muted ? 0.0f : (userGain * effectiveWeights[static_cast<size_t> (s)]);
                const float scanRate   = 0.15f + 0.65f * drift; // Gentle continuous scan through snapshot.

                float peak = slotPeaks[static_cast<size_t> (s)];

                for (int n = 0; n < chunkLen; ++n)
                {
                    // Update fade envelope
                    if (st.fadeEnv < st.fadeTarget)
                        st.fadeEnv = std::min (st.fadeTarget, st.fadeEnv + fadeStep_);
                    else if (st.fadeEnv > st.fadeTarget)
                        st.fadeEnv = std::max (st.fadeTarget, st.fadeEnv - fadeStep_);

                    if (st.clearing && st.fadeEnv <= 1.0e-5f)
                    {
                        st.fadeEnv  = 0.0f;
                        st.frozen   = false;
                        st.clearing = false;
                    }

                    // Advance slow scan playhead
                    st.scanPos += scanRate;
                    if (st.scanPos >= fLen)
                        st.scanPos -= fLen;

                    float sumL = 0.0f;
                    float sumR = 0.0f;

                    if (st.frozen || st.fadeEnv > 1.0e-6f)
                    {
                        for (int g = 0; g < kNumGrains; ++g)
                        {
                            auto& gr = st.grains[static_cast<size_t> (g)];

                            gr.phase += phaseInc;
                            if (gr.phase >= 1.0f)
                            {
                                gr.phase -= 1.0f;
                                if (gr.phase >= 1.0f)
                                    gr.phase = 0.0f;

                                // Per-grain drift: start-position jitter, micro-detune (+-18 cents), L/R scatter.
                                const float jitterNorm   = nextRandSigned (st.rngState) * 0.45f * drift;
                                float       newStart     = st.scanPos + jitterNorm * fLen;
                                while (newStart < 0.0f)  newStart += fLen;
                                while (newStart >= fLen) newStart -= fLen;
                                gr.readPos = newStart;

                                const float detuneCents  = nextRandSigned (st.rngState) * 18.0f * drift;
                                gr.speed = baseSpeed * std::exp2 (detuneCents / 1200.0f);

                                const float scatter      = nextRandSigned (st.rngState) * 0.35f * drift;
                                const float scAngle      = (scatter + 1.0f) * 0.25f * kPi;
                                gr.gainL = std::cos (scAngle) * kSqrt2;
                                gr.gainR = std::sin (scAngle) * kSqrt2;
                            }

                            const float w  = 0.5f * (1.0f - std::cos (kTwoPi * gr.phase));
                            const float sL = readCubicHermite (st.snapL, vLen, gr.readPos);
                            const float sR = readCubicHermite (st.snapR, vLen, gr.readPos);

                            sumL += sL * w * gr.gainL;
                            sumR += sR * w * gr.gainR;

                            gr.readPos += gr.speed;
                            if (gr.readPos >= fLen)
                            {
                                gr.readPos -= fLen;
                                if (gr.readPos >= fLen)
                                    gr.readPos = std::fmod (gr.readPos, fLen);
                            }
                            if (gr.readPos < 0.0f || ! std::isfinite (gr.readPos))
                                gr.readPos = 0.0f;
                        }
                    }

                    float l = sumL * grainNorm;
                    float r = sumR * grainNorm;

                    // 1-pole Filter Tilt (~900 Hz crossover)
                    st.lpStateL = flushDenormal (st.lpStateL + tiltLpCoeff_ * (l - st.lpStateL));
                    st.lpStateR = flushDenormal (st.lpStateR + tiltLpCoeff_ * (r - st.lpStateR));
                    l = st.lpStateL * lowGain + (l - st.lpStateL) * highGain;
                    r = st.lpStateR * lowGain + (r - st.lpStateR) * highGain;

                    // Stereo Width (M/S)
                    const float mid  = 0.5f * (l + r);
                    const float side = 0.5f * (l - r) * width;
                    l = mid + side;
                    r = mid - side;

                    // Constant-Power Pan
                    l *= panL;
                    r *= panR;

                    // Smoothed Gain + Fade Envelope + Euclidean Gate
                    st.smoothedGain += gainSmoothCoeff_ * (targetGain - st.smoothedGain);
                    st.smoothedGain  = flushDenormal (st.smoothedGain);

                    float totalGain = st.smoothedGain * st.fadeEnv;
                    if (sp.euclidGated)
                        totalGain *= gateBuf[static_cast<size_t> (n)];

                    l = flushDenormal (l * totalGain + st.tailL);
                    r = flushDenormal (r * totalGain + st.tailR);

                    st.tailL = flushDenormal (st.tailL * tailDecay_);
                    st.tailR = flushDenormal (st.tailR * tailDecay_);

                    st.lastOutL = l;
                    st.lastOutR = r;

                    outL[offset + n] += l;
                    outR[offset + n] += r;

                    const float absMax = std::max (std::abs (l), std::abs (r));
                    if (absMax > peak)
                        peak = absMax;
                }

                slotPeaks[static_cast<size_t> (s)] = peak;
            }

            offset += chunkLen;
        }

        // 3. Publish per-stratum telemetry & cached block stats
        lastSlotLevels_ = slotPeaks;
        float combined = 0.0f;
        for (int s = 0; s < kNumStrata; ++s)
            combined = std::max (combined, slotPeaks[static_cast<size_t> (s)]);
        lastCombinedEnergy_ = combined;

        if (telemetry != nullptr)
        {
            for (int s = 0; s < kNumStrata; ++s)
            {
                const auto& st = strata_[static_cast<size_t> (s)];
                auto& t = telemetry->strata[static_cast<size_t> (s)];
                t.outputLevel.store (slotPeaks[static_cast<size_t> (s)], std::memory_order_relaxed);
                t.effectiveWeight.store (effectiveWeights[static_cast<size_t> (s)], std::memory_order_relaxed);

                float playNorm = 0.0f;
                if (st.validLength > 0)
                    playNorm = std::clamp (st.grains[0].readPos / static_cast<float> (st.validLength), 0.0f, 1.0f);
                t.playheadNorm.store (playNorm, std::memory_order_relaxed);
            }
        }
    }

    uint8_t getFrozenMask() const noexcept
    {
        uint8_t mask = 0u;
        for (int s = 0; s < kNumStrata; ++s)
            if (isSlotFrozen (s))
                mask |= static_cast<uint8_t> (1u << static_cast<unsigned> (s));
        return mask;
    }

    uint8_t getFrozenCount() const noexcept
    {
        uint8_t cnt = 0u;
        for (int s = 0; s < kNumStrata; ++s)
            if (isSlotFrozen (s))
                ++cnt;
        return cnt;
    }

    float getLastCombinedEnergy() const noexcept { return lastCombinedEnergy_; }
    const std::array<float, kNumStrata>& getLastSlotLevels() const noexcept { return lastSlotLevels_; }

    float getDominantPitchSemi (const MasterParams& params) const noexcept
    {
        float weightedSum = 0.0f;
        float totalW = 0.0f;
        for (int s = 0; s < kNumStrata; ++s)
        {
            if (isSlotFrozen (s))
            {
                const float w = std::max (0.05f, lastSlotLevels_[static_cast<size_t> (s)]);
                weightedSum += params.strata[static_cast<size_t> (s)].pitchSemi * w;
                totalW += w;
            }
        }
        return (totalW > 0.0f) ? (weightedSum / totalW) : 0.0f;
    }

private:
    struct HistoryBuffer
    {
        std::vector<float> bufL;
        std::vector<float> bufR;
        int                writeIdx = 0;
    };

    struct GrainHead
    {
        float phase   = 0.0f;
        float readPos = 0.0f;
        float speed   = 1.0f;
        float gainL   = 1.0f;
        float gainR   = 1.0f;
    };

    struct Stratum
    {
        std::vector<float> snapL;
        std::vector<float> snapR;
        int                validLength  = 0;
        bool               frozen       = false;
        bool               clearing     = false;
        StreamSource       source       = StreamSource::Tonal;

        float              fadeEnv      = 0.0f;
        float              fadeTarget   = 0.0f;
        float              smoothedGain = 0.0f;

        float              lpStateL     = 0.0f;
        float              lpStateR     = 0.0f;

        float              lastOutL     = 0.0f;
        float              lastOutR     = 0.0f;
        float              tailL        = 0.0f;
        float              tailR        = 0.0f;

        float              scanPos      = 0.0f;
        uint32_t           rngState     = 0x9E3779B9u;

        std::array<GrainHead, kNumGrains> grains {};
    };

    static inline float sanitize (float x) noexcept
    {
        if (! std::isfinite (x) || std::abs (x) < 1.0e-20f)
            return 0.0f;
        return std::clamp (x, -8.0f, 8.0f);
    }

    static inline float flushDenormal (float x) noexcept
    {
        if (! std::isfinite (x) || std::abs (x) < 1.0e-20f)
            return 0.0f;
        return x;
    }

    static inline float nextRandSigned (uint32_t& state) noexcept
    {
        uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = (x != 0u) ? x : 0x9E3779B9u;
        // Map top 24 bits to [-1.0, +1.0]
        const float u = static_cast<float> (state >> 8) * (1.0f / 8388608.0f) - 1.0f;
        return u;
    }

    static inline float readCubicHermite (const std::vector<float>& buf, int len, float pos) noexcept
    {
        int i1 = static_cast<int> (pos);
        if (i1 < 0 || i1 >= len)
            i1 = 0;

        const float frac = pos - static_cast<float> (i1);
        const int i0 = (i1 == 0) ? (len - 1) : (i1 - 1);
        const int i2 = (i1 + 1 >= len) ? 0 : (i1 + 1);
        const int i3 = (i2 + 1 >= len) ? 0 : (i2 + 1);

        const float y0 = buf[static_cast<size_t> (i0)];
        const float y1 = buf[static_cast<size_t> (i1)];
        const float y2 = buf[static_cast<size_t> (i2)];
        const float y3 = buf[static_cast<size_t> (i3)];

        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    static void applyEdgeTaper (std::vector<float>& bufL, std::vector<float>& bufR, int len) noexcept
    {
        const int taper = std::min (64, len / 8);
        if (taper <= 1)
            return;

        const float inv = 1.0f / static_cast<float> (taper);
        for (int i = 0; i < taper; ++i)
        {
            const float w = 0.5f * (1.0f - std::cos (kPi * static_cast<float> (i) * inv));
            bufL[static_cast<size_t> (i)]           *= w;
            bufR[static_cast<size_t> (i)]           *= w;
            bufL[static_cast<size_t> (len - 1 - i)] *= w;
            bufR[static_cast<size_t> (len - 1 - i)] *= w;
        }
    }

    static float findActiveCenter (const std::vector<float>& bufL, const std::vector<float>& bufR, int len) noexcept
    {
        if (len <= 0)
            return 0.0f;
        // Find the peak/active region so grain playback immediately locks onto non-silent audio
        int bestIdx = len / 2;
        float bestEnv = 0.0f;
        const int step = std::max (1, len / 128);
        for (int i = 0; i < len; i += step)
        {
            const float a = std::max (std::abs (bufL[static_cast<size_t> (i)]),
                                      std::abs (bufR[static_cast<size_t> (i)]));
            if (a > bestEnv)
            {
                bestEnv = a;
                bestIdx = i;
            }
        }
        return static_cast<float> (bestIdx);
    }

    static void resetGrainHeads (Stratum& st, float startPos) noexcept
    {
        const float fLen = static_cast<float> (std::max (1, st.validLength));
        const float spread = std::min (0.15f * fLen, 4096.0f);
        for (int g = 0; g < kNumGrains; ++g)
        {
            auto& gr  = st.grains[static_cast<size_t> (g)];
            gr.phase  = static_cast<float> (g) * (1.0f / static_cast<float> (kNumGrains));
            float pos = startPos + (gr.phase - 0.5f) * spread;
            while (pos < 0.0f)  pos += fLen;
            while (pos >= fLen) pos -= fLen;
            gr.readPos = pos;
            gr.speed   = 1.0f;
            gr.gainL   = 1.0f;
            gr.gainR   = 1.0f;
        }
    }

    static void publishStratumTelemetry (int slot, const Stratum& st, FossilTelemetry* telemetry) noexcept
    {
        auto& t = telemetry->strata[static_cast<size_t> (slot)];
        const bool active = st.frozen && ! st.clearing;
        t.frozen.store (active, std::memory_order_relaxed);
        t.source.store (static_cast<int> (st.source), std::memory_order_relaxed);

        if (! active || st.validLength <= 0)
        {
            for (auto& bin : t.wavePreview)
                bin.store (0.0f, std::memory_order_relaxed);
            t.captureCounter.fetch_add (1u, std::memory_order_release);
            return;
        }

        const int len = std::min (st.validLength, static_cast<int> (st.snapL.size()));
        std::array<float, kWavePreviewBins> peaks {};
        float maxPeak = 0.0f;

        for (int b = 0; b < kWavePreviewBins; ++b)
        {
            const int start = (b * len) / kWavePreviewBins;
            const int end   = std::max (start + 1, ((b + 1) * len) / kWavePreviewBins);
            float binPeak = 0.0f;
            for (int i = start; i < end && i < len; ++i)
            {
                const float v = std::max (std::abs (st.snapL[static_cast<size_t> (i)]),
                                          std::abs (st.snapR[static_cast<size_t> (i)]));
                if (v > binPeak)
                    binPeak = v;
            }
            peaks[static_cast<size_t> (b)] = binPeak;
            if (binPeak > maxPeak)
                maxPeak = binPeak;
        }

        const float norm = (maxPeak > 1.0e-6f) ? (1.0f / maxPeak) : 0.0f;
        for (int b = 0; b < kWavePreviewBins; ++b)
            t.wavePreview[static_cast<size_t> (b)].store (std::clamp (peaks[static_cast<size_t> (b)] * norm, 0.0f, 1.0f),
                                                          std::memory_order_relaxed);

        t.captureCounter.fetch_add (1u, std::memory_order_release);
    }

    double sampleRate_      = 44100.0;
    int    historyLen_      = 0;
    int    captureLen_      = 0;
    int    roundRobinSlot_  = 0;

    float  fadeStep_        = 0.0015f;
    float  gainSmoothCoeff_ = 0.002f;
    float  tailDecay_       = 0.994f;
    float  tiltLpCoeff_     = 0.12f;

    std::array<HistoryBuffer, kNumStreams> history_ {};
    std::array<Stratum, kNumStrata>        strata_  {};
    std::array<float, kNumStrata>          lastSlotLevels_ {};
    float                                  lastCombinedEnergy_ = 0.0f;
};

} // namespace fossil::dsp
