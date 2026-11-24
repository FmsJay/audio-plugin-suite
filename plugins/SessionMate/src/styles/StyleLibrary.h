#pragma once

#include "../SessionMateTypes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <string>

namespace sessionmate {

enum class GenreFamily : int {
    RockIndie = 0,
    FunkSoulRnB,
    JazzLatin,
    ElectronicHipHop,
    RootsWorld,
    OddModern
};

inline const char* genreFamilyName(GenreFamily f) noexcept {
    switch (f) {
        case GenreFamily::RockIndie:        return "Rock & Indie";
        case GenreFamily::FunkSoulRnB:      return "Funk, Soul & R&B";
        case GenreFamily::JazzLatin:        return "Jazz & Latin";
        case GenreFamily::ElectronicHipHop: return "Electronic & Hip-Hop";
        case GenreFamily::RootsWorld:       return "Roots & World";
        case GenreFamily::OddModern:        return "Odd & Modern";
    }
    return "Rock & Indie";
}

struct StyleGenome {
    std::string name;
    GenreFamily family = GenreFamily::RockIndie;
    TimeSignature defaultTimeSig { 4, 4 };
    double defaultBpm = 110.0;
    float defaultSwing = 0.50f;
    float defaultPushPullMs = 0.0f;
    std::array<int, kNumSlots> recommendedPatches { 0, 0, 0, 0, 0, 0 };

    // Step probability / accent tables (0.0 = silent, 0.01..1.0 = probability & accent weight)
    std::array<float, kMaxStepsPerBar> kick {};
    std::array<float, kMaxStepsPerBar> snare {};
    std::array<float, kMaxStepsPerBar> ghostSnare {};
    std::array<float, kMaxStepsPerBar> hatClosed {};
    std::array<float, kMaxStepsPerBar> hatOpen {};
    std::array<float, kMaxStepsPerBar> ride {};
    std::array<float, kMaxStepsPerBar> percPrimary {};
    std::array<float, kMaxStepsPerBar> percSecondary {};
    std::array<float, kMaxStepsPerBar> bassSync {};
    std::array<float, kMaxStepsPerBar> keysComp {};
    std::array<float, kMaxStepsPerBar> guitarStrum {};
    std::array<float, kMaxStepsPerBar> guitarUpstroke {};
    std::array<float, kMaxStepsPerBar> hornsStab {};

    float bassOctaveProb = 0.20f;
    float bassWalkProb = 0.25f;
    int keysSustainSteps = 4;
    float guitarMuteProb = 0.20f;
    float hornsFillDensity = 0.35f;
    float drumsFillDensity = 0.50f;
    float feelDrunkJitterMs = 0.0f;
    bool useSideStick = false;
    bool useBrushFeel = false;
    bool reggaeSkank = false;
    bool isSwingTriplet = false;
};

class StyleLibrary {
public:
    static constexpr int kNumStyles = 32;

    StyleLibrary() {
        buildAllStyles();
    }

    int count() const noexcept {
        return kNumStyles;
    }

    const StyleGenome& get(int index) const noexcept {
        const int clamped = std::clamp(index, 0, kNumStyles - 1);
        return styles_[static_cast<size_t>(clamped)];
    }

    // Real-time safe (zero heap allocations): returns a morphed genome with short SSO name
    StyleGenome morph(int indexA, int indexB, float morphAmount) const noexcept {
        const StyleGenome& a = get(indexA);
        const StyleGenome& b = get(indexB);
        const float t = std::clamp(morphAmount, 0.0f, 1.0f);
        const float inv = 1.0f - t;

        StyleGenome out {};
        out.name = "Morph"; // 5 chars: guaranteed SSO, zero heap allocation on audio thread
        out.family = (t < 0.5f) ? a.family : b.family;
        out.defaultTimeSig = (t < 0.5f) ? a.defaultTimeSig : b.defaultTimeSig;
        out.defaultBpm = a.defaultBpm * static_cast<double>(inv) + b.defaultBpm * static_cast<double>(t);
        out.defaultSwing = a.defaultSwing * inv + b.defaultSwing * t;
        out.defaultPushPullMs = a.defaultPushPullMs * inv + b.defaultPushPullMs * t;
        out.recommendedPatches = (t < 0.5f) ? a.recommendedPatches : b.recommendedPatches;

        auto lerpArr = [&](const std::array<float, kMaxStepsPerBar>& va,
                           const std::array<float, kMaxStepsPerBar>& vb,
                           std::array<float, kMaxStepsPerBar>& dst) noexcept {
            for (size_t i = 0; i < static_cast<size_t>(kMaxStepsPerBar); ++i) {
                dst[i] = va[i] * inv + vb[i] * t;
            }
        };

        lerpArr(a.kick,           b.kick,           out.kick);
        lerpArr(a.snare,          b.snare,          out.snare);
        lerpArr(a.ghostSnare,     b.ghostSnare,     out.ghostSnare);
        lerpArr(a.hatClosed,      b.hatClosed,      out.hatClosed);
        lerpArr(a.hatOpen,        b.hatOpen,        out.hatOpen);
        lerpArr(a.ride,           b.ride,           out.ride);
        lerpArr(a.percPrimary,    b.percPrimary,    out.percPrimary);
        lerpArr(a.percSecondary,  b.percSecondary,  out.percSecondary);
        lerpArr(a.bassSync,       b.bassSync,       out.bassSync);
        lerpArr(a.keysComp,       b.keysComp,       out.keysComp);
        lerpArr(a.guitarStrum,    b.guitarStrum,    out.guitarStrum);
        lerpArr(a.guitarUpstroke, b.guitarUpstroke, out.guitarUpstroke);
        lerpArr(a.hornsStab,      b.hornsStab,      out.hornsStab);

        out.bassOctaveProb    = a.bassOctaveProb    * inv + b.bassOctaveProb    * t;
        out.bassWalkProb      = a.bassWalkProb      * inv + b.bassWalkProb      * t;
        out.keysSustainSteps  = std::max(1, static_cast<int>(std::lround(
                                    static_cast<float>(a.keysSustainSteps) * inv +
                                    static_cast<float>(b.keysSustainSteps) * t)));
        out.guitarMuteProb    = a.guitarMuteProb    * inv + b.guitarMuteProb    * t;
        out.hornsFillDensity  = a.hornsFillDensity  * inv + b.hornsFillDensity  * t;
        out.drumsFillDensity  = a.drumsFillDensity  * inv + b.drumsFillDensity  * t;
        out.feelDrunkJitterMs = a.feelDrunkJitterMs * inv + b.feelDrunkJitterMs * t;

        out.useSideStick   = (t < 0.5f) ? a.useSideStick   : b.useSideStick;
        out.useBrushFeel   = (t < 0.5f) ? a.useBrushFeel   : b.useBrushFeel;
        out.reggaeSkank    = (t < 0.5f) ? a.reggaeSkank    : b.reggaeSkank;
        out.isSwingTriplet = (t < 0.5f) ? a.isSwingTriplet : b.isSwingTriplet;

        return out;
    }

private:
    std::array<StyleGenome, kNumStyles> styles_ {};

#ifdef _MSC_VER
    __declspec(noinline)
#endif
    static std::array<float, kMaxStepsPerBar> pat(std::initializer_list<float> vals) noexcept {
        std::array<float, kMaxStepsPerBar> a {};
        size_t i = 0;
        for (float v : vals) {
            if (i < static_cast<size_t>(kMaxStepsPerBar)) a[i++] = v;
        }
        return a;
    }

#ifdef _MSC_VER
#pragma optimize("", off)
#endif
    void buildAllStyles() {
        // =========================================================================
        // 0..5: ROCK & INDIE
        // =========================================================================
        { // 0: 70s Classic Rock
            auto& s = styles_[0];
            s.name = "70s Classic Rock";
            s.family = GenreFamily::RockIndie;
            s.defaultTimeSig = { 4, 4 };
            s.defaultBpm = 118.0;
            s.defaultSwing = 0.52f;
            s.defaultPushPullMs = 1.5f;
            s.recommendedPatches = { 0, 1, 3, 4, 0, 0 };
            s.kick           = pat({ 1.0f, 0, 0, 0, 0, 0, 0, 0.35f, 0.92f, 0, 0.72f, 0, 0, 0, 0.25f, 0 });
            s.snare          = pat({ 0, 0, 0, 0, 1.0f, 0, 0, 0, 0, 0, 0, 0, 1.0f, 0, 0, 0 });
            s.ghostSnare     = pat({ 0, 0, 0, 0, 0, 0, 0, 0.22f, 0, 0.25f, 0, 0, 0, 0, 0, 0.20f });
            s.hatClosed      = pat({ 0.90f, 0, 0.70f, 0, 0.85f, 0, 0.70f, 0, 0.90f, 0, 0.70f, 0, 0.85f, 0, 0.45f, 0 });
            s.hatOpen        = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.55f, 0 });
            s.ride           = pat({ 0.85f, 0, 0.65f, 0, 0.80f, 0, 0.65f, 0, 0.85f, 0, 0.65f, 0, 0.80f, 0, 0.65f, 0 });
            s.percPrimary    = pat({ 0, 0, 0, 0, 0.70f, 0, 0, 0, 0, 0, 0, 0, 0.75f, 0, 0, 0 });
            s.percSecondary  = pat({ 0, 0, 0.30f, 0, 0, 0, 0.30f, 0, 0, 0, 0.30f, 0, 0, 0, 0.30f, 0 });
            s.bassSync       = pat({ 1.0f, 0, 0.55f, 0, 0.45f, 0, 0.55f, 0, 0.90f, 0, 0.75f, 0, 0.50f, 0, 0.60f, 0 });
            s.keysComp       = pat({ 0.85f, 0, 0, 0, 0, 0, 0.45f, 0, 0.70f, 0, 0, 0, 0, 0, 0.40f, 0 });
            s.guitarStrum    = pat({ 1.0f, 0, 0.45f, 0, 0.70f, 0, 0.60f, 0, 0.85f, 0, 0.50f, 0, 0.70f, 0, 0.55f, 0 });
            s.guitarUpstroke = pat({ 0, 0, 0.35f, 0, 0, 0, 0.50f, 0, 0, 0, 0.40f, 0, 0, 0, 0.55f, 0 });
            s.hornsStab      = pat({ 0.45f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.35f, 0, 0, 0, 0.30f, 0 });
            s.bassOctaveProb = 0.20f; s.bassWalkProb = 0.35f; s.keysSustainSteps = 6;
            s.guitarMuteProb = 0.25f; s.hornsFillDensity = 0.25f; s.drumsFillDensity = 0.60f;
        }

        { // 1: 90s Grunge
            auto& s = styles_[1];
            s.name = "90s Grunge";
            s.family = GenreFamily::RockIndie;
            s.defaultTimeSig = { 4, 4 };
            s.defaultBpm = 110.0;
            s.defaultSwing = 0.50f;
            s.defaultPushPullMs = 4.0f;
            s.recommendedPatches = { 3, 1, 5, 4, 0, 1 };
            s.kick           = pat({ 1.0f, 0, 0, 0.65f, 0, 0, 0.45f, 0, 0.85f, 0, 0.80f, 0, 0, 0, 0.35f, 0 });
            s.snare          = pat({ 0, 0, 0, 0, 1.0f, 0, 0, 0, 0, 0, 0, 0, 1.0f, 0, 0, 0.25f });
            s.ghostSnare     = pat({ 0, 0, 0, 0, 0, 0, 0, 0.30f, 0, 0.20f, 0, 0, 0, 0, 0, 0.25f });
            s.hatClosed      = pat({ 0.75f, 0, 0.65f, 0, 0.75f, 0, 0.65f, 0, 0.75f, 0, 0.65f, 0, 0.75f, 0, 0.50f, 0 });
            s.hatOpen        = pat({ 0.35f, 0, 0, 0, 0.30f, 0, 0, 0, 0.35f, 0, 0, 0, 0.30f, 0, 0.70f, 0 });
            s.ride           = pat({ 0.80f, 0, 0.70f, 0, 0.80f, 0, 0.70f, 0, 0.80f, 0, 0.70f, 0, 0.80f, 0, 0.70f, 0 });
            s.percPrimary    = pat({ 0, 0, 0, 0, 0.40f, 0, 0, 0, 0, 0, 0, 0, 0.40f, 0, 0, 0 });
            s.percSecondary  = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
            s.bassSync       = pat({ 1.0f, 0, 0.70f, 0.60f, 0.50f, 0, 0.65f, 0, 0.90f, 0, 0.80f, 0, 0.55f, 0, 0.65f, 0 });
            s.keysComp       = pat({ 0.65f, 0, 0, 0, 0, 0, 0, 0, 0.50f, 0, 0, 0, 0, 0, 0, 0 });
            s.guitarStrum    = pat({ 1.0f, 0, 0.75f, 0.65f, 0.85f, 0, 0.70f, 0, 0.95f, 0, 0.80f, 0, 0.85f, 0, 0.75f, 0 });
            s.guitarUpstroke = pat({ 0, 0, 0.45f, 0, 0, 0, 0.45f, 0, 0, 0, 0.45f, 0, 0, 0, 0.50f, 0 });
            s.hornsStab      = pat({ 0.30f, 0, 0, 0, 0, 0, 0, 0, 0.25f, 0, 0, 0, 0, 0, 0, 0 });
            s.bassOctaveProb = 0.15f; s.bassWalkProb = 0.20f; s.keysSustainSteps = 8;
            s.guitarMuteProb = 0.45f; s.hornsFillDensity = 0.15f; s.drumsFillDensity = 0.65f;
        }

