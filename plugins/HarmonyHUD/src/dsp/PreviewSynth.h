#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace harmonyhud {

// Zero-allocation Rhodes/warm-FM-sine chord preview synthesizer.
// Completely silent (zero added samples, preserving bit-transparent pass-through)
// unless the user actively triggers a substitution or keyboard/chord audition!
class PreviewSynth {
public:
    static constexpr int kMaxVoices = 8;
    static constexpr int kMaxChordNotes = 8;

    struct TriggerCommand {
        std::array<int, kMaxChordNotes> notes{};
        int count = 0;
        float durationSec = 1.4f;
        float gain = 0.22f;
    };

    void prepare(double sampleRate) {
        sampleRate_ = std::max(8000.0, sampleRate);
        reset();
    }

    void reset() {
        for (auto& v : voices_)
            v = Voice{};
        pendingRead_.store(0, std::memory_order_relaxed);
        pendingWrite_.store(0, std::memory_order_relaxed);
        active_.store(false, std::memory_order_relaxed);
    }

    // Called from UI thread (lock-free SPSC push)
    void triggerChord(const std::vector<int>& midiNotes, float durationSec = 1.4f, float gain = 0.22f) {
        if (midiNotes.empty())
            return;
        const uint32_t w = pendingWrite_.load(std::memory_order_relaxed);
        const uint32_t nextW = (w + 1u) & (kQueueSize - 1u);
        if (nextW == pendingRead_.load(std::memory_order_acquire))
            return; // full, drop safely

        TriggerCommand cmd{};
        cmd.count = std::min<int>(static_cast<int>(midiNotes.size()), kMaxChordNotes);
        for (int i = 0; i < cmd.count; ++i)
            cmd.notes[static_cast<size_t>(i)] = std::clamp(midiNotes[static_cast<size_t>(i)], 24, 108);
        cmd.durationSec = std::clamp(durationSec, 0.15f, 4.0f);
        cmd.gain = std::clamp(gain, 0.02f, 0.5f);

        queue_[w] = cmd;
        pendingWrite_.store(nextW, std::memory_order_release);
    }

    void setReferenceA4(float a4Hz) {
        referenceA4_.store(std::clamp(a4Hz, 415.0f, 466.0f), std::memory_order_relaxed);
    }

    bool isAuditioning() const {
        return active_.load(std::memory_order_relaxed);
    }

    // Audio thread ONLY: returns true if any sample was mixed into left/right.
    // When idle (no queued triggers & no active voices), touches ZERO samples
    // so main stereo audio remains 100% bit-transparent!
    bool processAdd(float* left, float* right, int numSamples) {
        drainQueue();

        bool anyActive = false;
        for (const auto& v : voices_) {
            if (v.active) {
                anyActive = true;
                break;
            }
        }
        active_.store(anyActive, std::memory_order_relaxed);
        if (!anyActive || numSamples <= 0)
            return false;

        constexpr double kTwoPi = 6.28318530717958647692;
        const float sr = static_cast<float>(sampleRate_);
        const float attackInc = 1.0f / std::max(1.0f, sr * 0.012f);
        const float releaseDec = 1.0f / std::max(1.0f, sr * 0.35f);
        const float bellDecay = std::exp(-1.0f / std::max(1.0f, sr * 0.18f));
        const float bodyDecay = std::exp(-1.0f / std::max(1.0f, sr * 1.15f));

        for (int i = 0; i < numSamples; ++i) {
            float mixL = 0.0f;
            float mixR = 0.0f;

            for (auto& v : voices_) {
                if (!v.active) continue;

                // Envelope
                if (v.samplesRemaining > 0) {
                    --v.samplesRemaining;
                    v.env = std::min(1.0f, v.env + attackInc);
                } else {
                    v.env -= releaseDec;
                    if (v.env <= 0.0001f) {
                        v.active = false;
                        v.env = 0.0f;
                        continue;
                    }
                }

                // Warm electric-piano (Rhodes-like) FM tine + fundamental sine
                double tine = std::sin(v.phase * 4.0) * v.bellEnv * 0.45;
                double body = std::sin(v.phase + tine * 0.65) * 0.85
                            + std::sin(v.phase * 2.0) * 0.15 * v.bodyEnv;
                float sample = static_cast<float>(body) * v.env * v.gain * v.bodyEnv;

                mixL += sample * v.panL;
                mixR += sample * v.panR;

                v.phase += v.phaseInc;
                if (v.phase >= kTwoPi) v.phase -= kTwoPi;
                v.bellEnv *= bellDecay;
                v.bodyEnv = 0.35f + 0.65f * (v.bodyEnv - 0.35f) * bodyDecay;
            }

            // Gentle soft saturation to prevent clipping
            left[i]  += std::tanh(mixL);
            if (right != nullptr)
                right[i] += std::tanh(mixR);
        }

        // Re-check active state after block
        anyActive = false;
        for (const auto& v : voices_) {
            if (v.active) { anyActive = true; break; }
        }
        active_.store(anyActive, std::memory_order_relaxed);
        return true;
    }

private:
    struct Voice {
        bool active = false;
        double phase = 0.0;
        double phaseInc = 0.0;
        float env = 0.0f;
        float bellEnv = 1.0f;
        float bodyEnv = 1.0f;
        float gain = 0.0f;
        float panL = 0.707f;
        float panR = 0.707f;
        int samplesRemaining = 0;
    };

