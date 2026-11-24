#pragma once

#include "../SessionMateTypes.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace sessionmate {

namespace detail {

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kTwoPi = 6.28318530717958647692;

inline float sanitizeAudioSample(float x) noexcept {
    if (!std::isfinite(x)) return 0.0f;
    if (std::fabs(x) < 1.0e-20f) return 0.0f;
    return std::clamp(x, -16.0f, 16.0f);
}

// Fixed-size, zero-allocation, in-place Cooley-Tukey radix-2 FFT with Hann window.
template <size_t N>
class FixedRadix2Fft {
    static_assert((N & (N - 1)) == 0 && N >= 16, "FFT size must be a power of two >= 16");

public:
    FixedRadix2Fft() noexcept { initTables(); }

    void initTables() noexcept {
        if (initialized_) return;

        // Bit-reversal permutation table
        size_t bits = 0;
        for (size_t tmp = N; tmp > 1; tmp >>= 1) ++bits;
        for (size_t i = 0; i < N; ++i) {
            size_t r = 0;
            size_t x = i;
            for (size_t b = 0; b < bits; ++b) {
                r = (r << 1) | (x & 1u);
                x >>= 1;
            }
            bitRev_[i] = static_cast<uint16_t>(r);
        }

        // Twiddle factors for forward FFT: exp(-j * 2*pi * k / N), k = 0 .. N/2 - 1
        for (size_t k = 0; k < N / 2; ++k) {
            const double angle = -kTwoPi * static_cast<double>(k) / static_cast<double>(N);
            twiddleCos_[k] = static_cast<float>(std::cos(angle));
            twiddleSin_[k] = static_cast<float>(std::sin(angle));
        }

        // Hann window
        const double invNm1 = 1.0 / static_cast<double>(N - 1);
        for (size_t i = 0; i < N; ++i) {
            hann_[i] = static_cast<float>(0.5 * (1.0 - std::cos(kTwoPi * static_cast<double>(i) * invNm1)));
        }

        initialized_ = true;
    }

    // Applies Hann window to `input` (size N) and computes in-place forward FFT into re_/im_.
    void forwardRealWindowed(const float* input) noexcept {
        for (size_t i = 0; i < N; ++i) {
            const size_t j = bitRev_[i];
            re_[j] = input[i] * hann_[i];
            im_[j] = 0.0f;
        }

        for (size_t len = 2; len <= N; len <<= 1) {
            const size_t half = len >> 1;
            const size_t step = N / len;
            for (size_t i = 0; i < N; i += len) {
                size_t twIdx = 0;
                for (size_t j = 0; j < half; ++j) {
                    const float wr = twiddleCos_[twIdx];
                    const float wi = twiddleSin_[twIdx];
                    twIdx += step;

                    const size_t u = i + j;
                    const size_t v = u + half;

                    const float vr = re_[v];
                    const float vi = im_[v];
                    const float tr = vr * wr - vi * wi;
                    const float ti = vr * wi + vi * wr;

                    re_[v] = re_[u] - tr;
                    im_[v] = im_[u] - ti;
                    re_[u] += tr;
                    im_[u] += ti;
                }
            }
        }
    }

    float magnitude(size_t bin) const noexcept {
        if (bin >= N / 2) return 0.0f;
        const float r = re_[bin];
        const float im = im_[bin];
        return std::sqrt(r * r + im * im);
    }

    float power(size_t bin) const noexcept {
        if (bin >= N / 2) return 0.0f;
        const float r = re_[bin];
        const float im = im_[bin];
        return r * r + im * im;
    }

private:
    bool initialized_ = false;
    std::array<uint16_t, N> bitRev_ {};
    std::array<float, N / 2> twiddleCos_ {};
    std::array<float, N / 2> twiddleSin_ {};
    std::array<float, N> hann_ {};
    std::array<float, N> re_ {};
    std::array<float, N> im_ {};
};

} // namespace detail

class LiveAudioListener {
public:
    LiveAudioListener() noexcept {
        buildChordTemplates();
        prepare(44100.0);
    }

    void prepare(double sampleRate) noexcept {
        sampleRate_ = (std::isfinite(sampleRate) && sampleRate >= 8000.0) ? sampleRate : 44100.0;
        onsetFft_.initTables();
        chromaFft_.initTables();
        precomputeMidiHarmonicBins();
        reset();
    }

