#include "PluginEditor.h"

#include <cmath>

namespace harmonyhud {

HarmonyHUDEditor::HarmonyHUDEditor(HarmonyHUDProcessor& p)
    : AudioProcessorEditor(&p), processor_(p) {
    setLookAndFeel(&lookAndFeel_);

    // Link Bus selector (Off / Bus A..H)
    linkBusSelector_.addItem("Link: Off", 1);
    for (int i = 0; i < suite::link::kNumBuses; ++i)
        linkBusSelector_.addItem(juce::String("Link: Bus ") + suite::link::busLabel(i), i + 2);
    linkBusSelector_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(linkBusSelector_);
    linkBusAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor_.getAPVTS(), param::kLinkBus, linkBusSelector_);

    // View Mode selector (Input | Band | Both + Clash)
    viewModeSelector_.addItem("View: Input", 1);
    viewModeSelector_.addItem("View: Band", 2);
    viewModeSelector_.addItem("View: Both + Clash", 3);
    viewModeSelector_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(viewModeSelector_);
    viewModeAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor_.getAPVTS(), param::kViewMode, viewModeSelector_);

    // Scale / Guide-Tone / Improvisation Overlay selector
    overlayModeSelector_.addItem("Overlay: Off", 1);
    overlayModeSelector_.addItem("Overlay: Chord Tones", 2);
    overlayModeSelector_.addItem("Overlay: 3rd/7th Guide", 3);
    overlayModeSelector_.addItem("Overlay: Pentatonic", 4);
    overlayModeSelector_.addItem("Overlay: Active Mode", 5);
    overlayModeSelector_.addItem("Overlay: Tensions", 6);
    overlayModeSelector_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(overlayModeSelector_);
    overlayModeAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor_.getAPVTS(), param::kOverlayMode, overlayModeSelector_);

    // Guitar tuning ComboBox
    tuningSelector_.addItem("EADGBE", 1);
    tuningSelector_.addItem("Drop D", 2);
    tuningSelector_.addItem("DADGAD", 3);
    tuningSelector_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(tuningSelector_);
    tuningAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor_.getAPVTS(), param::kGuitarTuning, tuningSelector_);

    // Guitar Fret Position / CAGED filter
    fretPosSelector_.addItem("Pos: Auto", 1);
    fretPosSelector_.addItem("Pos: Open", 2);
    fretPosSelector_.addItem("Pos: 5th", 3);
    fretPosSelector_.addItem("Pos: 7th", 4);
    fretPosSelector_.addItem("Pos: 9th", 5);
    fretPosSelector_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(fretPosSelector_);
    fretPosAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor_.getAPVTS(), param::kFretPosition, fretPosSelector_);

    resetButton_.onClick = [this] { processor_.resetAnalysis(); };
    addAndMakeVisible(resetButton_);

    // Sub-components & callbacks
    chordRibbon_.onAuditionSubstitution = [this](const SubstitutionSuggestion& sub) {
        processor_.auditionChord(sub.midiNotes, 1.5f);
    };
    chordRibbon_.onPushSubToBand = [this](const SubstitutionSuggestion& sub, ReharmPushTiming timing) {
        processor_.pushReharmToBand(sub.chord, timing, 1);
    };
    chordRibbon_.onAuditionHistory = [this](const ChordHistoryEntry& entry) {
        auto notes = entry.midiNotes.empty() ? entry.chord.voicingMidiNotes(3) : entry.midiNotes;
        processor_.auditionChord(notes, 1.4f);
    };
    chordRibbon_.onSendChartToBand = [this] {
        processor_.sendChartToBand();
    };
    chordRibbon_.onPullBandChart = [this] {
        processor_.pullChartFromBand();
    };
    chordRibbon_.onEditHistoryChord = [this](size_t idx, const ChordSymbol& chord) {
        processor_.editHistoryChord(idx, chord);
    };
    chordRibbon_.onDeleteHistoryChord = [this](size_t idx) {
        processor_.deleteHistoryChord(idx);
    };
    chordRibbon_.onAppendChord = [this](const ChordSymbol& chord) {
        processor_.appendHistoryChord(chord);
    };
    chordRibbon_.onClearHistory = [this] {
        processor_.clearHistory();
    };
    addAndMakeVisible(chordRibbon_);

    keyboard_.onNoteClicked = [this](int midiNote) {
        processor_.auditionSingleNote(midiNote, 0.75f);
    };
    addAndMakeVisible(keyboard_);

    fretboard_.onStrumClicked = [this](const std::vector<int>& notes) {
        if (!notes.empty())
            processor_.auditionChord(notes, 1.6f);
    };
    addAndMakeVisible(fretboard_);

    addAndMakeVisible(clashMonitor_);

    // Bottom rotary controls
    configureRotary(a4Slider_, a4Label_, "A4 TUNING", " Hz");
    configureRotary(sensitivitySlider_, sensitivityLabel_, "SENSITIVITY", "%");
    configureRotary(smoothingSlider_, smoothingLabel_, "SMOOTHING", "%");
    configureRotary(clashSensSlider_, clashSensLabel_, "CLASH SENS", "%");
    configureRotary(auditionGainSlider_, auditionGainLabel_, "AUDITION", " dB");

    a4Attach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor_.getAPVTS(), param::kTuningA4, a4Slider_);
    sensitivityAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor_.getAPVTS(), param::kSensitivity, sensitivitySlider_);
    smoothingAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor_.getAPVTS(), param::kSmoothing, smoothingSlider_);
    clashSensAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor_.getAPVTS(), param::kClashSensitivity, clashSensSlider_);
    auditionGainAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor_.getAPVTS(), param::kAuditionGain, auditionGainSlider_);

    addAndMakeVisible(autoPushToggle_);
    addAndMakeVisible(clashAssistToggle_);
    addAndMakeVisible(freezeToggle_);

    autoPushAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor_.getAPVTS(), param::kAutoPushLive, autoPushToggle_);
    clashAssistAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor_.getAPVTS(), param::kClashAssist, clashAssistToggle_);
    freezeAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor_.getAPVTS(), param::kFreezeHistory, freezeToggle_);

    setResizable(true, true);
    setResizeLimits(980, 650, 1580, 1020);
    setSize(1160, 740);

    timerCallback();
    startTimerHz(30);
}

