#pragma once

#include "../SessionMateTypes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace sessionmate {

class DrumSynthEngine {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = (sampleRate > 8000.0 && sampleRate < 384000.0) ? sampleRate : 48000.0;
        invSr_ = 1.0f / static_cast<float>(sampleRate_);
        reset();
    }

    void reset() noexcept {
        kick_ = {};
        snare_ = {};
        hat_ = {};
        clap_ = {};
        ride_ = {};
        crash1_ = {};
        crash2_ = {};
        splash_ = {};
        tamb_ = {};
        cowbell_ = {};
        for (auto& t : toms_) t = {};
        hatOpenness_ = 0.0f;
        rngState_ = 0x12345678u;
    }

    void setPatch(int patchIndex) noexcept {
        if (patchIndex < 0) patchIndex = 0;
        patch_ = static_cast<DrumPatch>(patchIndex % 6);
    }

    void handleEvent(const BandNoteEvent& ev) noexcept {
        if (ev.type == EventType::ControlChange) {
            if (ev.data1 == 4) {
                hatOpenness_ = std::clamp(static_cast<float>(ev.data2) / 127.0f, 0.0f, 1.0f);
                // If an open hat is currently ringing and pedal closes, choke smoothly
                if (hat_.active && hat_.isOpen && hatOpenness_ < 0.22f) {
                    hat_.choking = true;
                }
            }
            return;
        }

        if (ev.type != EventType::NoteOn || ev.data2 == 0) {
            return;
        }

        const float vel = std::clamp(static_cast<float>(ev.data2) / 127.0f, 0.02f, 1.0f);
        const int note = static_cast<int>(ev.data1);

        switch (note) {
            case DrumNote::Kick:
                triggerKick(vel);
                break;
            case DrumNote::SideStick:
                triggerSnare(vel, Articulation::SideStick);
                break;
            case DrumNote::Snare:
                triggerSnare(vel, ev.articulation);
                break;
            case DrumNote::SnareRim:
                triggerSnare(vel, Articulation::Rimshot);
                break;
            case DrumNote::Clap:
                triggerClap(vel);
                break;
            case DrumNote::ClosedHat:
                triggerHat(vel, false, false, ev.articulation);
                break;
            case DrumNote::PedalHat:
                triggerHat(vel, false, true, Articulation::HatPedal);
                break;
            case DrumNote::OpenHat:
                triggerHat(vel, true, false, ev.articulation);
                break;
            case DrumNote::HighTom:
                triggerTom(0, vel, 185.0f, -0.32f);
                break;
            case DrumNote::MidTom:
                triggerTom(1, vel, 142.0f, -0.10f);
                break;
            case DrumNote::LowTom:
                triggerTom(2, vel, 108.0f, +0.18f);
                break;
            case DrumNote::HighFloorTom:
                triggerTom(3, vel, 86.0f, +0.34f);
                break;
            case DrumNote::LowFloorTom:
                triggerTom(4, vel, 68.0f, +0.42f);
                break;
            case DrumNote::Ride:
                triggerRide(vel, ev.articulation == Articulation::RideBell);
                break;
            case DrumNote::RideBell:
                triggerRide(vel, true);
                break;
            case DrumNote::Crash1:
                triggerCymbal(crash1_, vel, 0.92f, -0.42f, false);
                break;
            case DrumNote::Crash2:
                triggerCymbal(crash2_, vel, 1.06f, +0.42f, false);
                break;
            case DrumNote::Splash:
                triggerCymbal(splash_, vel, 1.38f, -0.18f, true);
                break;
            case DrumNote::Tambourine:
                triggerTambourine(vel);
                break;
            case DrumNote::Cowbell:
                triggerCowbell(vel);
                break;
            default:
                break;
        }
    }

    void renderSample(float& outL, float& outR) noexcept {
        float l = 0.0f;
        float r = 0.0f;

        // Kick (center)
        if (kick_.active) {
            const float s = renderKick();
            l += s;
            r += s;
        }

        // Snare (very slightly left for acoustic realism)
        if (snare_.active) {
            const float s = renderSnare();
            addPanned(s, -0.04f, l, r);
        }

        // Clap (slight stereo spread)
        if (clap_.active) {
            float clL = 0.0f, clR = 0.0f;
            renderClap(clL, clR);
            l += clL;
            r += clR;
        }

        // Hi-Hat (slightly left)
        if (hat_.active) {
            const float s = renderHat();
            addPanned(s, -0.28f, l, r);
        }

        // Toms (panned L -> R)
        for (auto& t : toms_) {
            if (t.active) {
                const float s = renderTom(t);
                addPanned(s, t.pan, l, r);
            }
        }

        // Ride (right, subtle stereo shimmer)
        if (ride_.active) {
            float rL = 0.0f, rR = 0.0f;
            renderRide(rL, rR);
            l += rL;
            r += rR;
        }

        // Crashes & Splash (wide stereo overheads)
        if (crash1_.active) {
            float cL = 0.0f, cR = 0.0f;
            renderCymbal(crash1_, cL, cR);
            l += cL;
            r += cR;
        }
        if (crash2_.active) {
            float cL = 0.0f, cR = 0.0f;
            renderCymbal(crash2_, cL, cR);
            l += cL;
            r += cR;
        }
        if (splash_.active) {
            float cL = 0.0f, cR = 0.0f;
            renderCymbal(splash_, cL, cR);
            l += cL;
            r += cR;
        }

        // Tambourine & Cowbell
        if (tamb_.active) {
            const float s = renderTambourine();
            addPanned(s, -0.22f, l, r);
        }
        if (cowbell_.active) {
            const float s = renderCowbell();
            addPanned(s, +0.20f, l, r);
        }

        // Gentle kit glue saturation for VintageDry / LoFiCrisp / ModernPunch
        if (patch_ == DrumPatch::VintageDry || patch_ == DrumPatch::LoFiCrisp) {
            l = fastTanh(l * 1.15f) * 0.90f;
            r = fastTanh(r * 1.15f) * 0.90f;
        } else if (patch_ == DrumPatch::ModernPunch) {
            l = fastTanh(l * 1.22f) * 0.88f;
            r = fastTanh(r * 1.22f) * 0.88f;
        }

        outL = sanitize(l);
        outR = sanitize(r);
    }

