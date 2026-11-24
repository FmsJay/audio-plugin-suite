#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace harmonyhud {

HarmonyHUDProcessor::HarmonyHUDProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread("HarmonyHUD-Analyzer"),
      apvts_(*this, nullptr, "HarmonyHUDState", createParameterLayout()) {
    pTuningA4_         = apvts_.getRawParameterValue(param::kTuningA4);
    pSensitivity_      = apvts_.getRawParameterValue(param::kSensitivity);
    pSmoothing_        = apvts_.getRawParameterValue(param::kSmoothing);
    pGuitarTuning_     = apvts_.getRawParameterValue(param::kGuitarTuning);
    pAuditionGain_     = apvts_.getRawParameterValue(param::kAuditionGain);
    pClashSensitivity_ = apvts_.getRawParameterValue(param::kClashSensitivity);
    pFreezeHistory_    = apvts_.getRawParameterValue(param::kFreezeHistory);
    pLinkBus_          = apvts_.getRawParameterValue(param::kLinkBus);
    pViewMode_         = apvts_.getRawParameterValue(param::kViewMode);
    pOverlayMode_      = apvts_.getRawParameterValue(param::kOverlayMode);
    pFretPosition_     = apvts_.getRawParameterValue(param::kFretPosition);
    pAutoPushLive_     = apvts_.getRawParameterValue(param::kAutoPushLive);
    pClashAssist_      = apvts_.getRawParameterValue(param::kClashAssist);

    tracker_.prepare(48000.0);
    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        latestSnapshot_ = tracker_.getSnapshot();
    }
    startThread(juce::Thread::Priority::low);
}

HarmonyHUDProcessor::~HarmonyHUDProcessor() {
    signalThreadShouldExit();
    notify();
    stopThread(1500);
    linkEndpoint_.disconnect();
}

juce::AudioProcessorValueTreeState::ParameterLayout HarmonyHUDProcessor::createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{param::kTuningA4, 1},
        "A4 Reference",
        juce::NormalisableRange<float>(415.0f, 466.0f, 0.1f),
        440.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{param::kSensitivity, 1},
        "Sensitivity",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.5f),
        55.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{param::kSmoothing, 1},
        "Smoothing",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.5f),
        50.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{param::kGuitarTuning, 1},
        "Guitar Tuning",
        juce::StringArray{"Standard (EADGBE)", "Drop D (DADGBE)", "DADGAD"},
        0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{param::kAuditionGain, 1},
        "Audition Level",
        juce::NormalisableRange<float>(-24.0f, 0.0f, 0.5f),
        -12.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{param::kClashSensitivity, 1},
        "Clash Sensitivity",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.5f),
        60.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{param::kFreezeHistory, 1},
        "Freeze History",
        false));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{param::kLinkBus, 1},
        "HarmonyLink Bus",
        juce::StringArray{"Off", "Bus A", "Bus B", "Bus C", "Bus D", "Bus E", "Bus F", "Bus G", "Bus H"},
        0));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{param::kViewMode, 1},
        "View Mode",
        juce::StringArray{"Input", "Band", "Both + Clash"},
        2));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{param::kOverlayMode, 1},
        "Scale Overlay",
        juce::StringArray{"Off", "Chord Tones", "3rd/7th Guide", "Pentatonic", "Active Mode", "Tensions"},
        1));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{param::kFretPosition, 1},
        "Fret Position",
        juce::StringArray{"Auto", "Open", "Pos 5", "Pos 7", "Pos 9"},
        0));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{param::kAutoPushLive, 1},
        "Auto-Push Live",
        true));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{param::kClashAssist, 1},
        "Clash Assist",
        true));

    return {params.begin(), params.end()};
}

void HarmonyHUDProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/) {
    sampleRate_.store(sampleRate, std::memory_order_relaxed);
    mainAudioFifo_.reset();
    sidechainAudioFifo_.reset();
    midiEventFifo_.reset();
    previewSynth_.prepare(sampleRate);
    previewSynth_.setReferenceA4(pTuningA4_ ? pTuningA4_->load() : 440.0f);

    syncLinkBusConnectionIfNeeded();

    {
        std::lock_guard<std::mutex> lock(analysisMutex_);
        tracker_.prepare(sampleRate);
        if (pTuningA4_) tracker_.setReferenceA4(pTuningA4_->load());
        if (pSensitivity_) tracker_.setSensitivity(pSensitivity_->load() * 0.01f);
        if (pSmoothing_) tracker_.setSmoothing(pSmoothing_->load() * 0.01f);
        if (pClashSensitivity_) tracker_.setClashSensitivity(pClashSensitivity_->load() * 0.01f);
    }
    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        latestSnapshot_ = tracker_.getSnapshot();
    }
}

