#pragma once

#include "../SessionMateTypes.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>

namespace sessionmate {

class SyncTransport {
public:
    struct HostPlayHeadInfo {
        bool valid = false;
        bool isPlaying = false;
        double bpm = 120.0;
        double ppqPosition = 0.0;
        TimeSignature timeSig { 4, 4 };
    };

    SyncTransport() noexcept {
        prepare(44100.0);
    }

    void prepare(double sampleRate) noexcept {
        sampleRate_ = (std::isfinite(sampleRate) && sampleRate >= 8000.0) ? sampleRate : 44100.0;
        reset();
    }

    void reset() noexcept {
        internalPpq_ = 0.0;
        liveBpm_ = getManualBpm();
        lastGoodLiveBpm_ = liveBpm_;
        liveAutoPlaying_ = false;
        liveSilenceSeconds_ = 0.0;
        wasPlaying_ = false;
        tapIntervalCount_ = 0;
        tapIntervalWritePos_ = 0;
        lastTapTime_ = -1.0;
        tapIntervals_.fill(0.0);
    }

    void setStandalonePlaying(bool playing) noexcept {
        standalonePlaying_.store(playing, std::memory_order_release);
    }

    bool isStandalonePlaying() const noexcept {
        return standalonePlaying_.load(std::memory_order_acquire);
    }

    void setManualBpm(double bpm) noexcept {
        if (!std::isfinite(bpm)) return;
        const double clamped = std::clamp(bpm, kMinManualBpm, kMaxManualBpm);
        manualBpm_.store(clamped, std::memory_order_release);
    }

    double getManualBpm() const noexcept {
        return manualBpm_.load(std::memory_order_acquire);
    }

    // Averages the last 2..4 tap intervals within [0.25s, 2.0s] and updates manual BPM smoothly in [45, 220].
    void triggerTapTempo(double nowSeconds) noexcept {
        if (!std::isfinite(nowSeconds) || nowSeconds < 0.0) return;

        if (lastTapTime_ < 0.0) {
            lastTapTime_ = nowSeconds;
            tapIntervalCount_ = 0;
            tapIntervalWritePos_ = 0;
            return;
        }

        const double dt = nowSeconds - lastTapTime_;
        lastTapTime_ = nowSeconds;

        if (dt < kMinTapIntervalSec || dt > kMaxTapIntervalSec) {
            // Too fast (bounce) or too slow (new tap phrase): restart sequence
            tapIntervalCount_ = 0;
            tapIntervalWritePos_ = 0;
            return;
        }

        tapIntervals_[static_cast<size_t>(tapIntervalWritePos_)] = dt;
        tapIntervalWritePos_ = (tapIntervalWritePos_ + 1) % kMaxTapIntervals;
        if (tapIntervalCount_ < kMaxTapIntervals) {
            ++tapIntervalCount_;
        }

        // Compute mean interval across stored taps (smoothly averages 2..4 intervals; also responds on 1st valid interval)
        double sum = 0.0;
        for (int i = 0; i < tapIntervalCount_; ++i) {
            sum += tapIntervals_[static_cast<size_t>(i)];
        }
        const double avgDt = sum / static_cast<double>(tapIntervalCount_);
        if (avgDt > 1.0e-6) {
            const double tappedBpm = std::clamp(60.0 / avgDt, kMinManualBpm, kMaxManualBpm);
            if (tapIntervalCount_ == 1) {
                // First interval: blend moderately with current BPM to avoid single-tap jitter
                const double cur = getManualBpm();
                setManualBpm(0.75 * tappedBpm + 0.25 * cur);
            } else {
                // 2..4 averaged intervals: already smooth and accurate
                setManualBpm(tappedBpm);
            }
        }
    }

