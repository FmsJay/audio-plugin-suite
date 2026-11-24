#pragma once

#include "../SessionMateTypes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace sessionmate {

class DrumMapTranslator {
public:
    DrumMapTranslator() noexcept {
        resetCustomMapping();
    }

    void resetCustomMapping() noexcept {
        for (size_t i = 0; i < customMap.size(); ++i)
            customMap[i] = static_cast<uint8_t>(i);
    }

    void setCustomMapping(int sourceNote, int targetNote) noexcept {
        if (sourceNote < 0 || sourceNote > 127)
            return;
        const int clampedTarget = std::clamp(targetNote, 0, 127);
        customMap[static_cast<size_t>(sourceNote)] = static_cast<uint8_t>(clampedTarget);
    }

    int getCustomMapping(int sourceNote) const noexcept {
        if (sourceNote < 0 || sourceNote > 127)
            return std::clamp(sourceNote, 0, 127);
        return static_cast<int>(customMap[static_cast<size_t>(sourceNote)]);
    }

    uint8_t mapVelocity(uint8_t vel, float velocityCurve) const noexcept {
        const int clampedVel = std::clamp(static_cast<int>(vel), 1, 127);
        if (std::abs(velocityCurve - 1.0f) < 1.0e-4f)
            return static_cast<uint8_t>(clampedVel);

        const float curve = (velocityCurve > 0.0f) ? std::clamp(velocityCurve, 0.25f, 4.0f) : 1.0f;
        const float norm = static_cast<float>(clampedVel) / 127.0f;
        const float curved = std::pow(norm, curve);
        const int mapped = static_cast<int>(std::lround(curved * 127.0f));
        return static_cast<uint8_t>(std::clamp(mapped, 1, 127));
    }

    int translateEvent(const BandNoteEvent& inEv, const SlotConfig& cfg, BandNoteEvent outEvents[2]) const noexcept {
        if (inEv.type == EventType::ControlChange) {
            if (inEv.data1 == 64 && !cfg.cc64SustainEnabled)
                return 0;
            if (inEv.data1 == 4 && !cfg.cc4HiHatEnabled)
                return 0;
            outEvents[0] = inEv;
            return 1;
        }

        if (inEv.type == EventType::PitchBend) {
            outEvents[0] = inEv;
            return 1;
        }

        BandNoteEvent ev = inEv;

        if (inEv.type == EventType::NoteOn) {
            ev.data2 = mapVelocity(inEv.data2, cfg.velocityCurve);
        }

        if (inEv.slot == static_cast<uint8_t>(SlotId::Drums)) {
            const int srcNote = static_cast<int>(inEv.data1);
            const bool isHat = isHiHatEvent(srcNote, inEv.articulation);

            ev.data1 = static_cast<uint8_t>(std::clamp(
                mapDrumNote(srcNote, inEv.articulation, cfg.drumMap), 0, 127));

            // Toontrack SD3 ghost snare uses center snare with softer velocity scaling
            if (cfg.drumMap == DrumMapPreset::ToontrackSD3 &&
                inEv.type == EventType::NoteOn &&
                srcNote == DrumNote::Snare &&
                inEv.articulation == Articulation::Ghost) {
                const int ghostVel = static_cast<int>(std::lround(static_cast<float>(ev.data2) * 0.72f));
                ev.data2 = static_cast<uint8_t>(std::clamp(ghostVel, 1, 127));
            }

            if (inEv.type == EventType::NoteOn && isHat && cfg.cc4HiHatEnabled) {
                BandNoteEvent cc4Ev {};
                cc4Ev.sampleOffset = inEv.sampleOffset;
                cc4Ev.slot = inEv.slot;
                cc4Ev.type = EventType::ControlChange;
                cc4Ev.data1 = 4;
                cc4Ev.data2 = hiHatCc4Value(srcNote, inEv.articulation);
                cc4Ev.articulation = inEv.articulation;
                cc4Ev.ppqPosition = inEv.ppqPosition;

                outEvents[0] = cc4Ev;
                outEvents[1] = ev;
                return 2;
            }
        }

        outEvents[0] = ev;
        return 1;
    }

private:
    static bool isHiHatEvent(int note, Articulation art) noexcept {
        if (note == DrumNote::ClosedHat || note == DrumNote::OpenHat || note == DrumNote::PedalHat)
            return true;
        return art == Articulation::HatOpen ||
               art == Articulation::HatHalfOpen ||
               art == Articulation::HatClosed ||
               art == Articulation::HatPedal;
    }