    void reset() noexcept {
        onsetRing_.fill(0.0f);
        onsetHopCounter_ = 0;
        prevOnsetMag_.fill(0.0f);
        hasPrevOnsetMag_ = false;

        osfRing_.fill(0.0f);
        osfWritePos_ = 0;
        osfCount_ = 0;
        hopsSinceTempoUpdate_ = 0;
        fluxMean_ = 0.0f;
        fluxDev_ = 0.05f;
        samplesSinceLastOnset_ = static_cast<int>(sampleRate_);
        onsetRateEma_ = 0.0f;
        onsetBurst_ = 0.0f;

        kalmanBpm_ = 104.0;
        kalmanVar_ = 25.0;
        smoothedTempoConf_ = 0.0f;
        beatPhase_ = 0.0;

        chromaRing_.fill(0.0f);
        chromaHopCounter_ = 0;
        chromaEma_.fill(0.0f);
        longTermChroma_.fill(0.0f);
        longTermChromaEnergy_ = 0.0f;
        hmmLogProb_.fill(0.0f);
        currentChordState_ = 4 * kNumTrackedQualities + 9; // E Minor9 default
        chordConfidence_ = 0.0f;
        detectedKeyRoot_ = 4;
        detectedKeyMinor_ = true;

        fastRmsSq_ = 0.0f;
        slowRmsSq_ = 0.0f;
        energyEstimate_ = 0.0f;
        userActivity_ = 0.0f;
        inputRms_ = 0.0f;

        ListenerTelemetry initTelemetry {};
        initTelemetry.detectedBpm = kalmanBpm_;
        initTelemetry.tempoConfidence = 0.0f;
        initTelemetry.beatPhase = 0.0;
        initTelemetry.detectedChord = makeChordSymbolForState(currentChordState_);
        initTelemetry.chordConfidence = 0.0f;
        initTelemetry.detectedKeyRoot = detectedKeyRoot_;
        initTelemetry.detectedKeyMinor = detectedKeyMinor_;
        initTelemetry.inputRms = 0.0f;
        initTelemetry.userActivity = 0.0f;
        initTelemetry.energyEstimate = 0.0f;
        initTelemetry.onsetDetected = false;

        telemetryBuf_[0] = initTelemetry;
        telemetryBuf_[1] = initTelemetry;
        telemetryReadIdx_.store(0, std::memory_order_release);
    }

    // Audio-thread safe: zero heap allocations, zero locks, finite & denormal safe.
    void processAudioBlock(const float* left, const float* right, int numSamples) noexcept {
        if (numSamples <= 0) return;

        const double dtBlock = static_cast<double>(numSamples) / sampleRate_;
        bool blockOnset = false;
        double sumSq = 0.0;

        for (int i = 0; i < numSamples; ++i) {
            const float l = (left != nullptr) ? detail::sanitizeAudioSample(left[i]) : 0.0f;
            const float r = (right != nullptr) ? detail::sanitizeAudioSample(right[i]) : l;
            const float mono = (left != nullptr && right != nullptr) ? 0.5f * (l + r)
                             : (left != nullptr ? l : r);

            sumSq += static_cast<double>(mono) * static_cast<double>(mono);
            ++samplesSinceLastOnset_;

            // Feed onset 512-sample sliding buffer (256-sample hop)
            onsetRing_[static_cast<size_t>(kOnsetHopSize + onsetHopCounter_)] = mono;
            ++onsetHopCounter_;
            if (onsetHopCounter_ >= kOnsetHopSize) {
                if (processOnsetHop()) {
                    blockOnset = true;
                }
                // Shift upper half into lower half (256 floats = 1 KB)
                for (size_t k = 0; k < kOnsetHopSize; ++k) {
                    onsetRing_[k] = onsetRing_[k + kOnsetHopSize];
                }
                onsetHopCounter_ = 0;
            }

            // Feed chromagram 4096-sample sliding buffer (2048-sample hop)
            chromaRing_[static_cast<size_t>(kChromaHopSize + chromaHopCounter_)] = mono;
            ++chromaHopCounter_;
            if (chromaHopCounter_ >= kChromaHopSize) {
                processChromaHop();
                for (size_t k = 0; k < kChromaHopSize; ++k) {
                    chromaRing_[k] = chromaRing_[k + kChromaHopSize];
                }
                chromaHopCounter_ = 0;
            }
        }

        // Advance beat phase smoothly by block duration
        const double beatsElapsed = (kalmanBpm_ / 60.0) * dtBlock;
        beatPhase_ = wrapUnitPhase(beatPhase_ + beatsElapsed);
        if (blockOnset && smoothedTempoConf_ > 0.20f) {
            // Soft PLL phase lock to detected onset (pull beatPhase_ toward 0.0)
            double phaseErr = beatPhase_;
            if (phaseErr > 0.5) phaseErr -= 1.0;
            const double pllGain = 0.14 * static_cast<double>(smoothedTempoConf_);
            beatPhase_ = wrapUnitPhase(beatPhase_ - pllGain * phaseErr);
        }

        // Subsystem 3 & 4: Dynamic Energy & "Leave Space" User Activity Envelope
        const float blockMeanSq = static_cast<float>(sumSq / static_cast<double>(numSamples));
        const float blockRms = std::sqrt(std::max(0.0f, blockMeanSq));
        inputRms_ = blockRms;

        // Dual-window RMS trackers (~50ms fast, ~1.5s slow)
        const float fastAlpha = timeConstantAlpha(dtBlock, 0.050);
        const float slowAlpha = timeConstantAlpha(dtBlock, 1.500);
        fastRmsSq_ += fastAlpha * (blockMeanSq - fastRmsSq_);
        slowRmsSq_ += slowAlpha * (blockMeanSq - slowRmsSq_);

        // Decay onset rate (~1.5s) and onset burst (~250ms)
        const float onsetRateDecay = static_cast<float>(std::exp(-dtBlock / 1.500));
        onsetRateEma_ *= onsetRateDecay;
        const float burstDecay = static_cast<float>(std::exp(-dtBlock / 0.250));
        onsetBurst_ *= burstDecay;

        // Energy estimate in [0, 1] (~1.5s smooth time constant)
        const float slowRms = std::sqrt(std::max(0.0f, slowRmsSq_));
        const float slowDb = 20.0f * std::log10(std::max(slowRms, 1.0e-7f));
        const float rmsNorm = std::clamp((slowDb + 48.0f) / 42.0f, 0.0f, 1.0f);
        const float rateNorm = std::clamp(onsetRateEma_ / 6.0f, 0.0f, 1.0f);
        const float targetEnergy = std::clamp(0.72f * rmsNorm + 0.28f * rateNorm, 0.0f, 1.0f);
        energyEstimate_ += slowAlpha * (targetEnergy - energyEstimate_);

        // "Leave Space" user activity: fast attack (~35ms), musical release (~600ms)
        // Noise floor at -42 dBFS
        const float fastRms = std::sqrt(std::max(0.0f, fastRmsSq_));
        const float fastDb = 20.0f * std::log10(std::max(fastRms, 1.0e-7f));
        const float rmsAboveFloor = std::clamp((fastDb - (-42.0f)) / 30.0f, 0.0f, 1.0f);
        const float targetActivity = std::clamp(rmsAboveFloor + 0.55f * onsetBurst_, 0.0f, 1.0f);
        const float actAlpha = (targetActivity > userActivity_)
            ? timeConstantAlpha(dtBlock, 0.035)
            : timeConstantAlpha(dtBlock, 0.600);
        userActivity_ += actAlpha * (targetActivity - userActivity_);

        // Publish lock-free telemetry snapshot
        const int writeIdx = 1 - telemetryReadIdx_.load(std::memory_order_relaxed);
        ListenerTelemetry& out = telemetryBuf_[static_cast<size_t>(writeIdx)];
        out.detectedBpm = kalmanBpm_;
        out.tempoConfidence = smoothedTempoConf_;
        out.beatPhase = beatPhase_;
        out.detectedChord = makeChordSymbolForState(currentChordState_);
        out.chordConfidence = chordConfidence_;
        out.detectedKeyRoot = detectedKeyRoot_;
        out.detectedKeyMinor = detectedKeyMinor_;
        out.inputRms = inputRms_;
        out.userActivity = userActivity_;
        out.energyEstimate = energyEstimate_;
        out.onsetDetected = blockOnset;
        telemetryReadIdx_.store(writeIdx, std::memory_order_release);
    }

