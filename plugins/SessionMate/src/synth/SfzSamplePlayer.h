#pragma once

#include "../SessionMateTypes.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace sessionmate {

class SfzSamplePlayer {
public:
    SfzSamplePlayer() = default;
    ~SfzSamplePlayer() {
        activeBank_.store(nullptr, std::memory_order_release);
    }

    SfzSamplePlayer(const SfzSamplePlayer&) = delete;
    SfzSamplePlayer& operator=(const SfzSamplePlayer&) = delete;

    void prepare(double sampleRate) noexcept {
        sampleRate_ = (sampleRate > 8000.0 && sampleRate < 384000.0) ? sampleRate : 48000.0;
        invSr_ = 1.0f / static_cast<float>(sampleRate_);
        reset();
    }

    void reset() noexcept {
        for (auto& v : voices_) v = {};
        sustainPedal_ = false;
        ageCounter_ = 0;
    }

    bool loadFile(const std::string& path, std::string& outError) {
        outError.clear();
        if (path.empty()) {
            outError = "Empty file path";
            return false;
        }

        std::filesystem::path fsPath(path);
        std::error_code ec;
        if (!std::filesystem::exists(fsPath, ec)) {
            outError = "File not found: " + path;
            return false;
        }

        std::string ext = fsPath.extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        auto bank = std::make_unique<SampleBank>();
        bank->name = fsPath.filename().string();

        if (ext == ".wav") {
            auto wav = std::make_shared<WavData>();
            if (!loadWavFile(fsPath, *wav, outError)) {
                return false;
            }
            Region reg {};
            reg.wav = wav;
            reg.lokey = 0;
            reg.hikey = 127;
            reg.pitchKeyCenter = 60;
            reg.lovel = 0;
            reg.hivel = 127;
            reg.gain = 1.0f;
            reg.pan = 0.0f;
            bank->regions.push_back(std::move(reg));
        } else if (ext == ".sfz") {
            if (!parseSfzFile(fsPath, *bank, outError)) {
                return false;
            }
            if (bank->regions.empty()) {
                outError = "No valid playable regions found in SFZ";
                return false;
            }
        } else {
            outError = "Unsupported sample format (expected .sfz or .wav): " + ext;
            return false;
        }

        const SampleBank* rawPtr = bank.get();
        {
            std::lock_guard<std::mutex> lock(bankMutex_);
            if (currentOwnedBank_) {
                retiredBanks_.push_back(std::move(currentOwnedBank_));
            }
            currentOwnedBank_ = std::move(bank);
            loadedName_ = rawPtr->name;
        }
        activeBank_.store(rawPtr, std::memory_order_release);
        return true;
    }

    void clear() {
        activeBank_.store(nullptr, std::memory_order_release);
        std::lock_guard<std::mutex> lock(bankMutex_);
        if (currentOwnedBank_) {
            retiredBanks_.push_back(std::move(currentOwnedBank_));
        }
        loadedName_.clear();
    }

    bool isLoaded() const noexcept {
        return activeBank_.load(std::memory_order_acquire) != nullptr;
    }

    std::string loadedName() const {
        std::lock_guard<std::mutex> lock(bankMutex_);
        return loadedName_;
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

        const SampleBank* bank = activeBank_.load(std::memory_order_acquire);
        if (!bank || bank->regions.empty()) return;

        const int note = static_cast<int>(ev.data1);
        const int vel = static_cast<int>(ev.data2);

        // Trigger matching region(s) (up to 2 layers per note to preserve polyphony)
        int triggered = 0;
        for (const auto& reg : bank->regions) {
            if (!reg.wav || reg.wav->numFrames == 0) continue;
            if (note >= reg.lokey && note <= reg.hikey && vel >= reg.lovel && vel <= reg.hivel) {
                triggerRegion(reg, note, vel);
                if (++triggered >= 2) break;
            }
        }
    }

