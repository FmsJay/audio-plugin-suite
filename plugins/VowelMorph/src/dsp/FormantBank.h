#pragma once

#include "PhoneticTables.h"
#include "SuiteDSP.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace vowelmorph::dsp {

class FormantBank {
public:
    struct ProcessParams {
        FormantTarget target {};
        float breathAir01        = 0.0f;
        float fossilNoiseBoost01 = 0.0f;
        float choirWidth01       = 0.0f;
        float drive01            = 0.0f;
        float mix01              = 1.0f;
        float outputGain         = 1.0f;
        float consonantDip01     = 0.0f;
        float inputEnv01         = 0.0f;
        int   harmonyRootPc      = -1;
        float harmonyDrone01     = 0.0f;
    };

    void prepare(double sampleRate, int maxBlockSize) noexcept {
        (void)maxBlockSize;
        sampleRate_ = std::max(8000.0, std::isfinite(sampleRate) ? sampleRate : 48000.0);

        for (size_t ch = 0; ch < 2; ++ch) {
            for (size_t b = 0; b < 5; ++b) {
                formants_[ch][b].prepare(sampleRate_);
            }
            for (size_t b = 0; b < 3; ++b) {
                breathSvf_[ch][b].prepare(sampleRate_);
            }
            for (size_t b = 0; b < 2; ++b) {
                harmonySvf_[ch][b].prepare(sampleRate_);
            }
            bodyLp_[ch].prepare(sampleRate_);
            bodyLp_[ch].setParameters(suite::dsp::TptSVF::Mode::LowPass, 180.0f, 0.70710678f);
        }

        // DC blocker at ~12 Hz
        dcCoeff_ = static_cast<float>(std::exp(-suite::dsp::kTwoPi * 12.0 / sampleRate_));

        for (size_t b = 0; b < 5; ++b) {
            freqSmooth_[b].prepare(sampleRate_, 10.0f);
            qSmooth_[b].prepare(sampleRate_, 12.0f);
            gainSmooth_[b].prepare(sampleRate_, 10.0f);
        }

        breathSmooth_.prepare(sampleRate_, 15.0f);
        widthSmooth_.prepare(sampleRate_, 15.0f);
        driveSmooth_.prepare(sampleRate_, 12.0f);
        mixSmooth_.prepare(sampleRate_, 12.0f);
        outGainSmooth_.prepare(sampleRate_, 12.0f);
        dipSmooth_.prepare(sampleRate_, 6.0f);
        harmonySmooth_.prepare(sampleRate_, 25.0f);

        reset();
    }

    void reset() noexcept {
        for (size_t ch = 0; ch < 2; ++ch) {
            for (size_t b = 0; b < 5; ++b) {
                formants_[ch][b].reset();
            }
            for (size_t b = 0; b < 3; ++b) {
                breathSvf_[ch][b].reset();
            }
            for (size_t b = 0; b < 2; ++b) {
                harmonySvf_[ch][b].reset();
            }
            bodyLp_[ch].reset();
            dcPrevIn_[ch]  = 0.0f;
            dcPrevOut_[ch] = 0.0f;
        }

        const FormantTarget defaults {};
        for (size_t b = 0; b < 5; ++b) {
            freqSmooth_[b].reset(defaults.freqHz[b]);
            qSmooth_[b].reset(defaults.q[b]);
            gainSmooth_[b].reset(defaults.gain[b]);
        }

        breathSmooth_.reset(0.0f);
        widthSmooth_.reset(0.0f);
        driveSmooth_.reset(0.0f);
        mixSmooth_.reset(1.0f);
        outGainSmooth_.reset(1.0f);
        dipSmooth_.reset(0.0f);
        harmonySmooth_.reset(0.0f);

        lastHarmonyRootPc_ = -1;
        subBlockCounter_   = 0;
        rngState_[0]       = 0x6D2B79F5u;
        rngState_[1]       = 0x9E3779B9u;
    }