void HarmonyHUDProcessor::releaseResources() {
    previewSynth_.reset();
}

bool HarmonyHUDProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& mainOut = layouts.getMainOutputChannelSet();
    const auto& mainIn  = layouts.getMainInputChannelSet();

    if (mainOut != juce::AudioChannelSet::mono() &&
        mainOut != juce::AudioChannelSet::stereo())
        return false;

    if (mainIn != mainOut)
        return false;

    if (layouts.inputBuses.size() > 1) {
        const auto& sc = layouts.inputBuses[1];
        if (!sc.isDisabled() &&
            sc != juce::AudioChannelSet::mono() &&
            sc != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

void HarmonyHUDProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                       juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    // 1. Update host transport metadata (lock-free atomics)
    if (auto* ph = getPlayHead()) {
        if (auto pos = ph->getPosition()) {
            if (auto bpm = pos->getBpm())
                currentBpm_.store(*bpm, std::memory_order_relaxed);
            if (auto ppq = pos->getPpqPosition())
                currentPpq_.store(*ppq, std::memory_order_relaxed);
            isPlaying_.store(pos->getIsPlaying(), std::memory_order_relaxed);
        }
    }

    // 2. Capture Main Input mono sum into lock-free FIFO (ZERO modification to audio buffer!)
    auto mainBus = getBusBuffer(buffer, true, 0);
    const int mainCh = mainBus.getNumChannels();
    if (mainCh >= 2) {
        mainAudioFifo_.pushMonoSum(mainBus.getReadPointer(0),
                                   mainBus.getReadPointer(1),
                                   static_cast<size_t>(numSamples));
    } else if (mainCh == 1) {
        mainAudioFifo_.pushMonoSum(mainBus.getReadPointer(0),
                                   nullptr,
                                   static_cast<size_t>(numSamples));
    }

    // 3. Capture optional Sidechain Input mono sum into lock-free FIFO
    bool hasSidechainBus = false;
    if (getBusCount(true) > 1) {
        auto* scBusObj = getBus(true, 1);
        if (scBusObj != nullptr && scBusObj->isEnabled()) {
            auto scBus = getBusBuffer(buffer, true, 1);
            const int scCh = scBus.getNumChannels();
            if (scCh >= 2) {
                hasSidechainBus = true;
                sidechainAudioFifo_.pushMonoSum(scBus.getReadPointer(0),
                                                scBus.getReadPointer(1),
                                                static_cast<size_t>(numSamples));
            } else if (scCh == 1) {
                hasSidechainBus = true;
                sidechainAudioFifo_.pushMonoSum(scBus.getReadPointer(0),
                                                nullptr,
                                                static_cast<size_t>(numSamples));
            }
        }
    }
    sidechainPresent_.store(hasSidechainBus, std::memory_order_relaxed);

    // 4. Capture incoming MIDI note events into lock-free FIFO (MIDI passes through untouched)
    for (const auto metadata : midiMessages) {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn()) {
            midiEventFifo_.push({static_cast<uint8_t>(msg.getNoteNumber()), 1u});
        } else if (msg.isNoteOff()) {
            midiEventFifo_.push({static_cast<uint8_t>(msg.getNoteNumber()), 0u});
        } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
            midiEventFifo_.push({255u, 0u});
        }
    }

    // 5. Optional chord-substitution audition preview synth.
    //    When idle, touches ZERO samples -> 100% bit-transparent audio pass-through!
    previewSynth_.setReferenceA4(pTuningA4_ ? pTuningA4_->load() : 440.0f);
    auto outBus = getBusBuffer(buffer, false, 0);
    if (outBus.getNumChannels() >= 2) {
        previewSynth_.processAdd(outBus.getWritePointer(0), outBus.getWritePointer(1), numSamples);
    } else if (outBus.getNumChannels() == 1) {
        previewSynth_.processAdd(outBus.getWritePointer(0), nullptr, numSamples);
    }
}

