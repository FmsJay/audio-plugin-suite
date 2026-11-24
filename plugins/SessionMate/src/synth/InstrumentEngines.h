#pragma once

#include "../SessionMateTypes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace sessionmate {

namespace synth_detail {

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTwoPi = 6.28318530717958647692f;

inline float flushDenormal(float x) noexcept {
    return (std::abs(x) < 1.0e-20f) ? 0.0f : x;
}

inline float sanitize(float x) noexcept {
    if (!std::isfinite(x)) return 0.0f;
    return std::clamp(flushDenormal(x), -4.0f, 4.0f);
}

inline float fastTanh(float x) noexcept {
    const float c = std::clamp(x, -4.5f, 4.5f);
    const float c2 = c * c;
    return c * (27.0f + c2) / (27.0f + 9.0f * c2);
}

inline float midiToFreq(float midiNote) noexcept {
    return 440.0f * std::pow(2.0f, (midiNote - 69.0f) / 12.0f);
}

inline float polyBlep(float t, float dt) noexcept {
    if (dt <= 0.0f) return 0.0f;
    if (t < dt) {
        const float x = t / dt;
        return x + x - x * x - 1.0f;
    }
    if (t > 1.0f - dt) {
        const float x = (t - 1.0f) / dt;
        return x * x + x + x + 1.0f;
    }
    return 0.0f;
}

inline float blepSaw(float phase, float dt) noexcept {
    float s = 2.0f * phase - 1.0f;
    s -= polyBlep(phase, dt);
    return s;
}

inline float blepPulse(float phase, float dt, float pw) noexcept {
    float s = (phase < pw) ? 1.0f : -1.0f;
    s += polyBlep(phase, dt);
    float p2 = phase - pw;
    if (p2 < 0.0f) p2 += 1.0f;
    s -= polyBlep(p2, dt);
    return s;
}

inline void addPanned(float mono, float pan, float& l, float& r) noexcept {
    const float p = std::clamp(pan, -1.0f, 1.0f);
    const float angle = (p + 1.0f) * 0.25f * kPi;
    l += mono * std::cos(angle);
    r += mono * std::sin(angle);
}

inline float expDecay(float secondsToMinus60dB, double sampleRate) noexcept {
    const float s = std::max(secondsToMinus60dB, 0.002f);
    return std::exp(-6.907755f / (s * static_cast<float>(sampleRate)));
}

struct Svf2Pole {
    float ic1eq = 0.0f;
    float ic2eq = 0.0f;

    void reset() noexcept { ic1eq = 0.0f; ic2eq = 0.0f; }

    void process(float x, float freqHz, float q, float invSr,
                 float& lp, float& bp, float& hp) noexcept {
        const float f = std::clamp(freqHz * invSr, 0.0004f, 0.44f);
        const float g = std::tan(kPi * f);
        const float k = 1.0f / std::clamp(q, 0.5f, 14.0f);
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

struct FastRng {
    uint32_t state = 0x5A17C391u;

    void reset(uint32_t seed = 0x5A17C391u) noexcept { state = seed ? seed : 1u; }

    float next() noexcept {
        uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return (static_cast<float>(x & 0x00FFFFFFu) * (1.0f / 8388608.0f)) - 1.0f;
    }
};

} // namespace synth_detail

// ============================================================================
// 1. BassSynthEngine
// ============================================================================
class BassSynthEngine {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = (sampleRate > 8000.0 && sampleRate < 384000.0) ? sampleRate : 48000.0;
        invSr_ = 1.0f / static_cast<float>(sampleRate_);
        reset();
    }

    void reset() noexcept {
        for (auto& v : voices_) v = {};
        rng_.reset(0xB4551234u);
        pitchBendSemis_ = 0.0f;
        ageCounter_ = 0;
    }

    void setPatch(int patchIndex) noexcept {
        if (patchIndex < 0) patchIndex = 0;
        patch_ = static_cast<BassPatch>(patchIndex % 6);
    }

    void handleEvent(const BandNoteEvent& ev) noexcept {
        if (ev.type == EventType::PitchBend) {
            const int bend14 = (static_cast<int>(ev.data2) << 7) | static_cast<int>(ev.data1);
            pitchBendSemis_ = (static_cast<float>(bend14 - 8192) / 8192.0f) * 2.0f;
            return;
        }
        if (ev.type == EventType::NoteOff || (ev.type == EventType::NoteOn && ev.data2 == 0)) {
            for (auto& v : voices_) {
                if (v.active && v.note == ev.data1) {
                    v.gate = false;
                }
            }
            return;
        }
        if (ev.type != EventType::NoteOn) return;

        // Mono-priority with gentle release: release all currently gated voices
        for (auto& v : voices_) {
            if (v.active) {
                v.gate = false;
                v.fastRelease = true;
            }
        }

        // Pick free or oldest voice
        Voice* target = &voices_[0];
        for (auto& v : voices_) {
            if (!v.active) {
                target = &v;
                break;
            }
            if (v.age < target->age) {
                target = &v;
            }
        }

        target->active = true;
        target->gate = true;
        target->fastRelease = false;
        target->note = ev.data1;
        target->vel = std::clamp(static_cast<float>(ev.data2) / 127.0f, 0.05f, 1.0f);
        target->age = ++ageCounter_;
        target->phase = 0.0f;
        target->subPhase = 0.0f;
        target->ampEnv = 0.0f;
        target->attackStage = true;
        target->fltEnv = 1.0f;
        target->transientEnv = 1.0f;
        target->isSlap = (patch_ == BassPatch::SlapFunk || ev.articulation == Articulation::Slap);
        target->isMute = (ev.articulation == Articulation::PalmMute);
        target->filter.reset();
        target->popFilter.reset();
    }

    void renderSample(float& outL, float& outR) noexcept {
        float sum = 0.0f;
        for (auto& v : voices_) {
            if (!v.active) continue;
            sum += renderVoice(v);
        }

        const float out = synth_detail::sanitize(synth_detail::fastTanh(sum * 0.92f));
        outL = out;
        outR = out;
    }

private:
    struct Voice {
        bool active = false;
        bool gate = false;
        bool fastRelease = false;
        bool attackStage = false;
        bool isSlap = false;
        bool isMute = false;
        uint8_t note = 36;
        uint32_t age = 0;
        float vel = 0.8f;
        float phase = 0.0f;
        float subPhase = 0.0f;
        float ampEnv = 0.0f;
        float fltEnv = 0.0f;
        float transientEnv = 0.0f;
        synth_detail::Svf2Pole filter {};
        synth_detail::Svf2Pole popFilter {};
    };

