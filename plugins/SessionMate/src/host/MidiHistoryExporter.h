#pragma once

#include "../SessionMateTypes.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace sessionmate {

class MidiHistoryExporter {
public:
    static constexpr size_t kRingCapacity = 8192;
    static constexpr int kTicksPerQuarterNote = 960;

    MidiHistoryExporter() = default;

    // LOCK-FREE, ALLOCATION-FREE SPSC PUSH FROM AUDIO THREAD
    bool pushEventFromAudioThread(const BandNoteEvent& ev) noexcept {
        const size_t w = writeIndex.load(std::memory_order_relaxed);
        const size_t next = (w + 1) & kRingMask;
        if (next == readIndex.load(std::memory_order_acquire))
            return false;

        ringBuffer[w] = ev;
        writeIndex.store(next, std::memory_order_release);
        return true;
    }

    // Called on UI/message thread or before export
    void drainFromAudioThread(TimeSignature ts = { 4, 4 }) {
        std::lock_guard<std::mutex> lock(historyMutex);

        size_t r = readIndex.load(std::memory_order_relaxed);
        const size_t w = writeIndex.load(std::memory_order_acquire);

        bool addedAny = false;
        while (r != w) {
            const BandNoteEvent& ev = ringBuffer[r];
            history.push_back(ev);
            if (!hasLatestPpq || ev.ppqPosition > latestPpq) {
                latestPpq = ev.ppqPosition;
                hasLatestPpq = true;
            }
            r = (r + 1) & kRingMask;
            addedAny = true;
        }
        readIndex.store(r, std::memory_order_release);

        if (!addedAny)
            return;

        const double barLen = std::max(0.25, ts.beatsPerBarQuarter());
        const double cutoffPpq = latestPpq - static_cast<double>(kMaxBarsHistory) * barLen;

        auto removeIt = std::remove_if(history.begin(), history.end(),
            [cutoffPpq](const BandNoteEvent& ev) {
                return ev.ppqPosition < cutoffPpq;
            });
        history.erase(removeIt, history.end());

        // Guard against unbounded memory growth in extreme edge cases
        constexpr size_t kMaxHistoryEvents = 65536;
        if (history.size() > kMaxHistoryEvents) {
            const size_t excess = history.size() - kMaxHistoryEvents;
            history.erase(history.begin(), history.begin() + static_cast<std::ptrdiff_t>(excess));
        }
    }

    void clear() {
        // Consume any pending ring items from the consumer side
        const size_t w = writeIndex.load(std::memory_order_acquire);
        readIndex.store(w, std::memory_order_release);

        std::lock_guard<std::mutex> lock(historyMutex);
        history.clear();
        latestPpq = 0.0;
        hasLatestPpq = false;
    }

    int getRecordedEventCount() const {
        std::lock_guard<std::mutex> lock(historyMutex);
        return static_cast<int>(history.size());
    }

    int getRecordedBarCount(TimeSignature ts) const {
        std::lock_guard<std::mutex> lock(historyMutex);
        if (history.empty())
            return 0;

        double minPpq = history.front().ppqPosition;
        double maxPpq = minPpq;
        for (const auto& ev : history) {
            minPpq = std::min(minPpq, ev.ppqPosition);
            maxPpq = std::max(maxPpq, ev.ppqPosition);
        }

        const double barLen = std::max(0.25, ts.beatsPerBarQuarter());
        const double span = std::max(0.0, maxPpq - minPpq);
        const int bars = static_cast<int>(std::floor(span / barLen)) + 1;
        return std::clamp(bars, 1, kMaxBarsHistory);
    }

