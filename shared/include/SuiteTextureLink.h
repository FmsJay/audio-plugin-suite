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

namespace suite::texture_link {

// ============================================================================
// Constants & Flag Bits
// ============================================================================
inline constexpr uint32_t kLinkMagic          = 0x53544C31u; // "STL1"
inline constexpr uint32_t kLinkVersion        = 1u;
inline constexpr int      kNumBuses           = 8;           // Buses 0..7 => A..H
inline constexpr uint64_t kHeartbeatTimeoutMs = 1500ull;

namespace FossilFlags {
    inline constexpr uint32_t kFossilEuclidOn       = 1u << 0;
    inline constexpr uint32_t kFossilHostSynced     = 1u << 1;
    inline constexpr uint32_t kFossilAnyFrozen      = 1u << 2;
    inline constexpr uint32_t kFossilMomentary      = 1u << 3;
    inline constexpr uint32_t kFossilHarmonyLinked  = 1u << 4;
} // namespace FossilFlags

namespace VowelFlags {
    inline constexpr uint32_t kVowelSeqActive        = 1u << 0;
    inline constexpr uint32_t kVowelConsonantClosure = 1u << 1;
    inline constexpr uint32_t kVowelBreathActive     = 1u << 2;
    inline constexpr uint32_t kVowelMidiControlled   = 1u << 3;
    inline constexpr uint32_t kVowelHarmonyLinked    = 1u << 4;
} // namespace VowelFlags

using namespace FossilFlags;
using namespace VowelFlags;

inline constexpr const char* busLabel(int busIndex) noexcept {
    constexpr const char* kLabels[kNumBuses] = { "A", "B", "C", "D", "E", "F", "G", "H" };
    if (busIndex < 0 || busIndex >= kNumBuses) return "Off";
    return kLabels[busIndex];
}

// ============================================================================
// Bidirectional Seqlock Payloads (Trivially Copyable, 8B-Aligned PODs)
// ============================================================================
struct FossilToVowelPayload {
    uint64_t heartbeatMs{ 0 };
    uint32_t fossilInstanceId{ 0 };
    uint32_t flags{ 0 };

    float    energyT{ 0.0f };
    float    energyH{ 0.0f };
    float    energyN{ 0.0f };
    float    transientTrigger{ 0.0f };

    double   bpm{ 120.0 };
    double   ppqPosition{ 0.0 };

    uint32_t euclidPatternMask{ 0 };
    uint32_t euclidStepSeq{ 0 };
    uint32_t euclidHitSeq{ 0 };
    float    euclidEnvelope{ 0.0f };

    uint8_t  euclidStep{ 0 };
    uint8_t  euclidSteps{ 16 };
    uint8_t  euclidHits{ 0 };
    uint8_t  euclidActiveHit{ 0 };

    uint32_t freezeCaptureSeq{ 0 };
    uint8_t  lastCaptureSource{ 0 }; // 0=T, 1=H, 2=N
    uint8_t  frozenStrataMask{ 0 };  // 4-bit mask (L1..L4)
    uint8_t  frozenStrataCount{ 0 };
    uint8_t  reserved0{ 0 };

    float    strataEnergy{ 0.0f };
    float    crossfadePos{ 0.0f };
    float    shimmerSmear{ 0.0f };
    float    dominantPitchSemi{ 0.0f };

    float    strataLevels[4]{};
};

struct VowelToFossilPayload {
    uint64_t heartbeatMs{ 0 };
    uint32_t vowelInstanceId{ 0 };
    uint32_t flags{ 0 };

    float    vowelX{ 0.5f };
    float    vowelY{ 0.5f };
    float    jawOpen{ 0.5f };
    float    throatSize{ 0.0f };
    float    breathAir{ 0.0f };
    float    modEnvelope{ 0.0f };
    float    formantBrightness{ 0.5f };
    float    resonanceQ{ 0.5f };

    float    formantFreqHz[5]{};
    float    formantGain[5]{};

    uint32_t syllableStepSeq{ 0 };
    uint8_t  activeSyllableStep{ 0 };
    uint8_t  activePhonemeId{ 0 };
    uint8_t  activeTractProfile{ 0 };
    uint8_t  modMode{ 0 };

    char     syllableLabel[8]{};
    char     tractLabel[24]{};
};

static_assert(std::is_trivially_copyable_v<FossilToVowelPayload>, "FossilToVowelPayload must be trivially copyable");
static_assert(std::is_trivially_copyable_v<VowelToFossilPayload>, "VowelToFossilPayload must be trivially copyable");
static_assert((sizeof(FossilToVowelPayload) % sizeof(uint64_t)) == 0, "FossilToVowelPayload must be 8-byte aligned");
static_assert((sizeof(VowelToFossilPayload) % sizeof(uint64_t)) == 0, "VowelToFossilPayload must be 8-byte aligned");

// ============================================================================
// Shared Memory Region with Dual Independent Strict-C++20 Seqlocks
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
    alignas(64) std::atomic<uint32_t>      magic{ 0 };
    std::atomic<uint32_t>                  version{ 0 };
    std::atomic<uint32_t>                  initState{ 0 }; // 0 = fresh, 1 = initializing, 2 = ready
    uint32_t                               reservedHeader{ 0 };