    void renderSample(float& outL, float& outR) noexcept {
        float sumL = 0.0f;
        float sumR = 0.0f;

        for (auto& v : voices_) {
            if (!v.active || !v.wav) continue;

            const size_t numFrames = v.wav->numFrames;
            if (numFrames == 0) {
                v.active = false;
                continue;
            }

            if (v.pos >= static_cast<double>(numFrames - 1)) {
                v.active = false;
                continue;
            }

            const size_t i0 = static_cast<size_t>(v.pos);
            const size_t i1 = (i0 + 1 < numFrames) ? (i0 + 1) : i0;
            const float frac = static_cast<float>(v.pos - static_cast<double>(i0));

            const float sL = v.wav->left[i0] * (1.0f - frac) + v.wav->left[i1] * frac;
            const float sR = v.wav->isStereo
                ? (v.wav->right[i0] * (1.0f - frac) + v.wav->right[i1] * frac)
                : sL;

            if (v.attackStage) {
                v.env += invSr_ * 400.0f;
                if (v.env >= 1.0f) {
                    v.env = 1.0f;
                    v.attackStage = false;
                }
            } else if (!v.gate) {
                v.env *= v.releaseCoeff;
                if (std::abs(v.env) < 1.0e-20f) v.env = 0.0f;
                if (v.env < 0.0005f) {
                    v.active = false;
                    continue;
                }
            }

            const float g = v.env * v.gain;
            sumL += sL * g * v.panL;
            sumR += sR * g * v.panR;

            v.pos += v.step;
        }

        if (!std::isfinite(sumL)) sumL = 0.0f;
        if (!std::isfinite(sumR)) sumR = 0.0f;
        if (std::abs(sumL) < 1.0e-20f) sumL = 0.0f;
        if (std::abs(sumR) < 1.0e-20f) sumR = 0.0f;
        outL = std::clamp(sumL, -4.0f, 4.0f);
        outR = std::clamp(sumR, -4.0f, 4.0f);
    }

private:
    static constexpr float kPi = 3.14159265358979323846f;

    struct WavData {
        double sampleRate = 48000.0;
        bool isStereo = false;
        size_t numFrames = 0;
        std::vector<float> left;
        std::vector<float> right;
    };

    struct Region {
        std::shared_ptr<WavData> wav;
        int lokey = 0;
        int hikey = 127;
        int pitchKeyCenter = 60;
        int lovel = 0;
        int hivel = 127;
        float gain = 1.0f;
        float pan = 0.0f;
    };

    struct SampleBank {
        std::string name;
        std::vector<Region> regions;
    };

    struct Voice {
        bool active = false;
        bool gate = false;
        bool sustained = false;
        bool attackStage = false;
        uint8_t note = 60;
        uint32_t age = 0;
        const WavData* wav = nullptr;
        double pos = 0.0;
        double step = 1.0;
        float env = 0.0f;
        float releaseCoeff = 0.999f;
        float gain = 1.0f;
        float panL = 1.0f;
        float panR = 1.0f;
    };

    void triggerRegion(const Region& reg, int note, int vel) noexcept {
        Voice* target = nullptr;
        for (auto& v : voices_) {
            if (!v.active) {
                target = &v;
                break;
            }
        }
        if (!target) {
            target = &voices_[0];
            for (auto& v : voices_) {
                if (v.age < target->age) target = &v;
            }
        }

        const double semitoneDiff = static_cast<double>(note - reg.pitchKeyCenter);
        const double pitchRatio = std::pow(2.0, semitoneDiff / 12.0);
        const double srRatio = reg.wav->sampleRate / sampleRate_;

        target->active = true;
        target->gate = true;
        target->sustained = false;
        target->attackStage = true;
        target->note = static_cast<uint8_t>(note);
        target->age = ++ageCounter_;
        target->wav = reg.wav.get();
        target->pos = 0.0;
        target->step = std::clamp(pitchRatio * srRatio, 0.0625, 16.0);
        target->env = 0.0f;
        target->releaseCoeff = std::exp(-6.907755f / (0.18f * static_cast<float>(sampleRate_)));

        const float velNorm = std::clamp(static_cast<float>(vel) / 127.0f, 0.05f, 1.0f);
        target->gain = reg.gain * velNorm;

        const float p = std::clamp(reg.pan, -1.0f, 1.0f);
        const float angle = (p + 1.0f) * 0.25f * kPi;
        target->panL = std::cos(angle) * 1.41421356f;
        target->panR = std::sin(angle) * 1.41421356f;
    }

    static uint16_t readU16LE(const uint8_t* p) noexcept {
        return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
    }

    static uint32_t readU32LE(const uint8_t* p) noexcept {
        return static_cast<uint32_t>(p[0])
             | (static_cast<uint32_t>(p[1]) << 8)
             | (static_cast<uint32_t>(p[2]) << 16)
             | (static_cast<uint32_t>(p[3]) << 24);
    }

