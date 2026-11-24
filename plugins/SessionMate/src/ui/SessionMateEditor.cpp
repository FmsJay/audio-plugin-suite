#include "SessionMateEditor.h"
#include <algorithm>
#include <cmath>

namespace sessionmate::ui {

namespace {

void configureFooterSlider(juce::Slider& s, juce::Colour trackColour, bool bipolar) {
    s.setSliderStyle(juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    s.setColour(juce::Slider::trackColourId, trackColour);
    s.getProperties().set("bipolar", bipolar);
}

void configureFooterLabel(juce::Label& l, const juce::String& text) {
    l.setText(text, juce::dontSendNotification);
    l.setJustificationType(juce::Justification::centredLeft);
    l.setFont(Palette::font(10.5f, true));
    l.setColour(juce::Label::textColourId, Palette::textMuted);
}

} // namespace

SessionMateEditor::SessionMateEditor(juce::AudioProcessor& proc,
                                     juce::AudioProcessorValueTreeState& apvtsRef,
                                     ISessionMateUiBridge& bridgeRef)
    : juce::AudioProcessorEditor(&proc),
      apvts(apvtsRef),
      bridge(bridgeRef) {
    setLookAndFeel(&lookAndFeel);

    // -------------------------------------------------------------------------
    // Header Row 1: Transport, Sync, BPM, Tap, Key, Live Chord, Time Sig, Presets
    // -------------------------------------------------------------------------
    playStopBtn.setButtonText("PLAY");
    playStopBtn.setColour(juce::TextButton::buttonColourId, Palette::bgControl);
    playStopBtn.setColour(juce::TextButton::buttonOnColourId, Palette::accentEmerald);
    playStopBtn.setColour(juce::TextButton::textColourOffId, Palette::accentEmerald);
    playStopBtn.setColour(juce::TextButton::textColourOnId, Palette::bgDark);
    playStopBtn.setTooltip("Start / stop standalone jam transport");
    playStopBtn.onClick = [this]() {
        const bool next = !bridge.isStandalonePlaying();
        bridge.setStandalonePlaying(next);
        playStopBtn.setToggleState(next, juce::dontSendNotification);
        playStopBtn.setButtonText(next ? "STOP" : "PLAY");
    };
    addAndMakeVisible(playStopBtn);

    syncModeBox.addItem("DAW Sync", 1);
    syncModeBox.addItem("Live Follow", 2);
    syncModeBox.addItem("Tap / Jam", 3);
    syncModeBox.setTooltip("Clock & sync source: DAW Host, Live Audio Follow, or Tap/Jam");
    addAndMakeVisible(syncModeBox);
    syncModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, ParamID::syncMode, syncModeBox);

    bpmSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    bpmSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    bpmSlider.setColour(juce::Slider::rotarySliderFillColourId, Palette::accentCyan);
    bpmSlider.setTooltip("Manual / Jam tempo (BPM)");
    addAndMakeVisible(bpmSlider);
    bpmAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::manualBpm, bpmSlider);

    tapBtn.setButtonText("TAP");
    tapBtn.setColour(juce::TextButton::buttonColourId, Palette::bgControl);
    tapBtn.setColour(juce::TextButton::textColourOffId, Palette::accentAmber);
    tapBtn.setTooltip("Tap tempo (switches smoothly in Tap / Jam mode)");
    tapBtn.onClick = [this]() { bridge.triggerTapTempo(); };
    addAndMakeVisible(tapBtn);

    const char* kKeyRoots[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    for (int i = 0; i < 12; ++i) {
        keyRootBox.addItem(kKeyRoots[i], i + 1);
    }
    keyRootBox.setTooltip("Song key root");
    addAndMakeVisible(keyRootBox);
    keyRootAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, ParamID::keyRoot, keyRootBox);

