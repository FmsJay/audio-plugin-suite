#pragma once

#include "../FossilParams.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace fossil::dsp
{

class ShimmerDiffusion
{
public:
    static constexpr int   kNumFdnChannels = 4;
    static constexpr float kPi             = 3.14159265358979323846f;
    static constexpr float kTwoPi          = 6.28318530717958647692f;
    static constexpr float kHalfPi         = 1.57079632679489661923f;

    ShimmerDiffusion() = default;

    void prepare (double sampleRate, int /*maxBlockSize*/)
    {
        sampleRate_ = (std::isfinite (sampleRate) && sampleRate >= 8000.0) ? sampleRate : 44100.0;

        // 2-stage stereo input diffusion allpasses (mutually prime lengths)
        constexpr std::array<double, 2> kApMsL = { 3.7, 6.1 };
        constexpr std::array<double, 2> kApMsR = { 4.3, 7.1 };
        int prevPrime = 13;
        for (int i = 0; i < 2; ++i)
        {
            const int lenL = nextPrime (std::max (prevPrime + 2, static_cast<int> (std::llround (sampleRate_ * kApMsL[static_cast<size_t> (i)] * 0.001))));
            prevPrime = lenL;
            apL_[static_cast<size_t> (i)].prepare (lenL, 0.50f);

            const int lenR = nextPrime (std::max (prevPrime + 2, static_cast<int> (std::llround (sampleRate_ * kApMsR[static_cast<size_t> (i)] * 0.001))));
            prevPrime = lenR;
            apR_[static_cast<size_t> (i)].prepare (lenR, 0.50f);
        }

        // 4-channel Householder FDN with mutually prime delay lengths (~23ms, 31ms, 43ms, 59ms)
        constexpr std::array<double, kNumFdnChannels> kFdnMs = { 23.0, 31.0, 43.0, 59.0 };
        for (int c = 0; c < kNumFdnChannels; ++c)
        {
            const int target = std::max (prevPrime + 2, static_cast<int> (std::llround (sampleRate_ * kFdnMs[static_cast<size_t> (c)] * 0.001)));
            const int primeLen = nextPrime (target);
            prevPrime = primeLen;
            fdnBuf_[static_cast<size_t> (c)].assign (static_cast<size_t> (primeLen), 0.0f);
            fdnLen_[static_cast<size_t> (c)] = primeLen;
            fdnWrite_[static_cast<size_t> (c)] = 0;
        }

        // Dual-head Hann-windowed +12 semitone pitch-shifter (~50 ms circular grain window)
        pitchGrainLen_ = std::max (128, static_cast<int> (std::llround (sampleRate_ * 0.050)));
        pitchBufLen_   = pitchGrainLen_ + 16;
        pitchBufL_.assign (static_cast<size_t> (pitchBufLen_), 0.0f);
        pitchBufR_.assign (static_cast<size_t> (pitchBufLen_), 0.0f);
        pitchPhaseInc_ = 1.0f / static_cast<float> (pitchGrainLen_);

        // Loop filters: 1-pole HF damping (~6.5 kHz) and 1-pole DC blocker (~20 Hz)
        hfDampCoeff_ = static_cast<float> (1.0 - std::exp (-static_cast<double> (kTwoPi) * 6500.0 / sampleRate_));
        hfDampCoeff_ = std::clamp (hfDampCoeff_, 0.02f, 0.98f);

        dcBlockR_ = static_cast<float> (1.0 - (static_cast<double> (kTwoPi) * 20.0 / sampleRate_));
        dcBlockR_ = std::clamp (dcBlockR_, 0.90f, 0.9999f);

        paramSmoothCoeff_ = static_cast<float> (1.0 - std::exp (-1.0 / std::max (1.0, sampleRate_ * 0.015)));

        prepared_ = true;
        reset();
    }

    void reset() noexcept
    {
        for (auto& ap : apL_) ap.reset();
        for (auto& ap : apR_) ap.reset();

        for (int c = 0; c < kNumFdnChannels; ++c)
        {
            std::fill (fdnBuf_[static_cast<size_t> (c)].begin(), fdnBuf_[static_cast<size_t> (c)].end(), 0.0f);
            fdnWrite_[static_cast<size_t> (c)] = 0;
            hfState_[static_cast<size_t> (c)]  = 0.0f;
            dcPrevIn_[static_cast<size_t> (c)] = 0.0f;
            dcPrevOut_[static_cast<size_t> (c)] = 0.0f;
        }

        std::fill (pitchBufL_.begin(), pitchBufL_.end(), 0.0f);
        std::fill (pitchBufR_.begin(), pitchBufR_.end(), 0.0f);
        pitchWrite_  = 0;
        pitchPhase_  = 0.0f;

        smoothSmear_   = 0.0f;
        smoothShimmer_ = 0.0f;
    }

    void processBlock (float* ioL, float* ioR, int numSamples,
                       float smearAmount, float shimmerOctaveAmount,
                       float vowelBrightness01 = 0.55f,
                       float shimmerIntervalSemi = 12.0f) noexcept
    {
        if (! prepared_ || ioL == nullptr || ioR == nullptr || numSamples <= 0)
            return;

        const float targetSmear   = std::isfinite (smearAmount)         ? std::clamp (smearAmount, 0.0f, 1.0f)         : 0.0f;
        const float targetShimmer = std::isfinite (shimmerOctaveAmount) ? std::clamp (shimmerOctaveAmount, 0.0f, 1.0f) : 0.0f;
        const float bright        = std::isfinite (vowelBrightness01)   ? std::clamp (vowelBrightness01, 0.0f, 1.0f)   : 0.55f;

        // Formant-colored HF damping cutoff: 1400 Hz (warm OO/MM) -> 12500 Hz (bright EE/AA)
        const double dampHz = 1400.0 * std::pow (12500.0 / 1400.0, static_cast<double> (bright));
        const float targetDampCoeff = std::clamp (
            static_cast<float> (1.0 - std::exp (-static_cast<double> (kTwoPi) * dampHz / sampleRate_)),
            0.02f, 0.98f);

        // Pitch-shifter rate for shimmer feedback tap (default +12st = 2.0x; supports +7st fifth etc.)
        const float semiClamped = std::isfinite (shimmerIntervalSemi) ? std::clamp (shimmerIntervalSemi, 1.0f, 24.0f) : 12.0f;
        const float pitchRatio  = std::exp2 (semiClamped / 12.0f);
        const float phaseInc    = std::max (0.00005f, (pitchRatio - 1.0f) / static_cast<float> (pitchGrainLen_));

        for (int n = 0; n < numSamples; ++n)
        {
            smoothSmear_   += paramSmoothCoeff_ * (targetSmear   - smoothSmear_);
            smoothShimmer_ += paramSmoothCoeff_ * (targetShimmer - smoothShimmer_);
            hfDampCoeff_   += paramSmoothCoeff_ * (targetDampCoeff - hfDampCoeff_);
            smoothSmear_    = flushDenormal (smoothSmear_);
            smoothShimmer_  = flushDenormal (smoothShimmer_);

            const float dryL = sanitize (ioL[n]);
            const float dryR = sanitize (ioR[n]);

            // 1. 2-stage stereo input diffusion allpasses
            float diffL = apL_[1].process (apL_[0].process (dryL));
            float diffR = apR_[1].process (apR_[0].process (dryR));

            // 2. Read current FDN delay outputs
            std::array<float, kNumFdnChannels> d {};
            for (int c = 0; c < kNumFdnChannels; ++c)
                d[static_cast<size_t> (c)] = fdnBuf_[static_cast<size_t> (c)][static_cast<size_t> (fdnWrite_[static_cast<size_t> (c)])];

            // 3. Dual-head Hann-windowed pitch-shifter tapping FDN
            const float tapL = 0.5f * (d[0] + d[2]);
            const float tapR = 0.5f * (d[1] + d[3]);

            pitchBufL_[static_cast<size_t> (pitchWrite_)] = tapL;
            pitchBufR_[static_cast<size_t> (pitchWrite_)] = tapR;

            const float p1 = pitchPhase_;
            const float p2 = (p1 + 0.5f >= 1.0f) ? (p1 - 0.5f) : (p1 + 0.5f);

            const float w1 = 0.5f * (1.0f - std::cos (kTwoPi * p1));
            const float w2 = 0.5f * (1.0f - std::cos (kTwoPi * p2));

            // Decreasing delay from pitchGrainLen_ -> 0 yields pitchRatio read speed.
            const float delay1 = (1.0f - p1) * static_cast<float> (pitchGrainLen_);
            const float delay2 = (1.0f - p2) * static_cast<float> (pitchGrainLen_);

            const float shimL = readPitchDelay (pitchBufL_, delay1) * w1 + readPitchDelay (pitchBufL_, delay2) * w2;
            const float shimR = readPitchDelay (pitchBufR_, delay1) * w1 + readPitchDelay (pitchBufR_, delay2) * w2;

            if (++pitchWrite_ >= pitchBufLen_)
                pitchWrite_ = 0;

            pitchPhase_ += phaseInc;
            if (pitchPhase_ >= 1.0f)
                pitchPhase_ -= 1.0f;

            // 4. Unitary 4x4 Householder feedback matrix: m_k = 0.5 * sum(d) - d_k
            const float halfSum = 0.5f * (d[0] + d[1] + d[2] + d[3]);
            std::array<float, kNumFdnChannels> m {};
            for (int c = 0; c < kNumFdnChannels; ++c)
                m[static_cast<size_t> (c)] = halfSum - d[static_cast<size_t> (c)];

            // 5. Loop feedback with shimmer injection, HF damping, DC blocking, and soft tanh saturation
            const float fbBase    = 0.22f + 0.69f * smoothSmear_; // max 0.91
            const float shimMix   = smoothShimmer_ * (0.20f + 0.22f * smoothSmear_);
            const float normalFb  = fbBase * (1.0f - 0.35f * smoothShimmer_);

            // Distribute stereo input + shimmer into the 4 FDN channels
            const std::array<float, kNumFdnChannels> inj = {
                0.5f * diffL + shimMix * shimL,
                0.5f * diffR + shimMix * shimR,
                0.5f * diffL + shimMix * shimR,
                0.5f * diffR + shimMix * shimL
            };

            for (int c = 0; c < kNumFdnChannels; ++c)
            {
                const size_t idx = static_cast<size_t> (c);
                float x = inj[idx] + normalFb * m[idx];

                // 1-pole HF damping
                hfState_[idx] = flushDenormal (hfState_[idx] + hfDampCoeff_ * (x - hfState_[idx]));
                x = hfState_[idx];

                // 1-pole DC blocker
                const float dcOut = flushDenormal (x - dcPrevIn_[idx] + dcBlockR_ * dcPrevOut_[idx]);
                dcPrevIn_[idx]  = x;
                dcPrevOut_[idx] = dcOut;

                // Soft tanh saturation for rock-solid loop stability
                const float sat = flushDenormal (1.5f * std::tanh (dcOut * (1.0f / 1.5f)));

                fdnBuf_[idx][static_cast<size_t> (fdnWrite_[idx])] = sat;
                if (++fdnWrite_[idx] >= fdnLen_[idx])
                    fdnWrite_[idx] = 0;
            }

            // 6. Stereo wet output tap & smooth dry/wet blend
            const float wetL = 0.5f * (d[0] + d[2]);
            const float wetR = 0.5f * (d[1] + d[3]);

            const float dryGain = std::cos (smoothSmear_ * kHalfPi * 0.60f);
            const float wetGain = std::sin (smoothSmear_ * kHalfPi * 0.85f);

            ioL[n] = sanitize (dryL * dryGain + wetL * wetGain);
            ioR[n] = sanitize (dryR * dryGain + wetR * wetGain);
        }
    }

private:
    struct Allpass
    {
        std::vector<float> buf;
        int                len      = 1;
        int                writeIdx = 0;
        float              g        = 0.5f;

        void prepare (int delaySamples, float coeff)
        {
            len = std::max (1, delaySamples);
            g   = coeff;
            buf.assign (static_cast<size_t> (len), 0.0f);
            writeIdx = 0;
        }

        void reset() noexcept
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            writeIdx = 0;
        }

        inline float process (float x) noexcept
        {
            const float d = buf[static_cast<size_t> (writeIdx)];
            const float v = flushDenormal (x + g * d);
            const float y = flushDenormal (d - g * v);
            buf[static_cast<size_t> (writeIdx)] = v;
            if (++writeIdx >= len)
                writeIdx = 0;
            return y;
        }
    };

    static bool isPrime (int n) noexcept
    {
        if (n < 2) return false;
        if (n == 2 || n == 3) return true;
        if (n % 2 == 0 || n % 3 == 0) return false;
        for (int i = 5; i * i <= n; i += 6)
        {
            if (n % i == 0 || n % (i + 2) == 0)
                return false;
        }
        return true;
    }

    static int nextPrime (int n) noexcept
    {
        int cand = std::max (2, n);
        if (cand > 2 && (cand % 2 == 0))
            ++cand;
        while (! isPrime (cand))
            cand += 2;
        return cand;
    }

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

    inline float readPitchDelay (const std::vector<float>& buf, float delaySamples) const noexcept
    {
        float readPos = static_cast<float> (pitchWrite_) - delaySamples;
        const float fLen = static_cast<float> (pitchBufLen_);
        while (readPos < 0.0f)  readPos += fLen;
        while (readPos >= fLen) readPos -= fLen;

        const int   i0   = std::clamp (static_cast<int> (readPos), 0, pitchBufLen_ - 1);
        const int   i1   = (i0 + 1 >= pitchBufLen_) ? 0 : (i0 + 1);
        const float frac = readPos - static_cast<float> (i0);

        return buf[static_cast<size_t> (i0)] + frac * (buf[static_cast<size_t> (i1)] - buf[static_cast<size_t> (i0)]);
    }

    double sampleRate_       = 44100.0;
    bool   prepared_         = false;

    std::array<Allpass, 2> apL_ {};
    std::array<Allpass, 2> apR_ {};

    std::array<std::vector<float>, kNumFdnChannels> fdnBuf_ {};
    std::array<int, kNumFdnChannels>                fdnLen_ {};
    std::array<int, kNumFdnChannels>                fdnWrite_ {};

    std::array<float, kNumFdnChannels> hfState_   {};
    std::array<float, kNumFdnChannels> dcPrevIn_  {};
    std::array<float, kNumFdnChannels> dcPrevOut_ {};

    std::vector<float> pitchBufL_;
    std::vector<float> pitchBufR_;
    int                pitchGrainLen_ = 2205;
    int                pitchBufLen_   = 2221;
    int                pitchWrite_    = 0;
    float              pitchPhase_    = 0.0f;
    float              pitchPhaseInc_ = 1.0f / 2205.0f;

    float hfDampCoeff_      = 0.5f;
    float dcBlockR_         = 0.997f;
    float paramSmoothCoeff_ = 0.002f;
    float smoothSmear_      = 0.0f;
    float smoothShimmer_    = 0.0f;
};

} // namespace fossil::dsp
