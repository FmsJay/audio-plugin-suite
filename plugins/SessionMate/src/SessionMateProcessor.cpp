#include "SessionMateProcessor.h"
#include "ui/SessionMateEditor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sessionmate {

juce::AudioProcessor::BusesProperties SessionMateProcessor::makeBusesLayout() {
    return BusesProperties()
        .withInput("Audio In", juce::AudioChannelSet::stereo(), true)
        .withOutput("Master Out", juce::AudioChannelSet::stereo(), true)
        .withOutput("Drums Out", juce::AudioChannelSet::stereo(), false)
        .withOutput("Bass Out", juce::AudioChannelSet::stereo(), false)
        .withOutput("Keys Out", juce::AudioChannelSet::stereo(), false)
        .withOutput("Guitar Out", juce::AudioChannelSet::stereo(), false)
        .withOutput("Perc Out", juce::AudioChannelSet::stereo(), false)
        .withOutput("Horns Out", juce::AudioChannelSet::stereo(), false);
}

SessionMateProcessor::SessionMateProcessor()
    : AudioProcessor(makeBusesLayout()),
      apvts(*this, nullptr, "SESSIONMATE_PARAMS", createParameterLayout()),
      liveListener(std::make_unique<LiveAudioListener>()),
      grooveComposer(std::make_unique<GrooveComposer>()),
      bandSynth(std::make_unique<BuiltInBandSynth>()),
      vstHost(std::make_unique<VstSubHost>()),
      midiHistory(std::make_unique<MidiHistoryExporter>()),
      harmonyLink(std::make_unique<suite::link::HarmonyLinkEndpoint>()) {
    cacheParameterPointers();
    apvts.addParameterListener(ParamID::linkBus, this);

    for (int s = 0; s < kNumSlots; ++s) {
        slotSourceModes[static_cast<size_t>(s)].store(static_cast<int>(SlotSourceMode::BuiltIn),
                                                      std::memory_order_relaxed);
    }

    // Initialize default musical chord progression: | Em9 | A13 | Dmaj7 | B7#9 |
    setChordChartText("| Em9 | A13 | Dmaj7 | B7#9 |");

    // In Standalone / Jam mode, start ready to play; in DAW mode follows DAW PlayHead
    syncTransport.setStandalonePlaying(true);
    standalonePlayingFlag.store(true, std::memory_order_relaxed);

    // Connect default HarmonyHUD Link bus (Bus A) cleanly on non-RT thread
    syncLinkBusConnection();
}

SessionMateProcessor::~SessionMateProcessor() {
    apvts.removeParameterListener(ParamID::linkBus, this);
    harmonyLink->disconnect();
    vstHost->closeAllEditors();
}

void SessionMateProcessor::parameterChanged(const juce::String& parameterID, float) {
    if (parameterID == ParamID::linkBus) {
        syncLinkBusConnection();
    }
}

void SessionMateProcessor::setLinkTestSuffix(const std::string& suffix) {
    {
        std::lock_guard<std::mutex> lock(metadataMutex);
        linkTestSuffix = suffix;
    }
    lastSeenHudReharmSeq = 0;
    lastSeenHudChartSeq = 0;
    syncLinkBusConnection();
}

void SessionMateProcessor::syncLinkBusConnection() {
    const float raw = globalParams.linkBus ? globalParams.linkBus->load(std::memory_order_relaxed) : 1.0f;
    const int busChoice = std::clamp(static_cast<int>(std::lround(raw)), 0, suite::link::kNumBuses);
    if (busChoice <= 0) {
        harmonyLink->disconnect();
        return;
    }
    std::string suffixCopy;
    {
        std::lock_guard<std::mutex> lock(metadataMutex);
        suffixCopy = linkTestSuffix;
    }
    harmonyLink->connect(busChoice - 1, suffixCopy.empty() ? nullptr : suffixCopy.c_str());
}

void SessionMateProcessor::cacheParameterPointers() {
    globalParams.syncMode       = apvts.getRawParameterValue(ParamID::syncMode);
    globalParams.manualBpm      = apvts.getRawParameterValue(ParamID::manualBpm);
    globalParams.styleA         = apvts.getRawParameterValue(ParamID::styleA);
    globalParams.styleB         = apvts.getRawParameterValue(ParamID::styleB);
    globalParams.styleMorph     = apvts.getRawParameterValue(ParamID::styleMorph);
    globalParams.timeSig        = apvts.getRawParameterValue(ParamID::timeSig);
    globalParams.section        = apvts.getRawParameterValue(ParamID::section);
    globalParams.energy         = apvts.getRawParameterValue(ParamID::energy);
    globalParams.swing          = apvts.getRawParameterValue(ParamID::swing);
    globalParams.pushPullMs     = apvts.getRawParameterValue(ParamID::pushPullMs);
    globalParams.leaveSpace     = apvts.getRawParameterValue(ParamID::leaveSpace);
    globalParams.autoChord      = apvts.getRawParameterValue(ParamID::autoChord);
    globalParams.autoSection    = apvts.getRawParameterValue(ParamID::autoSection);
    globalParams.keyRoot        = apvts.getRawParameterValue(ParamID::keyRoot);
    globalParams.keyMinor       = apvts.getRawParameterValue(ParamID::keyMinor);
    globalParams.halfTime       = apvts.getRawParameterValue(ParamID::halfTime);
    globalParams.doubleTime     = apvts.getRawParameterValue(ParamID::doubleTime);
    globalParams.masterVolDb    = apvts.getRawParameterValue(ParamID::masterVolDb);
    globalParams.harmonicSource = apvts.getRawParameterValue(ParamID::harmonicSource);
    globalParams.linkBus        = apvts.getRawParameterValue(ParamID::linkBus);
    globalParams.clashAvoid     = apvts.getRawParameterValue(ParamID::clashAvoid);
    globalParams.keysVoicing    = apvts.getRawParameterValue(ParamID::keysVoicing);

    for (int s = 0; s < kNumSlots; ++s) {
        auto& sp = slotParams[static_cast<size_t>(s)];
        sp.patch    = apvts.getRawParameterValue(ParamID::slotParam(s, "patch"));
        sp.drumMap  = apvts.getRawParameterValue(ParamID::slotParam(s, "drummap"));
        sp.busy     = apvts.getRawParameterValue(ParamID::slotParam(s, "busy"));
        sp.kickLock = apvts.getRawParameterValue(ParamID::slotParam(s, "kicklock"));
        sp.vol      = apvts.getRawParameterValue(ParamID::slotParam(s, "vol"));
        sp.pan      = apvts.getRawParameterValue(ParamID::slotParam(s, "pan"));
        sp.reverb   = apvts.getRawParameterValue(ParamID::slotParam(s, "reverb"));
        sp.tilt     = apvts.getRawParameterValue(ParamID::slotParam(s, "tilt"));
        sp.mute     = apvts.getRawParameterValue(ParamID::slotParam(s, "mute"));
        sp.solo     = apvts.getRawParameterValue(ParamID::slotParam(s, "solo"));
        sp.lock     = apvts.getRawParameterValue(ParamID::slotParam(s, "lock"));
        sp.callResp = apvts.getRawParameterValue(ParamID::slotParam(s, "callresp"));
    }
}