    keyModeBtn.setClickingTogglesState(true);
    keyModeBtn.setColour(juce::TextButton::buttonColourId, Palette::bgControl);
    keyModeBtn.setColour(juce::TextButton::buttonOnColourId, Palette::accentViolet);
    keyModeBtn.setColour(juce::TextButton::textColourOffId, Palette::textPrimary);
    keyModeBtn.setColour(juce::TextButton::textColourOnId, Palette::bgDark);
    keyModeBtn.setTooltip("Toggle Major / Minor key quality");
    keyModeBtn.onStateChange = [this]() {
        keyModeBtn.setButtonText(keyModeBtn.getToggleState() ? "Min" : "Maj");
    };
    addAndMakeVisible(keyModeBtn);
    keyModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, ParamID::keyMinor, keyModeBtn);
    keyModeBtn.setButtonText(keyModeBtn.getToggleState() ? "Min" : "Maj");

    const char* kTimeSigs[6] = { "4/4", "3/4", "5/4", "6/8", "7/8", "12/8" };
    for (int i = 0; i < 6; ++i) {
        timeSigBox.addItem(kTimeSigs[i], i + 1);
    }
    timeSigBox.setTooltip("Meter / Time Signature");
    addAndMakeVisible(timeSigBox);
    timeSigAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, ParamID::timeSig, timeSigBox);

    savePresetBtn.setButtonText("Save Preset");
    savePresetBtn.setColour(juce::TextButton::buttonColourId, Palette::bgCard);
    savePresetBtn.setTooltip("Export portable .sessionmate band preset");
    savePresetBtn.onClick = [this]() { openSavePresetChooser(); };
    addAndMakeVisible(savePresetBtn);

    loadPresetBtn.setButtonText("Load Preset");
    loadPresetBtn.setColour(juce::TextButton::buttonColourId, Palette::bgCard);
    loadPresetBtn.setTooltip("Import .sessionmate preset (with graceful VST3 fallback)");
    loadPresetBtn.onClick = [this]() { openLoadPresetChooser(); };
    addAndMakeVisible(loadPresetBtn);

    // -------------------------------------------------------------------------
    // Header Row 2: Style A <-> Style B DNA Crossfader
    // -------------------------------------------------------------------------
    const int numStyles = std::max(1, bridge.getStyleCount());
    for (int i = 0; i < numStyles; ++i) {
        const auto name = bridge.getStyleName(i);
        const juce::String itemLabel = name.empty() ? ("Style " + juce::String(i + 1)) : juce::String(name);
        styleABox.addItem(itemLabel, i + 1);
        styleBBox.addItem(itemLabel, i + 1);
    }
    styleABox.setTooltip("Primary Style DNA (A)");
    styleBBox.setTooltip("Secondary Style DNA (B) for hybrid genre morphing");
    addAndMakeVisible(styleABox);
    addAndMakeVisible(styleBBox);

    if (auto* paramA = apvts.getParameter(ParamID::styleA)) {
        styleAAttachment = std::make_unique<juce::ParameterAttachment>(
            *paramA,
            [this, numStyles](float newValue) {
                const int idx = std::clamp(juce::roundToInt(newValue), 0, numStyles - 1);
                styleABox.setSelectedId(idx + 1, juce::dontSendNotification);
            },
            nullptr);
        styleAAttachment->sendInitialUpdate();
        styleABox.onChange = [this]() {
            if (styleAAttachment != nullptr) {
                styleAAttachment->setValueAsCompleteGesture(static_cast<float>(std::max(0, styleABox.getSelectedId() - 1)));
            }
        };
    }

    if (auto* paramB = apvts.getParameter(ParamID::styleB)) {
        styleBAttachment = std::make_unique<juce::ParameterAttachment>(
            *paramB,
            [this, numStyles](float newValue) {
                const int idx = std::clamp(juce::roundToInt(newValue), 0, numStyles - 1);
                styleBBox.setSelectedId(idx + 1, juce::dontSendNotification);
            },
            nullptr);
        styleBAttachment->sendInitialUpdate();
        styleBBox.onChange = [this]() {
            if (styleBAttachment != nullptr) {
                styleBAttachment->setValueAsCompleteGesture(static_cast<float>(std::max(0, styleBBox.getSelectedId() - 1)));
            }
        };
    }

    styleMorphSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    styleMorphSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    styleMorphSlider.setColour(juce::Slider::trackColourId, Palette::accentCyan);
    styleMorphSlider.getProperties().set("crossfader", true);
    styleMorphSlider.setTooltip("Morph groove DNA continuously between Style A and Style B");
    addAndMakeVisible(styleMorphSlider);
    styleMorphAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::styleMorph, styleMorphSlider);

    // -------------------------------------------------------------------------
    // Header Row 3: HarmonyHUD Link & Keys Voicing Strip
    // -------------------------------------------------------------------------
    harmonicSourceBox.addItem("Chart", 1);
    harmonicSourceBox.addItem("Live Audio", 2);
    harmonicSourceBox.addItem("HUD Link", 3);
    harmonicSourceBox.setTooltip("Harmonic chord source: Chord Track Chart, Live Audio Detector, or HarmonyHUD Link");
    addAndMakeVisible(harmonicSourceBox);
    harmonicSourceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, ParamID::harmonicSource, harmonicSourceBox);

    linkBusBox.addItem("Link: Off", 1);
    for (int bus = 0; bus < 8; ++bus) {
        const char busChar = static_cast<char>('A' + bus);
        linkBusBox.addItem(juce::String("Link: Bus ") + juce::String::charToString(static_cast<juce::juce_wchar>(busChar)),
                           bus + 2);
    }
    linkBusBox.setTooltip("Bidirectional HarmonyHUD Link Bus (matches HarmonyHUD Link Bus A..H)");
    addAndMakeVisible(linkBusBox);
    linkBusAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, ParamID::linkBus, linkBusBox);

    clashAvoidBtn.setButtonText("Clash Avoid");
    clashAvoidBtn.setColour(juce::ToggleButton::tickColourId, Palette::accentEmerald);
    clashAvoidBtn.setTooltip("Automatically steer accompaniment voicings away from HarmonyHUD avoid/clash tones");
    addAndMakeVisible(clashAvoidBtn);
    clashAvoidAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, ParamID::clashAvoid, clashAvoidBtn);

    keysVoicingBox.addItem("Close", 1);
    keysVoicingBox.addItem("Drop-2", 2);
    keysVoicingBox.addItem("Rootless", 3);
    keysVoicingBox.addItem("Shell 3-7", 4);
    keysVoicingBox.addItem("Open Spread", 5);
    keysVoicingBox.setTooltip("Keys & comping voicing style");
    addAndMakeVisible(keysVoicingBox);
    keysVoicingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, ParamID::keysVoicing, keysVoicingBox);

    // -------------------------------------------------------------------------
    // Middle Top: Song Arranger & Chord Track
    // -------------------------------------------------------------------------
    arrangerStrip = std::make_unique<ArrangerAndChordStrip>(apvts, bridge);
    addAndMakeVisible(*arrangerStrip);

    // -------------------------------------------------------------------------
    // Main Center: 6-Player Band Rack
    // -------------------------------------------------------------------------
    for (int s = 0; s < kNumSlots; ++s) {
        auto slotComp = std::make_unique<BandSlotComponent>(s, apvts, bridge);
        slotComp->onSlotSelected = [this](int idx) { selectSlot(idx); };
        addAndMakeVisible(*slotComp);
        bandSlots[static_cast<size_t>(s)] = std::move(slotComp);
    }

    // -------------------------------------------------------------------------
    // Footer: Performance Controls & Master Output
    // -------------------------------------------------------------------------
    configureFooterSlider(energySlider, Palette::accentCoral, false);
    energySlider.setTooltip("Global band energy & dynamics");
    addAndMakeVisible(energySlider);
    energyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::energy, energySlider);
    configureFooterLabel(energyLabel, "ENERGY");
    addAndMakeVisible(energyLabel);

    configureFooterSlider(swingSlider, Palette::accentAmber, false);
    swingSlider.setTooltip("Groove swing ratio (50% straight -> 72% heavy triplet swing)");
    addAndMakeVisible(swingSlider);
    swingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::swing, swingSlider);
    configureFooterLabel(swingLabel, "SWING");
    addAndMakeVisible(swingLabel);

    configureFooterSlider(pushPullSlider, Palette::accentViolet, true);
    pushPullSlider.setTooltip("Micro-timing pocket feel (-25ms laid-back <-> +25ms pushed)");
    addAndMakeVisible(pushPullSlider);
    pushPullAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::pushPullMs, pushPullSlider);
    configureFooterLabel(pushPullLabel, "PUSH/PULL");
    addAndMakeVisible(pushPullLabel);

    configureFooterSlider(leaveSpaceSlider, Palette::accentEmerald, false);
    leaveSpaceSlider.setTooltip("Call-and-Response ducking: band leaves space while you play");
    addAndMakeVisible(leaveSpaceSlider);
    leaveSpaceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::leaveSpace, leaveSpaceSlider);
    configureFooterLabel(leaveSpaceLabel, "LEAVE SPACE");
    addAndMakeVisible(leaveSpaceLabel);

    masterVolSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    masterVolSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    masterVolSlider.setColour(juce::Slider::rotarySliderFillColourId, Palette::accentCyan);
    masterVolSlider.setTooltip("Master output level (dB)");
    addAndMakeVisible(masterVolSlider);
    masterVolAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, ParamID::masterVolDb, masterVolSlider);

    masterLabel.setText("MASTER", juce::dontSendNotification);
    masterLabel.setJustificationType(juce::Justification::centred);
    masterLabel.setFont(Palette::font(9.5f, true));
    masterLabel.setColour(juce::Label::textColourId, Palette::textMuted);
    addAndMakeVisible(masterLabel);

    // -------------------------------------------------------------------------
    // Footer Action & Export Bar
    // -------------------------------------------------------------------------
    dragAllMidiBtn = std::make_unique<MidiDragButton>("Drag All MIDI (32 Bars)", Palette::accentCyan);
    dragAllMidiBtn->setTooltip("Drag onto any DAW track to drop multi-channel band MIDI, or click to save .mid");
    dragAllMidiBtn->onCreateDragFile = [this]() { return bridge.createDragMidiFile(-1); };
    dragAllMidiBtn->onClickFallback  = [this]() { openSaveMidiChooser(-1); };
    addAndMakeVisible(*dragAllMidiBtn);

    dragSlotMidiBtn = std::make_unique<MidiDragButton>("Drag Selected Slot (1. DRUMS)", Palette::accentAmber);
    dragSlotMidiBtn->setTooltip("Drag onto a DAW track to drop the selected player's MIDI, or click to save .mid");
    dragSlotMidiBtn->onCreateDragFile = [this]() { return bridge.createDragMidiFile(selectedSlotIndex); };
    dragSlotMidiBtn->onClickFallback  = [this]() { openSaveMidiChooser(selectedSlotIndex); };
    addAndMakeVisible(*dragSlotMidiBtn);

    saveMidiBtn.setButtonText("Save .mid");
    saveMidiBtn.setColour(juce::TextButton::buttonColourId, Palette::bgCard);
    saveMidiBtn.setTooltip("Export rolling MIDI capture buffer to a Standard MIDI File");
    saveMidiBtn.onClick = [this]() { openSaveMidiChooser(-1); };
    addAndMakeVisible(saveMidiBtn);

    scanVstBtn.setButtonText("Scan VST3");
    scanVstBtn.setColour(juce::TextButton::buttonColourId, Palette::bgCard);
    scanVstBtn.setColour(juce::TextButton::buttonOnColourId, Palette::accentAmber);
    scanVstBtn.setColour(juce::TextButton::textColourOnId, Palette::bgDark);
    scanVstBtn.setTooltip("Scan installed VST3 instrument plugins in background");
    scanVstBtn.onClick = [this]() { bridge.startVst3ScanAsync(); };
    addAndMakeVisible(scanVstBtn);

    setResizable(true, true);
    setResizeLimits(980, 650, 1920, 1200);
    setSize(1160, 760);

    startTimerHz(30);
}

