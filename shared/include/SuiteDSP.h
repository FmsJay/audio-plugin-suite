#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

#if defined(_MSC_VER) || defined(__SSE__) || defined(__x86_64__) || defined(_M_X64) || defined(_M_IX86)
    #include <xmmintrin.h>
    #include <pmmintrin.h>
    #define SUITE_DSP_HAS_SSE 1
#else
    #define SUITE_DSP_HAS_SSE 0
#endif

namespace suite::dsp {

// ============================================================================
// Constants
// ============================================================================
inline constexpr double kPi          = 3.1415926535897932384626433832795;
inline constexpr double kTwoPi       = 6.2831853071795864769252867665590;
inline constexpr double kHalfPi      = 1.5707963267948966192313216916398;
inline constexpr double kSqrt2       = 1.4142135623730950488016887242097;
inline constexpr double kInvSqrt2    = 0.70710678118654752440084436210485;
inline constexpr float  kPiF         = 3.14159265358979323846f;
inline constexpr float  kTwoPiF      = 6.28318530717958647692f;
inline constexpr float  kHalfPiF     = 1.57079632679489661923f;

// ============================================================================
// Denormal Guard & Math Helpers (Real-time Safe)
// ============================================================================
class ScopedDenormalGuard {
public:
    ScopedDenormalGuard() noexcept {
#if SUITE_DSP_HAS_SSE
        prevFtz_ = _MM_GET_FLUSH_ZERO_MODE();
        prevDaz_ = _MM_GET_DENORMALS_ZERO_MODE();
        _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
        _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif
    }

    ~ScopedDenormalGuard() noexcept {
#if SUITE_DSP_HAS_SSE
        _MM_SET_FLUSH_ZERO_MODE(prevFtz_);
        _MM_SET_DENORMALS_ZERO_MODE(prevDaz_);
#endif
    }