    ListenerTelemetry getTelemetry() const noexcept {
        const int idx = telemetryReadIdx_.load(std::memory_order_acquire);
        return telemetryBuf_[static_cast<size_t>(idx)];
    }

private:
    static constexpr size_t kOnsetFftSize = 512;
    static constexpr size_t kOnsetHopSize = 256;
    static constexpr size_t kOsfCapacity = 1536; // > 4s up to 96 kHz
    static constexpr int kTempoUpdateIntervalHops = 8;

    static constexpr size_t kChromaFftSize = 4096;
    static constexpr size_t kChromaHopSize = 2048;
    static constexpr int kMinMidiNote = 36; // C2
    static constexpr int kMaxMidiNote = 83; // B5
    static constexpr int kNumMidiNotes = kMaxMidiNote - kMinMidiNote + 1; // 48

    static constexpr int kNumRoots = 12;
    static constexpr int kNumTrackedQualities = 12;
    static constexpr int kNumChordStates = kNumRoots * kNumTrackedQualities; // 144

    static constexpr std::array<ChordQuality, kNumTrackedQualities> kTrackedQualities {
        ChordQuality::Major,
        ChordQuality::Minor,
        ChordQuality::Dominant7,
        ChordQuality::Major7,
        ChordQuality::Minor7,
        ChordQuality::Minor7b5,
        ChordQuality::Diminished,
        ChordQuality::Augmented,
        ChordQuality::Sus4,
        ChordQuality::Minor9,
        ChordQuality::Major9,
        ChordQuality::Dominant9
    };

    struct MidiHarmonicBins {
        float h1 = 0.0f;
        float h2 = 0.0f;
        float h3 = 0.0f;
        int pitchClass = 0;
    };

    static float timeConstantAlpha(double dtSec, double tauSec) noexcept {
        if (tauSec <= 1.0e-6) return 1.0f;
        return static_cast<float>(1.0 - std::exp(-dtSec / tauSec));
    }

    static double wrapUnitPhase(double p) noexcept {
        p = std::fmod(p, 1.0);
        if (p < 0.0) p += 1.0;
        return p;
    }