bool SessionMateProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::stereo())
        return false;

    const auto mainIn = layouts.getMainInputChannelSet();
    if (!mainIn.isDisabled() &&
        mainIn != juce::AudioChannelSet::stereo() &&
        mainIn != juce::AudioChannelSet::mono())
        return false;

    for (int bus = 1; bus < layouts.outputBuses.size(); ++bus) {
        const auto& set = layouts.outputBuses.getReference(bus);
        if (!set.isDisabled() && set != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

void SessionMateProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    currentSampleRate = sampleRate > 8000.0 ? sampleRate : 44100.0;
    currentBlockSize  = samplesPerBlock > 0 ? samplesPerBlock : 512;

    liveListener->prepare(currentSampleRate);
    syncTransport.prepare(currentSampleRate);
    grooveComposer->prepare(currentSampleRate);
    bandSynth->prepare(currentSampleRate, currentBlockSize);
    vstHost->prepare(currentSampleRate, currentBlockSize);
    syncLinkBusConnection();

    for (int s = 0; s < kNumSlots; ++s) {
        slotAudioBuffers[static_cast<size_t>(s)].setSize(2, currentBlockSize, false, true, true);
        slotMidiBuffers[static_cast<size_t>(s)].ensureSize(2048);
    }

    lastAppliedChordVersion = 0;
    applyPendingChordProgressionOnAudioThread();
}

void SessionMateProcessor::releaseResources() {
    grooveComposer->reset();
    bandSynth->reset();
    liveListener->reset();
}

void SessionMateProcessor::readParametersToControls(
    PerformanceControls& perf,
    SyncMode& outSyncMode,
    double& outManualBpm,
    std::array<SlotConfig, kNumSlots>& outSlots) const noexcept {

    auto load = [](std::atomic<float>* p, float fallback) noexcept {
        return p != nullptr ? p->load(std::memory_order_relaxed) : fallback;
    };

    const int sm = static_cast<int>(std::lround(load(globalParams.syncMode, 0.0f)));
    outSyncMode = static_cast<SyncMode>(std::clamp(sm, 0, 2));
    outManualBpm = static_cast<double>(std::clamp(load(globalParams.manualBpm, 104.0f), 45.0f, 220.0f));

    const int maxStyle = std::max(0, grooveComposer->library().count() - 1);
    perf.styleAIndex = std::clamp(static_cast<int>(std::lround(load(globalParams.styleA, 6.0f))), 0, maxStyle);
    perf.styleBIndex = std::clamp(static_cast<int>(std::lround(load(globalParams.styleB, 7.0f))), 0, maxStyle);
    perf.styleMorph  = std::clamp(load(globalParams.styleMorph, 0.25f), 0.0f, 1.0f);
    perf.timeSig     = timeSigFromPreset(static_cast<int>(std::lround(load(globalParams.timeSig, 0.0f))));
    perf.section     = static_cast<SongSection>(
        std::clamp(static_cast<int>(std::lround(load(globalParams.section, 3.0f))), 0, kNumSections - 1));
    perf.energy      = std::clamp(load(globalParams.energy, 0.68f), 0.0f, 1.0f);
    perf.swing       = std::clamp(load(globalParams.swing, 0.56f), 0.50f, 0.72f);
    perf.pushPullMs  = std::clamp(load(globalParams.pushPullMs, 2.0f), -25.0f, 25.0f);
    perf.leaveSpace  = std::clamp(load(globalParams.leaveSpace, 0.65f), 0.0f, 1.0f);
    perf.autoDetectChords = load(globalParams.autoChord, 0.0f) >= 0.5f;
    perf.autoSectionFromEnergy = load(globalParams.autoSection, 0.0f) >= 0.5f;
    perf.keyRoot     = std::clamp(static_cast<int>(std::lround(load(globalParams.keyRoot, 4.0f))), 0, 11);
    perf.keyMinor    = load(globalParams.keyMinor, 1.0f) >= 0.5f;
    perf.halfTime    = load(globalParams.halfTime, 0.0f) >= 0.5f;
    perf.doubleTime  = load(globalParams.doubleTime, 0.0f) >= 0.5f;
    perf.masterVolumeDb = std::clamp(load(globalParams.masterVolDb, -2.0f), -36.0f, 6.0f);

    const int hsIdx = std::clamp(static_cast<int>(std::lround(load(globalParams.harmonicSource, 0.0f))), 0, 2);
    perf.harmonicSource = static_cast<HarmonicSourceMode>(hsIdx);
    // Keep legacy auto_chord toggle seamlessly compatible
    if (perf.autoDetectChords && perf.harmonicSource == HarmonicSourceMode::Chart)
        perf.harmonicSource = HarmonicSourceMode::LiveAudio;

    perf.linkBus = std::clamp(static_cast<int>(std::lround(load(globalParams.linkBus, 1.0f))), 0, suite::link::kNumBuses);
    perf.clashAvoidance = load(globalParams.clashAvoid, 1.0f) >= 0.5f;
    const int kvIdx = std::clamp(static_cast<int>(std::lround(load(globalParams.keysVoicing, 0.0f))), 0, kNumKeysVoicings - 1);
    perf.keysVoicing = static_cast<KeysVoicingStyle>(kvIdx);

    for (int s = 0; s < kNumSlots; ++s) {
        const auto& sp = slotParams[static_cast<size_t>(s)];
        auto& cfg = outSlots[static_cast<size_t>(s)];
        cfg.sourceMode = static_cast<SlotSourceMode>(
            slotSourceModes[static_cast<size_t>(s)].load(std::memory_order_relaxed));
        cfg.builtInPatch = std::clamp(static_cast<int>(std::lround(load(sp.patch, 0.0f))),
                                      0, numPatchesForSlot(s) - 1);
        cfg.drumMap = static_cast<DrumMapPreset>(
            std::clamp(static_cast<int>(std::lround(load(sp.drumMap, 0.0f))), 0, kNumDrumMaps - 1));
        cfg.busy        = std::clamp(load(sp.busy, 0.65f), 0.0f, 1.0f);
        cfg.kickLock    = std::clamp(load(sp.kickLock, 0.80f), 0.0f, 1.0f);
        cfg.volumeDb    = std::clamp(load(sp.vol, 0.0f), -60.0f, 6.0f);
        cfg.pan         = std::clamp(load(sp.pan, 0.0f), -1.0f, 1.0f);
        cfg.reverbSend  = std::clamp(load(sp.reverb, 0.16f), 0.0f, 1.0f);
        cfg.tiltEq      = std::clamp(load(sp.tilt, 0.0f), -1.0f, 1.0f);
        cfg.mute        = load(sp.mute, 0.0f) >= 0.5f;
        cfg.solo        = load(sp.solo, 0.0f) >= 0.5f;
        cfg.patternLock = load(sp.lock, 0.0f) >= 0.5f;
        cfg.callAndResponse = load(sp.callResp, s == 5 ? 1.0f : 0.0f) >= 0.5f;
        cfg.patternLockBars = 2;
        cfg.velocityCurve = 1.0f;
        cfg.cc4HiHatEnabled = true;
        cfg.cc64SustainEnabled = true;
    }
}

void SessionMateProcessor::applyPendingChordProgressionOnAudioThread() noexcept {
    const uint32_t ver = chordChartVersion.load(std::memory_order_acquire);
    if (ver != lastAppliedChordVersion) {
        const int idx = activeChordBufferIdx.load(std::memory_order_acquire);
        const auto& slot = chordBuffers[static_cast<size_t>(idx)];
        audioThreadChart = slot.chords;
        audioThreadChartBars = std::clamp(slot.numBars, 1, kMaxChordBars);
        grooveComposer->setChordProgression(audioThreadChart, audioThreadChartBars);
        lastAppliedChordVersion = ver;
        ++publishedBandChartSeq;
    }
}

void SessionMateProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                        juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    // 1. Read live audio input bus into LiveAudioListener BEFORE clearing outputs
    if (getBusCount(true) > 0) {
        auto inBus = getBusBuffer(buffer, true, 0);
        if (inBus.getNumChannels() >= 2) {
            liveListener->processAudioBlock(inBus.getReadPointer(0),
                                            inBus.getReadPointer(1),
                                            numSamples);
        } else if (inBus.getNumChannels() == 1) {
            liveListener->processAudioBlock(inBus.getReadPointer(0),
                                            inBus.getReadPointer(0),
                                            numSamples);
        }
    }

    const ListenerTelemetry listener = liveListener->getTelemetry();

    // 2. Read parameters lock-freely
    PerformanceControls perf {};
    SyncMode syncMode = SyncMode::DawHost;
    double manualBpm = 104.0;
    std::array<SlotConfig, kNumSlots> slotConfigs {};
    readParametersToControls(perf, syncMode, manualBpm, slotConfigs);

    // 2b. Lock-free HarmonyHUD Link Read (zero allocs, zero syscalls)
    HarmonyLinkHints linkHints {};
    if (perf.linkBus > 0 && harmonyLink->isConnected()) {
        suite::link::HudToBandPayload hudPayload {};
        bool peerAlive = false;
        if (harmonyLink->readHud(hudPayload, peerAlive) && peerAlive) {
            linkHints.peerAlive = true;
            linkHints.liveChord = fromLinkChord(hudPayload.liveChord);
            linkHints.chordConfidence = static_cast<float>(hudPayload.liveChord.confidence) * 0.01f;
            linkHints.keyRootPc = std::clamp(static_cast<int>(hudPayload.keyRootPc), 0, 11);
            linkHints.keyIsMinor = (hudPayload.keyIsMinor != 0);
            linkHints.activeMode = fromLinkMode(hudPayload.activeMode);
            linkHints.scalePitchClassMask = hudPayload.scalePitchClassMask;
            linkHints.avoidPitchClassMask = hudPayload.avoidPitchClassMask;
            linkHints.userPitchClassMask = hudPayload.userActiveNotes.pitchClassMask12();
            linkHints.userActivity = std::clamp(hudPayload.userActivityLevel, 0.0f, 1.0f);
            linkHints.userEnergy = std::clamp(hudPayload.userEnergy, 0.0f, 1.0f);
            linkHints.userHasSignal = (hudPayload.userHasSignal != 0);
            linkHints.clashCount = static_cast<int>(hudPayload.clashCount);

            // Auto-push live chords flag from HarmonyHUD if user toggled it
            if (hudPayload.autoPushLiveChords != 0 && linkHints.liveChord.valid) {
                perf.harmonicSource = HarmonicSourceMode::HarmonyHudLink;
            }

            // One-shot Live Reharmonization push from HarmonyHUD
            if (hudPayload.reharmSeq != 0 && hudPayload.reharmSeq != lastSeenHudReharmSeq) {
                lastSeenHudReharmSeq = hudPayload.reharmSeq;
                const ChordSymbol sub = fromLinkChord(hudPayload.reharmChord);
                if (sub.valid) {
                    const int timingMode = std::clamp(static_cast<int>(hudPayload.reharmTiming), 0, 2);
                    const int durBars = std::clamp(static_cast<int>(hudPayload.reharmLockBars), 1, 8);
                    grooveComposer->queueReharmChord(sub, timingMode, durBars);
                }
            }

            // One-shot Chord Chart push ([Send Chart -> SessionMate])
            if (hudPayload.chartSeq != 0 && hudPayload.chartSeq != lastSeenHudChartSeq &&
                hudPayload.chartNumBars > 0) {
                lastSeenHudChartSeq = hudPayload.chartSeq;
                const int nBars = std::clamp(static_cast<int>(hudPayload.chartNumBars), 1, kMaxChordBars);
                int validBars = 0;
                for (int b = 0; b < nBars; ++b) {
                    const ChordSymbol ch = fromLinkChord(hudPayload.chartBars[b]);
                    if (ch.valid) {
                        audioThreadChart[static_cast<size_t>(validBars++)] = ch;
                    }
                }
                if (validBars > 0) {
                    audioThreadChartBars = validBars;
                    grooveComposer->setChordProgression(audioThreadChart, audioThreadChartBars);
                    ++publishedBandChartSeq;

                    // Publish back to UI thread via lock-free double buffer
                    const int wIdx = 1 - incomingLinkChartIdx.load(std::memory_order_relaxed);
                    incomingLinkCharts[static_cast<size_t>(wIdx)].chords = audioThreadChart;
                    incomingLinkCharts[static_cast<size_t>(wIdx)].numBars = validBars;
                    incomingLinkChartIdx.store(wIdx, std::memory_order_release);
                    incomingLinkChartVersion.fetch_add(1, std::memory_order_release);
                }
            }
        }
    }

    // Handle incoming MIDI foot-controller triggers (Program Change 0..6 -> section, CC 103 -> Fill)
    SongSection activeSection = perf.section;
    for (const auto meta : midiMessages) {
        const auto msg = meta.getMessage();
        if (msg.isProgramChange()) {
            const int pc = msg.getProgramChangeNumber();
            if (pc >= 0 && pc < kNumSections)
                activeSection = static_cast<SongSection>(pc);
        } else if (msg.isController() && msg.getControllerNumber() == 103 && msg.getControllerValue() >= 64) {
            grooveComposer->triggerManualFill();
        }
    }
    perf.section = activeSection;

    if (perf.autoSectionFromEnergy) {
        const float effEnergy = std::max(listener.energyEstimate,
                                         (linkHints.peerAlive && linkHints.userHasSignal) ? linkHints.userEnergy : 0.0f);
        if (effEnergy > 0.72f)
            perf.section = SongSection::Chorus;
        else if (effEnergy > 0.45f)
            perf.section = SongSection::PreChorus;
        else if (effEnergy > 0.18f)
            perf.section = SongSection::Verse;
        else
            perf.section = SongSection::Intro;
    }

    // 3. Apply pending chord progression, reharm clear, or fill requests
    applyPendingChordProgressionOnAudioThread();
    if (pendingFillRequest.exchange(false, std::memory_order_acq_rel)) {
        grooveComposer->triggerManualFill();
    }
    if (pendingClearReharm.exchange(false, std::memory_order_acq_rel)) {
        grooveComposer->clearReharm();
    }

    // 4. Sync Transport
    syncTransport.setManualBpm(manualBpm);
    syncTransport.setStandalonePlaying(standalonePlayingFlag.load(std::memory_order_relaxed));

    SyncTransport::HostPlayHeadInfo hostInfo {};
    if (auto* ph = getPlayHead()) {
        if (const auto pos = ph->getPosition()) {
            hostInfo.valid = true;
            hostInfo.isPlaying = pos->getIsPlaying();
            if (const auto b = pos->getBpm())
                hostInfo.bpm = *b;
            if (const auto ppq = pos->getPpqPosition())
                hostInfo.ppqPosition = *ppq;
            if (const auto ts = pos->getTimeSignature()) {
                hostInfo.timeSig.numerator = ts->numerator;
                hostInfo.timeSig.denominator = ts->denominator;
            }
        }
    }

    const TransportState transport = syncTransport.advanceBlock(
        numSamples, syncMode, perf.timeSig, hostInfo, listener);

    // 5. Compose 6-player musical events (with HarmonyHUD Link hints, Auto-Clash Avoidance & Voicings!)
    masterBlockEvents.clear();
    ComposerExtendedResult compRes {};

    grooveComposer->processBlockEx(numSamples,
                                   transport,
                                   perf,
                                   slotConfigs,
                                   listener,
                                   linkHints,
                                   masterBlockEvents,
                                   compRes);

    // 6. Translate events per slot, push to MIDI history, emit multi-channel MIDI Out (Ch 1..6)
    midiMessages.clear();
    std::array<bool, kNumSlots> slotNoteActivity {};
    for (int s = 0; s < kNumSlots; ++s) {
        slotMidiBuffers[static_cast<size_t>(s)].clear();
        slotEventLists[static_cast<size_t>(s)].clear();
    }

    for (int i = 0; i < masterBlockEvents.count; ++i) {
        const auto& rawEv = masterBlockEvents.events[static_cast<size_t>(i)];
        const int s = std::clamp(static_cast<int>(rawEv.slot), 0, kNumSlots - 1);

        midiHistory->pushEventFromAudioThread(rawEv);

        if (rawEv.type == EventType::NoteOn && rawEv.data2 > 0)
            slotNoteActivity[static_cast<size_t>(s)] = true;

        BandNoteEvent translated[2];
        const int numOut = drumMapTranslator.translateEvent(rawEv, slotConfigs[static_cast<size_t>(s)], translated);

        for (int k = 0; k < numOut; ++k) {
            const auto& ev = translated[k];
            const int sampleOff = std::clamp(ev.sampleOffset, 0, numSamples - 1);
            const int midiCh = slotMidiChannel(s);

            if (s == 0 && k == 0) {
                slotEventLists[0].push(rawEv);
            } else if (s != 0) {
                slotEventLists[static_cast<size_t>(s)].push(ev);
            } else if (ev.type == EventType::ControlChange) {
                slotEventLists[0].push(ev);
            }

            juce::MidiMessage jmsg;
            bool validMsg = false;
            switch (ev.type) {
                case EventType::NoteOn:
                    jmsg = juce::MidiMessage::noteOn(midiCh, static_cast<int>(ev.data1), ev.data2);
                    validMsg = true;
                    break;
                case EventType::NoteOff:
                    jmsg = juce::MidiMessage::noteOff(midiCh, static_cast<int>(ev.data1), ev.data2);
                    validMsg = true;
                    break;
                case EventType::ControlChange:
                    jmsg = juce::MidiMessage::controllerEvent(midiCh, static_cast<int>(ev.data1), static_cast<int>(ev.data2));
                    validMsg = true;
                    break;
                case EventType::PitchBend:
                    jmsg = juce::MidiMessage::pitchWheel(midiCh, (static_cast<int>(ev.data2) << 7) | static_cast<int>(ev.data1));
                    validMsg = true;
                    break;
            }
            if (validMsg) {
                slotMidiBuffers[static_cast<size_t>(s)].addEvent(jmsg, sampleOff);
                midiMessages.addEvent(jmsg, sampleOff);
            }
        }
    }

    // 7. Clear all output buses cleanly
    const int numOutBuses = getBusCount(false);
    for (int busIdx = 0; busIdx < numOutBuses; ++busIdx) {
        auto outBus = getBusBuffer(buffer, false, busIdx);
        outBus.clear();
    }

    auto masterBus = getBusBuffer(buffer, false, 0);
    if (masterBus.getNumChannels() < 2)
        return;

    float* masterL = masterBus.getWritePointer(0);
    float* masterR = masterBus.getWritePointer(1);

    bool anySolo = false;
    for (int s = 0; s < kNumSlots; ++s) {
        if (slotConfigs[static_cast<size_t>(s)].solo) {
            anySolo = true;
            break;
        }
    }

    // 8. Render each of the 6 band slots (Hosted VST3 OR Built-In / SFZ) & mix
    std::array<float, kNumSlots> slotPeakL {};
    std::array<float, kNumSlots> slotPeakR {};
    std::array<bool, kNumSlots> slotFallback {};

    for (int s = 0; s < kNumSlots; ++s) {
        auto& sBuf = slotAudioBuffers[static_cast<size_t>(s)];
        if (sBuf.getNumSamples() < numSamples)
            continue;
        sBuf.clear(0, numSamples);

        const auto& cfg = slotConfigs[static_cast<size_t>(s)];
        bandSynth->setSlotPatch(s, cfg.builtInPatch);

        bool renderedByHost = false;
        if (cfg.sourceMode == SlotSourceMode::HostedVST3) {
            float* ptrs[2] = { sBuf.getWritePointer(0), sBuf.getWritePointer(1) };
            juce::AudioBuffer<float> slotAlias(ptrs, 2, numSamples);
            renderedByHost = vstHost->processSlotAudio(s, slotAlias, slotMidiBuffers[static_cast<size_t>(s)]);
        }

        slotFallback[static_cast<size_t>(s)] = vstHost->isSlotFallbackActive(s);

        if (!renderedByHost) {
            const bool useSfz = (cfg.sourceMode == SlotSourceMode::CustomSFZ) && bandSynth->hasSfzLoaded(s);
            const auto& evList = slotEventLists[static_cast<size_t>(s)];
            bandSynth->processSlotBlock(s,
                                        sBuf.getWritePointer(0),
                                        sBuf.getWritePointer(1),
                                        numSamples,
                                        evList.events.data(),
                                        evList.count,
                                        useSfz);
        }

        if (s + 1 < numOutBuses) {
            auto auxBus = getBusBuffer(buffer, false, s + 1);
            if (auxBus.getNumChannels() >= 2) {
                auxBus.copyFrom(0, 0, sBuf, 0, 0, numSamples);
                auxBus.copyFrom(1, 0, sBuf, 1, 0, numSamples);
            }
        }

        bandSynth->applyChannelStripAndMix(s,
                                           sBuf.getWritePointer(0),
                                           sBuf.getWritePointer(1),
                                           masterL,
                                           masterR,
                                           numSamples,
                                           cfg,
                                           anySolo,
                                           slotPeakL[static_cast<size_t>(s)],
                                           slotPeakR[static_cast<size_t>(s)]);
    }

    // 9. Finalize Studio Room Reverb + Master Output Volume + Soft Limiter + Finite Clamp
    float masterPeakL = 0.0f;
    float masterPeakR = 0.0f;
    bandSynth->finalizeMasterReverbAndLimit(masterL,
                                            masterR,
                                            numSamples,
                                            perf.masterVolumeDb,
                                            masterPeakL,
                                            masterPeakR);

    // 9b. Lock-free Broadcast BandToHudPayload to HarmonyHUD (zero allocs, zero syscalls)
    if (perf.linkBus > 0 && harmonyLink->isConnected()) {
        suite::link::BandToHudPayload bandPayload {};
        bandPayload.activeChord = toLinkChord(compRes.activeChord, 100);
        bandPayload.nextChord = toLinkChord(compRes.nextChord, 100);
        bandPayload.beatsUntilNextChord = compRes.beatsUntilNextChord;

        bandPayload.bpm = transport.bpm;
        bandPayload.ppqPosition = transport.ppqPosition;
        bandPayload.currentBar = transport.currentBar;
        bandPayload.currentBeat = transport.currentBeat;
        bandPayload.beatInBar = static_cast<float>(std::max(0.0, transport.ppqPosition - transport.barStartPpq));

        bandPayload.activeChartBarIndex = static_cast<int8_t>(
            perf.harmonicSource == HarmonicSourceMode::Chart ? compRes.activeChordBarIndex : -1);
        bandPayload.timeSigNum = static_cast<uint8_t>(std::clamp(transport.timeSig.numerator, 1, 32));
        bandPayload.timeSigDen = static_cast<uint8_t>(std::clamp(transport.timeSig.denominator, 1, 32));
        bandPayload.isPlaying = transport.isPlaying ? 1u : 0u;
        bandPayload.songSection = static_cast<uint8_t>(perf.section);
        bandPayload.fillActive = compRes.fillActive ? 1u : 0u;
        bandPayload.styleMorphPct = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(perf.styleMorph * 100.0f)), 0, 100));
        bandPayload.keyRootPc = static_cast<int8_t>(perf.keyRoot);
        bandPayload.keyIsMinor = perf.keyMinor ? 1u : 0u;
        bandPayload.harmonicSourceMode = toLinkHarmonicSource(perf.harmonicSource);
        bandPayload.clashAvoidanceActive = (perf.clashAvoidance && linkHints.peerAlive) ? 1u : 0u;
        bandPayload.reharmPendingActive = compRes.reharmActive ? 1u : 0u;

        // Style & Section labels (fixed null-terminated buffers, zero heap allocs!)
        const auto& styleGenome = grooveComposer->library().get(
            perf.styleMorph >= 0.5f ? perf.styleBIndex : perf.styleAIndex);
        copyFixedString(bandPayload.styleName, sizeof(bandPayload.styleName), styleGenome.name.c_str());
        copyFixedString(bandPayload.sectionLabel, sizeof(bandPayload.sectionLabel), sectionName(perf.section));

        // Live Per-Slot Active MIDI Note Bitmasks for HarmonyHUD Band X-Ray!
        std::array<uint64_t, kNumSlots> noteLo {}, noteHi {};
        grooveComposer->getSlotActiveNoteMasks(noteLo, noteHi);
        for (int s = 0; s < kNumSlots; ++s) {
            const auto& scfg = slotConfigs[static_cast<size_t>(s)];
            const bool audible = !scfg.mute && (!anySolo || scfg.solo);
            if (audible) {
                bandPayload.slotNotes[s].lo = noteLo[static_cast<size_t>(s)];
                bandPayload.slotNotes[s].hi = noteHi[static_cast<size_t>(s)];
            } else {
                bandPayload.slotNotes[s].clearAll();
            }
            bandPayload.slotActive[s] = (audible && (bandPayload.slotNotes[s].any() || slotNoteActivity[static_cast<size_t>(s)])) ? 1u : 0u;
        }

        // Bidirectional Band Chord Chart snapshot
        bandPayload.bandChartSeq = publishedBandChartSeq;
        bandPayload.bandChartNumBars = static_cast<uint8_t>(std::clamp(audioThreadChartBars, 0, suite::link::kMaxChartBars));
        for (int b = 0; b < static_cast<int>(bandPayload.bandChartNumBars); ++b) {
            bandPayload.bandChartBars[b] = toLinkChord(audioThreadChart[static_cast<size_t>(b)], 100);
        }

        harmonyLink->publishBand(bandPayload);
    }

    // 10. Publish lock-free UI snapshot
    const int writeIdx = 1 - publishedSnapshotIdx.load(std::memory_order_relaxed);
    auto& snap = uiSnapshots[static_cast<size_t>(writeIdx)];
    snap.transport = transport;
    snap.listener = listener;
    snap.activeChord = compRes.activeChord;
    snap.nextChord = compRes.nextChord;
    snap.beatsUntilNextChord = compRes.beatsUntilNextChord;
    snap.activeChordBarIndex = compRes.activeChordBarIndex;
    snap.activeSection = perf.section;
    snap.fillActive = compRes.fillActive;
    snap.hudLinkConnected = linkHints.peerAlive;
    snap.hudDetectedChord = linkHints.liveChord;
    snap.hudChordConfidence = linkHints.chordConfidence;
    snap.hudActiveMode = linkHints.activeMode;
    snap.hudClashCount = linkHints.clashCount;
    snap.reharmActive = compRes.reharmActive;
    snap.reharmChord = compRes.reharmChord;
    snap.clashAvoidedThisBlock = compRes.clashAvoided;
    snap.slotPeakL = slotPeakL;
    snap.slotPeakR = slotPeakR;
    snap.slotNoteActivity = slotNoteActivity;
    snap.slotFallbackActive = slotFallback;
    snap.masterPeakL = masterPeakL;
    snap.masterPeakR = masterPeakR;
    snap.recordedMidiEvents = midiHistory->getRecordedEventCount();
    snap.recordedBars = midiHistory->getRecordedBarCount(transport.timeSig);
    publishedSnapshotIdx.store(writeIdx, std::memory_order_release);
}