    void processSample(float inL, float inR, float& outL, float& outR, const ProcessParams& p) noexcept {
        const float cleanL = suite::dsp::clampFinite(inL, -8.0f, 8.0f);
        const float cleanR = suite::dsp::clampFinite(inR, -8.0f, 8.0f);

        // Smooth per-band and global parameters every sample
        std::array<float, 5> curFreq {};
        std::array<float, 5> curQ {};
        std::array<float, 5> curGain {};

        for (size_t b = 0; b < 5; ++b) {
            curFreq[b] = freqSmooth_[b].process(p.target.freqHz[b]);
            curQ[b]    = qSmooth_[b].process(p.target.q[b]);
            curGain[b] = gainSmooth_[b].process(p.target.gain[b]);
        }

        const float breathAmt  = breathSmooth_.process(std::clamp(p.breathAir01, 0.0f, 1.0f));
        const float widthAmt   = widthSmooth_.process(std::clamp(p.choirWidth01, 0.0f, 1.0f));
        const float driveAmt   = driveSmooth_.process(std::clamp(p.drive01, 0.0f, 1.0f));
        const float mixAmt     = mixSmooth_.process(std::clamp(p.mix01, 0.0f, 1.0f));
        const float outGain    = outGainSmooth_.process(suite::dsp::clampFinite(p.outputGain, 0.0f, 8.0f));
        const float dipAmt     = dipSmooth_.process(std::clamp(p.consonantDip01, 0.0f, 1.0f));
        const float harmAmt    = harmonySmooth_.process(
            (p.harmonyRootPc >= 0) ? std::clamp(p.harmonyDrone01, 0.0f, 1.0f) : 0.0f);

        // Update SVF coefficients every 4 samples (zipper-free and light on std::tan)
        if ((subBlockCounter_ & 3u) == 0u) {
            const float maxHz = std::min(14000.0f, static_cast<float>(0.44 * sampleRate_));
            static constexpr std::array<float, 5> kStereoOffsets { 0.0f, 0.032f, -0.045f, 0.038f, -0.020f };

            for (size_t b = 0; b < 5; ++b) {
                const float detune = kStereoOffsets[b] * widthAmt;
                const float fL = std::clamp(curFreq[b] * (1.0f + detune), 60.0f, maxHz);
                const float fR = std::clamp(curFreq[b] * (1.0f - detune), 60.0f, maxHz);
                const float q  = std::clamp(curQ[b], 1.0f, 26.0f);
                formants_[0][b].setParameters(suite::dsp::TptSVF::Mode::BandPass, fL, q);
                formants_[1][b].setParameters(suite::dsp::TptSVF::Mode::BandPass, fR, q);
            }

            // Tune 3-band breath aspiration filters to live F3, F4, F5
            for (size_t b = 0; b < 3; ++b) {
                const size_t srcBand = b + 2;
                const float detune = kStereoOffsets[srcBand] * widthAmt;
                const float fL = std::clamp(curFreq[srcBand] * (1.0f + detune), 400.0f, maxHz);
                const float fR = std::clamp(curFreq[srcBand] * (1.0f - detune), 400.0f, maxHz);
                const float breathQ = 4.0f + static_cast<float>(b) * 1.2f;
                breathSvf_[0][b].setParameters(suite::dsp::TptSVF::Mode::BandPass, fL, breathQ);
                breathSvf_[1][b].setParameters(suite::dsp::TptSVF::Mode::BandPass, fR, breathQ);
            }

            // Tune 2-band HarmonyLink chord choir resonators (root + 5th in 220..660 Hz vocal register)
            if (p.harmonyRootPc >= 0 && p.harmonyRootPc != lastHarmonyRootPc_) {
                lastHarmonyRootPc_ = p.harmonyRootPc;
                const int pc = ((p.harmonyRootPc % 12) + 12) % 12;
                float rootHz = 261.625565f * std::pow(2.0f, static_cast<float>(pc) / 12.0f);
                if (rootHz > 440.0f) {
                    rootHz *= 0.5f;
                }
                const float fifthHz = rootHz * 1.49830708f; // Just/equal-tempered 5th
                constexpr float kHarmonyQ = 14.0f;
                harmonySvf_[0][0].setParameters(suite::dsp::TptSVF::Mode::BandPass, rootHz, kHarmonyQ);
                harmonySvf_[1][0].setParameters(suite::dsp::TptSVF::Mode::BandPass, rootHz * 1.003f, kHarmonyQ);
                harmonySvf_[0][1].setParameters(suite::dsp::TptSVF::Mode::BandPass, fifthHz, kHarmonyQ);
                harmonySvf_[1][1].setParameters(suite::dsp::TptSVF::Mode::BandPass, fifthHz * 0.997f, kHarmonyQ);
            }
        }
        ++subBlockCounter_;

        // Gentle glottal warmth/drive before the formant tract
        const float driveGain = 1.0f + 3.0f * driveAmt;
        const float driveNorm = 1.0f / std::max(1.0f, 1.0f + 1.4f * driveAmt);
        const float excL = (driveAmt > 1.0e-4f)
            ? (cleanL * (1.0f - driveAmt) + suite::dsp::fastTanh(cleanL * driveGain) * driveNorm * driveAmt)
            : cleanL;
        const float excR = (driveAmt > 1.0e-4f)
            ? (cleanR * (1.0f - driveAmt) + suite::dsp::fastTanh(cleanR * driveGain) * driveNorm * driveAmt)
            : cleanR;

        // Consonant closure dip attenuates F1-F3 body during syllable transitions
        const std::array<float, 5> dipAtten {
            1.0f - 0.65f * dipAmt,
            1.0f - 0.65f * dipAmt,
            1.0f - 0.35f * dipAmt,
            1.0f,
            1.0f
        };

        // 5-band parallel ZDF SVF formant sum (unity-peak normalized by 1/Q, alternating Klatt polarity)
        float wetL = 0.0f;
        float wetR = 0.0f;
        for (size_t b = 0; b < 5; ++b) {
            const float sign = (b % 2 == 0) ? 1.0f : -1.0f;
            const float bandScale = sign * curGain[b] * dipAtten[b] / std::max(1.0f, curQ[b]);
            wetL += formants_[0][b].process(excL) * bandScale;
            wetR += formants_[1][b].process(excR) * bandScale;
        }

        // Subtle low-end body foundation (~180 Hz LP) so basslines preserve sub weight
        const float bodyScale = 0.16f * (1.0f - 0.45f * dipAmt);
        wetL += bodyLp_[0].process(excL) * bodyScale;
        wetR += bodyLp_[1].process(excR) * bodyScale;

        // Breath aspiration engine (upper vocal tract turbulence on F3, F4, F5)
        const float fossilBoost = std::clamp(p.fossilNoiseBoost01, 0.0f, 1.0f);
        const float totalBreath = std::clamp(breathAmt + fossilBoost * 0.55f + dipAmt * 0.22f, 0.0f, 1.5f);
        if (totalBreath > 1.0e-4f) {
            const float rawNoiseL = nextNoise(0);
            const float rawNoiseR = nextNoise(1);
            const float monoNoise = 0.5f * (rawNoiseL + rawNoiseR);
            const float nL = monoNoise + widthAmt * (rawNoiseL - monoNoise);
            const float nR = monoNoise + widthAmt * (rawNoiseR - monoNoise);

            const float envScale = 0.18f + 0.82f * std::clamp(p.inputEnv01 * 2.8f, 0.0f, 1.0f);
            const float puffBoost = 1.0f + 0.65f * dipAmt;
            const float breathGain = totalBreath * envScale * 0.14f;

            float bSumL = 0.0f;
            float bSumR = 0.0f;
            constexpr std::array<float, 3> kBreathWeights { 0.45f, 0.35f, 0.25f };
            for (size_t b = 0; b < 3; ++b) {
                const float w = kBreathWeights[b] * (b >= 1 ? puffBoost : 1.0f) / (4.0f + static_cast<float>(b) * 1.2f);
                bSumL += breathSvf_[0][b].process(nL) * w;
                bSumR += breathSvf_[1][b].process(nR) * w;
            }
            wetL += bSumL * breathGain;
            wetR += bSumR * breathGain;
        }

        // HarmonyLink chord choir resonance (root + 5th vocal shimmer)
        if (harmAmt > 1.0e-4f) {
            constexpr float kHarmonyScale = 0.28f / 14.0f;
            const float hL = (harmonySvf_[0][0].process(excL) + 0.72f * harmonySvf_[0][1].process(excL)) * kHarmonyScale;
            const float hR = (harmonySvf_[1][0].process(excR) + 0.72f * harmonySvf_[1][1].process(excR)) * kHarmonyScale;
            wetL += hL * harmAmt;
            wetR += hR * harmAmt;
        }

        // Gentle DC blocker on wet path
        wetL = processDcBlock(0, wetL);
        wetR = processDcBlock(1, wetR);

        // Soft ceiling on hot formant resonances
        wetL = suite::dsp::fastTanh(wetL * 0.9f) * 1.1111111f;
        wetR = suite::dsp::fastTanh(wetR * 0.9f) * 1.1111111f;

        // Wet/dry mix and output gain
        const float mixedL = (cleanL * (1.0f - mixAmt) + wetL * mixAmt) * outGain;
        const float mixedR = (cleanR * (1.0f - mixAmt) + wetR * mixAmt) * outGain;

        outL = suite::dsp::clampFinite(suite::dsp::flushDenormal(mixedL), -4.0f, 4.0f);
        outR = suite::dsp::clampFinite(suite::dsp::flushDenormal(mixedR), -4.0f, 4.0f);
    }

private:
    float nextNoise(size_t ch) noexcept {
        uint32_t x = rngState_[ch];
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        rngState_[ch] = x;
        // Convert to [-1.0f, +1.0f]
        return static_cast<float>(static_cast<int32_t>(x)) * 4.6566128730773926e-10f;
    }

