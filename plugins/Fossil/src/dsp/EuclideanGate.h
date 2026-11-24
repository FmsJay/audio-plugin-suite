#pragma once

#include "../FossilParams.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace fossil::dsp
{

class EuclideanGate
{
public:
    EuclideanGate() noexcept
    {
        pattern_     = computePattern (steps_, hits_, rotation_);
        patternMask_ = patternToMask (pattern_, steps_);
        updateCoeffs();
    }

    // Exact Bjorklund algorithm (zero heap allocations, noexcept).
    static std::array<bool, kMaxEuclidSteps> computePattern (int steps, int hits, int rotation) noexcept
    {
        std::array<bool, kMaxEuclidSteps> out {};

        const int n   = std::clamp (steps, 1, kMaxEuclidSteps);
        const int k   = std::clamp (hits, 0, n);
        const int rot = std::clamp (rotation, 0, n - 1);

        if (k == 0)
            return out;

        if (k == n)
        {
            for (int i = 0; i < n; ++i)
                out[static_cast<size_t> (i)] = true;
            return out;
        }

        // Fixed-capacity Bjorklund groups: each group packs up to 16 bits MSB-first.
        struct Group
        {
            uint16_t bits = 0;
            uint8_t  len  = 0;
        };

        std::array<Group, kMaxEuclidSteps> groups {};
        for (int i = 0; i < k; ++i)
            groups[static_cast<size_t> (i)] = { 1u, 1u };
        for (int i = k; i < n; ++i)
            groups[static_cast<size_t> (i)] = { 0u, 1u };

        int numFront = k;
        int numBack  = n - k;

        while (numBack > 1)
        {
            const int minPair = std::min (numFront, numBack);
            for (int i = 0; i < minPair; ++i)
            {
                auto&       front = groups[static_cast<size_t> (i)];
                const auto& back  = groups[static_cast<size_t> (numFront + i)];
                front.bits = static_cast<uint16_t> ((front.bits << back.len) | back.bits);
                front.len  = static_cast<uint8_t> (front.len + back.len);
            }

            if (numFront >= numBack)
            {
                // Leftover front groups at [numBack .. numFront - 1] become the new back.
                const int newBack  = numFront - numBack;
                const int newFront = numBack;
                numFront = newFront;
                numBack  = newBack;
            }
            else
            {
                // Leftover back groups at [2 * numFront .. numFront + numBack - 1] shift down.
                const int newBack = numBack - numFront;
                for (int i = 0; i < newBack; ++i)
                    groups[static_cast<size_t> (numFront + i)] = groups[static_cast<size_t> (numFront + numFront + i)];
                numBack = newBack;
            }

            if (numBack == 0)
                break;
        }

        std::array<bool, kMaxEuclidSteps> base {};
        int writeIdx = 0;
        const int totalGroups = numFront + numBack;
        for (int g = 0; g < totalGroups && writeIdx < n; ++g)
        {
            const auto& grp = groups[static_cast<size_t> (g)];
            for (int b = static_cast<int> (grp.len) - 1; b >= 0 && writeIdx < n; --b)
            {
                base[static_cast<size_t> (writeIdx++)] = ((grp.bits >> b) & 1u) != 0u;
            }
        }

        for (int i = 0; i < n; ++i)
            out[static_cast<size_t> ((i + rot) % n)] = base[static_cast<size_t> (i)];

        return out;
    }

    static uint32_t patternToMask (const std::array<bool, kMaxEuclidSteps>& pat, int steps) noexcept
    {
        const int n = std::clamp (steps, 0, kMaxEuclidSteps);
        uint32_t mask = 0u;
        for (int i = 0; i < n; ++i)
        {
            if (pat[static_cast<size_t> (i)])
                mask |= (1u << static_cast<uint32_t> (i));
        }
        return mask;
    }

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = (std::isfinite (sampleRate) && sampleRate >= 1000.0) ? sampleRate : 44100.0;
        updateCoeffs();
        reset();
    }

    void reset() noexcept
    {
        stepPhase_      = 0.0;
        currentStep_    = 0;
        prevStep_       = -1;
        stepSeq_        = 0u;
        hitSeq_         = 0u;
        envelope_       = 1.0f;
        hostSynced_     = false;
        activeBpm_      = static_cast<double> (internalBpm_);
        stepsPerSample_ = (activeBpm_ / 60.0) * stepsPerQuarter_ / sampleRate_;
    }

    void configure (bool  enabled,
                    int   steps,
                    int   hits,
                    int   rotation,
                    float smoothness,
                    float depth,
                    int   rateChoice,
                    float internalBpm) noexcept
    {
        enabled_ = enabled;

        const int clampedSteps = std::clamp (steps, 1, kMaxEuclidSteps);
        const int clampedHits  = std::clamp (hits, 0, clampedSteps);
        const int clampedRot   = std::clamp (rotation, 0, clampedSteps - 1);

        if (clampedSteps != steps_ || clampedHits != hits_ || clampedRot != rotation_)
        {
            steps_       = clampedSteps;
            hits_        = clampedHits;
            rotation_    = clampedRot;
            pattern_     = computePattern (steps_, hits_, rotation_);
            patternMask_ = patternToMask (pattern_, steps_);

            if (stepPhase_ >= static_cast<double> (steps_))
                stepPhase_ = std::fmod (stepPhase_, static_cast<double> (steps_));
            if (stepPhase_ < 0.0 || ! std::isfinite (stepPhase_))
                stepPhase_ = 0.0;
            currentStep_ = std::clamp (static_cast<int> (stepPhase_), 0, steps_ - 1);
        }

        const float clampedSmooth = std::isfinite (smoothness) ? std::clamp (smoothness, 0.0f, 1.0f) : 0.35f;
        if (clampedSmooth != smoothness_)
        {
            smoothness_ = clampedSmooth;
            updateCoeffs();
        }

        depth_ = std::isfinite (depth) ? std::clamp (depth, 0.0f, 1.0f) : 0.85f;

        switch (std::clamp (rateChoice, 0, 4))
        {
            case 0: stepsPerQuarter_ = 1.0; break; // 1/4
            case 1: stepsPerQuarter_ = 2.0; break; // 1/8
            case 2: stepsPerQuarter_ = 4.0; break; // 1/16
            case 3: stepsPerQuarter_ = 3.0; break; // 1/8T
            case 4: stepsPerQuarter_ = 8.0; break; // 1/32
            default: stepsPerQuarter_ = 4.0; break;
        }

        internalBpm_ = std::isfinite (internalBpm) ? std::clamp (internalBpm, 20.0f, 300.0f) : 120.0f;

        if (! hostSynced_)
            activeBpm_ = static_cast<double> (internalBpm_);

        stepsPerSample_ = (activeBpm_ / 60.0) * stepsPerQuarter_ / sampleRate_;
    }

    void syncBlock (bool hostPlaying, double hostBpm, double hostPpqPosition) noexcept
    {
        if (hostPlaying && std::isfinite (hostBpm) && hostBpm > 1.0 && std::isfinite (hostPpqPosition))
        {
            hostSynced_ = true;
            activeBpm_  = std::clamp (hostBpm, 20.0, 400.0);

            const double totalSteps = hostPpqPosition * stepsPerQuarter_;
            double wrapped = std::fmod (totalSteps, static_cast<double> (steps_));
            if (wrapped < 0.0)
                wrapped += static_cast<double> (steps_);
            stepPhase_   = std::isfinite (wrapped) ? wrapped : 0.0;
            currentStep_ = std::clamp (static_cast<int> (stepPhase_), 0, steps_ - 1);
        }
        else
        {
            hostSynced_ = false;
            activeBpm_  = static_cast<double> (internalBpm_);
            if (stepPhase_ >= static_cast<double> (steps_) || stepPhase_ < 0.0 || ! std::isfinite (stepPhase_))
                stepPhase_ = 0.0;
            currentStep_ = std::clamp (static_cast<int> (stepPhase_), 0, steps_ - 1);
        }

        stepsPerSample_ = (activeBpm_ / 60.0) * stepsPerQuarter_ / sampleRate_;
    }

    float processSample() noexcept
    {
        stepPhase_ += stepsPerSample_;
        const double dSteps = static_cast<double> (steps_);
        if (stepPhase_ >= dSteps)
        {
            stepPhase_ -= dSteps;
            if (stepPhase_ >= dSteps)
                stepPhase_ = std::fmod (stepPhase_, dSteps);
        }
        if (stepPhase_ < 0.0 || ! std::isfinite (stepPhase_))
            stepPhase_ = 0.0;

        currentStep_ = std::clamp (static_cast<int> (stepPhase_), 0, steps_ - 1);
        const bool isHit = pattern_[static_cast<size_t> (currentStep_)];

        if (currentStep_ != prevStep_)
        {
            prevStep_ = currentStep_;
            ++stepSeq_;
            if (enabled_ && isHit)
                ++hitSeq_;
        }

        if (! enabled_)
        {
            envelope_ = 1.0f;
            return 1.0f;
        }

        const float target = isHit ? 1.0f : 0.0f;
        const float coeff  = (target > envelope_) ? attackCoeff_ : releaseCoeff_;
        envelope_ += coeff * (target - envelope_);

        if (! std::isfinite (envelope_) || std::abs (envelope_) < 1.0e-20f)
            envelope_ = 0.0f;
        else
            envelope_ = std::clamp (envelope_, 0.0f, 1.0f);

        const float gain = (1.0f - depth_) + depth_ * envelope_;
        return std::isfinite (gain) ? std::clamp (gain, 0.0f, 1.0f) : 1.0f;
    }

    int      getCurrentStep() const noexcept     { return currentStep_; }
    float    getCurrentEnvelope() const noexcept { return envelope_; }
    uint32_t getPatternMask() const noexcept     { return patternMask_; }
    uint32_t getStepSeq() const noexcept         { return stepSeq_; }
    uint32_t getHitSeq() const noexcept          { return hitSeq_; }
    bool     isCurrentStepHit() const noexcept   { return pattern_[static_cast<size_t> (std::clamp (currentStep_, 0, steps_ - 1))]; }
    double   getActiveBpm() const noexcept       { return activeBpm_; }
    bool     isHostSynced() const noexcept       { return hostSynced_; }

private:
    void updateCoeffs() noexcept
    {
        // Smoothness [0..1] maps attack 1ms..35ms and release 12ms..180ms exponentially.
        const double attSec = 0.001 * std::pow (35.0, static_cast<double> (smoothness_));
        const double relSec = 0.012 * std::pow (15.0, static_cast<double> (smoothness_));

        attackCoeff_  = static_cast<float> (1.0 - std::exp (-1.0 / std::max (1.0, attSec * sampleRate_)));
        releaseCoeff_ = static_cast<float> (1.0 - std::exp (-1.0 / std::max (1.0, relSec * sampleRate_)));
    }

    double sampleRate_      = 44100.0;
    bool   enabled_         = false;
    int    steps_           = 16;
    int    hits_            = 5;
    int    rotation_        = 0;
    float  smoothness_      = 0.35f;
    float  depth_           = 0.85f;
    double stepsPerQuarter_ = 4.0;
    float  internalBpm_     = 120.0f;

    double activeBpm_       = 120.0;
    bool   hostSynced_      = false;
    double stepPhase_       = 0.0;
    double stepsPerSample_  = 0.0;
    int    currentStep_     = 0;
    int    prevStep_        = -1;
    uint32_t stepSeq_       = 0u;
    uint32_t hitSeq_        = 0u;

    float  attackCoeff_     = 0.01f;
    float  releaseCoeff_    = 0.002f;
    float  envelope_        = 1.0f;

    std::array<bool, kMaxEuclidSteps> pattern_ {};
    uint32_t                          patternMask_ = 0u;
};

} // namespace fossil::dsp