juce::AudioProcessorEditor* SessionMateProcessor::createEditor() {
    return new ui::SessionMateEditor(*this, apvts, *this);
}

// ============================================================================
// ISessionMateUiBridge Implementation
// ============================================================================

void SessionMateProcessor::syncIncomingLinkChartToUi() const {
    const uint32_t ver = incomingLinkChartVersion.load(std::memory_order_acquire);
    if (ver != appliedIncomingLinkChartVersion.load(std::memory_order_relaxed)) {
        const int idx = incomingLinkChartIdx.load(std::memory_order_acquire);
        const auto& incoming = incomingLinkCharts[static_cast<size_t>(idx)];
        const int nBars = std::clamp(incoming.numBars, 1, kMaxChordBars);

        std::vector<ChordSymbol> vec;
        vec.reserve(static_cast<size_t>(nBars));
        for (int i = 0; i < nBars; ++i)
            vec.push_back(incoming.chords[static_cast<size_t>(i)]);

        // Mirror into UI chord buffers and chart text without re-bumping audio version
        const int wIdx = 1 - activeChordBufferIdx.load(std::memory_order_relaxed);
        chordBuffers[static_cast<size_t>(wIdx)].chords = incoming.chords;
        chordBuffers[static_cast<size_t>(wIdx)].numBars = nBars;
        activeChordBufferIdx.store(wIdx, std::memory_order_release);

        {
            std::lock_guard<std::mutex> lock(metadataMutex);
            chordChartText = formatChordTrack(vec);
        }
        appliedIncomingLinkChartVersion.store(ver, std::memory_order_release);
    }
}