SessionMateEditor::~SessionMateEditor() {
    stopTimer();
    activeChooser.reset();

    keysVoicingAttachment.reset();
    clashAvoidAttachment.reset();
    linkBusAttachment.reset();
    harmonicSourceAttachment.reset();
    masterVolAttachment.reset();
    leaveSpaceAttachment.reset();
    pushPullAttachment.reset();
    swingAttachment.reset();
    energyAttachment.reset();
    styleMorphAttachment.reset();
    styleBAttachment.reset();
    styleAAttachment.reset();
    timeSigAttachment.reset();
    keyModeAttachment.reset();
    keyRootAttachment.reset();
    bpmAttachment.reset();
    syncModeAttachment.reset();

    setLookAndFeel(nullptr);
}

void SessionMateEditor::selectSlot(int slotIndex) {
    selectedSlotIndex = std::clamp(slotIndex, 0, kNumSlots - 1);
    const juce::String sName(slotShortName(static_cast<SlotId>(selectedSlotIndex)));
    if (dragSlotMidiBtn != nullptr) {
        dragSlotMidiBtn->setLabel("Drag Slot: " + sName);
    }
    repaint();
}

void SessionMateEditor::openSaveMidiChooser(int slotFilter) {
    const juce::String title = (slotFilter < 0)
        ? "Export SessionMate Full Band MIDI"
        : ("Export " + juce::String(slotName(static_cast<SlotId>(slotFilter))) + " MIDI");

    activeChooser = std::make_unique<juce::FileChooser>(title, juce::File(), "*.mid");
    juce::Component::SafePointer<SessionMateEditor> safeThis(this);
    constexpr int chooserFlags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                               | juce::FileBrowserComponent::warnAboutOverwriting;
    activeChooser->launchAsync(chooserFlags, [safeThis, slotFilter](const juce::FileChooser& chooser) {
        if (safeThis == nullptr) return;
        auto file = chooser.getResult();
        if (file == juce::File()) return;
        if (!file.hasFileExtension(".mid")) {
            file = file.withFileExtension(".mid");
        }
        const bool ok = safeThis->bridge.exportMidiToFile(file, slotFilter);
        safeThis->toastStatusText = ok ? ("Saved " + file.getFileName().toStdString()) : "MIDI export failed";
        safeThis->toastCountdownTicks = 90;
    });
}

