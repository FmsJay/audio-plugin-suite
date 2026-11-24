#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fossil
{

inline constexpr int kNumStrata = 4;
inline constexpr int kWavePreviewBins = 64;
inline constexpr int kMaxEuclidSteps = 16;

enum class StreamSource : int
{
    Transient = 0, // T
    Tonal     = 1, // H
    Noise     = 2  // N
};

inline const char* sourceShortName (StreamSource s) noexcept
{
    switch (s)
    {
        case StreamSource::Transient: return "T";
        case StreamSource::Tonal:     return "H";
        case StreamSource::Noise:     return "N";
    }
    return "H";
}

inline const char* sourceLongName (StreamSource s) noexcept
{
    switch (s)
    {
        case StreamSource::Transient: return "Transient";
        case StreamSource::Tonal:     return "Tonal";
        case StreamSource::Noise:     return "Air/Noise";
    }
    return "Tonal";
}

namespace param_ids
{
    // Decomposition & Dry Pass
    inline constexpr const char* hpssSep         = "hpss_sep";
    inline constexpr const char* noiseThresh     = "noise_thresh";
    inline constexpr const char* dryT            = "dry_t";
    inline constexpr const char* dryH            = "dry_h";
    inline constexpr const char* dryN            = "dry_n";

    // Master Strata & Shimmer
    inline constexpr const char* strataBlend     = "strata_blend";
    inline constexpr const char* shimmerSmear    = "shimmer_smear";
    inline constexpr const char* shimmerPitch    = "shimmer_pitch";
    inline constexpr const char* outputGain      = "output_gain";
    inline constexpr const char* crossfadePos    = "crossfade_pos";
    inline constexpr const char* crossfadeDepth  = "crossfade_depth";
    inline constexpr const char* freezeMode      = "freeze_mode";       // 0 = Latch, 1 = Momentary
    inline constexpr const char* midiFreeze      = "midi_freeze";       // bool

    // Euclidean Gate
    inline constexpr const char* euclidEnable    = "euclid_enable";
    inline constexpr const char* euclidSteps     = "euclid_steps";      // 1..16
    inline constexpr const char* euclidHits      = "euclid_hits";       // 0..16
    inline constexpr const char* euclidRot       = "euclid_rot";        // 0..15
    inline constexpr const char* euclidSmooth    = "euclid_smooth";     // 0..1
    inline constexpr const char* euclidDepth     = "euclid_depth";      // 0..1
    inline constexpr const char* euclidRate      = "euclid_rate";       // 0=1/4,1=1/8,2=1/16,3=1/8T,4=1/32
    inline constexpr const char* internalBpm     = "internal_bpm";      // 40..240

    // SuiteTextureLink (VowelMorph) & SuiteHarmonyLink (SessionMate / HarmonyHUD)
    inline constexpr const char* textureLinkBus  = "tex_link_bus";      // 0=Off, 1..8=Bus A..H
    inline constexpr const char* vowelModDepth   = "vowel_mod_depth";   // 0..1
    inline constexpr const char* harmonyLinkBus  = "harm_link_bus";     // 0=Off, 1..8=Bus A..H
    inline constexpr const char* harmQuantize    = "harm_quantize";     // bool

    // Per-stratum ID tables (index 0..3 -> L1..L4)
    inline constexpr std::array<const char*, kNumStrata> layerSource = { "l1_source", "l2_source", "l3_source", "l4_source" };
    inline constexpr std::array<const char*, kNumStrata> layerGain   = { "l1_gain",   "l2_gain",   "l3_gain",   "l4_gain"   };
    inline constexpr std::array<const char*, kNumStrata> layerPan    = { "l1_pan",    "l2_pan",    "l3_pan",    "l4_pan"    };
    inline constexpr std::array<const char*, kNumStrata> layerWidth  = { "l1_width",  "l2_width",  "l3_width",  "l4_width"  };
    inline constexpr std::array<const char*, kNumStrata> layerPitch  = { "l1_pitch",  "l2_pitch",  "l3_pitch",  "l4_pitch"  };
    inline constexpr std::array<const char*, kNumStrata> layerGrain  = { "l1_grain",  "l2_grain",  "l3_grain",  "l4_grain"  };
    inline constexpr std::array<const char*, kNumStrata> layerDrift  = { "l1_drift",  "l2_drift",  "l3_drift",  "l4_drift"  };
    inline constexpr std::array<const char*, kNumStrata> layerTilt   = { "l1_tilt",   "l2_tilt",   "l3_tilt",   "l4_tilt"   };
    inline constexpr std::array<const char*, kNumStrata> layerMute   = { "l1_mute",   "l2_mute",   "l3_mute",   "l4_mute"   };
    inline constexpr std::array<const char*, kNumStrata> layerSolo   = { "l1_solo",   "l2_solo",   "l3_solo",   "l4_solo"   };
    inline constexpr std::array<const char*, kNumStrata> layerGate   = { "l1_gate",   "l2_gate",   "l3_gate",   "l4_gate"   };
} // namespace param_ids

// Plain-data DSP parameter snapshot passed from Processor to DSP modules every block
struct StratumParams
{
    StreamSource source = StreamSource::Tonal;
    float gain          = 0.80f;   // 0.0 .. 1.5
    float pan           = 0.0f;    // -1.0 .. +1.0
    float width         = 1.0f;    // 0.0 .. 2.0
    float pitchSemi     = 0.0f;    // -24.0 .. +24.0
    float grainMs       = 180.0f;  // 20.0 .. 500.0
    float drift         = 0.30f;   // 0.0 .. 1.0
    float tilt          = 0.0f;    // -1.0 .. +1.0
    bool  mute          = false;
    bool  solo          = false;
    bool  euclidGated   = true;
};

struct MasterParams
{
    float hpssSep        = 0.72f;
    float noiseThresh    = 0.45f;
    float dryT           = 0.90f;
    float dryH           = 0.50f;
    float dryN           = 0.35f;
    float strataBlend    = 0.65f;
    float shimmerSmear   = 0.38f;
    float shimmerPitch   = 0.35f;
    float outputGainDb   = 0.0f;
    float crossfadePos   = 0.50f;
    float crossfadeDepth = 0.0f;
    int   freezeMode     = 0;      // 0=Latch, 1=Momentary
    bool  midiFreeze     = true;

    bool  euclidEnable   = false;
    int   euclidSteps    = 16;
    int   euclidHits     = 5;
    int   euclidRot      = 0;
    float euclidSmooth   = 0.35f;
    float euclidDepth    = 0.85f;
    int   euclidRate     = 2;      // 1/16
    float internalBpm    = 120.0f;

    int   textureLinkBus = 0;      // 0=Off, 1..8=Bus A..H
    float vowelModDepth  = 0.65f;  // 0..1
    int   harmonyLinkBus = 0;      // 0=Off, 1..8=Bus A..H
    bool  harmQuantize   = true;

    std::array<StratumParams, kNumStrata> strata;
};

// Lock-free UI -> Audio command queue
enum class CommandType : uint8_t
{
    None = 0,
    CaptureStream,      // slot (-1 = auto next), source (T/H/N)
    ReleaseMomentary,   // end momentary freeze on slot (-1 = last momentary)
    ClearSlot,          // slot 0..3
    ClearAll,
    SwapSlots           // slotA, slotB
};

struct FossilCommand
{
    CommandType  type   = CommandType::None;
    int          slotA  = -1;
    int          slotB  = -1;
    StreamSource source = StreamSource::Tonal;
};

template <typename T, size_t Capacity>
class LockFreeQueue
{
    static_assert ((Capacity & (Capacity - 1)) == 0, "Capacity must be power of two");
public:
    bool push (const T& item) noexcept
    {
        const auto w = writeIdx_.load (std::memory_order_relaxed);
        const auto next = (w + 1) & (Capacity - 1);
        if (next == readIdx_.load (std::memory_order_acquire))
            return false;
        buffer_[w] = item;
        writeIdx_.store (next, std::memory_order_release);
        return true;
    }

    bool pop (T& out) noexcept
    {
        const auto r = readIdx_.load (std::memory_order_relaxed);
        if (r == writeIdx_.load (std::memory_order_acquire))
            return false;
        out = buffer_[r];
        readIdx_.store ((r + 1) & (Capacity - 1), std::memory_order_release);
        return true;
    }

    void reset() noexcept
    {
        readIdx_.store (0, std::memory_order_relaxed);
        writeIdx_.store (0, std::memory_order_relaxed);
    }

private:
    std::array<T, Capacity> buffer_ {};
    std::atomic<size_t> readIdx_  { 0 };
    std::atomic<size_t> writeIdx_ { 0 };
};

// Lock-free Audio -> UI telemetry
struct StratumTelemetry
{
    std::atomic<bool>  frozen          { false };
    std::atomic<int>   source          { 1 };     // StreamSource
    std::atomic<float> outputLevel     { 0.0f };
    std::atomic<float> playheadNorm    { 0.0f };  // 0..1
    std::atomic<float> effectiveWeight { 1.0f };  // 0..1
    std::atomic<uint32_t> captureCounter { 0 };   // increments on new snapshot
    std::array<std::atomic<float>, kWavePreviewBins> wavePreview {};
};

struct FossilTelemetry
{
    std::atomic<float>    inputPeakL          { 0.0f };
    std::atomic<float>    inputPeakR          { 0.0f };
    std::atomic<float>    outputPeakL         { 0.0f };
    std::atomic<float>    outputPeakR         { 0.0f };
    std::atomic<float>    liveEnergyT         { 0.0f };
    std::atomic<float>    liveEnergyH         { 0.0f };
    std::atomic<float>    liveEnergyN         { 0.0f };
    std::atomic<double>   currentBpm          { 120.0 };
    std::atomic<bool>     hostSynced          { false };
    std::atomic<int>      currentEuclidStep   { 0 };
    std::atomic<float>    currentEuclidEnv    { 1.0f };
    std::atomic<uint32_t> euclidPatternMask   { 0x1111u };
    std::atomic<int>      activeFreezeSource  { -1 }; // -1 none, 0=T, 1=H, 2=N
    std::atomic<int>      lastCapturedSlot    { 0 };
    std::atomic<uint32_t> freezeCaptureSeq    { 0 };

    // SuiteTextureLink (VowelMorph) live peer telemetry
    std::atomic<bool>     vowelPeerAlive      { false };
    std::atomic<float>    vowelX              { 0.5f };
    std::atomic<float>    vowelY              { 0.5f };
    std::atomic<float>    vowelBrightness     { 0.5f };
    std::atomic<float>    vowelModEnv         { 0.0f };
    std::atomic<float>    effectiveCrossfade  { 0.5f };
    std::array<std::atomic<char>, 8>  vowelSyllableLabel {};
    std::array<std::atomic<char>, 24> vowelTractLabel    {};

    // SuiteHarmonyLink (SessionMate / HarmonyHUD) live peer telemetry
    std::atomic<bool>     harmonyPeerAlive    { false };
    std::atomic<uint16_t> activeHarmonyMask   { 0 };
    std::atomic<int>      activeChordRootPc   { -1 };
    std::array<std::atomic<char>, 16> harmonyChordLabel  {};

    std::array<StratumTelemetry, kNumStrata> strata;
};

// Factory Presets (14 presets)
struct FactoryPreset
{
    const char* name;
    const char* category;
    MasterParams params;
};

inline const std::vector<FactoryPreset>& getFactoryPresets()
{
    static const std::vector<FactoryPreset> presets = []()
    {
        std::vector<FactoryPreset> list;
        auto makeBase = []() -> MasterParams
        {
            MasterParams p;
            p.strata[0] = { StreamSource::Tonal,     0.85f,  0.0f,  1.0f,   0.0f, 220.0f, 0.25f, -0.20f, false, false, false };
            p.strata[1] = { StreamSource::Tonal,     0.72f,  0.0f,  1.45f, 12.0f, 180.0f, 0.35f,  0.20f, false, false, false };
            p.strata[2] = { StreamSource::Noise,     0.65f,  0.0f,  1.30f,  0.0f, 140.0f, 0.45f,  0.35f, false, false, true  };
            p.strata[3] = { StreamSource::Transient, 0.55f,  0.0f,  1.10f,  0.0f,  90.0f, 0.30f,  0.10f, false, false, true  };
            return p;
        };

        // 1. Geological Pad
        {
            auto p = makeBase();
            p.dryT = 0.90f; p.dryH = 0.50f; p.dryN = 0.30f;
            p.strataBlend = 0.62f; p.shimmerSmear = 0.44f; p.shimmerPitch = 0.35f;
            p.euclidEnable = true; p.euclidSteps = 16; p.euclidHits = 5; p.euclidRot = 0; p.euclidSmooth = 0.35f; p.euclidDepth = 0.85f; p.euclidRate = 2;
            list.push_back ({ "Geological Pad", "Pads", p });
        }

        // 2. Acoustic Soloist
        {
            auto p = makeBase();
            p.dryT = 1.00f; p.dryH = 0.75f; p.dryN = 0.45f;
            p.strataBlend = 0.52f; p.shimmerSmear = 0.30f; p.shimmerPitch = 0.15f;
            p.euclidEnable = false;
            p.strata[0] = { StreamSource::Tonal, 0.90f,  0.0f, 1.15f,   0.0f, 260.0f, 0.20f, -0.30f, false, false, false };
            p.strata[1] = { StreamSource::Tonal, 0.65f,  0.0f, 0.85f, -12.0f, 300.0f, 0.15f, -0.50f, false, false, false };
            p.strata[2] = { StreamSource::Tonal, 0.45f,  0.0f, 1.60f,  12.0f, 200.0f, 0.35f,  0.30f, false, false, false };
            p.strata[3] = { StreamSource::Noise, 0.30f,  0.0f, 1.35f,   0.0f, 240.0f, 0.40f,  0.15f, false, false, false };
            list.push_back ({ "Acoustic Soloist", "Live", p });
        }

        // 3. Breath Pulse
        {
            auto p = makeBase();
            p.hpssSep = 0.78f; p.noiseThresh = 0.38f;
            p.dryT = 0.95f; p.dryH = 0.40f; p.dryN = 0.15f;
            p.strataBlend = 0.72f; p.shimmerSmear = 0.28f; p.shimmerPitch = 0.10f;
            p.euclidEnable = true; p.euclidSteps = 16; p.euclidHits = 5; p.euclidRot = 2; p.euclidSmooth = 0.20f; p.euclidDepth = 0.95f; p.euclidRate = 2;
            p.strata[0] = { StreamSource::Noise, 0.95f, -0.25f, 1.40f,  0.0f, 120.0f, 0.50f,  0.45f, false, false, true };
            p.strata[1] = { StreamSource::Noise, 0.80f,  0.25f, 1.60f,  7.0f, 160.0f, 0.60f,  0.60f, false, false, true };
            p.strata[2] = { StreamSource::Tonal, 0.55f,  0.0f,  1.20f,  0.0f, 280.0f, 0.25f, -0.25f, false, false, false };
            p.strata[3] = { StreamSource::Transient, 0.45f, 0.0f, 1.10f, 12.0f, 65.0f, 0.45f, 0.30f, false, false, true };
            list.push_back ({ "Breath Pulse", "Rhythmic", p });
        }

        // 4. Cathedral Shimmer
        {
            auto p = makeBase();
            p.dryT = 0.80f; p.dryH = 0.45f; p.dryN = 0.25f;
            p.strataBlend = 0.78f; p.shimmerSmear = 0.82f; p.shimmerPitch = 0.72f;
            p.euclidEnable = false;
            p.strata[0] = { StreamSource::Tonal, 0.85f,  0.0f, 1.30f,  0.0f, 360.0f, 0.45f, -0.10f, false, false, false };
            p.strata[1] = { StreamSource::Tonal, 0.75f, -0.2f, 1.65f, 12.0f, 380.0f, 0.55f,  0.35f, false, false, false };
            p.strata[2] = { StreamSource::Tonal, 0.55f,  0.2f, 1.75f,  7.0f, 320.0f, 0.60f,  0.25f, false, false, false };
            p.strata[3] = { StreamSource::Noise, 0.45f,  0.0f, 1.80f, 12.0f, 400.0f, 0.50f,  0.50f, false, false, false };
            list.push_back ({ "Cathedral Shimmer", "Ambient", p });
        }

        // 5. Percussive Fossils
        {
            auto p = makeBase();
            p.hpssSep = 0.85f; p.dryT = 0.85f; p.dryH = 0.55f; p.dryN = 0.20f;
            p.strataBlend = 0.70f; p.shimmerSmear = 0.22f; p.shimmerPitch = 0.0f;
            p.euclidEnable = true; p.euclidSteps = 16; p.euclidHits = 7; p.euclidRot = 1; p.euclidSmooth = 0.12f; p.euclidDepth = 0.92f; p.euclidRate = 2;
            p.strata[0] = { StreamSource::Transient, 0.95f, -0.35f, 1.20f,   0.0f, 42.0f, 0.35f,  0.15f, false, false, true };
            p.strata[1] = { StreamSource::Transient, 0.80f,  0.35f, 1.30f,  12.0f, 35.0f, 0.55f,  0.45f, false, false, true };
            p.strata[2] = { StreamSource::Transient, 0.75f,  0.0f,  0.80f, -12.0f, 58.0f, 0.25f, -0.35f, false, false, true };
            p.strata[3] = { StreamSource::Noise,     0.50f,  0.0f,  1.50f,   0.0f, 48.0f, 0.65f,  0.40f, false, false, true };
            list.push_back ({ "Percussive Fossils", "Rhythmic", p });
        }

        // 6. Subterranean Drone
        {
            auto p = makeBase();
            p.dryT = 0.85f; p.dryH = 0.35f; p.dryN = 0.20f;
            p.strataBlend = 0.75f; p.shimmerSmear = 0.58f; p.shimmerPitch = 0.05f;
            p.euclidEnable = false;
            p.strata[0] = { StreamSource::Tonal, 0.95f,  0.0f, 0.70f, -12.0f, 420.0f, 0.18f, -0.70f, false, false, false };
            p.strata[1] = { StreamSource::Tonal, 0.80f,  0.0f, 0.50f, -24.0f, 460.0f, 0.12f, -0.85f, false, false, false };
            p.strata[2] = { StreamSource::Tonal, 0.55f,  0.0f, 1.35f,   0.0f, 380.0f, 0.30f, -0.55f, false, false, false };
            p.strata[3] = { StreamSource::Noise, 0.60f,  0.0f, 1.25f, -12.0f, 450.0f, 0.25f, -0.65f, false, false, false };
            list.push_back ({ "Subterranean Drone", "Cinematic", p });
        }

        // 7. Glass Harmonics
        {
            auto p = makeBase();
            p.dryT = 0.92f; p.dryH = 0.45f; p.dryN = 0.20f;
            p.strataBlend = 0.68f; p.shimmerSmear = 0.70f; p.shimmerPitch = 0.80f;
            p.euclidEnable = false;
            p.strata[0] = { StreamSource::Tonal, 0.70f,  0.0f, 1.30f, 12.0f, 210.0f, 0.35f,  0.55f, false, false, false };
            p.strata[1] = { StreamSource::Tonal, 0.65f, -0.3f, 1.70f, 19.0f, 190.0f, 0.45f,  0.70f, false, false, false };
            p.strata[2] = { StreamSource::Tonal, 0.45f,  0.3f, 1.80f, 24.0f, 170.0f, 0.50f,  0.80f, false, false, false };
            p.strata[3] = { StreamSource::Noise, 0.35f,  0.0f, 1.50f, 12.0f, 240.0f, 0.40f,  0.65f, false, false, false };
            list.push_back ({ "Glass Harmonics", "Ambient", p });
        }

        // 8. Euclidean Polyrhythm
        {
            auto p = makeBase();
            p.dryT = 0.88f; p.dryH = 0.40f; p.dryN = 0.20f;
            p.strataBlend = 0.76f; p.shimmerSmear = 0.32f; p.shimmerPitch = 0.25f;
            p.euclidEnable = true; p.euclidSteps = 13; p.euclidHits = 5; p.euclidRot = 3; p.euclidSmooth = 0.14f; p.euclidDepth = 0.96f; p.euclidRate = 2;
            p.strata[0] = { StreamSource::Tonal,     0.85f, -0.25f, 1.15f,  0.0f, 140.0f, 0.25f,  0.0f,  false, false, true };
            p.strata[1] = { StreamSource::Tonal,     0.72f,  0.25f, 1.45f,  7.0f, 120.0f, 0.35f,  0.25f, false, false, true };
            p.strata[2] = { StreamSource::Noise,     0.70f,  0.0f,  1.60f,  0.0f,  95.0f, 0.55f,  0.40f, false, false, true };
            p.strata[3] = { StreamSource::Transient, 0.65f,  0.0f,  1.20f, 12.0f,  50.0f, 0.40f,  0.20f, false, false, true };
            list.push_back ({ "Euclidean Polyrhythm", "Rhythmic", p });
        }

        // 9. Tape Dust Strata
        {
            auto p = makeBase();
            p.noiseThresh = 0.35f; p.dryT = 0.78f; p.dryH = 0.60f; p.dryN = 0.40f;
            p.strataBlend = 0.58f; p.shimmerSmear = 0.48f; p.shimmerPitch = 0.0f;
            p.euclidEnable = false;
            p.strata[0] = { StreamSource::Tonal, 0.80f, -0.35f, 1.10f,  0.18f, 250.0f, 0.52f, -0.40f, false, false, false };
            p.strata[1] = { StreamSource::Tonal, 0.80f,  0.35f, 1.10f, -0.18f, 270.0f, 0.58f, -0.45f, false, false, false };
            p.strata[2] = { StreamSource::Noise, 0.70f,  0.0f,  1.45f,  0.0f,  190.0f, 0.65f, -0.20f, false, false, false };
            p.strata[3] = { StreamSource::Tonal, 0.50f,  0.0f,  0.90f, -12.0f, 340.0f, 0.40f, -0.60f, false, false, false };
            list.push_back ({ "Tape Dust Strata", "Lo-Fi", p });
        }

        // 10. Crystalline Pluck
        {
            auto p = makeBase();
            p.dryT = 1.00f; p.dryH = 0.18f; p.dryN = 0.15f;
            p.strataBlend = 0.68f; p.shimmerSmear = 0.55f; p.shimmerPitch = 0.45f;
            p.euclidEnable = true; p.euclidSteps = 8; p.euclidHits = 3; p.euclidRot = 0; p.euclidSmooth = 0.65f; p.euclidDepth = 0.70f; p.euclidRate = 1;
            p.strata[0] = { StreamSource::Tonal, 0.85f,  0.0f, 1.35f,  0.0f, 240.0f, 0.30f,  0.10f, false, false, true };
            p.strata[1] = { StreamSource::Tonal, 0.70f,  0.0f, 1.65f, 12.0f, 180.0f, 0.40f,  0.45f, false, false, true };
            p.strata[2] = { StreamSource::Noise, 0.45f,  0.0f, 1.50f,  0.0f, 150.0f, 0.50f,  0.35f, false, false, true };
            p.strata[3] = { StreamSource::Transient, 0.30f, 0.0f, 1.25f, 12.0f, 70.0f, 0.35f, 0.40f, false, false, false };
            list.push_back ({ "Crystalline Pluck", "Live", p });
        }

        // 11. Ambient Film Score
        {
            auto p = makeBase();
            p.dryT = 0.70f; p.dryH = 0.45f; p.dryN = 0.35f;
            p.strataBlend = 0.80f; p.shimmerSmear = 0.68f; p.shimmerPitch = 0.40f;
            p.crossfadePos = 0.50f; p.crossfadeDepth = 0.45f;
            p.euclidEnable = false;
            p.strata[0] = { StreamSource::Tonal, 0.90f,  0.0f, 1.20f,   0.0f, 380.0f, 0.30f, -0.25f, false, false, false };
            p.strata[1] = { StreamSource::Tonal, 0.78f, -0.2f, 1.50f,   7.0f, 340.0f, 0.40f,  0.05f, false, false, false };
            p.strata[2] = { StreamSource::Tonal, 0.72f,  0.2f, 1.75f,  12.0f, 300.0f, 0.45f,  0.30f, false, false, false };
            p.strata[3] = { StreamSource::Noise, 0.65f,  0.0f, 1.85f,   0.0f, 420.0f, 0.50f,  0.20f, false, false, false };
            list.push_back ({ "Ambient Film Score", "Cinematic", p });
        }

        // 12. Stutter Matrix
        {
            auto p = makeBase();
            p.hpssSep = 0.82f; p.dryT = 0.60f; p.dryH = 0.30f; p.dryN = 0.15f;
            p.strataBlend = 0.82f; p.shimmerSmear = 0.18f; p.shimmerPitch = 0.15f;
            p.euclidEnable = true; p.euclidSteps = 16; p.euclidHits = 9; p.euclidRot = 4; p.euclidSmooth = 0.08f; p.euclidDepth = 1.0f; p.euclidRate = 2;
            p.strata[0] = { StreamSource::Transient, 0.90f, -0.4f, 1.30f,  0.0f, 28.0f, 0.75f,  0.25f, false, false, true };
            p.strata[1] = { StreamSource::Tonal,     0.75f,  0.4f, 1.40f, 12.0f, 32.0f, 0.80f,  0.35f, false, false, true };
            p.strata[2] = { StreamSource::Noise,     0.70f,  0.0f, 1.60f, -5.0f, 25.0f, 0.85f,  0.15f, false, false, true };
            p.strata[3] = { StreamSource::Transient, 0.65f,  0.0f, 1.20f, -12.0f, 38.0f, 0.70f, -0.20f, false, false, true };
            list.push_back ({ "Stutter Matrix", "Glitch", p });
        }

        // 13. Octave Organ
        {
            auto p = makeBase();
            p.dryT = 0.95f; p.dryH = 0.50f; p.dryN = 0.15f;
            p.strataBlend = 0.70f; p.shimmerSmear = 0.35f; p.shimmerPitch = 0.10f;
            p.euclidEnable = false;
            p.strata[0] = { StreamSource::Tonal, 0.85f,  0.0f, 0.85f, -12.0f, 150.0f, 0.05f, -0.35f, false, false, false };
            p.strata[1] = { StreamSource::Tonal, 0.90f,  0.0f, 1.05f,   0.0f, 150.0f, 0.05f,  0.0f,  false, false, false };
            p.strata[2] = { StreamSource::Tonal, 0.68f,  0.0f, 1.30f,  12.0f, 140.0f, 0.08f,  0.25f, false, false, false };
            p.strata[3] = { StreamSource::Tonal, 0.45f,  0.0f, 1.55f,  24.0f, 130.0f, 0.10f,  0.45f, false, false, false };
            list.push_back ({ "Octave Organ", "Harmonic", p });
        }

        // 14. Frozen Ghost Choir
        {
            auto p = makeBase();
            p.noiseThresh = 0.42f; p.dryT = 0.65f; p.dryH = 0.35f; p.dryN = 0.25f;
            p.strataBlend = 0.78f; p.shimmerSmear = 0.62f; p.shimmerPitch = 0.48f;
            p.euclidEnable = true; p.euclidSteps = 16; p.euclidHits = 3; p.euclidRot = 0; p.euclidSmooth = 0.82f; p.euclidDepth = 0.55f; p.euclidRate = 1;
            p.strata[0] = { StreamSource::Tonal, 0.82f, -0.15f, 1.40f,  0.0f, 320.0f, 0.42f,  0.10f, false, false, true };
            p.strata[1] = { StreamSource::Tonal, 0.68f,  0.15f, 1.60f, 12.0f, 340.0f, 0.48f,  0.30f, false, false, true };
            p.strata[2] = { StreamSource::Noise, 0.75f,  0.0f,  1.75f,  0.0f, 290.0f, 0.55f,  0.45f, false, false, true };
            p.strata[3] = { StreamSource::Tonal, 0.52f,  0.0f,  1.25f, -5.0f, 360.0f, 0.38f, -0.15f, false, false, false };
            list.push_back ({ "Frozen Ghost Choir", "Ambient", p });
        }

        return list;
    }();
    return presets;
}

} // namespace fossil