    static bool loadWavFile(const std::filesystem::path& path, WavData& outWav, std::string& outError) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            outError = "Failed to open WAV: " + path.string();
            return false;
        }
        const std::streamsize size = file.tellg();
        if (size < 44 || size > 256 * 1024 * 1024) {
            outError = "Invalid WAV file size: " + path.string();
            return false;
        }
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> buf(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(buf.data()), size)) {
            outError = "Failed to read WAV data: " + path.string();
            return false;
        }

        if (std::memcmp(buf.data(), "RIFF", 4) != 0 || std::memcmp(buf.data() + 8, "WAVE", 4) != 0) {
            outError = "Not a valid RIFF WAVE file: " + path.string();
            return false;
        }

        bool foundFmt = false;
        bool foundData = false;
        uint16_t audioFormat = 0;
        uint16_t numChannels = 0;
        uint32_t sr = 48000;
        uint16_t bitsPerSample = 0;
        size_t dataOffset = 0;
        size_t dataSize = 0;

        size_t offset = 12;
        while (offset + 8 <= buf.size()) {
            const uint8_t* hdr = buf.data() + offset;
            const uint32_t chunkSize = readU32LE(hdr + 4);
            const size_t chunkData = offset + 8;
            if (chunkData + chunkSize > buf.size()) break;

            if (std::memcmp(hdr, "fmt ", 4) == 0 && chunkSize >= 16) {
                audioFormat = readU16LE(buf.data() + chunkData);
                numChannels = readU16LE(buf.data() + chunkData + 2);
                sr = readU32LE(buf.data() + chunkData + 4);
                bitsPerSample = readU16LE(buf.data() + chunkData + 14);
                // Handle WAVE_FORMAT_EXTENSIBLE (0xFFFE)
                if (audioFormat == 0xFFFE && chunkSize >= 40) {
                    audioFormat = readU16LE(buf.data() + chunkData + 24);
                }
                foundFmt = true;
            } else if (std::memcmp(hdr, "data", 4) == 0) {
                dataOffset = chunkData;
                dataSize = chunkSize;
                foundData = true;
            }

            offset = chunkData + chunkSize + (chunkSize & 1u);
        }

        if (!foundFmt || !foundData) {
            outError = "Missing fmt or data chunk in WAV: " + path.string();
            return false;
        }

        if (numChannels < 1 || numChannels > 2) {
            outError = "Only mono/stereo WAV supported";
            return false;
        }

        const bool isPcm16 = (audioFormat == 1 && bitsPerSample == 16);
        const bool isPcm24 = (audioFormat == 1 && bitsPerSample == 24);
        const bool isFloat32 = (audioFormat == 3 && bitsPerSample == 32);

        if (!isPcm16 && !isPcm24 && !isFloat32) {
            outError = "Unsupported WAV encoding (need 16/24-bit PCM or 32-bit float)";
            return false;
        }

        const size_t bytesPerSample = bitsPerSample / 8;
        const size_t frameBytes = bytesPerSample * numChannels;
        if (frameBytes == 0) return false;
        const size_t numFrames = dataSize / frameBytes;
        if (numFrames == 0) {
            outError = "WAV contains 0 audio frames";
            return false;
        }

        outWav.sampleRate = (sr > 4000 && sr < 384000) ? static_cast<double>(sr) : 48000.0;
        outWav.isStereo = (numChannels == 2);
        outWav.numFrames = numFrames;
        outWav.left.resize(numFrames);
        if (outWav.isStereo) outWav.right.resize(numFrames);

        const uint8_t* ptr = buf.data() + dataOffset;
        for (size_t i = 0; i < numFrames; ++i) {
            for (uint16_t ch = 0; ch < numChannels; ++ch) {
                float val = 0.0f;
                if (isPcm16) {
                    const int16_t s = static_cast<int16_t>(readU16LE(ptr));
                    val = static_cast<float>(s) * (1.0f / 32768.0f);
                } else if (isPcm24) {
                    int32_t s = static_cast<int32_t>(ptr[0])
                              | (static_cast<int32_t>(ptr[1]) << 8)
                              | (static_cast<int32_t>(ptr[2]) << 16);
                    if (s & 0x00800000) s |= 0xFF000000;
                    val = static_cast<float>(s) * (1.0f / 8388608.0f);
                } else {
                    std::memcpy(&val, ptr, sizeof(float));
                    if (!std::isfinite(val)) val = 0.0f;
                }
                if (ch == 0) outWav.left[i] = val;
                else outWav.right[i] = val;
                ptr += bytesPerSample;
            }
        }

        return true;
    }

    static std::string trim(const std::string& s) {
        size_t a = 0;
        while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
        size_t b = s.size();
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
        return s.substr(a, b - a);
    }

    static int parseMidiNoteValue(const std::string& raw, int fallback) {
        std::string s = trim(raw);
        if (s.empty()) return fallback;
        // Pure integer?
        bool allDigits = true;
        size_t idx = (s[0] == '-' || s[0] == '+') ? 1 : 0;
        if (idx >= s.size()) allDigits = false;
        for (size_t i = idx; i < s.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(s[i]))) {
                allDigits = false;
                break;
            }
        }
        if (allDigits) {
            try { return std::clamp(std::stoi(s), 0, 127); } catch (...) { return fallback; }
        }
        // Note name e.g. c3, f#4, bb2
        const char letter = static_cast<char>(std::tolower(static_cast<unsigned char>(s[0])));
        int base = -1;
        switch (letter) {
            case 'c': base = 0; break;
            case 'd': base = 2; break;
            case 'e': base = 4; break;
            case 'f': base = 5; break;
            case 'g': base = 7; break;
            case 'a': base = 9; break;
            case 'b': base = 11; break;
            default: return fallback;
        }
        size_t pos = 1;
        if (pos < s.size()) {
            if (s[pos] == '#') { base += 1; ++pos; }
            else if (s[pos] == 'b' || s[pos] == 'B') { base -= 1; ++pos; }
        }
        try {
            const int oct = std::stoi(s.substr(pos));
            return std::clamp((oct + 1) * 12 + base, 0, 127);
        } catch (...) {
            return fallback;
        }
    }

    struct RawRegion {
        std::string sample;
        int lokey = 0;
        int hikey = 127;
        int pitchKeyCenter = 60;
        bool hasKeyCenter = false;
        int lovel = 0;
        int hivel = 127;
        float volumeDb = 0.0f;
        float pan = 0.0f;
    };

    static void applyOpcode(const std::string& key, const std::string& val, RawRegion& r) {
        if (key == "sample") {
            r.sample = trim(val);
        } else if (key == "lokey") {
            r.lokey = parseMidiNoteValue(val, r.lokey);
        } else if (key == "hikey") {
            r.hikey = parseMidiNoteValue(val, r.hikey);
        } else if (key == "key") {
            const int k = parseMidiNoteValue(val, 60);
            r.lokey = k;
            r.hikey = k;
            r.pitchKeyCenter = k;
            r.hasKeyCenter = true;
        } else if (key == "pitch_keycenter") {
            r.pitchKeyCenter = parseMidiNoteValue(val, r.pitchKeyCenter);
            r.hasKeyCenter = true;
        } else if (key == "lovel") {
            try { r.lovel = std::clamp(std::stoi(val), 0, 127); } catch (...) {}
        } else if (key == "hivel") {
            try { r.hivel = std::clamp(std::stoi(val), 0, 127); } catch (...) {}
        } else if (key == "volume") {
            try { r.volumeDb = std::clamp(std::stof(val), -60.0f, 24.0f); } catch (...) {}
        } else if (key == "pan") {
            try { r.pan = std::clamp(std::stof(val) * 0.01f, -1.0f, 1.0f); } catch (...) {}
        }
    }

    static bool parseSfzFile(const std::filesystem::path& sfzPath, SampleBank& bank, std::string& outError) {
        std::ifstream file(sfzPath);
        if (!file.is_open()) {
            outError = "Failed to open SFZ: " + sfzPath.string();
            return false;
        }

        const std::filesystem::path baseDir = sfzPath.parent_path();
        std::string defaultPath;

        enum class Section { None, Control, Group, Region };
        Section section = Section::None;
        RawRegion groupDefaults {};
        RawRegion currentRegion {};
        bool hasRegion = false;

        std::vector<RawRegion> rawRegions;
        auto flushRegion = [&]() {
            if (hasRegion && !currentRegion.sample.empty()) {
                rawRegions.push_back(currentRegion);
            }
            hasRegion = false;
        };

        std::string line;
        while (std::getline(file, line)) {
            const size_t commentPos = line.find("//");
            if (commentPos != std::string::npos) {
                line.erase(commentPos);
            }
            line = trim(line);
            if (line.empty()) continue;

            // Scan line for <headers> and key=value opcodes
            size_t pos = 0;
            while (pos < line.size()) {
                while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) ++pos;
                if (pos >= line.size()) break;

                if (line[pos] == '<') {
                    const size_t close = line.find('>', pos);
                    if (close == std::string::npos) break;
                    std::string tag = line.substr(pos + 1, close - pos - 1);
                    for (char& c : tag) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                    if (tag == "control") {
                        flushRegion();
                        section = Section::Control;
                    } else if (tag == "group") {
                        flushRegion();
                        section = Section::Group;
                        groupDefaults = RawRegion {};
                    } else if (tag == "region") {
                        flushRegion();
                        section = Section::Region;
                        currentRegion = groupDefaults;
                        hasRegion = true;
                    } else {
                        flushRegion();
                        section = Section::None;
                    }
                    pos = close + 1;
                    continue;
                }

                const size_t eq = line.find('=', pos);
                if (eq == std::string::npos) break;
                std::string key = trim(line.substr(pos, eq - pos));
                for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                size_t valStart = eq + 1;
                size_t valEnd = line.size();

                // For sample= or default_path=, value may contain spaces until next known opcode or <tag> or EOL
                if (key == "sample" || key == "default_path") {
                    size_t scan = valStart;
                    while (scan < line.size()) {
                        if (line[scan] == '<') {
                            valEnd = scan;
                            break;
                        }
                        if (line[scan] == '=') {
                            // Backtrack to whitespace before the next opcode name
                            size_t ws = scan;
                            while (ws > valStart && !std::isspace(static_cast<unsigned char>(line[ws - 1]))) --ws;
                            if (ws > valStart) {
                                valEnd = ws;
                                break;
                            }
                        }
                        ++scan;
                    }
                } else {
                    size_t scan = valStart;
                    while (scan < line.size() && !std::isspace(static_cast<unsigned char>(line[scan])) && line[scan] != '<') {
                        ++scan;
                    }
                    valEnd = scan;
                }

                const std::string val = trim(line.substr(valStart, valEnd - valStart));
                pos = valEnd;

                if (section == Section::Control && key == "default_path") {
                    defaultPath = val;
                } else if (section == Section::Group) {
                    applyOpcode(key, val, groupDefaults);
                } else if (section == Section::Region) {
                    applyOpcode(key, val, currentRegion);
                }
            }
        }
        flushRegion();

        // Deduplicate WAV loads across regions
        std::unordered_map<std::string, std::shared_ptr<WavData>> wavCache;
        for (const auto& rr : rawRegions) {
            std::string rel = defaultPath + rr.sample;
            std::replace(rel.begin(), rel.end(), '\\', '/');
            std::filesystem::path fullPath = baseDir / std::filesystem::path(rel);
            const std::string cacheKey = fullPath.lexically_normal().string();

            std::shared_ptr<WavData> wav;
            auto it = wavCache.find(cacheKey);
            if (it != wavCache.end()) {
                wav = it->second;
            } else {
                auto loaded = std::make_shared<WavData>();
                std::string wavErr;
                if (!loadWavFile(fullPath, *loaded, wavErr)) {
                    continue; // Skip missing/unreadable region gracefully
                }
                wavCache[cacheKey] = loaded;
                wav = loaded;
            }

            Region reg {};
            reg.wav = wav;
            reg.lokey = std::clamp(rr.lokey, 0, 127);
            reg.hikey = std::clamp(rr.hikey, reg.lokey, 127);
            reg.pitchKeyCenter = rr.hasKeyCenter ? std::clamp(rr.pitchKeyCenter, 0, 127) : reg.lokey;
            reg.lovel = std::clamp(rr.lovel, 0, 127);
            reg.hivel = std::clamp(rr.hivel, reg.lovel, 127);
            reg.gain = std::pow(10.0f, rr.volumeDb / 20.0f);
            reg.pan = rr.pan;
            bank.regions.push_back(std::move(reg));
        }

        return true;
    }

    double sampleRate_ = 48000.0;
    float invSr_ = 1.0f / 48000.0f;
    bool sustainPedal_ = false;
    uint32_t ageCounter_ = 0;
    std::array<Voice, 16> voices_ {};

    std::atomic<const SampleBank*> activeBank_ { nullptr };
    mutable std::mutex bankMutex_;
    std::unique_ptr<SampleBank> currentOwnedBank_;
    std::vector<std::unique_ptr<SampleBank>> retiredBanks_;
    std::string loadedName_;
};

} // namespace sessionmate