void SessionMateEditor::openSavePresetChooser() {
    activeChooser = std::make_unique<juce::FileChooser>(
        "Save SessionMate Band Preset", juce::File(), "*.sessionmate");
    juce::Component::SafePointer<SessionMateEditor> safeThis(this);
    constexpr int chooserFlags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                               | juce::FileBrowserComponent::warnAboutOverwriting;
    activeChooser->launchAsync(chooserFlags, [safeThis](const juce::FileChooser& chooser) {
        if (safeThis == nullptr) return;
        auto file = chooser.getResult();
        if (file == juce::File()) return;
        if (!file.hasFileExtension(".sessionmate")) {
            file = file.withFileExtension(".sessionmate");
        }
        const bool ok = safeThis->bridge.exportSessionMatePreset(file);
        safeThis->toastStatusText = ok ? ("Saved preset: " + file.getFileName().toStdString()) : "Preset save failed";
        safeThis->toastCountdownTicks = 90;
    });
}

void SessionMateEditor::openLoadPresetChooser() {
    activeChooser = std::make_unique<juce::FileChooser>(
        "Load SessionMate Band Preset", juce::File(), "*.sessionmate");
    juce::Component::SafePointer<SessionMateEditor> safeThis(this);
    constexpr int chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    activeChooser->launchAsync(chooserFlags, [safeThis](const juce::FileChooser& chooser) {
        if (safeThis == nullptr) return;
        const auto file = chooser.getResult();
        if (!file.existsAsFile()) return;
        std::string status;
        const bool ok = safeThis->bridge.importSessionMatePreset(file, status);
        safeThis->toastStatusText = !status.empty() ? status : (ok ? "Preset loaded" : "Preset load failed");
        safeThis->toastCountdownTicks = 120;
    });
}

void SessionMateEditor::timerCallback() {
    latestSnapshot = bridge.getUiSnapshot();
    activeChordText = bridge.formatChordSymbol(latestSnapshot.activeChord);
    hudDetectedChordText = bridge.formatChordSymbol(latestSnapshot.hudDetectedChord);
    hudModeText = bridge.getModeName(latestSnapshot.hudActiveMode);

    if (latestSnapshot.clashAvoidedThisBlock) {
        clashPulseTicks = 12;
    } else if (clashPulseTicks > 0) {
        --clashPulseTicks;
    }

    // Sync standalone play button state
    const bool isPlaying = bridge.isStandalonePlaying();
    if (playStopBtn.getToggleState() != isPlaying) {
        playStopBtn.setToggleState(isPlaying, juce::dontSendNotification);
        playStopBtn.setButtonText(isPlaying ? "STOP" : "PLAY");
    }

    // Update Arranger & Chord Track
    if (arrangerStrip != nullptr) {
        arrangerStrip->updateLiveState(latestSnapshot);
    }

    // Update all 6 Band Slots
    for (int s = 0; s < kNumSlots; ++s) {
        const size_t idx = static_cast<size_t>(s);
        if (bandSlots[idx] != nullptr) {
            bandSlots[idx]->updateLiveState(
                latestSnapshot.slotPeakL[idx],
                latestSnapshot.slotPeakR[idx],
                latestSnapshot.slotNoteActivity[idx],
                latestSnapshot.slotFallbackActive[idx] || bridge.isSlotFallbackActive(s),
                s == selectedSlotIndex);
        }
    }

    // Smooth master meters and listener telemetry overlays
    const float pkL = latestSnapshot.masterPeakL;
    const float pkR = latestSnapshot.masterPeakR;
    masterMeterL = (pkL > masterMeterL) ? pkL : masterMeterL * 0.85f;
    masterMeterR = (pkR > masterMeterR) ? pkR : masterMeterR * 0.85f;
    if (masterMeterL < 0.001f) masterMeterL = 0.0f;
    if (masterMeterR < 0.001f) masterMeterR = 0.0f;

    smoothListenerEnergy += (latestSnapshot.listener.energyEstimate - smoothListenerEnergy) * 0.25f;
    smoothUserActivity   += (latestSnapshot.listener.userActivity   - smoothUserActivity)   * 0.30f;

    // Update dynamic footer readouts
    energyLabel.setText("ENERGY  " + juce::String(juce::roundToInt(energySlider.getValue() * 100.0)) + "%",
                        juce::dontSendNotification);
    swingLabel.setText("SWING  " + juce::String(juce::roundToInt(swingSlider.getValue() * 100.0)) + "%",
                       juce::dontSendNotification);
    const double pp = pushPullSlider.getValue();
    pushPullLabel.setText("PUSH/PULL  " + juce::String(pp >= 0.0 ? "+" : "") + juce::String(pp, 1) + " ms",
                          juce::dontSendNotification);
    leaveSpaceLabel.setText("LEAVE SPACE  " + juce::String(juce::roundToInt(leaveSpaceSlider.getValue() * 100.0)) + "%",
                            juce::dontSendNotification);

    const double mDb = masterVolSlider.getValue();
    masterLabel.setText(juce::String(mDb > 0.05 ? "+" : "") + juce::String(mDb, 1) + " dB",
                        juce::dontSendNotification);

    // Update Drag All MIDI bar count label
    if (dragAllMidiBtn != nullptr) {
        const int bars = latestSnapshot.recordedBars > 0 ? latestSnapshot.recordedBars : kMaxBarsHistory;
        dragAllMidiBtn->setLabel("Drag All MIDI (" + juce::String(bars) + " Bars)");
    }

    // Update VST3 scan badge
    isScanningVst = bridge.isVst3Scanning();
    scanStatusText = bridge.getVst3ScanStatus();
    scanVstBtn.setToggleState(isScanningVst, juce::dontSendNotification);
    scanVstBtn.setButtonText(isScanningVst ? "Scanning..." : "Scan VST3");

    if (toastCountdownTicks > 0) {
        --toastCountdownTicks;
        if (toastCountdownTicks == 0) {
            toastStatusText.clear();
        }
    }

    repaint();
}

