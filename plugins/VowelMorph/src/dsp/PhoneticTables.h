#pragma once

#include "SuiteDSP.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace vowelmorph::dsp {

struct FormantTarget {
    std::array<float, 5> freqHz { 500.0f, 1400.0f, 2500.0f, 3500.0f, 4500.0f };
    std::array<float, 5> q      { 6.0f, 8.0f, 10.0f, 12.0f, 12.0f };
    std::array<float, 5> gain   { 1.0f, 0.7f, 0.45f, 0.25f, 0.15f };
    float brightness            = 0.5f;
    int   nearestPhoneme        = 4;
};

struct PhonemeDef {
    const char* code;
    const char* ipa;
    float x;
    float y;
    std::array<float, 5> fHz;
    std::array<float, 5> bwHz;
    std::array<float, 5> gainDb;
};

// Authentic IPA 2D vowel space:
//   x: 0.0 (front) .. 1.0 (back)
//   y: 0.0 (open)  .. 1.0 (close)
// Ordered to match vowelmorph::PhonemeId (0..9).
inline const std::array<PhonemeDef, 10>& getPhonemeTable() noexcept {
    static constexpr std::array<PhonemeDef, 10> kTable {{
        // 0: EE /i/ "beet"
        { "EE", "/i/",  0.10f, 0.92f,
          { 280.0f, 2300.0f, 3000.0f, 3700.0f, 4600.0f },
          {  45.0f,   95.0f,  150.0f,  210.0f,  280.0f },
          {   0.0f,   -5.0f,   -9.0f,  -15.0f,  -21.0f } },

        // 1: IH /I/ "bit"
        { "IH", "/I/",  0.22f, 0.76f,
          { 400.0f, 2000.0f, 2600.0f, 3600.0f, 4500.0f },
          {  55.0f,   95.0f,  145.0f,  205.0f,  270.0f },
          {   0.0f,   -4.5f,   -9.5f,  -15.5f,  -21.5f } },

        // 2: EH /E/ "bet"
        { "EH", "/E/",  0.26f, 0.52f,
          { 550.0f, 1800.0f, 2500.0f, 3500.0f, 4400.0f },
          {  65.0f,  100.0f,  145.0f,  200.0f,  265.0f },
          {   0.0f,   -3.5f,   -8.5f,  -14.5f,  -20.5f } },

        // 3: AE /ae/ "bat"
        { "AE", "/ae/", 0.24f, 0.24f,
          { 690.0f, 1660.0f, 2490.0f, 3450.0f, 4350.0f },
          {  75.0f,  105.0f,  150.0f,  200.0f,  260.0f },
          {   0.0f,   -2.5f,   -7.5f,  -14.0f,  -20.0f } },

        // 4: AH /a/ "father"
        { "AH", "/a/",  0.52f, 0.12f,
          { 730.0f, 1090.0f, 2440.0f, 3400.0f, 4300.0f },
          {  80.0f,   95.0f,  140.0f,  195.0f,  255.0f },
          {   0.0f,   -2.0f,   -9.0f,  -15.0f,  -21.0f } },

        // 5: AW /c/ "bought"
        { "AW", "/c/",  0.78f, 0.28f,
          { 570.0f,  840.0f, 2410.0f, 3350.0f, 4250.0f },
          {  70.0f,   85.0f,  140.0f,  195.0f,  255.0f },
          {   0.0f,   -3.0f,  -11.0f,  -17.0f,  -23.0f } },

        // 6: OH /o/ "boat"
        { "OH", "/o/",  0.86f, 0.58f,
          { 450.0f,  800.0f, 2550.0f, 3400.0f, 4300.0f },
          {  60.0f,   80.0f,  145.0f,  200.0f,  260.0f },
          {   0.0f,   -4.0f,  -12.5f,  -18.5f,  -24.0f } },

        // 7: OO /u/ "boot"
        { "OO", "/u/",  0.90f, 0.88f,
          { 300.0f,  870.0f, 2240.0f, 3300.0f, 4200.0f },
          {  50.0f,   80.0f,  135.0f,  195.0f,  255.0f },
          {   0.0f,   -6.5f,  -14.5f,  -20.0f,  -25.0f } },

        // 8: UH /^/ "but"
        { "UH", "/^/",  0.52f, 0.48f,
          { 520.0f, 1200.0f, 2400.0f, 3400.0f, 4300.0f },
          {  65.0f,   90.0f,  140.0f,  195.0f,  260.0f },
          {   0.0f,   -3.5f,  -10.0f,  -16.0f,  -22.0f } },

        // 9: MM /m/ closed-lip nasal hum (F2..F5 strongly attenuated)
        { "MM", "/m/",  0.88f, 0.96f,
          { 250.0f, 1000.0f, 2200.0f, 3100.0f, 4000.0f },
          {  45.0f,  110.0f,  170.0f,  220.0f,  280.0f },
          {  -1.0f,  -13.0f,  -18.0f,  -24.0f,  -28.0f } }
    }};
    return kTable;
}