        { // 2: Garage Post-Punk
            auto& s = styles_[2];
            s.name = "Garage Post-Punk";
            s.family = GenreFamily::RockIndie;
            s.defaultTimeSig = { 4, 4 };
            s.defaultBpm = 142.0;
            s.defaultSwing = 0.50f;
            s.defaultPushPullMs = -3.0f;
            s.recommendedPatches = { 1, 1, 1, 0, 0, 3 };
            s.kick           = pat({ 1.0f, 0, 0, 0, 0.70f, 0, 0, 0, 1.0f, 0, 0.55f, 0, 0.65f, 0, 0, 0 });
            s.snare          = pat({ 0, 0, 0, 0, 1.0f, 0, 0, 0, 0, 0, 0, 0, 1.0f, 0, 0, 0 });
            s.ghostSnare     = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.20f, 0 });
            s.hatClosed      = pat({ 0.90f, 0, 0.85f, 0, 0.90f, 0, 0.85f, 0, 0.90f, 0, 0.85f, 0, 0.90f, 0, 0.85f, 0 });
            s.hatOpen        = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.45f, 0 });
            s.ride           = pat({ 0.85f, 0, 0.80f, 0, 0.85f, 0, 0.80f, 0, 0.85f, 0, 0.80f, 0, 0.85f, 0, 0.80f, 0 });
            s.percPrimary    = pat({ 0, 0, 0.45f, 0, 0, 0, 0.45f, 0, 0, 0, 0.45f, 0, 0, 0, 0.45f, 0 });
            s.percSecondary  = pat({ 0, 0, 0, 0, 0.50f, 0, 0, 0, 0, 0, 0, 0, 0.50f, 0, 0, 0 });
            s.bassSync       = pat({ 1.0f, 0, 0.90f, 0, 0.90f, 0, 0.90f, 0, 1.0f, 0, 0.90f, 0, 0.90f, 0, 0.90f, 0 });
            s.keysComp       = pat({ 0.75f, 0, 0, 0, 0.60f, 0, 0, 0, 0.75f, 0, 0, 0, 0.60f, 0, 0, 0 });
            s.guitarStrum    = pat({ 0.95f, 0, 0.80f, 0, 0.90f, 0, 0.80f, 0, 0.95f, 0, 0.80f, 0, 0.90f, 0, 0.80f, 0 });
            s.guitarUpstroke = pat({ 0, 0, 0.70f, 0, 0, 0, 0.70f, 0, 0, 0, 0.70f, 0, 0, 0, 0.70f, 0 });
            s.hornsStab      = pat({ 0.50f, 0, 0, 0, 0, 0, 0.35f, 0, 0, 0, 0, 0, 0.40f, 0, 0, 0 });
            s.bassOctaveProb = 0.25f; s.bassWalkProb = 0.30f; s.keysSustainSteps = 3;
            s.guitarMuteProb = 0.35f; s.hornsFillDensity = 0.25f; s.drumsFillDensity = 0.55f;
        }

        { // 3: Indie Dream-Pop
            auto& s = styles_[3];
            s.name = "Indie Dream-Pop";
            s.family = GenreFamily::RockIndie;
            s.defaultTimeSig = { 4, 4 };
            s.defaultBpm = 116.0;
            s.defaultSwing = 0.51f;
            s.defaultPushPullMs = 2.0f;
            s.recommendedPatches = { 0, 0, 5, 0, 0, 1 };
            s.kick           = pat({ 1.0f, 0, 0, 0, 0, 0, 0.70f, 0, 0.45f, 0, 0.75f, 0, 0, 0, 0, 0 });
            s.snare          = pat({ 0, 0, 0, 0, 0.92f, 0, 0, 0, 0, 0, 0, 0, 0.95f, 0, 0, 0 });
            s.ghostSnare     = pat({ 0, 0, 0, 0, 0, 0, 0, 0.18f, 0, 0, 0, 0, 0, 0, 0, 0.18f });
            s.hatClosed      = pat({ 0.75f, 0, 0.65f, 0, 0.75f, 0, 0.65f, 0, 0.75f, 0, 0.65f, 0, 0.75f, 0, 0.65f, 0 });
            s.hatOpen        = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.40f, 0 });
            s.ride           = pat({ 0.70f, 0, 0.55f, 0, 0.70f, 0, 0.55f, 0, 0.70f, 0, 0.55f, 0, 0.70f, 0, 0.55f, 0 });
            s.percPrimary    = pat({ 0.45f, 0, 0.55f, 0, 0.45f, 0, 0.55f, 0, 0.45f, 0, 0.55f, 0, 0.45f, 0, 0.55f, 0 });
            s.percSecondary  = pat({ 0, 0, 0, 0, 0.60f, 0, 0, 0, 0, 0, 0, 0, 0.60f, 0, 0, 0 });
            s.bassSync       = pat({ 1.0f, 0, 0.50f, 0, 0, 0, 0.75f, 0, 0.65f, 0, 0.70f, 0, 0, 0, 0.45f, 0 });
            s.keysComp       = pat({ 0.90f, 0, 0, 0, 0, 0, 0, 0, 0.75f, 0, 0, 0, 0, 0, 0, 0 });
            s.guitarStrum    = pat({ 0.85f, 0, 0.50f, 0, 0.60f, 0, 0.65f, 0, 0.75f, 0, 0.55f, 0, 0.60f, 0, 0.50f, 0 });
            s.guitarUpstroke = pat({ 0, 0, 0.40f, 0, 0, 0, 0.55f, 0, 0, 0, 0.45f, 0, 0, 0, 0.45f, 0 });
            s.hornsStab      = pat({ 0.55f, 0, 0, 0, 0, 0, 0, 0, 0.45f, 0, 0, 0, 0, 0, 0, 0 });
            s.bassOctaveProb = 0.30f; s.bassWalkProb = 0.25f; s.keysSustainSteps = 8;
            s.guitarMuteProb = 0.10f; s.hornsFillDensity = 0.30f; s.drumsFillDensity = 0.35f;
        }

        { // 4: Post-Rock Swell
            auto& s = styles_[4];
            s.name = "Post-Rock Swell";
            s.family = GenreFamily::RockIndie;
            s.defaultTimeSig = { 4, 4 };
            s.defaultBpm = 92.0;
            s.defaultSwing = 0.50f;
            s.defaultPushPullMs = 3.0f;
            s.recommendedPatches = { 0, 0, 2, 0, 0, 1 };
            s.kick           = pat({ 1.0f, 0, 0, 0, 0, 0, 0.55f, 0, 0.80f, 0, 0, 0, 0, 0, 0.45f, 0 });
            s.snare          = pat({ 0, 0, 0, 0, 0.85f, 0, 0, 0, 0, 0, 0, 0, 0.90f, 0, 0, 0 });
            s.ghostSnare     = pat({ 0, 0, 0.25f, 0, 0, 0, 0.30f, 0, 0, 0, 0.25f, 0, 0, 0, 0.30f, 0 });
            s.hatClosed      = pat({ 0.65f, 0, 0.50f, 0, 0.65f, 0, 0.50f, 0, 0.65f, 0, 0.50f, 0, 0.65f, 0, 0.50f, 0 });
            s.hatOpen        = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.35f, 0 });
            s.ride           = pat({ 0.85f, 0, 0.70f, 0, 0.80f, 0, 0.70f, 0, 0.85f, 0, 0.70f, 0, 0.80f, 0, 0.70f, 0 });
            s.percPrimary    = pat({ 0.50f, 0, 0, 0, 0, 0, 0, 0, 0.50f, 0, 0, 0, 0, 0, 0, 0 });
            s.percSecondary  = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.40f, 0, 0, 0 });
            s.bassSync       = pat({ 1.0f, 0, 0, 0, 0, 0, 0.55f, 0, 0.85f, 0, 0, 0, 0, 0, 0.45f, 0 });
            s.keysComp       = pat({ 0.95f, 0, 0, 0, 0, 0, 0, 0, 0.60f, 0, 0, 0, 0, 0, 0, 0 });
            s.guitarStrum    = pat({ 0.80f, 0, 0.65f, 0, 0.70f, 0, 0.65f, 0, 0.80f, 0, 0.65f, 0, 0.70f, 0, 0.65f, 0 });
            s.guitarUpstroke = pat({ 0, 0.30f, 0, 0.30f, 0, 0.30f, 0, 0.30f, 0, 0.30f, 0, 0.30f, 0, 0.30f, 0, 0.30f });
            s.hornsStab      = pat({ 0.80f, 0, 0, 0, 0, 0, 0, 0, 0.50f, 0, 0, 0, 0, 0, 0, 0 });
            s.bassOctaveProb = 0.15f; s.bassWalkProb = 0.20f; s.keysSustainSteps = 12;
            s.guitarMuteProb = 0.05f; s.hornsFillDensity = 0.45f; s.drumsFillDensity = 0.40f;
        }

        { // 5: Modern Arena Rock
            auto& s = styles_[5];
            s.name = "Modern Arena Rock";
            s.family = GenreFamily::RockIndie;
            s.defaultTimeSig = { 4, 4 };
            s.defaultBpm = 128.0;
            s.defaultSwing = 0.50f;
            s.defaultPushPullMs = -1.5f;
            s.recommendedPatches = { 3, 1, 2, 4, 0, 1 };
            s.kick           = pat({ 1.0f, 0, 0, 0, 0, 0, 0.65f, 0, 1.0f, 0, 0.65f, 0, 0, 0, 0.40f, 0 });
            s.snare          = pat({ 0, 0, 0, 0, 1.0f, 0, 0, 0, 0, 0, 0, 0, 1.0f, 0, 0, 0 });
            s.ghostSnare     = pat({ 0, 0, 0, 0, 0, 0, 0, 0.15f, 0, 0, 0, 0, 0, 0, 0, 0.20f });
            s.hatClosed      = pat({ 0.90f, 0, 0.75f, 0, 0.90f, 0, 0.75f, 0, 0.90f, 0, 0.75f, 0, 0.90f, 0, 0.65f, 0 });
            s.hatOpen        = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.55f, 0 });
            s.ride           = pat({ 0.90f, 0, 0.75f, 0, 0.90f, 0, 0.75f, 0, 0.90f, 0, 0.75f, 0, 0.90f, 0, 0.75f, 0 });
            s.percPrimary    = pat({ 0, 0, 0, 0, 0.80f, 0, 0, 0, 0, 0, 0, 0, 0.80f, 0, 0, 0 });
            s.percSecondary  = pat({ 0.40f, 0, 0.40f, 0, 0.40f, 0, 0.40f, 0, 0.40f, 0, 0.40f, 0, 0.40f, 0, 0.40f, 0 });
            s.bassSync       = pat({ 1.0f, 0, 0.80f, 0, 0.75f, 0, 0.80f, 0, 1.0f, 0, 0.80f, 0, 0.75f, 0, 0.80f, 0 });
            s.keysComp       = pat({ 0.90f, 0, 0, 0, 0.65f, 0, 0, 0, 0.85f, 0, 0, 0, 0.65f, 0, 0, 0 });
            s.guitarStrum    = pat({ 1.0f, 0, 0.75f, 0, 0.85f, 0, 0.75f, 0, 1.0f, 0, 0.75f, 0, 0.85f, 0, 0.75f, 0 });
            s.guitarUpstroke = pat({ 0, 0, 0.40f, 0, 0, 0, 0.50f, 0, 0, 0, 0.40f, 0, 0, 0, 0.50f, 0 });
            s.hornsStab      = pat({ 0.65f, 0, 0, 0, 0, 0, 0, 0, 0.55f, 0, 0, 0, 0, 0, 0, 0 });
            s.bassOctaveProb = 0.25f; s.bassWalkProb = 0.25f; s.keysSustainSteps = 6;
            s.guitarMuteProb = 0.30f; s.hornsFillDensity = 0.35f; s.drumsFillDensity = 0.70f;
        }

        // =========================================================================
        // 6..11: FUNK, SOUL & R&B
        // =========================================================================
        { // 6: 70s Dry Pocket Funk (Default Style A)
            auto& s = styles_[6];
            s.name = "70s Dry Pocket Funk";
            s.family = GenreFamily::FunkSoulRnB;
            s.defaultTimeSig = { 4, 4 };
            s.defaultBpm = 104.0;
            s.defaultSwing = 0.55f;
            s.defaultPushPullMs = 2.0f;
            s.recommendedPatches = { 1, 2, 4, 1, 1, 0 };
            s.kick           = pat({ 1.0f, 0, 0.35f, 0, 0, 0, 0, 0.55f, 0.30f, 0, 0.90f, 0, 0, 0, 0.45f, 0 });
            s.snare          = pat({ 0, 0, 0, 0, 1.0f, 0, 0, 0, 0, 0, 0, 0, 1.0f, 0, 0, 0 });
            s.ghostSnare     = pat({ 0, 0, 0, 0, 0, 0, 0, 0.65f, 0, 0.75f, 0, 0.35f, 0, 0, 0, 0.60f });
            s.hatClosed      = pat({ 0.95f, 0.55f, 0.85f, 0.50f, 0.90f, 0.50f, 0.80f, 0.45f, 0.95f, 0.55f, 0.85f, 0.50f, 0.90f, 0.50f, 0.55f, 0.45f });
            s.hatOpen        = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.70f, 0 });
            s.ride           = pat({ 0.75f, 0, 0.60f, 0, 0.75f, 0, 0.60f, 0, 0.75f, 0, 0.60f, 0, 0.75f, 0, 0.60f, 0 });
            s.percPrimary    = pat({ 0.65f, 0.45f, 0.60f, 0.45f, 0.65f, 0.45f, 0.60f, 0.45f, 0.65f, 0.45f, 0.60f, 0.45f, 0.65f, 0.45f, 0.60f, 0.45f });
            s.percSecondary  = pat({ 0, 0, 0.45f, 0, 0.65f, 0, 0, 0.40f, 0, 0, 0.50f, 0, 0.65f, 0, 0.55f, 0 });
            s.bassSync       = pat({ 1.0f, 0, 0.45f, 0.40f, 0, 0, 0.35f, 0.65f, 0, 0, 0.92f, 0, 0.35f, 0, 0.65f, 0.35f });
            s.keysComp       = pat({ 0.85f, 0, 0, 0.55f, 0, 0, 0.70f, 0, 0, 0.45f, 0, 0, 0.65f, 0, 0.55f, 0 });
            s.guitarStrum    = pat({ 0.85f, 0.40f, 0.70f, 0.55f, 0.80f, 0.40f, 0.75f, 0.45f, 0.85f, 0.40f, 0.70f, 0.50f, 0.80f, 0.40f, 0.75f, 0.50f });
            s.guitarUpstroke = pat({ 0, 0.50f, 0.60f, 0.55f, 0, 0.45f, 0.65f, 0.45f, 0, 0.50f, 0.60f, 0.55f, 0, 0.45f, 0.65f, 0.50f });
            s.hornsStab      = pat({ 0.85f, 0, 0, 0.50f, 0, 0, 0, 0, 0, 0, 0.75f, 0, 0, 0, 0.60f, 0 });
            s.bassOctaveProb = 0.55f; s.bassWalkProb = 0.40f; s.keysSustainSteps = 2;
            s.guitarMuteProb = 0.65f; s.hornsFillDensity = 0.70f; s.drumsFillDensity = 0.55f;
        }

        { // 7: Neo-Soul (J-Dilla) (Default Style B)
            auto& s = styles_[7];
            s.name = "Neo-Soul (J-Dilla)";
            s.family = GenreFamily::FunkSoulRnB;
            s.defaultTimeSig = { 4, 4 };
            s.defaultBpm = 86.0;
            s.defaultSwing = 0.61f;
            s.defaultPushPullMs = 9.0f;
            s.recommendedPatches = { 4, 0, 0, 0, 0, 2 };
            s.kick           = pat({ 1.0f, 0, 0, 0.45f, 0, 0, 0, 0.60f, 0.35f, 0, 0.85f, 0.40f, 0, 0, 0, 0.30f });
            s.snare          = pat({ 0, 0, 0, 0, 0.95f, 0, 0, 0, 0, 0, 0, 0, 0.98f, 0, 0, 0 });
            s.ghostSnare     = pat({ 0, 0, 0, 0.25f, 0, 0, 0.30f, 0.55f, 0, 0.60f, 0, 0, 0, 0, 0.35f, 0.50f });
            s.hatClosed      = pat({ 0.85f, 0.35f, 0.70f, 0.30f, 0.80f, 0.30f, 0.70f, 0.35f, 0.85f, 0.35f, 0.70f, 0.30f, 0.80f, 0.30f, 0.65f, 0.35f });
            s.hatOpen        = pat({ 0, 0, 0, 0, 0, 0, 0.35f, 0, 0, 0, 0, 0, 0, 0, 0.45f, 0 });
            s.ride           = pat({ 0.70f, 0, 0.55f, 0, 0.65f, 0, 0.55f, 0, 0.70f, 0, 0.55f, 0, 0.65f, 0, 0.55f, 0 });
            s.percPrimary    = pat({ 0.55f, 0.30f, 0.50f, 0.30f, 0.55f, 0.30f, 0.50f, 0.30f, 0.55f, 0.30f, 0.50f, 0.30f, 0.55f, 0.30f, 0.50f, 0.30f });
            s.percSecondary  = pat({ 0, 0, 0, 0, 0.45f, 0, 0, 0, 0, 0, 0, 0, 0.50f, 0, 0, 0 });
            s.bassSync       = pat({ 1.0f, 0, 0, 0.45f, 0, 0, 0, 0.65f, 0.40f, 0, 0.85f, 0, 0, 0, 0.50f, 0 });
            s.keysComp       = pat({ 0.95f, 0, 0, 0, 0, 0, 0.55f, 0, 0, 0, 0.60f, 0, 0, 0, 0.45f, 0 });
            s.guitarStrum    = pat({ 0.75f, 0, 0, 0.40f, 0, 0, 0.55f, 0, 0, 0, 0.60f, 0, 0, 0, 0.45f, 0 });
            s.guitarUpstroke = pat({ 0, 0, 0.30f, 0, 0, 0, 0.45f, 0, 0, 0, 0.40f, 0, 0, 0, 0.40f, 0 });
            s.hornsStab      = pat({ 0.45f, 0, 0, 0, 0, 0, 0.35f, 0, 0, 0, 0, 0, 0, 0, 0.55f, 0 });
            s.bassOctaveProb = 0.35f; s.bassWalkProb = 0.45f; s.keysSustainSteps = 6;
            s.guitarMuteProb = 0.30f; s.hornsFillDensity = 0.55f; s.drumsFillDensity = 0.45f;
            s.feelDrunkJitterMs = 8.5f;
        }

        { // 8: Motown Classic
            auto& s = styles_[8];
            s.name = "Motown Classic";
            s.family = GenreFamily::FunkSoulRnB;
            s.defaultTimeSig = { 4, 4 };
            s.defaultBpm = 118.0;
            s.defaultSwing = 0.53f;
            s.defaultPushPullMs = -1.0f;
            s.recommendedPatches = { 1, 0, 1, 0, 0, 0 };
            s.kick           = pat({ 1.0f, 0, 0, 0, 0.75f, 0, 0, 0, 1.0f, 0, 0.55f, 0, 0.75f, 0, 0, 0 });
            s.snare          = pat({ 0, 0, 0, 0, 1.0f, 0, 0, 0, 0, 0, 0, 0, 1.0f, 0, 0, 0 });
            s.ghostSnare     = pat({ 0, 0, 0, 0, 0, 0, 0, 0.25f, 0, 0, 0, 0, 0, 0, 0, 0.25f });
            s.hatClosed      = pat({ 0.85f, 0, 0.70f, 0, 0.85f, 0, 0.70f, 0, 0.85f, 0, 0.70f, 0, 0.85f, 0, 0.70f, 0 });
            s.hatOpen        = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.40f, 0 });
            s.ride           = pat({ 0.80f, 0, 0.65f, 0, 0.80f, 0, 0.65f, 0, 0.80f, 0, 0.65f, 0, 0.80f, 0, 0.65f, 0 });
            s.percPrimary    = pat({ 0.75f, 0, 0.65f, 0, 0.95f, 0, 0.65f, 0, 0.75f, 0, 0.65f, 0, 0.95f, 0, 0.65f, 0 });
            s.percSecondary  = pat({ 0, 0, 0, 0, 0.85f, 0, 0, 0, 0, 0, 0, 0, 0.85f, 0, 0, 0 });
            s.bassSync       = pat({ 1.0f, 0, 0.55f, 0.35f, 0.65f, 0, 0.70f, 0, 0.95f, 0, 0.75f, 0, 0.65f, 0, 0.70f, 0 });
            s.keysComp       = pat({ 0.80f, 0, 0, 0, 0.85f, 0, 0, 0, 0.80f, 0, 0, 0, 0.85f, 0, 0, 0 });
            s.guitarStrum    = pat({ 0, 0, 0, 0, 0.90f, 0, 0, 0, 0, 0, 0, 0, 0.90f, 0, 0.45f, 0 });
            s.guitarUpstroke = pat({ 0, 0, 0, 0, 0, 0, 0.40f, 0, 0, 0, 0, 0, 0, 0, 0.50f, 0 });
            s.hornsStab      = pat({ 0.80f, 0, 0, 0, 0, 0, 0.55f, 0, 0, 0, 0.65f, 0, 0, 0, 0.50f, 0 });
            s.bassOctaveProb = 0.25f; s.bassWalkProb = 0.70f; s.keysSustainSteps = 3;
            s.guitarMuteProb = 0.40f; s.hornsFillDensity = 0.65f; s.drumsFillDensity = 0.50f;
        }

        { // 9: Trap-Soul
            auto& s = styles_[9];
            s.name = "Trap-Soul";
            s.family = GenreFamily::FunkSoulRnB;
            s.defaultTimeSig = { 4, 4 };
            s.defaultBpm = 72.0;
            s.defaultSwing = 0.53f;
            s.defaultPushPullMs = 4.0f;
            s.recommendedPatches = { 5, 4, 0, 0, 3, 1 };
            s.kick           = pat({ 1.0f, 0, 0, 0, 0, 0, 0.55f, 0, 0, 0, 0.85f, 0, 0, 0.40f, 0, 0 });
            s.snare          = pat({ 0, 0, 0, 0, 0.90f, 0, 0, 0, 0, 0, 0, 0, 0.95f, 0, 0, 0 });
            s.ghostSnare     = pat({ 0, 0, 0, 0, 0, 0, 0, 0.30f, 0, 0, 0, 0, 0, 0, 0.35f, 0.30f });
            s.hatClosed      = pat({ 0.90f, 0.70f, 0.85f, 0.75f, 0.90f, 0.60f, 0.85f, 0.80f, 0.90f, 0.65f, 0.85f, 0.75f, 0.90f, 0.70f, 0.85f, 0.85f });
            s.hatOpen        = pat({ 0, 0, 0, 0, 0, 0, 0.40f, 0, 0, 0, 0, 0, 0, 0, 0.45f, 0 });
            s.ride           = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
            s.percPrimary    = pat({ 0, 0, 0, 0, 0, 0, 0.45f, 0, 0, 0, 0, 0, 0, 0, 0.50f, 0 });
            s.percSecondary  = pat({ 0, 0, 0, 0.35f, 0, 0, 0, 0, 0, 0, 0, 0.35f, 0, 0, 0, 0 });
            s.bassSync       = pat({ 1.0f, 0, 0, 0, 0, 0, 0.60f, 0, 0, 0, 0.85f, 0, 0, 0.40f, 0, 0 });
            s.keysComp       = pat({ 0.95f, 0, 0, 0, 0, 0, 0, 0, 0.70f, 0, 0, 0, 0, 0, 0, 0 });
            s.guitarStrum    = pat({ 0.65f, 0, 0, 0, 0, 0, 0.45f, 0, 0.55f, 0, 0, 0, 0, 0, 0, 0 });
            s.guitarUpstroke = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.35f, 0, 0, 0, 0.35f, 0 });
            s.hornsStab      = pat({ 0.50f, 0, 0, 0, 0, 0, 0, 0, 0.40f, 0, 0, 0, 0, 0, 0, 0 });
            s.bassOctaveProb = 0.25f; s.bassWalkProb = 0.15f; s.keysSustainSteps = 8;
            s.guitarMuteProb = 0.20f; s.hornsFillDensity = 0.35f; s.drumsFillDensity = 0.40f;
            s.feelDrunkJitterMs = 2.5f;
        }

        { // 10: Gospel Chops
            auto& s = styles_[10];
            s.name = "Gospel Chops";
            s.family = GenreFamily::FunkSoulRnB;
            s.defaultTimeSig = { 4, 4 };
            s.defaultBpm = 96.0;
            s.defaultSwing = 0.54f;
            s.defaultPushPullMs = 1.0f;
            s.recommendedPatches = { 3, 0, 3, 0, 0, 0 };
            s.kick           = pat({ 1.0f, 0, 0, 0.55f, 0, 0, 0.45f, 0.50f, 0.80f, 0, 0.70f, 0, 0, 0.45f, 0.50f, 0 });
            s.snare          = pat({ 0, 0, 0, 0, 1.0f, 0, 0, 0, 0, 0, 0, 0, 1.0f, 0, 0, 0 });
            s.ghostSnare     = pat({ 0, 0.35f, 0, 0.45f, 0, 0.30f, 0, 0.65f, 0, 0.65f, 0, 0.45f, 0, 0.35f, 0, 0.65f });
            s.hatClosed      = pat({ 0.90f, 0.55f, 0.80f, 0.55f, 0.90f, 0.55f, 0.80f, 0.55f, 0.90f, 0.55f, 0.80f, 0.55f, 0.90f, 0.55f, 0.60f, 0.55f });
            s.hatOpen        = pat({ 0, 0, 0, 0, 0, 0, 0.45f, 0, 0, 0, 0, 0, 0, 0, 0.60f, 0 });
            s.ride           = pat({ 0.85f, 0, 0.70f, 0, 0.85f, 0, 0.70f, 0, 0.85f, 0, 0.70f, 0, 0.85f, 0, 0.70f, 0 });
            s.percPrimary    = pat({ 0.60f, 0, 0.60f, 0, 0.85f, 0, 0.60f, 0, 0.60f, 0, 0.60f, 0, 0.85f, 0, 0.60f, 0 });
            s.percSecondary  = pat({ 0, 0, 0, 0, 0.75f, 0, 0, 0, 0, 0, 0, 0, 0.75f, 0, 0, 0 });
            s.bassSync       = pat({ 1.0f, 0, 0, 0.60f, 0, 0, 0.55f, 0.50f, 0.85f, 0, 0.75f, 0, 0, 0.45f, 0.60f, 0 });
            s.keysComp       = pat({ 0.95f, 0, 0, 0.50f, 0.65f, 0, 0.60f, 0, 0.85f, 0, 0.55f, 0, 0.65f, 0, 0.60f, 0 });
            s.guitarStrum    = pat({ 0.80f, 0, 0.45f, 0, 0.65f, 0, 0.55f, 0, 0.75f, 0, 0.50f, 0, 0.65f, 0, 0.55f, 0 });
            s.guitarUpstroke = pat({ 0, 0, 0.45f, 0, 0, 0, 0.50f, 0, 0, 0, 0.45f, 0, 0, 0, 0.50f, 0 });
            s.hornsStab      = pat({ 0.75f, 0, 0, 0.45f, 0, 0, 0.55f, 0, 0, 0, 0.60f, 0, 0, 0, 0.65f, 0 });
            s.bassOctaveProb = 0.45f; s.bassWalkProb = 0.60f; s.keysSustainSteps = 4;
            s.guitarMuteProb = 0.35f; s.hornsFillDensity = 0.65f; s.drumsFillDensity = 0.90f;
        }

        { // 11: Disco Funk
            auto& s = styles_[11];
            s.name = "Disco Funk";
            s.family = GenreFamily::FunkSoulRnB;
            s.defaultTimeSig = { 4, 4 };
            s.defaultBpm = 120.0;
            s.defaultSwing = 0.51f;
            s.defaultPushPullMs = -1.5f;
            s.recommendedPatches = { 0, 2, 4, 1, 0, 1 };
            s.kick           = pat({ 1.0f, 0, 0, 0, 0.95f, 0, 0, 0, 1.0f, 0, 0, 0, 0.95f, 0, 0, 0 });
            s.snare          = pat({ 0, 0, 0, 0, 1.0f, 0, 0, 0, 0, 0, 0, 0, 1.0f, 0, 0, 0 });
            s.ghostSnare     = pat({ 0, 0, 0, 0, 0, 0, 0, 0.25f, 0, 0, 0, 0, 0, 0, 0, 0.25f });
            s.hatClosed      = pat({ 0.85f, 0.45f, 0.15f, 0.45f, 0.85f, 0.45f, 0.15f, 0.45f, 0.85f, 0.45f, 0.15f, 0.45f, 0.85f, 0.45f, 0.15f, 0.45f });
            s.hatOpen        = pat({ 0, 0, 0.90f, 0, 0, 0, 0.90f, 0, 0, 0, 0.90f, 0, 0, 0, 0.90f, 0 });
            s.ride           = pat({ 0.80f, 0, 0.80f, 0, 0.80f, 0, 0.80f, 0, 0.80f, 0, 0.80f, 0, 0.80f, 0, 0.80f, 0 });
            s.percPrimary    = pat({ 0.70f, 0.45f, 0.70f, 0.45f, 0.85f, 0.45f, 0.70f, 0.45f, 0.70f, 0.45f, 0.70f, 0.45f, 0.85f, 0.45f, 0.70f, 0.45f });
            s.percSecondary  = pat({ 0, 0, 0.55f, 0, 0.75f, 0, 0.55f, 0, 0, 0, 0.55f, 0, 0.75f, 0, 0.55f, 0 });
            s.bassSync       = pat({ 1.0f, 0, 0.90f, 0, 0.85f, 0, 0.90f, 0, 1.0f, 0, 0.90f, 0, 0.85f, 0, 0.90f, 0 });
            s.keysComp       = pat({ 0.80f, 0, 0.45f, 0, 0.75f, 0, 0.50f, 0, 0.80f, 0, 0.45f, 0, 0.75f, 0, 0.50f, 0 });
            s.guitarStrum    = pat({ 0.90f, 0.55f, 0.80f, 0.55f, 0.90f, 0.55f, 0.80f, 0.55f, 0.90f, 0.55f, 0.80f, 0.55f, 0.90f, 0.55f, 0.80f, 0.55f });
            s.guitarUpstroke = pat({ 0, 0.55f, 0.75f, 0.55f, 0, 0.55f, 0.75f, 0.55f, 0, 0.55f, 0.75f, 0.55f, 0, 0.55f, 0.75f, 0.55f });
            s.hornsStab      = pat({ 0.75f, 0, 0, 0, 0, 0, 0.65f, 0, 0, 0, 0.70f, 0, 0, 0, 0.65f, 0 });
            s.bassOctaveProb = 0.85f; s.bassWalkProb = 0.25f; s.keysSustainSteps = 3;
            s.guitarMuteProb = 0.60f; s.hornsFillDensity = 0.65f; s.drumsFillDensity = 0.55f;
        }

        // =========================================================================
        // 12..17: JAZZ & LATIN
        // =========================================================================
        { // 12: Brush Jazz Trio
            auto& s = styles_[12];
            s.name = "Brush Jazz Trio";
            s.family = GenreFamily::JazzLatin;
            s.defaultTimeSig = { 4, 4 };
            s.defaultBpm = 112.0;
            s.defaultSwing = 0.66f;
            s.defaultPushPullMs = 3.0f;
            s.recommendedPatches = { 2, 3, 2, 0, 0, 1 };
            s.kick           = pat({ 0.55f, 0, 0, 0, 0.25f, 0, 0, 0, 0.50f, 0, 0, 0.30f, 0.25f, 0, 0, 0 });
            s.snare          = pat({ 0, 0, 0, 0, 0.55f, 0, 0, 0, 0, 0, 0, 0, 0.60f, 0, 0, 0 });
            s.ghostSnare     = pat({ 0.45f, 0, 0.35f, 0, 0.40f, 0, 0.45f, 0, 0.45f, 0, 0.35f, 0, 0.40f, 0, 0.45f, 0 });
            s.hatClosed      = pat({ 0, 0, 0, 0, 0.80f, 0, 0, 0, 0, 0, 0, 0, 0.80f, 0, 0, 0 });
            s.hatOpen        = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
            s.ride           = pat({ 0.90f, 0, 0, 0, 0.95f, 0, 0.70f, 0, 0.90f, 0, 0, 0, 0.95f, 0, 0.70f, 0 });
            s.percPrimary    = pat({ 0.30f, 0, 0, 0, 0.30f, 0, 0, 0, 0.30f, 0, 0, 0, 0.30f, 0, 0, 0 });
            s.percSecondary  = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
            s.bassSync       = pat({ 1.0f, 0, 0, 0, 0.95f, 0, 0, 0, 0.95f, 0, 0, 0, 0.95f, 0, 0.35f, 0 });
            s.keysComp       = pat({ 0.75f, 0, 0, 0, 0, 0, 0.65f, 0, 0, 0, 0.50f, 0, 0, 0, 0.55f, 0 });
            s.guitarStrum    = pat({ 0.70f, 0, 0, 0, 0.65f, 0, 0, 0, 0.70f, 0, 0, 0, 0.65f, 0, 0, 0 });
            s.guitarUpstroke = pat({ 0, 0, 0, 0, 0, 0, 0.35f, 0, 0, 0, 0, 0, 0, 0, 0.35f, 0 });
            s.hornsStab      = pat({ 0.45f, 0, 0, 0, 0, 0, 0.45f, 0, 0, 0, 0, 0, 0, 0, 0.40f, 0 });
            s.bassOctaveProb = 0.10f; s.bassWalkProb = 0.92f; s.keysSustainSteps = 4;
            s.guitarMuteProb = 0.25f; s.hornsFillDensity = 0.45f; s.drumsFillDensity = 0.45f;
            s.useBrushFeel = true; s.isSwingTriplet = true;
        }

        { // 13: Bebop Ride
            auto& s = styles_[13];
            s.name = "Bebop Ride";
            s.family = GenreFamily::JazzLatin;
            s.defaultTimeSig = { 4, 4 };
            s.defaultBpm = 178.0;
            s.defaultSwing = 0.63f;
            s.defaultPushPullMs = -2.0f;
            s.recommendedPatches = { 2, 3, 2, 0, 0, 0 };
            s.kick           = pat({ 0.45f, 0, 0, 0, 0.20f, 0, 0.35f, 0, 0.40f, 0, 0, 0, 0.25f, 0, 0.45f, 0 });
            s.snare          = pat({ 0, 0, 0.40f, 0, 0.45f, 0, 0.50f, 0, 0, 0, 0.45f, 0, 0.45f, 0, 0.55f, 0 });
            s.ghostSnare     = pat({ 0, 0, 0.30f, 0, 0, 0, 0.35f, 0, 0, 0, 0.30f, 0, 0, 0, 0.35f, 0 });
            s.hatClosed      = pat({ 0, 0, 0, 0, 0.90f, 0, 0, 0, 0, 0, 0, 0, 0.90f, 0, 0, 0 });
            s.hatOpen        = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
            s.ride           = pat({ 0.95f, 0, 0, 0, 1.0f, 0, 0.80f, 0, 0.95f, 0, 0, 0, 1.0f, 0, 0.80f, 0 });
            s.percPrimary    = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
            s.percSecondary  = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
            s.bassSync       = pat({ 1.0f, 0, 0, 0, 0.98f, 0, 0, 0, 0.98f, 0, 0, 0, 0.98f, 0, 0, 0 });
            s.keysComp       = pat({ 0.65f, 0, 0.40f, 0, 0, 0, 0.75f, 0, 0, 0, 0.55f, 0, 0, 0, 0.65f, 0 });
            s.guitarStrum    = pat({ 0.65f, 0, 0, 0, 0, 0, 0.60f, 0, 0, 0, 0.55f, 0, 0, 0, 0.55f, 0 });
            s.guitarUpstroke = pat({ 0, 0, 0, 0, 0, 0, 0.35f, 0, 0, 0, 0, 0, 0, 0, 0.35f, 0 });
            s.hornsStab      = pat({ 0.60f, 0, 0, 0, 0, 0, 0.65f, 0, 0, 0, 0, 0, 0, 0, 0.65f, 0 });
            s.bassOctaveProb = 0.08f; s.bassWalkProb = 0.98f; s.keysSustainSteps = 3;
            s.guitarMuteProb = 0.25f; s.hornsFillDensity = 0.65f; s.drumsFillDensity = 0.65f;
            s.isSwingTriplet = true;
        }

        { // 14: Bossa Nova
            auto& s = styles_[14];
            s.name = "Bossa Nova";
            s.family = GenreFamily::JazzLatin;
            s.defaultTimeSig = { 4, 4 };
            s.defaultBpm = 128.0;
            s.defaultSwing = 0.51f;
            s.defaultPushPullMs = 1.5f;
            s.recommendedPatches = { 1, 3, 0, 5, 1, 1 };
            s.kick           = pat({ 1.0f, 0, 0, 0.65f, 0.85f, 0, 0, 0.65f, 1.0f, 0, 0, 0.65f, 0.85f, 0, 0, 0.65f });
            s.snare          = pat({ 0.85f, 0, 0, 0.85f, 0, 0, 0.85f, 0, 0, 0, 0.85f, 0, 0, 0.85f, 0, 0 }); // Bossa clave sidestick!
            s.ghostSnare     = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
            s.hatClosed      = pat({ 0.80f, 0, 0.65f, 0, 0.80f, 0, 0.65f, 0, 0.80f, 0, 0.65f, 0, 0.80f, 0, 0.65f, 0 });
            s.hatOpen        = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
            s.ride           = pat({ 0.75f, 0, 0.60f, 0, 0.75f, 0, 0.60f, 0, 0.75f, 0, 0.60f, 0, 0.75f, 0, 0.60f, 0 });
            s.percPrimary    = pat({ 0.75f, 0.45f, 0.65f, 0.45f, 0.75f, 0.45f, 0.65f, 0.45f, 0.75f, 0.45f, 0.65f, 0.45f, 0.75f, 0.45f, 0.65f, 0.45f });
            s.percSecondary  = pat({ 0.60f, 0, 0, 0.55f, 0, 0, 0.60f, 0, 0, 0, 0.55f, 0, 0, 0.55f, 0, 0 });
            s.bassSync       = pat({ 1.0f, 0, 0, 0.65f, 0.75f, 0, 0, 0.65f, 1.0f, 0, 0, 0.65f, 0.75f, 0, 0, 0.65f });
            s.keysComp       = pat({ 0.85f, 0, 0, 0.75f, 0, 0, 0.80f, 0, 0, 0, 0.75f, 0, 0, 0.70f, 0, 0 });
            s.guitarStrum    = pat({ 0.90f, 0, 0, 0.80f, 0, 0, 0.85f, 0, 0, 0, 0.80f, 0, 0, 0.75f, 0, 0 });
            s.guitarUpstroke = pat({ 0, 0, 0, 0.60f, 0, 0, 0, 0, 0, 0, 0.55f, 0, 0, 0, 0, 0 });
            s.hornsStab      = pat({ 0.50f, 0, 0, 0, 0, 0, 0.45f, 0, 0, 0, 0, 0, 0, 0.45f, 0, 0 });
            s.bassOctaveProb = 0.10f; s.bassWalkProb = 0.30f; s.keysSustainSteps = 4;
            s.guitarMuteProb = 0.15f; s.hornsFillDensity = 0.35f; s.drumsFillDensity = 0.30f;
            s.useSideStick = true;
        }

        { // 15: Samba
            auto& s = styles_[15];
            s.name = "Samba";
            s.family = GenreFamily::JazzLatin;
            s.defaultTimeSig = { 4, 4 };
            s.defaultBpm = 108.0;
            s.defaultSwing = 0.52f;
            s.defaultPushPullMs = -1.5f;
            s.recommendedPatches = { 0, 0, 2, 5, 1, 0 };
            s.kick           = pat({ 1.0f, 0, 0, 0.75f, 0.90f, 0, 0, 0.75f, 1.0f, 0, 0, 0.75f, 0.90f, 0, 0, 0.75f });
            s.snare          = pat({ 0.75f, 0, 0.60f, 0.75f, 0, 0.60f, 0.80f, 0, 0.75f, 0, 0.60f, 0.75f, 0, 0.60f, 0.80f, 0 });
            s.ghostSnare     = pat({ 0, 0.55f, 0, 0, 0.55f, 0, 0, 0.55f, 0, 0.55f, 0, 0, 0.55f, 0, 0, 0.55f });
            s.hatClosed      = pat({ 0.85f, 0.60f, 0.75f, 0.60f, 0.85f, 0.60f, 0.75f, 0.60f, 0.85f, 0.60f, 0.75f, 0.60f, 0.85f, 0.60f, 0.75f, 0.60f });
            s.hatOpen        = pat({ 0, 0, 0, 0, 0, 0, 0.40f, 0, 0, 0, 0, 0, 0, 0, 0.45f, 0 });
            s.ride           = pat({ 0.80f, 0.50f, 0.70f, 0.50f, 0.80f, 0.50f, 0.70f, 0.50f, 0.80f, 0.50f, 0.70f, 0.50f, 0.80f, 0.50f, 0.70f, 0.50f });
            s.percPrimary    = pat({ 0.95f, 0.65f, 0.80f, 0.70f, 0.95f, 0.65f, 0.80f, 0.70f, 0.95f, 0.65f, 0.80f, 0.70f, 0.95f, 0.65f, 0.80f, 0.70f });
            s.percSecondary  = pat({ 0.85f, 0, 0, 0.80f, 0, 0, 0.85f, 0, 0, 0.75f, 0, 0.80f, 0, 0, 0.85f, 0 });
            s.bassSync       = pat({ 1.0f, 0, 0, 0.80f, 0.85f, 0, 0, 0.80f, 1.0f, 0, 0, 0.80f, 0.85f, 0, 0, 0.80f });
            s.keysComp       = pat({ 0.85f, 0, 0.60f, 0, 0, 0.65f, 0.75f, 0, 0.80f, 0, 0.60f, 0, 0, 0.65f, 0.75f, 0 });
            s.guitarStrum    = pat({ 0.90f, 0.50f, 0.75f, 0.65f, 0.80f, 0.50f, 0.75f, 0.65f, 0.90f, 0.50f, 0.75f, 0.65f, 0.80f, 0.50f, 0.75f, 0.65f });
            s.guitarUpstroke = pat({ 0, 0.45f, 0, 0.60f, 0, 0.45f, 0, 0.60f, 0, 0.45f, 0, 0.60f, 0, 0.45f, 0, 0.60f });
            s.hornsStab      = pat({ 0.75f, 0, 0, 0.65f, 0, 0, 0.60f, 0, 0, 0, 0.70f, 0, 0, 0, 0.65f, 0 });
            s.bassOctaveProb = 0.20f; s.bassWalkProb = 0.40f; s.keysSustainSteps = 3;
            s.guitarMuteProb = 0.25f; s.hornsFillDensity = 0.60f; s.drumsFillDensity = 0.65f;
            s.useSideStick = true;
        }

        { // 16: Acid Jazz
            auto& s = styles_[16];
            s.name = "Acid Jazz";
            s.family = GenreFamily::JazzLatin;
            s.defaultTimeSig = { 4, 4 };
            s.defaultBpm = 108.0;
            s.defaultSwing = 0.57f;
            s.defaultPushPullMs = 1.0f;
            s.recommendedPatches = { 1, 0, 0, 1, 1, 0 };
            s.kick           = pat({ 1.0f, 0, 0, 0.40f, 0, 0, 0.55f, 0, 0.75f, 0, 0.80f, 0, 0, 0, 0.40f, 0 });
            s.snare          = pat({ 0, 0, 0, 0, 1.0f, 0, 0, 0, 0, 0, 0, 0, 1.0f, 0, 0, 0 });
            s.ghostSnare     = pat({ 0, 0, 0, 0, 0, 0, 0, 0.60f, 0, 0.65f, 0, 0.30f, 0, 0, 0, 0.55f });
            s.hatClosed      = pat({ 0.90f, 0.55f, 0.80f, 0.55f, 0.85f, 0.50f, 0.80f, 0.55f, 0.90f, 0.55f, 0.80f, 0.55f, 0.85f, 0.50f, 0.60f, 0.55f });
            s.hatOpen        = pat({ 0, 0, 0, 0, 0, 0, 0.45f, 0, 0, 0, 0, 0, 0, 0, 0.60f, 0 });
            s.ride           = pat({ 0.80f, 0, 0.65f, 0, 0.80f, 0, 0.65f, 0, 0.80f, 0, 0.65f, 0, 0.80f, 0, 0.65f, 0 });
            s.percPrimary    = pat({ 0.70f, 0.45f, 0.65f, 0.45f, 0.70f, 0.45f, 0.65f, 0.45f, 0.70f, 0.45f, 0.65f, 0.45f, 0.70f, 0.45f, 0.65f, 0.45f });
            s.percSecondary  = pat({ 0, 0, 0.50f, 0, 0.60f, 0, 0, 0.45f, 0, 0, 0.50f, 0, 0.60f, 0, 0.50f, 0 });
            s.bassSync       = pat({ 1.0f, 0, 0.45f, 0.45f, 0, 0, 0.65f, 0, 0.80f, 0, 0.85f, 0, 0, 0, 0.55f, 0 });
            s.keysComp       = pat({ 0.90f, 0, 0, 0.55f, 0, 0, 0.70f, 0, 0, 0.50f, 0, 0, 0.65f, 0, 0.55f, 0 });
            s.guitarStrum    = pat({ 0.75f, 0.35f, 0.65f, 0.50f, 0.70f, 0.35f, 0.65f, 0.45f, 0.75f, 0.35f, 0.65f, 0.50f, 0.70f, 0.35f, 0.65f, 0.45f });
            s.guitarUpstroke = pat({ 0, 0.45f, 0.55f, 0.45f, 0, 0.40f, 0.55f, 0.40f, 0, 0.45f, 0.55f, 0.45f, 0, 0.40f, 0.55f, 0.40f });
            s.hornsStab      = pat({ 0.80f, 0, 0, 0.50f, 0, 0, 0.60f, 0, 0, 0, 0.65f, 0, 0, 0, 0.55f, 0 });
            s.bassOctaveProb = 0.45f; s.bassWalkProb = 0.55f; s.keysSustainSteps = 4;
            s.guitarMuteProb = 0.55f; s.hornsFillDensity = 0.70f; s.drumsFillDensity = 0.55f;
        }

        { // 17: 70s Jazz-Fusion
            auto& s = styles_[17];
            s.name = "70s Jazz-Fusion";
            s.family = GenreFamily::JazzLatin;
            s.defaultTimeSig = { 4, 4 };
            s.defaultBpm = 116.0;
            s.defaultSwing = 0.53f;
            s.defaultPushPullMs = -1.0f;
            s.recommendedPatches = { 0, 0, 0, 0, 1, 3 };
            s.kick           = pat({ 1.0f, 0, 0, 0.60f, 0, 0, 0.50f, 0.45f, 0, 0, 0.85f, 0, 0, 0.50f, 0, 0.40f });
            s.snare          = pat({ 0, 0, 0, 0, 0.95f, 0, 0, 0, 0, 0, 0, 0.40f, 0.95f, 0, 0, 0 });
            s.ghostSnare     = pat({ 0, 0.40f, 0, 0, 0, 0.35f, 0, 0.60f, 0, 0.65f, 0, 0, 0, 0.40f, 0, 0.60f });
            s.hatClosed      = pat({ 0.90f, 0.65f, 0.85f, 0.65f, 0.90f, 0.65f, 0.85f, 0.65f, 0.90f, 0.65f, 0.85f, 0.65f, 0.90f, 0.65f, 0.70f, 0.65f });
            s.hatOpen        = pat({ 0, 0, 0, 0, 0, 0, 0.40f, 0, 0, 0, 0, 0, 0, 0, 0.50f, 0 });
            s.ride           = pat({ 0.90f, 0.55f, 0.80f, 0.55f, 0.90f, 0.55f, 0.80f, 0.55f, 0.90f, 0.55f, 0.80f, 0.55f, 0.90f, 0.55f, 0.80f, 0.55f });
            s.percPrimary    = pat({ 0.60f, 0.40f, 0.55f, 0.40f, 0.60f, 0.40f, 0.55f, 0.40f, 0.60f, 0.40f, 0.55f, 0.40f, 0.60f, 0.40f, 0.55f, 0.40f });
            s.percSecondary  = pat({ 0, 0, 0.45f, 0, 0, 0.45f, 0, 0, 0.45f, 0, 0, 0.45f, 0, 0, 0.50f, 0 });
            s.bassSync       = pat({ 1.0f, 0, 0.45f, 0.65f, 0, 0, 0.65f, 0.45f, 0.50f, 0, 0.90f, 0, 0, 0.55f, 0.55f, 0 });
            s.keysComp       = pat({ 0.90f, 0, 0, 0.65f, 0, 0, 0.70f, 0, 0, 0, 0.75f, 0, 0, 0.55f, 0, 0 });
            s.guitarStrum    = pat({ 0.80f, 0, 0.50f, 0.55f, 0, 0, 0.65f, 0, 0.70f, 0, 0.60f, 0, 0, 0.50f, 0.55f, 0 });
            s.guitarUpstroke = pat({ 0, 0, 0.45f, 0, 0, 0, 0.50f, 0, 0, 0, 0.45f, 0, 0, 0, 0.50f, 0 });
            s.hornsStab      = pat({ 0.75f, 0, 0, 0.60f, 0, 0, 0.55f, 0, 0, 0, 0.70f, 0, 0, 0, 0.60f, 0 });
            s.bassOctaveProb = 0.40f; s.bassWalkProb = 0.65f; s.keysSustainSteps = 4;
            s.guitarMuteProb = 0.40f; s.hornsFillDensity = 0.65f; s.drumsFillDensity = 0.75f;
        }

        // =========================================================================
        // 18..23: ELECTRONIC & HIP-HOP
        // =========================================================================
        { // 18: 90s Boom-Bap
            auto& s = styles_[18];
            s.name = "90s Boom-Bap";
            s.family = GenreFamily::ElectronicHipHop;
            s.defaultTimeSig = { 4, 4 };
            s.defaultBpm = 90.0;
            s.defaultSwing = 0.58f;
            s.defaultPushPullMs = 4.0f;
            s.recommendedPatches = { 4, 0, 0, 0, 3, 2 };
            s.kick           = pat({ 1.0f, 0, 0, 0, 0, 0, 0, 0.75f, 0.30f, 0, 0.90f, 0, 0, 0, 0, 0.35f });
            s.snare          = pat({ 0, 0, 0, 0, 1.0f, 0, 0, 0, 0, 0, 0, 0, 1.0f, 0, 0, 0 });
            s.ghostSnare     = pat({ 0, 0, 0, 0, 0, 0, 0, 0.35f, 0, 0.45f, 0, 0, 0, 0, 0, 0.40f });
            s.hatClosed      = pat({ 0.90f, 0, 0.75f, 0.35f, 0.85f, 0, 0.75f, 0.35f, 0.90f, 0, 0.75f, 0.35f, 0.85f, 0, 0.60f, 0.35f });
            s.hatOpen        = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.55f, 0 });
            s.ride           = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
            s.percPrimary    = pat({ 0.45f, 0, 0.40f, 0, 0.45f, 0, 0.40f, 0, 0.45f, 0, 0.40f, 0, 0.45f, 0, 0.40f, 0 });
            s.percSecondary  = pat({ 0, 0, 0, 0, 0.50f, 0, 0, 0, 0, 0, 0, 0, 0.50f, 0, 0, 0 });
            s.bassSync       = pat({ 1.0f, 0, 0, 0, 0, 0, 0, 0.75f, 0, 0, 0.90f, 0, 0, 0, 0.45f, 0 });
            s.keysComp       = pat({ 0.90f, 0, 0, 0, 0, 0, 0.50f, 0, 0, 0, 0.65f, 0, 0, 0, 0, 0 });
            s.guitarStrum    = pat({ 0.70f, 0, 0, 0, 0, 0, 0.45f, 0, 0, 0, 0.55f, 0, 0, 0, 0, 0 });
            s.guitarUpstroke = pat({ 0, 0, 0, 0, 0, 0, 0.35f, 0, 0, 0, 0, 0, 0, 0, 0.35f, 0 });
            s.hornsStab      = pat({ 0.65f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.55f, 0, 0, 0, 0, 0 });
            s.bassOctaveProb = 0.25f; s.bassWalkProb = 0.30f; s.keysSustainSteps = 6;
            s.guitarMuteProb = 0.35f; s.hornsFillDensity = 0.45f; s.drumsFillDensity = 0.45f;
            s.feelDrunkJitterMs = 3.5f;
        }

        { // 19: Lo-Fi Chillhop
            auto& s = styles_[19];
            s.name = "Lo-Fi Chillhop";
            s.family = GenreFamily::ElectronicHipHop;
            s.defaultTimeSig = { 4, 4 };
            s.defaultBpm = 80.0;
            s.defaultSwing = 0.59f;
            s.defaultPushPullMs = 6.5f;
            s.recommendedPatches = { 4, 4, 0, 0, 3, 2 };
            s.kick           = pat({ 1.0f, 0, 0, 0, 0, 0, 0, 0.55f, 0, 0, 0.80f, 0, 0, 0, 0, 0 });
            s.snare          = pat({ 0, 0, 0, 0, 0.90f, 0, 0, 0, 0, 0, 0, 0, 0.92f, 0, 0, 0 });
            s.ghostSnare     = pat({ 0, 0, 0, 0, 0, 0, 0, 0.30f, 0, 0.35f, 0, 0, 0, 0, 0, 0.30f });
            s.hatClosed      = pat({ 0.80f, 0, 0.65f, 0, 0.75f, 0, 0.65f, 0, 0.80f, 0, 0.65f, 0, 0.75f, 0, 0.65f, 0 });
            s.hatOpen        = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.35f, 0 });
            s.ride           = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
            s.percPrimary    = pat({ 0.45f, 0, 0.45f, 0, 0.45f, 0, 0.45f, 0, 0.45f, 0, 0.45f, 0, 0.45f, 0, 0.45f, 0 });
            s.percSecondary  = pat({ 0, 0, 0, 0, 0.35f, 0, 0, 0, 0, 0, 0, 0, 0.35f, 0, 0, 0 });
            s.bassSync       = pat({ 1.0f, 0, 0, 0, 0, 0, 0, 0.55f, 0, 0, 0.80f, 0, 0, 0, 0, 0 });
            s.keysComp       = pat({ 0.95f, 0, 0, 0, 0, 0, 0.45f, 0, 0.60f, 0, 0, 0, 0, 0, 0.40f, 0 });
            s.guitarStrum    = pat({ 0.70f, 0, 0, 0, 0, 0, 0.50f, 0, 0, 0, 0.55f, 0, 0, 0, 0, 0 });
            s.guitarUpstroke = pat({ 0, 0, 0.25f, 0, 0, 0, 0.30f, 0, 0, 0, 0.25f, 0, 0, 0, 0.30f, 0 });
            s.hornsStab      = pat({ 0.40f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.35f, 0, 0, 0, 0, 0 });
            s.bassOctaveProb = 0.15f; s.bassWalkProb = 0.25f; s.keysSustainSteps = 8;
            s.guitarMuteProb = 0.15f; s.hornsFillDensity = 0.30f; s.drumsFillDensity = 0.30f;
            s.feelDrunkJitterMs = 6.0f;
        }

        { // 20: Deep House
            auto& s = styles_[20];
            s.name = "Deep House";
            s.family = GenreFamily::ElectronicHipHop;
            s.defaultTimeSig = { 4, 4 };
            s.defaultBpm = 122.0;
            s.defaultSwing = 0.54f;
            s.defaultPushPullMs = 0.0f;
            s.recommendedPatches = { 5, 4, 0, 0, 3, 3 };
            s.kick           = pat({ 1.0f, 0, 0, 0, 1.0f, 0, 0, 0, 1.0f, 0, 0, 0, 1.0f, 0, 0, 0 });
            s.snare          = pat({ 0, 0, 0, 0, 0.95f, 0, 0, 0, 0, 0, 0, 0, 0.95f, 0, 0, 0 });
            s.ghostSnare     = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
            s.hatClosed      = pat({ 0.45f, 0.55f, 0.20f, 0.65f, 0.45f, 0.55f, 0.20f, 0.65f, 0.45f, 0.55f, 0.20f, 0.65f, 0.45f, 0.55f, 0.20f, 0.65f });
            s.hatOpen        = pat({ 0, 0, 0.95f, 0, 0, 0, 0.95f, 0, 0, 0, 0.95f, 0, 0, 0, 0.95f, 0 });
            s.ride           = pat({ 0, 0, 0.70f, 0, 0, 0, 0.70f, 0, 0, 0, 0.70f, 0, 0, 0, 0.70f, 0 });
            s.percPrimary    = pat({ 0.55f, 0.50f, 0.65f, 0.50f, 0.55f, 0.50f, 0.65f, 0.50f, 0.55f, 0.50f, 0.65f, 0.50f, 0.55f, 0.50f, 0.65f, 0.50f });
            s.percSecondary  = pat({ 0, 0, 0, 0.55f, 0, 0, 0.45f, 0, 0, 0, 0, 0.55f, 0, 0, 0.50f, 0 });
            s.bassSync       = pat({ 0.90f, 0, 0, 0.75f, 0, 0, 0.80f, 0, 0, 0.65f, 0, 0, 0.70f, 0, 0.75f, 0 });
            s.keysComp       = pat({ 0.90f, 0, 0, 0.75f, 0, 0, 0.80f, 0, 0, 0, 0.65f, 0, 0, 0, 0.70f, 0 });
            s.guitarStrum    = pat({ 0, 0, 0.60f, 0, 0, 0, 0.60f, 0, 0, 0, 0.60f, 0, 0, 0, 0.60f, 0 });
            s.guitarUpstroke = pat({ 0, 0, 0.50f, 0, 0, 0, 0.50f, 0, 0, 0, 0.50f, 0, 0, 0, 0.50f, 0 });
            s.hornsStab      = pat({ 0.65f, 0, 0, 0.55f, 0, 0, 0, 0, 0, 0, 0.55f, 0, 0, 0, 0, 0 });
            s.bassOctaveProb = 0.50f; s.bassWalkProb = 0.20f; s.keysSustainSteps = 4;
            s.guitarMuteProb = 0.55f; s.hornsFillDensity = 0.40f; s.drumsFillDensity = 0.45f;
        }

        { // 21: UK Garage / 2-Step
            auto& s = styles_[21];
            s.name = "UK Garage / 2-Step";
            s.family = GenreFamily::ElectronicHipHop;
            s.defaultTimeSig = { 4, 4 };
            s.defaultBpm = 132.0;
            s.defaultSwing = 0.62f;
            s.defaultPushPullMs = 0.5f;
            s.recommendedPatches = { 5, 4, 0, 1, 3, 3 };
            s.kick           = pat({ 1.0f, 0, 0, 0, 0, 0, 0, 0.75f, 0, 0, 0.85f, 0, 0, 0, 0, 0.45f });
            s.snare          = pat({ 0, 0, 0, 0, 0.95f, 0, 0, 0, 0, 0, 0, 0, 0.95f, 0, 0, 0 });
            s.ghostSnare     = pat({ 0, 0, 0, 0.55f, 0, 0, 0.40f, 0, 0, 0.60f, 0, 0.45f, 0, 0, 0.40f, 0 });
            s.hatClosed      = pat({ 0.70f, 0, 0.90f, 0.65f, 0.65f, 0, 0.90f, 0.60f, 0.70f, 0, 0.90f, 0.65f, 0.65f, 0, 0.90f, 0.60f });
            s.hatOpen        = pat({ 0, 0, 0.65f, 0, 0, 0, 0.50f, 0, 0, 0, 0.65f, 0, 0, 0, 0.60f, 0 });
            s.ride           = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
            s.percPrimary    = pat({ 0.55f, 0, 0.70f, 0.60f, 0.55f, 0, 0.70f, 0.60f, 0.55f, 0, 0.70f, 0.60f, 0.55f, 0, 0.70f, 0.60f });
            s.percSecondary  = pat({ 0, 0, 0, 0.55f, 0, 0, 0, 0.50f, 0, 0, 0, 0.55f, 0, 0, 0, 0.50f });
            s.bassSync       = pat({ 1.0f, 0, 0, 0.70f, 0, 0, 0, 0.80f, 0, 0, 0.85f, 0, 0, 0, 0.65f, 0 });
            s.keysComp       = pat({ 0.85f, 0, 0, 0.75f, 0, 0, 0.65f, 0, 0, 0.60f, 0, 0, 0.55f, 0, 0.70f, 0 });
            s.guitarStrum    = pat({ 0.60f, 0, 0, 0.55f, 0, 0, 0.55f, 0, 0, 0, 0.60f, 0, 0, 0, 0.55f, 0 });
            s.guitarUpstroke = pat({ 0, 0, 0, 0.50f, 0, 0, 0, 0, 0, 0, 0.50f, 0, 0, 0, 0, 0 });
            s.hornsStab      = pat({ 0.60f, 0, 0, 0.55f, 0, 0, 0, 0, 0, 0, 0.55f, 0, 0, 0, 0.45f, 0 });
            s.bassOctaveProb = 0.50f; s.bassWalkProb = 0.25f; s.keysSustainSteps = 3;
            s.guitarMuteProb = 0.55f; s.hornsFillDensity = 0.45f; s.drumsFillDensity = 0.55f;
        }

        { // 22: Drum & Bass
            auto& s = styles_[22];
            s.name = "Drum & Bass";
            s.family = GenreFamily::ElectronicHipHop;
            s.defaultTimeSig = { 4, 4 };
            s.defaultBpm = 174.0;
            s.defaultSwing = 0.50f;
            s.defaultPushPullMs = -2.0f;
            s.recommendedPatches = { 3, 4, 5, 0, 3, 3 };
            s.kick           = pat({ 1.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.95f, 0, 0, 0, 0, 0 });
            s.snare          = pat({ 0, 0, 0, 0, 1.0f, 0, 0, 0, 0, 0, 0, 0, 1.0f, 0, 0, 0 });
            s.ghostSnare     = pat({ 0, 0, 0, 0, 0, 0, 0, 0.65f, 0, 0.70f, 0, 0, 0, 0, 0, 0.65f });
            s.hatClosed      = pat({ 0.90f, 0.55f, 0.85f, 0.55f, 0.90f, 0.55f, 0.85f, 0.55f, 0.90f, 0.55f, 0.85f, 0.55f, 0.90f, 0.55f, 0.85f, 0.55f });
            s.hatOpen        = pat({ 0, 0, 0, 0, 0, 0, 0.45f, 0, 0, 0, 0, 0, 0, 0, 0.45f, 0 });
            s.ride           = pat({ 0.85f, 0, 0.75f, 0, 0.85f, 0, 0.75f, 0, 0.85f, 0, 0.75f, 0, 0.85f, 0, 0.75f, 0 });
            s.percPrimary    = pat({ 0.70f, 0.50f, 0.70f, 0.50f, 0.70f, 0.50f, 0.70f, 0.50f, 0.70f, 0.50f, 0.70f, 0.50f, 0.70f, 0.50f, 0.70f, 0.50f });
            s.percSecondary  = pat({ 0, 0, 0.50f, 0, 0, 0, 0.50f, 0, 0, 0, 0.50f, 0, 0, 0, 0.50f, 0 });
            s.bassSync       = pat({ 1.0f, 0, 0, 0, 0, 0, 0.55f, 0, 0, 0, 0.90f, 0, 0, 0, 0.50f, 0 });
            s.keysComp       = pat({ 0.90f, 0, 0, 0, 0, 0, 0, 0, 0.65f, 0, 0, 0, 0, 0, 0, 0 });
            s.guitarStrum    = pat({ 0.60f, 0, 0, 0, 0, 0, 0.45f, 0, 0, 0, 0.55f, 0, 0, 0, 0, 0 });
            s.guitarUpstroke = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
            s.hornsStab      = pat({ 0.65f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.60f, 0, 0, 0, 0, 0 });
            s.bassOctaveProb = 0.30f; s.bassWalkProb = 0.25f; s.keysSustainSteps = 8;
            s.guitarMuteProb = 0.30f; s.hornsFillDensity = 0.40f; s.drumsFillDensity = 0.75f;
        }

        { // 23: Synthwave
            auto& s = styles_[23];
            s.name = "Synthwave";
            s.family = GenreFamily::ElectronicHipHop;
            s.defaultTimeSig = { 4, 4 };
            s.defaultBpm = 108.0;
            s.defaultSwing = 0.50f;
            s.defaultPushPullMs = 0.0f;
            s.recommendedPatches = { 5, 5, 5, 0, 3, 3 };
            s.kick           = pat({ 1.0f, 0, 0, 0, 0.85f, 0, 0, 0, 1.0f, 0, 0, 0, 0.85f, 0, 0, 0 });
            s.snare          = pat({ 0, 0, 0, 0, 1.0f, 0, 0, 0, 0, 0, 0, 0, 1.0f, 0, 0, 0 });
            s.ghostSnare     = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
            s.hatClosed      = pat({ 0.85f, 0.55f, 0.80f, 0.55f, 0.85f, 0.55f, 0.80f, 0.55f, 0.85f, 0.55f, 0.80f, 0.55f, 0.85f, 0.55f, 0.80f, 0.55f });
            s.hatOpen        = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.55f, 0 });
            s.ride           = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
            s.percPrimary    = pat({ 0, 0, 0, 0, 0.65f, 0, 0, 0, 0, 0, 0, 0, 0.65f, 0, 0, 0 });
            s.percSecondary  = pat({ 0.45f, 0, 0.45f, 0, 0.45f, 0, 0.45f, 0, 0.45f, 0, 0.45f, 0, 0.45f, 0, 0.45f, 0 });
            s.bassSync       = pat({ 1.0f, 0.85f, 0.95f, 0.85f, 0.95f, 0.85f, 0.95f, 0.85f, 1.0f, 0.85f, 0.95f, 0.85f, 0.95f, 0.85f, 0.95f, 0.85f });
            s.keysComp       = pat({ 0.95f, 0, 0, 0, 0, 0, 0, 0, 0.85f, 0, 0, 0, 0, 0, 0, 0 });
            s.guitarStrum    = pat({ 0.75f, 0, 0.60f, 0, 0.75f, 0, 0.60f, 0, 0.75f, 0, 0.60f, 0, 0.75f, 0, 0.60f, 0 });
            s.guitarUpstroke = pat({ 0, 0, 0.40f, 0, 0, 0, 0.40f, 0, 0, 0, 0.40f, 0, 0, 0, 0.40f, 0 });
            s.hornsStab      = pat({ 0.80f, 0, 0, 0, 0, 0, 0.50f, 0, 0.70f, 0, 0, 0, 0, 0, 0.50f, 0 });
            s.bassOctaveProb = 0.75f; s.bassWalkProb = 0.15f; s.keysSustainSteps = 8;
            s.guitarMuteProb = 0.35f; s.hornsFillDensity = 0.45f; s.drumsFillDensity = 0.60f;
        }

        // =========================================================================
        // 24..29: ROOTS & WORLD
        // =========================================================================
        { // 24: Singer-Songwriter Acoustic
            auto& s = styles_[24];
            s.name = "Singer-Songwriter Acoustic";
            s.family = GenreFamily::RootsWorld;
            s.defaultTimeSig = { 4, 4 };
            s.defaultBpm = 98.0;
            s.defaultSwing = 0.52f;
            s.defaultPushPullMs = 2.0f;
            s.recommendedPatches = { 0, 3, 2, 2, 0, 1 };
            s.kick           = pat({ 1.0f, 0, 0, 0, 0, 0, 0.65f, 0, 0.70f, 0, 0.50f, 0, 0, 0, 0, 0 });
            s.snare          = pat({ 0, 0, 0, 0, 0.85f, 0, 0, 0, 0, 0, 0, 0, 0.88f, 0, 0, 0 });
            s.ghostSnare     = pat({ 0, 0, 0, 0, 0, 0, 0, 0.20f, 0, 0, 0, 0, 0, 0, 0, 0.20f });
            s.hatClosed      = pat({ 0.75f, 0, 0.60f, 0, 0.75f, 0, 0.60f, 0, 0.75f, 0, 0.60f, 0, 0.75f, 0, 0.60f, 0 });
            s.hatOpen        = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.30f, 0 });
            s.ride           = pat({ 0.65f, 0, 0.50f, 0, 0.65f, 0, 0.50f, 0, 0.65f, 0, 0.50f, 0, 0.65f, 0, 0.50f, 0 });
            s.percPrimary    = pat({ 0.70f, 0.35f, 0.60f, 0.35f, 0.70f, 0.35f, 0.60f, 0.35f, 0.70f, 0.35f, 0.60f, 0.35f, 0.70f, 0.35f, 0.60f, 0.35f });
            s.percSecondary  = pat({ 0, 0, 0, 0, 0.65f, 0, 0, 0, 0, 0, 0, 0, 0.65f, 0, 0, 0 });
            s.bassSync       = pat({ 1.0f, 0, 0, 0, 0, 0, 0.70f, 0, 0.80f, 0, 0, 0, 0, 0, 0.50f, 0 });
            s.keysComp       = pat({ 0.85f, 0, 0, 0, 0.55f, 0, 0, 0, 0.75f, 0, 0, 0, 0.55f, 0, 0, 0 });
            s.guitarStrum    = pat({ 1.0f, 0, 0, 0, 0.85f, 0, 0.80f, 0, 0, 0, 0.80f, 0, 0.85f, 0, 0.75f, 0 });
            s.guitarUpstroke = pat({ 0, 0, 0, 0, 0, 0, 0.80f, 0, 0, 0, 0.80f, 0, 0, 0, 0.75f, 0 });
            s.hornsStab      = pat({ 0.50f, 0, 0, 0, 0, 0, 0, 0, 0.40f, 0, 0, 0, 0, 0, 0, 0 });
            s.bassOctaveProb = 0.15f; s.bassWalkProb = 0.35f; s.keysSustainSteps = 6;
            s.guitarMuteProb = 0.10f; s.hornsFillDensity = 0.25f; s.drumsFillDensity = 0.35f;
            s.useSideStick = true;
        }

        { // 25: Americana Waltz (3/4 -> 12 steps)
            auto& s = styles_[25];
            s.name = "Americana Waltz (3/4)";
            s.family = GenreFamily::RootsWorld;
            s.defaultTimeSig = { 3, 4 };
            s.defaultBpm = 108.0;
            s.defaultSwing = 0.52f;
            s.defaultPushPullMs = 2.0f;
            s.recommendedPatches = { 2, 3, 2, 2, 0, 1 };
            s.kick           = pat({ 1.0f, 0, 0, 0, 0, 0, 0, 0, 0.35f, 0, 0, 0 });
            s.snare          = pat({ 0, 0, 0, 0, 0.85f, 0, 0, 0, 0.80f, 0, 0, 0 });
            s.ghostSnare     = pat({ 0, 0, 0.25f, 0, 0, 0, 0.25f, 0, 0, 0, 0.25f, 0 });
            s.hatClosed      = pat({ 0.80f, 0, 0.55f, 0, 0.75f, 0, 0.55f, 0, 0.75f, 0, 0.55f, 0 });
            s.hatOpen        = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.30f, 0 });
            s.ride           = pat({ 0.80f, 0, 0.50f, 0, 0.75f, 0, 0.50f, 0, 0.75f, 0, 0.50f, 0 });
            s.percPrimary    = pat({ 0.70f, 0, 0, 0, 0.65f, 0, 0, 0, 0.65f, 0, 0, 0 });
            s.percSecondary  = pat({ 0, 0, 0, 0, 0.60f, 0, 0, 0, 0.60f, 0, 0, 0 });
            s.bassSync       = pat({ 1.0f, 0, 0, 0, 0.45f, 0, 0, 0, 0.65f, 0, 0, 0 });
            s.keysComp       = pat({ 0.90f, 0, 0, 0, 0.75f, 0, 0, 0, 0.75f, 0, 0, 0 });
            s.guitarStrum    = pat({ 1.0f, 0, 0, 0, 0.85f, 0, 0.55f, 0, 0.85f, 0, 0.55f, 0 });
            s.guitarUpstroke = pat({ 0, 0, 0, 0, 0, 0, 0.55f, 0, 0, 0, 0.55f, 0 });
            s.hornsStab      = pat({ 0.65f, 0, 0, 0, 0, 0, 0, 0, 0.40f, 0, 0, 0 });
            s.bassOctaveProb = 0.15f; s.bassWalkProb = 0.50f; s.keysSustainSteps = 4;
            s.guitarMuteProb = 0.05f; s.hornsFillDensity = 0.30f; s.drumsFillDensity = 0.35f;
            s.useBrushFeel = true;
        }

        { // 26: Country Train Beat
            auto& s = styles_[26];
            s.name = "Country Train Beat";
            s.family = GenreFamily::RootsWorld;
            s.defaultTimeSig = { 4, 4 };
            s.defaultBpm = 134.0;
            s.defaultSwing = 0.53f;
            s.defaultPushPullMs = -2.0f;
            s.recommendedPatches = { 0, 3, 2, 2, 0, 1 };
            s.kick           = pat({ 1.0f, 0, 0, 0, 0, 0, 0, 0, 0.95f, 0, 0, 0, 0, 0, 0, 0 });
            s.snare          = pat({ 0, 0, 0, 0, 1.0f, 0, 0, 0, 0, 0, 0, 0, 1.0f, 0, 0, 0 });
            s.ghostSnare     = pat({ 0.65f, 0.55f, 0.65f, 0.55f, 0, 0.55f, 0.65f, 0.55f, 0.65f, 0.55f, 0.65f, 0.55f, 0, 0.55f, 0.65f, 0.55f });
            s.hatClosed      = pat({ 0.75f, 0, 0.65f, 0, 0.85f, 0, 0.65f, 0, 0.75f, 0, 0.65f, 0, 0.85f, 0, 0.65f, 0 });
            s.hatOpen        = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
            s.ride           = pat({ 0.75f, 0, 0.60f, 0, 0.80f, 0, 0.60f, 0, 0.75f, 0, 0.60f, 0, 0.80f, 0, 0.60f, 0 });
            s.percPrimary    = pat({ 0.65f, 0, 0.55f, 0, 0.75f, 0, 0.55f, 0, 0.65f, 0, 0.55f, 0, 0.75f, 0, 0.55f, 0 });
            s.percSecondary  = pat({ 0, 0, 0, 0, 0.65f, 0, 0, 0, 0, 0, 0, 0, 0.65f, 0, 0, 0 });
            s.bassSync       = pat({ 1.0f, 0, 0, 0, 0.45f, 0, 0, 0, 0.95f, 0, 0, 0, 0.50f, 0, 0, 0 });
            s.keysComp       = pat({ 0.80f, 0, 0, 0, 0.85f, 0, 0, 0, 0.80f, 0, 0, 0, 0.85f, 0, 0, 0 });
            s.guitarStrum    = pat({ 0.95f, 0, 0.70f, 0, 0.90f, 0, 0.70f, 0, 0.95f, 0, 0.70f, 0, 0.90f, 0, 0.70f, 0 });
            s.guitarUpstroke = pat({ 0, 0, 0.65f, 0, 0, 0, 0.65f, 0, 0, 0, 0.65f, 0, 0, 0, 0.65f, 0 });
            s.hornsStab      = pat({ 0.45f, 0, 0, 0, 0, 0, 0, 0, 0.45f, 0, 0, 0, 0, 0, 0, 0 });
            s.bassOctaveProb = 0.10f; s.bassWalkProb = 0.65f; s.keysSustainSteps = 3;
            s.guitarMuteProb = 0.20f; s.hornsFillDensity = 0.25f; s.drumsFillDensity = 0.50f;
            s.useBrushFeel = true;
        }

        { // 27: 12-Bar Blues Shuffle (12/8 -> 24 steps)
            auto& s = styles_[27];
            s.name = "12-Bar Blues Shuffle (12/8)";
            s.family = GenreFamily::RootsWorld;
            s.defaultTimeSig = { 12, 8 };
            s.defaultBpm = 92.0;
            s.defaultSwing = 0.50f; // Triplet grid is native in 12/8!
            s.defaultPushPullMs = 4.0f;
            s.recommendedPatches = { 1, 0, 3, 0, 0, 0 };
            // 12/8 has 24 16th steps: beats at 0, 6, 12, 18; shuffle upbeats at 4, 10, 16, 22
            s.kick           = pat({ 1.0f,0,0,0,0.45f,0, 0,0,0,0,0,0, 0.90f,0,0,0,0.50f,0, 0,0,0,0,0.35f,0 });
            s.snare          = pat({ 0,0,0,0,0,0, 1.0f,0,0,0,0,0, 0,0,0,0,0,0, 1.0f,0,0,0,0,0 });
            s.ghostSnare     = pat({ 0,0,0,0,0.30f,0, 0,0,0,0,0.40f,0, 0,0,0,0,0.30f,0, 0,0,0,0,0.45f,0 });
            s.hatClosed      = pat({ 0.90f,0,0,0,0.75f,0, 0.90f,0,0,0,0.75f,0, 0.90f,0,0,0,0.75f,0, 0.90f,0,0,0,0.75f,0 });
            s.hatOpen        = pat({ 0,0,0,0,0,0, 0,0,0,0,0,0, 0,0,0,0,0,0, 0,0,0,0,0.45f,0 });
            s.ride           = pat({ 0.90f,0,0,0,0.75f,0, 0.90f,0,0,0,0.75f,0, 0.90f,0,0,0,0.75f,0, 0.90f,0,0,0,0.75f,0 });
            s.percPrimary    = pat({ 0,0,0,0,0,0, 0.70f,0,0,0,0,0, 0,0,0,0,0,0, 0.70f,0,0,0,0,0 });
            s.percSecondary  = pat({ 0,0,0,0,0,0, 0,0,0,0,0,0, 0,0,0,0,0,0, 0,0,0,0,0,0 });
            s.bassSync       = pat({ 1.0f,0,0,0,0.65f,0, 0.90f,0,0,0,0.65f,0, 0.95f,0,0,0,0.65f,0, 0.90f,0,0,0,0.65f,0 });
            s.keysComp       = pat({ 0.90f,0,0,0,0.55f,0, 0.70f,0,0,0,0.55f,0, 0.85f,0,0,0,0.55f,0, 0.70f,0,0,0,0.55f,0 });
            s.guitarStrum    = pat({ 0.95f,0,0,0,0.75f,0, 0.85f,0,0,0,0.75f,0, 0.95f,0,0,0,0.75f,0, 0.85f,0,0,0,0.75f,0 });
            s.guitarUpstroke = pat({ 0,0,0,0,0.65f,0, 0,0,0,0,0.65f,0, 0,0,0,0,0.65f,0, 0,0,0,0,0.65f,0 });
            s.hornsStab      = pat({ 0.75f,0,0,0,0,0, 0,0,0,0,0.55f,0, 0,0,0,0,0,0, 0,0,0,0,0.60f,0 });
            s.bassOctaveProb = 0.20f; s.bassWalkProb = 0.80f; s.keysSustainSteps = 4;
            s.guitarMuteProb = 0.30f; s.hornsFillDensity = 0.60f; s.drumsFillDensity = 0.55f;
        }

        { // 28: Reggae One-Drop
            auto& s = styles_[28];
            s.name = "Reggae One-Drop";
            s.family = GenreFamily::RootsWorld;
            s.defaultTimeSig = { 4, 4 };
            s.defaultBpm = 78.0;
            s.defaultSwing = 0.57f;
            s.defaultPushPullMs = 6.0f;
            s.recommendedPatches = { 1, 0, 3, 3, 2, 0 };
            // Beat 3 (step 8) One-Drop kick + rimshot!
            s.kick           = pat({ 0.20f, 0, 0, 0, 0, 0, 0, 0, 1.0f, 0, 0, 0, 0, 0, 0, 0 });
            s.snare          = pat({ 0, 0, 0, 0, 0.30f, 0, 0, 0, 0.95f, 0, 0, 0, 0.35f, 0, 0, 0 });
            s.ghostSnare     = pat({ 0, 0, 0.25f, 0, 0, 0, 0.25f, 0, 0, 0, 0.25f, 0, 0, 0, 0.25f, 0 });
            s.hatClosed      = pat({ 0.80f, 0, 0.70f, 0.45f, 0.85f, 0, 0.70f, 0.45f, 0.80f, 0, 0.70f, 0.45f, 0.85f, 0, 0.60f, 0.45f });
            s.hatOpen        = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.55f, 0 });
            s.ride           = pat({ 0.70f, 0, 0.60f, 0, 0.75f, 0, 0.60f, 0, 0.70f, 0, 0.60f, 0, 0.75f, 0, 0.60f, 0 });
            s.percPrimary    = pat({ 0.65f, 0, 0.60f, 0, 0.65f, 0, 0.60f, 0, 0.65f, 0, 0.60f, 0, 0.65f, 0, 0.60f, 0 });
            s.percSecondary  = pat({ 0, 0, 0.50f, 0, 0, 0, 0.55f, 0, 0.75f, 0, 0.50f, 0, 0, 0, 0.55f, 0 });
            s.bassSync       = pat({ 0.80f, 0, 0, 0.45f, 0, 0, 0.60f, 0, 0.95f, 0, 0.65f, 0, 0, 0, 0.50f, 0 });
            s.keysComp       = pat({ 0, 0, 0.85f, 0, 0.90f, 0, 0.75f, 0, 0, 0, 0.85f, 0, 0.90f, 0, 0.75f, 0 }); // Organ bubble!
            s.guitarStrum    = pat({ 0, 0, 0, 0, 1.0f, 0, 0, 0, 0, 0, 0, 0, 1.0f, 0, 0, 0 }); // Skank on 2 & 4!
            s.guitarUpstroke = pat({ 0, 0, 0, 0, 0.90f, 0, 0, 0, 0, 0, 0, 0, 0.90f, 0, 0, 0 });
            s.hornsStab      = pat({ 0.65f, 0, 0, 0, 0, 0, 0.55f, 0, 0, 0, 0.60f, 0, 0, 0, 0, 0 });
            s.bassOctaveProb = 0.25f; s.bassWalkProb = 0.40f; s.keysSustainSteps = 2;
            s.guitarMuteProb = 0.75f; s.hornsFillDensity = 0.60f; s.drumsFillDensity = 0.45f;
            s.useSideStick = true; s.reggaeSkank = true;
        }

        { // 29: Slow 6/8 Soul Ballad (6/8 -> 12 steps)
            auto& s = styles_[29];
            s.name = "Slow 6/8 Soul Ballad";
            s.family = GenreFamily::RootsWorld;
            s.defaultTimeSig = { 6, 8 };
            s.defaultBpm = 68.0;
            s.defaultSwing = 0.50f;
            s.defaultPushPullMs = 4.5f;
            s.recommendedPatches = { 0, 0, 3, 0, 0, 1 };
            // 6/8 = 12 16th steps: beat 1 = 0, eighth 2 = 2, eighth 3 = 4, beat 4 (backbeat) = 6, eighth 5 = 8, eighth 6 = 10
            s.kick           = pat({ 1.0f, 0, 0, 0, 0.55f, 0, 0, 0, 0.45f, 0, 0.65f, 0 });
            s.snare          = pat({ 0, 0, 0, 0, 0, 0, 1.0f, 0, 0, 0, 0, 0 });
            s.ghostSnare     = pat({ 0, 0, 0.25f, 0, 0.30f, 0, 0, 0, 0.30f, 0, 0.35f, 0 });
            s.hatClosed      = pat({ 0.85f, 0, 0.65f, 0, 0.70f, 0, 0.90f, 0, 0.65f, 0, 0.70f, 0 });
            s.hatOpen        = pat({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.40f, 0 });
            s.ride           = pat({ 0.85f, 0, 0.65f, 0, 0.70f, 0, 0.90f, 0, 0.65f, 0, 0.70f, 0 });
            s.percPrimary    = pat({ 0.60f, 0, 0.45f, 0, 0.50f, 0, 0.80f, 0, 0.45f, 0, 0.50f, 0 });
            s.percSecondary  = pat({ 0, 0, 0, 0, 0, 0, 0.70f, 0, 0, 0, 0, 0 });
            s.bassSync       = pat({ 1.0f, 0, 0, 0, 0.65f, 0, 0.50f, 0, 0.60f, 0, 0.70f, 0 });
            s.keysComp       = pat({ 0.95f, 0, 0, 0, 0.55f, 0, 0.85f, 0, 0, 0, 0.55f, 0 });
            s.guitarStrum    = pat({ 0.90f, 0, 0.55f, 0, 0.65f, 0, 0.85f, 0, 0.55f, 0, 0.65f, 0 });
            s.guitarUpstroke = pat({ 0, 0, 0.40f, 0, 0, 0, 0, 0, 0.40f, 0, 0, 0 });
            s.hornsStab      = pat({ 0.70f, 0, 0, 0, 0, 0, 0.65f, 0, 0, 0, 0, 0 });
            s.bassOctaveProb = 0.20f; s.bassWalkProb = 0.45f; s.keysSustainSteps = 6;
            s.guitarMuteProb = 0.15f; s.hornsFillDensity = 0.50f; s.drumsFillDensity = 0.45f;
        }

        // =========================================================================
        // 30..31: ODD & MODERN
        // =========================================================================
        { // 30: Math-Rock (5/4 -> 20 steps: 3+2 grouping)
            auto& s = styles_[30];
            s.name = "Math-Rock (5/4)";
            s.family = GenreFamily::OddModern;
            s.defaultTimeSig = { 5, 4 };
            s.defaultBpm = 132.0;
            s.defaultSwing = 0.50f;
            s.defaultPushPullMs = -1.0f;
            s.recommendedPatches = { 3, 1, 1, 0, 0, 3 };
            // 20 steps: beats at 0, 4, 8, 12, 16
            s.kick           = pat({ 1.0f,0,0,0.60f, 0,0,0.65f,0, 0.85f,0,0,0, 0.95f,0,0.55f,0, 0,0,0.50f,0 });
            s.snare          = pat({ 0,0,0,0, 1.0f,0,0,0, 0,0,0.75f,0, 0,0,0,0, 1.0f,0,0,0 });
            s.ghostSnare     = pat({ 0,0,0.35f,0, 0,0,0,0.50f, 0,0.45f,0,0, 0,0,0,0.45f, 0,0,0.40f,0 });
            s.hatClosed      = pat({ 0.90f,0,0.75f,0, 0.90f,0,0.75f,0, 0.85f,0,0.75f,0, 0.90f,0,0.75f,0, 0.90f,0,0.65f,0 });
            s.hatOpen        = pat({ 0,0,0,0, 0,0,0,0, 0,0,0.45f,0, 0,0,0,0, 0,0,0.55f,0 });
            s.ride           = pat({ 0.90f,0,0.75f,0, 0.90f,0,0.75f,0, 0.85f,0,0.75f,0, 0.90f,0,0.75f,0, 0.90f,0,0.75f,0 });
            s.percPrimary    = pat({ 0.65f,0,0,0.50f, 0,0,0.55f,0, 0,0,0.60f,0, 0.65f,0,0,0, 0.60f,0,0,0 });
            s.percSecondary  = pat({ 0,0,0,0, 0.60f,0,0,0, 0,0,0.55f,0, 0,0,0,0, 0.60f,0,0,0 });
            s.bassSync       = pat({ 1.0f,0,0,0.65f, 0,0,0.70f,0, 0.85f,0,0.65f,0, 0.95f,0,0.65f,0, 0,0,0.60f,0 });
            s.keysComp       = pat({ 0.90f,0,0,0.60f, 0,0,0.70f,0, 0,0,0.70f,0, 0.85f,0,0,0, 0.65f,0,0,0 });
            s.guitarStrum    = pat({ 0.95f,0,0.60f,0.70f, 0.70f,0,0.75f,0, 0.85f,0,0.75f,0, 0.95f,0,0.70f,0, 0.80f,0,0.65f,0 });
            s.guitarUpstroke = pat({ 0,0,0.50f,0, 0,0,0.55f,0, 0,0,0.55f,0, 0,0,0.55f,0, 0,0,0.55f,0 });
            s.hornsStab      = pat({ 0.75f,0,0,0, 0,0,0.55f,0, 0,0,0.60f,0, 0.70f,0,0,0, 0,0,0,0 });
            s.bassOctaveProb = 0.40f; s.bassWalkProb = 0.45f; s.keysSustainSteps = 4;
            s.guitarMuteProb = 0.35f; s.hornsFillDensity = 0.50f; s.drumsFillDensity = 0.70f;
        }

        { // 31: Prog Fusion (7/8 -> 14 steps: 2+2+3 grouping)
            auto& s = styles_[31];
            s.name = "Prog Fusion (7/8)";
            s.family = GenreFamily::OddModern;
            s.defaultTimeSig = { 7, 8 };
            s.defaultBpm = 124.0;
            s.defaultSwing = 0.50f;
            s.defaultPushPullMs = -0.5f;
            s.recommendedPatches = { 3, 0, 0, 4, 1, 3 };
            // 14 steps: 2+2+3 eighths -> accents at 0, 4, 8
            s.kick           = pat({ 1.0f,0,0,0.55f, 0.85f,0,0,0, 0.95f,0,0.60f,0, 0,0 });
            s.snare          = pat({ 0,0,0,0, 0,0,0.95f,0, 0,0,0,0, 1.0f,0 });
            s.ghostSnare     = pat({ 0,0,0.45f,0, 0,0.40f,0,0, 0,0.45f,0,0.50f, 0,0.45f });
            s.hatClosed      = pat({ 0.90f,0.50f,0.80f,0.50f, 0.90f,0.50f,0.85f,0.50f, 0.90f,0.50f,0.80f,0.50f, 0.85f,0.50f });
            s.hatOpen        = pat({ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0.60f });
            s.ride           = pat({ 0.95f,0,0.80f,0, 0.95f,0,0.85f,0, 0.95f,0,0.80f,0, 0.85f,0 });
            s.percPrimary    = pat({ 0.75f,0,0.55f,0, 0.75f,0,0.55f,0, 0.75f,0,0.55f,0, 0.65f,0 });
            s.percSecondary  = pat({ 0,0,0,0, 0,0,0.65f,0, 0,0,0,0, 0.70f,0 });
            s.bassSync       = pat({ 1.0f,0,0.55f,0.60f, 0.85f,0,0.65f,0, 0.95f,0,0.70f,0, 0.65f,0 });
            s.keysComp       = pat({ 0.90f,0,0,0, 0.80f,0,0,0, 0.90f,0,0,0.65f, 0,0 });
            s.guitarStrum    = pat({ 0.95f,0,0.65f,0, 0.90f,0,0.70f,0, 0.95f,0,0.70f,0, 0.80f,0 });
            s.guitarUpstroke = pat({ 0,0,0.55f,0, 0,0,0.55f,0, 0,0,0.55f,0, 0,0.50f });
            s.hornsStab      = pat({ 0.85f,0,0,0, 0.70f,0,0,0, 0.80f,0,0,0, 0.65f,0 });
            s.bassOctaveProb = 0.45f; s.bassWalkProb = 0.60f; s.keysSustainSteps = 4;
            s.guitarMuteProb = 0.35f; s.hornsFillDensity = 0.65f; s.drumsFillDensity = 0.80f;
        }
    }
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
};

} // namespace sessionmate
