#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "ui/HudPalette.h"
#include "ui/KeyboardComponent.h"
#include "ui/FretboardComponent.h"
#include "ui/ChordRibbonComponent.h"
#include "ui/ClashMonitorComponent.h"

namespace harmonyhud {

class HarmonyHUDEditor : public juce::AudioProcessorEditor,
                         public juce::DragAndDropContainer,
                         private juce::Timer {
public:
    explicit HarmonyHUDEditor(HarmonyHUDProcessor& processor);
    ~HarmonyHUDEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    void timerCallback() override;
    void configureRotary(juce::Slider& slider, juce::Label& label, const juce::String& title,
                         const juce::String& suffix);

    HarmonyHUDProcessor& processor_;
    ui::HarmonyLookAndFeel lookAndFeel_;

    // Latest snapshot cached for painting header & hero chord card
    AnalysisSnapshot snapshot_;
    double currentBpm_ = 120.0;

    // Header & Toolbar controls
    juce::ComboBox linkBusSelector_;
    juce::ComboBox viewModeSelector_;
    juce::ComboBox overlayModeSelector_;
    juce::ComboBox tuningSelector_;
    juce::ComboBox fretPosSelector_;
    juce::TextButton resetButton_{"Reset"};

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> linkBusAttach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> viewModeAttach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> overlayModeAttach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tuningAttach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> fretPosAttach_;

    // Main HUD sub-components
    ui::ChordRibbonComponent chordRibbon_;
    ui::KeyboardComponent keyboard_;
    ui::FretboardComponent fretboard_;
    ui::ClashMonitorComponent clashMonitor_;

    // Hero chord clickable bounds for click-to-audition
    juce::Rectangle<int> heroCardBounds_;

    // Bottom control strip (APVTS attached)
    juce::Slider a4Slider_, sensitivitySlider_, smoothingSlider_, clashSensSlider_, auditionGainSlider_;
    juce::Label a4Label_, sensitivityLabel_, smoothingLabel_, clashSensLabel_, auditionGainLabel_;
    juce::ToggleButton autoPushToggle_{"Auto-Push Live"};
    juce::ToggleButton clashAssistToggle_{"Clash Assist"};
    juce::ToggleButton freezeToggle_{"Freeze History"};

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> a4Attach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sensitivityAttach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smoothingAttach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> clashSensAttach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> auditionGainAttach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoPushAttach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> clashAssistAttach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttach_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HarmonyHUDEditor)
};

} // namespace harmonyhud
