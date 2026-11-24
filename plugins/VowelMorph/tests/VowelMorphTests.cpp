#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "VowelMorphParams.h"
#include "dsp/FormantBank.h"
#include "dsp/ModulationEngine.h"
#include "dsp/PhoneticTables.h"

#include <SuiteDSP.h>
#include <SuiteHarmonyLink.h>
#include <SuiteTextureLink.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

#ifdef _WIN32
    #include <malloc.h>
#endif

// ============================================================================
// Thread-Local Real-Time Heap Allocation Tracker (Test 16)
// ============================================================================
namespace {

thread_local bool gTrackRtAllocs = false;
std::atomic<uint64_t> gRtAllocCount{ 0 };

inline void recordRtAlloc() noexcept {
    if (gTrackRtAllocs) {
        gRtAllocCount.fetch_add(1u, std::memory_order_relaxed);
    }
}

} // namespace

void* operator new(std::size_t size) {
    recordRtAlloc();
    if (size == 0) size = 1;
    void* ptr = std::malloc(size);
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

void* operator new[](std::size_t size) {
    recordRtAlloc();
    if (size == 0) size = 1;
    void* ptr = std::malloc(size);
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    recordRtAlloc();
    if (size == 0) size = 1;
    return std::malloc(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    recordRtAlloc();
    if (size == 0) size = 1;
    return std::malloc(size);
}

void operator delete(void* ptr) noexcept {
    std::free(ptr);
}

void operator delete[](void* ptr) noexcept {
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

#if defined(__cpp_aligned_new)
void* operator new(std::size_t size, std::align_val_t align) {
    recordRtAlloc();
    const std::size_t a = static_cast<std::size_t>(align);
    if (size == 0) size = a;
#ifdef _WIN32
    void* ptr = _aligned_malloc(size, a);
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, a, size) != 0) ptr = nullptr;
#endif
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

void* operator new[](std::size_t size, std::align_val_t align) {
    recordRtAlloc();
    const std::size_t a = static_cast<std::size_t>(align);
    if (size == 0) size = a;
#ifdef _WIN32
    void* ptr = _aligned_malloc(size, a);
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, a, size) != 0) ptr = nullptr;
#endif
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

void* operator new(std::size_t size, std::align_val_t align, const std::nothrow_t&) noexcept {
    recordRtAlloc();
    const std::size_t a = static_cast<std::size_t>(align);
    if (size == 0) size = a;
#ifdef _WIN32
    return _aligned_malloc(size, a);
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, a, size) != 0) ptr = nullptr;
    return ptr;
#endif
}

void* operator new[](std::size_t size, std::align_val_t align, const std::nothrow_t&) noexcept {
    recordRtAlloc();
    const std::size_t a = static_cast<std::size_t>(align);
    if (size == 0) size = a;
#ifdef _WIN32
    return _aligned_malloc(size, a);
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, a, size) != 0) ptr = nullptr;
    return ptr;
#endif
}

