#pragma once

#include "MusicTheory.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace harmonyhud {

// Lock-free single-producer single-consumer audio ring buffer.
// Zero heap allocations, zero locks, safe for real-time processBlock().
template <size_t Capacity = 16384>
class SpscAudioFifo {
    static_assert((Capacity & (Capacity - 1)) == 0 && Capacity >= 64,
                  "SpscAudioFifo Capacity must be a power of two >= 64");

public:
    static constexpr size_t kCapacity = Capacity;
    static constexpr size_t kMask = Capacity - 1;

    SpscAudioFifo() noexcept { reset(); }

    void reset() noexcept {
        writeIdx_.store(0, std::memory_order_relaxed);
        readIdx_.store(0, std::memory_order_relaxed);
        buffer_.fill(0.0f);
    }

    size_t availableToRead() const noexcept {
        const size_t w = writeIdx_.load(std::memory_order_acquire);
        const size_t r = readIdx_.load(std::memory_order_relaxed);
        return w - r;
    }

    size_t availableToWrite() const noexcept {
        const size_t w = writeIdx_.load(std::memory_order_relaxed);
        const size_t r = readIdx_.load(std::memory_order_acquire);
        return kCapacity - (w - r);
    }

    bool push(float sample) noexcept {
        const size_t w = writeIdx_.load(std::memory_order_relaxed);
        const size_t r = readIdx_.load(std::memory_order_acquire);
        if (kCapacity - (w - r) == 0)
            return false;
        buffer_[w & kMask] = std::isfinite(sample) ? sample : 0.0f;
        writeIdx_.store(w + 1, std::memory_order_release);
        return true;
    }

    size_t push(const float* samples, size_t count) noexcept {
        if (samples == nullptr || count == 0)
            return 0;

        const size_t w = writeIdx_.load(std::memory_order_relaxed);
        const size_t r = readIdx_.load(std::memory_order_acquire);
        const size_t avail = kCapacity - (w - r);
        const size_t toWrite = (count < avail) ? count : avail;

        for (size_t i = 0; i < toWrite; ++i) {
            const float s = samples[i];
            buffer_[(w + i) & kMask] = std::isfinite(s) ? s : 0.0f;
        }

        writeIdx_.store(w + toWrite, std::memory_order_release);
        return toWrite;
    }

    int pushBlock(const float* samples, int numSamples) noexcept {
        if (samples == nullptr || numSamples <= 0)
            return 0;
        return static_cast<int>(push(samples, static_cast<size_t>(numSamples)));
    }

    // Convenience helper for processBlock: mono-sums stereo (or copies mono if right == nullptr).
    size_t pushMonoSum(const float* left, const float* right, size_t count) noexcept {
        if (left == nullptr || count == 0)
            return 0;

        const size_t w = writeIdx_.load(std::memory_order_relaxed);
        const size_t r = readIdx_.load(std::memory_order_acquire);
        const size_t avail = kCapacity - (w - r);
        const size_t toWrite = (count < avail) ? count : avail;

        if (right != nullptr) {
            for (size_t i = 0; i < toWrite; ++i) {
                const float s = 0.5f * (left[i] + right[i]);
                buffer_[(w + i) & kMask] = std::isfinite(s) ? s : 0.0f;
            }
        } else {
            for (size_t i = 0; i < toWrite; ++i) {
                const float s = left[i];
                buffer_[(w + i) & kMask] = std::isfinite(s) ? s : 0.0f;
            }
        }

        writeIdx_.store(w + toWrite, std::memory_order_release);
        return toWrite;
    }

    size_t pop(float* dest, size_t maxCount) noexcept {
        if (dest == nullptr || maxCount == 0)
            return 0;

        const size_t r = readIdx_.load(std::memory_order_relaxed);
        const size_t w = writeIdx_.load(std::memory_order_acquire);
        const size_t avail = w - r;
        const size_t toRead = (maxCount < avail) ? maxCount : avail;

        for (size_t i = 0; i < toRead; ++i) {
            dest[i] = buffer_[(r + i) & kMask];
        }

        readIdx_.store(r + toRead, std::memory_order_release);
        return toRead;
    }