    float renderVoice(Voice& v) noexcept {
        const float freq = synth_detail::midiToFreq(static_cast<float>(v.note) + pitchBendSemis_);
        const float dt = std::clamp(freq * invSr_, 0.0001f, 0.45f);
        v.phase += dt;
        if (v.phase >= 1.0f) v.phase -= 1.0f;

        v.subPhase += dt * 0.5f;
        if (v.subPhase >= 1.0f) v.subPhase -= 1.0f;

        // Attack / Decay / Release
        const float atkRate = (patch_ == BassPatch::UprightJazz) ? 140.0f : 380.0f;
        if (v.attackStage) {
            v.ampEnv += invSr_ * atkRate;
            if (v.ampEnv >= 1.0f) {
                v.ampEnv = 1.0f;
                v.attackStage = false;
            }
        } else if (v.gate) {
            float holdDecaySec = 2.6f;
            if (patch_ == BassPatch::UprightJazz) holdDecaySec = 0.72f;
            else if (patch_ == BassPatch::SlapFunk) holdDecaySec = 1.15f;
            else if (patch_ == BassPatch::FingerBass) holdDecaySec = 1.85f;
            else if (patch_ == BassPatch::SynthSub) holdDecaySec = 6.0f;
            if (v.isMute) holdDecaySec *= 0.25f;
            v.ampEnv = synth_detail::flushDenormal(v.ampEnv * synth_detail::expDecay(holdDecaySec, sampleRate_));
        } else {
            const float relSec = v.fastRelease ? 0.045f : (patch_ == BassPatch::UprightJazz ? 0.14f : 0.09f);
            v.ampEnv = synth_detail::flushDenormal(v.ampEnv * synth_detail::expDecay(relSec, sampleRate_));
        }

        if (!v.attackStage && v.ampEnv < 0.0004f) {
            v.active = false;
            return 0.0f;
        }

        const float sin1 = std::sin(synth_detail::kTwoPi * v.phase);
        const float sin2 = std::sin(synth_detail::kTwoPi * 2.0f * v.phase);
        const float sin3 = std::sin(synth_detail::kTwoPi * 3.0f * v.phase);
        const float saw = synth_detail::blepSaw(v.phase, dt);
        const float sq = synth_detail::blepPulse(v.phase, dt, 0.5f);

        float osc = 0.0f;
        float baseCut = 260.0f;
        float envCut = 1400.0f;
        float q = 1.1f;
        float fltDecaySec = 0.22f;
        float drive = 1.15f;

        switch (patch_) {
            case BassPatch::FingerBass:
                osc = 0.62f * sin1 + 0.24f * sin2 + 0.10f * sin3 + 0.14f * saw;
                baseCut = 240.0f;
                envCut = 1150.0f;
                q = 1.15f;
                fltDecaySec = 0.18f;
                drive = 1.18f;
                break;
            case BassPatch::PickedRock:
                osc = 0.32f * sin1 + 0.52f * saw + 0.22f * sq;
                baseCut = 340.0f;
                envCut = 2300.0f;
                q = 1.45f;
                fltDecaySec = 0.16f;
                drive = 1.55f;
                break;
            case BassPatch::SlapFunk:
                osc = 0.48f * sin1 + 0.26f * sin2 + 0.34f * saw;
                baseCut = 320.0f;
                envCut = 3200.0f;
                q = 1.65f;
                fltDecaySec = 0.14f;
                drive = 1.35f;
                break;
            case BassPatch::UprightJazz:
                osc = 0.72f * sin1 + 0.20f * sin2 + 0.08f * sin3;
                baseCut = 210.0f;
                envCut = 620.0f;
                q = 0.85f;
                fltDecaySec = 0.11f;
                drive = 1.08f;
                break;
            case BassPatch::SynthSub:
                osc = 0.78f * sin1 + 0.24f * std::sin(synth_detail::kTwoPi * v.subPhase) + 0.12f * saw;
                baseCut = 180.0f;
                envCut = 520.0f;
                q = 0.95f;
                fltDecaySec = 0.25f;
                drive = 1.12f;
                break;
            case BassPatch::Acid303:
                osc = 0.75f * saw + 0.25f * sq;
                baseCut = 210.0f;
                envCut = 3600.0f;
                q = 4.6f;
                fltDecaySec = 0.24f;
                drive = 1.65f;
                break;
        }

        const float cutoff = baseCut + envCut * v.fltEnv * (0.35f + 0.65f * v.vel);
        float lp = 0.0f, bp = 0.0f, hp = 0.0f;
        v.filter.process(osc, cutoff, q, invSr_, lp, bp, hp);

        float transient = 0.0f;
        if (v.isSlap) {
            float pLp = 0.0f, pBp = 0.0f, pHp = 0.0f;
            v.popFilter.process(rng_.next() + 0.5f * saw, 2800.0f, 1.8f, invSr_, pLp, pBp, pHp);
            transient = pBp * v.transientEnv * 0.45f * v.vel;
        } else if (patch_ == BassPatch::UprightJazz || patch_ == BassPatch::PickedRock) {
            float pLp = 0.0f, pBp = 0.0f, pHp = 0.0f;
            const float f = (patch_ == BassPatch::UprightJazz) ? 850.0f : 2400.0f;
            v.popFilter.process(rng_.next(), f, 1.2f, invSr_, pLp, pBp, pHp);
            transient = pBp * v.transientEnv * 0.16f * v.vel;
        }

        v.fltEnv = synth_detail::flushDenormal(v.fltEnv * synth_detail::expDecay(fltDecaySec, sampleRate_));
        v.transientEnv = synth_detail::flushDenormal(v.transientEnv * synth_detail::expDecay(0.018f, sampleRate_));

        return synth_detail::fastTanh((lp + transient) * drive) * v.ampEnv * v.vel * 0.62f;
    }

    double sampleRate_ = 48000.0;
    float invSr_ = 1.0f / 48000.0f;
    BassPatch patch_ = BassPatch::FingerBass;
    float pitchBendSemis_ = 0.0f;
    uint32_t ageCounter_ = 0;
    std::array<Voice, 4> voices_ {};
    synth_detail::FastRng rng_ {};
};

// ============================================================================
// 2. KeysSynthEngine (12-voice polyphonic + CC64 sustain)
// ============================================================================
class KeysSynthEngine {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = (sampleRate > 8000.0 && sampleRate < 384000.0) ? sampleRate : 48000.0;
        invSr_ = 1.0f / static_cast<float>(sampleRate_);
        reset();
    }

    void reset() noexcept {
        for (auto& v : voices_) v = {};
        sustainPedal_ = false;
        tremPhase_ = 0.0f;
        ageCounter_ = 0;
        rng_.reset(0x4E751234u);
    }

    void setPatch(int patchIndex) noexcept {
        if (patchIndex < 0) patchIndex = 0;
        patch_ = static_cast<KeysPatch>(patchIndex % 6);
    }