    ScopedDenormalGuard(const ScopedDenormalGuard&) = delete;
    ScopedDenormalGuard& operator=(const ScopedDenormalGuard&) = delete;

private:
#if SUITE_DSP_HAS_SSE
    unsigned int prevFtz_ = 0;
    unsigned int prevDaz_ = 0;
#endif
};

inline constexpr float flushDenormal(float x) noexcept {
    return (x > -1.0e-24f && x < 1.0e-24f) ? 0.0f : x;
}

inline constexpr double flushDenormal(double x) noexcept {
    return (x > -1.0e-30 && x < 1.0e-30) ? 0.0 : x;
}

inline float dbToGain(float db, float minusInfDb = -120.0f) noexcept {
    if (db <= minusInfDb) return 0.0f;
    return std::pow(10.0f, db * 0.05f);
}

inline double dbToGain(double db, double minusInfDb = -120.0) noexcept {
    if (db <= minusInfDb) return 0.0;
    return std::pow(10.0, db * 0.05);
}

inline float gainToDb(float gain, float floorDb = -120.0f) noexcept {
    const float absGain = std::abs(gain);
    if (absGain <= 1.0e-6f) {
        const float minGain = std::pow(10.0f, floorDb * 0.05f);
        if (absGain <= minGain) return floorDb;
    }
    return std::max(floorDb, 20.0f * std::log10(absGain));
}

inline double gainToDb(double gain, double floorDb = -120.0) noexcept {
    const double absGain = std::abs(gain);
    const double minGain = std::pow(10.0, floorDb * 0.05);
    if (absGain <= minGain) return floorDb;
    return std::max(floorDb, 20.0 * std::log10(absGain));
}

inline float fastTanh(float x) noexcept {
    if (x <= -3.0f) return -1.0f;
    if (x >=  3.0f) return  1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

inline double fastTanh(double x) noexcept {
    if (x <= -3.0) return -1.0;
    if (x >=  3.0) return  1.0;
    const double x2 = x * x;
    return x * (27.0 + x2) / (27.0 + 9.0 * x2);
}

inline float softClip(float x, float drive = 1.0f) noexcept {
    return fastTanh(x * drive);
}

inline float clampFinite(float x, float lo = -1.0f, float hi = 1.0f) noexcept {
    if (!std::isfinite(x)) return 0.0f;
    return std::clamp(x, lo, hi);
}

inline double clampFinite(double x, double lo = -1.0, double hi = 1.0) noexcept {
    if (!std::isfinite(x)) return 0.0;
    return std::clamp(x, lo, hi);
}

// ============================================================================
// OnePoleSmoother (Exponential Parameter Smoother)
// ============================================================================
class OnePoleSmoother {
public:
    void prepare(double sampleRate, float timeMs = 15.0f) noexcept {
        sampleRate_ = std::max(1.0, sampleRate);
        setTimeMs(timeMs);
    }

    void setTimeMs(float timeMs) noexcept {
        const double tau = std::max(0.0001, static_cast<double>(timeMs) * 0.001);
        coeff_ = static_cast<float>(1.0 - std::exp(-1.0 / (tau * sampleRate_)));
    }

    void reset(float value = 0.0f) noexcept {
        current_ = value;
        target_ = value;
    }

    void setTarget(float target) noexcept {
        target_ = target;
    }

    float getTarget() const noexcept { return target_; }
    float getCurrent() const noexcept { return current_; }

    bool isSmoothing(float eps = 1.0e-5f) const noexcept {
        return std::abs(target_ - current_) > eps;
    }

    float process() noexcept {
        current_ += coeff_ * (target_ - current_);
        current_ = flushDenormal(current_);
        return current_;
    }

    float process(float newTarget) noexcept {
        target_ = newTarget;
        return process();
    }

private:
    double sampleRate_ = 48000.0;
    float  coeff_      = 0.01f;
    float  current_    = 0.0f;
    float  target_     = 0.0f;
};

// ============================================================================
// Biquad (Transposed Direct Form II, Double Precision State)
// ============================================================================
class Biquad {
public:
    enum class Type {
        LowPass,
        HighPass,
        BandPass,
        Notch,
        Peak,
        LowShelf,
        HighShelf,
        AllPass
    };

    void reset() noexcept {
        z1_ = 0.0;
        z2_ = 0.0;
    }

    void setCoefficients(double b0, double b1, double b2, double a1, double a2) noexcept {
        b0_ = b0; b1_ = b1; b2_ = b2;
        a1_ = a1; a2_ = a2;
    }

    void configure(Type type, double sampleRate, double freqHz, double q = kInvSqrt2, double gainDb = 0.0) noexcept {
        const double fs = std::max(1.0, sampleRate);
        const double fc = std::clamp(freqHz, 5.0, fs * 0.495);
        const double Q  = std::max(0.05, q);

        const double w0    = kTwoPi * fc / fs;
        const double cosW0 = std::cos(w0);
        const double sinW0 = std::sin(w0);
        const double alpha = sinW0 / (2.0 * Q);
        const double A     = std::pow(10.0, gainDb / 40.0);

        double b0 = 1.0, b1 = 0.0, b2 = 0.0;
        double a0 = 1.0, a1 = 0.0, a2 = 0.0;

        switch (type) {
            case Type::LowPass:
                b0 = (1.0 - cosW0) * 0.5;
                b1 =  1.0 - cosW0;
                b2 = (1.0 - cosW0) * 0.5;
                a0 =  1.0 + alpha;
                a1 = -2.0 * cosW0;
                a2 =  1.0 - alpha;
                break;

            case Type::HighPass:
                b0 = (1.0 + cosW0) * 0.5;
                b1 = -(1.0 + cosW0);
                b2 = (1.0 + cosW0) * 0.5;
                a0 =  1.0 + alpha;
                a1 = -2.0 * cosW0;
                a2 =  1.0 - alpha;
                break;

            case Type::BandPass:
                b0 =  alpha;
                b1 =  0.0;
                b2 = -alpha;
                a0 =  1.0 + alpha;
                a1 = -2.0 * cosW0;
                a2 =  1.0 - alpha;
                break;

            case Type::Notch:
                b0 =  1.0;
                b1 = -2.0 * cosW0;
                b2 =  1.0;
                a0 =  1.0 + alpha;
                a1 = -2.0 * cosW0;
                a2 =  1.0 - alpha;
                break;

            case Type::Peak:
                b0 =  1.0 + alpha * A;
                b1 = -2.0 * cosW0;
                b2 =  1.0 - alpha * A;
                a0 =  1.0 + alpha / A;
                a1 = -2.0 * cosW0;
                a2 =  1.0 - alpha / A;
                break;

            case Type::LowShelf: {
                const double twoSqrtAAlpha = 2.0 * std::sqrt(A) * alpha;
                b0 =       A * ((A + 1.0) - (A - 1.0) * cosW0 + twoSqrtAAlpha);
                b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosW0);
                b2 =       A * ((A + 1.0) - (A - 1.0) * cosW0 - twoSqrtAAlpha);
                a0 =            (A + 1.0) + (A - 1.0) * cosW0 + twoSqrtAAlpha;
                a1 =    -2.0 * ((A - 1.0) + (A + 1.0) * cosW0);
                a2 =            (A + 1.0) + (A - 1.0) * cosW0 - twoSqrtAAlpha;
                break;
            }

            case Type::HighShelf: {
                const double twoSqrtAAlpha = 2.0 * std::sqrt(A) * alpha;
                b0 =        A * ((A + 1.0) + (A - 1.0) * cosW0 + twoSqrtAAlpha);
                b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosW0);
                b2 =        A * ((A + 1.0) + (A - 1.0) * cosW0 - twoSqrtAAlpha);
                a0 =             (A + 1.0) - (A - 1.0) * cosW0 + twoSqrtAAlpha;
                a1 =      2.0 * ((A - 1.0) - (A + 1.0) * cosW0);
                a2 =             (A + 1.0) - (A - 1.0) * cosW0 - twoSqrtAAlpha;
                break;
            }

            case Type::AllPass:
                b0 =  1.0 - alpha;
                b1 = -2.0 * cosW0;
                b2 =  1.0 + alpha;
                a0 =  1.0 + alpha;
                a1 = -2.0 * cosW0;
                a2 =  1.0 - alpha;
                break;
        }

        const double invA0 = 1.0 / a0;
        b0_ = b0 * invA0;
        b1_ = b1 * invA0;
        b2_ = b2 * invA0;
        a1_ = a1 * invA0;
        a2_ = a2 * invA0;
    }