    int popBlock(float* dest, int maxCount) noexcept {
        if (dest == nullptr || maxCount <= 0)
            return 0;
        return static_cast<int>(pop(dest, static_cast<size_t>(maxCount)));
    }

private:
    std::array<float, Capacity> buffer_{};
    std::atomic<size_t> writeIdx_{0};
    std::atomic<size_t> readIdx_{0};
};

// Self-contained iterative radix-2 Cooley-Tukey FFT with shared immutable twiddle tables.
template <int N>
class Radix2Fft {
    static_assert((N & (N - 1)) == 0 && N >= 16, "FFT size N must be a power of two >= 16");

public:
    static constexpr int kSize = N;
    static constexpr int kHalfSize = N / 2;

    static void forward(float* re, float* im) noexcept {
        const auto& tbl = tables();

        for (int i = 0; i < N; ++i) {
            const int j = tbl.bitRev[static_cast<size_t>(i)];
            if (i < j) {
                std::swap(re[i], re[j]);
                std::swap(im[i], im[j]);
            }
        }

        for (int len = 2; len <= N; len <<= 1) {
            const int halfLen = len >> 1;
            const int step = N / len;
            for (int i = 0; i < N; i += len) {
                int twIdx = 0;
                for (int j = 0; j < halfLen; ++j) {
                    const float wr = tbl.twiddleRe[static_cast<size_t>(twIdx)];
                    const float wi = tbl.twiddleIm[static_cast<size_t>(twIdx)];
                    twIdx += step;

                    const int u = i + j;
                    const int v = u + halfLen;
                    const float tr = wr * re[v] - wi * im[v];
                    const float ti = wr * im[v] + wi * re[v];

                    re[v] = re[u] - tr;
                    im[v] = im[u] - ti;
                    re[u] += tr;
                    im[u] += ti;
                }
            }
        }
    }

private:
    struct Tables {
        std::array<uint16_t, N> bitRev{};
        std::array<float, kHalfSize> twiddleRe{};
        std::array<float, kHalfSize> twiddleIm{};

        Tables() noexcept {
            constexpr double kPi = 3.14159265358979323846;
            int log2N = 0;
            for (int temp = N; temp > 1; temp >>= 1)
                ++log2N;

            for (int i = 0; i < N; ++i) {
                int rev = 0;
                int x = i;
                for (int b = 0; b < log2N; ++b) {
                    rev = (rev << 1) | (x & 1);
                    x >>= 1;
                }
                bitRev[static_cast<size_t>(i)] = static_cast<uint16_t>(rev);
            }

            for (int k = 0; k < kHalfSize; ++k) {
                const double angle = -2.0 * kPi * static_cast<double>(k) / static_cast<double>(N);
                twiddleRe[static_cast<size_t>(k)] = static_cast<float>(std::cos(angle));
                twiddleIm[static_cast<size_t>(k)] = static_cast<float>(std::sin(angle));
            }
        }
    };

    static const Tables& tables() noexcept {
        static const Tables kTables;
        return kTables;
    }
};

// Multi-resolution Constant-Q spectral bank + NMF harmonic template pitch salience solver.
class PolyphonicCqtNmf {
public:
    static constexpr int kBinsPerOctave = 36; // 3 bins per semitone (33.3 cents)
    static constexpr int kNumOctaves = 6;     // C1 (MIDI 24) .. B6 (MIDI 95)
    static constexpr int kMinMidi = 24;
    static constexpr int kMaxMidi = 95;
    static constexpr int kNumNotes = 72;
    static constexpr int kNumCqtBins = kBinsPerOctave * kNumOctaves; // 216
    static constexpr int kLowBandBins = 4 * kBinsPerOctave;          // 144 (C1..B4 via 4x-decimated 4096-pt FFT; C5..B6 via full-rate)
    static constexpr int kFftSize = 4096;
    static constexpr int kMagSize = kFftSize / 2 + 1;                // 2049
    static constexpr int kDecimFactor = 4;
    static constexpr int kRingSize = kFftSize * kDecimFactor;        // 16384
    static constexpr int kRingMask = kRingSize - 1;
    static constexpr int kNumHarmonics = 6;
    static constexpr int kNmfIterations = 16;

