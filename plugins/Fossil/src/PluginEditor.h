#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "ui/EuclideanRingView.h"
#include "ui/FossilPalette.h"
#include "ui/LayerInspector.h"
#include "ui/StrataStackView.h"

#include <memory>

class FossilAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   private juce::Timer
{
public:
    explicit FossilAudioProcessorEditor (FossilAudioProcessor& processor);
    ~FossilAudioProcessorEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void handleFreezeButtonDown (fossil::StreamSource source);
    void handleFreezeButtonUp();

    struct FreezeButton : public juce::TextButton
    {
        std::function<void()> onPressDown;
        std::function<void()> onReleaseUp;
        juce::Colour accentColour;

        void mouseDown (const juce::MouseEvent& e) override
        {
            juce::TextButton::mouseDown (e);
            if (onPressDown) onPressDown();
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            juce::TextButton::mouseUp (e);
            if (onReleaseUp) onReleaseUp();
        }
    };

    FossilAudioProcessor& processor_;
    fossil::ui::FossilLookAndFeel lookAndFeel_;

    // Header controls
    juce::ComboBox presetBox_;
    FreezeButton freezeTBtn_;
    FreezeButton freezeHBtn_;
    FreezeButton freezeNBtn_;
    juce::TextButton clearAllBtn_ { "CLEAR" };
    juce::ComboBox freezeModeBox_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> freezeModeAtt_;
    juce::Slider bpmSlider_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bpmAtt_;
    juce::Label bpmStatusLabel_;

    // Suite Link Bar controls (SuiteTextureLink + SuiteHarmonyLink)
    juce::Label    texLinkLabel_;
    juce::ComboBox texLinkBusBox_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> texLinkBusAtt_;
    juce::Slider   vowelModSlider_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> vowelModAtt_;

    juce::Label    harmLinkLabel_;
    juce::ComboBox harmLinkBusBox_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> harmLinkBusAtt_;
    juce::ToggleButton harmQuantizeBtn_ { "Harm Quantize" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> harmQuantizeAtt_;

    // Main center components
    fossil::ui::StrataStackView   strataStack_;
    fossil::ui::LayerInspector    layerInspector_;
    fossil::ui::EuclideanRingView euclidRing_;

    // Footer: Dry Pass & Decomposition
    juce::Slider dryTSlider_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dryTAtt_;
    juce::Label dryTLabel_;
    std::unique_ptr<fossil::ui::KnobControl> dryHKnob_;
    std::unique_ptr<fossil::ui::KnobControl> dryNKnob_;
    std::unique_ptr<fossil::ui::KnobControl> hpssSepKnob_;
    std::unique_ptr<fossil::ui::KnobControl> noiseSensKnob_;

    // Footer: Master Strata & Shimmer
    std::unique_ptr<fossil::ui::KnobControl> strataBlendKnob_;
    std::unique_ptr<fossil::ui::KnobControl> shimmerSmearKnob_;
    std::unique_ptr<fossil::ui::KnobControl> shimmerPitchKnob_;
    std::unique_ptr<fossil::ui::KnobControl> outputGainKnob_;

    // Meter smoothing & cached link badges for UI
    float uiEnergyT_   = 0.0f;
    float uiEnergyH_   = 0.0f;
    float uiEnergyN_   = 0.0f;
    float uiInPeakL_   = 0.0f;
    float uiInPeakR_   = 0.0f;
    float uiOutPeakL_  = 0.0f;
    float uiOutPeakR_  = 0.0f;

    bool uiVowelAlive_    = false;
    bool uiHarmonyAlive_  = false;
    juce::String uiVowelBadgeText_   { "Texture Link Off" };
    juce::String uiHarmonyBadgeText_ { "No Chord" };

    // Layout rects cached for paint()
    juce::Rectangle<int> headerArea_;
    juce::Rectangle<int> linkBarArea_;
    juce::Rectangle<int> vowelPeerBadgeArea_;
    juce::Rectangle<int> harmonyBadgeArea_;
    juce::Rectangle<int> liveEnergyArea_;
    juce::Rectangle<int> dryCardArea_;
    juce::Rectangle<int> masterCardArea_;
    juce::Rectangle<int> meterArea_;
    juce::Rectangle<int> euclidCardArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FossilAudioProcessorEditor)
};
