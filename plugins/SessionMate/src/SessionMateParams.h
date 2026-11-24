#pragma once

#include "SessionMateTypes.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace sessionmate {

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Global Transport & Sync
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamID::syncMode, 1), "Sync Mode",
        juce::StringArray { "DAW Sync", "Live Follow", "Tap / Jam" }, 0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamID::manualBpm, 1), "Tempo BPM",
        juce::NormalisableRange<float>(45.0f, 220.0f, 0.1f), 104.0f));

    // Style DNA A <-> B
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID(ParamID::styleA, 1), "Style A", 0, 39, 6));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID(ParamID::styleB, 1), "Style B", 0, 39, 7));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamID::styleMorph, 1), "Style Morph",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.25f));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamID::timeSig, 1), "Time Signature",
        juce::StringArray { "4/4", "3/4", "5/4", "6/8", "7/8", "12/8" }, 0));

    // Song Arranger & Feel
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamID::section, 1), "Song Section",
        juce::StringArray { "Intro", "Verse", "Pre-Ch", "Chorus", "Bridge", "Solo", "Outro" }, 3));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamID::energy, 1), "Energy",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.68f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamID::swing, 1), "Swing",
        juce::NormalisableRange<float>(0.50f, 0.72f, 0.005f), 0.56f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamID::pushPullMs, 1), "Push / Pull (ms)",
        juce::NormalisableRange<float>(-25.0f, 25.0f, 0.1f), 2.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamID::leaveSpace, 1), "Leave Space",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.65f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamID::autoChord, 1), "Auto Detect Chords", false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamID::autoSection, 1), "Auto Section From Energy", false));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamID::keyRoot, 1), "Key Root",
        juce::StringArray { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" }, 4));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamID::keyMinor, 1), "Minor Key", true));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamID::halfTime, 1), "Half Time", false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamID::doubleTime, 1), "Double Time", false));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID(ParamID::masterVolDb, 1), "Master Output dB",
        juce::NormalisableRange<float>(-36.0f, 6.0f, 0.1f), -2.0f));

    // HarmonyHUD Link & Expanded Voicing Controls
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamID::harmonicSource, 1), "Harmonic Source",
        juce::StringArray { "Chart", "Live Audio", "HUD Link" }, 0));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamID::linkBus, 1), "HarmonyHUD Link Bus",
        juce::StringArray { "Link: Off", "Link: Bus A", "Link: Bus B", "Link: Bus C",
                            "Link: Bus D", "Link: Bus E", "Link: Bus F", "Link: Bus G", "Link: Bus H" }, 1));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID(ParamID::clashAvoid, 1), "Auto Clash Avoidance", true));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID(ParamID::keysVoicing, 1), "Keys Voicing",
        juce::StringArray { "Close", "Drop-2", "Rootless", "Shell 3-7", "Open Spread" }, 0));

    // Per-Slot Parameters (6 Band Members)
    const float defaultVols[kNumSlots]     = { 0.0f, -1.5f, -3.5f, -5.0f, -6.5f, -5.5f };
    const float defaultPans[kNumSlots]     = { 0.0f,  0.0f, -0.22f, 0.26f, -0.35f, 0.28f };
    const float defaultBusy[kNumSlots]     = { 0.72f, 0.68f, 0.62f, 0.58f, 0.50f, 0.45f };
    const float defaultKickLock[kNumSlots] = { 0.50f, 0.85f, 0.55f, 0.50f, 0.50f, 0.75f };
    const float defaultReverb[kNumSlots]   = { 0.14f, 0.04f, 0.24f, 0.20f, 0.18f, 0.30f };

    for (int s = 0; s < kNumSlots; ++s) {
        const juce::String pfx = "Slot " + juce::String(s + 1) + " ";

        params.push_back(std::make_unique<juce::AudioParameterInt>(
            juce::ParameterID(ParamID::slotParam(s, "patch"), 1), pfx + "Patch",
            0, numPatchesForSlot(s) - 1, 0));

        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID(ParamID::slotParam(s, "drummap"), 1), pfx + "Drum Map",
            juce::StringArray { "General MIDI", "Toontrack SD3/EZD", "Addictive Drums 2",
                                "SSD 5", "GetGood Drums", "BFD3", "Custom Map" }, 0));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(ParamID::slotParam(s, "busy"), 1), pfx + "Busy",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), defaultBusy[s]));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(ParamID::slotParam(s, "kicklock"), 1), pfx + "KickLock/Space",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), defaultKickLock[s]));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(ParamID::slotParam(s, "vol"), 1), pfx + "Volume dB",
            juce::NormalisableRange<float>(-60.0f, 6.0f, 0.1f), defaultVols[s]));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(ParamID::slotParam(s, "pan"), 1), pfx + "Pan",
            juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), defaultPans[s]));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(ParamID::slotParam(s, "reverb"), 1), pfx + "Reverb",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), defaultReverb[s]));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(ParamID::slotParam(s, "tilt"), 1), pfx + "Tilt EQ",
            juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f));

        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID(ParamID::slotParam(s, "mute"), 1), pfx + "Mute", false));

        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID(ParamID::slotParam(s, "solo"), 1), pfx + "Solo", false));

        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID(ParamID::slotParam(s, "lock"), 1), pfx + "Pattern Lock", false));

        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID(ParamID::slotParam(s, "callresp"), 1), pfx + "Call & Response", s == 5));
    }

    return { params.begin(), params.end() };
}

} // namespace sessionmate
