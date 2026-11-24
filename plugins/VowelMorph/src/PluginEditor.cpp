#include "PluginEditor.h"

VowelMorphAudioProcessorEditor::VowelMorphAudioProcessorEditor(VowelMorphAudioProcessor& p)
    : AudioProcessorEditor(&p),
      processor_(p),
      headerBar_("VOWELMORPH", "5-Formant Vocal Tract Resonator & Syllable Sequencer", suite::ui::accentViolet),
      linkBar_(p.getAPVTS(), [this](int idx) {
          processor_.applyFactoryPreset(idx);
      }),
      syllableSeqBar_(p.getAPVTS()) {
    setLookAndFeel(&lookAndFeel_);

    addAndMakeVisible(headerBar_);
    addAndMakeVisible(linkBar_);
    addAndMakeVisible(vowelSpacePad_);
    addAndMakeVisible(syllableSeqBar_);
    addAndMakeVisible(modCard_);
    addAndMakeVisible(macroCard_);

    linkBar_.setSelectedPresetIndex(processor_.getCurrentProgram());

    // Wire 2D IPA Vowel Pad mouse drag to APVTS vowel_x / vowel_y
    vowelSpacePad_.onVowelXYChanged = [this](float x, float y) {
        auto& apvts = processor_.getAPVTS();
        if (auto* px = apvts.getParameter(vowelmorph::params::vowelX))
            px->setValueNotifyingHost(std::clamp(x, 0.0f, 1.0f));
        if (auto* py = apvts.getParameter(vowelmorph::params::vowelY))
            py->setValueNotifyingHost(std::clamp(y, 0.0f, 1.0f));
    };

    auto& apvts = processor_.getAPVTS();

    // Right Modulation & Harmony Inspector Controls
    envAttackKnob_ = std::make_unique<suite::ui::RotaryControl>(apvts, vowelmorph::params::envAttackMs, "ENV ATT", suite::ui::accentCyan);
    envAttackKnob_->setTextValueSuffix(" ms");
    addAndMakeVisible(*envAttackKnob_);

    envReleaseKnob_ = std::make_unique<suite::ui::RotaryControl>(apvts, vowelmorph::params::envReleaseMs, "ENV REL", suite::ui::accentCyan);
    envReleaseKnob_->setTextValueSuffix(" ms");
    addAndMakeVisible(*envReleaseKnob_);

    lfoRateKnob_ = std::make_unique<suite::ui::RotaryControl>(apvts, vowelmorph::params::lfoRateHz, "LFO HZ", suite::ui::accentViolet);
    lfoRateKnob_->setTextValueSuffix(" Hz");
    addAndMakeVisible(*lfoRateKnob_);

    chordChoirKnob_ = std::make_unique<suite::ui::RotaryControl>(apvts, vowelmorph::params::harmonyChoirDrone, "CHORD CHOIR", suite::ui::accentEmerald);
    chordChoirKnob_->setTextValueSuffix(" %");
    addAndMakeVisible(*chordChoirKnob_);

    addAndMakeVisible(lfoSyncToggle_);
    lfoSyncAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, vowelmorph::params::lfoSync, lfoSyncToggle_);

    for (int i = 0; i < 7; ++i)
        lfoDivCombo_.addItem("LFO " + juce::String(vowelmorph::seqRateName(i)), i + 1);
    addAndMakeVisible(lfoDivCombo_);
    lfoDivAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, vowelmorph::params::lfoDiv, lfoDivCombo_);

    const juce::StringArray shapes { "Sine", "Triangle", "Saw", "S&H" };
    for (int i = 0; i < shapes.size(); ++i)
        lfoShapeCombo_.addItem(shapes[i], i + 1);
    addAndMakeVisible(lfoShapeCombo_);
    lfoShapeAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, vowelmorph::params::lfoShape, lfoShapeCombo_);

    // Bottom Macro Rotary Knobs
    auto makeKnob = [&](const char* id, const char* label, const char* suffix, juce::Colour col) {
        auto k = std::make_unique<suite::ui::RotaryControl>(apvts, id, label, col);
        if (suffix != nullptr && suffix[0] != '\0')
            k->setTextValueSuffix(suffix);
        addAndMakeVisible(*k);
        return k;
    };

    throatKnob_     = makeKnob(vowelmorph::params::throatSize, "THROAT",    " %",  suite::ui::accentViolet);
    jawKnob_        = makeKnob(vowelmorph::params::jawOpen,    "JAW OPEN",  " %",  suite::ui::accentViolet);
    resonanceKnob_  = makeKnob(vowelmorph::params::resonance,  "RESONANCE", " %",  suite::ui::accentCyan);
    breathKnob_     = makeKnob(vowelmorph::params::breathAir,  "BREATH",    " %",  suite::ui::accentCyan);
    modDepthKnob_   = makeKnob(vowelmorph::params::modDepth,   "MOD DEPTH", " %",  suite::ui::accentAmber);
    choirWidthKnob_ = makeKnob(vowelmorph::params::choirWidth, "CHOIR",     " %",  suite::ui::accentEmerald);
    driveKnob_      = makeKnob(vowelmorph::params::drive,      "DRIVE",     " %",  suite::ui::accentCoral);
    mixKnob_        = makeKnob(vowelmorph::params::mix,        "MIX",       " %",  suite::ui::accentViolet);
    outputKnob_     = makeKnob(vowelmorph::params::outputDb,   "OUTPUT",    " dB", suite::ui::textPrimary);

    // Stereo I/O Meters
    addAndMakeVisible(inputMeter_);
    addAndMakeVisible(outputMeter_);

    auto setupMeterLabel = [this](juce::Label& lbl, const juce::String& text) {
        lbl.setText(text, juce::dontSendNotification);
        lbl.setJustificationType(juce::Justification::centred);
        lbl.setColour(juce::Label::textColourId, suite::ui::textMuted);
        lbl.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        addAndMakeVisible(lbl);
    };
    setupMeterLabel(inMeterLabel_, "IN");
    setupMeterLabel(outMeterLabel_, "OUT");

    setResizable(true, true);
    setResizeLimits(920, 580, 1440, 920);
    setSize(1040, 660);

    startTimerHz(30);
}

