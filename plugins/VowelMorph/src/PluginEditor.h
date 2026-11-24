#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <SuiteTheme.h>

#include "PluginProcessor.h"
#include "ui/VowelSpacePad.h"
#include "ui/SyllableSeqBar.h"
#include "ui/LinkBarComponent.h"

#include <memory>

class VowelMorphAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       private juce::Timer {
public:
    explicit VowelMorphAudioProcessorEditor(VowelMorphAudioProcessor& processor);
    ~VowelMorphAudioProcessorEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateModSubPanelVisibility(int modMode);

    VowelMorphAudioProcessor& processor_;
    suite::ui::SuiteLookAndFeel lookAndFeel_ { suite::ui::accentViolet };

    suite::ui::HeaderBar headerBar_;
    vowelmorph::ui::LinkBarComponent linkBar_;
    vowelmorph::ui::VowelSpacePad vowelSpacePad_;
    vowelmorph::ui::SyllableSeqBar syllableSeqBar_;

    // Right-hand side modulation & tone inspector card
    suite::ui::SectionCard modCard_ { "Modulation & Harmony", suite::ui::accentCyan };
    std::unique_ptr<suite::ui::RotaryControl> envAttackKnob_;
    std::unique_ptr<suite::ui::RotaryControl> envReleaseKnob_;
    std::unique_ptr<suite::ui::RotaryControl> lfoRateKnob_;
    std::unique_ptr<suite::ui::RotaryControl> chordChoirKnob_;
    juce::ToggleButton lfoSyncToggle_ { "LFO Sync" };
    juce::ComboBox lfoDivCombo_;
    juce::ComboBox lfoShapeCombo_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lfoSyncAtt_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> lfoDivAtt_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> lfoShapeAtt_;

    // Bottom Macro Rotary Knob Strip (8 Studio Knobs)
    suite::ui::SectionCard macroCard_ { "Vocal Tract & Master", suite::ui::accentViolet };
    std::unique_ptr<suite::ui::RotaryControl> throatKnob_;
    std::unique_ptr<suite::ui::RotaryControl> jawKnob_;
    std::unique_ptr<suite::ui::RotaryControl> resonanceKnob_;
    std::unique_ptr<suite::ui::RotaryControl> breathKnob_;
    std::unique_ptr<suite::ui::RotaryControl> modDepthKnob_;
    std::unique_ptr<suite::ui::RotaryControl> choirWidthKnob_;
    std::unique_ptr<suite::ui::RotaryControl> driveKnob_;
    std::unique_ptr<suite::ui::RotaryControl> mixKnob_;
    std::unique_ptr<suite::ui::RotaryControl> outputKnob_;

    // Stereo Input & Output Level Meters
    suite::ui::LevelMeter inputMeter_  { true, false };
    suite::ui::LevelMeter outputMeter_ { true, false };
    juce::Label inMeterLabel_;
    juce::Label outMeterLabel_;

    int lastModMode_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VowelMorphAudioProcessorEditor)
};