    void drainQueue() {
        uint32_t r = pendingRead_.load(std::memory_order_relaxed);
        const uint32_t w = pendingWrite_.load(std::memory_order_acquire);
        if (r == w) return;

        // Only apply latest queued chord trigger so rapid clicks stay clean
        TriggerCommand latest{};
        while (r != w) {
            latest = queue_[r];
            r = (r + 1u) & (kQueueSize - 1u);
        }
        pendingRead_.store(r, std::memory_order_release);

        if (latest.count <= 0) return;

        // Release existing voices softly by fading them out quickly, then allocate new voices
        const float a4 = referenceA4_.load(std::memory_order_relaxed);
        constexpr double kTwoPi = 6.28318530717958647692;
        const float perVoiceGain = latest.gain / std::sqrt(static_cast<float>(latest.count));
        const int holdSamples = static_cast<int>(latest.durationSec * static_cast<float>(sampleRate_));

        for (int i = 0; i < kMaxVoices; ++i) {
            if (i < latest.count) {
                int midi = latest.notes[static_cast<size_t>(i)];
                double freq = static_cast<double>(a4) * std::pow(2.0, (midi - 69) / 12.0);
                // Slight stereo spread across chord voices (low left-center -> high right-center)
                float normPos = (latest.count > 1)
                    ? (static_cast<float>(i) / static_cast<float>(latest.count - 1) * 2.0f - 1.0f)
                    : 0.0f;
                float pan = normPos * 0.28f; // gentle +/- 28% stereo width
                float angle = (pan + 1.0f) * 0.78539816339f; // 0..pi/2

                auto& v = voices_[static_cast<size_t>(i)];
                v.active = true;
                v.phase = 0.0;
                v.phaseInc = kTwoPi * freq / sampleRate_;
                v.env = 0.0f;
                v.bellEnv = 1.0f;
                v.bodyEnv = 1.0f;
                v.gain = perVoiceGain;
                v.panL = std::cos(angle);
                v.panR = std::sin(angle);
                v.samplesRemaining = holdSamples;
            } else {
                voices_[static_cast<size_t>(i)].samplesRemaining = 0;
            }
        }
    }

    static constexpr uint32_t kQueueSize = 16;
    std::array<TriggerCommand, kQueueSize> queue_{};
    std::atomic<uint32_t> pendingRead_{0};
    std::atomic<uint32_t> pendingWrite_{0};
    std::atomic<float> referenceA4_{440.0f};
    std::atomic<bool> active_{false};
    std::array<Voice, kMaxVoices> voices_{};
    double sampleRate_ = 48000.0;
};

} // namespace harmonyhud