VowelMorphAudioProcessorEditor::~VowelMorphAudioProcessorEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void VowelMorphAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(suite::ui::bgDark);
}

void VowelMorphAudioProcessorEditor::resized() {
    auto area = getLocalBounds();

    // Top Header Bar
    headerBar_.setBounds(area.removeFromTop(42));

    area.reduce(10, 8);

    // Link / Preset / Mod Ribbon
    linkBar_.setBounds(area.removeFromTop(68));
    area.removeFromTop(8);

    // Bottom Macro Knobs + I/O Meters Strip
    auto bottomStrip = area.removeFromBottom(112);
    area.removeFromBottom(8);

    // 8-Step Syllable Sequencer Bar
    syllableSeqBar_.setBounds(area.removeFromBottom(112));
    area.removeFromBottom(8);

    // Middle Row: Left 2D IPA Vowel Space + Formant Curve (73%), Right Mod/Harmony Card (27%)
    const int rightW = std::clamp(static_cast<int>(std::round(static_cast<float>(area.getWidth()) * 0.26f)), 230, 300);
    auto rightArea = area.removeFromRight(rightW);
    area.removeFromRight(8);

    vowelSpacePad_.setBounds(area);
    modCard_.setBounds(rightArea);

    // Lay out Right Mod/Harmony Card controls
    auto modInner = modCard_.getBounds().reduced(10);
    modInner.removeFromTop(22);

    auto modRow1 = modInner.removeFromTop(88);
    const int halfW = (modRow1.getWidth() - 6) / 2;
    envAttackKnob_->setBounds(modRow1.removeFromLeft(halfW));
    modRow1.removeFromLeft(6);
    envReleaseKnob_->setBounds(modRow1);

    modInner.removeFromTop(6);
    auto modRow2 = modInner.removeFromTop(88);
    lfoRateKnob_->setBounds(modRow2.removeFromLeft(halfW));
    modRow2.removeFromLeft(6);
    chordChoirKnob_->setBounds(modRow2);

    modInner.removeFromTop(8);
    auto lfoCtrlRow1 = modInner.removeFromTop(24);
    lfoSyncToggle_.setBounds(lfoCtrlRow1.removeFromLeft(94));
    lfoCtrlRow1.removeFromLeft(6);
    lfoDivCombo_.setBounds(lfoCtrlRow1);

    modInner.removeFromTop(6);
    lfoShapeCombo_.setBounds(modInner.removeFromTop(24));

    // Lay out Bottom Strip: Stereo Meters on far right, 9 Macro Knobs inside macroCard_
    auto meterArea = bottomStrip.removeFromRight(68);
    bottomStrip.removeFromRight(8);
    macroCard_.setBounds(bottomStrip);

    auto meterLeft = meterArea.removeFromLeft(30);
    meterArea.removeFromLeft(8);
    auto meterRight = meterArea;

    inMeterLabel_.setBounds(meterLeft.removeFromTop(14));
    inputMeter_.setBounds(meterLeft);
    outMeterLabel_.setBounds(meterRight.removeFromTop(14));
    outputMeter_.setBounds(meterRight);

    auto knobRow = macroCard_.getBounds().reduced(10, 6);
    knobRow.removeFromTop(18);
    suite::ui::RotaryControl* knobs[9] = {
        throatKnob_.get(), jawKnob_.get(), resonanceKnob_.get(), breathKnob_.get(),
        modDepthKnob_.get(), choirWidthKnob_.get(), driveKnob_.get(), mixKnob_.get(), outputKnob_.get()
    };
    const int kGap = 4;
    const int kW = std::max(54, (knobRow.getWidth() - kGap * 8) / 9);
    for (int i = 0; i < 9; ++i) {
        const int w = (i == 8) ? knobRow.getWidth() : kW;
        knobs[i]->setBounds(knobRow.removeFromLeft(w));
        if (i < 8)
            knobRow.removeFromLeft(kGap);
    }
}