    float process(float x) noexcept {
        return static_cast<float>(process(static_cast<double>(x)));
    }

    double process(double x) noexcept {
        const double y = b0_ * x + z1_;
        z1_ = flushDenormal(b1_ * x - a1_ * y + z2_);
        z2_ = flushDenormal(b2_ * x - a2_ * y);
        return y;
    }

private:
    double b0_ = 1.0, b1_ = 0.0, b2_ = 0.0;
    double a1_ = 0.0, a2_ = 0.0;
    double z1_ = 0.0, z2_ = 0.0;
};

// ============================================================================
// TptSVF (Zero-Delay Feedback Topology-Preserving State Variable Filter)
// Based on Andrew Simper's linear trapezoidal integrated SVF
// ============================================================================
class TptSVF {
public:
    enum class Mode {
        LowPass,
        HighPass,
        BandPass,
        Notch,
        Peak,
        Bell,
        LowShelf,
        HighShelf,
        AllPass
    };

    void prepare(double sampleRate) noexcept {
        sampleRate_ = std::max(1.0, sampleRate);
        reset();
        updateCoefficients();
    }

    void reset() noexcept {
        ic1eq_ = 0.0;
        ic2eq_ = 0.0;
    }

    void setParameters(Mode mode, float cutoffHz, float q = 0.70710678f, float gainDb = 0.0f) noexcept {
        mode_     = mode;
        cutoffHz_ = cutoffHz;
        q_        = q;
        gainDb_   = gainDb;
        updateCoefficients();
    }

    void setCutoff(float cutoffHz) noexcept {
        cutoffHz_ = cutoffHz;
        updateCoefficients();
    }

    void setResonance(float q) noexcept {
        q_ = q;
        updateCoefficients();
    }

    float process(float x) noexcept {
        const double v0 = static_cast<double>(x);
        const double v3 = v0 - ic2eq_;
        const double v1 = a1_ * ic1eq_ + a2_ * v3;
        const double v2 = ic2eq_ + a2_ * ic1eq_ + a3_ * v3;
        ic1eq_ = flushDenormal(2.0 * v1 - ic1eq_);
        ic2eq_ = flushDenormal(2.0 * v2 - ic2eq_);
        return static_cast<float>(m0_ * v0 + m1_ * v1 + m2_ * v2);
    }