    float processDcBlock(size_t ch, float x) noexcept {
        const float y = x - dcPrevIn_[ch] + dcCoeff_ * dcPrevOut_[ch];
        dcPrevIn_[ch]  = suite::dsp::flushDenormal(x);
        dcPrevOut_[ch] = suite::dsp::flushDenormal(y);
        return dcPrevOut_[ch];
    }

    double sampleRate_ = 48000.0;
    float  dcCoeff_    = 0.9984f;

    suite::dsp::TptSVF formants_[2][5] {};
    suite::dsp::TptSVF breathSvf_[2][3] {};
    suite::dsp::TptSVF harmonySvf_[2][2] {};
    suite::dsp::TptSVF bodyLp_[2] {};

    suite::dsp::OnePoleSmoother freqSmooth_[5] {};
    suite::dsp::OnePoleSmoother qSmooth_[5] {};
    suite::dsp::OnePoleSmoother gainSmooth_[5] {};

    suite::dsp::OnePoleSmoother breathSmooth_ {};
    suite::dsp::OnePoleSmoother widthSmooth_ {};
    suite::dsp::OnePoleSmoother driveSmooth_ {};
    suite::dsp::OnePoleSmoother mixSmooth_ {};
    suite::dsp::OnePoleSmoother outGainSmooth_ {};
    suite::dsp::OnePoleSmoother dipSmooth_ {};
    suite::dsp::OnePoleSmoother harmonySmooth_ {};

    float    dcPrevIn_[2]  { 0.0f, 0.0f };
    float    dcPrevOut_[2] { 0.0f, 0.0f };
    uint32_t rngState_[2]  { 0x6D2B79F5u, 0x9E3779B9u };
    uint32_t subBlockCounter_  = 0;
    int      lastHarmonyRootPc_ = -1;
};

} // namespace vowelmorph::dsp