    void handleEvent(const BandNoteEvent& ev) noexcept {
        if (ev.type == EventType::ControlChange) {
            if (ev.data1 == 64) {
                const bool down = ev.data2 >= 64;
                if (sustainPedal_ && !down) {
                    for (auto& v : voices_) {
                        if (v.active && v.sustained) {
                            v.sustained = false;
                            v.gate = false;
                        }
                    }
                }
                sustainPedal_ = down;
            }
            return;
        }

        if (ev.type == EventType::NoteOff || (ev.type == EventType::NoteOn && ev.data2 == 0)) {
            for (auto& v : voices_) {
                if (v.active && v.note == ev.data1 && v.gate) {
                    if (sustainPedal_) {
                        v.sustained = true;
                    } else {
                        v.gate = false;
                    }
                }
            }
            return;
        }

        if (ev.type != EventType::NoteOn) return;

        // Reuse same note or free/oldest voice
        Voice* target = nullptr;
        for (auto& v : voices_) {
            if (v.active && v.note == ev.data1) {
                target = &v;
                break;
            }
        }
        if (!target) {
            for (auto& v : voices_) {
                if (!v.active) {
                    target = &v;
                    break;
                }
            }
        }
        if (!target) {
            target = &voices_[0];
            for (auto& v : voices_) {
                if (v.age < target->age) target = &v;
            }
        }

        target->active = true;
        target->gate = true;
        target->sustained = false;
        target->attackStage = true;
        target->note = ev.data1;
        target->vel = std::clamp(static_cast<float>(ev.data2) / 127.0f, 0.05f, 1.0f);
        target->age = ++ageCounter_;
        target->phase = 0.0f;
        target->phase2 = 0.0f;
        target->phase3 = 0.0f;
        target->ampEnv = 0.0f;
        target->tineEnv = 1.0f;
        target->clickEnv = 1.0f;
        target->pan = std::clamp((static_cast<float>(ev.data1) - 60.0f) * 0.022f, -0.45f, 0.45f);
        target->filter.reset();
    }

    void renderSample(float& outL, float& outR) noexcept {
        // Stereo tremolo / rotary LFO
        const float lfoRate = (patch_ == KeysPatch::B3Organ) ? 5.6f
                            : (patch_ == KeysPatch::RhodesMk1) ? 4.2f : 0.45f;
        tremPhase_ += lfoRate * invSr_;
        if (tremPhase_ >= 1.0f) tremPhase_ -= 1.0f;
        const float lfoSin = std::sin(synth_detail::kTwoPi * tremPhase_);

        float sumL = 0.0f;
        float sumR = 0.0f;

        for (auto& v : voices_) {
            if (!v.active) continue;
            renderVoice(v, lfoSin, sumL, sumR);
        }

        // Rhodes stereo autopan tremolo
        if (patch_ == KeysPatch::RhodesMk1) {
            const float tremL = 0.86f + 0.14f * lfoSin;
            const float tremR = 0.86f - 0.14f * lfoSin;
            sumL *= tremL;
            sumR *= tremR;
        }

        outL = synth_detail::sanitize(synth_detail::fastTanh(sumL * 0.82f));
        outR = synth_detail::sanitize(synth_detail::fastTanh(sumR * 0.82f));
    }

private:
    struct Voice {
        bool active = false;
        bool gate = false;
        bool sustained = false;
        bool attackStage = false;
        uint8_t note = 60;
        uint32_t age = 0;
        float vel = 0.8f;
        float phase = 0.0f;
        float phase2 = 0.0f;
        float phase3 = 0.0f;
        float ampEnv = 0.0f;
        float tineEnv = 0.0f;
        float clickEnv = 0.0f;
        float pan = 0.0f;
        synth_detail::Svf2Pole filter {};
    };