    void processMultimode(float x, float& lp, float& bp, float& hp) noexcept {
        const double v0 = static_cast<double>(x);
        const double v3 = v0 - ic2eq_;
        const double v1 = a1_ * ic1eq_ + a2_ * v3;
        const double v2 = ic2eq_ + a2_ * ic1eq_ + a3_ * v3;
        ic1eq_ = flushDenormal(2.0 * v1 - ic1eq_);
        ic2eq_ = flushDenormal(2.0 * v2 - ic2eq_);
        lp = static_cast<float>(v2);
        bp = static_cast<float>(v1);
        hp = static_cast<float>(v0 - k_ * v1 - v2);
    }

private:
    void updateCoefficients() noexcept {
        const double fc = std::clamp(static_cast<double>(cutoffHz_), 5.0, sampleRate_ * 0.49);
        const double Q  = std::max(0.05, static_cast<double>(q_));
        const double A  = std::pow(10.0, static_cast<double>(gainDb_) / 40.0);

        double g = std::tan(kPi * fc / sampleRate_);
        double k = 1.0 / Q;

        switch (mode_) {
            case Mode::LowPass:
                m0_ = 0.0; m1_ = 0.0; m2_ = 1.0;
                break;
            case Mode::HighPass:
                m0_ = 1.0; m1_ = -k; m2_ = -1.0;
                break;
            case Mode::BandPass:
                m0_ = 0.0; m1_ = 1.0; m2_ = 0.0;
                break;
            case Mode::Notch:
                m0_ = 1.0; m1_ = -k; m2_ = 0.0;
                break;
            case Mode::Peak:
                m0_ = 1.0; m1_ = -k; m2_ = -2.0;
                break;
            case Mode::Bell:
                k = 1.0 / (Q * A);
                m0_ = 1.0; m1_ = k * (A * A - 1.0); m2_ = 0.0;
                break;
            case Mode::LowShelf:
                g /= std::sqrt(A);
                m0_ = 1.0; m1_ = k * (A - 1.0); m2_ = A * A - 1.0;
                break;
            case Mode::HighShelf:
                g *= std::sqrt(A);
                m0_ = A * A; m1_ = k * (1.0 - A) * A; m2_ = 1.0 - A * A;
                break;
            case Mode::AllPass:
                m0_ = 1.0; m1_ = -2.0 * k; m2_ = 0.0;
                break;
        }

        k_  = k;
        a1_ = 1.0 / (1.0 + g * (g + k));
        a2_ = g * a1_;
        a3_ = g * a2_;
    }

    double sampleRate_ = 48000.0;
    Mode   mode_       = Mode::LowPass;
    float  cutoffHz_   = 1000.0f;
    float  q_          = 0.70710678f;
    float  gainDb_     = 0.0f;

    double k_  = 1.41421356237;
    double a1_ = 0.0, a2_ = 0.0, a3_ = 0.0;
    double m0_ = 0.0, m1_ = 0.0, m2_ = 1.0;
    double ic1eq_ = 0.0, ic2eq_ = 0.0;
};

// ============================================================================
// FractionalDelay (Power-of-Two Ring Buffer with Linear, Hermite & Lagrange)
// ============================================================================
class FractionalDelay {
public:
    void prepare(size_t maxDelaySamples) {
        size_t cap = 16;
        const size_t needed = maxDelaySamples + 8;
        while (cap < needed) cap <<= 1;
        buffer_.assign(cap, 0.0f);
        mask_ = cap - 1;
        maxDelay_ = maxDelaySamples;
        writePos_ = 0;
    }

    void prepare(double sampleRate, float maxDelaySeconds) {
        const auto samples = static_cast<size_t>(std::ceil(std::max(1.0, sampleRate) * std::max(0.001f, maxDelaySeconds)));
        prepare(samples);
    }

    void reset() noexcept {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        writePos_ = 0;
    }

    size_t getMaxDelaySamples() const noexcept { return maxDelay_; }

    void push(float sample) noexcept {
        if (buffer_.empty()) return;
        buffer_[writePos_] = sample;
        writePos_ = (writePos_ + 1) & mask_;
    }

    float readLinear(float delaySamples) const noexcept {
        if (buffer_.empty()) return 0.0f;
        const float d = std::clamp(delaySamples, 0.0f, static_cast<float>(maxDelay_));
        const auto i0 = static_cast<size_t>(d);
        const float frac = d - static_cast<float>(i0);
        const size_t idx0 = (writePos_ - 1 - i0) & mask_;
        const size_t idx1 = (idx0 - 1) & mask_;
        return buffer_[idx0] + frac * (buffer_[idx1] - buffer_[idx0]);
    }

    // 4-point, 3rd-order Hermite (Catmull-Rom) interpolation
    float readHermite(float delaySamples) const noexcept {
        if (buffer_.empty()) return 0.0f;
        const float d = std::clamp(delaySamples, 1.0f, static_cast<float>(maxDelay_));
        const auto i0 = static_cast<size_t>(d);
        const float t = d - static_cast<float>(i0);

        const float xm1 = buffer_[(writePos_ - i0) & mask_];
        const float x0  = buffer_[(writePos_ - 1 - i0) & mask_];
        const float x1  = buffer_[(writePos_ - 2 - i0) & mask_];
        const float x2  = buffer_[(writePos_ - 3 - i0) & mask_];

        const float c0 = x0;
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }

    // 4-point, 3rd-order Lagrange interpolation
    float readLagrange(float delaySamples) const noexcept {
        if (buffer_.empty()) return 0.0f;
        const float d = std::clamp(delaySamples, 1.0f, static_cast<float>(maxDelay_));
        const auto i0 = static_cast<size_t>(d);
        const float t = d - static_cast<float>(i0);

        const float y0 = buffer_[(writePos_ - i0) & mask_];     // x = -1
        const float y1 = buffer_[(writePos_ - 1 - i0) & mask_]; // x =  0
        const float y2 = buffer_[(writePos_ - 2 - i0) & mask_]; // x = +1
        const float y3 = buffer_[(writePos_ - 3 - i0) & mask_]; // x = +2

        const float tm1 = t - 1.0f;
        const float tm2 = t - 2.0f;
        const float tp1 = t + 1.0f;

        const float w0 = (-1.0f / 6.0f) * t * tm1 * tm2;
        const float w1 = ( 0.5f)        * tp1 * tm1 * tm2;
        const float w2 = (-0.5f)        * tp1 * t * tm2;
        const float w3 = ( 1.0f / 6.0f) * tp1 * t * tm1;

        return w0 * y0 + w1 * y1 + w2 * y2 + w3 * y3;
    }

    float read(float delaySamples) const noexcept {
        return readHermite(delaySamples);
    }

private:
    std::vector<float> buffer_;
    size_t mask_     = 0;
    size_t maxDelay_ = 0;
    size_t writePos_ = 0;
};

// ============================================================================
// SpscRingBuffer<T, N> (Wait-Free Lock-Free Audio <-> UI FIFO)
// ============================================================================
template <typename T, size_t N>
class SpscRingBuffer {
    static_assert(N >= 2 && (N & (N - 1)) == 0, "SpscRingBuffer capacity N must be a power of two >= 2");

public:
    static constexpr size_t kCapacity = N;
    static constexpr size_t kMask     = N - 1;

    void clear() noexcept {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    size_t availableToRead() const noexcept {
        const size_t h = head_.load(std::memory_order_acquire);
        const size_t t = tail_.load(std::memory_order_relaxed);
        return (h - t) & kMask;
    }

    size_t availableToWrite() const noexcept {
        return (N - 1) - availableToRead();
    }

    bool push(const T& item) noexcept {
        const size_t h = head_.load(std::memory_order_relaxed);
        const size_t next = (h + 1) & kMask;
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        buffer_[h] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& item) noexcept {
        const size_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) {
            return false;
        }
        item = buffer_[t];
        tail_.store((t + 1) & kMask, std::memory_order_release);
        return true;
    }

    size_t pushBlock(const T* data, size_t count) noexcept {
        size_t written = 0;
        while (written < count && push(data[written])) {
            ++written;
        }
        return written;
    }

    size_t popBlock(T* data, size_t maxCount) noexcept {
        size_t read = 0;
        while (read < maxCount && pop(data[read])) {
            ++read;
        }
        return read;
    }

private:
    alignas(64) std::atomic<size_t> head_{ 0 };
    alignas(64) std::atomic<size_t> tail_{ 0 };
    std::array<T, N> buffer_{};
};

// ============================================================================
// ShortTermLufsMeter (ITU-R BS.1770-4 K-Weighted Loudness Meter)
// ============================================================================
class ShortTermLufsMeter {
public:
    void prepare(double sampleRate, float windowSeconds = 3.0f) {
        sampleRate_ = std::max(1.0, sampleRate);
        configureKWeighting(sampleRate_);

        // 10 ms sub-blocks for low-memory exact sliding window
        blockSize_ = std::max<size_t>(1, static_cast<size_t>(std::round(sampleRate_ * 0.01)));
        shortTermBlocks_ = std::max<size_t>(1, static_cast<size_t>(std::round(std::max(0.1f, windowSeconds) / 0.01f)));
        momentaryBlocks_ = std::max<size_t>(1, static_cast<size_t>(std::round(0.4 / 0.01))); // 400 ms

        const size_t ringLen = std::max(shortTermBlocks_, momentaryBlocks_);
        blockEnergies_.assign(ringLen, 0.0);
        reset();
    }

    void reset() noexcept {
        for (int ch = 0; ch < 2; ++ch) {
            preShelf_[ch].reset();
            rlbHighPass_[ch].reset();
        }
        std::fill(blockEnergies_.begin(), blockEnergies_.end(), 0.0);
        ringIndex_     = 0;
        sampleCounter_ = 0;
        accumEnergy_   = 0.0;
        filledBlocks_  = 0;
        shortTermMean_ = 0.0;
        momentaryMean_ = 0.0;
    }

    void processSample(float left, float right) noexcept {
        const double l = rlbHighPass_[0].process(preShelf_[0].process(static_cast<double>(left)));
        const double r = rlbHighPass_[1].process(preShelf_[1].process(static_cast<double>(right)));
        accumEnergy_ += (l * l + r * r);
        advanceSample();
    }

    void processSample(float mono) noexcept {
        const double m = rlbHighPass_[0].process(preShelf_[0].process(static_cast<double>(mono)));
        accumEnergy_ += (m * m);
        advanceSample();
    }