inline std::pair<float, float> getPhonemeXY(int phonemeIdx) noexcept {
    const auto& table = getPhonemeTable();
    const int idx = std::clamp(phonemeIdx, 0, static_cast<int>(table.size()) - 1);
    const auto& p = table[static_cast<size_t>(idx)];
    return { p.x, p.y };
}

inline int findNearestPhoneme(float x, float y) noexcept {
    const auto& table = getPhonemeTable();
    const float cx = suite::dsp::clampFinite(x, 0.0f, 1.0f);
    const float cy = suite::dsp::clampFinite(y, 0.0f, 1.0f);

    int bestIdx = 0;
    float bestDistSq = 1.0e9f;
    for (size_t i = 0; i < table.size(); ++i) {
        const float dx = cx - table[i].x;
        const float dy = cy - table[i].y;
        const float d2 = dx * dx + dy * dy;
        if (d2 < bestDistSq) {
            bestDistSq = d2;
            bestIdx = static_cast<int>(i);
        }
    }
    return bestIdx;
}

namespace detail {

struct TractProfileSpec {
    std::array<float, 5> freqScale;
    float qScale;
    std::array<float, 5> gainDbOffset;
};

inline constexpr std::array<TractProfileSpec, 8> kTractProfiles {{
    // 0: Bass (~0.78x)
    { { 0.78f, 0.78f, 0.80f, 0.82f, 0.84f }, 0.95f, {  1.0f,  0.0f, -1.0f, -2.0f, -2.5f } },
    // 1: Tenor (~0.92x)
    { { 0.92f, 0.92f, 0.93f, 0.94f, 0.95f }, 1.00f, {  0.5f,  0.0f,  0.0f, -0.5f, -1.0f } },
    // 2: AltoChoir (~1.05x)
    { { 1.05f, 1.05f, 1.05f, 1.04f, 1.03f }, 1.05f, {  0.0f,  0.5f,  0.5f,  0.0f,  0.0f } },
    // 3: Soprano (~1.22x)
    { { 1.22f, 1.22f, 1.20f, 1.18f, 1.15f }, 1.12f, { -0.5f,  1.0f,  1.5f,  1.0f,  0.5f } },
    // 4: Child (~1.38x)
    { { 1.38f, 1.38f, 1.34f, 1.28f, 1.22f }, 1.08f, { -1.0f,  1.0f,  2.0f,  1.5f,  1.0f } },
    // 5: WhisperAir (~1.08x + broader Q & boosted F3-F5)
    { { 1.08f, 1.08f, 1.08f, 1.08f, 1.08f }, 0.72f, { -2.0f,  0.0f,  3.5f,  4.5f,  5.0f } },
    // 6: TalkBoxSynth (~1.00x + sharper Q & boosted F2/F3)
    { { 1.00f, 1.00f, 1.00f, 1.00f, 1.00f }, 1.45f, {  0.5f,  3.5f,  3.0f,  0.5f, -1.0f } },
    // 7: AlienGiant (~0.64x + exotic non-uniform F2/F3 stretch + high Q)
    { { 0.64f, 0.76f, 1.14f, 0.92f, 1.22f }, 1.55f, {  1.5f,  2.0f,  2.5f,  1.0f,  0.5f } }
}};

} // namespace detail