HarmonyHUDEditor::~HarmonyHUDEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void HarmonyHUDEditor::configureRotary(juce::Slider& slider, juce::Label& label,
                                       const juce::String& title, const juce::String& suffix) {
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 16);
    slider.setTextValueSuffix(suffix);
    addAndMakeVisible(slider);

    label.setText(title, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(ui::palette::makeFont(10.5f, true));
    label.setColour(juce::Label::textColourId, ui::palette::textMuted);
    addAndMakeVisible(label);
}

void HarmonyHUDEditor::timerCallback() {
    snapshot_ = processor_.getSnapshot();
    currentBpm_ = processor_.getCurrentBpm();

    const auto viewMode    = processor_.getViewMode();
    const auto overlayMode = processor_.getOverlayMode();
    const auto tuning      = processor_.getGuitarTuning();
    const auto fretPos     = processor_.getFretPosition();

    keyboard_.setViewMode(viewMode);
    keyboard_.setOverlayMode(overlayMode);
    keyboard_.updateSnapshot(snapshot_);

    fretboard_.setTuning(tuning);
    fretboard_.setViewMode(viewMode);
    fretboard_.setOverlayMode(overlayMode);
    fretboard_.setPositionFilter(fretPos);
    fretboard_.updateSnapshot(snapshot_);

    chordRibbon_.updateSnapshot(snapshot_, currentBpm_);
    clashMonitor_.updateSnapshot(snapshot_);

    repaint();
}

void HarmonyHUDEditor::mouseDown(const juce::MouseEvent& e) {
    if (heroCardBounds_.contains(e.getPosition())) {
        const auto viewMode = processor_.getViewMode();
        const auto& displayChord = (viewMode == ViewMode::Band && snapshot_.bandLink.connected &&
                                    snapshot_.bandLink.activeChord.isValid())
                                       ? snapshot_.bandLink.activeChord
                                       : snapshot_.currentChord;
        if (displayChord.isValid())
            processor_.auditionChord(displayChord.voicingMidiNotes(3), 1.4f);
    }
}