    void processBlock(const float* left, const float* right, int numSamples) noexcept {
        if (!left || numSamples <= 0) return;
        if (right) {
            for (int i = 0; i < numSamples; ++i) {
                processSample(left[i], right[i]);
            }
        } else {
            for (int i = 0; i < numSamples; ++i) {
                processSample(left[i]);
            }
        }
    }

    float getLufs() const noexcept {
        if (shortTermMean_ <= 1.0e-12) return -100.0f;
        return static_cast<float>(std::max(-100.0, -0.691 + 10.0 * std::log10(shortTermMean_)));
    }

    float getMomentaryLufs() const noexcept {
        if (momentaryMean_ <= 1.0e-12) return -100.0f;
        return static_cast<float>(std::max(-100.0, -0.691 + 10.0 * std::log10(momentaryMean_)));
    }

private:
    void configureKWeighting(double fs) noexcept {
        // BS.1770-4 Stage 1: High-shelf (+4 dB above ~1681.97 Hz)
        constexpr double f0 = 1681.974450955533;
        constexpr double G  = 3.999843853973347;
        constexpr double Q  = 0.7071752369554196;
        const double K  = std::tan(kPi * f0 / fs);
        const double Vh = std::pow(10.0, G / 20.0);
        const double Vb = std::pow(Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        const double b0 = (Vh + Vb * K / Q + K * K) / a0;
        const double b1 = 2.0 * (K * K - Vh) / a0;
        const double b2 = (Vh - Vb * K / Q + K * K) / a0;
        const double a1 = 2.0 * (K * K - 1.0) / a0;
        const double a2 = (1.0 - K / Q + K * K) / a0;

        // BS.1770-4 Stage 2: RLB High-pass (~38.135 Hz)
        constexpr double f1 = 38.13547087602444;
        constexpr double Q1 = 0.5003270373238773;
        const double K1 = std::tan(kPi * f1 / fs);
        const double a0_hp = 1.0 + K1 / Q1 + K1 * K1;
        const double b0_hp = 1.0 / a0_hp;
        const double b1_hp = -2.0 / a0_hp;
        const double b2_hp = 1.0 / a0_hp;
        const double a1_hp = 2.0 * (K1 * K1 - 1.0) / a0_hp;
        const double a2_hp = (1.0 - K1 / Q1 + K1 * K1) / a0_hp;

        for (int ch = 0; ch < 2; ++ch) {
            preShelf_[ch].setCoefficients(b0, b1, b2, a1, a2);
            rlbHighPass_[ch].setCoefficients(b0_hp, b1_hp, b2_hp, a1_hp, a2_hp);
        }
    }

    void advanceSample() noexcept {
        ++sampleCounter_;
        if (sampleCounter_ < blockSize_ || blockEnergies_.empty()) return;

        const double blockMean = accumEnergy_ / static_cast<double>(blockSize_);
        accumEnergy_ = 0.0;
        sampleCounter_ = 0;

        blockEnergies_[ringIndex_] = blockMean;
        ringIndex_ = (ringIndex_ + 1) % blockEnergies_.size();
        if (filledBlocks_ < blockEnergies_.size()) ++filledBlocks_;

        const size_t stCount = std::min(filledBlocks_, shortTermBlocks_);
        const size_t moCount = std::min(filledBlocks_, momentaryBlocks_);
        const size_t len = blockEnergies_.size();

        double stSum = 0.0;
        for (size_t i = 0; i < stCount; ++i) {
            const size_t idx = (ringIndex_ + len - 1 - i) % len;
            stSum += blockEnergies_[idx];
        }
        shortTermMean_ = stCount > 0 ? stSum / static_cast<double>(stCount) : 0.0;

        double moSum = 0.0;
        for (size_t i = 0; i < moCount; ++i) {
            const size_t idx = (ringIndex_ + len - 1 - i) % len;
            moSum += blockEnergies_[idx];
        }
        momentaryMean_ = moCount > 0 ? moSum / static_cast<double>(moCount) : 0.0;
    }

    double sampleRate_ = 48000.0;
    Biquad preShelf_[2];
    Biquad rlbHighPass_[2];

    std::vector<double> blockEnergies_;
    size_t blockSize_       = 480;
    size_t shortTermBlocks_ = 300;
    size_t momentaryBlocks_ = 40;
    size_t ringIndex_       = 0;
    size_t sampleCounter_   = 0;
    size_t filledBlocks_    = 0;
    double accumEnergy_     = 0.0;
    double shortTermMean_   = 0.0;
    double momentaryMean_   = 0.0;
};

// ============================================================================
// EnvelopeFollower (Peak & RMS)
// ============================================================================
class EnvelopeFollower {
public:
    void prepare(double sampleRate, float attackMs = 5.0f, float releaseMs = 80.0f) noexcept {
        sampleRate_ = std::max(1.0, sampleRate);
        setTimes(attackMs, releaseMs);
    }