    struct FrameFeatures {
        std::array<float, kNumCqtBins> cqtBins{};
        std::array<float, kNumNotes> noteSalience{}; // MIDI 24..95, normalized 0..1
        std::array<float, 12> chroma{};              // folded from noteSalience, normalized 0..1
        std::array<float, 12> bassChroma{};          // weighted heavily toward lowest active note in MIDI 24..60
        int lowestActiveMidi = -1;
        float rmsDb = -100.0f;
        bool hasSignal = false;
    };

    PolyphonicCqtNmf()
        : ws_(std::make_unique<Workspace>()) {
        (void)staticTables(); // ensure one-time static init
        prepare(48000.0, 440.0f);
    }

    PolyphonicCqtNmf(const PolyphonicCqtNmf& other)
        : sampleRate_(other.sampleRate_),
          a4Hz_(other.a4Hz_),
          ws_(std::make_unique<Workspace>(*other.ws_)) {}

    PolyphonicCqtNmf& operator=(const PolyphonicCqtNmf& other) {
        if (this != &other) {
            sampleRate_ = other.sampleRate_;
            a4Hz_ = other.a4Hz_;
            *ws_ = *other.ws_;
        }
        return *this;
    }

    PolyphonicCqtNmf(PolyphonicCqtNmf&&) noexcept = default;
    PolyphonicCqtNmf& operator=(PolyphonicCqtNmf&&) noexcept = default;

    void prepare(double sampleRate, float a4Hz = 440.0f) noexcept {
        sampleRate_ = (std::isfinite(sampleRate) && sampleRate >= 8000.0) ? sampleRate : 48000.0;
        a4Hz_ = (std::isfinite(a4Hz) && a4Hz >= 400.0f && a4Hz <= 480.0f) ? a4Hz : 440.0f;
        rebuildCqtKernels();
        reset();
    }

    void setReferenceA4(float a4Hz) noexcept {
        const float clamped = (std::isfinite(a4Hz) && a4Hz >= 400.0f && a4Hz <= 480.0f) ? a4Hz : 440.0f;
        if (std::abs(clamped - a4Hz_) > 1e-4f) {
            a4Hz_ = clamped;
            rebuildCqtKernels();
        }
    }

    double getSampleRate() const noexcept { return sampleRate_; }
    float getReferenceA4() const noexcept { return a4Hz_; }

    void reset() noexcept {
        ws_->ring.fill(0.0f);
        ws_->ringWrite = 0;
        ws_->totalPushed = 0;
        ws_->samplesSinceHop = 0;
    }

    void pushAudio(const float* monoSamples, int numSamples) noexcept {
        if (monoSamples == nullptr || numSamples <= 0)
            return;

        auto& w = *ws_;
        for (int i = 0; i < numSamples; ++i) {
            const float s = monoSamples[i];
            w.ring[static_cast<size_t>(w.ringWrite)] = std::isfinite(s) ? s : 0.0f;
            w.ringWrite = (w.ringWrite + 1) & kRingMask;
        }
        w.totalPushed = std::min<int>(kRingSize, w.totalPushed + numSamples);
        w.samplesSinceHop += numSamples;
    }

    bool analyzeIfReady(FrameFeatures& out, int hopSize = 1024) noexcept {
        const int clampedHop = std::max(64, hopSize);
        if (ws_->samplesSinceHop < clampedHop || ws_->totalPushed < 512)
            return false;

        ws_->samplesSinceHop %= clampedHop;
        analyzeCurrentRing(out);
        return true;
    }

    // Direct/offline/test helper: analyzes an arbitrary mono buffer cleanly.
    FrameFeatures analyzeBuffer(const float* monoSamples, int numSamples) noexcept {
        FrameFeatures feat;
        if (monoSamples == nullptr || numSamples <= 0)
            return feat;

        reset();
        const int offset = (numSamples > kRingSize) ? (numSamples - kRingSize) : 0;
        pushAudio(monoSamples + offset, numSamples - offset);
        analyzeCurrentRing(feat);
        return feat;
    }

private:
    static constexpr int kMaxKernelTaps = 32;
    static constexpr int kFirHalf = 7;
    static constexpr int kFirLen = 2 * kFirHalf + 1;
    static constexpr int kMaxSparsePerNote = kNumHarmonics * 3; // 18 entries max per note