void HarmonyHUDEditor::paint(juce::Graphics& g) {
    namespace P = ui::palette;
    g.fillAll(P::bgDark);

    auto area = getLocalBounds();

    // 1. Top Header Bar (two sleek compact strips: 48px primary + 34px X-Ray / Overlay toolbar)
    auto header = area.removeFromTop(48);
    g.setColour(P::bgPanel);
    g.fillRect(header);
    g.setColour(P::border);
    g.drawHorizontalLine(header.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));

    auto titleArea = header.reduced(14, 7);
    g.setColour(P::textPrimary);
    g.setFont(P::makeFont(19.5f, true));
    g.drawText("HARMONY", titleArea.removeFromLeft(98), juce::Justification::centredLeft);

    g.setColour(P::accentCyan);
    g.drawText("HUD", titleArea.removeFromLeft(48), juce::Justification::centredLeft);

    // Key & Mode Badge Pill
    auto keyPill = titleArea.removeFromLeft(255).reduced(4, 2);
    const int keyPct = static_cast<int>(std::round(snapshot_.currentKey.confidence * 100.0f));
    const juce::String keyStr = "KEY: " + juce::String(snapshot_.currentKey.name()) +
                                " (" + juce::String(keyPct) + "%) · " +
                                juce::String(scaleModeName(snapshot_.activeMode));
    P::drawStatusPill(g, keyPill.toFloat(), keyStr, P::accentEmerald, 11.5f);

    // Live SessionMate Link Peer Badge
    auto linkPill = titleArea.removeFromLeft(270).reduced(4, 2);
    const int busIdx = processor_.getLinkBusIndex();
    if (snapshot_.bandLink.connected) {
        juce::String peerStr = "LINKED: SessionMate";
        if (!snapshot_.bandLink.styleName.empty())
            peerStr += " · " + juce::String(snapshot_.bandLink.styleName);
        if (!snapshot_.bandLink.sectionLabel.empty())
            peerStr += " [" + juce::String(snapshot_.bandLink.sectionLabel) + "]";
        P::drawStatusPill(g, linkPill.toFloat(), peerStr, P::accentCyan, 11.0f);
    } else if (busIdx >= 0) {
        juce::String waitStr = juce::String("BUS ") + suite::link::busLabel(busIdx) + ": Waiting for Band...";
        P::drawStatusPill(g, linkPill.toFloat(), waitStr, P::accentAmber, 11.0f);
    } else {
        P::drawStatusPill(g, linkPill.toFloat(), "HARMONYLINK: OFF", P::textMuted, 11.0f);
    }

    // Secondary Toolbar background (X-Ray View, Overlay, Tuning, Fret Pos)
    auto toolbar = area.removeFromTop(36);
    g.setColour(P::bgDark.brighter(0.04f));
    g.fillRect(toolbar);
    g.setColour(P::border);
    g.drawHorizontalLine(toolbar.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));

    // 2. Hero Current Chord Card
    if (!heroCardBounds_.isEmpty()) {
        auto card = heroCardBounds_.toFloat();
        const auto viewMode = processor_.getViewMode();
        const bool showBandChord = (viewMode == ViewMode::Band && snapshot_.bandLink.connected &&
                                    snapshot_.bandLink.activeChord.isValid());
        const auto& displayChord = showBandChord ? snapshot_.bandLink.activeChord : snapshot_.currentChord;

        P::drawPanelCard(g, card, 10.0f, P::bgPanel,
                         displayChord.isValid() ? P::accentCyan.withAlpha(0.50f) : P::border);

        auto inner = heroCardBounds_.reduced(14, 9);
        auto topRow = inner.removeFromTop(16);
        g.setColour(P::textMuted);
        g.setFont(P::makeFont(10.0f, true));
        g.drawText(showBandChord ? "BAND ACTIVE CHORD (CLICK TO AUDITION)"
                                 : "CURRENT CHORD (CLICK TO AUDITION)",
                   topRow.removeFromLeft(210), juce::Justification::centredLeft);

        if (displayChord.isValid()) {
            const int confPct = static_cast<int>(std::round(displayChord.confidence * 100.0f));
            g.setColour(P::accentCyan);
            g.drawText(juce::String(confPct) + "%", topRow, juce::Justification::centredRight);
        }

        const bool flats = snapshot_.currentKey.preferFlats();
        const juce::String chordName = displayChord.isValid()
                                           ? juce::String(displayChord.name(flats))
                                           : juce::String("N.C.");

        auto bottomInfo = inner.removeFromBottom(24);
        g.setColour(displayChord.isValid() ? P::textPrimary : P::textMuted.withAlpha(0.55f));
        g.setFont(P::makeFont(42.0f, true));
        g.drawText(chordName, inner, juce::Justification::centredLeft, true);

        if (displayChord.isValid()) {
            const juce::String roman = showBandChord
                                           ? juce::String(snapshot_.currentKey.romanNumeral(displayChord))
                                           : juce::String(snapshot_.currentRoman);
            auto romanPill = bottomInfo.removeFromLeft(84).reduced(0, 1);
            P::drawStatusPill(g, romanPill.toFloat(), roman, P::accentAmber, 12.0f);

            if (displayChord.isSlashChord()) {
                g.setColour(P::accentViolet);
                g.setFont(P::makeFont(11.5f, true));
                g.drawText("Slash: " + juce::String(pitchClassName(displayChord.bass, flats)),
                           bottomInfo.reduced(8, 0), juce::Justification::centredLeft);
            } else {
                g.setColour(P::textMuted);
                g.setFont(P::makeFont(11.0f));
                g.drawText("Mode: " + juce::String(scaleModeName(snapshot_.activeMode)),
                           bottomInfo.reduced(8, 0), juce::Justification::centredLeft, true);
            }
        } else {
            g.setColour(P::textMuted);
            g.setFont(P::makeFont(11.5f));
            g.drawText("Play audio, MIDI, or link SessionMate...", bottomInfo, juce::Justification::centredLeft);
        }
    }

    // 3. Footer Control Deck background
    auto footer = getLocalBounds().removeFromBottom(88);
    g.setColour(P::bgPanel);
    g.fillRect(footer);
    g.setColour(P::border);
    g.drawHorizontalLine(footer.getY(), 0.0f, static_cast<float>(getWidth()));
}