    TransportState advanceBlock(int numSamples,
                                SyncMode mode,
                                TimeSignature userTimeSig,
                                const HostPlayHeadInfo& hostInfo,
                                const ListenerTelemetry& listener) noexcept {
        const int safeSamples = std::max(0, numSamples);
        const double sr = (std::isfinite(sampleRate_) && sampleRate_ >= 8000.0) ? sampleRate_ : 44100.0;
        const double blockSec = static_cast<double>(safeSamples) / sr;
        const TimeSignature safeUserTs = sanitizeTimeSig(userTimeSig);
        const double manualBpm = getManualBpm();
        const bool standalonePlay = isStandalonePlaying();

        TransportState state {};
        state.syncMode = mode;

        switch (mode) {
            case SyncMode::DawHost: {
                if (hostInfo.valid) {
                    const double hostBpm = (std::isfinite(hostInfo.bpm) && hostInfo.bpm > 10.0)
                        ? std::clamp(hostInfo.bpm, 30.0, 300.0)
                        : manualBpm;
                    const TimeSignature hostTs = sanitizeTimeSig(hostInfo.timeSig);

                    state.bpm = hostBpm;
                    state.timeSig = hostTs;

                    if (hostInfo.isPlaying) {
                        state.isPlaying = true;
                        const double hostPpq = (std::isfinite(hostInfo.ppqPosition) && hostInfo.ppqPosition >= 0.0)
                            ? hostInfo.ppqPosition
                            : internalPpq_;
                        state.ppqPosition = hostPpq;
                        // Keep internalPpq_ aligned so stopping DAW and auditioning stays seamless
                        internalPpq_ = hostPpq + (hostBpm / 60.0) * blockSec;
                    } else {
                        // Host is stopped: respect standalonePlaying so user can audition/jam in DAW
                        state.isPlaying = standalonePlay;
                        state.ppqPosition = internalPpq_;
                        if (standalonePlay) {
                            internalPpq_ += (hostBpm / 60.0) * blockSec;
                        }
                    }
                } else {
                    // No host playhead (standalone host): fall back to manual transport
                    state.bpm = manualBpm;
                    state.timeSig = safeUserTs;
                    state.isPlaying = standalonePlay;
                    state.ppqPosition = internalPpq_;
                    if (standalonePlay) {
                        internalPpq_ += (manualBpm / 60.0) * blockSec;
                    }
                }
                break;
            }

            case SyncMode::LiveFollow: {
                state.timeSig = safeUserTs;

                // Update target tempo when listener confidence > 0.30, else hold last good tempo
                if (listener.tempoConfidence > 0.30f && std::isfinite(listener.detectedBpm)) {
                    lastGoodLiveBpm_ = std::clamp(listener.detectedBpm, kMinManualBpm, kMaxManualBpm);
                }

                // Organic slew-rate-limited tempo glide (max 12 BPM/sec + smooth 0.35s EMA)
                const double diff = lastGoodLiveBpm_ - liveBpm_;
                const double maxStep = 12.0 * blockSec;
                const double alpha = 1.0 - std::exp(-blockSec / 0.35);
                const double desiredStep = diff * alpha;
                const double clampedStep = std::clamp(desiredStep, -maxStep, maxStep);
                liveBpm_ = std::clamp(liveBpm_ + clampedStep, kMinManualBpm, kMaxManualBpm);

                // Auto-start when standalonePlaying is true OR live audio energy exceeds threshold
                const bool liveAudioActive = (listener.energyEstimate > 0.14f) ||
                                             (listener.userActivity > 0.22f) ||
                                             (listener.inputRms > 0.015f);
                if (liveAudioActive) {
                    liveAutoPlaying_ = true;
                    liveSilenceSeconds_ = 0.0;
                } else if (liveAutoPlaying_) {
                    liveSilenceSeconds_ += blockSec;
                    if (liveSilenceSeconds_ >= kLiveSilenceHoldSec) {
                        liveAutoPlaying_ = false;
                    }
                }

                state.bpm = liveBpm_;
                state.isPlaying = standalonePlay || liveAutoPlaying_;
                state.ppqPosition = internalPpq_;
                if (state.isPlaying) {
                    internalPpq_ += (liveBpm_ / 60.0) * blockSec;
                }
                break;
            }

            case SyncMode::StandaloneTap: {
                state.bpm = manualBpm;
                state.timeSig = safeUserTs;
                state.isPlaying = standalonePlay;
                state.ppqPosition = internalPpq_;
                if (standalonePlay) {
                    internalPpq_ += (manualBpm / 60.0) * blockSec;
                }
                break;
            }
        }

        // Compute bar / beat / step / barStartPpq accurately from ppqPosition & timeSig
        populateMusicalPosition(state);
        wasPlaying_ = state.isPlaying;
        return state;
    }

private:
    static constexpr double kMinManualBpm = 45.0;
    static constexpr double kMaxManualBpm = 220.0;
    static constexpr double kMinTapIntervalSec = 0.25;
    static constexpr double kMaxTapIntervalSec = 2.00;
    static constexpr int kMaxTapIntervals = 4;
    static constexpr double kLiveSilenceHoldSec = 3.0;

    static TimeSignature sanitizeTimeSig(TimeSignature ts) noexcept {
        TimeSignature out = ts;
        if (out.numerator <= 0 || out.numerator > 32) out.numerator = 4;
        if (out.denominator != 2 && out.denominator != 4 && out.denominator != 8 && out.denominator != 16) {
            out.denominator = 4;
        }
        return out;
    }

    static void populateMusicalPosition(TransportState& state) noexcept {
        const double ppq = (std::isfinite(state.ppqPosition) && state.ppqPosition >= 0.0)
            ? state.ppqPosition
            : 0.0;
        state.ppqPosition = ppq;

        const double beatsPerBar = std::max(0.25, state.timeSig.beatsPerBarQuarter());
        const int stepsPerBar = state.timeSig.stepsPerBar();

        const double barFloat = std::floor(ppq / beatsPerBar + 1.0e-9);
        const int barIdx = std::max(0, static_cast<int>(barFloat));
        const double barStart = static_cast<double>(barIdx) * beatsPerBar;
        const double ppqInBar = std::clamp(ppq - barStart, 0.0, std::max(0.0, beatsPerBar - 1.0e-9));

        const double beatLenQuarter = 4.0 / static_cast<double>(state.timeSig.denominator > 0 ? state.timeSig.denominator : 4);
        const int beatIdx = std::clamp(
            static_cast<int>(std::floor(ppqInBar / beatLenQuarter + 1.0e-9)),
            0,
            std::max(0, state.timeSig.numerator - 1));

        const double stepLenQuarter = beatsPerBar / static_cast<double>(stepsPerBar);
        const int stepIdx = std::clamp(
            static_cast<int>(std::floor(ppqInBar / stepLenQuarter + 1.0e-9)),
            0,
            std::max(0, stepsPerBar - 1));

        state.currentBar = barIdx;
        state.currentBeat = beatIdx;
        state.currentStep = stepIdx;
        state.barStartPpq = barStart;
    }

    double sampleRate_ = 44100.0;
    std::atomic<bool> standalonePlaying_ { false };
    std::atomic<double> manualBpm_ { 104.0 };

    double internalPpq_ = 0.0;
    double liveBpm_ = 104.0;
    double lastGoodLiveBpm_ = 104.0;
    bool liveAutoPlaying_ = false;
    double liveSilenceSeconds_ = 0.0;
    bool wasPlaying_ = false;

    std::array<double, kMaxTapIntervals> tapIntervals_ {};
    int tapIntervalCount_ = 0;
    int tapIntervalWritePos_ = 0;
    double lastTapTime_ = -1.0;
};

} // namespace sessionmate