    void renderVoice(Voice& v, float lfoSin, float& sumL, float& sumR) noexcept {
        const float baseFreq = synth_detail::midiToFreq(static_cast<float>(v.note));
        const float dt = std::clamp(baseFreq * invSr_, 0.0001f, 0.44f);

        // Envelope rates by patch
        float atkRate = 320.0f;
        float decaySec = 2.8f;
        float relSec = 0.25f;

        switch (patch_) {
            case KeysPatch::RhodesMk1:     atkRate = 360.0f; decaySec = 3.2f; relSec = 0.30f; break;
            case KeysPatch::Wurlitzer:     atkRate = 420.0f; decaySec = 2.1f; relSec = 0.22f; break;
            case KeysPatch::AcousticPiano: atkRate = 500.0f; decaySec = 2.4f; relSec = 0.35f; break;
            case KeysPatch::B3Organ:       atkRate = 450.0f; decaySec = 40.0f; relSec = 0.06f; break;
            case KeysPatch::Clavinet:      atkRate = 600.0f; decaySec = 0.75f; relSec = 0.08f; break;
            case KeysPatch::WarmPad:       atkRate = 3.2f;   decaySec = 18.0f; relSec = 0.95f; break;
        }

        if (v.attackStage) {
            v.ampEnv += invSr_ * atkRate;
            if (v.ampEnv >= 1.0f) {
                v.ampEnv = 1.0f;
                v.attackStage = false;
            }
        } else if (v.gate) {
            v.ampEnv = synth_detail::flushDenormal(v.ampEnv * synth_detail::expDecay(decaySec, sampleRate_));
        } else {
            v.ampEnv = synth_detail::flushDenormal(v.ampEnv * synth_detail::expDecay(relSec, sampleRate_));
        }

        if (!v.attackStage && v.ampEnv < 0.0003f) {
            v.active = false;
            return;
        }

        float sigL = 0.0f;
        float sigR = 0.0f;

        if (patch_ == KeysPatch::RhodesMk1) {
            v.phase += dt;
            if (v.phase >= 1.0f) v.phase -= 1.0f;
            const float fmIndex = (0.35f + 1.35f * v.vel) * v.tineEnv;
            const float bell = std::sin(synth_detail::kTwoPi * v.phase * 7.0f) * fmIndex;
            const float tone = std::sin(synth_detail::kTwoPi * v.phase + 0.22f * bell)
                             + 0.16f * std::sin(synth_detail::kTwoPi * 2.0f * v.phase) * v.tineEnv
                             + 0.12f * bell;
            const float s = tone * v.ampEnv * v.vel * 0.24f;
            synth_detail::addPanned(s, v.pan, sigL, sigR);
            v.tineEnv = synth_detail::flushDenormal(v.tineEnv * synth_detail::expDecay(0.42f, sampleRate_));
        } else if (patch_ == KeysPatch::Wurlitzer) {
            v.phase += dt;
            if (v.phase >= 1.0f) v.phase -= 1.0f;
            const float reed = std::sin(synth_detail::kTwoPi * v.phase)
                             + 0.34f * std::sin(synth_detail::kTwoPi * 2.0f * v.phase)
                             + 0.22f * std::sin(synth_detail::kTwoPi * 3.0f * v.phase) * (0.4f + 0.6f * v.tineEnv)
                             + 0.14f * std::sin(synth_detail::kTwoPi * 5.0f * v.phase) * v.tineEnv;
            const float bark = synth_detail::fastTanh(reed * (1.1f + 0.9f * v.vel));
            const float s = bark * v.ampEnv * v.vel * 0.22f;
            synth_detail::addPanned(s, v.pan, sigL, sigR);
            v.tineEnv = synth_detail::flushDenormal(v.tineEnv * synth_detail::expDecay(0.28f, sampleRate_));
        } else if (patch_ == KeysPatch::AcousticPiano) {
            v.phase += dt;
            v.phase2 += dt * 1.0014f; // slight unison string detune
            if (v.phase >= 1.0f) v.phase -= 1.0f;
            if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;

            const float bright = (0.35f + 0.65f * v.vel) * v.tineEnv;
            const float h1 = 0.55f * (std::sin(synth_detail::kTwoPi * v.phase) + std::sin(synth_detail::kTwoPi * v.phase2));
            const float h2 = 0.36f * std::sin(synth_detail::kTwoPi * 2.0f * v.phase);
            const float h3 = 0.22f * std::sin(synth_detail::kTwoPi * 3.0f * v.phase) * bright;
            const float h4 = 0.14f * std::sin(synth_detail::kTwoPi * 4.0f * v.phase) * bright;
            const float h5 = 0.08f * std::sin(synth_detail::kTwoPi * 5.0f * v.phase) * bright * bright;
            const float hammer = rng_.next() * v.clickEnv * 0.12f * v.vel;

            float lp = 0.0f, bp = 0.0f, hp = 0.0f;
            const float cut = 650.0f + (4800.0f * v.vel * v.tineEnv) + baseFreq * 2.0f;
            v.filter.process(h1 + h2 + h3 + h4 + h5 + hammer, cut, 0.72f, invSr_, lp, bp, hp);

            const float s = lp * v.ampEnv * v.vel * 0.22f;
            synth_detail::addPanned(s, v.pan, sigL, sigR);
            v.tineEnv = synth_detail::flushDenormal(v.tineEnv * synth_detail::expDecay(0.85f, sampleRate_));
            v.clickEnv = synth_detail::flushDenormal(v.clickEnv * synth_detail::expDecay(0.012f, sampleRate_));
        } else if (patch_ == KeysPatch::B3Organ) {
            const float vib = 1.0f + 0.0032f * lfoSin;
            v.phase += dt * vib;
            if (v.phase >= 1.0f) v.phase -= 1.0f;

            // Classic jazz/funk 888000000 + percussion drawbar blend
            const float drawbars = 0.42f * std::sin(synth_detail::kTwoPi * 0.5f * v.phase)
                                 + 0.48f * std::sin(synth_detail::kTwoPi * 1.0f * v.phase)
                                 + 0.32f * std::sin(synth_detail::kTwoPi * 1.5f * v.phase)
                                 + 0.26f * std::sin(synth_detail::kTwoPi * 2.0f * v.phase)
                                 + 0.14f * std::sin(synth_detail::kTwoPi * 3.0f * v.phase)
                                 + 0.18f * std::sin(synth_detail::kTwoPi * 4.0f * v.phase) * v.tineEnv;
            const float click = rng_.next() * v.clickEnv * 0.14f;
            const float s = synth_detail::fastTanh((drawbars + click) * 0.85f) * v.ampEnv * (0.45f + 0.55f * v.vel) * 0.21f;
            const float rotPan = std::clamp(v.pan * 0.4f + 0.28f * lfoSin, -0.8f, 0.8f);
            synth_detail::addPanned(s, rotPan, sigL, sigR);
            v.tineEnv = synth_detail::flushDenormal(v.tineEnv * synth_detail::expDecay(0.14f, sampleRate_));
            v.clickEnv = synth_detail::flushDenormal(v.clickEnv * synth_detail::expDecay(0.008f, sampleRate_));
        } else if (patch_ == KeysPatch::Clavinet) {
            v.phase += dt;
            if (v.phase >= 1.0f) v.phase -= 1.0f;
            const float p = synth_detail::blepPulse(v.phase, dt, 0.18f);
            float lp = 0.0f, bp = 0.0f, hp = 0.0f;
            const float cut = 900.0f + 4200.0f * v.tineEnv * v.vel;
            v.filter.process(p, cut, 1.85f, invSr_, lp, bp, hp);
            const float s = (0.75f * lp + 0.35f * bp) * v.ampEnv * v.vel * 0.24f;
            synth_detail::addPanned(s, v.pan, sigL, sigR);
            v.tineEnv = synth_detail::flushDenormal(v.tineEnv * synth_detail::expDecay(0.16f, sampleRate_));
        } else {
            // WarmPad: 3 detuned saws into warm filter
            const float dt1 = dt * 0.9945f;
            const float dt2 = dt * 1.0055f;
            v.phase += dt;
            v.phase2 += dt1;
            v.phase3 += dt2;
            if (v.phase >= 1.0f) v.phase -= 1.0f;
            if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;
            if (v.phase3 >= 1.0f) v.phase3 -= 1.0f;

            const float sawC = synth_detail::blepSaw(v.phase, dt);
            const float sawL = synth_detail::blepSaw(v.phase2, dt1);
            const float sawR = synth_detail::blepSaw(v.phase3, dt2);

            float lp = 0.0f, bp = 0.0f, hp = 0.0f;
            const float cut = 480.0f + 1300.0f * v.vel * (0.4f + 0.6f * v.ampEnv);
            v.filter.process((sawC + sawL + sawR) * 0.33f, cut, 0.82f, invSr_, lp, bp, hp);

            const float g = v.ampEnv * v.vel * 0.20f;
            sigL = (lp * 0.72f + sawL * 0.10f) * g;
            sigR = (lp * 0.72f + sawR * 0.10f) * g;
        }

        sumL += sigL;
        sumR += sigR;
    }

    double sampleRate_ = 48000.0;
    float invSr_ = 1.0f / 48000.0f;
    KeysPatch patch_ = KeysPatch::RhodesMk1;
    bool sustainPedal_ = false;
    float tremPhase_ = 0.0f;
    uint32_t ageCounter_ = 0;
    std::array<Voice, 12> voices_ {};
    synth_detail::FastRng rng_ {};
};