    void precomputeMidiHarmonicBins() noexcept {
        const double binHz = sampleRate_ / static_cast<double>(kChromaFftSize);
        const double invBinHz = 1.0 / std::max(binHz, 1.0e-6);
        for (int i = 0; i < kNumMidiNotes; ++i) {
            const int midi = kMinMidiNote + i;
            const double freq = 440.0 * std::pow(2.0, (static_cast<double>(midi) - 69.0) / 12.0);
            midiBins_[static_cast<size_t>(i)] = {
                static_cast<float>(freq * invBinHz),
                static_cast<float>(2.0 * freq * invBinHz),
                static_cast<float>(3.0 * freq * invBinHz),
                midi % 12
            };
        }
    }

    void buildChordTemplates() noexcept {
        struct QualitySpec {
            std::array<int, 7> intervals;
            int numTones;
            std::array<float, 5> weights;
            float parsimonyPenalty;
        };

        static constexpr std::array<QualitySpec, kNumTrackedQualities> kSpecs {{
            { { 0, 4, 7, -1, -1, -1, -1 }, 3, { 1.00f, 0.90f, 0.80f, 0.00f, 0.00f }, 0.000f }, // Major
            { { 0, 3, 7, -1, -1, -1, -1 }, 3, { 1.00f, 0.90f, 0.80f, 0.00f, 0.00f }, 0.000f }, // Minor
            { { 0, 4, 7, 10, -1, -1, -1 }, 4, { 1.00f, 0.88f, 0.75f, 0.76f, 0.00f }, 0.015f }, // Dominant7
            { { 0, 4, 7, 11, -1, -1, -1 }, 4, { 1.00f, 0.88f, 0.75f, 0.76f, 0.00f }, 0.015f }, // Major7
            { { 0, 3, 7, 10, -1, -1, -1 }, 4, { 1.00f, 0.88f, 0.75f, 0.76f, 0.00f }, 0.015f }, // Minor7
            { { 0, 3, 6, 10, -1, -1, -1 }, 4, { 1.00f, 0.88f, 0.80f, 0.76f, 0.00f }, 0.018f }, // Minor7b5
            { { 0, 3, 6, -1, -1, -1, -1 }, 3, { 1.00f, 0.90f, 0.85f, 0.00f, 0.00f }, 0.010f }, // Diminished
            { { 0, 4, 8, -1, -1, -1, -1 }, 3, { 1.00f, 0.90f, 0.85f, 0.00f, 0.00f }, 0.010f }, // Augmented
            { { 0, 5, 7, -1, -1, -1, -1 }, 3, { 1.00f, 0.88f, 0.80f, 0.00f, 0.00f }, 0.012f }, // Sus4
            { { 0, 3, 7, 10, 14, -1, -1 }, 5, { 1.00f, 0.86f, 0.70f, 0.72f, 0.62f }, 0.026f }, // Minor9
            { { 0, 4, 7, 11, 14, -1, -1 }, 5, { 1.00f, 0.86f, 0.70f, 0.72f, 0.62f }, 0.026f }, // Major9
            { { 0, 4, 7, 10, 14, -1, -1 }, 5, { 1.00f, 0.86f, 0.70f, 0.72f, 0.62f }, 0.026f }  // Dominant9
        }};

        for (int q = 0; q < kNumTrackedQualities; ++q) {
            qualityIntervals_[static_cast<size_t>(q)] = kSpecs[static_cast<size_t>(q)].intervals;
            qualityNumTones_[static_cast<size_t>(q)] = kSpecs[static_cast<size_t>(q)].numTones;
            qualityParsimony_[static_cast<size_t>(q)] = kSpecs[static_cast<size_t>(q)].parsimonyPenalty;
        }

        for (int root = 0; root < kNumRoots; ++root) {
            for (int q = 0; q < kNumTrackedQualities; ++q) {
                const int stateIdx = root * kNumTrackedQualities + q;
                auto& tmpl = chordTemplates_[static_cast<size_t>(stateIdx)];
                tmpl.fill(0.0f);

                const auto& spec = kSpecs[static_cast<size_t>(q)];
                float normSq = 0.0f;
                for (int t = 0; t < spec.numTones; ++t) {
                    const int pc = (root + spec.intervals[static_cast<size_t>(t)]) % 12;
                    const float w = spec.weights[static_cast<size_t>(t)];
                    tmpl[static_cast<size_t>(pc)] = std::max(tmpl[static_cast<size_t>(pc)], w);
                }
                for (float v : tmpl) normSq += v * v;
                const float invNorm = (normSq > 1.0e-9f) ? (1.0f / std::sqrt(normSq)) : 1.0f;
                for (float& v : tmpl) v *= invNorm;
            }
        }
    }