void SessionMateEditor::paint(juce::Graphics& g) {
    // Deep studio canvas background with subtle vertical depth gradient
    juce::ColourGradient bgGrad(Palette::bgDark.brighter(0.03f), 0.0f, 0.0f,
                                Palette::bgDark, 0.0f, static_cast<float>(getHeight()), false);
    g.setGradientFill(bgGrad);
    g.fillAll();

    // Header panel
    if (!headerPanelBounds.isEmpty()) {
        const auto hb = headerPanelBounds.toFloat();
        g.setColour(Palette::bgPanel);
        g.fillRoundedRectangle(hb, 8.0f);
        g.setColour(Palette::border);
        g.drawRoundedRectangle(hb, 8.0f, 1.0f);

        // Subtle horizontal separators between header row 1, row 2, and row 3
        g.setColour(Palette::border.withAlpha(0.55f));
        g.drawHorizontalLine(headerPanelBounds.getY() + 39, hb.getX() + 10.0f, hb.getRight() - 10.0f);
        g.drawHorizontalLine(headerPanelBounds.getY() + 75, hb.getX() + 10.0f, hb.getRight() - 10.0f);
    }

    // Glowing SESSIONMATE title
    if (!titleBounds.isEmpty()) {
        // Subtle cyan accent dot
        const float dotY = titleBounds.toFloat().getCentreY() - 3.5f;
        const float dotX = titleBounds.toFloat().getX() + 2.0f;
        g.setColour(Palette::accentCyan.withAlpha(0.32f));
        g.fillEllipse(dotX - 3.0f, dotY - 3.0f, 13.0f, 13.0f);
        g.setColour(Palette::accentCyan);
        g.fillEllipse(dotX, dotY, 7.0f, 7.0f);

        const auto textRect = titleBounds.withTrimmedLeft(15);
        g.setColour(Palette::accentCyan.withAlpha(0.22f));
        g.setFont(Palette::font(16.5f, true));
        g.drawFittedText("SESSIONMATE", textRect.translated(0, 1), juce::Justification::centredLeft, 1);

        g.setColour(Palette::textPrimary);
        g.drawFittedText("SESSIONMATE", textRect, juce::Justification::centredLeft, 1);
    }

    // Live BPM readout pill
    if (!liveBpmBounds.isEmpty()) {
        const auto bb = liveBpmBounds.toFloat();
        g.setColour(Palette::bgControl);
        g.fillRoundedRectangle(bb, 5.0f);
        g.setColour(Palette::border);
        g.drawRoundedRectangle(bb, 5.0f, 1.0f);

        const double shownBpm = (latestSnapshot.transport.bpm > 20.0)
            ? latestSnapshot.transport.bpm
            : bpmSlider.getValue();
        g.setColour(Palette::textPrimary);
        g.setFont(Palette::font(12.0f, true));
        g.drawFittedText(juce::String(shownBpm, 1) + " BPM", liveBpmBounds.reduced(4, 1),
                         juce::Justification::centred, 1);
    }

    // Live Chord readout pill [ Em9 ]
    if (!liveChordPillBounds.isEmpty()) {
        const auto cb = liveChordPillBounds.toFloat();
        g.setColour(Palette::accentCyan.withAlpha(0.12f));
        g.fillRoundedRectangle(cb, 6.0f);
        g.setColour(Palette::accentCyan.withAlpha(0.72f));
        g.drawRoundedRectangle(cb, 6.0f, 1.2f);

        const juce::String chordStr = activeChordText.empty() ? "---" : juce::String(activeChordText);
        g.setColour(Palette::accentCyan.brighter(0.30f));
        g.setFont(Palette::font(13.5f, true));
        g.drawFittedText(chordStr, liveChordPillBounds.reduced(6, 1), juce::Justification::centred, 1);
    }

    // Style DNA Morph center label
    if (!morphLabelBounds.isEmpty()) {
        const int pctB = juce::roundToInt(styleMorphSlider.getValue() * 100.0);
        const int pctA = 100 - pctB;
        g.setColour(Palette::textMuted);
        g.setFont(Palette::font(10.0f, true));
        g.drawFittedText("DNA MORPH  A " + juce::String(pctA) + "% / " + juce::String(pctB) + "% B",
                         morphLabelBounds, juce::Justification::centred, 1);
    }

    // Header Row 3: HarmonyHUD Link & Voicing Strip labels + live status pill
    if (!hudStripLabelBounds.isEmpty()) {
        g.setColour(Palette::textMuted);
        g.setFont(Palette::font(9.5f, true));
        g.drawFittedText("HUD LINK", hudStripLabelBounds, juce::Justification::centredLeft, 1);
    }
    if (!hudLinkPillBounds.isEmpty()) {
        drawHudLinkStatusPill(g, hudLinkPillBounds.toFloat());
    }
    if (!keysVoicingLabelBounds.isEmpty()) {
        g.setColour(Palette::textMuted);
        g.setFont(Palette::font(9.5f, true));
        g.drawFittedText("KEYS", keysVoicingLabelBounds, juce::Justification::centredRight, 1);
    }

    // Footer panel
    if (!footerPanelBounds.isEmpty()) {
        const auto fb = footerPanelBounds.toFloat();
        g.setColour(Palette::bgPanel);
        g.fillRoundedRectangle(fb, 8.0f);
        g.setColour(Palette::border);
        g.drawRoundedRectangle(fb, 8.0f, 1.0f);

        g.setColour(Palette::border.withAlpha(0.55f));
        g.drawHorizontalLine(footerPanelBounds.getBottom() - 44, fb.getX() + 10.0f, fb.getRight() - 10.0f);
    }

    // Live listener energy & user-activity overlay bars
    if (!energyMeterBounds.isEmpty()) {
        drawActivityOverlayBar(g, energyMeterBounds.toFloat(), smoothListenerEnergy,
                               Palette::accentCoral, "IN");
    }
    if (!userActivityMeterBounds.isEmpty()) {
        drawActivityOverlayBar(g, userActivityMeterBounds.toFloat(), smoothUserActivity,
                               Palette::accentEmerald, "YOU");
    }

    // Master stereo LED meter
    if (!masterMeterBounds.isEmpty()) {
        drawMasterMeter(g, masterMeterBounds.toFloat());
    }

    // Scan status / toast feedback badge
    if (!scanStatusBounds.isEmpty()) {
        const auto sb = scanStatusBounds.toFloat();
        const bool showToast = !toastStatusText.empty();
        const juce::String msg = showToast
            ? juce::String(toastStatusText)
            : (scanStatusText.empty() ? "VST3 Ready" : juce::String(scanStatusText));
        const auto badgeCol = showToast
            ? Palette::accentCyan
            : (isScanningVst ? Palette::accentAmber : Palette::textMuted);

        g.setColour(Palette::bgControl);
        g.fillRoundedRectangle(sb, 5.0f);
        g.setColour(isScanningVst || showToast ? badgeCol.withAlpha(0.75f) : Palette::border);
        g.drawRoundedRectangle(sb, 5.0f, 1.0f);

        g.setColour(badgeCol);
        g.setFont(Palette::font(10.5f, isScanningVst || showToast));
        g.drawFittedText(msg, scanStatusBounds.reduced(8, 2), juce::Justification::centredLeft, 1);
    }
}