    bool exportToMidiFile(const juce::File& targetFile, int slotFilter, double bpm, TimeSignature ts) const {
        std::vector<BandNoteEvent> filtered;
        {
            std::lock_guard<std::mutex> lock(historyMutex);
            filtered.reserve(history.size());
            for (const auto& ev : history) {
                if (ev.slot >= static_cast<uint8_t>(kNumSlots))
                    continue;
                if (slotFilter == -1 || static_cast<int>(ev.slot) == slotFilter)
                    filtered.push_back(ev);
            }
        }

        if (filtered.empty())
            return false;

        double minPpq = filtered.front().ppqPosition;
        for (const auto& ev : filtered)
            minPpq = std::min(minPpq, ev.ppqPosition);

        juce::MidiFile midiFile;
        midiFile.setTicksPerQuarterNote(kTicksPerQuarterNote);

        // Track 0: Tempo & Time Signature meta events
        {
            juce::MidiMessageSequence metaTrack;
            const double safeBpm = std::clamp(bpm, 20.0, 300.0);
            const int microsPerQuarter = static_cast<int>(std::lround(60000000.0 / safeBpm));
            metaTrack.addEvent(juce::MidiMessage::tempoMetaEvent(microsPerQuarter), 0.0);
            metaTrack.addEvent(juce::MidiMessage::timeSignatureMetaEvent(ts.numerator, ts.denominator), 0.0);
            metaTrack.addEvent(juce::MidiMessage::textMetaEvent(3, "SessionMate Tempo"), 0.0);
            midiFile.addTrack(metaTrack);
        }

        const int firstSlot = (slotFilter >= 0 && slotFilter < kNumSlots) ? slotFilter : 0;
        const int lastSlot  = (slotFilter >= 0 && slotFilter < kNumSlots) ? slotFilter : (kNumSlots - 1);

        for (int slot = firstSlot; slot <= lastSlot; ++slot) {
            std::vector<BandNoteEvent> slotEvents;
            for (const auto& ev : filtered) {
                if (static_cast<int>(ev.slot) == slot)
                    slotEvents.push_back(ev);
            }
            if (slotEvents.empty())
                continue;

            std::stable_sort(slotEvents.begin(), slotEvents.end(),
                [](const BandNoteEvent& a, const BandNoteEvent& b) {
                    return a.ppqPosition < b.ppqPosition;
                });

            juce::MidiMessageSequence seq;
            const int channel = slotMidiChannel(slot);
            const juce::String trackName = juce::String(slot + 1) + ". " +
                                           juce::String(slotName(static_cast<SlotId>(slot)));
            seq.addEvent(juce::MidiMessage::textMetaEvent(3, trackName), 0.0);

            std::array<bool, 128> activeNotes {};
            double maxTick = 0.0;

            for (const auto& ev : slotEvents) {
                const double tick = std::max(0.0, (ev.ppqPosition - minPpq) * static_cast<double>(kTicksPerQuarterNote));
                maxTick = std::max(maxTick, tick);

                switch (ev.type) {
                    case EventType::NoteOn: {
                        const int note = std::clamp(static_cast<int>(ev.data1), 0, 127);
                        const juce::uint8 vel = static_cast<juce::uint8>(std::clamp(static_cast<int>(ev.data2), 1, 127));
                        if (activeNotes[static_cast<size_t>(note)]) {
                            const double closeTick = std::max(0.0, tick - 1.0);
                            seq.addEvent(juce::MidiMessage::noteOff(channel, note), closeTick);
                        }
                        seq.addEvent(juce::MidiMessage::noteOn(channel, note, vel), tick);
                        activeNotes[static_cast<size_t>(note)] = true;
                        break;
                    }

                    case EventType::NoteOff: {
                        const int note = std::clamp(static_cast<int>(ev.data1), 0, 127);
                        seq.addEvent(juce::MidiMessage::noteOff(channel, note), tick);
                        activeNotes[static_cast<size_t>(note)] = false;
                        break;
                    }

                    case EventType::ControlChange: {
                        const int ccNum = std::clamp(static_cast<int>(ev.data1), 0, 127);
                        const int ccVal = std::clamp(static_cast<int>(ev.data2), 0, 127);
                        seq.addEvent(juce::MidiMessage::controllerEvent(channel, ccNum, ccVal), tick);
                        break;
                    }

                    case EventType::PitchBend: {
                        const int bend14 = ((static_cast<int>(ev.data2) & 0x7f) << 7) |
                                           (static_cast<int>(ev.data1) & 0x7f);
                        seq.addEvent(juce::MidiMessage::pitchWheel(channel, std::clamp(bend14, 0, 16383)), tick);
                        break;
                    }
                }
            }

            // Close any dangling NoteOns cleanly at track end
            const double endTick = maxTick + static_cast<double>(kTicksPerQuarterNote) * 0.25;
            for (int note = 0; note < 128; ++note) {
                if (activeNotes[static_cast<size_t>(note)]) {
                    seq.addEvent(juce::MidiMessage::noteOff(channel, note), endTick);
                    activeNotes[static_cast<size_t>(note)] = false;
                }
            }

            seq.sort();
            seq.updateMatchedPairs();
            midiFile.addTrack(seq);
        }

        targetFile.getParentDirectory().createDirectory();
        juce::FileOutputStream outStream(targetFile);
        if (!outStream.openedOk())
            return false;

        outStream.setPosition(0);
        outStream.truncate();
        return midiFile.writeTo(outStream);
    }

    juce::File createDragTempMidiFile(int slotFilter, double bpm, TimeSignature ts) const {
        const juce::File dragDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("SessionMateDrag");
        dragDir.createDirectory();

        juce::String slotTag = "FullBand";
        if (slotFilter >= 0 && slotFilter < kNumSlots) {
            slotTag = juce::String(slotName(static_cast<SlotId>(slotFilter)))
                .replaceCharacter('/', '_')
                .replaceCharacter(' ', '_');
        }

        const juce::String timestamp = juce::String(juce::Time::getCurrentTime().toMilliseconds());
        const juce::File outFile = dragDir.getChildFile("SessionMate_" + slotTag + "_" + timestamp + ".mid");

        if (!exportToMidiFile(outFile, slotFilter, bpm, ts))
            return {};

        return outFile;
    }

private:
    static constexpr size_t kRingMask = kRingCapacity - 1;
    static_assert((kRingCapacity & (kRingCapacity - 1)) == 0, "kRingCapacity must be a power of 2");

    std::array<BandNoteEvent, kRingCapacity> ringBuffer {};
    std::atomic<size_t> writeIndex { 0 };
    std::atomic<size_t> readIndex { 0 };

    mutable std::mutex historyMutex;
    std::vector<BandNoteEvent> history;
    double latestPpq = 0.0;
    bool hasLatestPpq = false;
};

} // namespace sessionmate