void HarmonyHUDProcessor::syncLinkBusConnectionIfNeeded() {
    std::lock_guard<std::mutex> lock(linkControlMutex_);
    if (customSuffixOverride_)
        return;

    const int desiredBus = getLinkBusIndex();
    if (desiredBus == connectedBusIndex_)
        return;

    if (desiredBus < 0) {
        linkEndpoint_.disconnect();
        connectedBusIndex_ = -1;
    } else {
        if (linkEndpoint_.connect(desiredBus, nullptr))
            connectedBusIndex_ = desiredBus;
    }
}

void HarmonyHUDProcessor::connectLinkBus(int busIndex, const char* customSuffix) {
    {
        std::lock_guard<std::mutex> lock(linkControlMutex_);
        if (busIndex < 0 || busIndex >= suite::link::kNumBuses) {
            customSuffixOverride_ = false;
            customLinkSuffix_.clear();
            linkEndpoint_.disconnect();
            connectedBusIndex_ = -1;
        } else {
            customSuffixOverride_ = (customSuffix != nullptr && customSuffix[0] != '\0');
            customLinkSuffix_ = customSuffixOverride_ ? std::string(customSuffix) : std::string();
            if (linkEndpoint_.connect(busIndex, customSuffixOverride_ ? customLinkSuffix_.c_str() : nullptr))
                connectedBusIndex_ = busIndex;
        }
    }

    if (auto* p = apvts_.getParameter(param::kLinkBus)) {
        const float choiceIdx = static_cast<float>((busIndex >= 0 && busIndex < suite::link::kNumBuses) ? (busIndex + 1) : 0);
        p->setValueNotifyingHost(p->convertTo0to1(choiceIdx));
    }
}

void HarmonyHUDProcessor::run() {
    while (!threadShouldExit()) {
        drainFifosAndAnalyze();
        wait(12); // ~80 Hz polling wakeup; zero CPU burn when idle
    }
}