inline FormantTarget computeFormants(float vowelX,
                                     float vowelY,
                                     float jawOpen01,
                                     float throatSizeNorm,
                                     int tractProfileIdx,
                                     float resonance01,
                                     double sampleRate = 48000.0) noexcept {
    const auto& table = getPhonemeTable();
    const float x = suite::dsp::clampFinite(vowelX, 0.0f, 1.0f);
    const float y = suite::dsp::clampFinite(vowelY, 0.0f, 1.0f);
    const float jaw = suite::dsp::clampFinite(jawOpen01, 0.0f, 1.0f);
    const float throat = suite::dsp::clampFinite(throatSizeNorm, -0.5f, 0.5f);
    const float res = suite::dsp::clampFinite(resonance01, 0.0f, 1.0f);
    const double sr = std::max(8000.0, std::isfinite(sampleRate) ? sampleRate : 48000.0);

    const int profileIdx = std::clamp(tractProfileIdx, 0, static_cast<int>(detail::kTractProfiles.size()) - 1);
    const auto& profile = detail::kTractProfiles[static_cast<size_t>(profileIdx)];

    // Smooth Gaussian-localized Shepard inverse-distance interpolation.
    // Hits exact phoneme coordinates when (x, y) coincides with an anchor.
    int exactIdx = -1;
    int nearestIdx = 0;
    float bestDistSq = 1.0e9f;
    std::array<float, 10> weights {};
    float weightSum = 0.0f;

    for (size_t i = 0; i < table.size(); ++i) {
        const float dx = x - table[i].x;
        const float dy = y - table[i].y;
        const float d2 = dx * dx + dy * dy;
        if (d2 < bestDistSq) {
            bestDistSq = d2;
            nearestIdx = static_cast<int>(i);
        }
        if (d2 <= 1.0e-7f) {
            exactIdx = static_cast<int>(i);
            break;
        }
        const float w = std::exp(-d2 * 5.0f) / (d2 * d2 + 1.0e-7f);
        weights[i] = w;
        weightSum += w;
    }

    std::array<float, 5> baseFreq {};
    std::array<float, 5> baseBw {};
    std::array<float, 5> baseGainDb {};

    if (exactIdx >= 0) {
        const auto& p = table[static_cast<size_t>(exactIdx)];
        baseFreq   = p.fHz;
        baseBw     = p.bwHz;
        baseGainDb = p.gainDb;
    } else {
        const float invSum = (weightSum > 1.0e-12f) ? (1.0f / weightSum) : 0.1f;
        std::array<float, 5> logFreqSum {};
        std::array<float, 5> logBwSum {};
        std::array<float, 5> gainDbSum {};

        for (size_t i = 0; i < table.size(); ++i) {
            const float wn = weights[i] * invSum;
            for (size_t b = 0; b < 5; ++b) {
                logFreqSum[b] += wn * std::log(table[i].fHz[b]);
                logBwSum[b]   += wn * std::log(table[i].bwHz[b]);
                gainDbSum[b]  += wn * table[i].gainDb[b];
            }
        }
        for (size_t b = 0; b < 5; ++b) {
            baseFreq[b]   = std::exp(logFreqSum[b]);
            baseBw[b]     = std::exp(logBwSum[b]);
            baseGainDb[b] = gainDbSum[b];
        }
    }

    // Throat size scaling: larger throat (+0.5) lowers formants, smaller (-0.5) raises them
    const float throatScale = std::pow(2.0f, -throat * 1.1f);

    // Jaw opening modulation on F1 and upper-band openness
    const float jawNorm = (jaw - 0.5f) * 2.0f; // -1..+1
    const float jawF1Scale = (jawNorm >= 0.0f) ? (1.0f + 0.32f * jawNorm)
                                               : (1.0f + 0.30f * jawNorm);
    const float closedAmt = std::clamp((0.5f - jaw) * 2.0f, 0.0f, 1.0f);
    const std::array<float, 5> jawGainTrimDb {
        0.0f,
        -1.2f * closedAmt,
        -3.0f * closedAmt,
        -5.5f * closedAmt,
        -8.0f * closedAmt
    };

    // Resonance Q multiplier: 0.55x (warm/broad) .. 2.20x (vocal-synth resonant)
    const float resScale = 0.55f * std::pow(4.0f, res);
    const float maxFreqHz = std::min(14000.0f, static_cast<float>(0.44 * sr));

    FormantTarget target;
    target.nearestPhoneme = nearestIdx;

    for (size_t b = 0; b < 5; ++b) {
        float f = baseFreq[b] * profile.freqScale[b] * throatScale;
        if (b == 0) {
            f *= jawF1Scale;
        }
        f = std::clamp(f, 80.0f, maxFreqHz);

        const float bw = std::max(15.0f, baseBw[b] * profile.freqScale[b] * throatScale);
        const float rawQ = (f / bw) * profile.qScale * resScale;
        const float qClamped = std::clamp(rawQ, 1.5f, 24.0f);

        const float gDb = baseGainDb[b] + profile.gainDbOffset[b] + jawGainTrimDb[b];
        const float gLin = suite::dsp::clampFinite(suite::dsp::dbToGain(gDb, -60.0f), 0.0f, 4.0f);

        target.freqHz[b] = f;
        target.q[b]      = qClamped;
        target.gain[b]   = gLin;
    }

    // Spectral brightness (0..1) from F1/F2/F3 weighted centroid for Fossil Shimmer damping
    const float wSum = target.gain[0] + target.gain[1] + target.gain[2] + 1.0e-5f;
    const float centroid = (target.freqHz[0] * target.gain[0]
                          + target.freqHz[1] * target.gain[1]
                          + target.freqHz[2] * target.gain[2]) / wSum;
    target.brightness = suite::dsp::clampFinite((centroid - 350.0f) / 1850.0f, 0.0f, 1.0f);

    return target;
}