void SessionMateEditor::drawHudLinkStatusPill(juce::Graphics& g, juce::Rectangle<float> area) const {
    const bool busEnabled = (linkBusBox.getSelectedId() > 1);
    const bool connected = busEnabled && latestSnapshot.hudLinkConnected;
    const float clashAlpha = std::clamp(static_cast<float>(clashPulseTicks) / 12.0f, 0.0f, 1.0f);
    const bool pulsingAmber = connected && (clashAlpha > 0.01f);

    const float corner = std::min(6.0f, area.getHeight() * 0.45f);

    if (connected) {
        const auto baseAccent = pulsingAmber
            ? Palette::accentEmerald.interpolatedWith(Palette::accentAmber, clashAlpha * 0.85f)
            : Palette::accentEmerald;

        // Soft outer glow
        g.setColour(baseAccent.withAlpha(pulsingAmber ? 0.28f : 0.16f));
        g.fillRoundedRectangle(area.expanded(1.2f), corner + 1.0f);

        g.setColour(baseAccent.withAlpha(pulsingAmber ? 0.24f : 0.14f));
        g.fillRoundedRectangle(area, corner);

        g.setColour(baseAccent.withAlpha(0.88f));
        g.drawRoundedRectangle(area, corner, pulsingAmber ? 1.4f : 1.1f);

        // Glowing LED dot
        const float ledSize = 6.5f;
        const float ledX = area.getX() + 9.0f;
        const float ledY = area.getCentreY() - ledSize * 0.5f;
        g.setColour(baseAccent.withAlpha(0.38f));
        g.fillEllipse(ledX - 2.5f, ledY - 2.5f, ledSize + 5.0f, ledSize + 5.0f);
        g.setColour(baseAccent);
        g.fillEllipse(ledX, ledY, ledSize, ledSize);

        const juce::String chordStr = hudDetectedChordText.empty() ? "---" : juce::String(hudDetectedChordText);
        const juce::String modeStr  = hudModeText.empty() ? "Ionian" : juce::String(hudModeText);
        const juce::String label = juce::String::fromUTF8("HUD LINKED \xC2\xB7 ") + chordStr + " (" + modeStr + ")";

        g.setColour(pulsingAmber ? Palette::accentAmber.brighter(0.35f) : Palette::accentEmerald.brighter(0.32f));
        g.setFont(Palette::font(11.0f, true));
        g.drawFittedText(label, area.toNearestInt().withTrimmedLeft(22).withTrimmedRight(8),
                         juce::Justification::centredLeft, 1);
    } else if (busEnabled) {
        g.setColour(Palette::bgControl.withAlpha(0.85f));
        g.fillRoundedRectangle(area, corner);
        g.setColour(Palette::border.withAlpha(0.80f));
        g.drawRoundedRectangle(area, corner, 1.0f);

        // Subtle muted amber dot
        const float ledSize = 5.5f;
        const float ledX = area.getX() + 9.0f;
        const float ledY = area.getCentreY() - ledSize * 0.5f;
        g.setColour(Palette::accentAmber.withAlpha(0.45f));
        g.fillEllipse(ledX, ledY, ledSize, ledSize);

        g.setColour(Palette::textMuted);
        g.setFont(Palette::font(11.0f, false));
        g.drawFittedText("HUD: Waiting...", area.toNearestInt().withTrimmedLeft(21).withTrimmedRight(8),
                         juce::Justification::centredLeft, 1);
    } else {
        g.setColour(Palette::bgControl.withAlpha(0.55f));
        g.fillRoundedRectangle(area, corner);
        g.setColour(Palette::border.withAlpha(0.55f));
        g.drawRoundedRectangle(area, corner, 1.0f);

        g.setColour(Palette::textMuted.withAlpha(0.65f));
        g.setFont(Palette::font(11.0f, false));
        g.drawFittedText("Link Off", area.toNearestInt().reduced(10, 1),
                         juce::Justification::centredLeft, 1);
    }
}