void HarmonyHUDProcessor::drainFifosAndAnalyze() {
    syncLinkBusConnectionIfNeeded();

    std::lock_guard<std::mutex> lock(analysisMutex_);

    if (resetAnalysisRequested_.exchange(false, std::memory_order_acq_rel)) {
        tracker_.reset();
    }
    if (clearHistoryRequested_.exchange(false, std::memory_order_acq_rel)) {
        tracker_.clearHistory();
    }

    // Sync parameters
    const float a4        = pTuningA4_ ? pTuningA4_->load() : 440.0f;
    const float sens      = pSensitivity_ ? (pSensitivity_->load() * 0.01f) : 0.55f;
    const float smooth    = pSmoothing_ ? (pSmoothing_->load() * 0.01f) : 0.50f;
    const float clashSens = pClashSensitivity_ ? (pClashSensitivity_->load() * 0.01f) : 0.60f;
    const bool freezeHist = pFreezeHistory_ ? (pFreezeHistory_->load() >= 0.5f) : false;

    tracker_.setReferenceA4(a4);
    tracker_.setSensitivity(sens);
    tracker_.setSmoothing(smooth);
    tracker_.setClashSensitivity(clashSens);
    tracker_.setFreezeHistory(freezeHist);
    tracker_.setTransportInfo(currentBpm_.load(std::memory_order_relaxed),
                              currentPpq_.load(std::memory_order_relaxed),
                              isPlaying_.load(std::memory_order_relaxed));

    // Read BandToHudPayload from SuiteHarmonyLink (if connected) BEFORE analyzing so
    // Player-vs-Band Clash Radar evaluates with fresh band slot notes!
    if (linkEndpoint_.isConnected()) {
        suite::link::BandToHudPayload bandPayload{};
        bool bandAlive = false;
        const bool ok = linkEndpoint_.readBand(bandPayload, bandAlive);
        auto bandState = fromBandPayload(bandPayload,
                                         ok && bandAlive,
                                         linkEndpoint_.currentBus(),
                                         tracker_.getSnapshot().currentKey);
        tracker_.updateBandLinkState(bandState);
        if (bandState.connected && bandState.bpm > 20.0)
            currentBpm_.store(bandState.bpm, std::memory_order_relaxed);
    } else {
        BandLinkState disconnected{};
        disconnected.connected = false;
        disconnected.busIndex = -1;
        tracker_.updateBandLinkState(disconnected);
    }

    // Drain MIDI events
    bool anyMidi = false;
    MidiNoteEvent ev{};
    while (midiEventFifo_.pop(ev)) {
        anyMidi = true;
        if (ev.note == 255u) {
            tracker_.clearMidiNotes();
        } else {
            tracker_.setMidiNoteState(static_cast<int>(ev.note), ev.isOn != 0);
        }
    }

    // Drain audio FIFOs in chunks
    const bool scEnabled = sidechainPresent_.load(std::memory_order_relaxed);
    int framesProcessed = 0;

    for (int iter = 0; iter < 16; ++iter) {
        const size_t nMain = mainAudioFifo_.pop(drainMainBuf_.data(), drainMainBuf_.size());
        size_t nSide = 0;
        if (scEnabled) {
            nSide = sidechainAudioFifo_.pop(drainSideBuf_.data(), nMain > 0 ? nMain : drainSideBuf_.size());
        } else {
            sidechainAudioFifo_.pop(drainSideBuf_.data(), drainSideBuf_.size());
        }

        if (nMain == 0 && nSide == 0)
            break;

        if (nMain > 0) {
            const float* scPtr = nullptr;
            if (scEnabled && nSide > 0) {
                if (nSide < nMain) {
                    std::fill(drainSideBuf_.begin() + static_cast<ptrdiff_t>(nSide),
                              drainSideBuf_.begin() + static_cast<ptrdiff_t>(nMain),
                              0.0f);
                }
                scPtr = drainSideBuf_.data();
            }
            framesProcessed += tracker_.processAudioBlocks(drainMainBuf_.data(), scPtr, static_cast<int>(nMain));
        }
    }

    if (anyMidi && framesProcessed == 0) {
        PolyphonicCqtNmf::FrameFeatures silentFeat{};
        tracker_.processMainFrame(silentFeat, nullptr);
    }

    auto snap = tracker_.getSnapshot();

    // Publish HudToBandPayload onto SuiteHarmonyLink (lock-free seqlock!)
    if (linkEndpoint_.isConnected()) {
        const bool autoPush    = pAutoPushLive_ ? (pAutoPushLive_->load() >= 0.5f) : true;
        const bool clashAssist = pClashAssist_ ? (pClashAssist_->load() >= 0.5f) : true;

        uint32_t rSeq = 0;
        ChordSymbol rChord;
        ReharmPushTiming rTiming = ReharmPushTiming::NextBar;
        uint8_t rLockBars = 1;
        uint32_t cSeq = 0;
        std::vector<ChordHistoryEntry> cChart;
        {
            std::lock_guard<std::mutex> lck(linkControlMutex_);
            rSeq = reharmSeq_;
            rChord = lastReharmChord_;
            rTiming = lastReharmTiming_;
            rLockBars = lastReharmLockBars_;
            cSeq = chartSeq_;
            cChart = lastSentChart_;
        }

        auto hudPayload = buildHudPayload(snap, autoPush, clashAssist,
                                          rSeq, rChord, rTiming, rLockBars,
                                          cSeq, cChart);
        linkEndpoint_.publishHud(hudPayload);
    }

    {
        std::lock_guard<std::mutex> snapLock(snapshotMutex_);
        latestSnapshot_ = std::move(snap);
    }
}

void HarmonyHUDProcessor::flushAnalysisNow() {
    drainFifosAndAnalyze();
}

AnalysisSnapshot HarmonyHUDProcessor::getSnapshot() const {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    return latestSnapshot_;
}

GuitarTuning HarmonyHUDProcessor::getGuitarTuning() const {
    int idx = pGuitarTuning_ ? static_cast<int>(std::round(pGuitarTuning_->load())) : 0;
    return static_cast<GuitarTuning>(std::clamp(idx, 0, 2));
}

ViewMode HarmonyHUDProcessor::getViewMode() const {
    int idx = pViewMode_ ? static_cast<int>(std::round(pViewMode_->load())) : 2;
    return static_cast<ViewMode>(std::clamp(idx, 0, 2));
}

OverlayMode HarmonyHUDProcessor::getOverlayMode() const {
    int idx = pOverlayMode_ ? static_cast<int>(std::round(pOverlayMode_->load())) : 1;
    return static_cast<OverlayMode>(std::clamp(idx, 0, 5));
}

FretPositionFilter HarmonyHUDProcessor::getFretPosition() const {
    int idx = pFretPosition_ ? static_cast<int>(std::round(pFretPosition_->load())) : 0;
    return static_cast<FretPositionFilter>(std::clamp(idx, 0, 4));
}

