#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "../src/FossilParams.h"
#include "../src/PluginProcessor.h"
#include "../src/dsp/EuclideanGate.h"
#include "../src/dsp/HpssDecomposer.h"
#include "../src/dsp/ShimmerDiffusion.h"
#include "../src/dsp/StrataEngine.h"

namespace
{
int gFailures = 0;

void check (bool cond, const std::string& msg)
{
    if (! cond)
    {
        std::cerr << "[FAIL] " << msg << "\n";
        ++gFailures;
    }
    else
    {
        std::cout << "[PASS] " << msg << "\n";
    }
}

bool allFinite (const float* data, int n)
{
    for (int i = 0; i < n; ++i)
        if (! std::isfinite (data[i]))
            return false;
    return true;
}

double computeEnergy (const std::vector<float>& buf, int start, int end)
{
    double sum = 0.0;
    for (int i = start; i < end; ++i)
    {
        const double v = static_cast<double> (buf[static_cast<size_t> (i)]);
        sum += v * v;
    }
    return sum;
}

void testHpssSeparationAndLatency()
{
    std::cout << "\n--- Test 1: HPSS 3-Way Decomposition (T / H / N) & Exact PDC Latency ---\n";
    constexpr double sr = 48000.0;
    constexpr int block = 256;

    // 1a. Exact impulse latency & Transient routing
    {
        fossil::dsp::HpssDecomposer hpss;
        hpss.prepare (sr, block);
        hpss.setParameters (0.75f, 0.45f);
        const int reportedLatency = hpss.getLatencySamples();
        check (reportedLatency > 0 && reportedLatency <= 2048,
               "HPSS reported latency is sane: " + std::to_string (reportedLatency) + " samples");

        const int total = 8192;
        const int impulsePos = 1024;
        std::vector<float> inL (total, 0.0f), inR (total, 0.0f);
        inL[impulsePos] = 1.0f;
        inR[impulsePos] = 1.0f;

        std::vector<float> tL (total), tR (total), hL (total), hR (total), nL (total), nR (total);
        for (int off = 0; off < total; off += block)
        {
            hpss.process (inL.data() + off, inR.data() + off,
                          tL.data() + off, tR.data() + off,
                          hL.data() + off, hR.data() + off,
                          nL.data() + off, nR.data() + off, block);
        }

        check (allFinite (tL.data(), total) && allFinite (hL.data(), total) && allFinite (nL.data(), total),
               "Impulse outputs are all finite");

        // Reconstructed sum peak index must align with impulsePos + reportedLatency
        int peakIdx = 0;
        float peakVal = 0.0f;
        for (int i = 0; i < total; ++i)
        {
            const float sum = std::abs (tL[static_cast<size_t> (i)]
                                      + hL[static_cast<size_t> (i)]
                                      + nL[static_cast<size_t> (i)]);
            if (sum > peakVal)
            {
                peakVal = sum;
                peakIdx = i;
            }
        }
        const int measuredDelay = peakIdx - impulsePos;
        check (std::abs (measuredDelay - reportedLatency) <= 2,
               "Impulse PDC latency exact match: measured=" + std::to_string (measuredDelay)
               + " reported=" + std::to_string (reportedLatency));

        const double eT = computeEnergy (tL, impulsePos, total);
        const double eH = computeEnergy (hL, impulsePos, total);
        const double eN = computeEnergy (nL, impulsePos, total);
        const double eTot = eT + eH + eN + 1e-12;
        const double ratioT = eT / eTot;
        check (ratioT >= 0.70,
               "Impulse -> Transient energy ratio = " + std::to_string (ratioT * 100.0) + "% (>= 70%)");
    }

    // 1b. Pure sine wave -> Tonal (H)
    {
        fossil::dsp::HpssDecomposer hpss;
        hpss.prepare (sr, block);
        hpss.setParameters (0.75f, 0.45f);

        const int total = 24576;
        std::vector<float> inL (total), inR (total);
        for (int i = 0; i < total; ++i)
        {
            const float s = 0.7f * std::sin (2.0f * 3.14159265358979f * 440.0f * static_cast<float> (i) / static_cast<float> (sr));
            inL[static_cast<size_t> (i)] = s;
            inR[static_cast<size_t> (i)] = s;
        }

        std::vector<float> tL (total), tR (total), hL (total), hR (total), nL (total), nR (total);
        for (int off = 0; off < total; off += block)
        {
            hpss.process (inL.data() + off, inR.data() + off,
                          tL.data() + off, tR.data() + off,
                          hL.data() + off, hR.data() + off,
                          nL.data() + off, nR.data() + off, block);
        }

        // Measure steady-state region (skip initial latency + warmup)
        const int steadyStart = 4096;
        const double eT = computeEnergy (tL, steadyStart, total);
        const double eH = computeEnergy (hL, steadyStart, total);
        const double eN = computeEnergy (nL, steadyStart, total);
        const double eTot = eT + eH + eN + 1e-12;
        const double ratioH = eH / eTot;
        check (ratioH >= 0.80,
               "440Hz Sine -> Tonal (H) energy ratio = " + std::to_string (ratioH * 100.0) + "% (>= 80%)");
    }

    // 1c. Stationary white noise -> Air/Noise (N)
    {
        fossil::dsp::HpssDecomposer hpss;
        hpss.prepare (sr, block);
        hpss.setParameters (0.75f, 0.45f);

        const int total = 32768;
        std::vector<float> inL (total), inR (total);
        uint32_t rng = 0x12345678u;
        auto nextFloat = [&rng]() -> float
        {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            return (static_cast<float> (rng & 0xFFFFu) / 32768.0f) - 1.0f;
        };
        for (int i = 0; i < total; ++i)
        {
            inL[static_cast<size_t> (i)] = 0.4f * nextFloat();
            inR[static_cast<size_t> (i)] = 0.4f * nextFloat();
        }

        std::vector<float> tL (total), tR (total), hL (total), hR (total), nL (total), nR (total);
        for (int off = 0; off < total; off += block)
        {
            hpss.process (inL.data() + off, inR.data() + off,
                          tL.data() + off, tR.data() + off,
                          hL.data() + off, hR.data() + off,
                          nL.data() + off, nR.data() + off, block);
        }

        const int steadyStart = 4096;
        const double eT = computeEnergy (tL, steadyStart, total);
        const double eH = computeEnergy (hL, steadyStart, total);
        const double eN = computeEnergy (nL, steadyStart, total);
        const double eTot = eT + eH + eN + 1e-12;
        const double ratioN = eN / eTot;
        check (ratioN >= 0.65,
               "White Noise -> Air/Noise (N) energy ratio = " + std::to_string (ratioN * 100.0) + "% (>= 65%)");
    }
}

void testEuclideanBjorklund()
{
    std::cout << "\n--- Test 2: Euclidean Bjorklund Pattern Math & Gate Envelope ---\n";

    auto countHits = [] (const std::array<bool, fossil::kMaxEuclidSteps>& p, int steps)
    {
        int c = 0;
        for (int i = 0; i < steps; ++i)
            if (p[static_cast<size_t> (i)]) ++c;
        return c;
    };

    // E(8, 3, 0) = 1 0 0 1 0 0 1 0 (Tresillo)
    {
        auto p = fossil::dsp::EuclideanGate::computePattern (8, 3, 0);
        check (countHits (p, 8) == 3, "E(8,3,0) has 3 hits");
        const bool exact = p[0] && !p[1] && !p[2] && p[3] && !p[4] && !p[5] && p[6] && !p[7];
        check (exact, "E(8,3,0) matches Tresillo [1,0,0,1,0,0,1,0]");
    }

    // E(16, 5, 0) has 5 hits with inter-onset intervals strictly in {3, 4}
    {
        auto p = fossil::dsp::EuclideanGate::computePattern (16, 5, 0);
        check (countHits (p, 16) == 5, "E(16,5,0) has 5 hits");
        std::vector<int> idxs;
        for (int i = 0; i < 16; ++i)
            if (p[static_cast<size_t> (i)]) idxs.push_back (i);
        bool evenIntervals = (idxs.size() == 5);
        for (size_t k = 0; k < idxs.size(); ++k)
        {
            const int diff = (k + 1 < idxs.size()) ? (idxs[k + 1] - idxs[k]) : (16 - idxs[k] + idxs[0]);
            if (diff != 3 && diff != 4)
                evenIntervals = false;
        }
        check (evenIntervals, "E(16,5,0) inter-onset spacing strictly in {3, 4}");
    }

    // Rotation check
    {
        auto p0 = fossil::dsp::EuclideanGate::computePattern (8, 3, 0);
        auto p2 = fossil::dsp::EuclideanGate::computePattern (8, 3, 2);
        bool rotOk = true;
        for (int i = 0; i < 8; ++i)
            if (p2[static_cast<size_t> ((i + 2) % 8)] != p0[static_cast<size_t> (i)])
                rotOk = false;
        check (rotOk, "E(8,3,2) rotation shifts pattern right by 2 steps");
    }

    // Edge cases: 0 hits, full hits
    {
        auto pZero = fossil::dsp::EuclideanGate::computePattern (16, 0, 0);
        auto pFull = fossil::dsp::EuclideanGate::computePattern (16, 16, 0);
        check (countHits (pZero, 16) == 0, "E(16,0,0) has 0 hits");
        check (countHits (pFull, 16) == 16, "E(16,16,0) has 16 hits");
    }

    // Envelope continuity & range check
    {
        fossil::dsp::EuclideanGate gate;
        gate.prepare (48000.0);
        gate.configure (true, 16, 5, 0, 0.35f, 0.85f, 2, 120.0f);
        gate.syncBlock (false, 120.0, 0.0);
        float minG = 2.0f, maxG = -1.0f, maxStep = 0.0f;
        float prev = gate.processSample();
        for (int i = 0; i < 48000; ++i)
        {
            const float g = gate.processSample();
            minG = std::min (minG, g);
            maxG = std::max (maxG, g);
            maxStep = std::max (maxStep, std::abs (g - prev));
            prev = g;
        }
        check (std::isfinite (minG) && std::isfinite (maxG) && minG >= 0.10f && maxG <= 1.001f && (maxG - minG) > 0.5f,
               "Euclidean gate smoothly modulates between [" + std::to_string (minG) + ", " + std::to_string (maxG) + "]");
        check (maxStep < 0.05f, "Euclidean gate has no hard single-sample clicks (maxStep=" + std::to_string (maxStep) + ")");
    }
}

void testFreezeAndStrataPlayback()
{
    std::cout << "\n--- Test 3: 4-Strata Freeze Capture, Continuous Playback, Pitch/Drift & Shimmer ---\n";
    constexpr double sr = 48000.0;
    constexpr int block = 256;

    fossil::dsp::StrataEngine strata;
    fossil::dsp::EuclideanGate gate;
    fossil::dsp::ShimmerDiffusion shimmer;
    fossil::FossilTelemetry telemetry;

    strata.prepare (sr, block);
    gate.prepare (sr);
    shimmer.prepare (sr, block);

    fossil::MasterParams params = fossil::getFactoryPresets()[0].params;
    params.euclidEnable = false;

    std::vector<float> tL (block, 0.0f), tR (block, 0.0f);
    std::vector<float> hL (block, 0.0f), hR (block, 0.0f);
    std::vector<float> nL (block, 0.0f), nR (block, 0.0f);
    std::vector<float> outL (block, 0.0f), outR (block, 0.0f);

    // Feed 1.5 seconds of lush chord into H stream
    int phase = 0;
    for (int b = 0; b < 280; ++b)
    {
        for (int i = 0; i < block; ++i)
        {
            const float t = static_cast<float> (phase++) / static_cast<float> (sr);
            const float chord = 0.25f * std::sin (2.0f * 3.14159265f * 261.63f * t)
                              + 0.25f * std::sin (2.0f * 3.14159265f * 329.63f * t)
                              + 0.20f * std::sin (2.0f * 3.14159265f * 392.00f * t);
            hL[static_cast<size_t> (i)] = chord;
            hR[static_cast<size_t> (i)] = chord * 0.95f;
        }
        strata.processBlock (tL.data(), tR.data(), hL.data(), hR.data(), nL.data(), nR.data(),
                             outL.data(), outR.data(), block, params, gate, &telemetry);
    }

    // Capture into L1 (0st) and L2 (+12st)
    const int s0 = strata.captureStream (0, fossil::StreamSource::Tonal, &telemetry);
    const int s1 = strata.captureStream (1, fossil::StreamSource::Tonal, &telemetry);
    check (s0 == 0 && s1 == 1 && strata.isSlotFrozen (0) && strata.isSlotFrozen (1),
           "Captured snapshots cleanly into L1 and L2");

    // Feed SILENCE for 2 seconds and verify continuous frozen playback + no NaNs + no clicks
    std::fill (hL.begin(), hL.end(), 0.0f);
    std::fill (hR.begin(), hR.end(), 0.0f);

    double totalFrozenEnergy = 0.0;
    float maxJump = 0.0f;
    float prevL = 0.0f;
    bool finiteOk = true;

    for (int b = 0; b < 375; ++b) // ~2.0 seconds
    {
        strata.processBlock (tL.data(), tR.data(), hL.data(), hR.data(), nL.data(), nR.data(),
                             outL.data(), outR.data(), block, params, gate, &telemetry);
        shimmer.processBlock (outL.data(), outR.data(), block, 0.55f, 0.50f);

        for (int i = 0; i < block; ++i)
        {
            const float l = outL[static_cast<size_t> (i)];
            const float r = outR[static_cast<size_t> (i)];
            if (! std::isfinite (l) || ! std::isfinite (r))
                finiteOk = false;
            totalFrozenEnergy += static_cast<double> (l * l + r * r);
            maxJump = std::max (maxJump, std::abs (l - prevL));
            prevL = l;
        }
    }

    check (finiteOk, "2.0s continuous frozen playback + Shimmer Smear has zero NaNs/Infs");
    check (totalFrozenEnergy > 50.0,
           "Frozen strata sustain continuous non-zero energy during silent input (E=" + std::to_string (totalFrozenEnergy) + ")");
    check (maxJump < 0.35f,
           "Frozen granular + shimmer playback is click-free (maxJump=" + std::to_string (maxJump) + ")");

    // Swap slots and clear all -> verify clean fade to silence
    strata.swapSlots (0, 1, &telemetry);
    check (strata.isSlotFrozen (0) && strata.isSlotFrozen (1), "swapSlots preserves frozen strata cleanly");

    strata.clearAll (&telemetry);
    for (int b = 0; b < 40; ++b)
    {
        strata.processBlock (tL.data(), tR.data(), hL.data(), hR.data(), nL.data(), nR.data(),
                             outL.data(), outR.data(), block, params, gate, &telemetry);
    }
    double tailEnergy = 0.0;
    for (int i = 0; i < block; ++i)
        tailEnergy += static_cast<double> (outL[static_cast<size_t> (i)] * outL[static_cast<size_t> (i)]);
    check (tailEnergy < 1e-5, "clearAll fades frozen strata smoothly to silence");
}

void testProcessorStabilityAndStateRoundTrip()
{
    std::cout << "\n--- Test 4: Full Processor Multi-SR / Block-Size Stability, PDC & APVTS Round-Trip ---\n";

    // Multi-SR & varying block size stability
    const std::vector<double> sampleRates { 44100.0, 48000.0, 88200.0, 96000.0 };
    const std::vector<int> blockSizes { 32, 64, 128, 256, 512, 1024, 2048 };

    bool allRunsClean = true;
    for (double sr : sampleRates)
    {
        FossilAudioProcessor proc;
        proc.prepareToPlay (sr, 512);
        const int pdc = proc.getLatencySamples();
        if (pdc != proc.getDecompositionLatencySamples() || pdc <= 0)
            allRunsClean = false;

        proc.loadFactoryPreset (3); // Cathedral Shimmer

        for (int bs : blockSizes)
        {
            juce::AudioBuffer<float> buf (2, bs);
            juce::MidiBuffer midi;
            for (int i = 0; i < bs; ++i)
            {
                const float s = 0.4f * std::sin (2.0f * 3.14159265f * 330.0f * static_cast<float> (i) / static_cast<float> (sr));
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
            }
            if (bs == 256)
                midi.addEvent (juce::MidiMessage::noteOn (1, 50, 0.9f), 0); // MIDI Freeze H

            proc.processBlock (buf, midi);

            for (int ch = 0; ch < 2; ++ch)
            {
                const float* p = buf.getReadPointer (ch);
                for (int i = 0; i < bs; ++i)
                {
                    if (! std::isfinite (p[i]) || std::abs (p[i]) > 1.05f)
                        allRunsClean = false;
                }
            }
        }
    }
    check (allRunsClean, "Full FossilAudioProcessor stable across 44.1k-96k & 32-2048 blocks with exact PDC & limiter ceiling");

    // APVTS + Frozen Snapshot state round-trip
    {
        FossilAudioProcessor procA;
        procA.prepareToPlay (48000.0, 256);
        procA.loadFactoryPreset (7); // Euclidean Polyrhythm

        // Feed audio & trigger freeze on L1
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer emptyMidi;
        int idx = 0;
        for (int b = 0; b < 80; ++b)
        {
            for (int i = 0; i < 256; ++i)
            {
                const float v = 0.5f * std::sin (2.0f * 3.14159265f * 220.0f * static_cast<float> (idx++) / 48000.0f);
                buf.setSample (0, i, v);
                buf.setSample (1, i, v);
            }
            procA.processBlock (buf, emptyMidi);
        }
        procA.triggerFreeze (fossil::StreamSource::Tonal, 0);
        procA.processBlock (buf, emptyMidi);

        juce::MemoryBlock stateBlob;
        procA.getStateInformation (stateBlob);
        check (stateBlob.getSize() > 100, "getStateInformation serialized APVTS + frozen stratum (" + std::to_string (stateBlob.getSize()) + " bytes)");

        FossilAudioProcessor procB;
        procB.setStateInformation (stateBlob.getData(), static_cast<int> (stateBlob.getSize()));
        procB.prepareToPlay (48000.0, 256);

        const float stepsA = procA.getAPVTS().getRawParameterValue (fossil::param_ids::euclidSteps)->load();
        const float stepsB = procB.getAPVTS().getRawParameterValue (fossil::param_ids::euclidSteps)->load();
        const float pitchA = procA.getAPVTS().getRawParameterValue (fossil::param_ids::layerPitch[1])->load();
        const float pitchB = procB.getAPVTS().getRawParameterValue (fossil::param_ids::layerPitch[1])->load();
        check (std::abs (stepsA - stepsB) < 1e-4f && std::abs (pitchA - pitchB) < 1e-4f,
               "APVTS parameters round-tripped accurately");

        // Feed silence into procB and verify restored frozen stratum produces audio!
        buf.clear();
        double restoredEnergy = 0.0;
        for (int b = 0; b < 30; ++b)
        {
            buf.clear();
            procB.processBlock (buf, emptyMidi);
            for (int i = 0; i < 256; ++i)
                restoredEnergy += static_cast<double> (buf.getSample (0, i) * buf.getSample (0, i));
        }
        check (restoredEnergy > 0.1,
               "Restored frozen stratum in procB plays continuous audio (E=" + std::to_string (restoredEnergy) + ")");
    }
}

void testSuiteTextureAndHarmonyLinks()
{
    std::cout << "\n--- Test 5: SuiteTextureLink (VowelMorph) & SuiteHarmonyLink (Harmonic Quantize) ---\n";

    // 5a. Harmonic Quantize unit math
    {
        const uint16_t minTriad = suite::harmony_link::chordQualityPitchMask (suite::harmony_link::LinkChordQuality::Minor);
        const uint16_t majTriad = suite::harmony_link::chordQualityPitchMask (suite::harmony_link::LinkChordQuality::Major);

        check (FossilAudioProcessor::quantizeSemitoneToMask (4.0f, minTriad) == 3.0f,
               "Harmonic Quantize: +4st snaps to +3st on Minor chord");
        check (FossilAudioProcessor::quantizeSemitoneToMask (3.0f, majTriad) == 4.0f,
               "Harmonic Quantize: +3st snaps to +4st on Major chord");
        check (FossilAudioProcessor::quantizeSemitoneToMask (16.0f, minTriad) == 15.0f,
               "Harmonic Quantize: +16st snaps to +15st on Minor chord");
        check (FossilAudioProcessor::quantizeSemitoneToMask (12.0f, minTriad) == 12.0f,
               "Harmonic Quantize: +12st octave preserved");
        check (FossilAudioProcessor::quantizeSemitoneToMask (0.0f, minTriad) == 0.0f,
               "Harmonic Quantize: 0st unison preserved");
    }

    // 5b. Bidirectional SuiteTextureLink loopback (Fossil <-> Mock VowelMorph)
    {
        const char* suffix = "FossilUnitTexLoop";
        FossilAudioProcessor proc;
        proc.prepareToPlay (48000.0, 256);
        proc.loadFactoryPreset (0); // Geological Pad (Euclid 5/16 ON)
        proc.connectTextureLinkBus (0, suffix);

        suite::texlink::TextureLinkEndpoint mockVowel;
        check (mockVowel.connect (0, suffix), "Mock VowelMorph connected to isolated TextureLink Bus A");

        // Publish bright EE VowelMorph payload
        suite::texlink::VowelToFossilPayload v2f {};
        v2f.flags             = suite::texlink::kVowelSeqActive | suite::texlink::kVowelBreathActive;
        v2f.vowelX            = 0.92f;
        v2f.vowelY            = 0.88f;
        v2f.jawOpen           = 0.70f;
        v2f.throatSize        = 0.15f;
        v2f.breathAir         = 0.35f;
        v2f.modEnvelope       = 0.85f;
        v2f.formantBrightness = 0.90f;
        v2f.resonanceQ        = 0.65f;
        std::snprintf (v2f.syllableLabel, sizeof (v2f.syllableLabel), "EE");
        std::snprintf (v2f.tractLabel,    sizeof (v2f.tractLabel),    "Alto Choir");
        check (mockVowel.publishVowel (v2f), "Mock VowelMorph published VowelToFossilPayload");

        // Feed audio, trigger Freeze H, and process ~0.5s so Euclid steps & freeze publish cleanly
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer emptyMidi;
        int sampleIdx = 0;
        for (int b = 0; b < 95; ++b)
        {
            for (int i = 0; i < 256; ++i)
            {
                const float s = 0.55f * std::sin (2.0f * 3.14159265f * 440.0f * static_cast<float> (sampleIdx++) / 48000.0f);
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
            }
            if (b == 60)
                proc.triggerFreeze (fossil::StreamSource::Tonal, 0);
            proc.processBlock (buf, emptyMidi);
        }

        const auto& tel = proc.getTelemetry();
        const float cfHigh = tel.effectiveCrossfade.load();
        check (tel.vowelPeerAlive.load(), "Fossil detected live VowelMorph peer on TextureLink");
        check (cfHigh > 0.65f,
               "VowelMorph vowelX=0.92 swept Fossil effectiveCrossfade upward (" + std::to_string (cfHigh) + " > 0.65)");

        // Now sweep VowelMorph to dark low vowelX=0.05 and verify crossfade sweeps downward!
        v2f.vowelX = 0.05f;
        v2f.formantBrightness = 0.12f;
        v2f.modEnvelope = 0.10f;
        std::snprintf (v2f.syllableLabel, sizeof (v2f.syllableLabel), "OO");
        mockVowel.publishVowel (v2f);
        for (int b = 0; b < 25; ++b)
            proc.processBlock (buf, emptyMidi);

        const float cfLow = tel.effectiveCrossfade.load();
        check (cfLow < 0.35f && cfLow < cfHigh - 0.30f,
               "VowelMorph vowelX=0.05 swept Fossil effectiveCrossfade downward (" + std::to_string (cfLow) + " < 0.35)");

        // Read back Fossil -> VowelMorph payload
        suite::texlink::FossilToVowelPayload f2v {};
        bool fossilAlive = false;
        check (mockVowel.readFossil (f2v, fossilAlive) && fossilAlive,
               "Mock VowelMorph read live FossilToVowelPayload");
        check (f2v.energyH > 0.05f,
               "Fossil published live Tonal energyH (" + std::to_string (f2v.energyH) + ")");
        check (f2v.freezeCaptureSeq >= 1u && f2v.frozenStrataCount >= 1u && (f2v.frozenStrataMask & 1u) != 0u,
               "Fossil published freezeCaptureSeq=" + std::to_string (f2v.freezeCaptureSeq)
               + " frozenStrataCount=" + std::to_string (f2v.frozenStrataCount));
        check (f2v.euclidStepSeq > 0u && f2v.euclidHitSeq > 0u && (f2v.flags & suite::texlink::kFossilEuclidOn) != 0u,
               "Fossil published live Euclid stepSeq=" + std::to_string (f2v.euclidStepSeq)
               + " hitSeq=" + std::to_string (f2v.euclidHitSeq));

        proc.connectTextureLinkBus (-1);
        mockVowel.disconnect();
    }

    // 5c. Read-only SuiteHarmonyLink loopback (Mock SessionMate -> Fossil)
    {
        const char* suffix = "FossilUnitHarmLoop";
        FossilAudioProcessor proc;
        proc.prepareToPlay (48000.0, 256);
        proc.connectHarmonyLinkBus (1, suffix);

        suite::harmony_link::HarmonyLinkEndpoint mockBand;
        check (mockBand.connect (1, suffix), "Mock SessionMate connected to isolated HarmonyLink Bus B");

        suite::harmony_link::BandToHudPayload b2h {};
        b2h.activeChord = suite::harmony_link::makeLinkChord (4, suite::harmony_link::LinkChordQuality::Min9); // Em9
        b2h.keyRootPc   = 4;
        b2h.keyIsMinor  = 1;
        b2h.bpm         = 118.0;
        b2h.isPlaying   = 1;
        check (mockBand.publishBand (b2h), "Mock SessionMate published Em9 BandToHudPayload");

        juce::AudioBuffer<float> buf (2, 256);
        buf.clear();
        juce::MidiBuffer emptyMidi;
        proc.processBlock (buf, emptyMidi);

        const auto& tel = proc.getTelemetry();
        check (tel.harmonyPeerAlive.load() && tel.activeChordRootPc.load() == 4,
               "Fossil received live HarmonyLink Em9 chord (rootPc=" + std::to_string (tel.activeChordRootPc.load()) + ")");

        char lbl[16] {};
        for (size_t i = 0; i < 15; ++i)
            lbl[i] = tel.harmonyChordLabel[i].load();
        check (std::string (lbl) == "Em9",
               "Fossil formatted HarmonyLink chord badge accurately ('" + std::string (lbl) + "')");

        proc.connectHarmonyLinkBus (-1);
        mockBand.disconnect();
    }
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "========================================\n";
    std::cout << "  Fossil Plugin Suite - Unit Tests\n";
    std::cout << "========================================\n";

    testHpssSeparationAndLatency();
    testEuclideanBjorklund();
    testFreezeAndStrataPlayback();
    testProcessorStabilityAndStateRoundTrip();
    testSuiteTextureAndHarmonyLinks();

    std::cout << "\n========================================\n";
    if (gFailures == 0)
    {
        std::cout << "ALL FOSSIL TESTS PASSED!\n";
        return 0;
    }
    std::cerr << gFailures << " TEST(S) FAILED!\n";
    return 1;
}