    SeqlockChannel<FossilToVowelPayload>   fossilToVowel;
    SeqlockChannel<VowelToFossilPayload>   vowelToFossil;
};

static_assert(std::atomic<uint64_t>::is_always_lock_free, "Seqlock requires lock-free 64-bit atomics");
static_assert(std::atomic<uint32_t>::is_always_lock_free, "Header init requires lock-free 32-bit atomics");
static_assert(std::atomic_ref<uint64_t>::is_always_lock_free, "Seqlock words require lock-free 64-bit atomic_ref");

inline bool ensureRegionInitialized(SharedMemoryRegion* region) noexcept {
    if (!region) return false;
    const uint32_t state = region->initState.load(std::memory_order_acquire);
    if (state == 2u) {
        return region->magic.load(std::memory_order_relaxed) == kLinkMagic &&
               region->version.load(std::memory_order_relaxed) == kLinkVersion;
    }

    uint32_t expected = 0u;
    if (region->initState.compare_exchange_strong(expected, 1u, std::memory_order_acq_rel)) {
        region->fossilToVowel.seq.store(0ull, std::memory_order_relaxed);
        for (auto& w : region->fossilToVowel.words) std::atomic_ref<uint64_t>(w).store(0ull, std::memory_order_relaxed);
        region->vowelToFossil.seq.store(0ull, std::memory_order_relaxed);
        for (auto& w : region->vowelToFossil.words) std::atomic_ref<uint64_t>(w).store(0ull, std::memory_order_relaxed);
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
    const uint32_t pid = 0x7E41u;
#endif
    const uint32_t id = (pid * 2654435761u) ^ (seq * 2246822519u) ^ static_cast<uint32_t>(nowMsVal & 0xFFFFu);
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
// TextureLinkEndpoint (Lock-Free RT Audio Safe Bus Client)
// ============================================================================
class TextureLinkEndpoint {
public:
    TextureLinkEndpoint() noexcept
        : instanceId_(detail::nextInstanceId(nowMs())) {}

    ~TextureLinkEndpoint() {
        activeBus_.store(-1, std::memory_order_release);
        activeView_.store(nullptr, std::memory_order_release);
        closeAllMappings();
    }

    TextureLinkEndpoint(const TextureLinkEndpoint&) = delete;
    TextureLinkEndpoint& operator=(const TextureLinkEndpoint&) = delete;

    static uint64_t nowMs() noexcept {
#ifdef _WIN32
        return static_cast<uint64_t>(::GetTickCount64());
#else
        using namespace std::chrono;
        return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
#endif
    }

    static constexpr const char* busLabel(int busIndex) noexcept {
        return suite::texture_link::busLabel(busIndex);
    }

    // Call from message thread / prepareToPlay (cached views stay mapped until destructor)
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
    bool publishFossil(const FossilToVowelPayload& payload) noexcept {
        auto* region = activeView_.load(std::memory_order_acquire);
        if (!region) return false;
        FossilToVowelPayload copy = payload;
        if (copy.heartbeatMs == 0ull)     copy.heartbeatMs = nowMs();
        if (copy.fossilInstanceId == 0u)  copy.fossilInstanceId = instanceId_;
        return detail::seqlockWrite(region->fossilToVowel, copy);
    }

    bool readFossil(FossilToVowelPayload& out, bool& peerAlive, uint64_t timeoutMs = kHeartbeatTimeoutMs) const noexcept {
        peerAlive = false;
        auto* region = activeView_.load(std::memory_order_acquire);
        if (!region) return false;
        if (!detail::seqlockRead(region->fossilToVowel, out)) return false;
        peerAlive = isTimestampAlive(out.heartbeatMs, timeoutMs);
        return true;
    }

    bool publishVowel(const VowelToFossilPayload& payload) noexcept {
        auto* region = activeView_.load(std::memory_order_acquire);
        if (!region) return false;
        VowelToFossilPayload copy = payload;
        if (copy.heartbeatMs == 0ull)    copy.heartbeatMs = nowMs();
        if (copy.vowelInstanceId == 0u)  copy.vowelInstanceId = instanceId_;
        return detail::seqlockWrite(region->vowelToFossil, copy);
    }

    bool readVowel(VowelToFossilPayload& out, bool& peerAlive, uint64_t timeoutMs = kHeartbeatTimeoutMs) const noexcept {
        peerAlive = false;
        auto* region = activeView_.load(std::memory_order_acquire);
        if (!region) return false;
        if (!detail::seqlockRead(region->vowelToFossil, out)) return false;
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
        std::string name = "Local\\PluginSuite_TextureLink_v1_Bus" + std::to_string(busIndex);
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

    uint32_t                                 instanceId_{ 1u };
    std::mutex                               connectMutex_;
    std::array<MappingSlot, kNumBuses>       standardSlots_{};
    std::vector<MappingSlot>                 extraSlots_;
    std::atomic<detail::SharedMemoryRegion*> activeView_{ nullptr };
    std::atomic<int>                         activeBus_{ -1 };
};

} // namespace suite::texture_link

namespace suite {
namespace texlink = texture_link;
} // namespace suite