// ============================================================================
// 3. GuitarSynthEngine (8-voice Karplus-Strong / waveguide)
// ============================================================================
class GuitarSynthEngine {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = (sampleRate > 8000.0 && sampleRate < 384000.0) ? sampleRate : 48000.0;
        invSr_ = 1.0f / static_cast<float>(sampleRate_);
        reset();
    }

    void reset() noexcept {
        for (auto& v : voices_) v.reset();
        ageCounter_ = 0;
        rng_.reset(0x617A4123u);
    }

    void setPatch(int patchIndex) noexcept {
        if (patchIndex < 0) patchIndex = 0;
        patch_ = static_cast<GuitarPatch>(patchIndex % 6);
    }

    void handleEvent(const BandNoteEvent& ev) noexcept {
        if (ev.type == EventType::NoteOff || (ev.type == EventType::NoteOn && ev.data2 == 0)) {
            for (auto& v : voices_) {
                if (v.active && v.note == ev.data1) {
                    v.gate = false;
                }
            }
            return;
        }
        if (ev.type != EventType::NoteOn) return;

        Voice* target = nullptr;
        for (auto& v : voices_) {
            if (v.active && v.note == ev.data1) {
                target = &v;
                break;
            }
        }
        if (!target) {
            for (auto& v : voices_) {
                if (!v.active) {
                    target = &v;
                    break;
                }
            }
        }
        if (!target) {
            target = &voices_[0];
            for (auto& v : voices_) {
                if (v.age < target->age) target = &v;
            }
        }

        const float freq = std::clamp(synth_detail::midiToFreq(static_cast<float>(ev.data1)), 55.0f, 2200.0f);
        const float period = std::clamp(static_cast<float>(sampleRate_) / freq, 8.0f, static_cast<float>(kMaxDelay - 4));

        target->active = true;
        target->gate = true;
        target->note = ev.data1;
        target->vel = std::clamp(static_cast<float>(ev.data2) / 127.0f, 0.05f, 1.0f);
        target->age = ++ageCounter_;
        target->delaySamples = period;
        target->writeIdx = 0;
        target->lpState = 0.0f;
        target->dcX = 0.0f;
        target->dcY = 0.0f;
        target->env = 1.0f;
        target->palmMute = (ev.articulation == Articulation::PalmMute || patch_ == GuitarPatch::FunkMute);
        target->pan = std::clamp((static_cast<float>(ev.data1) - 58.0f) * 0.025f, -0.45f, 0.45f);
        target->bodyFilter.reset();

        // Pick-position comb excitation into delay line
        const int intPeriod = std::clamp(static_cast<int>(period), 8, kMaxDelay - 2);
        const float pickPosRatio = (patch_ == GuitarPatch::NylonArp) ? 0.28f
                                 : (patch_ == GuitarPatch::FunkMute || patch_ == GuitarPatch::ReggaeSkank) ? 0.13f
                                 : 0.19f;
        const int combDelay = std::max(2, static_cast<int>(period * pickPosRatio));

        float prev = 0.0f;
        const float exciteBright = (patch_ == GuitarPatch::NylonArp) ? 0.30f
                                 : (patch_ == GuitarPatch::AcousticStrum) ? 0.58f
                                 : (patch_ == GuitarPatch::FunkMute) ? 0.72f : 0.62f;

        for (int i = 0; i < intPeriod; ++i) {
            const float n = rng_.next();
            prev = prev + exciteBright * (n - prev);
            target->delay[static_cast<size_t>(i)] = prev * target->vel;
        }
        // Apply pick-position comb notch for authentic plucked timbre
        for (int i = intPeriod - 1; i >= combDelay; --i) {
            target->delay[static_cast<size_t>(i)] -= 0.72f * target->delay[static_cast<size_t>(i - combDelay)];
        }
        // Clear rest of buffer lightly so wrap is clean
        for (int i = intPeriod; i < std::min(intPeriod + 8, kMaxDelay); ++i) {
            target->delay[static_cast<size_t>(i)] = 0.0f;
        }
        target->writeIdx = intPeriod % kMaxDelay;
    }

    void renderSample(float& outL, float& outR) noexcept {
        float sumL = 0.0f;
        float sumR = 0.0f;
        for (auto& v : voices_) {
            if (!v.active) continue;
            const float s = renderVoice(v);
            synth_detail::addPanned(s, v.pan, sumL, sumR);
        }

        if (patch_ == GuitarPatch::CrunchRock) {
            sumL = synth_detail::fastTanh(sumL * 2.4f) * 0.58f;
            sumR = synth_detail::fastTanh(sumR * 2.4f) * 0.58f;
        }

        outL = synth_detail::sanitize(sumL);
        outR = synth_detail::sanitize(sumR);
    }

private:
    static constexpr int kMaxDelay = 2048;

    struct Voice {
        bool active = false;
        bool gate = false;
        bool palmMute = false;
        uint8_t note = 52;
        uint32_t age = 0;
        float vel = 0.8f;
        float delaySamples = 200.0f;
        int writeIdx = 0;
        float lpState = 0.0f;
        float dcX = 0.0f;
        float dcY = 0.0f;
        float env = 0.0f;
        float pan = 0.0f;
        std::array<float, kMaxDelay> delay {};
        synth_detail::Svf2Pole bodyFilter {};

        void reset() noexcept {
            active = false;
            gate = false;
            palmMute = false;
            env = 0.0f;
            lpState = 0.0f;
            dcX = 0.0f;
            dcY = 0.0f;
            writeIdx = 0;
            delay.fill(0.0f);
            bodyFilter.reset();
        }
    };

    float renderVoice(Voice& v) noexcept {
        const float readPos = static_cast<float>(v.writeIdx) - v.delaySamples;
        float wrapped = readPos;
        while (wrapped < 0.0f) wrapped += static_cast<float>(kMaxDelay);

        const int i0 = static_cast<int>(wrapped) % kMaxDelay;
        const int i1 = (i0 + 1) % kMaxDelay;
        const float frac = wrapped - std::floor(wrapped);
        const float delayed = v.delay[static_cast<size_t>(i0)] * (1.0f - frac)
                            + v.delay[static_cast<size_t>(i1)] * frac;

        // Loop damping & feedback by patch
        float feedback = 0.9955f;
        float damp = 0.42f;
        if (v.palmMute) {
            feedback = 0.955f;
            damp = 0.72f;
        } else if (patch_ == GuitarPatch::ReggaeSkank) {
            feedback = 0.978f;
            damp = 0.48f;
        } else if (patch_ == GuitarPatch::AcousticStrum) {
            feedback = 0.9962f;
            damp = 0.36f;
        } else if (patch_ == GuitarPatch::NylonArp) {
            feedback = 0.9945f;
            damp = 0.58f;
        } else if (patch_ == GuitarPatch::CrunchRock) {
            feedback = 0.9972f;
            damp = 0.32f;
        }

        if (!v.gate) {
            feedback *= (patch_ == GuitarPatch::FunkMute || patch_ == GuitarPatch::ReggaeSkank) ? 0.94f : 0.985f;
        }

        // One-pole string damping filter
        v.lpState = synth_detail::flushDenormal(v.lpState + (1.0f - damp) * (delayed - v.lpState));
        const float fbSample = synth_detail::flushDenormal(v.lpState * feedback);
        v.delay[static_cast<size_t>(v.writeIdx)] = fbSample;
        v.writeIdx = (v.writeIdx + 1) % kMaxDelay;

        // DC blocker
        const float dcOut = delayed - v.dcX + 0.995f * v.dcY;
        v.dcX = delayed;
        v.dcY = synth_detail::flushDenormal(dcOut);

        // Acoustic body / amp tone shaping
        float lp = 0.0f, bp = 0.0f, hp = 0.0f;
        float shaped = dcOut;
        if (patch_ == GuitarPatch::AcousticStrum) {
            v.bodyFilter.process(dcOut, 260.0f, 1.35f, invSr_, lp, bp, hp);
            shaped = dcOut * 0.78f + bp * 0.55f;
        } else if (patch_ == GuitarPatch::ReggaeSkank || patch_ == GuitarPatch::FunkMute) {
            v.bodyFilter.process(dcOut, 1650.0f, 1.05f, invSr_, lp, bp, hp);
            shaped = dcOut * 0.45f + bp * 0.75f;
        } else if (patch_ == GuitarPatch::CrunchRock) {
            v.bodyFilter.process(dcOut, 2900.0f, 0.90f, invSr_, lp, bp, hp);
            shaped = lp * 1.15f;
        } else if (patch_ == GuitarPatch::NylonArp) {
            v.bodyFilter.process(dcOut, 1800.0f, 0.75f, invSr_, lp, bp, hp);
            shaped = lp;
        }

        // Energy envelope tracker to auto-retire silent strings
        const float envDecay = v.gate
            ? synth_detail::expDecay(v.palmMute ? 0.16f : 3.2f, sampleRate_)
            : synth_detail::expDecay(0.18f, sampleRate_);
        v.env = synth_detail::flushDenormal(v.env * envDecay);
        if (v.env < 0.0004f) {
            v.active = false;
            return 0.0f;
        }

        return shaped * v.env * 0.42f;
    }

    double sampleRate_ = 48000.0;
    float invSr_ = 1.0f / 48000.0f;
    GuitarPatch patch_ = GuitarPatch::CleanElectric;
    uint32_t ageCounter_ = 0;
    std::array<Voice, 8> voices_ {};
    synth_detail::FastRng rng_ {};
};

