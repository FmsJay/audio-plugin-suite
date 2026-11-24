#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <type_traits>
#include <vector>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef NOGDI
        #define NOGDI
    #endif
    #ifndef NOUSER
        #define NOUSER
    #endif
    #include <windows.h>
    #include <intrin.h>
    #ifdef min
        #undef min
    #endif
    #ifdef max
        #undef max
    #endif
    #ifdef near
        #undef near
    #endif
    #ifdef far
        #undef far
    #endif
    #ifdef DrawText
        #undef DrawText
    #endif
    #ifdef LoadImage
        #undef LoadImage
    #endif
    #ifdef Rectangle
        #undef Rectangle
    #endif
#else
    #include <chrono>
    #include <map>
    #include <memory>
    #include <thread>
#endif

namespace suite::harmony_link {

// ============================================================================
// Constants
// ============================================================================
inline constexpr uint32_t kLinkMagic          = 0x53484C31u; // "SHL1"
inline constexpr uint32_t kLinkVersion        = 1u;
inline constexpr int      kNumBuses           = 8;           // Buses 0..7 => A..H
inline constexpr uint64_t kHeartbeatTimeoutMs = 1500ull;
inline constexpr int      kMaxChartBars       = 32;
inline constexpr int      kNumBandSlots       = 6;           // Drums, Bass, Keys, Guitar, Perc, Horns

// ============================================================================
// Canonical Shared Enums
// ============================================================================
enum class LinkChordQuality : uint8_t {
    None = 0,
    Major,
    Minor,
    Dom7,
    Maj7,
    Min7,
    Min7b5,
    Dim,
    Dim7,
    Aug,
    Sus2,
    Sus4,
    Maj6,
    Min6,
    Dom9,
    Maj9,
    Min9,
    Dom11,
    Min11,
    Dom13,
    Maj13,
    Min13,
    Dom7Sharp9,
    Dom7Flat9,
    Power5,
    Add9
};

enum class LinkMode : uint8_t {
    Ionian = 0,
    Dorian,
    Phrygian,
    Lydian,
    Mixolydian,
    Aeolian,
    Locrian,
    HarmonicMinor,
    MelodicMinor,
    Altered,
    PentatonicMajor,
    PentatonicMinor,
    Blues
};

enum class ReharmTiming : uint8_t {
    Immediate = 0,
    NextBeat  = 1,
    NextBar   = 2
};

enum class HarmonicSourceMode : uint8_t {
    Chart          = 0,
    LiveAudio      = 1,
    HarmonyHudLink = 2
};

// ============================================================================
// Music-Theory Bitmask & Formatting Helpers
// ============================================================================
inline constexpr uint16_t rotatePitchClassMask(uint16_t mask, int8_t rootPc) noexcept {
    const unsigned r = static_cast<unsigned>(((static_cast<int>(rootPc) % 12) + 12) % 12);
    const unsigned m = static_cast<unsigned>(mask) & 0x0FFFu;
    return static_cast<uint16_t>(((m << r) | (m >> (12u - r))) & 0x0FFFu);
}

inline constexpr uint16_t chordQualityPitchMask(LinkChordQuality quality) noexcept {
    constexpr auto bit = [](unsigned pc) constexpr -> uint16_t {
        return static_cast<uint16_t>(1u << (pc % 12u));
    };
    switch (quality) {
        case LinkChordQuality::None:       return 0u;
        case LinkChordQuality::Major:      return static_cast<uint16_t>(bit(0) | bit(4) | bit(7));
        case LinkChordQuality::Minor:      return static_cast<uint16_t>(bit(0) | bit(3) | bit(7));
        case LinkChordQuality::Dom7:       return static_cast<uint16_t>(bit(0) | bit(4) | bit(7) | bit(10));
        case LinkChordQuality::Maj7:       return static_cast<uint16_t>(bit(0) | bit(4) | bit(7) | bit(11));
        case LinkChordQuality::Min7:       return static_cast<uint16_t>(bit(0) | bit(3) | bit(7) | bit(10));
        case LinkChordQuality::Min7b5:     return static_cast<uint16_t>(bit(0) | bit(3) | bit(6) | bit(10));
        case LinkChordQuality::Dim:        return static_cast<uint16_t>(bit(0) | bit(3) | bit(6));
        case LinkChordQuality::Dim7:       return static_cast<uint16_t>(bit(0) | bit(3) | bit(6) | bit(9));
        case LinkChordQuality::Aug:        return static_cast<uint16_t>(bit(0) | bit(4) | bit(8));
        case LinkChordQuality::Sus2:       return static_cast<uint16_t>(bit(0) | bit(2) | bit(7));
        case LinkChordQuality::Sus4:       return static_cast<uint16_t>(bit(0) | bit(5) | bit(7));
        case LinkChordQuality::Maj6:       return static_cast<uint16_t>(bit(0) | bit(4) | bit(7) | bit(9));
        case LinkChordQuality::Min6:       return static_cast<uint16_t>(bit(0) | bit(3) | bit(7) | bit(9));
        case LinkChordQuality::Dom9:       return static_cast<uint16_t>(bit(0) | bit(2) | bit(4) | bit(7) | bit(10));
        case LinkChordQuality::Maj9:       return static_cast<uint16_t>(bit(0) | bit(2) | bit(4) | bit(7) | bit(11));
        case LinkChordQuality::Min9:       return static_cast<uint16_t>(bit(0) | bit(2) | bit(3) | bit(7) | bit(10));
        case LinkChordQuality::Dom11:      return static_cast<uint16_t>(bit(0) | bit(2) | bit(4) | bit(5) | bit(7) | bit(10));
        case LinkChordQuality::Min11:      return static_cast<uint16_t>(bit(0) | bit(2) | bit(3) | bit(5) | bit(7) | bit(10));
        case LinkChordQuality::Dom13:      return static_cast<uint16_t>(bit(0) | bit(2) | bit(4) | bit(7) | bit(9) | bit(10));
        case LinkChordQuality::Maj13:      return static_cast<uint16_t>(bit(0) | bit(2) | bit(4) | bit(7) | bit(9) | bit(11));
        case LinkChordQuality::Min13:      return static_cast<uint16_t>(bit(0) | bit(2) | bit(3) | bit(7) | bit(9) | bit(10));
        case LinkChordQuality::Dom7Sharp9: return static_cast<uint16_t>(bit(0) | bit(3) | bit(4) | bit(7) | bit(10));
        case LinkChordQuality::Dom7Flat9:  return static_cast<uint16_t>(bit(0) | bit(1) | bit(4) | bit(7) | bit(10));
        case LinkChordQuality::Power5:     return static_cast<uint16_t>(bit(0) | bit(7));
        case LinkChordQuality::Add9:       return static_cast<uint16_t>(bit(0) | bit(2) | bit(4) | bit(7));
    }
    return 0u;
}

inline constexpr const char* chordQualitySuffix(LinkChordQuality quality) noexcept {
    switch (quality) {
        case LinkChordQuality::None:       return "";
        case LinkChordQuality::Major:      return "";
        case LinkChordQuality::Minor:      return "m";
        case LinkChordQuality::Dom7:       return "7";
        case LinkChordQuality::Maj7:       return "maj7";
        case LinkChordQuality::Min7:       return "m7";
        case LinkChordQuality::Min7b5:     return "m7b5";
        case LinkChordQuality::Dim:        return "dim";
        case LinkChordQuality::Dim7:       return "dim7";
        case LinkChordQuality::Aug:        return "aug";
        case LinkChordQuality::Sus2:       return "sus2";
        case LinkChordQuality::Sus4:       return "sus4";
        case LinkChordQuality::Maj6:       return "6";
        case LinkChordQuality::Min6:       return "m6";
        case LinkChordQuality::Dom9:       return "9";
        case LinkChordQuality::Maj9:       return "maj9";
        case LinkChordQuality::Min9:       return "m9";
        case LinkChordQuality::Dom11:      return "11";
        case LinkChordQuality::Min11:      return "m11";
        case LinkChordQuality::Dom13:      return "13";
        case LinkChordQuality::Maj13:      return "maj13";
        case LinkChordQuality::Min13:      return "m13";
        case LinkChordQuality::Dom7Sharp9: return "7#9";
        case LinkChordQuality::Dom7Flat9:  return "7b9";
        case LinkChordQuality::Power5:     return "5";
        case LinkChordQuality::Add9:       return "add9";
    }
    return "";
}

inline constexpr const char* pitchClassName(int8_t pc, bool preferFlats = false) noexcept {
    constexpr const char* kSharps[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    constexpr const char* kFlats[12]  = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    if (pc < 0 || pc >= 12) return "?";
    return preferFlats ? kFlats[pc] : kSharps[pc];
}

inline constexpr uint16_t modePitchClassMask(int8_t rootPc, LinkMode mode) noexcept {
    constexpr auto bit = [](unsigned pc) constexpr -> uint16_t {
        return static_cast<uint16_t>(1u << (pc % 12u));
    };
    uint16_t base = 0u;
    switch (mode) {
        case LinkMode::Ionian:          base = static_cast<uint16_t>(bit(0)|bit(2)|bit(4)|bit(5)|bit(7)|bit(9)|bit(11)); break;
        case LinkMode::Dorian:          base = static_cast<uint16_t>(bit(0)|bit(2)|bit(3)|bit(5)|bit(7)|bit(9)|bit(10)); break;
        case LinkMode::Phrygian:        base = static_cast<uint16_t>(bit(0)|bit(1)|bit(3)|bit(5)|bit(7)|bit(8)|bit(10)); break;
        case LinkMode::Lydian:          base = static_cast<uint16_t>(bit(0)|bit(2)|bit(4)|bit(6)|bit(7)|bit(9)|bit(11)); break;
        case LinkMode::Mixolydian:      base = static_cast<uint16_t>(bit(0)|bit(2)|bit(4)|bit(5)|bit(7)|bit(9)|bit(10)); break;
        case LinkMode::Aeolian:         base = static_cast<uint16_t>(bit(0)|bit(2)|bit(3)|bit(5)|bit(7)|bit(8)|bit(10)); break;
        case LinkMode::Locrian:         base = static_cast<uint16_t>(bit(0)|bit(1)|bit(3)|bit(5)|bit(6)|bit(8)|bit(10)); break;
        case LinkMode::HarmonicMinor:   base = static_cast<uint16_t>(bit(0)|bit(2)|bit(3)|bit(5)|bit(7)|bit(8)|bit(11)); break;
        case LinkMode::MelodicMinor:    base = static_cast<uint16_t>(bit(0)|bit(2)|bit(3)|bit(5)|bit(7)|bit(9)|bit(11)); break;
        case LinkMode::Altered:         base = static_cast<uint16_t>(bit(0)|bit(1)|bit(3)|bit(4)|bit(6)|bit(8)|bit(10)); break;
        case LinkMode::PentatonicMajor: base = static_cast<uint16_t>(bit(0)|bit(2)|bit(4)|bit(7)|bit(9)); break;
        case LinkMode::PentatonicMinor: base = static_cast<uint16_t>(bit(0)|bit(3)|bit(5)|bit(7)|bit(10)); break;
        case LinkMode::Blues:           base = static_cast<uint16_t>(bit(0)|bit(3)|bit(5)|bit(6)|bit(7)|bit(10)); break;
    }
    if (rootPc < 0 || rootPc >= 12) return base;
    return rotatePitchClassMask(base, rootPc);
}

inline constexpr const char* modeName(LinkMode mode) noexcept {
    switch (mode) {
        case LinkMode::Ionian:          return "Ionian";
        case LinkMode::Dorian:          return "Dorian";
        case LinkMode::Phrygian:        return "Phrygian";
        case LinkMode::Lydian:          return "Lydian";
        case LinkMode::Mixolydian:      return "Mixolydian";
        case LinkMode::Aeolian:         return "Aeolian";
        case LinkMode::Locrian:         return "Locrian";
        case LinkMode::HarmonicMinor:   return "Harmonic Minor";
        case LinkMode::MelodicMinor:    return "Melodic Minor";
        case LinkMode::Altered:         return "Altered";
        case LinkMode::PentatonicMajor: return "Major Pentatonic";
        case LinkMode::PentatonicMinor: return "Minor Pentatonic";
        case LinkMode::Blues:           return "Blues";
    }
    return "Ionian";
}

inline constexpr const char* busLabel(int busIndex) noexcept {
    constexpr const char* kLabels[kNumBuses] = { "A", "B", "C", "D", "E", "F", "G", "H" };
    if (busIndex < 0 || busIndex >= kNumBuses) return "Off";
    return kLabels[busIndex];
}

// ============================================================================
// Core POD Structs (Trivially Copyable for Cross-Process Shared Memory)
// ============================================================================
struct LinkChord {
    int8_t           rootPc{ -1 };
    int8_t           bassPc{ -1 };
    LinkChordQuality quality{ LinkChordQuality::None };
    uint8_t          confidence{ 0 };
    uint16_t         pitchClassMask{ 0 };
    uint16_t         durationBeatsQ8{ 1024 }; // beats * 256 (default 4.0 beats)

    constexpr bool isValid() const noexcept {
        return rootPc >= 0 && rootPc < 12 && quality != LinkChordQuality::None;
    }

    constexpr bool operator==(const LinkChord&) const noexcept = default;
};

inline constexpr LinkChord makeLinkChord(int8_t rootPc,
                                         LinkChordQuality quality,
                                         int8_t bassPc = -1,
                                         uint8_t confidence = 100,
                                         uint16_t durationBeatsQ8 = 1024) noexcept {
    LinkChord c{};
    if (rootPc >= 0 && rootPc < 12 && quality != LinkChordQuality::None) {
        c.rootPc          = rootPc;
        c.bassPc          = (bassPc >= 0 && bassPc < 12) ? bassPc : -1;
        c.quality         = quality;
        c.confidence      = confidence;
        c.pitchClassMask  = rotatePitchClassMask(chordQualityPitchMask(quality), rootPc);
        if (c.bassPc >= 0) {
            c.pitchClassMask = static_cast<uint16_t>(c.pitchClassMask | (1u << static_cast<unsigned>(c.bassPc)));
        }
        c.durationBeatsQ8 = durationBeatsQ8;
    }
    return c;
}

inline size_t formatLinkChord(const LinkChord& chord, char* buf, size_t len) noexcept {
    if (!buf || len == 0) return 0;
    if (!chord.isValid()) {
        const int n = std::snprintf(buf, len, "N.C.");
        return (n > 0) ? static_cast<size_t>(n < static_cast<int>(len) ? n : static_cast<int>(len - 1)) : 0u;
    }
    const char* rootStr = pitchClassName(chord.rootPc, false);
    const char* sufStr  = chordQualitySuffix(chord.quality);
    int n = 0;
    if (chord.bassPc >= 0 && chord.bassPc < 12 && chord.bassPc != chord.rootPc) {
        n = std::snprintf(buf, len, "%s%s/%s", rootStr, sufStr, pitchClassName(chord.bassPc, false));
    } else {
        n = std::snprintf(buf, len, "%s%s", rootStr, sufStr);
    }
    if (n <= 0) {
        buf[0] = '\0';
        return 0u;
    }
    return static_cast<size_t>(n < static_cast<int>(len) ? n : static_cast<int>(len - 1));
}

struct NoteMask128 {
    uint64_t lo{ 0 }; // MIDI notes 0..63
    uint64_t hi{ 0 }; // MIDI notes 64..127

    constexpr void clearAll() noexcept {
        lo = 0;
        hi = 0;
    }

    constexpr void set(int midi) noexcept {
        if (midi < 0 || midi > 127) return;
        if (midi < 64) lo |= (uint64_t{ 1 } << midi);
        else           hi |= (uint64_t{ 1 } << (midi - 64));
    }

    constexpr void clear(int midi) noexcept {
        if (midi < 0 || midi > 127) return;
        if (midi < 64) lo &= ~(uint64_t{ 1 } << midi);
        else           hi &= ~(uint64_t{ 1 } << (midi - 64));
    }

    constexpr bool test(int midi) const noexcept {
        if (midi < 0 || midi > 127) return false;
        if (midi < 64) return (lo & (uint64_t{ 1 } << midi)) != 0;
        return (hi & (uint64_t{ 1 } << (midi - 64))) != 0;
    }

    constexpr bool any() const noexcept {
        return (lo | hi) != 0;
    }

    constexpr void orWith(const NoteMask128& o) noexcept {
        lo |= o.lo;
        hi |= o.hi;
    }

    constexpr uint16_t pitchClassMask12() const noexcept {
        uint16_t mask = 0;
        uint64_t m = lo;
        for (int n = 0; m != 0 && n < 64; ++n, m >>= 1) {
            if ((m & 1ull) != 0ull) mask = static_cast<uint16_t>(mask | (1u << (n % 12)));
        }
        m = hi;
        for (int n = 64; m != 0 && n < 128; ++n, m >>= 1) {
            if ((m & 1ull) != 0ull) mask = static_cast<uint16_t>(mask | (1u << (n % 12)));
        }
        return mask;
    }

    constexpr bool operator==(const NoteMask128&) const noexcept = default;
};

// ============================================================================
// Bidirectional Seqlock Payloads
// ============================================================================
struct HudToBandPayload {
    uint64_t     heartbeatMs{ 0 };
    uint32_t     hudInstanceId{ 0 };
    uint32_t     flags{ 0 };

    LinkChord    liveChord{};
    int8_t       keyRootPc{ 0 };
    uint8_t      keyIsMinor{ 0 };
    uint8_t      keyConfidence{ 0 };
    LinkMode     activeMode{ LinkMode::Ionian };

    uint16_t     scalePitchClassMask{ 0 };
    uint16_t     avoidPitchClassMask{ 0 };
    uint16_t     clashPitchClassMask{ 0 };
    uint16_t     reserved0{ 0 };

    float        chroma[12]{};
    NoteMask128  userActiveNotes{};

    float        userActivityLevel{ 0.0f };
    float        userEnergy{ 0.0f };
    uint8_t      userHasSignal{ 0 };
    uint8_t      clashCount{ 0 };
    uint8_t      clashSeverity{ 0 };
    uint8_t      autoPushLiveChords{ 0 };

    uint32_t     reharmSeq{ 0 };
    LinkChord    reharmChord{};
    ReharmTiming reharmTiming{ ReharmTiming::NextBar };
    uint8_t      reharmLockBars{ 1 };
    uint16_t     reserved1{ 0 };

    uint32_t     chartSeq{ 0 };
    uint8_t      chartNumBars{ 0 };
    uint8_t      reserved2[3]{};
    LinkChord    chartBars[kMaxChartBars]{};
};

struct BandToHudPayload {
    uint64_t           heartbeatMs{ 0 };
    uint32_t           bandInstanceId{ 0 };
    uint32_t           flags{ 0 };

    LinkChord          activeChord{};
    LinkChord          nextChord{};
    float              beatsUntilNextChord{ 0.0f };

    double             bpm{ 120.0 };
    double             ppqPosition{ 0.0 };
    int32_t            currentBar{ 0 };
    int32_t            currentBeat{ 0 };
    float              beatInBar{ 0.0f };

    int8_t             activeChartBarIndex{ -1 };
    uint8_t            timeSigNum{ 4 };
    uint8_t            timeSigDen{ 4 };
    uint8_t            isPlaying{ 0 };
    uint8_t            songSection{ 0 };
    uint8_t            fillActive{ 0 };
    uint8_t            styleMorphPct{ 0 };
    int8_t             keyRootPc{ 0 };
    uint8_t            keyIsMinor{ 0 };
    HarmonicSourceMode harmonicSourceMode{ HarmonicSourceMode::Chart };
    uint8_t            clashAvoidanceActive{ 0 };
    uint8_t            reharmPendingActive{ 0 };
    uint8_t            reserved3{ 0 };

    char               styleName[48]{};
    char               sectionLabel[16]{};

    NoteMask128        slotNotes[kNumBandSlots]{};
    uint8_t            slotActive[kNumBandSlots]{};
    uint8_t            reserved4[2]{};

    uint32_t           bandChartSeq{ 0 };
    uint8_t            bandChartNumBars{ 0 };
    uint8_t            reserved5[3]{};
    LinkChord          bandChartBars[kMaxChartBars]{};
};

static_assert(std::is_trivially_copyable_v<LinkChord>, "LinkChord must be trivially copyable");
static_assert(std::is_trivially_copyable_v<NoteMask128>, "NoteMask128 must be trivially copyable");
static_assert(std::is_trivially_copyable_v<HudToBandPayload>, "HudToBandPayload must be trivially copyable");
static_assert(std::is_trivially_copyable_v<BandToHudPayload>, "BandToHudPayload must be trivially copyable");
static_assert(sizeof(LinkChord) == 8, "LinkChord must be compact 8-byte POD");
static_assert(sizeof(NoteMask128) == 16, "NoteMask128 must be 16-byte POD");

// ============================================================================
// Shared Memory Region with Dual Independent Seqlocks
// ============================================================================
namespace detail {

inline void cpuPause() noexcept {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    _mm_pause();
#elif defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#endif
}

template <typename PayloadT>
struct SeqlockChannel {
    static_assert(std::is_trivially_copyable_v<PayloadT>, "PayloadT must be trivially copyable");
    static_assert((sizeof(PayloadT) % sizeof(uint64_t)) == 0, "PayloadT size must be a multiple of 8 bytes");
    static constexpr size_t kWordCount = sizeof(PayloadT) / sizeof(uint64_t);

    alignas(64) std::atomic<uint64_t> seq{ 0 }; // even = stable, odd = write in progress
    alignas(8) mutable uint64_t       words[kWordCount]{};
};

struct SharedMemoryRegion {
    alignas(64) std::atomic<uint32_t> magic{ 0 };
    std::atomic<uint32_t>             version{ 0 };
    std::atomic<uint32_t>             initState{ 0 }; // 0 = fresh, 1 = initializing, 2 = ready
    uint32_t                          reservedHeader{ 0 };

    SeqlockChannel<HudToBandPayload>  hudToBand;
    SeqlockChannel<BandToHudPayload>  bandToHud;
};

static_assert(std::atomic<uint64_t>::is_always_lock_free, "Seqlock requires lock-free 64-bit atomics");
static_assert(std::atomic<uint32_t>::is_always_lock_free, "Header init requires lock-free 32-bit atomics");
static_assert(std::atomic_ref<uint64_t>::is_always_lock_free, "Seqlock words require lock-free 64-bit atomic_ref");

inline bool ensureRegionInitialized(SharedMemoryRegion* region) noexcept {
    if (!region) return false;
    uint32_t state = region->initState.load(std::memory_order_acquire);
    if (state == 2u) {
        return region->magic.load(std::memory_order_relaxed) == kLinkMagic &&
               region->version.load(std::memory_order_relaxed) == kLinkVersion;
    }

    uint32_t expected = 0u;
    if (region->initState.compare_exchange_strong(expected, 1u, std::memory_order_acq_rel)) {
        region->hudToBand.seq.store(0ull, std::memory_order_relaxed);
        for (auto& w : region->hudToBand.words) std::atomic_ref<uint64_t>(w).store(0ull, std::memory_order_relaxed);
        region->bandToHud.seq.store(0ull, std::memory_order_relaxed);
        for (auto& w : region->bandToHud.words) std::atomic_ref<uint64_t>(w).store(0ull, std::memory_order_relaxed);
        region->magic.store(kLinkMagic, std::memory_order_relaxed);
        region->version.store(kLinkVersion, std::memory_order_relaxed);
        region->initState.store(2u, std::memory_order_release);
        return true;
    }

    for (int i = 0; i < 4096; ++i) {
        if (region->initState.load(std::memory_order_acquire) == 2u) {
            return region->magic.load(std::memory_order_relaxed) == kLinkMagic &&
                   region->version.load(std::memory_order_relaxed) == kLinkVersion;
        }
        cpuPause();
    }
    return false;
}

template <typename PayloadT>
inline bool seqlockWrite(SeqlockChannel<PayloadT>& channel, const PayloadT& payload) noexcept {
    alignas(8) uint64_t localWords[SeqlockChannel<PayloadT>::kWordCount]{};
    std::memcpy(localWords, &payload, sizeof(PayloadT));

    uint64_t s = channel.seq.load(std::memory_order_relaxed);
    for (int tries = 0; tries < 64; ++tries) {
        if ((s & 1ull) != 0ull) {
            cpuPause();
            s = channel.seq.load(std::memory_order_acquire);
            continue;
        }
        if (channel.seq.compare_exchange_weak(s, s + 1ull, std::memory_order_acq_rel, std::memory_order_relaxed)) {
            std::atomic_thread_fence(std::memory_order_release);
            for (size_t i = 0; i < SeqlockChannel<PayloadT>::kWordCount; ++i) {
                std::atomic_ref<uint64_t>(channel.words[i]).store(localWords[i], std::memory_order_relaxed);
            }
            channel.seq.store(s + 2ull, std::memory_order_release);
            return true;
        }
    }
    return false;
}

template <typename PayloadT>
inline bool seqlockRead(const SeqlockChannel<PayloadT>& channel, PayloadT& out) noexcept {
    alignas(8) uint64_t localWords[SeqlockChannel<PayloadT>::kWordCount]{};
    for (int tries = 0; tries < 16; ++tries) {
        const uint64_t s1 = channel.seq.load(std::memory_order_acquire);
        if (s1 == 0ull) return false; // Never published yet
        if ((s1 & 1ull) != 0ull) {
            cpuPause();
            continue;
        }
        for (size_t i = 0; i < SeqlockChannel<PayloadT>::kWordCount; ++i) {
            localWords[i] = std::atomic_ref<uint64_t>(channel.words[i]).load(std::memory_order_relaxed);
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        const uint64_t s2 = channel.seq.load(std::memory_order_acquire);
        if (s1 == s2) {
            std::memcpy(&out, localWords, sizeof(PayloadT));
            return true;
        }
    }
    return false;
}

inline uint32_t nextInstanceId(uint64_t nowMsVal) noexcept {
    static std::atomic<uint32_t> sCounter{ 1u };
    const uint32_t seq = sCounter.fetch_add(1u, std::memory_order_relaxed);
#ifdef _WIN32
    const uint32_t pid = static_cast<uint32_t>(::GetCurrentProcessId());
#else
    const uint32_t pid = 0x5A17u;
#endif
    uint32_t id = (pid * 2654435761u) ^ (seq * 2246822519u) ^ static_cast<uint32_t>(nowMsVal & 0xFFFFu);
    return (id == 0u) ? 1u : id;
}

#ifndef _WIN32
inline SharedMemoryRegion* acquireInProcessRegion(const std::string& name) {
    static std::mutex sRegMutex;
    static std::map<std::string, std::unique_ptr<SharedMemoryRegion>> sRegions;
    std::lock_guard<std::mutex> lock(sRegMutex);
    auto& ptr = sRegions[name];
    if (!ptr) {
        ptr = std::make_unique<SharedMemoryRegion>();
        ensureRegionInitialized(ptr.get());
    }
    return ptr.get();
}
#endif

} // namespace detail

// ============================================================================
// HarmonyLinkEndpoint (Lock-Free RT Audio Safe Bus Client)
// ============================================================================
class HarmonyLinkEndpoint {
public:
    HarmonyLinkEndpoint() noexcept
        : instanceId_(detail::nextInstanceId(nowMs())) {}

    ~HarmonyLinkEndpoint() {
        activeBus_.store(-1, std::memory_order_release);
        activeView_.store(nullptr, std::memory_order_release);
        closeAllMappings();
    }

    HarmonyLinkEndpoint(const HarmonyLinkEndpoint&) = delete;
    HarmonyLinkEndpoint& operator=(const HarmonyLinkEndpoint&) = delete;

    static uint64_t nowMs() noexcept {
#ifdef _WIN32
        return static_cast<uint64_t>(::GetTickCount64());
#else
        using namespace std::chrono;
        return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
#endif
    }

    // Call from message thread / prepareToPlay (never unmaps live views while audio thread may hold them)
    bool connect(int busIndex, const char* customSuffix = nullptr) {
        std::lock_guard<std::mutex> lock(connectMutex_);

        if (busIndex < 0 || busIndex >= kNumBuses) {
            activeBus_.store(-1, std::memory_order_release);
            activeView_.store(nullptr, std::memory_order_release);
            return false;
        }

        const bool hasSuffix = (customSuffix != nullptr && customSuffix[0] != '\0');
        const std::string name = makeMappingName(busIndex, customSuffix);

        if (!hasSuffix) {
            auto& slot = standardSlots_[static_cast<size_t>(busIndex)];
            if (!slot.view) {
                if (!openMapping(name, slot)) {
                    return false;
                }
            }
            activeView_.store(slot.view, std::memory_order_release);
            activeBus_.store(busIndex, std::memory_order_release);
            return true;
        }

        for (auto& extra : extraSlots_) {
            if (extra.name == name && extra.view != nullptr) {
                activeView_.store(extra.view, std::memory_order_release);
                activeBus_.store(busIndex, std::memory_order_release);
                return true;
            }
        }

        MappingSlot newSlot{};
        if (!openMapping(name, newSlot)) {
            return false;
        }
        extraSlots_.push_back(newSlot);
        activeView_.store(newSlot.view, std::memory_order_release);
        activeBus_.store(busIndex, std::memory_order_release);
        return true;
    }

    void disconnect() noexcept {
        activeBus_.store(-1, std::memory_order_release);
        activeView_.store(nullptr, std::memory_order_release);
    }

    bool isConnected() const noexcept {
        return activeView_.load(std::memory_order_acquire) != nullptr &&
               activeBus_.load(std::memory_order_acquire) >= 0;
    }

    int currentBus() const noexcept {
        return activeBus_.load(std::memory_order_acquire);
    }

    uint32_t instanceId() const noexcept {
        return instanceId_;
    }

    // Real-time safe: zero allocations, zero mutexes, zero syscalls
    bool publishHud(const HudToBandPayload& payload) noexcept {
        auto* region = activeView_.load(std::memory_order_acquire);
        if (!region) return false;
        HudToBandPayload copy = payload;
        if (copy.heartbeatMs == 0ull)   copy.heartbeatMs = nowMs();
        if (copy.hudInstanceId == 0u)   copy.hudInstanceId = instanceId_;
        return detail::seqlockWrite(region->hudToBand, copy);
    }

    bool readHud(HudToBandPayload& out, bool& peerAlive, uint64_t timeoutMs = kHeartbeatTimeoutMs) const noexcept {
        peerAlive = false;
        auto* region = activeView_.load(std::memory_order_acquire);
        if (!region) return false;
        if (!detail::seqlockRead(region->hudToBand, out)) return false;
        peerAlive = isTimestampAlive(out.heartbeatMs, timeoutMs);
        return true;
    }

    bool publishBand(const BandToHudPayload& payload) noexcept {
        auto* region = activeView_.load(std::memory_order_acquire);
        if (!region) return false;
        BandToHudPayload copy = payload;
        if (copy.heartbeatMs == 0ull)    copy.heartbeatMs = nowMs();
        if (copy.bandInstanceId == 0u)   copy.bandInstanceId = instanceId_;
        return detail::seqlockWrite(region->bandToHud, copy);
    }

    bool readBand(BandToHudPayload& out, bool& peerAlive, uint64_t timeoutMs = kHeartbeatTimeoutMs) const noexcept {
        peerAlive = false;
        auto* region = activeView_.load(std::memory_order_acquire);
        if (!region) return false;
        if (!detail::seqlockRead(region->bandToHud, out)) return false;
        peerAlive = isTimestampAlive(out.heartbeatMs, timeoutMs);
        return true;
    }

private:
    struct MappingSlot {
        std::string                 name;
        void*                       handle{ nullptr };
        detail::SharedMemoryRegion* view{ nullptr };
    };

    static bool isTimestampAlive(uint64_t heartbeatMs, uint64_t timeoutMs) noexcept {
        if (heartbeatMs == 0ull) return false;
        const uint64_t now = nowMs();
        if (now >= heartbeatMs) return (now - heartbeatMs) <= timeoutMs;
        return (heartbeatMs - now) <= timeoutMs;
    }

    static std::string makeMappingName(int busIndex, const char* customSuffix) {
        std::string name = "Local\\PluginSuite_HarmonyLink_v1_Bus" + std::to_string(busIndex);
        if (customSuffix != nullptr && customSuffix[0] != '\0') {
            name += "_";
            name += customSuffix;
        }
        return name;
    }

    static bool openMapping(const std::string& name, MappingSlot& outSlot) {
#ifdef _WIN32
        constexpr DWORD sizeBytes = static_cast<DWORD>(sizeof(detail::SharedMemoryRegion));
        HANDLE hMap = ::CreateFileMappingA(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            0,
            sizeBytes,
            name.c_str()
        );
        if (!hMap) return false;

        void* ptr = ::MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeBytes);
        if (!ptr) {
            ::CloseHandle(hMap);
            return false;
        }

        auto* region = static_cast<detail::SharedMemoryRegion*>(ptr);
        if (!detail::ensureRegionInitialized(region)) {
            ::UnmapViewOfFile(ptr);
            ::CloseHandle(hMap);
            return false;
        }

        outSlot.name   = name;
        outSlot.handle = static_cast<void*>(hMap);
        outSlot.view   = region;
        return true;
#else
        outSlot.name   = name;
        outSlot.handle = nullptr;
        outSlot.view   = detail::acquireInProcessRegion(name);
        return outSlot.view != nullptr;
#endif
    }

    void closeAllMappings() noexcept {
#ifdef _WIN32
        auto closeSlot = [](MappingSlot& s) noexcept {
            if (s.view) {
                ::UnmapViewOfFile(s.view);
                s.view = nullptr;
            }
            if (s.handle) {
                ::CloseHandle(static_cast<HANDLE>(s.handle));
                s.handle = nullptr;
            }
        };
        for (auto& s : standardSlots_) closeSlot(s);
        for (auto& s : extraSlots_)    closeSlot(s);
#endif
        extraSlots_.clear();
    }

    uint32_t                                        instanceId_{ 1u };
    std::mutex                                      connectMutex_;
    std::array<MappingSlot, kNumBuses>              standardSlots_{};
    std::vector<MappingSlot>                        extraSlots_;
    std::atomic<detail::SharedMemoryRegion*>        activeView_{ nullptr };
    std::atomic<int>                                activeBus_{ -1 };
};

} // namespace suite::harmony_link

namespace suite {
namespace link = harmony_link;
} // namespace suite