UiSnapshot SessionMateProcessor::getUiSnapshot() const {
    syncIncomingLinkChartToUi();
    midiHistory->drainFromAudioThread();
    const_cast<VstSubHost&>(*vstHost).collectGarbage();

    const int idx = publishedSnapshotIdx.load(std::memory_order_acquire);
    UiSnapshot snap = uiSnapshots[static_cast<size_t>(idx)];
    snap.recordedMidiEvents = midiHistory->getRecordedEventCount();
    snap.recordedBars = midiHistory->getRecordedBarCount(snap.transport.timeSig);
    return snap;
}

int SessionMateProcessor::getStyleCount() const {
    return grooveComposer->library().count();
}

std::string SessionMateProcessor::getStyleName(int index) const {
    return grooveComposer->library().get(index).name;
}

std::string SessionMateProcessor::getStyleFamilyName(int index) const {
    return genreFamilyName(grooveComposer->library().get(index).family);
}

void SessionMateProcessor::setStandalonePlaying(bool shouldPlay) {
    standalonePlayingFlag.store(shouldPlay, std::memory_order_relaxed);
    syncTransport.setStandalonePlaying(shouldPlay);
}

bool SessionMateProcessor::isStandalonePlaying() const {
    return standalonePlayingFlag.load(std::memory_order_relaxed);
}