    struct CqtKernel {
        int startBin = 0;
        int numTaps = 0;
        std::array<float, kMaxKernelTaps> weights{};
    };

    struct SparseEntry {
        uint16_t bin = 0;
        float weight = 0.0f;
    };

    struct StaticTables {
        std::array<float, kFftSize> hannFull{};
        float hannFullNorm = 1.0f;
        std::array<float, kFirLen> antiAliasFir{};
        std::array<std::array<SparseEntry, kMaxSparsePerNote>, kNumNotes> nmfW{};
        std::array<int, kNumNotes> nmfWCount{};
        std::array<std::array<float, kNumNotes>, kNumNotes> nmfGram{};

        StaticTables() {
            constexpr double kPi = 3.14159265358979323846;

            double winSum = 0.0;
            for (int i = 0; i < kFftSize; ++i) {
                const double w = 0.5 * (1.0 - std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(kFftSize)));
                hannFull[static_cast<size_t>(i)] = static_cast<float>(w);
                winSum += w;
            }
            hannFullNorm = static_cast<float>(2.0 / std::max(winSum, 1.0));

            double firSum = 0.0;
            constexpr double fc = 0.1075;
            for (int i = -kFirHalf; i <= kFirHalf; ++i) {
                const double sinc = (i == 0) ? (2.0 * fc)
                                             : (std::sin(2.0 * kPi * fc * static_cast<double>(i)) / (kPi * static_cast<double>(i)));
                const double nNorm = static_cast<double>(i + kFirHalf) / static_cast<double>(kFirLen - 1);
                const double win = 0.42 - 0.5 * std::cos(2.0 * kPi * nNorm) + 0.08 * std::cos(4.0 * kPi * nNorm);
                const double val = sinc * win;
                antiAliasFir[static_cast<size_t>(i + kFirHalf)] = static_cast<float>(val);
                firSum += val;
            }
            for (float& c : antiAliasFir)
                c = static_cast<float>(static_cast<double>(c) / firSum);

            static constexpr std::array<float, kNumHarmonics> kHarmWeights = {
                1.00f, 0.55f, 0.38f, 0.28f, 0.22f, 0.17f
            };
            static constexpr std::array<int, kNumHarmonics> kHarmOffsets = {
                0, 36, 57, 72, 84, 93
            };
            static constexpr std::array<float, 3> kSubBinShape = {0.28f, 1.00f, 0.28f};

            float fullNormSq = 0.0f;
            for (int h = 0; h < kNumHarmonics; ++h) {
                const float hw = kHarmWeights[static_cast<size_t>(h)];
                for (float sb : kSubBinShape) {
                    const float v = hw * sb;
                    fullNormSq += v * v;
                }
            }
            const float fullNorm = std::sqrt(fullNormSq);

            auto denseW = std::make_unique<std::array<std::array<float, kNumCqtBins>, kNumNotes>>();

            for (int n = 0; n < kNumNotes; ++n) {
                const int baseBin = n * 3;
                int count = 0;
                std::array<float, kNumCqtBins> col{};

                for (int h = 0; h < kNumHarmonics; ++h) {
                    const int center = baseBin + kHarmOffsets[static_cast<size_t>(h)];
                    const float hw = kHarmWeights[static_cast<size_t>(h)];
                    for (int d = -1; d <= 1; ++d) {
                        const int b = center + d;
                        if (b >= 0 && b < kNumCqtBins) {
                            col[static_cast<size_t>(b)] += hw * kSubBinShape[static_cast<size_t>(d + 1)];
                        }
                    }
                }

                float inRangeSq = 0.0f;
                for (int b = 0; b < kNumCqtBins; ++b)
                    inRangeSq += col[static_cast<size_t>(b)] * col[static_cast<size_t>(b)];
                const float norm = std::max(std::sqrt(inRangeSq), 0.75f * fullNorm);
                const float invNorm = (norm > 1e-8f) ? (1.0f / norm) : 1.0f;

                for (int b = 0; b < kNumCqtBins; ++b) {
                    const float w = col[static_cast<size_t>(b)] * invNorm;
                    (*denseW)[static_cast<size_t>(n)][static_cast<size_t>(b)] = w;
                    if (w > 1e-6f && count < kMaxSparsePerNote) {
                        nmfW[static_cast<size_t>(n)][static_cast<size_t>(count++)] = {
                            static_cast<uint16_t>(b), w
                        };
                    }
                }
                nmfWCount[static_cast<size_t>(n)] = count;
            }