void SessionMateEditor::drawActivityOverlayBar(juce::Graphics& g,
                                               juce::Rectangle<float> area,
                                               float level,
                                               juce::Colour colour,
                                               const juce::String& tag) const {
    g.setColour(Palette::bgDark);
    g.fillRoundedRectangle(area, 3.0f);
    g.setColour(Palette::border.withAlpha(0.7f));
    g.drawRoundedRectangle(area, 3.0f, 0.7f);

    const float clamped = std::clamp(level, 0.0f, 1.0f);
    const auto inner = area.reduced(1.5f);
    const float fillW = inner.getWidth() * clamped;
    if (fillW > 1.0f) {
        g.setColour(colour.withAlpha(0.72f));
        g.fillRoundedRectangle(inner.getX(), inner.getY(), fillW, inner.getHeight(), 2.0f);
    }

    g.setColour(Palette::textMuted.withAlpha(0.85f));
    g.setFont(Palette::font(8.5f, true));
    g.drawFittedText(tag, area.toNearestInt().reduced(3, 0), juce::Justification::centredRight, 1);
}

void SessionMateEditor::drawMasterMeter(juce::Graphics& g, juce::Rectangle<float> area) const {
    g.setColour(Palette::bgDark);
    g.fillRoundedRectangle(area, 3.0f);
    g.setColour(Palette::border);
    g.drawRoundedRectangle(area, 3.0f, 0.8f);

    const auto inner = area.reduced(2.0f);
    const float colW = (inner.getWidth() - 2.0f) * 0.5f;

    auto drawBar = [&](juce::Rectangle<float> bar, float linearPeak) {
        const float db = (linearPeak > 1e-5f) ? (20.0f * std::log10(linearPeak)) : -60.0f;
        const float norm = std::clamp((db + 48.0f) / 48.0f, 0.0f, 1.0f);
        constexpr int segs = 12;
        const float gap = 1.2f;
        const float segH = (bar.getHeight() - gap * (segs - 1)) / segs;
        const int lit = static_cast<int>(std::round(norm * segs));

        for (int i = 0; i < segs; ++i) {
            const float y = bar.getBottom() - (i + 1) * segH - i * gap;
            juce::Colour c = Palette::accentEmerald;
            if (i >= 10) c = Palette::accentCoral;
            else if (i >= 8) c = Palette::accentAmber;
            g.setColour(i < lit ? c : Palette::bgCard.withAlpha(0.55f));
            g.fillRoundedRectangle(bar.getX(), y, bar.getWidth(), std::max(1.5f, segH), 1.0f);
        }
    };

    drawBar({ inner.getX(), inner.getY(), colW, inner.getHeight() }, masterMeterL);
    drawBar({ inner.getX() + colW + 2.0f, inner.getY(), colW, inner.getHeight() }, masterMeterR);
}