    ChordSymbol makeChordSymbolForState(int stateIdx) const noexcept {
        const int clamped = std::clamp(stateIdx, 0, kNumChordStates - 1);
        const int root = clamped / kNumTrackedQualities;
        const int q = clamped % kNumTrackedQualities;

        ChordSymbol sym {};
        sym.rootPitchClass = root;
        sym.bassPitchClass = -1;
        sym.quality = kTrackedQualities[static_cast<size_t>(q)];
        sym.valid = true;
        sym.intervals = qualityIntervals_[static_cast<size_t>(q)];
        sym.numTones = qualityNumTones_[static_cast<size_t>(q)];
        return sym;
    }

    // Subsystem 1: Onset spectral flux + 4s OSF autocorrelation + 1D Kalman tempo tracker
    bool processOnsetHop() noexcept {
        onsetFft_.forwardRealWindowed(onsetRing_.data());

        float flux = 0.0f;
        constexpr size_t kNumBins = kOnsetFftSize / 2;
        const float invMaxBin = 1.0f / static_cast<float>(kNumBins - 1);

        for (size_t bin = 1; bin < kNumBins; ++bin) {
            const float mag = std::log1p(10.0f * onsetFft_.magnitude(bin));
            if (hasPrevOnsetMag_) {
                const float diff = mag - prevOnsetMag_[bin];
                if (diff > 0.0f) {
                    // High-frequency-weighted spectral flux emphasizes percussive/pluck onsets
                    const float hfWeight = 0.5f + 1.5f * (static_cast<float>(bin) * invMaxBin);
                    flux += hfWeight * diff;
                }
            }
            prevOnsetMag_[bin] = mag;
        }
        hasPrevOnsetMag_ = true;

        // Adaptive threshold for onset detection
        const float dev = std::fabs(flux - fluxMean_);
        fluxMean_ += 0.06f * (flux - fluxMean_);
        fluxDev_ += 0.06f * (dev - fluxDev_);

        const float osfVal = std::max(0.0f, flux - fluxMean_);
        osfRing_[static_cast<size_t>(osfWritePos_)] = osfVal;
        osfWritePos_ = (osfWritePos_ + 1) % static_cast<int>(kOsfCapacity);
        if (osfCount_ < static_cast<int>(kOsfCapacity)) ++osfCount_;

        bool onset = false;
        const int refractorySamples = static_cast<int>(0.075 * sampleRate_);
        const float threshold = fluxMean_ + 1.40f * fluxDev_ + 0.18f;
        if (flux > threshold && samplesSinceLastOnset_ >= refractorySamples) {
            onset = true;
            samplesSinceLastOnset_ = 0;
            onsetRateEma_ = std::min(16.0f, onsetRateEma_ + 1.0f);
            onsetBurst_ = std::min(1.0f, onsetBurst_ + 0.45f);
        }

        ++hopsSinceTempoUpdate_;
        if (hopsSinceTempoUpdate_ >= kTempoUpdateIntervalHops) {
            hopsSinceTempoUpdate_ = 0;
            updateTempoFromOsf();
        }

        return onset;
    }