// ============================================================================
// 4. PercSynthEngine
// ============================================================================
class PercSynthEngine {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = (sampleRate > 8000.0 && sampleRate < 384000.0) ? sampleRate : 48000.0;
        invSr_ = 1.0f / static_cast<float>(sampleRate_);
        reset();
    }

    void reset() noexcept {
        for (auto& v : voices_) v = {};
        rng_.reset(0x9E2C4567u);
        ageCounter_ = 0;
    }

    void setPatch(int patchIndex) noexcept {
        if (patchIndex < 0) patchIndex = 0;
        patch_ = static_cast<PercPatch>(patchIndex % 4);
    }

    void handleEvent(const BandNoteEvent& ev) noexcept {
        if (ev.type != EventType::NoteOn || ev.data2 == 0) return;

        Voice* target = &voices_[0];
        for (auto& v : voices_) {
            if (!v.active) {
                target = &v;
                break;
            }
            if (v.age < target->age) target = &v;
        }

        target->active = true;
        target->age = ++ageCounter_;
        target->vel = std::clamp(static_cast<float>(ev.data2) / 127.0f, 0.05f, 1.0f);
        target->env = 1.0f;
        target->pitchEnv = 1.0f;
        target->atk = 0.0f;
        target->p1 = 0.0f;
        target->p2 = 0.0f;
        target->filter.reset();

        const float pitchMult = (patch_ == PercPatch::ElectronicMicro) ? 1.18f
                              : (patch_ == PercPatch::AfrobeatPerc) ? 0.94f : 1.0f;

        const int note = static_cast<int>(ev.data1);
        switch (note) {
            case PercNote::Shaker:
                target->kind = Kind::Shaker;
                target->freq1 = 6800.0f * pitchMult;
                target->q = 1.1f;
                target->decaySec = 0.085f;
                target->pan = -0.35f;
                break;
            case PercNote::Cabasa:
                target->kind = Kind::Shaker;
                target->freq1 = 5400.0f * pitchMult;
                target->q = 1.4f;
                target->decaySec = 0.11f;
                target->pan = +0.32f;
                break;
            case PercNote::Tambourine:
                target->kind = Kind::Tambourine;
                target->freq1 = 7600.0f * pitchMult;
                target->q = 0.95f;
                target->decaySec = 0.19f;
                target->pan = +0.38f;
                break;
            case PercNote::HighBongo:
                target->kind = Kind::Membrane;
                target->freq1 = 365.0f * pitchMult;
                target->pitchDrop = 140.0f;
                target->decaySec = 0.16f;
                target->pan = -0.26f;
                break;
            case PercNote::LowBongo:
                target->kind = Kind::Membrane;
                target->freq1 = 275.0f * pitchMult;
                target->pitchDrop = 105.0f;
                target->decaySec = 0.20f;
                target->pan = -0.10f;
                break;
            case PercNote::MuteConga:
                target->kind = Kind::Membrane;
                target->freq1 = 245.0f * pitchMult;
                target->pitchDrop = 90.0f;
                target->decaySec = 0.075f;
                target->pan = +0.12f;
                break;
            case PercNote::OpenConga:
                target->kind = Kind::Membrane;
                target->freq1 = 215.0f * pitchMult;
                target->pitchDrop = 85.0f;
                target->decaySec = 0.24f;
                target->pan = +0.24f;
                break;
            case PercNote::LowConga:
                target->kind = Kind::Membrane;
                target->freq1 = 165.0f * pitchMult;
                target->pitchDrop = 65.0f;
                target->decaySec = 0.29f;
                target->pan = +0.36f;
                break;
            case PercNote::Cowbell:
                target->kind = Kind::Metallic;
                target->freq1 = 562.0f * pitchMult;
                target->freq2 = 845.0f * pitchMult;
                target->decaySec = 0.24f;
                target->pan = +0.28f;
                break;
            case PercNote::Clave:
                target->kind = Kind::Wood;
                target->freq1 = 2480.0f * pitchMult;
                target->freq2 = 3820.0f * pitchMult;
                target->decaySec = 0.042f;
                target->pan = -0.22f;
                break;
            case PercNote::Woodblock:
            default:
                target->kind = Kind::Wood;
                target->freq1 = 1120.0f * pitchMult;
                target->freq2 = 2290.0f * pitchMult;
                target->decaySec = 0.065f;
                target->pan = -0.18f;
                break;
        }
    }

    void renderSample(float& outL, float& outR) noexcept {
        float l = 0.0f;
        float r = 0.0f;
        for (auto& v : voices_) {
            if (!v.active) continue;
            const float s = renderVoice(v);
            synth_detail::addPanned(s, v.pan, l, r);
        }
        outL = synth_detail::sanitize(l);
        outR = synth_detail::sanitize(r);
    }