void SessionMateEditor::resized() {
    auto area = getLocalBounds().reduced(10);

    // -------------------------------------------------------------------------
    // Header Panel (3 rows: Transport + Style DNA + HarmonyHUD Link/Voicing, 112px)
    // -------------------------------------------------------------------------
    headerPanelBounds = area.removeFromTop(112);
    auto headerInner = headerPanelBounds.reduced(10, 7);

    // Header Row 1 (28px)
    auto row1 = headerInner.removeFromTop(28);
    titleBounds = row1.removeFromLeft(140);
    row1.removeFromLeft(6);

    playStopBtn.setBounds(row1.removeFromLeft(54));
    row1.removeFromLeft(6);

    syncModeBox.setBounds(row1.removeFromLeft(104));
    row1.removeFromLeft(6);

    bpmSlider.setBounds(row1.removeFromLeft(28));
    row1.removeFromLeft(4);
    liveBpmBounds = row1.removeFromLeft(74);
    row1.removeFromLeft(4);
    tapBtn.setBounds(row1.removeFromLeft(42));
    row1.removeFromLeft(8);

    keyRootBox.setBounds(row1.removeFromLeft(48));
    row1.removeFromLeft(4);
    keyModeBtn.setBounds(row1.removeFromLeft(38));
    row1.removeFromLeft(6);

    timeSigBox.setBounds(row1.removeFromLeft(58));
    row1.removeFromLeft(8);

    // Right-aligned Preset buttons
    loadPresetBtn.setBounds(row1.removeFromRight(82));
    row1.removeFromRight(6);
    savePresetBtn.setBounds(row1.removeFromRight(82));
    row1.removeFromRight(8);

    // Live Chord readout pill fills the remaining space cleanly (clamped width)
    const int pillW = std::clamp(row1.getWidth(), 72, 130);
    liveChordPillBounds = row1.withSizeKeepingCentre(pillW, row1.getHeight());

    // Header Row 2 (28px): Style A <---[====*==]---> Style B
    headerInner.removeFromTop(8);
    auto row2 = headerInner.removeFromTop(28);
    const int styleBoxW = std::clamp(row2.getWidth() * 28 / 100, 190, 320);
    styleABox.setBounds(row2.removeFromLeft(styleBoxW));
    styleBBox.setBounds(row2.removeFromRight(styleBoxW));

    row2.reduce(12, 0);
    morphLabelBounds = row2.removeFromTop(11);
    styleMorphSlider.setBounds(row2);

    // Header Row 3 (26px): HarmonyHUD Link & Keys Voicing Strip
    headerInner.removeFromTop(8);
    auto row3 = headerInner;
    hudStripLabelBounds = row3.removeFromLeft(62);
    row3.removeFromLeft(6);
    harmonicSourceBox.setBounds(row3.removeFromLeft(104));
    row3.removeFromLeft(6);
    linkBusBox.setBounds(row3.removeFromLeft(104));
    row3.removeFromLeft(8);

    keysVoicingBox.setBounds(row3.removeFromRight(114));
    row3.removeFromRight(5);
    keysVoicingLabelBounds = row3.removeFromRight(36);
    row3.removeFromRight(8);
    clashAvoidBtn.setBounds(row3.removeFromRight(104));
    row3.removeFromRight(8);

    hudLinkPillBounds = row3;

    area.removeFromTop(8);

    // -------------------------------------------------------------------------
    // Middle Top: Song Arranger & Chord Track (78px)
    // -------------------------------------------------------------------------
    if (arrangerStrip != nullptr) {
        arrangerStrip->setBounds(area.removeFromTop(78));
    }
    area.removeFromTop(8);

    // -------------------------------------------------------------------------
    // Footer Panel (108px)
    // -------------------------------------------------------------------------
    footerPanelBounds = area.removeFromBottom(108);
    area.removeFromBottom(8);

    auto footerInner = footerPanelBounds.reduced(10, 7);

    // Footer Top Row: 4 Performance Sliders + Master Rotary & Stereo Meter
    auto perfRow = footerInner.removeFromTop(52);
    masterMeterBounds = perfRow.removeFromRight(20).reduced(1, 2);
    perfRow.removeFromRight(6);

    auto masterArea = perfRow.removeFromRight(62);
    masterLabel.setBounds(masterArea.removeFromBottom(14));
    masterVolSlider.setBounds(masterArea.reduced(6, 0));
    perfRow.removeFromRight(12);

    const int perfGap = 12;
    const int perfColW = std::max(100, (perfRow.getWidth() - perfGap * 3) / 4);

    auto layoutPerfSlider = [&](juce::Rectangle<int> col, juce::Label& lbl, juce::Slider& sld,
                                juce::Rectangle<int>* overlayRect) {
        auto topLine = col.removeFromTop(16);
        if (overlayRect != nullptr) {
            *overlayRect = topLine.removeFromRight(62).reduced(0, 2);
            topLine.removeFromRight(4);
        }
        lbl.setBounds(topLine);
        sld.setBounds(col.reduced(0, 3));
    };

    layoutPerfSlider(perfRow.removeFromLeft(perfColW), energyLabel, energySlider, &energyMeterBounds);
    perfRow.removeFromLeft(perfGap);
    layoutPerfSlider(perfRow.removeFromLeft(perfColW), swingLabel, swingSlider, nullptr);
    perfRow.removeFromLeft(perfGap);
    layoutPerfSlider(perfRow.removeFromLeft(perfColW), pushPullLabel, pushPullSlider, nullptr);
    perfRow.removeFromLeft(perfGap);
    layoutPerfSlider(perfRow, leaveSpaceLabel, leaveSpaceSlider, &userActivityMeterBounds);

    footerInner.removeFromTop(10);

    // Footer Bottom Row: [Drag All MIDI] [Drag Selected Slot] [Save .mid] [Scan VST3] [Status]
    auto actionRow = footerInner;
    if (dragAllMidiBtn != nullptr) {
        dragAllMidiBtn->setBounds(actionRow.removeFromLeft(205));
    }
    actionRow.removeFromLeft(8);
    if (dragSlotMidiBtn != nullptr) {
        dragSlotMidiBtn->setBounds(actionRow.removeFromLeft(195));
    }
    actionRow.removeFromLeft(8);
    saveMidiBtn.setBounds(actionRow.removeFromLeft(88));
    actionRow.removeFromLeft(12);
    scanVstBtn.setBounds(actionRow.removeFromLeft(96));
    actionRow.removeFromLeft(8);
    scanStatusBounds = actionRow;

    // -------------------------------------------------------------------------
    // Main Center: 6-Player Band Rack (6 equal-width vertical cards)
    // -------------------------------------------------------------------------
    const int cardGap = 8;
    const int cardW = std::max(120, (area.getWidth() - cardGap * (kNumSlots - 1)) / kNumSlots);
    for (int s = 0; s < kNumSlots; ++s) {
        auto slotRect = (s == kNumSlots - 1) ? area : area.removeFromLeft(cardW);
        if (bandSlots[static_cast<size_t>(s)] != nullptr) {
            bandSlots[static_cast<size_t>(s)]->setBounds(slotRect);
        }
        if (s < kNumSlots - 1) {
            area.removeFromLeft(cardGap);
        }
    }
}

} // namespace sessionmate::ui