    void updateTempoFromOsf() noexcept {
        const double hopRate = sampleRate_ / static_cast<double>(kOnsetHopSize);
        const int windowHops = std::clamp(
            static_cast<int>(std::round(4.0 * hopRate)),
            64,
            static_cast<int>(kOsfCapacity));

        const int minLag = std::max(2, static_cast<int>(std::floor(hopRate * 60.0 / 180.0)));
        const int maxLag = std::min(windowHops / 2, static_cast<int>(std::ceil(hopRate * 60.0 / 60.0)));

        if (osfCount_ < maxLag + 16 || minLag >= maxLag) {
            smoothedTempoConf_ *= 0.96f;
            return;
        }

        const int N = std::min(osfCount_, windowHops);

        // Compute zero-lag energy R[0]
        double r0 = 0.0;
        for (int i = 0; i < N; ++i) {
            const float x = osfAtAge(i);
            r0 += static_cast<double>(x) * static_cast<double>(x);
        }

        if (r0 < 1.0e-3) {
            // Silence or steady drone: gently decay confidence and widen Kalman variance slightly
            smoothedTempoConf_ *= 0.94f;
            kalmanVar_ = std::min(36.0, kalmanVar_ + 0.05);
            return;
        }

        const double invR0 = 1.0 / r0;
        double bestScore = -1.0;
        int bestLag = minLag;
        double sumWeighted = 0.0;
        int numLags = 0;

        // Store raw weighted scores for parabolic peak refinement
        std::array<double, kOsfCapacity / 2 + 2> scores {};

        for (int lag = minLag; lag <= maxLag; ++lag) {
            double acc = 0.0;
            const int limit = N - lag;
            for (int i = 0; i < limit; ++i) {
                acc += static_cast<double>(osfAtAge(i)) * static_cast<double>(osfAtAge(i + lag));
            }
            // Normalize by zero-lag energy and overlap compensation
            const double overlapFactor = static_cast<double>(N) / static_cast<double>(std::max(1, limit));
            const double normCorr = std::clamp(acc * invR0 * std::sqrt(overlapFactor), 0.0, 1.5);

            // Perceptual tempo prior peaking around 112 BPM (sigma = 0.65 octaves)
            const double candBpm = 60.0 * hopRate / static_cast<double>(lag);
            const double logOct = std::log2(candBpm / 112.0);
            const double prior = std::exp(-0.5 * (logOct / 0.65) * (logOct / 0.65));

            const double score = normCorr * (0.45 + 0.55 * prior);
            scores[static_cast<size_t>(lag)] = score;
            sumWeighted += score;
            ++numLags;

            if (score > bestScore) {
                bestScore = score;
                bestLag = lag;
            }
        }

        // Parabolic interpolation around bestLag for sub-hop BPM precision
        double refinedLag = static_cast<double>(bestLag);
        if (bestLag > minLag && bestLag < maxLag) {
            const double yL = scores[static_cast<size_t>(bestLag - 1)];
            const double yC = scores[static_cast<size_t>(bestLag)];
            const double yR = scores[static_cast<size_t>(bestLag + 1)];
            const double denom = (yL - 2.0 * yC + yR);
            if (std::fabs(denom) > 1.0e-9) {
                const double delta = std::clamp(0.5 * (yL - yR) / denom, -0.5, 0.5);
                refinedLag += delta;
            }
        }

        double measuredBpm = std::clamp(60.0 * hopRate / std::max(1.0, refinedLag), 60.0, 180.0);

        // Peak-to-average confidence metric in [0, 1]
        const double meanScore = (numLags > 0) ? (sumWeighted / static_cast<double>(numLags)) : 0.0;
        const double prominence = (meanScore > 1.0e-6) ? ((bestScore / meanScore) - 1.0) : 0.0;
        const float rawConf = std::clamp(
            static_cast<float>(0.60 * std::clamp(bestScore * 1.4, 0.0, 1.0) +
                               0.40 * std::clamp(prominence * 0.45, 0.0, 1.0)),
            0.0f, 1.0f);

        // Octave-confusion guard against current Kalman estimate when locked
        if (smoothedTempoConf_ > 0.35f && std::fabs(measuredBpm - kalmanBpm_) > 24.0) {
            const double doubled = measuredBpm * 2.0;
            const double halved = measuredBpm * 0.5;
            if (doubled <= 180.0 && std::fabs(doubled - kalmanBpm_) < 12.0) {
                measuredBpm = doubled;
            } else if (halved >= 60.0 && std::fabs(halved - kalmanBpm_) < 12.0) {
                measuredBpm = halved;
            }
        }

        // 1D Kalman Filter update
        constexpr double kProcessQ = 0.08;
        const double confSafe = std::max(0.08, static_cast<double>(rawConf));
        const double measR = (1.6 / confSafe) * (1.6 / confSafe);

        const double predVar = kalmanVar_ + kProcessQ;
        const double gainK = predVar / (predVar + measR);
        kalmanBpm_ = std::clamp(kalmanBpm_ + gainK * (measuredBpm - kalmanBpm_), 60.0, 180.0);
        kalmanVar_ = std::max(0.05, (1.0 - gainK) * predVar);

        const float kalmanCertainty = static_cast<float>(1.0 / (1.0 + kalmanVar_ * 0.14));
        const float targetConf = std::clamp(0.55f * rawConf + 0.45f * kalmanCertainty, 0.0f, 1.0f);
        smoothedTempoConf_ += 0.18f * (targetConf - smoothedTempoConf_);
    }

    float osfAtAge(int age) const noexcept {
        int idx = osfWritePos_ - 1 - age;
        if (idx < 0) idx += static_cast<int>(kOsfCapacity);
        return osfRing_[static_cast<size_t>(idx)];
    }

    float sampleSpectralPeak(float fractionalBin) const noexcept {
        const int b0 = static_cast<int>(std::floor(fractionalBin));
        if (b0 < 1 || b0 + 1 >= static_cast<int>(kChromaFftSize / 2)) return 0.0f;
        const float frac = fractionalBin - static_cast<float>(b0);
        const float p0 = chromaFft_.power(static_cast<size_t>(b0));
        const float p1 = chromaFft_.power(static_cast<size_t>(b0 + 1));
        return p0 + frac * (p1 - p0);
    }