private:
    enum class Kind : uint8_t { Shaker, Tambourine, Membrane, Metallic, Wood };

    struct Voice {
        bool active = false;
        Kind kind = Kind::Shaker;
        uint32_t age = 0;
        float vel = 0.8f;
        float env = 0.0f;
        float pitchEnv = 0.0f;
        float atk = 0.0f;
        float p1 = 0.0f;
        float p2 = 0.0f;
        float freq1 = 400.0f;
        float freq2 = 800.0f;
        float pitchDrop = 0.0f;
        float q = 1.0f;
        float decaySec = 0.1f;
        float pan = 0.0f;
        synth_detail::Svf2Pole filter {};
    };

    float renderVoice(Voice& v) noexcept {
        float out = 0.0f;

        if (v.kind == Kind::Shaker) {
            v.atk = std::min(1.0f, v.atk + invSr_ * 140.0f);
            float lp = 0.0f, bp = 0.0f, hp = 0.0f;
            v.filter.process(rng_.next(), v.freq1, v.q, invSr_, lp, bp, hp);
            out = (bp * 0.72f + hp * 0.28f) * v.atk * v.env * v.vel * 0.42f;
        } else if (v.kind == Kind::Tambourine) {
            v.p1 += 4410.0f * invSr_;
            v.p2 += 6390.0f * invSr_;
            if (v.p1 >= 1.0f) v.p1 -= 1.0f;
            if (v.p2 >= 1.0f) v.p2 -= 1.0f;
            const float src = 0.45f * (std::sin(synth_detail::kTwoPi * v.p1) + std::sin(synth_detail::kTwoPi * v.p2))
                            + 0.55f * rng_.next();
            float lp = 0.0f, bp = 0.0f, hp = 0.0f;
            v.filter.process(src, v.freq1, v.q, invSr_, lp, bp, hp);
            out = hp * v.env * v.vel * 0.36f;
        } else if (v.kind == Kind::Membrane) {
            const float f = v.freq1 + v.pitchDrop * v.pitchEnv * v.pitchEnv;
            v.p1 += f * invSr_;
            v.p2 += (f * 1.56f) * invSr_;
            if (v.p1 >= 1.0f) v.p1 -= 1.0f;
            if (v.p2 >= 1.0f) v.p2 -= 1.0f;
            const float slap = rng_.next() * v.pitchEnv * 0.16f;
            const float body = 0.78f * std::sin(synth_detail::kTwoPi * v.p1)
                             + 0.22f * std::sin(synth_detail::kTwoPi * v.p2) * v.pitchEnv;
            out = synth_detail::fastTanh((body + slap) * 1.15f) * v.env * v.vel * 0.52f;
            v.pitchEnv = synth_detail::flushDenormal(v.pitchEnv * synth_detail::expDecay(0.032f, sampleRate_));
        } else if (v.kind == Kind::Metallic) {
            v.p1 += v.freq1 * invSr_;
            v.p2 += v.freq2 * invSr_;
            if (v.p1 >= 1.0f) v.p1 -= 1.0f;
            if (v.p2 >= 1.0f) v.p2 -= 1.0f;
            const float sq = ((v.p1 < 0.5f) ? 0.5f : -0.5f) + ((v.p2 < 0.5f) ? 0.5f : -0.5f);
            float lp = 0.0f, bp = 0.0f, hp = 0.0f;
            v.filter.process(sq, 960.0f, 2.0f, invSr_, lp, bp, hp);
            out = bp * v.env * v.vel * 0.48f;
        } else {
            // Wood (clave / woodblock)
            v.p1 += v.freq1 * invSr_;
            v.p2 += v.freq2 * invSr_;
            if (v.p1 >= 1.0f) v.p1 -= 1.0f;
            if (v.p2 >= 1.0f) v.p2 -= 1.0f;
            out = (0.72f * std::sin(synth_detail::kTwoPi * v.p1)
                 + 0.28f * std::sin(synth_detail::kTwoPi * v.p2)) * v.env * v.vel * 0.48f;
        }

        v.env = synth_detail::flushDenormal(v.env * synth_detail::expDecay(v.decaySec, sampleRate_));
        if (v.env < 0.0005f) v.active = false;
        return out;
    }

    double sampleRate_ = 48000.0;
    float invSr_ = 1.0f / 48000.0f;
    PercPatch patch_ = PercPatch::StudioShakerTamb;
    uint32_t ageCounter_ = 0;
    std::array<Voice, 8> voices_ {};
    synth_detail::FastRng rng_ {};
};

// ============================================================================
// 5. HornsStringsEngine (8-voice polyphonic)
// ============================================================================
class HornsStringsEngine {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = (sampleRate > 8000.0 && sampleRate < 384000.0) ? sampleRate : 48000.0;
        invSr_ = 1.0f / static_cast<float>(sampleRate_);
        reset();
    }

    void reset() noexcept {
        for (auto& v : voices_) v = {};
        sustainPedal_ = false;
        vibPhase_ = 0.0f;
        ageCounter_ = 0;
    }

    void setPatch(int patchIndex) noexcept {
        if (patchIndex < 0) patchIndex = 0;
        patch_ = static_cast<HornsPatch>(patchIndex % 4);
    }

    void handleEvent(const BandNoteEvent& ev) noexcept {
        if (ev.type == EventType::ControlChange) {
            if (ev.data1 == 64) {
                const bool down = ev.data2 >= 64;
                if (sustainPedal_ && !down) {
                    for (auto& v : voices_) {
                        if (v.active && v.sustained) {
                            v.sustained = false;
                            v.gate = false;
                        }
                    }
                }
                sustainPedal_ = down;
            }
            return;
        }

        if (ev.type == EventType::NoteOff || (ev.type == EventType::NoteOn && ev.data2 == 0)) {
            for (auto& v : voices_) {
                if (v.active && v.note == ev.data1 && v.gate) {
                    if (sustainPedal_) v.sustained = true;
                    else v.gate = false;
                }
            }
            return;
        }

        if (ev.type != EventType::NoteOn) return;

        Voice* target = nullptr;
        for (auto& v : voices_) {
            if (v.active && v.note == ev.data1) {
                target = &v;
                break;
            }
        }
        if (!target) {
            for (auto& v : voices_) {
                if (!v.active) {
                    target = &v;
                    break;
                }
            }
        }
        if (!target) {
            target = &voices_[0];
            for (auto& v : voices_) {
                if (v.age < target->age) target = &v;
            }
        }

        target->active = true;
        target->gate = true;
        target->sustained = false;
        target->attackStage = true;
        target->note = ev.data1;
        target->vel = std::clamp(static_cast<float>(ev.data2) / 127.0f, 0.05f, 1.0f);
        target->age = ++ageCounter_;
        target->p1 = 0.0f;
        target->p2 = 0.17f;
        target->p3 = 0.61f;
        target->ampEnv = 0.0f;
        target->fltEnv = 0.0f;
        target->bellEnv = 1.0f;
        target->isSwell = (ev.articulation == Articulation::Swell);
        target->pan = std::clamp((static_cast<float>(ev.data1) - 62.0f) * 0.032f, -0.55f, 0.55f);
        target->filterL.reset();
        target->filterR.reset();
    }

    void renderSample(float& outL, float& outR) noexcept {
        vibPhase_ += 5.2f * invSr_;
        if (vibPhase_ >= 1.0f) vibPhase_ -= 1.0f;
        const float vib = std::sin(synth_detail::kTwoPi * vibPhase_);

        float sumL = 0.0f;
        float sumR = 0.0f;
        for (auto& v : voices_) {
            if (!v.active) continue;
            renderVoice(v, vib, sumL, sumR);
        }

        outL = synth_detail::sanitize(synth_detail::fastTanh(sumL * 0.85f));
        outR = synth_detail::sanitize(synth_detail::fastTanh(sumR * 0.85f));
    }