void SessionMateProcessor::triggerTapTempo() {
    const double nowSec = juce::Time::getMillisecondCounterHiRes() * 0.001;
    syncTransport.triggerTapTempo(nowSec);
    const double newBpm = syncTransport.getManualBpm();
    if (auto* param = apvts.getParameter(ParamID::manualBpm)) {
        param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(newBpm)));
    }
}

void SessionMateProcessor::triggerFillNow() {
    pendingFillRequest.store(true, std::memory_order_release);
}

std::string SessionMateProcessor::getChordChartText() const {
    syncIncomingLinkChartToUi();
    std::lock_guard<std::mutex> lock(metadataMutex);
    return chordChartText;
}

void SessionMateProcessor::setChordChartText(const std::string& chartText) {
    auto parsed = parseChordTrack(chartText);
    if (parsed.empty()) {
        parsed.push_back(parseChord("Em9"));
    }

    const int writeIdx = 1 - activeChordBufferIdx.load(std::memory_order_relaxed);
    auto& slot = chordBuffers[static_cast<size_t>(writeIdx)];
    slot.numBars = std::min(static_cast<int>(parsed.size()), kMaxChordBars);
    for (int i = 0; i < slot.numBars; ++i) {
        slot.chords[static_cast<size_t>(i)] = parsed[static_cast<size_t>(i)];
    }

    {
        std::lock_guard<std::mutex> lock(metadataMutex);
        chordChartText = formatChordTrack(parsed);
    }

    activeChordBufferIdx.store(writeIdx, std::memory_order_release);
    chordChartVersion.fetch_add(1, std::memory_order_release);
}