void HarmonyHUDEditor::resized() {
    auto area = getLocalBounds();

    // Header top right: Link Bus + Reset
    auto header = area.removeFromTop(48).reduced(12, 9);
    resetButton_.setBounds(header.removeFromRight(62));
    header.removeFromRight(8);
    linkBusSelector_.setBounds(header.removeFromRight(122));

    // Secondary X-Ray / Overlay / Guitar toolbar
    auto toolbar = area.removeFromTop(36).reduced(12, 5);
    viewModeSelector_.setBounds(toolbar.removeFromLeft(168));
    toolbar.removeFromLeft(8);
    overlayModeSelector_.setBounds(toolbar.removeFromLeft(185));
    toolbar.removeFromLeft(16);
    tuningSelector_.setBounds(toolbar.removeFromLeft(116));
    toolbar.removeFromLeft(8);
    fretPosSelector_.setBounds(toolbar.removeFromLeft(112));

    // Footer control strip: 5 knobs + 3 stacked toggles on the right
    auto footer = area.removeFromBottom(88).reduced(14, 5);
    auto toggleCol = footer.removeFromRight(170);
    const int tH = toggleCol.getHeight() / 3;
    autoPushToggle_.setBounds(toggleCol.removeFromTop(tH).reduced(2, 1));
    clashAssistToggle_.setBounds(toggleCol.removeFromTop(tH).reduced(2, 1));
    freezeToggle_.setBounds(toggleCol.reduced(2, 1));

    const int colW = footer.getWidth() / 5;
    auto placeKnob = [&](juce::Slider& s, juce::Label& l, juce::Rectangle<int> cell) {
        l.setBounds(cell.removeFromTop(15));
        s.setBounds(cell.reduced(4, 0));
    };
    placeKnob(a4Slider_, a4Label_, footer.removeFromLeft(colW));
    placeKnob(sensitivitySlider_, sensitivityLabel_, footer.removeFromLeft(colW));
    placeKnob(smoothingSlider_, smoothingLabel_, footer.removeFromLeft(colW));
    placeKnob(clashSensSlider_, clashSensLabel_, footer.removeFromLeft(colW));
    placeKnob(auditionGainSlider_, auditionGainLabel_, footer);

    // Main Workspace
    auto body = area.reduced(12, 8);

    // Top Hero + Expanded 5-Chip Ribbon & Lookahead Section
    auto topSection = body.removeFromTop(148);
    heroCardBounds_ = topSection.removeFromLeft(272);
    topSection.removeFromLeft(10);
    chordRibbon_.setBounds(topSection);

    body.removeFromTop(8);

    // Middle Keyboard Strip
    const int kbHeight = std::clamp(body.getHeight() * 43 / 100, 140, 210);
    keyboard_.setBounds(body.removeFromTop(kbHeight));

    body.removeFromTop(8);

    // Bottom Split: Fretboard (64%) + Clash & Chroma Radar (36%)
    const int clashW = std::max(300, body.getWidth() * 35 / 100);
    clashMonitor_.setBounds(body.removeFromRight(clashW));
    body.removeFromRight(10);
    fretboard_.setBounds(body);
}

} // namespace harmonyhud