    static uint8_t hiHatCc4Value(int note, Articulation art) noexcept {
        switch (art) {
            case Articulation::HatPedal:    return 127;
            case Articulation::HatClosed:   return 120;
            case Articulation::HatHalfOpen: return 64;
            case Articulation::HatOpen:     return 12;
            default: break;
        }
        if (note == DrumNote::PedalHat)  return 127;
        if (note == DrumNote::ClosedHat) return 120;
        if (note == DrumNote::OpenHat)   return 12;
        return 120;
    }

    int mapDrumNote(int gmNote, Articulation art, DrumMapPreset preset) const noexcept {
        switch (preset) {
            case DrumMapPreset::GeneralMIDI:
                return gmNote;

            case DrumMapPreset::ToontrackSD3:
                return mapToontrackSD3(gmNote, art);

            case DrumMapPreset::AddictiveDrums2:
                return mapAddictiveDrums2(gmNote, art);

            case DrumMapPreset::StevenSlate5:
                return mapStevenSlate5(gmNote, art);

            case DrumMapPreset::GetGoodDrums:
                return mapGetGoodDrums(gmNote, art);

            case DrumMapPreset::BFD3:
                return mapBFD3(gmNote, art);

            case DrumMapPreset::Custom:
                return getCustomMapping(gmNote);
        }
        return gmNote;
    }

    static int mapToontrackSD3(int gmNote, Articulation art) noexcept {
        switch (gmNote) {
            case DrumNote::Kick:         return 36;
            case DrumNote::SideStick:    return 37;
            case DrumNote::Snare:
                if (art == Articulation::Rimshot)   return 40;
                if (art == Articulation::SideStick) return 37;
                return 38;
            case DrumNote::SnareRim:     return 40;
            case DrumNote::ClosedHat:
                if (art == Articulation::HatOpen)   return 46;
                if (art == Articulation::HatPedal)  return 44;
                return 42;
            case DrumNote::PedalHat:     return 44;
            case DrumNote::OpenHat:
                if (art == Articulation::HatClosed) return 42;
                if (art == Articulation::HatPedal)  return 44;
                return 46;
            case DrumNote::HighTom:      return 48;
            case DrumNote::MidTom:       return 47;
            case DrumNote::LowTom:       return 45;
            case DrumNote::HighFloorTom: return 43;
            case DrumNote::LowFloorTom:  return 41;
            case DrumNote::Crash1:       return 49;
            case DrumNote::Crash2:       return 57;
            case DrumNote::Ride:
                return (art == Articulation::RideBell) ? 53 : 51;
            case DrumNote::RideBell:     return 53;
            default:                     return gmNote;
        }
    }

    static int mapAddictiveDrums2(int gmNote, Articulation art) noexcept {
        switch (gmNote) {
            case DrumNote::Kick:         return 36;
            case DrumNote::SideStick:    return 42;
            case DrumNote::Snare:
                if (art == Articulation::Rimshot)   return 37;
                if (art == Articulation::SideStick) return 42;
                if (art == Articulation::Ghost)     return 40;
                return 38;
            case DrumNote::SnareRim:     return 37;
            case DrumNote::ClosedHat:
                if (art == Articulation::HatOpen || art == Articulation::HatHalfOpen) return 54;
                if (art == Articulation::HatPedal)  return 48;
                return 49;
            case DrumNote::PedalHat:     return 48;
            case DrumNote::OpenHat:
                if (art == Articulation::HatClosed) return 49;
                if (art == Articulation::HatPedal)  return 48;
                return 54;
            case DrumNote::HighTom:      return 71;
            case DrumNote::MidTom:       return 69;
            case DrumNote::LowTom:       return 67;
            case DrumNote::HighFloorTom: return 65;
            case DrumNote::LowFloorTom:  return 65;
            case DrumNote::Crash1:       return 77;
            case DrumNote::Crash2:       return 79;
            case DrumNote::Ride:
                return (art == Articulation::RideBell) ? 61 : 60;
            case DrumNote::RideBell:     return 61;
            default:                     return gmNote;
        }
    }