std::vector<std::string> SessionMateProcessor::getChordBarLabels() const {
    syncIncomingLinkChartToUi();
    const int idx = activeChordBufferIdx.load(std::memory_order_acquire);
    const auto& slot = chordBuffers[static_cast<size_t>(idx)];
    std::vector<std::string> out;
    out.reserve(static_cast<size_t>(slot.numBars));
    for (int i = 0; i < slot.numBars; ++i) {
        out.push_back(formatChord(slot.chords[static_cast<size_t>(i)]));
    }
    return out;
}

std::string SessionMateProcessor::formatChordSymbol(const ChordSymbol& chord) const {
    return formatChord(chord);
}

std::vector<std::pair<std::string, std::string>> SessionMateProcessor::getChordProgressionPresets() const {
    return getFactoryChordProgressions();
}

void SessionMateProcessor::clearActiveReharm() {
    pendingClearReharm.store(true, std::memory_order_release);
}

std::string SessionMateProcessor::getModeName(ScaleMode mode) const {
    return scaleModeName(mode);
}

std::vector<AvailablePluginItem> SessionMateProcessor::getAvailableVst3Plugins() const {
    return vstHost->getAvailableInstruments();
}

void SessionMateProcessor::startVst3ScanAsync() {
    vstHost->scanPluginsAsync();
}