void operator delete(void* ptr, std::align_val_t) noexcept {
#ifdef _WIN32
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

void operator delete[](void* ptr, std::align_val_t) noexcept {
#ifdef _WIN32
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept {
#ifdef _WIN32
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept {
#ifdef _WIN32
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}
#endif

// ============================================================================
// Minimal Test Harness Helpers
// ============================================================================
namespace {

int gPassCount = 0;
int gFailCount = 0;

void reportTest(const char* name, bool ok, const std::string& detail = {}) {
    if (ok) {
        ++gPassCount;
        std::printf("[PASS] %s\n", name);
    } else {
        ++gFailCount;
        std::printf("[FAIL] %s%s%s\n",
                    name,
                    detail.empty() ? "" : " -- ",
                    detail.c_str());
    }
}

void setParamPlain(VowelMorphAudioProcessor& proc, const char* paramId, float plainValue) {
    if (auto* param = proc.getAPVTS().getParameter(paramId)) {
        param->setValueNotifyingHost(param->convertTo0to1(plainValue));
    }
}

float getParamPlain(const VowelMorphAudioProcessor& proc, const char* paramId) {
    if (auto* raw = proc.getAPVTS().getRawParameterValue(paramId)) {
        return raw->load(std::memory_order_relaxed);
    }
    return 0.0f;
}

void fillHarmonicBuffer(juce::AudioBuffer<float>& buf,
                        double sampleRate,
                        double& phase,
                        double fundamentalHz = 110.0,
                        float amplitude = 0.25f) {
    const int numCh = buf.getNumChannels();
    const int numSamples = buf.getNumSamples();
    const double inc = fundamentalHz / sampleRate;
    for (int i = 0; i < numSamples; ++i) {
        // Rich sawtooth-like harmonic excitation
        const float saw = static_cast<float>(2.0 * phase - 1.0) * amplitude;
        phase += inc;
        if (phase >= 1.0) phase -= 1.0;
        for (int ch = 0; ch < numCh; ++ch) {
            buf.setSample(ch, i, saw);
        }
    }
}

// Compute single-frequency DFT magnitude at freqHz over a signal buffer
double dftMagnitudeAt(const std::vector<float>& samples, double freqHz, double sampleRate) {
    const size_t n = samples.size();
    if (n == 0) return 0.0;
    const double omega = suite::dsp::kTwoPi * freqHz / sampleRate;
    double re = 0.0;
    double im = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double angle = omega * static_cast<double>(i);
        const double s = static_cast<double>(samples[i]);
        re += s * std::cos(angle);
        im -= s * std::sin(angle);
    }
    return std::sqrt(re * re + im * im) / static_cast<double>(n);
}

// Measure average DFT energy across a narrow frequency band around centerHz
double dftBandEnergy(const std::vector<float>& samples,
                     double lowHz,
                     double highHz,
                     int numPoints,
                     double sampleRate) {
    if (numPoints <= 0) return 0.0;
    double sumSq = 0.0;
    for (int k = 0; k < numPoints; ++k) {
        const double frac = (numPoints > 1) ? static_cast<double>(k) / static_cast<double>(numPoints - 1) : 0.5;
        const double f = lowHz + frac * (highHz - lowHz);
        const double mag = dftMagnitudeAt(samples, f, sampleRate);
        sumSq += mag * mag;
    }
    return std::sqrt(sumSq / static_cast<double>(numPoints));
}

// Capture settled linear impulse response of FormantBank for a given ProcessParams
std::vector<float> captureFormantImpulseResponse(const vowelmorph::dsp::FormantBank::ProcessParams& params,
                                                 double sampleRate = 48000.0,
                                                 int settleSamples = 4096,
                                                 int irSamples = 4096) {
    vowelmorph::dsp::FormantBank bank;
    bank.prepare(sampleRate, 256);

    float outL = 0.0f;
    float outR = 0.0f;
    for (int i = 0; i < settleSamples; ++i) {
        bank.processSample(0.0f, 0.0f, outL, outR, params);
    }

    std::vector<float> ir(static_cast<size_t>(irSamples), 0.0f);
    for (int i = 0; i < irSamples; ++i) {
        const float in = (i == 0) ? 0.25f : 0.0f;
        bank.processSample(in, in, outL, outR, params);
        ir[static_cast<size_t>(i)] = outL;
    }
    return ir;
}

// ============================================================================
// Test 1: Phonetic Table Anchor Accuracy, Nasal Attenuation & Brightness
// ============================================================================
void testPhoneticTableAnchors() {
    using namespace vowelmorph::dsp;
    const auto& table = getPhonemeTable();
    bool ok = true;
    std::string failMsg;

    // Profile 6 (TalkBoxSynth) has unit frequency scale (1.0x across F1..F5).
    // With jawOpen=0.5f and throatSize=0.0f, anchor formants must match table exactly.
    for (int i = 0; i < 10; ++i) {
        const auto& p = table[static_cast<size_t>(i)];
        const auto t = computeFormants(p.x, p.y, 0.5f, 0.0f, 6, 0.5f, 48000.0);
        if (t.nearestPhoneme != i || findNearestPhoneme(p.x, p.y) != i) {
            ok = false;
            failMsg = "Nearest phoneme mismatch at index " + std::to_string(i);
            break;
        }
        for (size_t b = 0; b < 5; ++b) {
            if (std::abs(t.freqHz[b] - p.fHz[b]) > 0.5f) {
                ok = false;
                failMsg = "Anchor frequency mismatch at phoneme " + std::to_string(i) + " band " + std::to_string(b);
                break;
            }
        }
        if (!ok) break;
    }

    // Verify MM nasal attenuation (F2..F5 substantially lower than AH)
    const auto [eeX, eeY] = getPhonemeXY(0);
    const auto [ahX, ahY] = getPhonemeXY(4);
    const auto [ooX, ooY] = getPhonemeXY(7);
    const auto [mmX, mmY] = getPhonemeXY(9);

    const auto ee = computeFormants(eeX, eeY, 0.5f, 0.0f, 2, 0.5f, 48000.0);
    const auto ah = computeFormants(ahX, ahY, 0.5f, 0.0f, 2, 0.5f, 48000.0);
    const auto oo = computeFormants(ooX, ooY, 0.5f, 0.0f, 2, 0.5f, 48000.0);
    const auto mm = computeFormants(mmX, mmY, 0.5f, 0.0f, 2, 0.5f, 48000.0);

    const float mmUpperSum = mm.gain[1] + mm.gain[2] + mm.gain[3] + mm.gain[4];
    const float ahUpperSum = ah.gain[1] + ah.gain[2] + ah.gain[3] + ah.gain[4];
    if (ok && !(mm.gain[1] < ah.gain[1] * 0.45f && mm.gain[2] < ah.gain[2] * 0.60f && mmUpperSum < ahUpperSum * 0.45f)) {
        ok = false;
        failMsg = "MM nasal F2..F5 attenuation insufficient relative to AH";
    }

    // Brightness ordering: EE > AH > OO
    if (ok && !(ee.brightness > ah.brightness && ah.brightness > oo.brightness)) {
        ok = false;
        failMsg = "Brightness ordering failed (expected EE > AH > OO)";
    }

    reportTest("1. Phonetic Table Anchor Accuracy, Nasal Attenuation & Brightness", ok, failMsg);
}

// ============================================================================
// Test 2: Continuous 2D Vowel Interpolation (EE -> AH -> OO)
// ============================================================================
void testContinuous2DInterpolation() {
    using namespace vowelmorph::dsp;
    const auto [eeX, eeY] = getPhonemeXY(0);
    const auto [ahX, ahY] = getPhonemeXY(4);
    const auto [ooX, ooY] = getPhonemeXY(7);

    bool ok = true;
    std::string failMsg;

    auto checkSegment = [&](float x0, float y0, float x1, float y1, const char* segName) {
        constexpr int kSteps = 40;
        FormantTarget prev = computeFormants(x0, y0, 0.5f, 0.0f, 2, 0.5f, 48000.0);
        for (int s = 1; s <= kSteps; ++s) {
            const float u = static_cast<float>(s) / static_cast<float>(kSteps);
            const float x = x0 + u * (x1 - x0);
            const float y = y0 + u * (y1 - y0);
            const auto cur = computeFormants(x, y, 0.5f, 0.0f, 2, 0.5f, 48000.0);

            if (!std::isfinite(cur.brightness)) {
                ok = false;
                failMsg = std::string(segName) + " produced non-finite brightness";
                return;
            }
            for (size_t b = 0; b < 5; ++b) {
                if (!std::isfinite(cur.freqHz[b]) || !std::isfinite(cur.q[b]) || !std::isfinite(cur.gain[b])) {
                    ok = false;
                    failMsg = std::string(segName) + " produced non-finite formant";
                    return;
                }
                if (cur.freqHz[b] < 80.0f || cur.freqHz[b] > 14000.0f) {
                    ok = false;
                    failMsg = std::string(segName) + " formant out of bounds";
                    return;
                }
                const float relJump = std::abs(cur.freqHz[b] - prev.freqHz[b]) / std::max(100.0f, prev.freqHz[b]);
                if (relJump > 0.28f) {
                    ok = false;
                    failMsg = std::string(segName) + " discontinuity detected (relJump=" + std::to_string(relJump) + ")";
                    return;
                }
            }
            prev = cur;
        }
    };

    checkSegment(eeX, eeY, ahX, ahY, "EE->AH");
    if (ok) checkSegment(ahX, ahY, ooX, ooY, "AH->OO");

    const auto ee = computeFormants(eeX, eeY, 0.5f, 0.0f, 2, 0.5f, 48000.0);
    const auto ah = computeFormants(ahX, ahY, 0.5f, 0.0f, 2, 0.5f, 48000.0);
    const auto oo = computeFormants(ooX, ooY, 0.5f, 0.0f, 2, 0.5f, 48000.0);
    if (ok && !(ah.freqHz[0] > ee.freqHz[0] && ah.freqHz[0] > oo.freqHz[0] &&
                ee.freqHz[1] > ah.freqHz[1] && ah.freqHz[1] > oo.freqHz[1])) {
        ok = false;
        failMsg = "EE -> AH -> OO F1/F2 trajectory endpoints invalid";
    }

    reportTest("2. Continuous 2D Vowel Interpolation (EE -> AH -> OO)", ok, failMsg);
}

// ============================================================================
// Test 3: All 8 Vocal-Tract Profiles, Throat Size & Jaw Open
// ============================================================================
void testTractProfilesThroatAndJaw() {
    using namespace vowelmorph::dsp;
    const auto [ahX, ahY] = getPhonemeXY(4);

    std::array<FormantTarget, 8> profiles {};
    for (int i = 0; i < 8; ++i) {
        profiles[static_cast<size_t>(i)] = computeFormants(ahX, ahY, 0.5f, 0.0f, i, 0.5f, 48000.0);
    }

    bool ok = true;
    std::string failMsg;

    // Bass (0) < Tenor (1) < AltoChoir (2) < Soprano (3) < Child (4) across all 5 formants
    for (size_t b = 0; b < 5; ++b) {
        if (!(profiles[0].freqHz[b] < profiles[1].freqHz[b] &&
              profiles[1].freqHz[b] < profiles[2].freqHz[b] &&
              profiles[2].freqHz[b] < profiles[3].freqHz[b] &&
              profiles[3].freqHz[b] < profiles[4].freqHz[b])) {
            ok = false;
            failMsg = "Bass..Child frequency progression broken at band " + std::to_string(b);
            break;
        }
    }

    // WhisperAir (5): broader Q than AltoChoir (2) + high-band boost (F3..F5)
    if (ok) {
        for (size_t b = 0; b < 5; ++b) {
            if (!(profiles[5].q[b] < profiles[2].q[b])) {
                ok = false;
                failMsg = "WhisperAir Q not broader than AltoChoir";
                break;
            }
        }
        if (ok && !(profiles[5].gain[2] > profiles[2].gain[2] &&
                    profiles[5].gain[3] > profiles[2].gain[3] &&
                    profiles[5].gain[4] > profiles[2].gain[4])) {
            ok = false;
            failMsg = "WhisperAir missing F3..F5 high-band boost";
        }
    }

    // TalkBoxSynth (6): sharper Q than Tenor (1)
    if (ok) {
        for (size_t b = 0; b < 5; ++b) {
            if (!(profiles[6].q[b] > profiles[1].q[b])) {
                ok = false;
                failMsg = "TalkBoxSynth Q not sharper than Tenor";
                break;
            }
        }
    }

    // AlienGiant (7): distinct non-uniform profile (lower F1 than Bass, higher F3 than Tenor)
    if (ok && !(profiles[7].freqHz[0] < profiles[0].freqHz[0] &&
                profiles[7].freqHz[2] > profiles[1].freqHz[2])) {
        ok = false;
        failMsg = "AlienGiant profile not distinct";
    }

    // Throat Size: -50% raises formants, +50% lowers formants
    const auto throatSmall = computeFormants(ahX, ahY, 0.5f, -0.5f, 2, 0.5f, 48000.0);
    const auto throatNeut  = computeFormants(ahX, ahY, 0.5f,  0.0f, 2, 0.5f, 48000.0);
    const auto throatLarge = computeFormants(ahX, ahY, 0.5f,  0.5f, 2, 0.5f, 48000.0);
    if (ok) {
        for (size_t b = 0; b < 5; ++b) {
            if (!(throatSmall.freqHz[b] > throatNeut.freqHz[b] &&
                  throatNeut.freqHz[b]  > throatLarge.freqHz[b])) {
                ok = false;
                failMsg = "Throat size scaling direction failed at band " + std::to_string(b);
                break;
            }
        }
    }

    // Jaw Open: 0% -> 50% -> 100% raises F1, and 0% dims upper bands
    const auto jawClosed = computeFormants(ahX, ahY, 0.0f, 0.0f, 2, 0.5f, 48000.0);
    const auto jawMid    = computeFormants(ahX, ahY, 0.5f, 0.0f, 2, 0.5f, 48000.0);
    const auto jawWide   = computeFormants(ahX, ahY, 1.0f, 0.0f, 2, 0.5f, 48000.0);
    if (ok && !(jawClosed.freqHz[0] < jawMid.freqHz[0] &&
                jawMid.freqHz[0]    < jawWide.freqHz[0] &&
                jawClosed.gain[3]   < jawMid.gain[3])) {
        ok = false;
        failMsg = "Jaw open F1 progression or upper-band trim failed";
    }

    reportTest("3. All 8 Vocal-Tract Profiles, Throat Size & Jaw Open", ok, failMsg);
}

// ============================================================================
// Test 4: 5-Band ZDF SVF Formant Spectral Peak Verification (Real Audio DFT)
// ============================================================================
void testZdfSpectralPeaks() {
    using namespace vowelmorph::dsp;
    constexpr double kSr = 48000.0;

    auto runVowelIr = [&](int phonemeIdx) {
        const auto [x, y] = getPhonemeXY(phonemeIdx);
        FormantBank::ProcessParams p {};
        // Profile 6 has 1.0x formant frequencies so F1/F2 match canonical IPA targets directly
        p.target       = computeFormants(x, y, 0.5f, 0.0f, 6, 0.65f, kSr);
        p.mix01        = 1.0f;
        p.breathAir01  = 0.0f;
        p.drive01      = 0.0f;
        p.choirWidth01 = 0.0f;
        p.outputGain   = 1.0f;
        return captureFormantImpulseResponse(p, kSr, 4096, 4096);
    };

    const auto irEE = runVowelIr(0); // F1 ~280, F2 ~2300
    const auto irAH = runVowelIr(4); // F1 ~730, F2 ~1090
    const auto irOO = runVowelIr(7); // F1 ~300, F2 ~870

    const double eeF1     = dftMagnitudeAt(irEE, 280.0, kSr);
    const double eeF2     = dftMagnitudeAt(irEE, 2300.0, kSr);
    const double eeValley = dftMagnitudeAt(irEE, 1100.0, kSr);
    const double eeOob    = dftMagnitudeAt(irEE, 8000.0, kSr);

    const double ahF1     = dftMagnitudeAt(irAH, 730.0, kSr);
    const double ahF2     = dftMagnitudeAt(irAH, 1090.0, kSr);
    const double ahValley = dftMagnitudeAt(irAH, 1800.0, kSr);
    const double ahOob    = dftMagnitudeAt(irAH, 8000.0, kSr);

    const double ooF1     = dftMagnitudeAt(irOO, 300.0, kSr);
    const double ooF2     = dftMagnitudeAt(irOO, 870.0, kSr);
    const double ooValley = dftMagnitudeAt(irOO, 1750.0, kSr);
    const double ooOob    = dftMagnitudeAt(irOO, 8000.0, kSr);

    const double ooAt2300 = dftMagnitudeAt(irOO, 2300.0, kSr);
    const double eeAt730  = dftMagnitudeAt(irEE, 730.0, kSr);

    bool ok = true;
    std::string failMsg;

    if (!(eeF1 > eeValley * 2.5 && eeF2 > eeValley * 2.5 && eeF2 > eeOob * 4.0)) {
        ok = false;
        failMsg = "EE spectral peaks not prominent at 280/2300 Hz";
    } else if (!(ahF1 > ahValley * 2.5 && ahF2 > ahValley * 2.5 && ahF1 > ahOob * 4.0)) {
        ok = false;
        failMsg = "AH spectral peaks not prominent at 730/1090 Hz";
    } else if (!(ooF1 > ooValley * 2.5 && ooF2 > ooValley * 2.5 && ooF1 > ooOob * 4.0)) {
        ok = false;
        failMsg = "OO spectral peaks not prominent at 300/870 Hz";
    } else if (!(eeF2 > ooAt2300 * 3.0 && ahF1 > eeAt730 * 3.0)) {
        ok = false;
        failMsg = "Cross-vowel spectral discrimination (EE vs AH vs OO) too weak";
    }

    reportTest("4. 5-Band ZDF SVF Formant Spectral Peak Verification (Real DFT)", ok, failMsg);
}

// ============================================================================
// Test 5: Analytical vs Real ZDF Response Consistency
// ============================================================================
void testAnalyticalVsRealResponse() {
    using namespace vowelmorph::dsp;
    constexpr double kSr = 48000.0;
    const auto [eeX, eeY] = getPhonemeXY(0);
    const auto target = computeFormants(eeX, eeY, 0.5f, 0.0f, 6, 0.60f, kSr);

    bool ok = true;
    std::string failMsg;

    // Sweep 50 Hz .. 16 kHz and ensure finite, bounded dB values everywhere
    for (int i = 0; i <= 200; ++i) {
        const double frac = static_cast<double>(i) / 200.0;
        const float freq = static_cast<float>(50.0 * std::pow(16000.0 / 50.0, frac));
        const float db = evaluateFormantResponseDb(freq, target, kSr);
        if (!std::isfinite(db) || db < -60.5f || db > 36.5f) {
            ok = false;
            failMsg = "Non-finite or out-of-range analytical dB at " + std::to_string(freq) + " Hz";
            break;
        }
    }

    // Verify analytical peaks at F1, F2, F3 exceed valleys between them
    const float dbF1   = evaluateFormantResponseDb(target.freqHz[0], target, kSr);
    const float dbF2   = evaluateFormantResponseDb(target.freqHz[1], target, kSr);
    const float dbF3   = evaluateFormantResponseDb(target.freqHz[2], target, kSr);
    const float dbV12  = evaluateFormantResponseDb(0.5f * (target.freqHz[0] + target.freqHz[1]), target, kSr);
    const float dbHigh = evaluateFormantResponseDb(10000.0f, target, kSr);

    if (ok && !(dbF1 > dbV12 + 8.0f && dbF2 > dbV12 + 8.0f && dbF3 > dbHigh + 8.0f)) {
        ok = false;
        failMsg = "Analytical response missing expected F1..F3 peaks";
    }

    // Compare analytical F2/F1 ratio against real ZDF impulse DFT F2/F1 ratio
    FormantBank::ProcessParams p {};
    p.target     = target;
    p.mix01      = 1.0f;
    p.outputGain = 1.0f;
    const auto ir = captureFormantImpulseResponse(p, kSr, 4096, 4096);
    const double realF1 = dftMagnitudeAt(ir, target.freqHz[0], kSr);
    const double realF2 = dftMagnitudeAt(ir, target.freqHz[1], kSr);
    const double realRatioDb = suite::dsp::gainToDb(realF2 / std::max(1.0e-9, realF1), -60.0);
    const double anaRatioDb  = static_cast<double>(dbF2 - dbF1);

    if (ok && std::abs(realRatioDb - anaRatioDb) > 3.5) {
        ok = false;
        failMsg = "Analytical vs real ZDF F2/F1 ratio diverged (ana=" + std::to_string(anaRatioDb) +
                  " dB, real=" + std::to_string(realRatioDb) + " dB)";
    }

    reportTest("5. Analytical vs Real ZDF Response Consistency", ok, failMsg);
}

// ============================================================================
// Test 6: Breath Air Aspiration Engine & Fossil Noise Boost
// ============================================================================
void testBreathAirAspiration() {
    using namespace vowelmorph::dsp;
    constexpr double kSr = 48000.0;
    const auto [ahX, ahY] = getPhonemeXY(4);
    const auto target = computeFormants(ahX, ahY, 0.6f, 0.0f, 2, 0.5f, kSr);

    auto measureUpperBandEnergy = [&](float breathAmt, float fossilBoost) {
        FormantBank bank;
        bank.prepare(kSr, 256);
        FormantBank::ProcessParams p {};
        p.target             = target;
        p.breathAir01        = breathAmt;
        p.fossilNoiseBoost01 = fossilBoost;
        p.inputEnv01         = 0.7f;
        p.mix01              = 1.0f;
        p.outputGain         = 1.0f;

        // Warm up smoothers with clean 200 Hz sine
        float outL = 0.0f;
        float outR = 0.0f;
        double phase = 0.0;
        const double inc = suite::dsp::kTwoPi * 200.0 / kSr;
        for (int i = 0; i < 4096; ++i) {
            const float s = static_cast<float>(std::sin(phase)) * 0.4f;
            phase += inc;
            bank.processSample(s, s, outL, outR, p);
        }

        std::vector<float> capture(4096, 0.0f);
        for (size_t i = 0; i < capture.size(); ++i) {
            const float s = static_cast<float>(std::sin(phase)) * 0.4f;
            phase += inc;
            bank.processSample(s, s, outL, outR, p);
            capture[i] = outL;
        }
        return dftBandEnergy(capture, 2200.0, 5200.0, 32, kSr);
    };

    const double eDry   = measureUpperBandEnergy(0.0f, 0.0f);
    const double eAir   = measureUpperBandEnergy(0.85f, 0.0f);
    const double eBoost = measureUpperBandEnergy(0.85f, 0.95f);

    bool ok = std::isfinite(eDry) && std::isfinite(eAir) && std::isfinite(eBoost) &&
              (eAir > eDry * 4.0) && (eBoost > eAir * 1.35);

    reportTest("6. Breath Air Aspiration Engine & Fossil Noise Boost",
               ok,
               ok ? "" : ("eDry=" + std::to_string(eDry) + " eAir=" + std::to_string(eAir) +
                          " eBoost=" + std::to_string(eBoost)));
}

// ============================================================================
// Test 7: Consonant Closure Dip Attenuation & Click-Free Transition
// ============================================================================
void testConsonantClosureDip() {
    using namespace vowelmorph::dsp;
    constexpr double kSr = 48000.0;
    const auto [ahX, ahY] = getPhonemeXY(4);
    const auto target = computeFormants(ahX, ahY, 0.6f, 0.0f, 2, 0.55f, kSr);

    auto measureBodyRms = [&](float dipVal) {
        FormantBank bank;
        bank.prepare(kSr, 256);
        FormantBank::ProcessParams p {};
        p.target         = target;
        p.consonantDip01 = dipVal;
        p.breathAir01    = 0.0f;
        p.mix01          = 1.0f;
        p.outputGain     = 1.0f;

        double ph1 = 0.0;
        double ph2 = 0.0;
        const double inc1 = suite::dsp::kTwoPi * static_cast<double>(target.freqHz[0]) / kSr;
        const double inc2 = suite::dsp::kTwoPi * static_cast<double>(target.freqHz[1]) / kSr;
        float outL = 0.0f;
        float outR = 0.0f;

        for (int i = 0; i < 4096; ++i) {
            const float s = 0.25f * static_cast<float>(std::sin(ph1) + std::sin(ph2));
            ph1 += inc1;
            ph2 += inc2;
            bank.processSample(s, s, outL, outR, p);
        }

        double sumSq = 0.0;
        constexpr int kMeas = 2048;
        for (int i = 0; i < kMeas; ++i) {
            const float s = 0.25f * static_cast<float>(std::sin(ph1) + std::sin(ph2));
            ph1 += inc1;
            ph2 += inc2;
            bank.processSample(s, s, outL, outR, p);
            sumSq += static_cast<double>(outL) * static_cast<double>(outL);
        }
        return std::sqrt(sumSq / static_cast<double>(kMeas));
    };

    const double rmsOpen = measureBodyRms(0.0f);
    const double rmsDip  = measureBodyRms(1.0f);

    // Also verify smooth dynamic dip pulse has no NaNs or hard clicks
    FormantBank dynBank;
    dynBank.prepare(kSr, 256);
    FormantBank::ProcessParams dynP {};
    dynP.target     = target;
    dynP.mix01      = 1.0f;
    dynP.outputGain = 1.0f;
    float prevOut = 0.0f;
    float maxDelta = 0.0f;
    bool allFinite = true;
    double ph = 0.0;
    const double inc = suite::dsp::kTwoPi * static_cast<double>(target.freqHz[0]) / kSr;
    for (int i = 0; i < 4096; ++i) {
        dynP.consonantDip01 = (i >= 1024 && i < 2048) ? 1.0f : 0.0f;
        const float s = 0.25f * static_cast<float>(std::sin(ph));
        ph += inc;
        float outL = 0.0f;
        float outR = 0.0f;
        dynBank.processSample(s, s, outL, outR, dynP);
        if (!std::isfinite(outL) || !std::isfinite(outR)) allFinite = false;
        if (i > 100) maxDelta = std::max(maxDelta, std::abs(outL - prevOut));
        prevOut = outL;
    }

    const bool ok = allFinite && (rmsDip < rmsOpen * 0.55) && (maxDelta < 0.25f);
    reportTest("7. Consonant Closure Dip Attenuation & Click-Free Transition",
               ok,
               ok ? "" : ("rmsOpen=" + std::to_string(rmsOpen) + " rmsDip=" + std::to_string(rmsDip) +
                          " maxDelta=" + std::to_string(maxDelta)));
}

// ============================================================================
// Test 8: Stereo Choir Width & Mono Compatibility
// ============================================================================
void testStereoChoirWidthAndMonoCompat() {
    using namespace vowelmorph::dsp;
    constexpr double kSr = 48000.0;
    const auto [eeX, eeY] = getPhonemeXY(0);
    const auto target = computeFormants(eeX, eeY, 0.6f, 0.0f, 2, 0.6f, kSr);

    auto measureStereo = [&](float width01, double& outMidRms, double& outSideRms, float& outMaxDiff) {
        FormantBank bank;
        bank.prepare(kSr, 256);
        FormantBank::ProcessParams p {};
        p.target       = target;
        p.choirWidth01 = width01;
        p.breathAir01  = 0.0f;
        p.mix01        = 1.0f;
        p.outputGain   = 1.0f;

        double phase = 0.0;
        const double inc = 130.0 / kSr;
        float outL = 0.0f;
        float outR = 0.0f;

        for (int i = 0; i < 4096; ++i) {
            const float s = static_cast<float>(2.0 * phase - 1.0) * 0.25f;
            phase += inc;
            if (phase >= 1.0) phase -= 1.0;
            bank.processSample(s, s, outL, outR, p);
        }

        double midSq = 0.0;
        double sideSq = 0.0;
        float maxDiff = 0.0f;
        constexpr int kMeas = 4096;
        for (int i = 0; i < kMeas; ++i) {
            const float s = static_cast<float>(2.0 * phase - 1.0) * 0.25f;
            phase += inc;
            if (phase >= 1.0) phase -= 1.0;
            bank.processSample(s, s, outL, outR, p);
            const double m = 0.5 * (static_cast<double>(outL) + static_cast<double>(outR));
            const double d = 0.5 * (static_cast<double>(outL) - static_cast<double>(outR));
            midSq += m * m;
            sideSq += d * d;
            maxDiff = std::max(maxDiff, std::abs(outL - outR));
        }
        outMidRms  = std::sqrt(midSq / static_cast<double>(kMeas));
        outSideRms = std::sqrt(sideSq / static_cast<double>(kMeas));
        outMaxDiff = maxDiff;
    };

    double mid0 = 0.0, side0 = 0.0;
    float maxDiff0 = 0.0f;
    measureStereo(0.0f, mid0, side0, maxDiff0);

    double midW = 0.0, sideW = 0.0;
    float maxDiffW = 0.0f;
    measureStereo(0.95f, midW, sideW, maxDiffW);

    const bool ok = (maxDiff0 < 1.0e-6f) &&
                    (sideW > midW * 0.05) &&
                    (midW > mid0 * 0.65);

    reportTest("8. Stereo Choir Width & Mono Compatibility",
               ok,
               ok ? "" : ("maxDiff0=" + std::to_string(maxDiff0) + " mid0=" + std::to_string(mid0) +
                          " midW=" + std::to_string(midW) + " sideW=" + std::to_string(sideW)));
}

// ============================================================================
// Test 9: Modulation Mode — Envelope Follower (Env)
// ============================================================================
void testModModeEnvFollower() {
    using namespace vowelmorph::dsp;
    ModulationEngine mod;
    mod.prepare(48000.0);

    ModulationEngine::ModConfig cfg {};
    cfg.modMode      = 0;
    cfg.modDepth     = 0.85f;
    cfg.baseX        = 0.65f;
    cfg.baseY        = 0.65f;
    cfg.baseJaw      = 0.40f;
    cfg.envAttackMs  = 5.0f;
    cfg.envReleaseMs = 120.0f;
    const ModulationEngine::ExternalLinkInputs ext {};

    ModulationEngine::ModOutput quietOut {};
    for (int i = 0; i < 20; ++i) {
        quietOut = mod.processBlock(128, 0.0f, cfg, ext);
    }

    ModulationEngine::ModOutput loudOut {};
    for (int i = 0; i < 40; ++i) {
        loudOut = mod.processBlock(128, 0.9f, cfg, ext);
    }

    const bool ok = (loudOut.vowelX < quietOut.vowelX - 0.15f) &&
                    (loudOut.vowelY < quietOut.vowelY - 0.15f) &&
                    (loudOut.jawOpen > quietOut.jawOpen + 0.15f) &&
                    (loudOut.inputEnv > 0.7f);

    reportTest("9. Modulation Mode: Envelope Follower (Env)", ok);
}

// ============================================================================
// Test 10: Modulation Mode — LFO (All 4 Shapes: Sine, Triangle, Saw, S&H)
// ============================================================================
void testModModeLfoShapes() {
    using namespace vowelmorph::dsp;
    std::array<std::vector<float>, 4> trajX {};
    bool ok = true;
    std::string failMsg;

    for (int shape = 0; shape < 4; ++shape) {
        ModulationEngine mod;
        mod.prepare(48000.0);

        ModulationEngine::ModConfig cfg {};
        cfg.modMode   = 1;
        cfg.modDepth  = 0.80f;
        cfg.baseX     = 0.50f;
        cfg.baseY     = 0.50f;
        cfg.lfoSync   = false;
        cfg.lfoRateHz = 3.0f;
        cfg.lfoShape  = shape;
        const ModulationEngine::ExternalLinkInputs ext {};

        float minX = 1.0f;
        float maxX = 0.0f;
        trajX[static_cast<size_t>(shape)].reserve(300);
        for (int b = 0; b < 300; ++b) {
            const auto out = mod.processBlock(256, 0.0f, cfg, ext);
            if (!std::isfinite(out.vowelX) || out.vowelX < 0.0f || out.vowelX > 1.0f) {
                ok = false;
                failMsg = "LFO shape " + std::to_string(shape) + " out of range";
                break;
            }
            minX = std::min(minX, out.vowelX);
            maxX = std::max(maxX, out.vowelX);
            trajX[static_cast<size_t>(shape)].push_back(out.vowelX);
        }
        if (ok && (maxX - minX) < 0.20f) {
            ok = false;
            failMsg = "LFO shape " + std::to_string(shape) + " excursion too small";
        }
        if (!ok) break;
    }

    // Verify all 4 shapes produce pairwise distinct trajectories
    if (ok) {
        for (int a = 0; a < 4; ++a) {
            for (int b = a + 1; b < 4; ++b) {
                double diffSum = 0.0;
                const auto& va = trajX[static_cast<size_t>(a)];
                const auto& vb = trajX[static_cast<size_t>(b)];
                for (size_t i = 0; i < va.size(); ++i) {
                    diffSum += std::abs(static_cast<double>(va[i] - vb[i]));
                }
                const double meanDiff = diffSum / static_cast<double>(va.size());
                if (meanDiff < 0.025) {
                    ok = false;
                    failMsg = "LFO shapes " + std::to_string(a) + " and " + std::to_string(b) + " not distinct";
                    break;
                }
            }
            if (!ok) break;
        }
    }

    reportTest("10. Modulation Mode: LFO (Sine, Triangle, Saw, S&H)", ok, failMsg);
}

// ============================================================================
// Test 11: Modulation Mode — 8-Step Syllable Sequencer (Steps, Rest, Accent, Glide, Dip)
// ============================================================================
void testModModeStepSeq() {
    using namespace vowelmorph::dsp;
    ModulationEngine mod;
    mod.prepare(48000.0);

    ModulationEngine::ModConfig cfg {};
    cfg.modMode        = 2;
    cfg.modDepth       = 1.0f;
    cfg.baseX          = 0.50f;
    cfg.baseY          = 0.50f;
    cfg.baseJaw        = 0.50f;
    cfg.bpm            = 120.0;
    cfg.seqRate        = 3; // 1/16 = 0.125 s per step
    cfg.seqGlide01     = 0.10f;
    cfg.consonantDip01 = 0.85f;
    // Step 3 = Rest (10), Step 4 = AH accented, Step 5 = AH unaccented
    cfg.seqSteps       = { 0, 2, 4, ModulationEngine::kStepRestValue, 4, 4, 7, 9 };
    cfg.seqAccents     = { false, false, false, false, true, false, false, false };
    const ModulationEngine::ExternalLinkInputs ext {};

    std::array<bool, 8> visited {};
    float minRestJaw = 1.0f;
    float maxAccentJaw = 0.0f;
    float settledPlainJawSum = 0.0f;
    int   settledPlainCount = 0;
    int   blocksInCurrentStep = 0;
    float maxDipOnTransition = 0.0f;
    uint32_t lastSeq = 0;
    bool transitionSeen = false;

    // 1.25 seconds = 10 sixteenth-note steps (covers 0..7 fully)
    for (int b = 0; b < 240; ++b) {
        const auto out = mod.processBlock(256, 0.0f, cfg, ext);
        visited[static_cast<size_t>(out.activeStep & 7)] = true;
        if (out.stepChangeSeq != lastSeq) {
            lastSeq = out.stepChangeSeq;
            transitionSeen = true;
            blocksInCurrentStep = 0;
            maxDipOnTransition = std::max(maxDipOnTransition, out.consonantDip);
        } else {
            ++blocksInCurrentStep;
        }
        if (out.activeStep == 3 && blocksInCurrentStep >= 8) {
            minRestJaw = std::min(minRestJaw, out.jawOpen);
        } else if (out.activeStep == 4 && blocksInCurrentStep >= 8) {
            maxAccentJaw = std::max(maxAccentJaw, out.jawOpen);
        } else if (out.activeStep == 5 && blocksInCurrentStep >= 12) {
            settledPlainJawSum += out.jawOpen;
            ++settledPlainCount;
        }
    }

    bool allVisited = true;
    for (bool v : visited) {
        if (!v) allVisited = false;
    }

    const float avgPlainJaw = (settledPlainCount > 0) ? (settledPlainJawSum / static_cast<float>(settledPlainCount)) : 0.5f;

    // Verify Glide slows transition relative to 0% glide
    auto measureFirstBlockJump = [&](float glide01) {
        ModulationEngine m;
        m.prepare(48000.0);
        auto c = cfg;
        c.seqGlide01 = glide01;
        c.seqSteps   = { 0, 7, 0, 7, 0, 7, 0, 7 }; // EE -> OO
        // Settle at step 0
        for (int i = 0; i < 10; ++i) (void)m.processBlock(256, 0.0f, c, ext);
        const auto before = m.processBlock(256, 0.0f, c, ext);
        // Step via host PPQ jump to step 1
        c.hostPlaying = true;
        c.ppqPosition = 0.26; // step 1
        const auto after = m.processBlock(256, 0.0f, c, ext);
        return std::abs(after.vowelX - before.vowelX);
    };
    const float fastJump = measureFirstBlockJump(0.0f);
    const float slowJump = measureFirstBlockJump(0.95f);

    const bool ok = allVisited &&
                    transitionSeen &&
                    (maxDipOnTransition > 0.15f) &&
                    (minRestJaw < avgPlainJaw - 0.15f) &&
                    (maxAccentJaw > avgPlainJaw + 0.12f) &&
                    (fastJump > slowJump * 3.0f);

    std::string detail;
    if (!ok) {
        detail = "allVisited=" + std::to_string(allVisited)
               + " dip=" + std::to_string(maxDipOnTransition)
               + " rest=" + std::to_string(minRestJaw)
               + " accent=" + std::to_string(maxAccentJaw)
               + " plain=" + std::to_string(avgPlainJaw)
               + " fast/slow=" + std::to_string(fastJump) + "/" + std::to_string(slowJump);
    }
    reportTest("11. Modulation Mode: 8-Step StepSeq (Steps, Rest, Accent, Glide, Dip)", ok, detail);
}

// ============================================================================
// Test 12: Modulation Mode — MIDI Trigger (C3=EE, E3=AH, G3=OO + Throat Shift)
// ============================================================================
void testModModeMidiTrigger() {
    VowelMorphAudioProcessor proc;
    proc.setCustomLinkSuffix("vm_unit_test_midi");
    setParamPlain(proc, vowelmorph::params::textureBus, 0.0f);
    setParamPlain(proc, vowelmorph::params::harmonyBus, 0.0f);
    setParamPlain(proc, vowelmorph::params::modMode, static_cast<float>(vowelmorph::ModMode::MidiTrigger));
    setParamPlain(proc, vowelmorph::params::modDepth, 100.0f);
    setParamPlain(proc, vowelmorph::params::seqGlide, 0.0f);
    setParamPlain(proc, vowelmorph::params::throatSize, 0.0f);
    proc.prepareToPlay(48000.0, 256);

    juce::AudioBuffer<float> buf(2, 256);
    double phase = 0.0;

    auto triggerAndSettle = [&](int midiNote) {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, midiNote, 0.9f), 0);
        fillHarmonicBuffer(buf, 48000.0, phase);
        proc.processBlock(buf, midi);

        juce::MidiBuffer emptyMidi;
        for (int b = 0; b < 12; ++b) {
            fillHarmonicBuffer(buf, 48000.0, phase);
            proc.processBlock(buf, emptyMidi);
        }
        return proc.getTelemetrySnapshot();
    };

    const auto snapEE = triggerAndSettle(48); // C3 -> EE (0)
    const auto snapAH = triggerAndSettle(52); // E3 -> AH (4)
    const auto snapOO = triggerAndSettle(55); // G3 -> OO (7)

    const bool ok = (snapEE.activePhoneme == static_cast<int>(vowelmorph::PhonemeId::EE)) &&
                    (snapAH.activePhoneme == static_cast<int>(vowelmorph::PhonemeId::AH)) &&
                    (snapOO.activePhoneme == static_cast<int>(vowelmorph::PhonemeId::OO)) &&
                    (snapOO.liveThroat < snapEE.liveThroat - 0.15f);

    reportTest("12. Modulation Mode: MIDI Trigger (C3=EE, E3=AH, G3=OO + Throat Shift)",
               ok,
               ok ? "" : ("phonemes=" + std::to_string(snapEE.activePhoneme) + "," +
                          std::to_string(snapAH.activePhoneme) + "," + std::to_string(snapOO.activePhoneme) +
                          " throatEE=" + std::to_string(snapEE.liveThroat) +
                          " throatOO=" + std::to_string(snapOO.liveThroat)));
}

// ============================================================================
// Test 13: Multi-Sample-Rate, Multi-Block-Size & Mono/Stereo Stability
// ============================================================================
void testMultiRateAndBlockStability() {
    constexpr double kRates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
    constexpr int    kBlocks[] = { 32, 64, 128, 512, 1024 };

    bool ok = true;
    std::string failMsg;

    VowelMorphAudioProcessor proc;
    proc.setCustomLinkSuffix("vm_unit_test_stab");
    setParamPlain(proc, vowelmorph::params::textureBus, 0.0f);
    setParamPlain(proc, vowelmorph::params::harmonyBus, 0.0f);

    auto runConfig = [&](double sr, int blockSize, int channels) {
        proc.prepareToPlay(sr, blockSize);
        juce::AudioBuffer<float> buf(channels, blockSize);
        juce::MidiBuffer midi;
        double phase = 0.0;

        for (int b = 0; b < 8; ++b) {
            fillHarmonicBuffer(buf, sr, phase, 110.0, 0.35f);
            proc.processBlock(buf, midi);
            for (int ch = 0; ch < channels; ++ch) {
                const float* ptr = buf.getReadPointer(ch);
                for (int i = 0; i < blockSize; ++i) {
                    if (!std::isfinite(ptr[i]) || std::abs(ptr[i]) > 4.01f) {
                        ok = false;
                        failMsg = "Non-finite/unbounded output at sr=" + std::to_string(sr) +
                                  " block=" + std::to_string(blockSize);
                        return;
                    }
                }
            }
        }

        // Silence decay check (zero denormals, finite)
        for (int b = 0; b < 24; ++b) {
            buf.clear();
            proc.processBlock(buf, midi);
            for (int ch = 0; ch < channels; ++ch) {
                const float* ptr = buf.getReadPointer(ch);
                for (int i = 0; i < blockSize; ++i) {
                    if (!std::isfinite(ptr[i]) || std::fpclassify(ptr[i]) == FP_SUBNORMAL) {
                        ok = false;
                        failMsg = "Denormal or NaN on silence decay at sr=" + std::to_string(sr);
                        return;
                    }
                }
            }
        }
    };

    for (double sr : kRates) {
        for (int bs : kBlocks) {
            runConfig(sr, bs, 2);
            if (!ok) break;
        }
        if (!ok) break;
    }

    // Mono layout check
    if (ok) {
        runConfig(48000.0, 128, 1);
    }

    reportTest("13. Multi-Sample-Rate (44.1..96k), Multi-Block (32..1024) & Mono/Stereo Stability", ok, failMsg);
}

// ============================================================================
// Test 14: All 18 Factory Presets Smoke & Audio Test
// ============================================================================
void testAllFactoryPresets() {
    VowelMorphAudioProcessor proc;
    proc.setCustomLinkSuffix("vm_unit_test_presets");
    setParamPlain(proc, vowelmorph::params::textureBus, 0.0f);
    setParamPlain(proc, vowelmorph::params::harmonyBus, 0.0f);
    proc.prepareToPlay(48000.0, 256);

    juce::AudioBuffer<float> buf(2, 256);
    juce::MidiBuffer midi;
    double phase = 0.0;

    bool ok = true;
    std::string failMsg;

    for (int p = 0; p < vowelmorph::kNumPresets; ++p) {
        proc.applyFactoryPreset(p);
        if (proc.getCurrentProgram() != p || proc.getProgramName(p).isEmpty()) {
            ok = false;
            failMsg = "Preset metadata mismatch at index " + std::to_string(p);
            break;
        }

        // Send a MIDI note for MIDI-mode preset so it articulates cleanly too
        juce::MidiBuffer initMidi;
        initMidi.addEvent(juce::MidiMessage::noteOn(1, 52, 0.85f), 0);

        float maxPeak = 0.0f;
        for (int b = 0; b < 20; ++b) {
            fillHarmonicBuffer(buf, 48000.0, phase, 110.0, 0.30f);
            proc.processBlock(buf, (b == 0) ? initMidi : midi);
            for (int ch = 0; ch < 2; ++ch) {
                const float* ptr = buf.getReadPointer(ch);
                for (int i = 0; i < 256; ++i) {
                    if (!std::isfinite(ptr[i]) || std::abs(ptr[i]) > 4.01f) {
                        ok = false;
                        failMsg = "Preset " + std::to_string(p) + " produced non-finite/clipped audio";
                        break;
                    }
                    maxPeak = std::max(maxPeak, std::abs(ptr[i]));
                }
                if (!ok) break;
            }
            if (!ok) break;
        }
        if (!ok) break;

        const auto snap = proc.getTelemetrySnapshot();
        bool snapValid = (snap.activePhoneme >= 0 && snap.activePhoneme < vowelmorph::kNumPhonemes) &&
                         (snap.tractProfile >= 0 && snap.tractProfile < vowelmorph::kNumTracts);
        for (float f : snap.formantFreqHz) {
            if (!std::isfinite(f) || f < 60.0f || f > 15000.0f) snapValid = false;
        }
        if (maxPeak < 1.0e-3f || !snapValid) {
            ok = false;
            failMsg = "Preset " + std::to_string(p) + " silent or invalid telemetry (peak=" + std::to_string(maxPeak) + ")";
            break;
        }
    }

    reportTest("14. All 18 Factory Presets Smoke & Audio Test", ok, failMsg);
}

// ============================================================================
// Test 15: State Save / Restore Round-Trip
// ============================================================================
void testStateSaveRestoreRoundTrip() {
    juce::MemoryBlock stateBlob;
    {
        VowelMorphAudioProcessor src;
        src.setCustomLinkSuffix("vm_unit_test_state_a");
        src.applyFactoryPreset(11);
        setParamPlain(src, vowelmorph::params::vowelX, 0.77f);
        setParamPlain(src, vowelmorph::params::vowelY, 0.23f);
        setParamPlain(src, vowelmorph::params::tractProfile, 5.0f);
        setParamPlain(src, vowelmorph::params::throatSize, -18.5f);
        setParamPlain(src, vowelmorph::params::seqStepIds[2], 7.0f);
        setParamPlain(src, vowelmorph::params::seqAccentIds[5], 1.0f);
        setParamPlain(src, vowelmorph::params::textureBus, 3.0f); // Bus C
        setParamPlain(src, vowelmorph::params::harmonyBus, 4.0f); // Bus D
        setParamPlain(src, vowelmorph::params::harmonyChoirDrone, 62.5f);

        src.getStateInformation(stateBlob);
    }

    VowelMorphAudioProcessor dst;
    dst.setCustomLinkSuffix("vm_unit_test_state_b");
    dst.setStateInformation(stateBlob.getData(), static_cast<int>(stateBlob.getSize()));

    const bool ok = (stateBlob.getSize() > 0) &&
                    (dst.getCurrentProgram() == 11) &&
                    (std::abs(getParamPlain(dst, vowelmorph::params::vowelX) - 0.77f) < 0.005f) &&
                    (std::abs(getParamPlain(dst, vowelmorph::params::vowelY) - 0.23f) < 0.005f) &&
                    (static_cast<int>(std::round(getParamPlain(dst, vowelmorph::params::tractProfile))) == 5) &&
                    (std::abs(getParamPlain(dst, vowelmorph::params::throatSize) - (-18.5f)) < 0.2f) &&
                    (static_cast<int>(std::round(getParamPlain(dst, vowelmorph::params::seqStepIds[2]))) == 7) &&
                    (getParamPlain(dst, vowelmorph::params::seqAccentIds[5]) >= 0.5f) &&
                    (static_cast<int>(std::round(getParamPlain(dst, vowelmorph::params::textureBus))) == 3) &&
                    (static_cast<int>(std::round(getParamPlain(dst, vowelmorph::params::harmonyBus))) == 4) &&
                    (std::abs(getParamPlain(dst, vowelmorph::params::harmonyChoirDrone) - 62.5f) < 0.2f);

    reportTest("15. State Save / Restore Round-Trip", ok);
}

// ============================================================================
// Test 16: Zero Audio-Thread Heap Allocations Check
// ============================================================================
void testZeroAudioThreadAllocations() {
    VowelMorphAudioProcessor proc;
    proc.setCustomLinkSuffix("vm_unit_test_rtalloc");
    setParamPlain(proc, vowelmorph::params::textureBus, 1.0f);
    setParamPlain(proc, vowelmorph::params::harmonyBus, 2.0f);
    setParamPlain(proc, vowelmorph::params::modMode, static_cast<float>(vowelmorph::ModMode::StepSeq));
    proc.prepareToPlay(48000.0, 256);

    juce::AudioBuffer<float> buf(2, 256);
    juce::MidiBuffer emptyMidi;
    double phase = 0.0;

    // Warm-up blocks before enabling RT allocation tracking
    for (int b = 0; b < 5; ++b) {
        fillHarmonicBuffer(buf, 48000.0, phase);
        proc.processBlock(buf, emptyMidi);
    }

    gRtAllocCount.store(0u, std::memory_order_relaxed);
    gTrackRtAllocs = true;
    for (int b = 0; b < 50; ++b) {
        fillHarmonicBuffer(buf, 48000.0, phase);
        proc.processBlock(buf, emptyMidi);
    }
    gTrackRtAllocs = false;

    const uint64_t allocs = gRtAllocCount.load(std::memory_order_relaxed);
    const bool ok = (allocs == 0u);
    reportTest("16. Zero Audio-Thread Heap Allocations (50 steady-state blocks)",
               ok,
               ok ? "" : ("detected " + std::to_string(allocs) + " heap allocation(s)"));
}

// ============================================================================
// Test 17: SuiteTextureLink IPC — VowelMorph -> Fossil Telemetry Publish
// ============================================================================
void testTextureLinkVowelPublish() {
    const char* kSuffix = "vm_unit_test_tex_pub";

    VowelMorphAudioProcessor proc;
    proc.setCustomLinkSuffix(kSuffix);
    setParamPlain(proc, vowelmorph::params::textureBus, 1.0f); // Bus A (0)
    setParamPlain(proc, vowelmorph::params::harmonyBus, 0.0f);
    setParamPlain(proc, vowelmorph::params::tractProfile, 3.0f); // Soprano
    setParamPlain(proc, vowelmorph::params::modMode, 0.0f);
    setParamPlain(proc, vowelmorph::params::modDepth, 0.0f);
    const auto [eeX, eeY] = vowelmorph::dsp::getPhonemeXY(0);
    setParamPlain(proc, vowelmorph::params::vowelX, eeX);
    setParamPlain(proc, vowelmorph::params::vowelY, eeY);
    proc.prepareToPlay(48000.0, 256);

    suite::texlink::TextureLinkEndpoint fossilPeer;
    const bool peerConnected = fossilPeer.connect(0, kSuffix);

    juce::AudioBuffer<float> buf(2, 256);
    juce::MidiBuffer midi;
    double phase = 0.0;
    for (int b = 0; b < 4; ++b) {
        fillHarmonicBuffer(buf, 48000.0, phase);
        proc.processBlock(buf, midi);
    }

    suite::texlink::VowelToFossilPayload payload {};
    bool vowelAlive = false;
    const bool readOk = fossilPeer.readVowel(payload, vowelAlive);

    bool formantsOrdered = true;
    for (int i = 0; i < 4; ++i) {
        if (!(payload.formantFreqHz[i] > 50.0f && payload.formantFreqHz[i] < payload.formantFreqHz[i + 1])) {
            formantsOrdered = false;
        }
    }

    const bool ok = peerConnected &&
                    readOk &&
                    vowelAlive &&
                    (payload.heartbeatMs > 0ull) &&
                    (std::abs(payload.vowelX - eeX) < 0.02f) &&
                    (std::abs(payload.vowelY - eeY) < 0.02f) &&
                    formantsOrdered &&
                    (payload.formantBrightness > 0.4f && payload.formantBrightness <= 1.0f) &&
                    (std::strcmp(payload.syllableLabel, "EE") == 0) &&
                    (std::strcmp(payload.tractLabel, "Soprano") == 0);

    reportTest("17. SuiteTextureLink IPC: VowelMorph -> Fossil Telemetry Publish", ok);
}

// ============================================================================
// Test 18: SuiteTextureLink IPC — Fossil Euclid Lockstep, Freeze Sweep & Modulation
// ============================================================================
void testTextureLinkFossilBidirectional() {
    const char* kSuffix = "vm_unit_test_tex_bidir";

    VowelMorphAudioProcessor proc;
    proc.setCustomLinkSuffix(kSuffix);
    setParamPlain(proc, vowelmorph::params::textureBus, 1.0f); // Bus A (0)
    setParamPlain(proc, vowelmorph::params::harmonyBus, 0.0f);
    setParamPlain(proc, vowelmorph::params::modMode, static_cast<float>(vowelmorph::ModMode::StepSeq));
    setParamPlain(proc, vowelmorph::params::modDepth, 100.0f);
    setParamPlain(proc, vowelmorph::params::linkEuclidSync, 1.0f);
    setParamPlain(proc, vowelmorph::params::linkFreezeSweep, 1.0f);
    setParamPlain(proc, vowelmorph::params::linkNoiseBreath, 1.0f);
    setParamPlain(proc, vowelmorph::params::linkTransientJaw, 1.0f);
    setParamPlain(proc, vowelmorph::params::breathAir, 10.0f);
    setParamPlain(proc, vowelmorph::params::jawOpen, 40.0f);
    setParamPlain(proc, vowelmorph::params::seqGlide, 0.0f);
    // Keep all steps identical (AH) during freeze sweep & noise/transient test
    for (int s = 0; s < vowelmorph::kNumSeqSteps; ++s) {
        setParamPlain(proc, vowelmorph::params::seqStepIds[s], 4.0f);
        setParamPlain(proc, vowelmorph::params::seqAccentIds[s], 0.0f);
    }
    proc.prepareToPlay(48000.0, 256);

    suite::texlink::TextureLinkEndpoint fossilPeer;
    bool ok = fossilPeer.connect(0, kSuffix);
    std::string failMsg;

    juce::AudioBuffer<float> buf(2, 256);
    juce::MidiBuffer midi;
    double phase = 0.0;

    // 18a. Euclidean 1-to-1 step lockstep (5 hits)
    suite::texlink::FossilToVowelPayload fp {};
    fp.flags            = suite::texlink::kFossilEuclidOn;
    fp.euclidHitSeq     = 10u;
    fp.freezeCaptureSeq = 1u;
    fp.euclidSteps      = 16u;
    fp.euclidHits       = 5u;
    fp.energyN          = 0.0f;
    fp.energyT          = 0.0f;
    fossilPeer.publishFossil(fp);

    fillHarmonicBuffer(buf, 48000.0, phase);
    proc.processBlock(buf, midi);
    const auto initSnap = proc.getTelemetrySnapshot();
    const int startStep = initSnap.activeStep;
    if (ok && !(initSnap.fossilConnected && initSnap.euclidLocked)) {
        ok = false;
        failMsg = "Fossil peer not connected or euclidLocked false";
    }

    for (int k = 1; ok && k <= 5; ++k) {
        fp.heartbeatMs  = 0ull; // auto-refresh timestamp
        fp.euclidHitSeq = 10u + static_cast<uint32_t>(k);
        fp.euclidStep   = static_cast<uint8_t>(k);
        fossilPeer.publishFossil(fp);

        fillHarmonicBuffer(buf, 48000.0, phase);
        proc.processBlock(buf, midi);
        const auto snap = proc.getTelemetrySnapshot();
        const int expectedStep = (startStep + k) & 7;
        if (snap.activeStep != expectedStep || !snap.euclidLocked) {
            ok = false;
            failMsg = "Euclid lockstep mismatch at hit " + std::to_string(k) +
                      " (got " + std::to_string(snap.activeStep) + ", expected " + std::to_string(expectedStep) + ")";
        }
    }

    // 18b. Freeze Capture Sweep (OO -> AH -> EE)
    if (ok) {
        fp.heartbeatMs      = 0ull;
        fp.freezeCaptureSeq = 2u;
        fossilPeer.publishFossil(fp);

        float earlyMaxX = 0.0f;
        float lateMinX  = 1.0f;
        // 0.70 s sweep at 48 kHz / 256 samples/block ~= 131 blocks
        for (int b = 0; b < 130; ++b) {
            if ((b % 10) == 0) {
                fp.heartbeatMs = 0ull;
                fossilPeer.publishFossil(fp);
            }
            fillHarmonicBuffer(buf, 48000.0, phase);
            proc.processBlock(buf, midi);
            const auto snap = proc.getTelemetrySnapshot();
            if (b >= 8 && b <= 35)  earlyMaxX = std::max(earlyMaxX, snap.liveVowelX);
            if (b >= 85 && b <= 115) lateMinX = std::min(lateMinX, snap.liveVowelX);
        }
        if (!(earlyMaxX > 0.59f && lateMinX < 0.44f && (earlyMaxX - lateMinX) > 0.16f)) {
            ok = false;
            failMsg = "Freeze sweep OO->AH->EE not observed (earlyMaxX=" + std::to_string(earlyMaxX) +
                      " lateMinX=" + std::to_string(lateMinX) + ")";
        }
    }

    // 18c. Fossil Noise -> Breath & Transient -> Jaw
    if (ok) {
        // Settle after freeze sweep finishes
        for (int b = 0; b < 15; ++b) {
            fp.heartbeatMs = 0ull;
            fp.energyN     = 0.0f;
            fp.energyT     = 0.0f;
            fossilPeer.publishFossil(fp);
            fillHarmonicBuffer(buf, 48000.0, phase);
            proc.processBlock(buf, midi);
        }
        const auto baseSnap = proc.getTelemetrySnapshot();

        fp.heartbeatMs = 0ull;
        fp.energyN     = 0.95f;
        fp.energyT     = 0.95f;
        fossilPeer.publishFossil(fp);
        fillHarmonicBuffer(buf, 48000.0, phase);
        proc.processBlock(buf, midi);
        const auto modSnap = proc.getTelemetrySnapshot();

        if (!(modSnap.liveBreath > baseSnap.liveBreath + 0.35f &&
              modSnap.liveJawOpen > baseSnap.liveJawOpen + 0.20f)) {
            ok = false;
            failMsg = "Fossil Noise->Breath or Transient->Jaw delta too small";
        }
    }

    reportTest("18. SuiteTextureLink IPC: Fossil Euclid Lockstep, Freeze Sweep & Mod", ok, failMsg);
}

// ============================================================================
// Test 19: Read-Only SuiteHarmonyLink Integration (Simulated SessionMate Peer)
// ============================================================================
void testHarmonyLinkIntegration() {
    const char* kSuffix = "vm_unit_test_harm";
    constexpr double kSr = 48000.0;

    VowelMorphAudioProcessor proc;
    proc.setCustomLinkSuffix(kSuffix);
    setParamPlain(proc, vowelmorph::params::textureBus, 0.0f);
    setParamPlain(proc, vowelmorph::params::harmonyBus, 2.0f); // Bus B (index 1)
    setParamPlain(proc, vowelmorph::params::harmonySectionLift, 1.0f);
    setParamPlain(proc, vowelmorph::params::harmonyChoirDrone, 90.0f);
    setParamPlain(proc, vowelmorph::params::jawOpen, 50.0f);
    setParamPlain(proc, vowelmorph::params::modMode, 0.0f);
    setParamPlain(proc, vowelmorph::params::modDepth, 0.0f);
    setParamPlain(proc, vowelmorph::params::mix, 100.0f);
    setParamPlain(proc, vowelmorph::params::breathAir, 0.0f);
    setParamPlain(proc, vowelmorph::params::choirWidth, 0.0f);
    const auto [eeX, eeY] = vowelmorph::dsp::getPhonemeXY(0);
    setParamPlain(proc, vowelmorph::params::vowelX, eeX);
    setParamPlain(proc, vowelmorph::params::vowelY, eeY);
    proc.prepareToPlay(kSr, 256);

    juce::AudioBuffer<float> buf(2, 256);
    juce::MidiBuffer midi;

    // Baseline without HarmonyLink peer active
    auto captureChordFreqEnergy = [&](VowelMorphAudioProcessor& p) {
        std::vector<float> out;
        out.reserve(4096);
        double phE = 0.0;
        double phB = 0.0;
        // Root E4 (329.63 Hz) + Fifth B4 (493.88 Hz) for Em9 (rootPc = 4)
        const double incE = suite::dsp::kTwoPi * 329.627557 / kSr;
        const double incB = suite::dsp::kTwoPi * 493.883301 / kSr;

        // Warm up smoothers (16 blocks = 4096 samples)
        for (int b = 0; b < 16; ++b) {
            for (int i = 0; i < 256; ++i) {
                const float s = 0.15f * static_cast<float>(std::sin(phE) + std::sin(phB));
                phE += incE;
                phB += incB;
                buf.setSample(0, i, s);
                buf.setSample(1, i, s);
            }
            p.processBlock(buf, midi);
        }

        // Capture 16 blocks = 4096 samples
        for (int b = 0; b < 16; ++b) {
            for (int i = 0; i < 256; ++i) {
                const float s = 0.15f * static_cast<float>(std::sin(phE) + std::sin(phB));
                phE += incE;
                phB += incB;
                buf.setSample(0, i, s);
                buf.setSample(1, i, s);
            }
            p.processBlock(buf, midi);
            const float* ptr = buf.getReadPointer(0);
            for (int i = 0; i < 256; ++i) out.push_back(ptr[i]);
        }
        return dftMagnitudeAt(out, 329.627557, kSr) + dftMagnitudeAt(out, 493.883301, kSr);
    };

    // Run a few blocks without HarmonyLink peer to capture baseline live telemetry
    buf.clear();
    for (int b = 0; b < 4; ++b) {
        proc.processBlock(buf, midi);
    }
    const auto baseSnap = proc.getTelemetrySnapshot();

    // Connect simulated SessionMate peer on Bus B (index 1) publishing Em9 + Chorus
    suite::harmony_link::HarmonyLinkEndpoint bandPeer;
    bool ok = bandPeer.connect(1, kSuffix);
    std::string failMsg;

    suite::harmony_link::BandToHudPayload bp {};
    bp.activeChord = suite::harmony_link::makeLinkChord(4, suite::harmony_link::LinkChordQuality::Min9);
    bp.songSection = 2u;
    std::snprintf(bp.sectionLabel, sizeof(bp.sectionLabel), "%s", "Chorus");
    bp.isPlaying   = 1u;

    // Measure with Chord Choir = 0% vs 90% (keeping Chorus lift constant to isolate Chord Choir resonance)
    setParamPlain(proc, vowelmorph::params::harmonyChoirDrone, 0.0f);
    bp.heartbeatMs = 0ull;
    bandPeer.publishBand(bp);
    const double droneOffEnergy = captureChordFreqEnergy(proc);

    setParamPlain(proc, vowelmorph::params::harmonyChoirDrone, 90.0f);
    bp.heartbeatMs = 0ull;
    bandPeer.publishBand(bp);
    const double droneOnEnergy = captureChordFreqEnergy(proc);
    const auto harmSnap = proc.getTelemetrySnapshot();

    if (ok && !harmSnap.harmonyConnected) {
        ok = false;
        failMsg = "harmonyConnected was false";
    } else if (ok && harmSnap.harmonyRootPc != 4) {
        ok = false;
        failMsg = "harmonyRootPc expected 4, got " + std::to_string(harmSnap.harmonyRootPc);
    } else if (ok && std::strncmp(harmSnap.harmonyChordLabel, "Em9", 3) != 0) {
        ok = false;
        failMsg = std::string("harmonyChordLabel mismatch: ") + harmSnap.harmonyChordLabel;
    } else if (ok && std::strcmp(harmSnap.harmonySectionLabel, "Chorus") != 0) {
        ok = false;
        failMsg = std::string("harmonySectionLabel mismatch: ") + harmSnap.harmonySectionLabel;
    } else if (ok && !(harmSnap.liveJawOpen >= baseSnap.liveJawOpen + 0.08f)) {
        ok = false;
        failMsg = "Chorus section jaw lift not applied";
    } else if (ok && !(droneOnEnergy > droneOffEnergy * 1.15)) {
        ok = false;
        failMsg = "Chord Choir harmonic resonance did not increase root+5th energy";
    }

    reportTest("19. Read-Only SuiteHarmonyLink Integration (Em9 + Chorus Lift + Chord Choir)", ok, failMsg);
}

// ============================================================================
// Test 20: Editor Instantiation, Resize (1040x660) & Telemetry Smoke Test
// ============================================================================
void testEditorSmoke() {
    VowelMorphAudioProcessor proc;
    proc.setCustomLinkSuffix("vm_unit_test_ui");
    setParamPlain(proc, vowelmorph::params::textureBus, 0.0f);
    setParamPlain(proc, vowelmorph::params::harmonyBus, 0.0f);
    proc.prepareToPlay(48000.0, 256);

    bool ok = false;
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
        if (editor != nullptr) {
            editor->setSize(1040, 660);

            juce::AudioBuffer<float> buf(2, 256);
            juce::MidiBuffer midi;
            double phase = 0.0;
            for (int b = 0; b < 10; ++b) {
                fillHarmonicBuffer(buf, 48000.0, phase);
                proc.processBlock(buf, midi);
            }

            const auto snap = proc.getTelemetrySnapshot();
            ok = (editor->getWidth() == 1040) &&
                 (editor->getHeight() == 660) &&
                 (snap.outputLevelL > 0.0f);
        }
    }

    reportTest("20. Editor Instantiation, Resize (1040x660) & Telemetry Smoke Test", ok);
}

} // namespace

// ============================================================================
// Entry Point
// ============================================================================
int main() {
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::printf("============================================================\n");
    std::printf("  VowelMorph C++20 Verification Suite (20 Tests)\n");
    std::printf("============================================================\n");

    testPhoneticTableAnchors();
    testContinuous2DInterpolation();
    testTractProfilesThroatAndJaw();
    testZdfSpectralPeaks();
    testAnalyticalVsRealResponse();
    testBreathAirAspiration();
    testConsonantClosureDip();
    testStereoChoirWidthAndMonoCompat();
    testModModeEnvFollower();
    testModModeLfoShapes();
    testModModeStepSeq();
    testModModeMidiTrigger();
    testMultiRateAndBlockStability();
    testAllFactoryPresets();
    testStateSaveRestoreRoundTrip();
    testZeroAudioThreadAllocations();
    testTextureLinkVowelPublish();
    testTextureLinkFossilBidirectional();
    testHarmonyLinkIntegration();
    testEditorSmoke();

    std::printf("============================================================\n");
    std::printf("  Summary: %d passed, %d failed (total %d)\n",
                gPassCount, gFailCount, gPassCount + gFailCount);
    std::printf("============================================================\n");

    return (gFailCount == 0) ? 0 : 1;
}