    // Subsystem 2: 12-bin HPCP Chromagram + HMM Chord Detector + Krumhansl-Schmuckler Key
    void processChromaHop() noexcept {
        // Check frame RMS to avoid analyzing silence / noise floor
        double sumSq = 0.0;
        for (float s : chromaRing_) sumSq += static_cast<double>(s) * static_cast<double>(s);
        const float frameRms = std::sqrt(static_cast<float>(sumSq / static_cast<double>(kChromaFftSize)));
        if (frameRms < 0.005f) {
            chordConfidence_ *= 0.92f;
            return;
        }

        chromaFft_.forwardRealWindowed(chromaRing_.data());

        // Accumulate 12-bin Harmonic Pitch Class Profile across C2 (36) .. B5 (83)
        std::array<float, 12> rawChroma {};
        for (int i = 0; i < kNumMidiNotes; ++i) {
            const auto& b = midiBins_[static_cast<size_t>(i)];
            const float e1 = sampleSpectralPeak(b.h1);
            const float e2 = sampleSpectralPeak(b.h2);
            const float e3 = sampleSpectralPeak(b.h3);
            // Fundamental + harmonic reinforcement gated by fundamental presence (prevents phantom subharmonics)
            const float noteEnergy = e1 + 0.40f * std::sqrt(e1 * e2) + 0.22f * std::sqrt(e1 * e3);
            rawChroma[static_cast<size_t>(b.pitchClass)] += noteEnergy;
        }

        // L2 normalize raw chromagram
        float normSq = 0.0f;
        for (float v : rawChroma) normSq += v * v;
        if (normSq < 1.0e-10f) {
            chordConfidence_ *= 0.92f;
            return;
        }
        const float invNorm = 1.0f / std::sqrt(normSq);
        for (size_t pc = 0; pc < 12; ++pc) {
            rawChroma[pc] *= invNorm;
            chromaEma_[pc] += 0.45f * (rawChroma[pc] - chromaEma_[pc]);
        }

        // Re-normalize smoothed chroma for exact cosine similarity
        float emaNormSq = 0.0f;
        for (float v : chromaEma_) emaNormSq += v * v;
        const float invEmaNorm = (emaNormSq > 1.0e-10f) ? (1.0f / std::sqrt(emaNormSq)) : 1.0f;
        std::array<float, 12> unitChroma {};
        for (size_t pc = 0; pc < 12; ++pc) unitChroma[pc] = chromaEma_[pc] * invEmaNorm;

        // Evaluate cosine similarity against all 144 chord templates
        std::array<float, kNumChordStates> cosSim {};
        float bestSim = -1.0f;
        float secondBestSim = -1.0f;
        for (int s = 0; s < kNumChordStates; ++s) {
            const auto& tmpl = chordTemplates_[static_cast<size_t>(s)];
            float dot = 0.0f;
            for (size_t pc = 0; pc < 12; ++pc) dot += unitChroma[pc] * tmpl[pc];
            const int q = s % kNumTrackedQualities;
            const float score = dot - qualityParsimony_[static_cast<size_t>(q)];
            cosSim[static_cast<size_t>(s)] = score;
            if (score > bestSim) {
                secondBestSim = bestSim;
                bestSim = score;
            } else if (score > secondBestSim) {
                secondBestSim = score;
            }
        }

        // Hidden Markov Model (HMM) log-domain forward/Viterbi update with self-transition stay bonus
        float prevMax = hmmLogProb_[0];
        for (int s = 1; s < kNumChordStates; ++s) {
            if (hmmLogProb_[static_cast<size_t>(s)] > prevMax) {
                prevMax = hmmLogProb_[static_cast<size_t>(s)];
            }
        }

        constexpr float kEmissionScale = 6.0f;
        constexpr float kStayBonus = 0.85f;
        constexpr float kSwitchPenalty = -0.35f;

        float newMax = -1.0e9f;
        int bestState = currentChordState_;
        std::array<float, kNumChordStates> nextLogProb {};

        for (int s = 0; s < kNumChordStates; ++s) {
            const float stayScore = hmmLogProb_[static_cast<size_t>(s)] + kStayBonus;
            const float switchScore = prevMax + kSwitchPenalty;
            const float val = kEmissionScale * cosSim[static_cast<size_t>(s)] + std::max(stayScore, switchScore);
            nextLogProb[static_cast<size_t>(s)] = val;
            if (val > newMax) {
                newMax = val;
                bestState = s;
            }
        }

        // Renormalize log probabilities to prevent overflow/underflow
        for (int s = 0; s < kNumChordStates; ++s) {
            hmmLogProb_[static_cast<size_t>(s)] = std::max(-30.0f, nextLogProb[static_cast<size_t>(s)] - newMax);
        }

        currentChordState_ = bestState;
        const float margin = std::max(0.0f, bestSim - secondBestSim);
        const float targetConf = std::clamp(0.75f * std::clamp(bestSim, 0.0f, 1.0f) + 0.25f * std::clamp(margin * 5.0f, 0.0f, 1.0f), 0.0f, 1.0f);
        chordConfidence_ += 0.25f * (targetConf - chordConfidence_);

        // Long-term chromagram accumulation for Krumhansl-Schmuckler key estimation
        const double hopSec = static_cast<double>(kChromaHopSize) / sampleRate_;
        const float keyAlpha = timeConstantAlpha(hopSec, 8.0);
        for (size_t pc = 0; pc < 12; ++pc) {
            longTermChroma_[pc] += keyAlpha * (unitChroma[pc] - longTermChroma_[pc]);
        }
        longTermChromaEnergy_ = std::min(1.0f, longTermChromaEnergy_ + static_cast<float>(hopSec * 0.5));
        if (longTermChromaEnergy_ > 0.25f) {
            updateKeyEstimate();
        }
    }