int HarmonyHUDProcessor::getLinkBusIndex() const {
    int choice = pLinkBus_ ? static_cast<int>(std::round(pLinkBus_->load())) : 0;
    return (choice >= 1 && choice <= suite::link::kNumBuses) ? (choice - 1) : -1;
}

void HarmonyHUDProcessor::pushReharmToBand(const ChordSymbol& chord,
                                           ReharmPushTiming timing,
                                           uint8_t lockBars) {
    if (!chord.isValid()) return;
    {
        std::lock_guard<std::mutex> lck(linkControlMutex_);
        ++reharmSeq_;
        lastReharmChord_ = chord;
        lastReharmTiming_ = timing;
        lastReharmLockBars_ = std::max<uint8_t>(1u, lockBars);
    }
    notify();
}

void HarmonyHUDProcessor::sendChartToBand() {
    auto snap = getSnapshot();
    std::vector<ChordHistoryEntry> chart = snap.history;
    if (chart.empty() && snap.currentChord.isValid()) {
        ChordHistoryEntry e;
        e.chord = snap.currentChord;
        e.roman = snap.currentRoman;
        e.barPosition = 1.0;
        e.durationBeats = 4.0;
        e.midiNotes = snap.currentChord.voicingMidiNotes(3);
        chart.push_back(std::move(e));
    }
    if (chart.empty()) return;

    {
        std::lock_guard<std::mutex> lck(linkControlMutex_);
        ++chartSeq_;
        lastSentChart_ = std::move(chart);
    }
    notify();
}

void HarmonyHUDProcessor::pullChartFromBand() {
    std::lock_guard<std::mutex> lock(analysisMutex_);
    auto snap = tracker_.getSnapshot();
    if (!snap.bandLink.bandChart.empty()) {
        tracker_.setHistory(snap.bandLink.bandChart);
        auto updated = tracker_.getSnapshot();
        std::lock_guard<std::mutex> snapLock(snapshotMutex_);
        latestSnapshot_ = std::move(updated);
    }
}

void HarmonyHUDProcessor::editHistoryChord(size_t index, const ChordSymbol& chord) {
    std::lock_guard<std::mutex> lock(analysisMutex_);
    tracker_.replaceHistoryEntry(index, chord);
    auto updated = tracker_.getSnapshot();
    std::lock_guard<std::mutex> snapLock(snapshotMutex_);
    latestSnapshot_ = std::move(updated);
}

void HarmonyHUDProcessor::deleteHistoryChord(size_t index) {
    std::lock_guard<std::mutex> lock(analysisMutex_);
    tracker_.removeHistoryEntry(index);
    auto updated = tracker_.getSnapshot();
    std::lock_guard<std::mutex> snapLock(snapshotMutex_);
    latestSnapshot_ = std::move(updated);
}

void HarmonyHUDProcessor::appendHistoryChord(const ChordSymbol& chord) {
    std::lock_guard<std::mutex> lock(analysisMutex_);
    tracker_.appendHistoryEntry(chord);
    auto updated = tracker_.getSnapshot();
    std::lock_guard<std::mutex> snapLock(snapshotMutex_);
    latestSnapshot_ = std::move(updated);
}

void HarmonyHUDProcessor::auditionChord(const std::vector<int>& midiNotes, float durationSec) {
    const float db = pAuditionGain_ ? pAuditionGain_->load() : -12.0f;
    const float lin = std::pow(10.0f, db / 20.0f);
    previewSynth_.triggerChord(midiNotes, durationSec, lin);
}

void HarmonyHUDProcessor::auditionSingleNote(int midiNote, float durationSec) {
    auditionChord({midiNote}, durationSec);
}

void HarmonyHUDProcessor::clearHistory() {
    clearHistoryRequested_.store(true, std::memory_order_release);
    notify();
}

void HarmonyHUDProcessor::resetAnalysis() {
    resetAnalysisRequested_.store(true, std::memory_order_release);
    notify();
}

void HarmonyHUDProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts_.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    if (xml != nullptr)
        copyXmlToBinary(*xml, destData);
}

void HarmonyHUDProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(apvts_.state.getType()))
        apvts_.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* HarmonyHUDProcessor::createEditor() {
    return new HarmonyHUDEditor(*this);
}

} // namespace harmonyhud

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new harmonyhud::HarmonyHUDProcessor();
}