            for (int i = 0; i < kNumNotes; ++i) {
                for (int j = 0; j < kNumNotes; ++j) {
                    float sum = 0.0f;
                    const int ci = nmfWCount[static_cast<size_t>(i)];
                    for (int k = 0; k < ci; ++k) {
                        const auto& e = nmfW[static_cast<size_t>(i)][static_cast<size_t>(k)];
                        sum += e.weight * (*denseW)[static_cast<size_t>(j)][e.bin];
                    }
                    nmfGram[static_cast<size_t>(i)][static_cast<size_t>(j)] = sum;
                }
            }
        }
    };

    static const StaticTables& staticTables() {
        static const StaticTables kTables;
        return kTables;
    }

    struct Workspace {
        std::array<float, kRingSize> ring{};
        int ringWrite = 0;
        int totalPushed = 0;
        int samplesSinceHop = 0;

        std::array<float, kFftSize> fftRe{};
        std::array<float, kFftSize> fftIm{};
        std::array<float, kMagSize> magFull{};
        std::array<float, kMagSize> magLow{};

        std::array<CqtKernel, kNumCqtBins> cqtFullKernels{};
        std::array<CqtKernel, kLowBandBins> cqtLowKernels{};
    };

    void rebuildCqtKernels() noexcept {
        constexpr double kPi = 3.14159265358979323846;
        const double qFactor = 1.0 / (std::pow(2.0, 1.0 / static_cast<double>(kBinsPerOctave)) - 1.0);

        const double fsFull = sampleRate_;
        const double fsLow = sampleRate_ / static_cast<double>(kDecimFactor);
        const double binHzFull = fsFull / static_cast<double>(kFftSize);
        const double binHzLow = fsLow / static_cast<double>(kFftSize);

        auto buildKernelForBand = [&](double fCenter, double binHz) -> CqtKernel {
            CqtKernel k;
            const double centerBin = fCenter / binHz;
            const double bwHz = fCenter / qFactor;
            const double halfWidth = std::clamp(0.50 * bwHz / binHz, 1.05, static_cast<double>(kMaxKernelTaps / 2 - 1));

            int start = static_cast<int>(std::floor(centerBin - halfWidth));
            int end = static_cast<int>(std::ceil(centerBin + halfWidth));
            start = std::max(1, start);
            end = std::min(kMagSize - 2, end);

            if (end - start + 1 > kMaxKernelTaps) {
                const int mid = static_cast<int>(std::llround(centerBin));
                start = std::max(1, mid - kMaxKernelTaps / 2);
                end = std::min(kMagSize - 2, start + kMaxKernelTaps - 1);
            }

            k.startBin = start;
            k.numTaps = std::max(0, end - start + 1);
            double wSum = 0.0;
            for (int i = 0; i < k.numTaps; ++i) {
                const double dist = (static_cast<double>(start + i) - centerBin) / halfWidth;
                double w = 0.0;
                if (std::abs(dist) <= 1.0) {
                    w = 0.5 * (1.0 + std::cos(kPi * dist));
                }
                k.weights[static_cast<size_t>(i)] = static_cast<float>(w);
                wSum += w;
            }
            if (wSum > 1e-9) {
                const float invSum = static_cast<float>(1.0 / wSum);
                for (int i = 0; i < k.numTaps; ++i)
                    k.weights[static_cast<size_t>(i)] *= invSum;
            } else if (k.numTaps > 0) {
                k.weights[0] = 1.0f;
            }
            return k;
        };

        auto& w = *ws_;
        for (int b = 0; b < kNumCqtBins; ++b) {
            const double midi = static_cast<double>(kMinMidi) + static_cast<double>(b) / 3.0;
            const double fCenter = static_cast<double>(a4Hz_) * std::pow(2.0, (midi - 69.0) / 12.0);
            w.cqtFullKernels[static_cast<size_t>(b)] = buildKernelForBand(fCenter, binHzFull);
            if (b < kLowBandBins) {
                w.cqtLowKernels[static_cast<size_t>(b)] = buildKernelForBand(fCenter, binHzLow);
            }
        }
    }

    float readRingBack(int samplesAgo) const noexcept {
        const int idx = (ws_->ringWrite - 1 - samplesAgo) & kRingMask;
        return ws_->ring[static_cast<size_t>(idx)];
    }

    void computeFullRateSpectrum(int availSamples) noexcept {
        constexpr double kPi = 3.14159265358979323846;
        const auto& tbl = staticTables();
        auto& w = *ws_;
        const int nUse = std::min(availSamples, kFftSize);

        w.fftRe.fill(0.0f);
        w.fftIm.fill(0.0f);

        float normScale = tbl.hannFullNorm;
        if (nUse == kFftSize) {
            for (int i = 0; i < kFftSize; ++i) {
                const int ago = (kFftSize - 1) - i;
                w.fftRe[static_cast<size_t>(i)] = readRingBack(ago) * tbl.hannFull[static_cast<size_t>(i)];
            }
        } else {
            double wSum = 0.0;
            for (int i = 0; i < nUse; ++i) {
                const double win = 0.5 * (1.0 - std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(nUse)));
                const int ago = (nUse - 1) - i;
                w.fftRe[static_cast<size_t>(i)] = readRingBack(ago) * static_cast<float>(win);
                wSum += win;
            }
            normScale = static_cast<float>(2.0 / std::max(wSum, 1.0));
        }

        Radix2Fft<kFftSize>::forward(w.fftRe.data(), w.fftIm.data());

        for (int k = 0; k < kMagSize; ++k) {
            const float r = w.fftRe[static_cast<size_t>(k)];
            const float im = w.fftIm[static_cast<size_t>(k)];
            w.magFull[static_cast<size_t>(k)] = std::sqrt(r * r + im * im) * normScale;
        }
    }

    bool computeDecimatedSpectrum(int availSamples) noexcept {
        constexpr double kPi = 3.14159265358979323846;
        if (availSamples < 2048)
            return false;

        const auto& tbl = staticTables();
        auto& w = *ws_;

        const int usableFull = std::max(0, availSamples - 2 * kFirHalf);
        const int nDecim = std::min(kFftSize, usableFull / kDecimFactor);
        if (nDecim < 256)
            return false;

        w.fftRe.fill(0.0f);
        w.fftIm.fill(0.0f);

        float normScale = tbl.hannFullNorm;
        double wSum = 0.0;
        const bool fullLength = (nDecim == kFftSize);

        for (int d = 0; d < nDecim; ++d) {
            const int centerAgo = kFirHalf + (nDecim - 1 - d) * kDecimFactor;
            float acc = 0.0f;
            for (int t = -kFirHalf; t <= kFirHalf; ++t) {
                acc += tbl.antiAliasFir[static_cast<size_t>(t + kFirHalf)] * readRingBack(centerAgo + t);
            }
            float win = 1.0f;
            if (fullLength) {
                win = tbl.hannFull[static_cast<size_t>(d)];
            } else {
                const double wd = 0.5 * (1.0 - std::cos(2.0 * kPi * static_cast<double>(d) / static_cast<double>(nDecim)));
                win = static_cast<float>(wd);
                wSum += wd;
            }
            w.fftRe[static_cast<size_t>(d)] = acc * win;
        }

        if (!fullLength) {
            normScale = static_cast<float>(2.0 / std::max(wSum, 1.0));
        }

        Radix2Fft<kFftSize>::forward(w.fftRe.data(), w.fftIm.data());

        for (int k = 0; k < kMagSize; ++k) {
            const float r = w.fftRe[static_cast<size_t>(k)];
            const float im = w.fftIm[static_cast<size_t>(k)];
            w.magLow[static_cast<size_t>(k)] = std::sqrt(r * r + im * im) * normScale;
        }
        return true;
    }

    void analyzeCurrentRing(FrameFeatures& out) noexcept {
        out = FrameFeatures{};
        const auto& tbl = staticTables();
        auto& w = *ws_;
        const int avail = w.totalPushed;
        if (avail <= 0)
            return;

        const int rmsLen = std::min(avail, kFftSize);
        double energy = 0.0;
        for (int i = 0; i < rmsLen; ++i) {
            const float s = readRingBack(i);
            energy += static_cast<double>(s) * static_cast<double>(s);
        }
        const float rms = static_cast<float>(std::sqrt(energy / static_cast<double>(rmsLen)));
        out.rmsDb = (rms > 1e-5f) ? (20.0f * std::log10(rms)) : -100.0f;

        if (!std::isfinite(out.rmsDb) || out.rmsDb < -72.0f) {
            out.rmsDb = std::isfinite(out.rmsDb) ? out.rmsDb : -100.0f;
            out.hasSignal = false;
            return;
        }

        computeFullRateSpectrum(avail);
        const bool hasDecim = computeDecimatedSpectrum(avail);

        float maxCqt = 0.0f;
        for (int b = 0; b < kNumCqtBins; ++b) {
            float val = 0.0f;
            if (b < kLowBandBins && hasDecim) {
                const auto& ker = w.cqtLowKernels[static_cast<size_t>(b)];
                for (int i = 0; i < ker.numTaps; ++i)
                    val += ker.weights[static_cast<size_t>(i)] * w.magLow[static_cast<size_t>(ker.startBin + i)];
            } else {
                const auto& ker = w.cqtFullKernels[static_cast<size_t>(b)];
                for (int i = 0; i < ker.numTaps; ++i)
                    val += ker.weights[static_cast<size_t>(i)] * w.magFull[static_cast<size_t>(ker.startBin + i)];
            }
            if (!std::isfinite(val) || val < 0.0f)
                val = 0.0f;
            out.cqtBins[static_cast<size_t>(b)] = val;
            if (val > maxCqt)
                maxCqt = val;
        }

        if (maxCqt < 1e-6f) {
            out.hasSignal = false;
            return;
        }

        std::array<float, kNumNotes> wtV{};
        std::array<float, kNumNotes> H{};
        const float eps = 1e-7f * maxCqt + 1e-12f;

        for (int n = 0; n < kNumNotes; ++n) {
            float sum = 0.0f;
            const int cnt = tbl.nmfWCount[static_cast<size_t>(n)];
            for (int k = 0; k < cnt; ++k) {
                const auto& e = tbl.nmfW[static_cast<size_t>(n)][static_cast<size_t>(k)];
                sum += e.weight * out.cqtBins[e.bin];
            }
            wtV[static_cast<size_t>(n)] = sum;
            H[static_cast<size_t>(n)] = std::max(sum, eps);
        }

        for (int iter = 0; iter < kNmfIterations; ++iter) {
            for (int n = 0; n < kNumNotes; ++n) {
                float denom = eps;
                const auto& gRow = tbl.nmfGram[static_cast<size_t>(n)];
                for (int m = 0; m < kNumNotes; ++m) {
                    denom += gRow[static_cast<size_t>(m)] * H[static_cast<size_t>(m)];
                }
                const float updated = H[static_cast<size_t>(n)] * (wtV[static_cast<size_t>(n)] / denom);
                H[static_cast<size_t>(n)] = std::isfinite(updated) ? updated : 0.0f;
            }
        }

        float maxH = 0.0f;
        for (int n = 0; n < kNumNotes; ++n) {
            const int b0 = n * 3;
            float fundPeak = out.cqtBins[static_cast<size_t>(b0)];
            if (b0 > 0) fundPeak = std::max(fundPeak, out.cqtBins[static_cast<size_t>(b0 - 1)]);
            if (b0 + 1 < kNumCqtBins) fundPeak = std::max(fundPeak, out.cqtBins[static_cast<size_t>(b0 + 1)]);
            if (fundPeak < 0.035f * maxCqt) {
                H[static_cast<size_t>(n)] *= 0.15f;
            }
            if (H[static_cast<size_t>(n)] > maxH)
                maxH = H[static_cast<size_t>(n)];
        }

        if (maxH < 1e-6f) {
            out.hasSignal = false;
            return;
        }

        out.hasSignal = true;
        const float invMaxH = 1.0f / maxH;
        for (int n = 0; n < kNumNotes; ++n) {
            const float norm = H[static_cast<size_t>(n)] * invMaxH;
            out.noteSalience[static_cast<size_t>(n)] = (norm >= 0.025f) ? std::min(1.0f, norm) : 0.0f;
        }

        std::array<float, 12> chromaSq{};
        for (int n = 0; n < kNumNotes; ++n) {
            const float s = out.noteSalience[static_cast<size_t>(n)];
            if (s > 0.0f) {
                const int pc = (kMinMidi + n) % 12;
                chromaSq[static_cast<size_t>(pc)] += s * s;
            }
        }
        float maxChroma = 0.0f;
        for (int pc = 0; pc < 12; ++pc) {
            out.chroma[static_cast<size_t>(pc)] = std::sqrt(chromaSq[static_cast<size_t>(pc)]);
            if (out.chroma[static_cast<size_t>(pc)] > maxChroma)
                maxChroma = out.chroma[static_cast<size_t>(pc)];
        }
        if (maxChroma > 1e-6f) {
            const float invMaxC = 1.0f / maxChroma;
            for (int pc = 0; pc < 12; ++pc)
                out.chroma[static_cast<size_t>(pc)] *= invMaxC;
        }

        int lowestMidi = -1;
        for (int n = 0; n < kNumNotes; ++n) {
            const float s = out.noteSalience[static_cast<size_t>(n)];
            if (s < 0.20f)
                continue;
            const float prevS = (n > 0) ? out.noteSalience[static_cast<size_t>(n - 1)] : 0.0f;
            const float nextS = (n + 1 < kNumNotes) ? out.noteSalience[static_cast<size_t>(n + 1)] : 0.0f;
            if (s >= prevS * 0.92f && s >= nextS * 0.92f) {
                lowestMidi = kMinMidi + n;
                break;
            }
        }
        if (lowestMidi < 0) {
            int bestN = 0;
            for (int n = 1; n < kNumNotes; ++n) {
                if (out.noteSalience[static_cast<size_t>(n)] > out.noteSalience[static_cast<size_t>(bestN)])
                    bestN = n;
            }
            lowestMidi = kMinMidi + bestN;
        }
        out.lowestActiveMidi = lowestMidi;

        float maxBass = 0.0f;
        const int bassLimit = std::min(kNumNotes - 1, 60 - kMinMidi);
        for (int n = 0; n <= bassLimit; ++n) {
            const float s = out.noteSalience[static_cast<size_t>(n)];
            if (s <= 0.0f)
                continue;
            const int midi = kMinMidi + n;
            float weight = std::exp(-0.09f * static_cast<float>(n));
            if (midi == lowestMidi)
                weight *= 3.0f;
            else if (lowestMidi >= 0 && midi > lowestMidi)
                weight *= std::exp(-0.18f * static_cast<float>(midi - lowestMidi));
            const int pc = midi % 12;
            out.bassChroma[static_cast<size_t>(pc)] += s * weight;
            if (out.bassChroma[static_cast<size_t>(pc)] > maxBass)
                maxBass = out.bassChroma[static_cast<size_t>(pc)];
        }
        if (maxBass > 1e-6f) {
            const float invBass = 1.0f / maxBass;
            for (int pc = 0; pc < 12; ++pc)
                out.bassChroma[static_cast<size_t>(pc)] *= invBass;
        } else if (lowestMidi >= 0) {
            out.bassChroma[static_cast<size_t>(lowestMidi % 12)] = 1.0f;
        }
    }

    double sampleRate_ = 48000.0;
    float a4Hz_ = 440.0f;
    std::unique_ptr<Workspace> ws_;
};

} // namespace harmonyhud