    void updateKeyEstimate() noexcept {
        // Krumhansl-Kessler tonal hierarchy profiles
        static constexpr std::array<float, 12> kMajorProfile {
            6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f,
            2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f
        };
        static constexpr std::array<float, 12> kMinorProfile {
            6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f,
            2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f
        };

        auto pearsonCorr = [&](int root, const std::array<float, 12>& profile) noexcept -> float {
            float meanX = 0.0f;
            float meanY = 0.0f;
            for (int i = 0; i < 12; ++i) {
                meanX += longTermChroma_[static_cast<size_t>((root + i) % 12)];
                meanY += profile[static_cast<size_t>(i)];
            }
            meanX /= 12.0f;
            meanY /= 12.0f;

            float num = 0.0f;
            float denX = 0.0f;
            float denY = 0.0f;
            for (int i = 0; i < 12; ++i) {
                const float dx = longTermChroma_[static_cast<size_t>((root + i) % 12)] - meanX;
                const float dy = profile[static_cast<size_t>(i)] - meanY;
                num += dx * dy;
                denX += dx * dx;
                denY += dy * dy;
            }
            const float denom = std::sqrt(denX * denY);
            return (denom > 1.0e-9f) ? (num / denom) : 0.0f;
        };

        float bestCorr = -2.0f;
        int bestRoot = detectedKeyRoot_;
        bool bestMinor = detectedKeyMinor_;

        for (int r = 0; r < 12; ++r) {
            float corrMaj = pearsonCorr(r, kMajorProfile);
            if (r == detectedKeyRoot_ && !detectedKeyMinor_) corrMaj += 0.03f; // hysteresis
            if (corrMaj > bestCorr) {
                bestCorr = corrMaj;
                bestRoot = r;
                bestMinor = false;
            }

            float corrMin = pearsonCorr(r, kMinorProfile);
            if (r == detectedKeyRoot_ && detectedKeyMinor_) corrMin += 0.03f; // hysteresis
            if (corrMin > bestCorr) {
                bestCorr = corrMin;
                bestRoot = r;
                bestMinor = true;
            }
        }

        detectedKeyRoot_ = bestRoot;
        detectedKeyMinor_ = bestMinor;
    }

    double sampleRate_ = 44100.0;

    // Subsystem 1: Onset FFT + OSF + Kalman Tempo Tracker
    detail::FixedRadix2Fft<kOnsetFftSize> onsetFft_ {};
    std::array<float, kOnsetFftSize> onsetRing_ {};
    int onsetHopCounter_ = 0;
    std::array<float, kOnsetFftSize / 2> prevOnsetMag_ {};
    bool hasPrevOnsetMag_ = false;

    std::array<float, kOsfCapacity> osfRing_ {};
    int osfWritePos_ = 0;
    int osfCount_ = 0;
    int hopsSinceTempoUpdate_ = 0;
    float fluxMean_ = 0.0f;
    float fluxDev_ = 0.05f;
    int samplesSinceLastOnset_ = 44100;
    float onsetRateEma_ = 0.0f;
    float onsetBurst_ = 0.0f;

    double kalmanBpm_ = 104.0;
    double kalmanVar_ = 25.0;
    float smoothedTempoConf_ = 0.0f;
    double beatPhase_ = 0.0;

    // Subsystem 2: Chromagram + HMM Chord Detector + Key Estimator
    detail::FixedRadix2Fft<kChromaFftSize> chromaFft_ {};
    std::array<float, kChromaFftSize> chromaRing_ {};
    int chromaHopCounter_ = 0;
    std::array<MidiHarmonicBins, kNumMidiNotes> midiBins_ {};
    std::array<float, 12> chromaEma_ {};
    std::array<float, 12> longTermChroma_ {};
    float longTermChromaEnergy_ = 0.0f;

    std::array<std::array<float, 12>, kNumChordStates> chordTemplates_ {};
    std::array<std::array<int, 7>, kNumTrackedQualities> qualityIntervals_ {};
    std::array<int, kNumTrackedQualities> qualityNumTones_ {};
    std::array<float, kNumTrackedQualities> qualityParsimony_ {};
    std::array<float, kNumChordStates> hmmLogProb_ {};
    int currentChordState_ = 4 * kNumTrackedQualities + 9;
    float chordConfidence_ = 0.0f;
    int detectedKeyRoot_ = 4;
    bool detectedKeyMinor_ = true;

    // Subsystem 3 & 4: Energy & User Activity
    float fastRmsSq_ = 0.0f;
    float slowRmsSq_ = 0.0f;
    float energyEstimate_ = 0.0f;
    float userActivity_ = 0.0f;
    float inputRms_ = 0.0f;

    // Lock-free double-buffered telemetry snapshot
    std::array<ListenerTelemetry, 2> telemetryBuf_ {};
    std::atomic<int> telemetryReadIdx_ { 0 };
};

} // namespace sessionmate