private:
    static constexpr float kTwoPi = 6.28318530717958647692f;
    static constexpr float kPi = 3.14159265358979323846f;

    struct SvfFilter {
        float ic1eq = 0.0f;
        float ic2eq = 0.0f;

        void reset() noexcept { ic1eq = 0.0f; ic2eq = 0.0f; }

        void process(float x, float freqHz, float q, float invSr,
                     float& lp, float& bp, float& hp) noexcept {
            const float f = std::clamp(freqHz * invSr, 0.0005f, 0.45f);
            const float g = std::tan(kPi * f);
            const float k = 1.0f / std::max(q, 0.5f);
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float a2 = g * a1;
            const float a3 = g * a2;

            const float v3 = x - ic2eq;
            const float v1 = a1 * ic1eq + a2 * v3;
            const float v2 = ic2eq + a2 * ic1eq + a3 * v3;
            ic1eq = flushDenormal(2.0f * v1 - ic1eq);
            ic2eq = flushDenormal(2.0f * v2 - ic2eq);

            lp = v2;
            bp = v1;
            hp = x - k * v1 - v2;
        }
    };

    struct KickVoice {
        bool active = false;
        float phase = 0.0f;
        float subPhase = 0.0f;
        float ampEnv = 0.0f;
        float pitchEnv = 0.0f;
        float clickEnv = 0.0f;
        float vel = 1.0f;
        float baseFreq = 52.0f;
        float pitchDrop = 140.0f;
        float ampDecay = 0.9994f;
        float pitchDecay = 0.996f;
        float clickDecay = 0.985f;
        float clickAmt = 0.35f;
        float drive = 1.3f;
        float subMix = 0.25f;
        SvfFilter clickFilter {};
    };

    struct SnareVoice {
        bool active = false;
        float phase1 = 0.0f;
        float phase2 = 0.0f;
        float rimPhase = 0.0f;
        float bodyEnv = 0.0f;
        float wireEnv = 0.0f;
        float rimEnv = 0.0f;
        float brushEnv = 0.0f;
        float brushAtk = 0.0f;
        float vel = 1.0f;
        float freq1 = 185.0f;
        float freq2 = 310.0f;
        float rimFreq = 860.0f;
        float bodyDecay = 0.9988f;
        float wireDecay = 0.9990f;
        float rimDecay = 0.9980f;
        float wireMix = 0.7f;
        float bodyMix = 0.6f;
        float rimMix = 0.0f;
        float wireFreq = 2600.0f;
        float wireQ = 0.85f;
        bool isBrush = false;
        bool isSideStick = false;
        SvfFilter wireFilter {};
        SvfFilter hpFilter {};
    };

    struct HatVoice {
        bool active = false;
        bool isOpen = false;
        bool choking = false;
        float env = 0.0f;
        float decay = 0.9985f;
        float vel = 1.0f;
        std::array<float, 6> phases {};
        SvfFilter hp {};
        SvfFilter bp {};
    };

    struct TomVoice {
        bool active = false;
        float phase = 0.0f;
        float phase2 = 0.0f;
        float ampEnv = 0.0f;
        float pitchEnv = 0.0f;
        float stickEnv = 0.0f;
        float baseFreq = 120.0f;
        float ampDecay = 0.9995f;
        float pitchDecay = 0.997f;
        float vel = 1.0f;
        float pan = 0.0f;
    };

    struct ClapVoice {
        bool active = false;
        int sampleCount = 0;
        float tailEnv = 0.0f;
        float tailDecay = 0.9991f;
        float vel = 1.0f;
        SvfFilter bpL {};
        SvfFilter bpR {};
    };

    struct RideVoice {
        bool active = false;
        bool isBell = false;
        float env = 0.0f;
        float pingEnv = 0.0f;
        float decay = 0.99985f;
        float vel = 1.0f;
        std::array<float, 6> phases {};
        SvfFilter bpL {};
        SvfFilter bpR {};
    };

    struct CymbalVoice {
        bool active = false;
        float env = 0.0f;
        float atkEnv = 0.0f;
        float decay = 0.99985f;
        float vel = 1.0f;
        float pitchScale = 1.0f;
        float pan = 0.0f;
        std::array<float, 6> phases {};
        SvfFilter hpL {};
        SvfFilter hpR {};
    };

    struct TambVoice {
        bool active = false;
        float env = 0.0f;
        float decay = 0.9991f;
        float vel = 1.0f;
        float p1 = 0.0f, p2 = 0.0f, p3 = 0.0f;
        SvfFilter hp {};
    };

    struct CowbellVoice {
        bool active = false;
        float env = 0.0f;
        float decay = 0.9993f;
        float vel = 1.0f;
        float p1 = 0.0f, p2 = 0.0f;
        SvfFilter bp {};
    };

    static inline float flushDenormal(float x) noexcept {
        return (std::abs(x) < 1.0e-20f) ? 0.0f : x;
    }

    static inline float sanitize(float x) noexcept {
        if (!std::isfinite(x)) return 0.0f;
        return std::clamp(flushDenormal(x), -4.0f, 4.0f);
    }

    static inline float fastTanh(float x) noexcept {
        const float clamped = std::clamp(x, -4.5f, 4.5f);
        const float x2 = clamped * clamped;
        return clamped * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    static inline void addPanned(float mono, float pan, float& l, float& r) noexcept {
        const float p = std::clamp(pan, -1.0f, 1.0f);
        const float angle = (p + 1.0f) * 0.25f * kPi;
        l += mono * std::cos(angle);
        r += mono * std::sin(angle);
    }

    inline float decayCoeff(float secondsToMinus60dB) const noexcept {
        const float s = std::max(secondsToMinus60dB, 0.004f);
        return std::exp(-6.907755f / (s * static_cast<float>(sampleRate_)));
    }

    inline float nextNoise() noexcept {
        uint32_t x = rngState_;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        rngState_ = x;
        return (static_cast<float>(x & 0x00FFFFFFu) * (1.0f / 8388608.0f)) - 1.0f;
    }

    void triggerKick(float vel) noexcept {
        kick_.active = true;
        kick_.phase = 0.0f;
        kick_.subPhase = 0.0f;
        kick_.ampEnv = 1.0f;
        kick_.pitchEnv = 1.0f;
        kick_.clickEnv = 1.0f;
        kick_.vel = vel;
        kick_.clickFilter.reset();

        switch (patch_) {
            case DrumPatch::AcousticKit:
                kick_.baseFreq = 52.0f;
                kick_.pitchDrop = 135.0f;
                kick_.ampDecay = decayCoeff(0.32f);
                kick_.pitchDecay = decayCoeff(0.055f);
                kick_.clickDecay = decayCoeff(0.012f);
                kick_.clickAmt = 0.36f * (0.5f + 0.5f * vel);
                kick_.drive = 1.25f;
                kick_.subMix = 0.22f;
                break;
            case DrumPatch::VintageDry:
                kick_.baseFreq = 58.0f;
                kick_.pitchDrop = 105.0f;
                kick_.ampDecay = decayCoeff(0.22f);
                kick_.pitchDecay = decayCoeff(0.045f);
                kick_.clickDecay = decayCoeff(0.010f);
                kick_.clickAmt = 0.24f;
                kick_.drive = 1.45f;
                kick_.subMix = 0.15f;
                break;
            case DrumPatch::BrushJazz:
                kick_.baseFreq = 62.0f;
                kick_.pitchDrop = 70.0f;
                kick_.ampDecay = decayCoeff(0.36f);
                kick_.pitchDecay = decayCoeff(0.060f);
                kick_.clickDecay = decayCoeff(0.014f);
                kick_.clickAmt = 0.14f;
                kick_.drive = 1.05f;
                kick_.subMix = 0.10f;
                break;
            case DrumPatch::ModernPunch:
                kick_.baseFreq = 48.0f;
                kick_.pitchDrop = 185.0f;
                kick_.ampDecay = decayCoeff(0.28f);
                kick_.pitchDecay = decayCoeff(0.042f);
                kick_.clickDecay = decayCoeff(0.009f);
                kick_.clickAmt = 0.48f * (0.5f + 0.5f * vel);
                kick_.drive = 1.55f;
                kick_.subMix = 0.32f;
                break;
            case DrumPatch::LoFiCrisp:
                kick_.baseFreq = 56.0f;
                kick_.pitchDrop = 115.0f;
                kick_.ampDecay = decayCoeff(0.24f);
                kick_.pitchDecay = decayCoeff(0.048f);
                kick_.clickDecay = decayCoeff(0.012f);
                kick_.clickAmt = 0.28f;
                kick_.drive = 1.65f;
                kick_.subMix = 0.18f;
                break;
            case DrumPatch::Electronic808:
                kick_.baseFreq = 44.0f;
                kick_.pitchDrop = 160.0f;
                kick_.ampDecay = decayCoeff(0.68f);
                kick_.pitchDecay = decayCoeff(0.050f);
                kick_.clickDecay = decayCoeff(0.006f);
                kick_.clickAmt = 0.20f;
                kick_.drive = 1.35f;
                kick_.subMix = 0.40f;
                break;
        }
    }

    float renderKick() noexcept {
        const float freq = kick_.baseFreq + kick_.pitchDrop * kick_.pitchEnv * kick_.pitchEnv;
        kick_.phase += freq * invSr_;
        if (kick_.phase >= 1.0f) kick_.phase -= 1.0f;

        kick_.subPhase += (kick_.baseFreq * 0.995f) * invSr_;
        if (kick_.subPhase >= 1.0f) kick_.subPhase -= 1.0f;

        const float sineBody = std::sin(kTwoPi * kick_.phase);
        const float triBody = 1.0f - 4.0f * std::abs(kick_.phase - 0.5f);
        const float body = (0.82f * sineBody + 0.18f * triBody) * kick_.ampEnv;
        const float sub = std::sin(kTwoPi * kick_.subPhase) * kick_.ampEnv * kick_.subMix;

        float lp = 0.0f, bp = 0.0f, hp = 0.0f;
        kick_.clickFilter.process(nextNoise(), 2400.0f, 1.1f, invSr_, lp, bp, hp);
        const float click = bp * kick_.clickEnv * kick_.clickAmt;

        const float raw = (body + sub + click) * kick_.vel * 1.15f;
        const float out = fastTanh(raw * kick_.drive) * 0.86f;

        kick_.ampEnv = flushDenormal(kick_.ampEnv * kick_.ampDecay);
        kick_.pitchEnv = flushDenormal(kick_.pitchEnv * kick_.pitchDecay);
        kick_.clickEnv = flushDenormal(kick_.clickEnv * kick_.clickDecay);

        if (kick_.ampEnv < 0.0005f) {
            kick_.active = false;
        }
        return out;
    }

    void triggerSnare(float vel, Articulation art) noexcept {
        snare_.active = true;
        snare_.phase1 = 0.0f;
        snare_.phase2 = 0.0f;
        snare_.rimPhase = 0.0f;
        snare_.vel = vel;
        snare_.wireFilter.reset();
        snare_.hpFilter.reset();
        snare_.isSideStick = (art == Articulation::SideStick);
        snare_.isBrush = (patch_ == DrumPatch::BrushJazz && !snare_.isSideStick);

        const bool isGhost = (art == Articulation::Ghost || vel < 0.35f);
        const bool isRim = (art == Articulation::Rimshot);

        if (snare_.isSideStick) {
            snare_.freq1 = 440.0f;
            snare_.freq2 = 1180.0f;
            snare_.rimFreq = 1850.0f;
            snare_.bodyEnv = 1.0f;
            snare_.wireEnv = 0.25f;
            snare_.rimEnv = 1.0f;
            snare_.bodyDecay = decayCoeff(0.045f);
            snare_.wireDecay = decayCoeff(0.025f);
            snare_.rimDecay = decayCoeff(0.055f);
            snare_.bodyMix = 0.55f;
            snare_.wireMix = 0.18f;
            snare_.rimMix = 0.65f;
            snare_.wireFreq = 3200.0f;
            snare_.wireQ = 1.4f;
            return;
        }

        if (snare_.isBrush) {
            snare_.freq1 = 195.0f;
            snare_.freq2 = 330.0f;
            snare_.bodyEnv = 0.65f;
            snare_.wireEnv = 1.0f;
            snare_.brushEnv = 1.0f;
            snare_.brushAtk = 0.0f;
            snare_.rimEnv = isRim ? 0.5f : 0.0f;
            snare_.bodyDecay = decayCoeff(0.14f);
            snare_.wireDecay = decayCoeff(0.28f);
            snare_.rimDecay = decayCoeff(0.10f);
            snare_.bodyMix = 0.28f;
            snare_.wireMix = 0.85f;
            snare_.rimMix = isRim ? 0.25f : 0.0f;
            snare_.wireFreq = 3600.0f;
            snare_.wireQ = 0.65f;
            return;
        }

        // Standard / patch-specific snares
        snare_.bodyEnv = 1.0f;
        snare_.wireEnv = 1.0f;
        snare_.rimEnv = isRim ? 1.0f : 0.0f;

        switch (patch_) {
            case DrumPatch::AcousticKit:
                snare_.freq1 = 188.0f;
                snare_.freq2 = 318.0f;
                snare_.rimFreq = 880.0f;
                snare_.bodyDecay = decayCoeff(isGhost ? 0.11f : 0.17f);
                snare_.wireDecay = decayCoeff(isGhost ? 0.13f : 0.24f);
                snare_.rimDecay = decayCoeff(0.18f);
                snare_.bodyMix = isGhost ? 0.42f : 0.62f;
                snare_.wireMix = isGhost ? 0.55f : 0.72f;
                snare_.rimMix = isRim ? 0.52f : 0.0f;
                snare_.wireFreq = isGhost ? 2100.0f : 2900.0f;
                snare_.wireQ = 0.85f;
                break;
            case DrumPatch::VintageDry:
                snare_.freq1 = 165.0f;
                snare_.freq2 = 278.0f;
                snare_.rimFreq = 790.0f;
                snare_.bodyDecay = decayCoeff(0.12f);
                snare_.wireDecay = decayCoeff(0.15f);
                snare_.rimDecay = decayCoeff(0.13f);
                snare_.bodyMix = 0.68f;
                snare_.wireMix = 0.62f;
                snare_.rimMix = isRim ? 0.45f : 0.0f;
                snare_.wireFreq = 2200.0f;
                snare_.wireQ = 0.95f;
                break;
            case DrumPatch::ModernPunch:
                snare_.freq1 = 202.0f;
                snare_.freq2 = 345.0f;
                snare_.rimFreq = 960.0f;
                snare_.bodyDecay = decayCoeff(0.16f);
                snare_.wireDecay = decayCoeff(0.22f);
                snare_.rimDecay = decayCoeff(0.16f);
                snare_.bodyMix = 0.66f;
                snare_.wireMix = 0.84f;
                snare_.rimMix = isRim ? 0.58f : 0.0f;
                snare_.wireFreq = 3500.0f;
                snare_.wireQ = 0.80f;
                break;
            case DrumPatch::LoFiCrisp:
                snare_.freq1 = 158.0f;
                snare_.freq2 = 264.0f;
                snare_.rimFreq = 740.0f;
                snare_.bodyDecay = decayCoeff(0.13f);
                snare_.wireDecay = decayCoeff(0.16f);
                snare_.rimDecay = decayCoeff(0.12f);
                snare_.bodyMix = 0.62f;
                snare_.wireMix = 0.68f;
                snare_.rimMix = isRim ? 0.40f : 0.0f;
                snare_.wireFreq = 2050.0f;
                snare_.wireQ = 1.05f;
                break;
            case DrumPatch::Electronic808:
                snare_.freq1 = 182.0f;
                snare_.freq2 = 332.0f;
                snare_.rimFreq = 920.0f;
                snare_.bodyDecay = decayCoeff(0.14f);
                snare_.wireDecay = decayCoeff(0.21f);
                snare_.rimDecay = decayCoeff(0.12f);
                snare_.bodyMix = 0.58f;
                snare_.wireMix = 0.78f;
                snare_.rimMix = isRim ? 0.38f : 0.0f;
                snare_.wireFreq = 4200.0f;
                snare_.wireQ = 0.72f;
                break;
            default:
                break;
        }
    }

    float renderSnare() noexcept {
        // Dual-mode shell oscillators with subtle initial pitch drop
        const float pitchLift = 1.0f + 0.14f * snare_.bodyEnv * snare_.bodyEnv;
        snare_.phase1 += (snare_.freq1 * pitchLift) * invSr_;
        snare_.phase2 += (snare_.freq2 * pitchLift) * invSr_;
        snare_.rimPhase += snare_.rimFreq * invSr_;
        if (snare_.phase1 >= 1.0f) snare_.phase1 -= 1.0f;
        if (snare_.phase2 >= 1.0f) snare_.phase2 -= 1.0f;
        if (snare_.rimPhase >= 1.0f) snare_.rimPhase -= 1.0f;

        const float shell = (0.68f * std::sin(kTwoPi * snare_.phase1) +
                             0.32f * std::sin(kTwoPi * snare_.phase2)) * snare_.bodyEnv * snare_.bodyMix;

        const float rim = (std::sin(kTwoPi * snare_.rimPhase) +
                           0.45f * std::sin(kTwoPi * snare_.rimPhase * 2.13f)) * snare_.rimEnv * snare_.rimMix;

        const float n = nextNoise();
        float lp = 0.0f, bp = 0.0f, hp = 0.0f;
        snare_.wireFilter.process(n, snare_.wireFreq, snare_.wireQ, invSr_, lp, bp, hp);

        float hpLp = 0.0f, hpBp = 0.0f, hpOut = 0.0f;
        snare_.hpFilter.process(bp + 0.35f * hp, 950.0f, 0.707f, invSr_, hpLp, hpBp, hpOut);

        float wireGain = snare_.wireEnv * snare_.wireMix;
        if (snare_.isBrush) {
            snare_.brushAtk = std::min(1.0f, snare_.brushAtk + invSr_ * 85.0f);
            wireGain *= snare_.brushAtk;
        }

        const float out = fastTanh((shell + rim + hpOut * wireGain) * snare_.vel * 1.12f) * 0.82f;

        snare_.bodyEnv = flushDenormal(snare_.bodyEnv * snare_.bodyDecay);
        snare_.wireEnv = flushDenormal(snare_.wireEnv * snare_.wireDecay);
        snare_.rimEnv = flushDenormal(snare_.rimEnv * snare_.rimDecay);

        if (snare_.bodyEnv < 0.0005f && snare_.wireEnv < 0.0005f && snare_.rimEnv < 0.0005f) {
            snare_.active = false;
        }
        return out;
    }

    void triggerHat(float vel, bool openNote, bool isPedal, Articulation art) noexcept {
        hat_.active = true;
        hat_.choking = false;
        hat_.env = 1.0f;
        hat_.vel = isPedal ? (vel * 0.65f) : vel;
        hat_.hp.reset();
        hat_.bp.reset();

        float openness = hatOpenness_;
        if (art == Articulation::HatHalfOpen) {
            openness = std::max(openness, 0.45f);
        } else if (openNote || art == Articulation::HatOpen) {
            openness = std::max(openness, 0.78f);
        } else if (isPedal || art == Articulation::HatPedal) {
            openness = 0.05f;
        }

        hat_.isOpen = (openness > 0.28f);

        float minSec = 0.045f;
        float maxSec = 0.58f;
        if (patch_ == DrumPatch::VintageDry || patch_ == DrumPatch::LoFiCrisp) {
            minSec = 0.036f;
            maxSec = 0.38f;
        } else if (patch_ == DrumPatch::Electronic808) {
            minSec = 0.042f;
            maxSec = 0.52f;
        } else if (patch_ == DrumPatch::BrushJazz) {
            minSec = 0.060f;
            maxSec = 0.62f;
        }

        const float decaySec = minSec + (maxSec - minSec) * (openness * openness * 0.65f + openness * 0.35f);
        hat_.decay = decayCoeff(decaySec);
    }

    float renderHat() noexcept {
        // 6 classic inharmonic metallic square/pulse oscillators
        static constexpr float kBaseFreqs[6] = { 263.0f, 389.0f, 521.0f, 677.0f, 829.0f, 1087.0f };
        const float pitchMult = (patch_ == DrumPatch::Electronic808) ? 1.15f
                              : (patch_ == DrumPatch::LoFiCrisp) ? 0.88f : 1.0f;

        float metal = 0.0f;
        for (int i = 0; i < 6; ++i) {
            hat_.phases[static_cast<size_t>(i)] += (kBaseFreqs[i] * pitchMult) * invSr_;
            if (hat_.phases[static_cast<size_t>(i)] >= 1.0f) {
                hat_.phases[static_cast<size_t>(i)] -= 1.0f;
            }
            metal += (hat_.phases[static_cast<size_t>(i)] < 0.5f) ? 1.0f : -1.0f;
        }
        metal *= (1.0f / 6.0f);

        const float src = 0.62f * metal + 0.38f * nextNoise();
        const float bpFreq = (patch_ == DrumPatch::VintageDry || patch_ == DrumPatch::LoFiCrisp) ? 7400.0f : 9200.0f;

        float lp1 = 0.0f, bp1 = 0.0f, hp1 = 0.0f;
        hat_.hp.process(src, 5800.0f, 0.72f, invSr_, lp1, bp1, hp1);

        float lp2 = 0.0f, bp2 = 0.0f, hp2 = 0.0f;
        hat_.bp.process(hp1, bpFreq, 1.05f, invSr_, lp2, bp2, hp2);

        const float out = (0.72f * bp2 + 0.28f * hp2) * hat_.env * hat_.vel * 0.62f;

        const float d = hat_.choking ? decayCoeff(0.018f) : hat_.decay;
        hat_.env = flushDenormal(hat_.env * d);
        if (hat_.env < 0.0005f) {
            hat_.active = false;
            hat_.choking = false;
        }
        return out;
    }

    void triggerTom(int idx, float vel, float baseFreq, float pan) noexcept {
        if (idx < 0 || idx >= 5) return;
        auto& t = toms_[static_cast<size_t>(idx)];
        t.active = true;
        t.phase = 0.0f;
        t.phase2 = 0.0f;
        t.ampEnv = 1.0f;
        t.pitchEnv = 1.0f;
        t.stickEnv = 1.0f;
        t.vel = vel;
        t.pan = pan;

        const float tuneScale = (patch_ == DrumPatch::BrushJazz) ? 1.18f
                              : (patch_ == DrumPatch::ModernPunch) ? 0.92f : 1.0f;
        t.baseFreq = baseFreq * tuneScale;

        const float dur = (patch_ == DrumPatch::VintageDry) ? 0.28f
                        : (patch_ == DrumPatch::BrushJazz) ? 0.42f : 0.38f;
        t.ampDecay = decayCoeff(dur * (120.0f / std::max(t.baseFreq, 60.0f)));
        t.pitchDecay = decayCoeff(0.075f);
    }

    float renderTom(TomVoice& t) noexcept {
        const float freq = t.baseFreq * (1.0f + 0.42f * t.pitchEnv * t.pitchEnv);
        t.phase += freq * invSr_;
        t.phase2 += (freq * 1.58f) * invSr_;
        if (t.phase >= 1.0f) t.phase -= 1.0f;
        if (t.phase2 >= 1.0f) t.phase2 -= 1.0f;

        const float body = (0.82f * std::sin(kTwoPi * t.phase) +
                            0.18f * std::sin(kTwoPi * t.phase2) * t.pitchEnv) * t.ampEnv;
        const float stick = nextNoise() * t.stickEnv * 0.14f;

        t.ampEnv = flushDenormal(t.ampEnv * t.ampDecay);
        t.pitchEnv = flushDenormal(t.pitchEnv * t.pitchDecay);
        t.stickEnv = flushDenormal(t.stickEnv * decayCoeff(0.010f));

        if (t.ampEnv < 0.0005f) {
            t.active = false;
        }
        return fastTanh((body + stick) * t.vel * 0.95f) * 0.78f;
    }

    void triggerClap(float vel) noexcept {
        clap_.active = true;
        clap_.sampleCount = 0;
        clap_.tailEnv = 1.0f;
        clap_.tailDecay = decayCoeff(patch_ == DrumPatch::Electronic808 ? 0.24f : 0.19f);
        clap_.vel = vel;
        clap_.bpL.reset();
        clap_.bpR.reset();
    }

    void renderClap(float& outL, float& outR) noexcept {
        const float tMs = static_cast<float>(clap_.sampleCount) * invSr_ * 1000.0f;
        ++clap_.sampleCount;

        // 4 multi-tap micro-bursts (0, 10, 21, 31 ms) + diffused tail
        float burst = 0.0f;
        if (tMs < 10.0f) {
            burst = std::exp(-tMs * 0.35f);
        } else if (tMs < 21.0f) {
            burst = std::exp(-(tMs - 10.0f) * 0.35f) * 0.92f;
        } else if (tMs < 31.0f) {
            burst = std::exp(-(tMs - 21.0f) * 0.35f) * 0.85f;
        } else {
            burst = clap_.tailEnv * 0.78f;
            clap_.tailEnv = flushDenormal(clap_.tailEnv * clap_.tailDecay);
        }

        const float nL = nextNoise();
        const float nR = nextNoise();
        float lp1 = 0.0f, bp1 = 0.0f, hp1 = 0.0f;
        float lp2 = 0.0f, bp2 = 0.0f, hp2 = 0.0f;
        clap_.bpL.process(nL, 1280.0f, 1.15f, invSr_, lp1, bp1, hp1);
        clap_.bpR.process(nR, 1340.0f, 1.15f, invSr_, lp2, bp2, hp2);

        const float g = burst * clap_.vel * 0.85f;
        outL = (0.72f * bp1 + 0.28f * bp2) * g;
        outR = (0.28f * bp1 + 0.72f * bp2) * g;

        if (tMs > 35.0f && clap_.tailEnv < 0.0005f) {
            clap_.active = false;
        }
    }

    void triggerRide(float vel, bool isBell) noexcept {
        ride_.active = true;
        ride_.isBell = isBell;
        ride_.env = 1.0f;
        ride_.pingEnv = 1.0f;
        ride_.vel = vel;
        ride_.decay = decayCoeff(isBell ? 0.95f : (patch_ == DrumPatch::BrushJazz ? 1.45f : 1.15f));
        ride_.bpL.reset();
        ride_.bpR.reset();
    }

    void renderRide(float& outL, float& outR) noexcept {
        static constexpr float kRideFreqs[6] = { 342.0f, 517.0f, 783.0f, 1192.0f, 1645.0f, 2410.0f };
        const float mult = ride_.isBell ? 1.38f : 1.0f;
        float metal = 0.0f;
        for (int i = 0; i < 6; ++i) {
            ride_.phases[static_cast<size_t>(i)] += (kRideFreqs[i] * mult) * invSr_;
            if (ride_.phases[static_cast<size_t>(i)] >= 1.0f) {
                ride_.phases[static_cast<size_t>(i)] -= 1.0f;
            }
            // FM-like metallic shimmer
            const float fm = std::sin(kTwoPi * ride_.phases[static_cast<size_t>(i)] * 1.41f);
            metal += std::sin(kTwoPi * ride_.phases[static_cast<size_t>(i)] + 0.65f * fm);
        }
        metal *= (1.0f / 6.0f);

        const float ping = std::sin(kTwoPi * ride_.phases[3] * (ride_.isBell ? 1.5f : 1.0f)) * ride_.pingEnv;
        const float src = 0.58f * metal + 0.24f * nextNoise() + (ride_.isBell ? 0.42f : 0.18f) * ping;

        float lp1 = 0.0f, bp1 = 0.0f, hp1 = 0.0f;
        float lp2 = 0.0f, bp2 = 0.0f, hp2 = 0.0f;
        const float f = ride_.isBell ? 3800.0f : 6200.0f;
        ride_.bpL.process(src, f, 0.95f, invSr_, lp1, bp1, hp1);
        ride_.bpR.process(src, f * 1.07f, 0.95f, invSr_, lp2, bp2, hp2);

        const float g = ride_.env * ride_.vel * 0.48f;
        float l = 0.0f, r = 0.0f;
        addPanned((bp1 + 0.35f * hp1) * g, +0.26f, l, r);
        addPanned((bp2 + 0.35f * hp2) * g, +0.38f, l, r);
        outL = l * 0.55f;
        outR = r * 0.55f;

        ride_.env = flushDenormal(ride_.env * ride_.decay);
        ride_.pingEnv = flushDenormal(ride_.pingEnv * decayCoeff(ride_.isBell ? 0.14f : 0.045f));
        if (ride_.env < 0.0005f) {
            ride_.active = false;
        }
    }

    void triggerCymbal(CymbalVoice& c, float vel, float pitchScale, float pan, bool isSplash) noexcept {
        c.active = true;
        c.env = 1.0f;
        c.atkEnv = 0.0f;
        c.vel = vel;
        c.pitchScale = pitchScale;
        c.pan = pan;
        c.decay = decayCoeff(isSplash ? 0.58f : 1.75f);
        c.hpL.reset();
        c.hpR.reset();
    }

    void renderCymbal(CymbalVoice& c, float& outL, float& outR) noexcept {
        static constexpr float kCrashFreqs[6] = { 295.0f, 447.0f, 679.0f, 991.0f, 1489.0f, 2137.0f };
        float metal = 0.0f;
        for (int i = 0; i < 6; ++i) {
            c.phases[static_cast<size_t>(i)] += (kCrashFreqs[i] * c.pitchScale) * invSr_;
            if (c.phases[static_cast<size_t>(i)] >= 1.0f) {
                c.phases[static_cast<size_t>(i)] -= 1.0f;
            }
            metal += (c.phases[static_cast<size_t>(i)] < 0.5f) ? 1.0f : -1.0f;
        }
        metal *= (1.0f / 6.0f);

        c.atkEnv = std::min(1.0f, c.atkEnv + invSr_ * 260.0f);
        const float srcL = 0.48f * metal + 0.52f * nextNoise();
        const float srcR = 0.48f * metal + 0.52f * nextNoise();

        float lp1 = 0.0f, bp1 = 0.0f, hp1 = 0.0f;
        float lp2 = 0.0f, bp2 = 0.0f, hp2 = 0.0f;
        const float cutoff = (patch_ == DrumPatch::LoFiCrisp) ? 3200.0f : 4200.0f;
        c.hpL.process(srcL, cutoff, 0.72f, invSr_, lp1, bp1, hp1);
        c.hpR.process(srcR, cutoff * 1.06f, 0.72f, invSr_, lp2, bp2, hp2);

        const float g = c.env * c.atkEnv * c.vel * 0.46f;
        float l = 0.0f, r = 0.0f;
        addPanned(hp1 * g, std::clamp(c.pan - 0.16f, -1.0f, 1.0f), l, r);
        addPanned(hp2 * g, std::clamp(c.pan + 0.16f, -1.0f, 1.0f), l, r);
        outL = l * 0.55f;
        outR = r * 0.55f;

        c.env = flushDenormal(c.env * c.decay);
        if (c.env < 0.0005f) {
            c.active = false;
        }
    }

    void triggerTambourine(float vel) noexcept {
        tamb_.active = true;
        tamb_.env = 1.0f;
        tamb_.decay = decayCoeff(0.22f);
        tamb_.vel = vel;
        tamb_.hp.reset();
    }

    float renderTambourine() noexcept {
        tamb_.p1 += 4120.0f * invSr_;
        tamb_.p2 += 5870.0f * invSr_;
        tamb_.p3 += 7640.0f * invSr_;
        if (tamb_.p1 >= 1.0f) tamb_.p1 -= 1.0f;
        if (tamb_.p2 >= 1.0f) tamb_.p2 -= 1.0f;
        if (tamb_.p3 >= 1.0f) tamb_.p3 -= 1.0f;

        const float jingle = (std::sin(kTwoPi * tamb_.p1) +
                              std::sin(kTwoPi * tamb_.p2) +
                              std::sin(kTwoPi * tamb_.p3)) * 0.25f + nextNoise() * 0.55f;
        float lp = 0.0f, bp = 0.0f, hp = 0.0f;
        tamb_.hp.process(jingle, 6400.0f, 0.85f, invSr_, lp, bp, hp);

        const float out = hp * tamb_.env * tamb_.vel * 0.45f;
        tamb_.env = flushDenormal(tamb_.env * tamb_.decay);
        if (tamb_.env < 0.0005f) tamb_.active = false;
        return out;
    }

    void triggerCowbell(float vel) noexcept {
        cowbell_.active = true;
        cowbell_.env = 1.0f;
        cowbell_.decay = decayCoeff(0.28f);
        cowbell_.vel = vel;
        cowbell_.p1 = 0.0f;
        cowbell_.p2 = 0.0f;
        cowbell_.bp.reset();
    }

    float renderCowbell() noexcept {
        cowbell_.p1 += 562.0f * invSr_;
        cowbell_.p2 += 845.0f * invSr_;
        if (cowbell_.p1 >= 1.0f) cowbell_.p1 -= 1.0f;
        if (cowbell_.p2 >= 1.0f) cowbell_.p2 -= 1.0f;

        const float sq = ((cowbell_.p1 < 0.5f) ? 0.5f : -0.5f) + ((cowbell_.p2 < 0.5f) ? 0.5f : -0.5f);
        float lp = 0.0f, bp = 0.0f, hp = 0.0f;
        cowbell_.bp.process(sq, 980.0f, 2.2f, invSr_, lp, bp, hp);

        const float out = bp * cowbell_.env * cowbell_.vel * 0.55f;
        cowbell_.env = flushDenormal(cowbell_.env * cowbell_.decay);
        if (cowbell_.env < 0.0005f) cowbell_.active = false;
        return out;
    }

    double sampleRate_ = 48000.0;
    float invSr_ = 1.0f / 48000.0f;
    DrumPatch patch_ = DrumPatch::AcousticKit;
    float hatOpenness_ = 0.0f;
    uint32_t rngState_ = 0x12345678u;

    KickVoice kick_ {};
    SnareVoice snare_ {};
    HatVoice hat_ {};
    std::array<TomVoice, 5> toms_ {};
    ClapVoice clap_ {};
    RideVoice ride_ {};
    CymbalVoice crash1_ {};
    CymbalVoice crash2_ {};
    CymbalVoice splash_ {};
    TambVoice tamb_ {};
    CowbellVoice cowbell_ {};
};

} // namespace sessionmate