private:
    struct Voice {
        bool active = false;
        bool gate = false;
        bool sustained = false;
        bool attackStage = false;
        bool isSwell = false;
        uint8_t note = 60;
        uint32_t age = 0;
        float vel = 0.8f;
        float p1 = 0.0f;
        float p2 = 0.0f;
        float p3 = 0.0f;
        float ampEnv = 0.0f;
        float fltEnv = 0.0f;
        float bellEnv = 0.0f;
        float pan = 0.0f;
        synth_detail::Svf2Pole filterL {};
        synth_detail::Svf2Pole filterR {};
    };

    void renderVoice(Voice& v, float vib, float& sumL, float& sumR) noexcept {
        const float baseFreq = synth_detail::midiToFreq(static_cast<float>(v.note));
        const float vibAmt = (patch_ == HornsPatch::WarmStrings) ? 0.0042f
                           : (patch_ == HornsPatch::RhodesFills) ? 0.0030f : 0.0018f;
        const float dt = std::clamp((baseFreq * (1.0f + vibAmt * vib)) * invSr_, 0.0001f, 0.44f);

        float atkRate = 28.0f;
        float relSec = 0.24f;
        if (patch_ == HornsPatch::WarmStrings || v.isSwell) {
            atkRate = 3.2f;
            relSec = 0.72f;
        } else if (patch_ == HornsPatch::RhodesFills) {
            atkRate = 340.0f;
            relSec = 0.32f;
        } else if (patch_ == HornsPatch::SynthLeadBrass) {
            atkRate = 55.0f;
            relSec = 0.22f;
        }

        if (v.attackStage) {
            v.ampEnv += invSr_ * atkRate;
            if (v.ampEnv >= 1.0f) {
                v.ampEnv = 1.0f;
                v.attackStage = false;
            }
        } else if (v.gate) {
            const float holdSec = (patch_ == HornsPatch::RhodesFills) ? 2.2f : 12.0f;
            v.ampEnv = synth_detail::flushDenormal(v.ampEnv * synth_detail::expDecay(holdSec, sampleRate_));
        } else {
            v.ampEnv = synth_detail::flushDenormal(v.ampEnv * synth_detail::expDecay(relSec, sampleRate_));
        }

        if (!v.attackStage && v.ampEnv < 0.0003f) {
            v.active = false;
            return;
        }

        if (patch_ == HornsPatch::RhodesFills) {
            v.p1 += dt;
            if (v.p1 >= 1.0f) v.p1 -= 1.0f;
            const float fm = std::sin(synth_detail::kTwoPi * v.p1 * 5.0f) * (0.4f + 1.1f * v.vel) * v.bellEnv;
            const float sig = std::sin(synth_detail::kTwoPi * v.p1 + 0.28f * fm) + 0.18f * fm;
            const float g = sig * v.ampEnv * v.vel * 0.24f;
            synth_detail::addPanned(g, v.pan, sumL, sumR);
            v.bellEnv = synth_detail::flushDenormal(v.bellEnv * synth_detail::expDecay(0.55f, sampleRate_));
            return;
        }

        // Detuned 3-oscillator stereo section for BrassStabs, WarmStrings, SynthLeadBrass
        const float detune = (patch_ == HornsPatch::WarmStrings) ? 0.0065f : 0.0042f;
        const float dtL = dt * (1.0f - detune);
        const float dtR = dt * (1.0f + detune);
        v.p1 += dt;
        v.p2 += dtL;
        v.p3 += dtR;
        if (v.p1 >= 1.0f) v.p1 -= 1.0f;
        if (v.p2 >= 1.0f) v.p2 -= 1.0f;
        if (v.p3 >= 1.0f) v.p3 -= 1.0f;

        const float sC = synth_detail::blepSaw(v.p1, dt);
        const float sL = synth_detail::blepSaw(v.p2, dtL);
        const float sR = synth_detail::blepSaw(v.p3, dtR);

        const float rawL = 0.55f * sL + 0.45f * sC;
        const float rawR = 0.55f * sR + 0.45f * sC;

        // Brass filter brightness envelope rises slightly on attack then blooms
        const float fltTarget = v.gate ? 1.0f : 0.0f;
        const float fltRate = (patch_ == HornsPatch::BrassStabs) ? 24.0f
                            : (patch_ == HornsPatch::SynthLeadBrass) ? 38.0f : 2.8f;
        v.fltEnv += (fltTarget - v.fltEnv) * std::min(1.0f, invSr_ * fltRate);
        v.fltEnv = synth_detail::flushDenormal(v.fltEnv);

        float baseCut = 420.0f;
        float envCut = 2600.0f;
        float q = 1.25f;
        if (patch_ == HornsPatch::WarmStrings) {
            baseCut = 550.0f;
            envCut = 1750.0f;
            q = 0.78f;
        } else if (patch_ == HornsPatch::SynthLeadBrass) {
            baseCut = 480.0f;
            envCut = 3400.0f;
            q = 2.1f;
        }

        const float cut = baseCut + envCut * v.fltEnv * (0.35f + 0.65f * v.vel);
        float lpL = 0.0f, bpL = 0.0f, hpL = 0.0f;
        float lpR = 0.0f, bpR = 0.0f, hpR = 0.0f;
        v.filterL.process(rawL, cut, q, invSr_, lpL, bpL, hpL);
        v.filterR.process(rawR, cut * 1.03f, q, invSr_, lpR, bpR, hpR);

        const float g = v.ampEnv * v.vel * 0.21f;
        const float panAngle = (v.pan + 1.0f) * 0.25f * synth_detail::kPi;
        const float pL = std::cos(panAngle);
        const float pR = std::sin(panAngle);
        sumL += lpL * g * (0.65f + 0.35f * pL);
        sumR += lpR * g * (0.65f + 0.35f * pR);
    }

    double sampleRate_ = 48000.0;
    float invSr_ = 1.0f / 48000.0f;
    HornsPatch patch_ = HornsPatch::BrassStabs;
    bool sustainPedal_ = false;
    float vibPhase_ = 0.0f;
    uint32_t ageCounter_ = 0;
    std::array<Voice, 8> voices_ {};
};

} // namespace sessionmate
