#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace fossil::dsp
{

/**
 * Real-time 3-Way Stereo STFT Decomposition Engine (Transient / Tonal / Air-Noise).
 *
 * Uses a 512-point, 50%-overlap (256-sample hop) sqrt-Hann STFT with a centered
 * 5-frame sliding median-filter HPSS (2-frame lookahead), 13-bin frequency median,
 * local & broadband spectral flatness, and positive spectral flux to separate
 * incoming stereo audio into three phase-coherent streams whose sum reconstructs
 * the delayed dry signal with exact partition-of-unity masks (mT + mH + mN == 1).
 *
 * Header-only C++20, zero heap allocations in process(), no external dependencies.
 */
class HpssDecomposer
{
public:
    static constexpr int FFT_SIZE            = 512;
    static constexpr int FFT_LOG2            = 9;
    static constexpr int HOP_SIZE            = 256;
    static constexpr int NUM_BINS            = FFT_SIZE / 2 + 1; // 257
    static constexpr int TIME_MED_LEN        = 5;
    static constexpr int CENTER_DELAY_FRAMES = TIME_MED_LEN / 2; // 2
    static constexpr int FREQ_MED_LEN        = 13;
    static constexpr int FREQ_MED_HALF       = FREQ_MED_LEN / 2; // 6

    HpssDecomposer() noexcept
    {
        initTables();
        reset();
    }

    void prepare (double sampleRate, int /*maxBlockSize*/)
    {
        sampleRate_ = (sampleRate > 1000.0 && std::isfinite (sampleRate)) ? sampleRate : 48000.0;
        const double tauSec = 0.060; // 60 ms RMS telemetry smoothing
        envAlpha_ = static_cast<float> (1.0 - std::exp (-1.0 / (tauSec * sampleRate_)));
        initTables();
        reset();
    }

    void reset() noexcept
    {
        inBufL_.fill (0.0f);
        inBufR_.fill (0.0f);

        olaTL_.fill (0.0f);
        olaTR_.fill (0.0f);
        olaHL_.fill (0.0f);
        olaHR_.fill (0.0f);
        olaNL_.fill (0.0f);
        olaNR_.fill (0.0f);

        for (int f = 0; f < TIME_MED_LEN; ++f)
        {
            histL_[f].fill (Complex { 0.0f, 0.0f });
            histR_[f].fill (Complex { 0.0f, 0.0f });
            histMag_[f].fill (0.0f);
            histPow_[f].fill (0.0f);
        }

        hopCounter_   = 0;
        histWriteIdx_ = 0;

        smoothPowT_ = 0.0f;
        smoothPowH_ = 0.0f;
        smoothPowN_ = 0.0f;
    }

    void setParameters (float separation, float noiseSensitivity) noexcept
    {
        separation_       = sanitizeClamp (separation, 0.0f, 1.0f, 0.72f);
        noiseSensitivity_ = sanitizeClamp (noiseSensitivity, 0.0f, 1.0f, 0.45f);
    }

    /**
     * Exact sample delay from input sample n to reconstructed output sample n.
     *
     * Trace for an impulse at input sample n = 0:
     *  - Written to inBuf[256] during samples 0..255; analyzed in Frame 0 at Hop 0 (n=255).
     *  - Shifted to inBuf[0]; analyzed in Frame 1 at Hop 1 (n=511).
     *  - With CENTER_DELAY_FRAMES = 2:
     *      Frame 0 is synthesized at Hop 2 (n=767) into ola[256] (w[256]^2).
     *      Frame 1 is synthesized at Hop 3 (n=1023) after shifting ola[256]->ola[0],
     *      adding w[0]^2 so ola[0] = w[256]^2 + w[0]^2 = 1.0.
     *  - Read out at sample n = 1024.
     *  Total exact latency = FFT_SIZE + CENTER_DELAY_FRAMES * HOP_SIZE = 1024 samples.
     */
    int getLatencySamples() const noexcept
    {
        return FFT_SIZE + CENTER_DELAY_FRAMES * HOP_SIZE;
    }

    void process (const float* inL,
                  const float* inR,
                  float* outTL,
                  float* outTR,
                  float* outHL,
                  float* outHR,
                  float* outNL,
                  float* outNR,
                  int numSamples) noexcept
    {
        if (numSamples <= 0)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            const float tL = sanitizeSample (olaTL_[static_cast<size_t> (hopCounter_)]);
            const float tR = sanitizeSample (olaTR_[static_cast<size_t> (hopCounter_)]);
            const float hL = sanitizeSample (olaHL_[static_cast<size_t> (hopCounter_)]);
            const float hR = sanitizeSample (olaHR_[static_cast<size_t> (hopCounter_)]);
            const float nL = sanitizeSample (olaNL_[static_cast<size_t> (hopCounter_)]);
            const float nR = sanitizeSample (olaNR_[static_cast<size_t> (hopCounter_)]);

            if (outTL != nullptr) outTL[i] = tL;
            if (outTR != nullptr) outTR[i] = tR;
            if (outHL != nullptr) outHL[i] = hL;
            if (outHR != nullptr) outHR[i] = hR;
            if (outNL != nullptr) outNL[i] = nL;
            if (outNR != nullptr) outNR[i] = nR;

            // Update smoothed RMS energy telemetry
            const float pT = 0.5f * (tL * tL + tR * tR);
            const float pH = 0.5f * (hL * hL + hR * hR);
            const float pN = 0.5f * (nL * nL + nR * nR);
            smoothPowT_ = flushDenormal (smoothPowT_ + envAlpha_ * (pT - smoothPowT_));
            smoothPowH_ = flushDenormal (smoothPowH_ + envAlpha_ * (pH - smoothPowH_));
            smoothPowN_ = flushDenormal (smoothPowN_ + envAlpha_ * (pN - smoothPowN_));

            const float sL = (inL != nullptr) ? sanitizeSample (inL[i]) : 0.0f;
            const float sR = (inR != nullptr) ? sanitizeSample (inR[i]) : sL;

            const size_t writePos = static_cast<size_t> (FFT_SIZE - HOP_SIZE + hopCounter_);
            inBufL_[writePos] = sL;
            inBufR_[writePos] = sR;

            ++hopCounter_;
            if (hopCounter_ == HOP_SIZE)
            {
                hopCounter_ = 0;
                processFrame();
            }
        }
    }

    void getLastEnergies (float& energyT, float& energyH, float& energyN) const noexcept
    {
        energyT = std::sqrt (std::max (0.0f, smoothPowT_));
        energyH = std::sqrt (std::max (0.0f, smoothPowH_));
        energyN = std::sqrt (std::max (0.0f, smoothPowN_));
    }

private:
    struct Complex
    {
        float re = 0.0f;
        float im = 0.0f;
    };

    static inline float flushDenormal (float x) noexcept
    {
        return (std::fabs (x) < 1.0e-24f) ? 0.0f : x;
    }

    static inline float sanitizeSample (float x) noexcept
    {
        if (! std::isfinite (x))
            return 0.0f;
        return flushDenormal (x);
    }

    static inline float sanitizeClamp (float x, float lo, float hi, float fallback) noexcept
    {
        if (! std::isfinite (x))
            return fallback;
        return std::clamp (x, lo, hi);
    }

    static inline float median5 (float a, float b, float c, float d, float e) noexcept
    {
        auto sort2 = [] (float& x, float& y) noexcept
        {
            if (x > y)
            {
                const float t = x;
                x = y;
                y = t;
            }
        };

        sort2 (a, b);
        sort2 (c, d);
        if (a > c)
        {
            std::swap (a, c);
            std::swap (b, d);
        }
        a = e;
        sort2 (a, b);
        if (a > c)
        {
            std::swap (a, c);
            std::swap (b, d);
        }
        return (b < c) ? b : c;
    }

    void initTables() noexcept
    {
        constexpr double kPi = 3.14159265358979323846264338327950288;

        // Exact COLA sqrt-Hann window: w[n] = sin(pi * (n + 0.5) / N)
        // Guarantees w[n]^2 + w[n + N/2]^2 == 1.0 for all n in [0, N/2 - 1]
        for (int n = 0; n < FFT_SIZE; ++n)
        {
            const double angle = kPi * (static_cast<double> (n) + 0.5) / static_cast<double> (FFT_SIZE);
            window_[static_cast<size_t> (n)] = static_cast<float> (std::sin (angle));
        }

        // Bit-reversal table
        for (int i = 0; i < FFT_SIZE; ++i)
        {
            int rev = 0;
            int val = i;
            for (int b = 0; b < FFT_LOG2; ++b)
            {
                rev = (rev << 1) | (val & 1);
                val >>= 1;
            }
            bitRev_[static_cast<size_t> (i)] = static_cast<uint16_t> (rev);
        }

        // Twiddle factors for radix-2 Cooley-Tukey FFT: exp(-j * 2*pi * k / N)
        for (int k = 0; k < FFT_SIZE / 2; ++k)
        {
            const double angle = -2.0 * kPi * static_cast<double> (k) / static_cast<double> (FFT_SIZE);
            twiddleCos_[static_cast<size_t> (k)] = static_cast<float> (std::cos (angle));
            twiddleSin_[static_cast<size_t> (k)] = static_cast<float> (std::sin (angle));
        }
    }

    void fftInPlace (std::array<Complex, FFT_SIZE>& data, bool inverse) const noexcept
    {
        // Bit-reversal permutation
        for (int i = 0; i < FFT_SIZE; ++i)
        {
            const int j = static_cast<int> (bitRev_[static_cast<size_t> (i)]);
            if (i < j)
                std::swap (data[static_cast<size_t> (i)], data[static_cast<size_t> (j)]);
        }

        const float sinSign = inverse ? -1.0f : 1.0f;

        for (int size = 2; size <= FFT_SIZE; size <<= 1)
        {
            const int halfSize = size >> 1;
            const int step     = FFT_SIZE / size;

            for (int base = 0; base < FFT_SIZE; base += size)
            {
                int twIdx = 0;
                for (int k = 0; k < halfSize; ++k)
                {
                    const float wr = twiddleCos_[static_cast<size_t> (twIdx)];
                    const float wi = sinSign * twiddleSin_[static_cast<size_t> (twIdx)];
                    twIdx += step;

                    const size_t i0 = static_cast<size_t> (base + k);
                    const size_t i1 = static_cast<size_t> (base + k + halfSize);

                    const float uRe = data[i0].re;
                    const float uIm = data[i0].im;
                    const float vRe = data[i1].re * wr - data[i1].im * wi;
                    const float vIm = data[i1].re * wi + data[i1].im * wr;

                    data[i0].re = uRe + vRe;
                    data[i0].im = uIm + vIm;
                    data[i1].re = uRe - vRe;
                    data[i1].im = uIm - vIm;
                }
            }
        }

        if (inverse)
        {
            constexpr float invN = 1.0f / static_cast<float> (FFT_SIZE);
            for (int i = 0; i < FFT_SIZE; ++i)
            {
                data[static_cast<size_t> (i)].re *= invN;
                data[static_cast<size_t> (i)].im *= invN;
            }
        }
    }

    void processFrame() noexcept
    {
        // 1. Window L + j*R into fftBuf_ and compute single packed stereo forward FFT
        for (int n = 0; n < FFT_SIZE; ++n)
        {
            const float w = window_[static_cast<size_t> (n)];
            fftBuf_[static_cast<size_t> (n)].re = inBufL_[static_cast<size_t> (n)] * w;
            fftBuf_[static_cast<size_t> (n)].im = inBufR_[static_cast<size_t> (n)] * w;
        }

        // Shift input buffer left by HOP_SIZE for next frame
        for (int n = 0; n < FFT_SIZE - HOP_SIZE; ++n)
        {
            inBufL_[static_cast<size_t> (n)] = inBufL_[static_cast<size_t> (n + HOP_SIZE)];
            inBufR_[static_cast<size_t> (n)] = inBufR_[static_cast<size_t> (n + HOP_SIZE)];
        }

        fftInPlace (fftBuf_, false);

        // 2. Unpack stereo complex spectra into circular history at histWriteIdx_
        auto& dstL   = histL_[static_cast<size_t> (histWriteIdx_)];
        auto& dstR   = histR_[static_cast<size_t> (histWriteIdx_)];
        auto& dstMag = histMag_[static_cast<size_t> (histWriteIdx_)];
        auto& dstPow = histPow_[static_cast<size_t> (histWriteIdx_)];

        for (int k = 0; k < NUM_BINS; ++k)
        {
            const int mirror = (k == 0 || k == FFT_SIZE / 2) ? k : (FFT_SIZE - k);
            const Complex zk = fftBuf_[static_cast<size_t> (k)];
            const Complex zm = fftBuf_[static_cast<size_t> (mirror)];

            const Complex xL {
                0.5f * (zk.re + zm.re),
                0.5f * (zk.im - zm.im)
            };
            const Complex xR {
                0.5f * (zk.im + zm.im),
                0.5f * (zm.re - zk.re)
            };

            dstL[static_cast<size_t> (k)] = xL;
            dstR[static_cast<size_t> (k)] = xR;

            const float pow = 0.5f * (xL.re * xL.re + xL.im * xL.im
                                    + xR.re * xR.re + xR.im * xR.im);
            dstPow[static_cast<size_t> (k)] = flushDenormal (pow);
            dstMag[static_cast<size_t> (k)] = std::sqrt (dstPow[static_cast<size_t> (k)]);
        }

        const int newestIdx = histWriteIdx_;
        histWriteIdx_ = (histWriteIdx_ + 1) % TIME_MED_LEN;

        // Center frame is CENTER_DELAY_FRAMES behind newestIdx
        const int centerIdx = (newestIdx + TIME_MED_LEN - CENTER_DELAY_FRAMES) % TIME_MED_LEN;
        const int prevIdx   = (centerIdx + TIME_MED_LEN - 1) % TIME_MED_LEN;

        const auto& cL   = histL_[static_cast<size_t> (centerIdx)];
        const auto& cR   = histR_[static_cast<size_t> (centerIdx)];
        const auto& cMag = histMag_[static_cast<size_t> (centerIdx)];
        const auto& cPow = histPow_[static_cast<size_t> (centerIdx)];
        const auto& pMag = histMag_[static_cast<size_t> (prevIdx)];

        // 3. Precompute log(power + eps) for fast local & broadband spectral flatness
        constexpr float kEps = 1.0e-12f;
        float broadLogSum = 0.0f;
        float broadPowSum = 0.0f;
        float fluxPosSum  = 0.0f;
        float magSum      = 0.0f;

        for (int k = 0; k < NUM_BINS; ++k)
        {
            const float pw = cPow[static_cast<size_t> (k)];
            const float lp = std::log (pw + kEps);
            logPow_[static_cast<size_t> (k)] = lp;

            if (k >= 1 && k < NUM_BINS - 1)
            {
                broadLogSum += lp;
                broadPowSum += pw;
            }

            const float mc = cMag[static_cast<size_t> (k)];
            const float mp = pMag[static_cast<size_t> (k)];
            magSum += mc;
            if (mc > mp)
                fluxPosSum += (mc - mp);
        }

        const float invBroadBins = 1.0f / static_cast<float> (NUM_BINS - 2);
        const float broadGeoMean = std::exp (broadLogSum * invBroadBins);
        const float broadArith   = broadPowSum * invBroadBins;
        const float sfBroad      = (broadArith > 1.0e-11f)
                                       ? std::clamp (broadGeoMean / (broadArith + kEps), 0.0f, 1.0f)
                                       : 0.0f;

        const float fluxNorm = (magSum > 1.0e-9f)
                                   ? std::clamp (fluxPosSum / (magSum + kEps), 0.0f, 1.0f)
                                   : 0.0f;

        // Positive spectral flux boosts transient weight only when flux exceeds stationary noise floor
        const float fluxExcess = std::clamp ((fluxNorm - 0.35f) / 0.45f, 0.0f, 1.0f);
        const float onsetBoost = 1.0f + 2.5f * fluxExcess;

        // Mask sharpness & noise routing gain from user parameters
        const float sharpness = 1.5f + 2.5f * separation_;
        const float noiseGain = 4.5f * noiseSensitivity_;

        // 4. Per-bin sliding median HPSS + local spectral flatness 3-way soft masking
        std::array<float, FREQ_MED_LEN> freqWin {};

        for (int k = 0; k < NUM_BINS; ++k)
        {
            // 4a. Temporal median across TIME_MED_LEN = 5 frames
            const float hMed = median5 (
                histMag_[0][static_cast<size_t> (k)],
                histMag_[1][static_cast<size_t> (k)],
                histMag_[2][static_cast<size_t> (k)],
                histMag_[3][static_cast<size_t> (k)],
                histMag_[4][static_cast<size_t> (k)]);

            // 4b. Frequency median across FREQ_MED_LEN = 13 adjacent bins (edge-clamped)
            //     and local spectral flatness over the same 13-bin neighborhood
            float locLogSum = 0.0f;
            float locPowSum = 0.0f;
            for (int off = -FREQ_MED_HALF; off <= FREQ_MED_HALF; ++off)
            {
                const int idx = std::clamp (k + off, 0, NUM_BINS - 1);
                freqWin[static_cast<size_t> (off + FREQ_MED_HALF)] = cMag[static_cast<size_t> (idx)];
                locLogSum += logPow_[static_cast<size_t> (idx)];
                locPowSum += cPow[static_cast<size_t> (idx)];
            }

            std::nth_element (freqWin.begin(), freqWin.begin() + FREQ_MED_HALF, freqWin.end());
            const float pMed = freqWin[static_cast<size_t> (FREQ_MED_HALF)];

            constexpr float invLocBins = 1.0f / static_cast<float> (FREQ_MED_LEN);
            const float locArith = locPowSum * invLocBins;
            const float sfLocal  = (locArith > 1.0e-11f)
                                       ? std::clamp (std::exp (locLogSum * invLocBins) / (locArith + kEps), 0.0f, 1.0f)
                                       : 0.0f;

            // Blend local and broadband flatness (local dominates so band-limited/pink noise still tracks)
            const float sfBlend = 0.75f * sfLocal + 0.25f * sfBroad;

            const float hpSum = hMed + pMed;
            float mT = 0.0f;
            float mH = 0.0f;
            float mN = 1.0f;

            if (hpSum > 1.0e-11f)
            {
                const float invHp = 1.0f / hpSum;
                const float hNorm = hMed * invHp; // ~1 for tonal, ~0 for impulse, ~0.5 for stationary noise
                const float pNorm = pMed * invHp; // ~0 for tonal, ~1 for impulse, ~0.5 for stationary noise

                // Ambiguity / temporal-spectral stationarity indicator in [0, 1]:
                // Near 0 for pure tones (hNorm ~ 1) and impulses (pNorm ~ 1); near 1 for stationary noise (hNorm ~ pNorm ~ 0.5)
                const float stationarity = 2.0f * std::min (hNorm, pNorm);

                // Tonal weight: requires temporal persistence (hNorm) AND low spectral flatness (peaky)
                const float tonalityFactor = 1.0f - 0.80f * sfBlend;
                const float wH = std::pow (hNorm, sharpness) * std::max (0.05f, tonalityFactor);

                // Transient weight: requires spectral continuity with brief time support (pNorm), boosted on onsets
                const float wT = std::pow (pNorm, sharpness) * onsetBoost;

                // Noise weight: requires stationary broadband character (high stationarity + high flatness)
                const float wN = noiseGain
                               * std::pow (stationarity, 1.5f)
                               * (0.20f + 0.80f * sfBlend)
                               * (1.0f - 0.5f * fluxExcess);

                const float wSum = wT + wH + wN;
                if (wSum > kEps && std::isfinite (wSum))
                {
                    const float invW = 1.0f / wSum;
                    mT = std::clamp (wT * invW, 0.0f, 1.0f);
                    mH = std::clamp (wH * invW, 0.0f, 1.0f);
                    // Enforce exact partition of unity: mT + mH + mN == 1.0f
                    mN = std::max (0.0f, 1.0f - mT - mH);
                }
            }

            maskT_[static_cast<size_t> (k)] = mT;
            maskH_[static_cast<size_t> (k)] = mH;
            maskN_[static_cast<size_t> (k)] = mN;
        }

        // 5. Synthesize and overlap-add each of the 3 stereo streams (T, H, N)
        synthesizeAndOverlapAdd (cL, cR, maskT_, olaTL_, olaTR_);
        synthesizeAndOverlapAdd (cL, cR, maskH_, olaHL_, olaHR_);
        synthesizeAndOverlapAdd (cL, cR, maskN_, olaNL_, olaNR_);
    }

    void synthesizeAndOverlapAdd (const std::array<Complex, NUM_BINS>& specL,
                                  const std::array<Complex, NUM_BINS>& specR,
                                  const std::array<float, NUM_BINS>& mask,
                                  std::array<float, FFT_SIZE>& olaL,
                                  std::array<float, FFT_SIZE>& olaR) noexcept
    {
        // Pack masked stereo spectra into single Hermitian-combined buffer Z = YL + j*YR
        {
            const float m0 = mask[0];
            fftBuf_[0].re = specL[0].re * m0;
            fftBuf_[0].im = specR[0].re * m0;

            const float mNyq = mask[static_cast<size_t> (FFT_SIZE / 2)];
            fftBuf_[static_cast<size_t> (FFT_SIZE / 2)].re = specL[static_cast<size_t> (FFT_SIZE / 2)].re * mNyq;
            fftBuf_[static_cast<size_t> (FFT_SIZE / 2)].im = specR[static_cast<size_t> (FFT_SIZE / 2)].re * mNyq;
        }

        for (int k = 1; k < FFT_SIZE / 2; ++k)
        {
            const float m = mask[static_cast<size_t> (k)];
            const float lRe = specL[static_cast<size_t> (k)].re * m;
            const float lIm = specL[static_cast<size_t> (k)].im * m;
            const float rRe = specR[static_cast<size_t> (k)].re * m;
            const float rIm = specR[static_cast<size_t> (k)].im * m;

            fftBuf_[static_cast<size_t> (k)].re = lRe - rIm;
            fftBuf_[static_cast<size_t> (k)].im = lIm + rRe;

            const size_t mirror = static_cast<size_t> (FFT_SIZE - k);
            fftBuf_[mirror].re = lRe + rIm;
            fftBuf_[mirror].im = rRe - lIm;
        }

        fftInPlace (fftBuf_, true);

        // Shift OLA buffer left by HOP_SIZE, zero new tail, and add windowed synthesis frame
        for (int n = 0; n < FFT_SIZE - HOP_SIZE; ++n)
        {
            const float w = window_[static_cast<size_t> (n)];
            olaL[static_cast<size_t> (n)] = flushDenormal (olaL[static_cast<size_t> (n + HOP_SIZE)]
                                                         + fftBuf_[static_cast<size_t> (n)].re * w);
            olaR[static_cast<size_t> (n)] = flushDenormal (olaR[static_cast<size_t> (n + HOP_SIZE)]
                                                         + fftBuf_[static_cast<size_t> (n)].im * w);
        }

        for (int n = FFT_SIZE - HOP_SIZE; n < FFT_SIZE; ++n)
        {
            const float w = window_[static_cast<size_t> (n)];
            olaL[static_cast<size_t> (n)] = flushDenormal (fftBuf_[static_cast<size_t> (n)].re * w);
            olaR[static_cast<size_t> (n)] = flushDenormal (fftBuf_[static_cast<size_t> (n)].im * w);
        }
    }

    // Parameters & telemetry state
    double sampleRate_       = 48000.0;
    float  separation_       = 0.72f;
    float  noiseSensitivity_ = 0.45f;
    float  envAlpha_         = 0.000347f;

    float smoothPowT_ = 0.0f;
    float smoothPowH_ = 0.0f;
    float smoothPowN_ = 0.0f;

    int hopCounter_   = 0;
    int histWriteIdx_ = 0;

    // Precomputed tables
    std::array<float, FFT_SIZE>        window_ {};
    std::array<uint16_t, FFT_SIZE>     bitRev_ {};
    std::array<float, FFT_SIZE / 2>    twiddleCos_ {};
    std::array<float, FFT_SIZE / 2>    twiddleSin_ {};

    // Time-domain I/O and OLA buffers
    std::array<float, FFT_SIZE> inBufL_ {};
    std::array<float, FFT_SIZE> inBufR_ {};
    std::array<float, FFT_SIZE> olaTL_ {};
    std::array<float, FFT_SIZE> olaTR_ {};
    std::array<float, FFT_SIZE> olaHL_ {};
    std::array<float, FFT_SIZE> olaHR_ {};
    std::array<float, FFT_SIZE> olaNL_ {};
    std::array<float, FFT_SIZE> olaNR_ {};

    // Working FFT & mask buffers
    std::array<Complex, FFT_SIZE> fftBuf_ {};
    std::array<float, NUM_BINS>   logPow_ {};
    std::array<float, NUM_BINS>   maskT_ {};
    std::array<float, NUM_BINS>   maskH_ {};
    std::array<float, NUM_BINS>   maskN_ {};

    // Circular STFT history
    std::array<std::array<Complex, NUM_BINS>, TIME_MED_LEN> histL_ {};
    std::array<std::array<Complex, NUM_BINS>, TIME_MED_LEN> histR_ {};
    std::array<std::array<float, NUM_BINS>, TIME_MED_LEN>   histMag_ {};
    std::array<std::array<float, NUM_BINS>, TIME_MED_LEN>   histPow_ {};
};

} // namespace fossil::dsp