bool SessionMateProcessor::isVst3Scanning() const {
    return vstHost->isScanning();
}

std::string SessionMateProcessor::getVst3ScanStatus() const {
    return vstHost->getScanStatus();
}

SlotSourceMode SessionMateProcessor::getSlotSourceMode(int slot) const {
    if (slot < 0 || slot >= kNumSlots) return SlotSourceMode::BuiltIn;
    return static_cast<SlotSourceMode>(slotSourceModes[static_cast<size_t>(slot)].load(std::memory_order_relaxed));
}

std::string SessionMateProcessor::getSlotSourceLabel(int slot) const {
    if (slot < 0 || slot >= kNumSlots) return "Built-In";
    const auto mode = getSlotSourceMode(slot);
    if (mode == SlotSourceMode::HostedVST3) {
        const auto name = vstHost->getSlotDisplayName(slot);
        if (vstHost->isSlotFallbackActive(slot))
            return "Built-In (Missing: " + (name.empty() ? "VST3" : name) + ")";
        return "VST3: " + (name.empty() ? "Loaded" : name);
    }
    if (mode == SlotSourceMode::CustomSFZ && bandSynth->hasSfzLoaded(slot)) {
        return "SFZ: " + bandSynth->getSfzName(slot);
    }
    return "Built-In";
}

bool SessionMateProcessor::isSlotFallbackActive(int slot) const {
    return vstHost->isSlotFallbackActive(slot);
}

void SessionMateProcessor::selectBuiltInForSlot(int slot) {
    if (slot < 0 || slot >= kNumSlots) return;
    vstHost->unloadSlot(slot);
    bandSynth->clearSfzForSlot(slot);
    slotSourceModes[static_cast<size_t>(slot)].store(static_cast<int>(SlotSourceMode::BuiltIn),
                                                     std::memory_order_release);
}

bool SessionMateProcessor::selectVst3ForSlot(int slot, const std::string& pluginIdentifier, std::string& outError) {
    if (slot < 0 || slot >= kNumSlots) return false;
    if (vstHost->loadPluginByIdSimple(slot, pluginIdentifier, outError)) {
        slotSourceModes[static_cast<size_t>(slot)].store(static_cast<int>(SlotSourceMode::HostedVST3),
                                                         std::memory_order_release);
        return true;
    }
    return false;
}

bool SessionMateProcessor::loadSfzForSlot(int slot, const juce::File& sfzOrWavFile, std::string& outError) {
    if (slot < 0 || slot >= kNumSlots) return false;
    if (bandSynth->loadSfzForSlot(slot, sfzOrWavFile.getFullPathName().toStdString(), outError)) {
        vstHost->unloadSlot(slot);
        {
            std::lock_guard<std::mutex> lock(metadataMutex);
            slotSfzPaths[static_cast<size_t>(slot)] = sfzOrWavFile.getFullPathName().toStdString();
        }
        slotSourceModes[static_cast<size_t>(slot)].store(static_cast<int>(SlotSourceMode::CustomSFZ),
                                                         std::memory_order_release);
        return true;
    }
    return false;
}

void SessionMateProcessor::openSlotPluginGui(int slot) {
    vstHost->openPluginEditor(slot);
}

bool SessionMateProcessor::hasHostedPluginInSlot(int slot) const {
    return vstHost->hasActivePlugin(slot);
}

juce::File SessionMateProcessor::createDragMidiFile(int slotFilter) const {
    midiHistory->drainFromAudioThread();
    const auto snap = getUiSnapshot();
    return midiHistory->createDragTempMidiFile(slotFilter, snap.transport.bpm, snap.transport.timeSig);
}

bool SessionMateProcessor::exportMidiToFile(const juce::File& file, int slotFilter) const {
    midiHistory->drainFromAudioThread();
    const auto snap = getUiSnapshot();
    return midiHistory->exportToMidiFile(file, slotFilter, snap.transport.bpm, snap.transport.timeSig);
}

void SessionMateProcessor::clearMidiHistory() {
    midiHistory->clear();
}

// ============================================================================
// Portable .sessionmate Preset & DAW State Serialization
// ============================================================================