    void setTimes(float attackMs, float releaseMs) noexcept {
        const double attSec = std::max(0.00005, static_cast<double>(attackMs) * 0.001);
        const double relSec = std::max(0.0001,  static_cast<double>(releaseMs) * 0.001);
        attackCoeff_  = static_cast<float>(std::exp(-1.0 / (attSec * sampleRate_)));
        releaseCoeff_ = static_cast<float>(std::exp(-1.0 / (relSec * sampleRate_)));
    }

    void reset(float value = 0.0f) noexcept {
        env_ = value;
        rmsSq_ = value * value;
    }

    float processPeak(float x) noexcept {
        const float rect = std::abs(x);
        const float c = (rect > env_) ? attackCoeff_ : releaseCoeff_;
        env_ = flushDenormal(rect + c * (env_ - rect));
        return env_;
    }

    float processRms(float x) noexcept {
        const float sq = x * x;
        const float c = (sq > rmsSq_) ? attackCoeff_ : releaseCoeff_;
        rmsSq_ = flushDenormal(sq + c * (rmsSq_ - sq));
        env_ = std::sqrt(std::max(0.0f, rmsSq_));
        return env_;
    }

    float process(float x) noexcept {
        return processPeak(x);
    }

    float getCurrent() const noexcept { return env_; }

private:
    double sampleRate_   = 48000.0;
    float  attackCoeff_  = 0.99f;
    float  releaseCoeff_ = 0.999f;
    float  env_          = 0.0f;
    float  rmsSq_        = 0.0f;
};

// ============================================================================
// Window Helpers
// ============================================================================
enum class WindowType {
    Hann,
    Hamming,
    Blackman,
    BlackmanHarris
};

inline float hannWindow(size_t index, size_t size) noexcept {
    if (size <= 1) return 1.0f;
    return 0.5f * (1.0f - std::cos(kTwoPiF * static_cast<float>(index) / static_cast<float>(size - 1)));
}

inline float blackmanWindow(size_t index, size_t size) noexcept {
    if (size <= 1) return 1.0f;
    const float phase = kTwoPiF * static_cast<float>(index) / static_cast<float>(size - 1);
    return 0.42f - 0.5f * std::cos(phase) + 0.08f * std::cos(2.0f * phase);
}

inline void fillWindow(float* dst, size_t size, WindowType type) noexcept {
    if (!dst || size == 0) return;
    if (size == 1) {
        dst[0] = 1.0f;
        return;
    }
    const double denom = static_cast<double>(size - 1);
    for (size_t i = 0; i < size; ++i) {
        const double p = kTwoPi * static_cast<double>(i) / denom;
        double w = 1.0;
        switch (type) {
            case WindowType::Hann:
                w = 0.5 * (1.0 - std::cos(p));
                break;
            case WindowType::Hamming:
                w = 0.54 - 0.46 * std::cos(p);
                break;
            case WindowType::Blackman:
                w = 0.42 - 0.5 * std::cos(p) + 0.08 * std::cos(2.0 * p);
                break;
            case WindowType::BlackmanHarris:
                w = 0.35875 - 0.48829 * std::cos(p) + 0.14128 * std::cos(2.0 * p) - 0.01168 * std::cos(3.0 * p);
                break;
        }
        dst[i] = static_cast<float>(w);
    }
}

// ============================================================================
// RealFFT (Allocation-Free After prepare(), Radix-2 Cooley-Tukey)
// ============================================================================
class RealFFT {
public:
    void prepare(size_t fftSize) {
        size_t n = 2;
        while (n < fftSize) n <<= 1;
        n_ = n;
        log2N_ = 0;
        for (size_t tmp = n_; tmp > 1; tmp >>= 1) ++log2N_;

        bitRev_.resize(n_);
        for (size_t i = 0; i < n_; ++i) {
            size_t x = i;
            size_t r = 0;
            for (size_t b = 0; b < log2N_; ++b) {
                r = (r << 1) | (x & 1);
                x >>= 1;
            }
            bitRev_[i] = r;
        }

        twiddlesFwd_.resize(n_ / 2);
        twiddlesInv_.resize(n_ / 2);
        for (size_t k = 0; k < n_ / 2; ++k) {
            const double angle = kTwoPi * static_cast<double>(k) / static_cast<double>(n_);
            twiddlesFwd_[k] = { static_cast<float>(std::cos(angle)), static_cast<float>(-std::sin(angle)) };
            twiddlesInv_[k] = { static_cast<float>(std::cos(angle)), static_cast<float>( std::sin(angle)) };
        }
        scratch_.resize(n_);
    }