// Analytical magnitude response in dB matching FormantBank's 5-band parallel ZDF SVF
// (unity-peak normalized bandpass with alternating Klatt polarity +1, -1, +1, -1, +1).
inline float evaluateFormantResponseDb(float freqHz,
                                       const FormantTarget& target,
                                       double sampleRate = 48000.0) noexcept {
    const double fs = std::max(8000.0, std::isfinite(sampleRate) ? sampleRate : 48000.0);
    const double f = std::clamp(static_cast<double>(freqHz), 10.0, fs * 0.49);
    const double w = std::tan(suite::dsp::kPi * f / fs);

    double sumRe = 0.0;
    double sumIm = 0.0;

    for (size_t i = 0; i < 5; ++i) {
        const double fc = std::clamp(static_cast<double>(target.freqHz[i]), 20.0, fs * 0.48);
        const double q  = std::max(0.5, static_cast<double>(target.q[i]));
        const double g  = std::tan(suite::dsp::kPi * fc / fs);
        const double k  = 1.0 / q;
        const double sign = (i % 2 == 0) ? 1.0 : -1.0;
        const double scale = sign * static_cast<double>(target.gain[i]) * k;

        const double a = g * g - w * w;
        const double b = k * g * w;
        const double denom = a * a + b * b + 1.0e-18;
        const double gw = g * w;

        sumRe += scale * (b * gw) / denom;
        sumIm += scale * (a * gw) / denom;
    }

    const double mag = std::sqrt(sumRe * sumRe + sumIm * sumIm);
    return suite::dsp::clampFinite(static_cast<float>(suite::dsp::gainToDb(mag, -60.0)), -60.0f, 36.0f);
}

} // namespace vowelmorph::dsp