void VowelMorphAudioProcessorEditor::updateModSubPanelVisibility(int modMode) {
    if (modMode == lastModMode_)
        return;
    lastModMode_ = modMode;

    const bool isEnv = (modMode == static_cast<int>(vowelmorph::ModMode::EnvFollower));
    const bool isLfo = (modMode == static_cast<int>(vowelmorph::ModMode::Lfo));
    envAttackKnob_->setAlpha(isEnv ? 1.0f : 0.65f);
    envReleaseKnob_->setAlpha(isEnv ? 1.0f : 0.65f);
    lfoRateKnob_->setAlpha(isLfo ? 1.0f : 0.65f);
    lfoSyncToggle_.setAlpha(isLfo ? 1.0f : 0.65f);
    lfoDivCombo_.setAlpha(isLfo ? 1.0f : 0.65f);
    lfoShapeCombo_.setAlpha(isLfo ? 1.0f : 0.65f);
}

void VowelMorphAudioProcessorEditor::timerCallback() {
    const auto snap = processor_.getTelemetrySnapshot();
    vowelSpacePad_.updateTelemetry(snap);
    syllableSeqBar_.updateTelemetry(snap);
    linkBar_.updateTelemetry(snap);
    linkBar_.setSelectedPresetIndex(processor_.getCurrentProgram());
    inputMeter_.setLinearLevels(snap.inputLevelL, snap.inputLevelR);
    outputMeter_.setLinearLevels(snap.outputLevelL, snap.outputLevelR);
    updateModSubPanelVisibility(snap.modMode);
}