juce::String SessionMateProcessor::serializePresetToJson() const {
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("format", "SessionMatePreset");
    root->setProperty("version", 2);

    auto state = const_cast<juce::AudioProcessorValueTreeState&>(apvts).copyState();
    if (auto xml = state.createXml()) {
        root->setProperty("apvtsXml", xml->toString());
    }

    root->setProperty("chordChart", juce::String(getChordChartText()));

    juce::Array<juce::var> slotsArray;
    for (int s = 0; s < kNumSlots; ++s) {
        auto slotObj = std::make_unique<juce::DynamicObject>();
        slotObj->setProperty("slot", s);
        slotObj->setProperty("sourceMode", static_cast<int>(getSlotSourceMode(s)));
        slotObj->setProperty("vstIdentifier", juce::String(vstHost->getSlotDesiredId(s)));
        slotObj->setProperty("vstName", juce::String(vstHost->getSlotDisplayName(s)));

        auto vstState = vstHost->getSlotSavedState(s);
        if (vstState.getSize() > 0) {
            slotObj->setProperty("vstStateBase64", vstState.toBase64Encoding());
        } else {
            slotObj->setProperty("vstStateBase64", juce::String());
        }

        {
            std::lock_guard<std::mutex> lock(metadataMutex);
            slotObj->setProperty("sfzPath", juce::String(slotSfzPaths[static_cast<size_t>(s)]));
        }

        slotsArray.add(juce::var(slotObj.release()));
    }
    root->setProperty("slots", slotsArray);

    return juce::JSON::toString(juce::var(root.release()), true);
}

bool SessionMateProcessor::deserializePresetFromJson(const juce::String& jsonText, std::string& outStatus) {
    const juce::var parsed = juce::JSON::parse(jsonText);
    if (!parsed.isObject()) {
        outStatus = "Invalid SessionMate JSON";
        return false;
    }

    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr || obj->getProperty("format").toString() != "SessionMatePreset") {
        outStatus = "Unrecognized preset format";
        return false;
    }

    const juce::String apvtsXmlStr = obj->getProperty("apvtsXml").toString();
    if (apvtsXmlStr.isNotEmpty()) {
        if (auto xml = juce::XmlDocument::parse(apvtsXmlStr)) {
            if (xml->hasTagName(apvts.state.getType())) {
                apvts.replaceState(juce::ValueTree::fromXml(*xml));
            }
        }
    }

    const juce::String chartStr = obj->getProperty("chordChart").toString();
    if (chartStr.isNotEmpty()) {
        setChordChartText(chartStr.toStdString());
    }

    int fallbackCount = 0;
    const juce::var slotsVar = obj->getProperty("slots");
    if (auto* arr = slotsVar.getArray()) {
        for (const auto& item : *arr) {
            auto* sObj = item.getDynamicObject();
            if (!sObj) continue;

            const int s = static_cast<int>(sObj->getProperty("slot"));
            if (s < 0 || s >= kNumSlots) continue;

            const auto mode = static_cast<SlotSourceMode>(static_cast<int>(sObj->getProperty("sourceMode")));
            const std::string vstId = sObj->getProperty("vstIdentifier").toString().toStdString();
            const std::string vstName = sObj->getProperty("vstName").toString().toStdString();
            const juce::String vstStateB64 = sObj->getProperty("vstStateBase64").toString();
            const std::string sfzPath = sObj->getProperty("sfzPath").toString().toStdString();

            if (mode == SlotSourceMode::HostedVST3 && !vstId.empty()) {
                juce::MemoryBlock stateBlock;
                if (vstStateB64.isNotEmpty()) {
                    stateBlock.fromBase64Encoding(vstStateB64);
                }
                bool usedFallback = false;
                std::string loadErr;
                vstHost->loadPluginByIdentifier(s, vstId, vstName, stateBlock,
                                                currentSampleRate, currentBlockSize,
                                                usedFallback, loadErr);
                slotSourceModes[static_cast<size_t>(s)].store(static_cast<int>(SlotSourceMode::HostedVST3),
                                                              std::memory_order_release);
                if (usedFallback)
                    ++fallbackCount;
            } else if (mode == SlotSourceMode::CustomSFZ && !sfzPath.empty()) {
                std::string err;
                if (bandSynth->loadSfzForSlot(s, sfzPath, err)) {
                    {
                        std::lock_guard<std::mutex> lock(metadataMutex);
                        slotSfzPaths[static_cast<size_t>(s)] = sfzPath;
                    }
                    slotSourceModes[static_cast<size_t>(s)].store(static_cast<int>(SlotSourceMode::CustomSFZ),
                                                                  std::memory_order_release);
                } else {
                    slotSourceModes[static_cast<size_t>(s)].store(static_cast<int>(SlotSourceMode::BuiltIn),
                                                                  std::memory_order_release);
                    ++fallbackCount;
                }
            } else {
                vstHost->unloadSlot(s);
                bandSynth->clearSfzForSlot(s);
                slotSourceModes[static_cast<size_t>(s)].store(static_cast<int>(SlotSourceMode::BuiltIn),
                                                              std::memory_order_release);
            }
        }
    }

    syncLinkBusConnection();

    if (fallbackCount > 0) {
        outStatus = "Loaded with " + std::to_string(fallbackCount) + " slot(s) using Built-In fallback";
    } else {
        outStatus = "Loaded cleanly";
    }
    return true;
}

bool SessionMateProcessor::exportSessionMatePreset(const juce::File& file) const {
    const juce::String json = serializePresetToJson();
    return file.replaceWithText(json);
}

bool SessionMateProcessor::importSessionMatePreset(const juce::File& file, std::string& outStatus) {
    if (!file.existsAsFile()) {
        outStatus = "File not found";
        return false;
    }
    return deserializePresetFromJson(file.loadFileAsString(), outStatus);
}

void SessionMateProcessor::getStateInformation(juce::MemoryBlock& destData) {
    const juce::String json = serializePresetToJson();
    const auto utf8 = json.toRawUTF8();
    destData.replaceAll(utf8, std::strlen(utf8));
}

void SessionMateProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (data == nullptr || sizeInBytes <= 0)
        return;
    const juce::String text = juce::String::fromUTF8(static_cast<const char*>(data), sizeInBytes);
    std::string status;
    deserializePresetFromJson(text, status);
}

} // namespace sessionmate

// JUCE Plugin Factory Entry Point
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new sessionmate::SessionMateProcessor();
}