    static int mapStevenSlate5(int gmNote, Articulation art) noexcept {
        switch (gmNote) {
            case DrumNote::Kick:         return 36;
            case DrumNote::SideStick:    return 37;
            case DrumNote::Snare:
                if (art == Articulation::Rimshot)   return 40;
                if (art == Articulation::SideStick) return 37;
                return 38;
            case DrumNote::SnareRim:     return 40;
            case DrumNote::ClosedHat:
                if (art == Articulation::HatOpen)   return 46;
                if (art == Articulation::HatPedal)  return 44;
                return 42;
            case DrumNote::PedalHat:     return 44;
            case DrumNote::OpenHat:
                if (art == Articulation::HatClosed) return 42;
                if (art == Articulation::HatPedal)  return 44;
                return 46;
            case DrumNote::HighTom:      return 48;
            case DrumNote::MidTom:       return 45;
            case DrumNote::LowTom:       return 45;
            case DrumNote::HighFloorTom: return 43;
            case DrumNote::LowFloorTom:  return 41;
            case DrumNote::Crash1:       return 49;
            case DrumNote::Crash2:       return 57;
            case DrumNote::Ride:
                return (art == Articulation::RideBell) ? 53 : 51;
            case DrumNote::RideBell:     return 53;
            default:                     return gmNote;
        }
    }

    static int mapGetGoodDrums(int gmNote, Articulation art) noexcept {
        switch (gmNote) {
            case DrumNote::Kick:         return 36;
            case DrumNote::SideStick:    return 37;
            case DrumNote::Snare:
                if (art == Articulation::Rimshot)   return 40;
                if (art == Articulation::SideStick) return 37;
                return 38;
            case DrumNote::SnareRim:     return 40;
            case DrumNote::ClosedHat:
                if (art == Articulation::HatOpen)   return 46;
                if (art == Articulation::HatPedal)  return 44;
                return 42;
            case DrumNote::PedalHat:     return 44;
            case DrumNote::OpenHat:
                if (art == Articulation::HatClosed) return 42;
                if (art == Articulation::HatPedal)  return 44;
                return 46;
            case DrumNote::HighTom:      return 48;
            case DrumNote::MidTom:       return 47;
            case DrumNote::LowTom:       return 45;
            case DrumNote::HighFloorTom: return 43;
            case DrumNote::LowFloorTom:  return 43;
            case DrumNote::Crash1:       return 49;
            case DrumNote::Crash2:       return 57;
            case DrumNote::Ride:
                return (art == Articulation::RideBell) ? 53 : 51;
            case DrumNote::RideBell:     return 53;
            default:                     return gmNote;
        }
    }

    static int mapBFD3(int gmNote, Articulation art) noexcept {
        switch (gmNote) {
            case DrumNote::Kick:         return 24;
            case DrumNote::SideStick:    return 25;
            case DrumNote::Snare:
                if (art == Articulation::Rimshot)   return 28;
                if (art == Articulation::SideStick) return 25;
                return 26;
            case DrumNote::SnareRim:     return 28;
            case DrumNote::ClosedHat:
                if (art == Articulation::HatOpen)   return 32;
                if (art == Articulation::HatPedal)  return 34;
                return 30;
            case DrumNote::PedalHat:     return 34;
            case DrumNote::OpenHat:
                if (art == Articulation::HatClosed) return 30;
                if (art == Articulation::HatPedal)  return 34;
                return 32;
            case DrumNote::LowFloorTom:  return 41;
            case DrumNote::HighFloorTom: return 43;
            case DrumNote::LowTom:       return 45;
            case DrumNote::MidTom:       return 47;
            case DrumNote::HighTom:      return 47;
            case DrumNote::Crash1:       return 49;
            case DrumNote::Crash2:       return 55;
            case DrumNote::Ride:
                return (art == Articulation::RideBell) ? 53 : 51;
            case DrumNote::RideBell:     return 53;
            default:                     return gmNote;
        }
    }

    std::array<uint8_t, 128> customMap {};
};

} // namespace sessionmate