    size_t getSize() const noexcept { return n_; }
    size_t getNumBins() const noexcept { return n_ > 0 ? (n_ / 2 + 1) : 0; }

    // Writes getNumBins() == N/2 + 1 complex bins into freqBins
    void forward(const float* timeDomain, std::complex<float>* freqBins) noexcept {
        if (n_ == 0 || !timeDomain || !freqBins) return;
        for (size_t i = 0; i < n_; ++i) {
            scratch_[bitRev_[i]] = { timeDomain[i], 0.0f };
        }
        transformInPlace(scratch_.data(), twiddlesFwd_.data());
        const size_t bins = n_ / 2 + 1;
        for (size_t k = 0; k < bins; ++k) {
            freqBins[k] = scratch_[k];
        }
    }

    void forwardSplit(const float* timeDomain, float* realOut, float* imagOut) noexcept {
        if (n_ == 0 || !timeDomain || !realOut || !imagOut) return;
        for (size_t i = 0; i < n_; ++i) {
            scratch_[bitRev_[i]] = { timeDomain[i], 0.0f };
        }
        transformInPlace(scratch_.data(), twiddlesFwd_.data());
        const size_t bins = n_ / 2 + 1;
        for (size_t k = 0; k < bins; ++k) {
            realOut[k] = scratch_[k].real();
            imagOut[k] = scratch_[k].imag();
        }
    }

    // Reads N/2 + 1 Hermitian bins, reconstructs N real samples (normalized by 1/N)
    void inverse(const std::complex<float>* freqBins, float* timeDomain) noexcept {
        if (n_ == 0 || !freqBins || !timeDomain) return;
        const size_t half = n_ / 2;
        scratch_[bitRev_[0]]    = { freqBins[0].real(), 0.0f };
        scratch_[bitRev_[half]] = { freqBins[half].real(), 0.0f };
        for (size_t k = 1; k < half; ++k) {
            scratch_[bitRev_[k]]      = freqBins[k];
            scratch_[bitRev_[n_ - k]] = std::conj(freqBins[k]);
        }
        transformInPlace(scratch_.data(), twiddlesInv_.data());
        const float invN = 1.0f / static_cast<float>(n_);
        for (size_t i = 0; i < n_; ++i) {
            timeDomain[i] = scratch_[i].real() * invN;
        }
    }

    void inverseSplit(const float* realIn, const float* imagIn, float* timeDomain) noexcept {
        if (n_ == 0 || !realIn || !imagIn || !timeDomain) return;
        const size_t half = n_ / 2;
        scratch_[bitRev_[0]]    = { realIn[0], 0.0f };
        scratch_[bitRev_[half]] = { realIn[half], 0.0f };
        for (size_t k = 1; k < half; ++k) {
            scratch_[bitRev_[k]]      = { realIn[k],  imagIn[k] };
            scratch_[bitRev_[n_ - k]] = { realIn[k], -imagIn[k] };
        }
        transformInPlace(scratch_.data(), twiddlesInv_.data());
        const float invN = 1.0f / static_cast<float>(n_);
        for (size_t i = 0; i < n_; ++i) {
            timeDomain[i] = scratch_[i].real() * invN;
        }
    }

    void computeMagnitudes(const std::complex<float>* freqBins, float* magOut) const noexcept {
        if (!freqBins || !magOut) return;
        const size_t bins = getNumBins();
        for (size_t k = 0; k < bins; ++k) {
            magOut[k] = std::abs(freqBins[k]);
        }
    }

private:
    void transformInPlace(std::complex<float>* data, const std::complex<float>* twiddles) const noexcept {
        for (size_t len = 2; len <= n_; len <<= 1) {
            const size_t halfLen = len >> 1;
            const size_t step = n_ / len;
            for (size_t i = 0; i < n_; i += len) {
                for (size_t j = 0; j < halfLen; ++j) {
                    const std::complex<float> u = data[i + j];
                    const std::complex<float> v = data[i + j + halfLen] * twiddles[j * step];
                    data[i + j]           = u + v;
                    data[i + j + halfLen] = u - v;
                }
            }
        }
    }

    size_t n_     = 0;
    size_t log2N_ = 0;
    std::vector<size_t>              bitRev_;
    std::vector<std::complex<float>> twiddlesFwd_;
    std::vector<std::complex<float>> twiddlesInv_;
    std::vector<std::complex<float>> scratch_;
};

} // namespace suite::dsp
