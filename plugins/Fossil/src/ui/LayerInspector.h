#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../FossilParams.h"
#include "FossilPalette.h"

#include <array>
#include <cmath>
#include <functional>
#include <memory>

namespace fossil::ui
{

// ============================================================================
// LayerInspector — Per-stratum parameter editor (L1..L4) with clean APVTS
// attachment teardown/rebind on layer switch
// ============================================================================
class LayerInspector : public juce::Component
{
public:
    LayerInspector (juce::AudioProcessorValueTreeState& apvts,
                    const FossilTelemetry& telemetry)
        : apvts_ (apvts), telemetry_ (telemetry)
    {
        // Source selector buttons [T: Trans] [H: Tonal] [N: Noise]
        const std::array<const char*, 3> srcLabels = { "T: Trans", "H: Tonal", "N: Noise" };
        const std::array<juce::Colour, 3> srcColours = { accentT, accentH, accentN };

        for (int i = 0; i < 3; ++i)
        {
            auto& btn = sourceButtons_[static_cast<size_t> (i)];
            btn.setButtonText (srcLabels[static_cast<size_t> (i)]);
            btn.setClickingTogglesState (false);
            btn.setColour (juce::TextButton::buttonOnColourId, srcColours[static_cast<size_t> (i)]);
            btn.setColour (juce::TextButton::textColourOnId, textPrimary);
            btn.onClick = [this, i]()
            {
                if (sourceAttach_ != nullptr)
                    sourceAttach_->setValueAsCompleteGesture (static_cast<float> (i));
                else
                    updateSourceButtons (static_cast<StreamSource> (i));
            };
            addAndMakeVisible (btn);
        }

        // Quick action buttons
        captureButton_.setButtonText ("Capture");
        captureButton_.setColour (juce::TextButton::buttonColourId, accentEmerald.withAlpha (0.22f));
        captureButton_.setColour (juce::TextButton::textColourOffId, accentEmerald.brighter (0.25f));
        captureButton_.onClick = [this]()
        {
            if (onCaptureIntoLayer)
                onCaptureIntoLayer (selectedLayer_, currentSource_);
        };
        addAndMakeVisible (captureButton_);

        clearButton_.setButtonText ("Clear");
        clearButton_.setColour (juce::TextButton::buttonColourId, accentCoral.withAlpha (0.18f));
        clearButton_.setColour (juce::TextButton::textColourOffId, accentCoral.brighter (0.20f));
        clearButton_.onClick = [this]()
        {
            if (onClearLayer)
                onClearLayer (selectedLayer_);
        };
        addAndMakeVisible (clearButton_);

        // 6 KnobControls in 2x3 grid
        const auto initAccent = colourForSource (currentSource_);
        pitchKnob_ = std::make_unique<KnobControl> (apvts_, param_ids::layerPitch[0], "PITCH", initAccent, "st");
        grainKnob_ = std::make_unique<KnobControl> (apvts_, param_ids::layerGrain[0], "GRAIN", initAccent, "ms");
        driftKnob_ = std::make_unique<KnobControl> (apvts_, param_ids::layerDrift[0], "DRIFT", initAccent, "%");
        tiltKnob_  = std::make_unique<KnobControl> (apvts_, param_ids::layerTilt[0],  "TILT",  initAccent, "");
        gainKnob_  = std::make_unique<KnobControl> (apvts_, param_ids::layerGain[0],  "GAIN",  initAccent, "%");
        widthKnob_ = std::make_unique<KnobControl> (apvts_, param_ids::layerWidth[0], "WIDTH", initAccent, "%");

        tiltKnob_->setValueFormatter ([] (double v) -> juce::String
        {
            const int pct = juce::roundToInt (std::abs (v) * 100.0);
            if (v < -0.06)
                return "Warm " + juce::String (pct) + "%";
            if (v > 0.06)
                return "Bright " + juce::String (pct) + "%";
            return "Neutral";
        });

        addAndMakeVisible (*pitchKnob_);
        addAndMakeVisible (*grainKnob_);
        addAndMakeVisible (*driftKnob_);
        addAndMakeVisible (*tiltKnob_);
        addAndMakeVisible (*gainKnob_);
        addAndMakeVisible (*widthKnob_);

        // Compact Pan slider (L..C..R)
        panTitle_.setText ("PAN", juce::dontSendNotification);
        panTitle_.setFont (makeFont (10.0f, true));
        panTitle_.setColour (juce::Label::textColourId, textMuted);
        panTitle_.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (panTitle_);

        panSlider_.setSliderStyle (juce::Slider::SliderStyle::LinearHorizontal);
        panSlider_.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        panSlider_.setColour (juce::Slider::thumbColourId, initAccent);
        panSlider_.setColour (juce::Slider::trackColourId, initAccent);
        panSlider_.onValueChange = [this]() { updatePanReadout(); };
        addAndMakeVisible (panSlider_);

        panValue_.setFont (makeFont (10.0f, true));
        panValue_.setColour (juce::Label::textColourId, textPrimary);
        panValue_.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (panValue_);

        // Bottom toggles: [Mute] [Solo] [Euclid Gate]
        muteToggle_.setButtonText ("Mute");
        muteToggle_.setColour (juce::ToggleButton::tickColourId, accentT);
        addAndMakeVisible (muteToggle_);

        soloToggle_.setButtonText ("Solo");
        soloToggle_.setColour (juce::ToggleButton::tickColourId, accentEmerald);
        addAndMakeVisible (soloToggle_);

        gateToggle_.setButtonText ("Euclid Gate");
        gateToggle_.setColour (juce::ToggleButton::tickColourId, accentH);
        addAndMakeVisible (gateToggle_);

        setSelectedLayer (0);
    }

    std::function<void (int, StreamSource)> onCaptureIntoLayer;
    std::function<void (int)>               onClearLayer;

    void setSelectedLayer (int layerIndex)
    {
        selectedLayer_ = juce::jlimit (0, kNumStrata - 1, layerIndex);
        const auto idx = static_cast<size_t> (selectedLayer_);

        // 1. Cleanly tear down ALL old APVTS attachments BEFORE creating new ones
        sourceAttach_.reset();
        panAttach_.reset();
        muteAttach_.reset();
        soloAttach_.reset();
        gateAttach_.reset();

        // 2. Reattach all 6 knobs (each KnobControl resets its own attachment first)
        pitchKnob_->reattach (apvts_, param_ids::layerPitch[idx]);
        grainKnob_->reattach (apvts_, param_ids::layerGrain[idx]);
        driftKnob_->reattach (apvts_, param_ids::layerDrift[idx]);
        tiltKnob_->reattach  (apvts_, param_ids::layerTilt[idx]);
        gainKnob_->reattach  (apvts_, param_ids::layerGain[idx]);
        widthKnob_->reattach (apvts_, param_ids::layerWidth[idx]);

        // 3. Reattach Pan & bottom toggles
        if (apvts_.getParameter (param_ids::layerPan[idx]) != nullptr)
            panAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, param_ids::layerPan[idx], panSlider_);
        updatePanReadout();

        if (apvts_.getParameter (param_ids::layerMute[idx]) != nullptr)
            muteAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                apvts_, param_ids::layerMute[idx], muteToggle_);

        if (apvts_.getParameter (param_ids::layerSolo[idx]) != nullptr)
            soloAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                apvts_, param_ids::layerSolo[idx], soloToggle_);

        if (apvts_.getParameter (param_ids::layerGate[idx]) != nullptr)
            gateAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                apvts_, param_ids::layerGate[idx], gateToggle_);

        // 4. Reattach source selector
        if (auto* srcParam = apvts_.getParameter (param_ids::layerSource[idx]))
        {
            sourceAttach_ = std::make_unique<juce::ParameterAttachment> (
                *srcParam,
                [this] (float newVal)
                {
                    const auto s = static_cast<StreamSource> (juce::jlimit (0, 2, juce::roundToInt (newVal)));
                    updateSourceButtons (s);
                },
                apvts_.undoManager);
            sourceAttach_->sendInitialUpdate();
        }
        else
        {
            updateSourceButtons (StreamSource::Tonal);
        }

        repaint();
    }

    int getSelectedLayer() const noexcept { return selectedLayer_; }

    void refreshFromTelemetry()
    {
        const auto idx = static_cast<size_t> (selectedLayer_);
        const bool frozenNow = telemetry_.strata[idx].frozen.load (std::memory_order_relaxed);
        if (frozenNow != cachedFrozen_)
        {
            cachedFrozen_ = frozenNow;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        const auto accent = colourForSource (currentSource_);

        // Panel background
        g.setColour (bgPanel);
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (border);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

        // Subtle top accent stripe
        g.setColour (accent.withAlpha (0.75f));
        g.fillRoundedRectangle (bounds.getX() + 10.0f, bounds.getY() + 1.0f,
                                juce::jmax (12.0f, bounds.getWidth() - 20.0f), 2.5f, 1.25f);

        // Header text
        if (! headerBounds_.isEmpty())
        {
            g.setFont (makeFont (11.5f, true));
            g.setColour (textPrimary);
            g.drawText ("LAYER INSPECTOR (L" + juce::String (selectedLayer_ + 1) + ")",
                        headerBounds_, juce::Justification::centredLeft, false);

            // Live status pill on right of header
            const bool frozen = telemetry_.strata[static_cast<size_t> (selectedLayer_)].frozen.load (std::memory_order_relaxed);
            const juce::String statusText = frozen ? "FROZEN" : "EMPTY";
            const auto statusCol = frozen ? accent : textMuted;
            auto pill = headerBounds_.withWidth (62).withRightX (headerBounds_.getRight()).reduced (0, 2).toFloat();
            g.setColour (statusCol.withAlpha (frozen ? 0.20f : 0.10f));
            g.fillRoundedRectangle (pill, 4.0f);
            g.setColour (statusCol.withAlpha (frozen ? 0.85f : 0.45f));
            g.drawRoundedRectangle (pill, 4.0f, 0.9f);
            g.setFont (makeFont (9.5f, true));
            g.setColour (statusCol);
            g.drawText (statusText, pill.toNearestInt(), juce::Justification::centred, false);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10, 8);

        // Header
        headerBounds_ = area.removeFromTop (20);
        area.removeFromTop (6);

        // Row 1: Source buttons [T: Trans] [H: Tonal] [N: Noise] + [Capture] [Clear]
        {
            auto topRow = area.removeFromTop (24);
            const int clearW = 46;
            const int capW   = 58;
            clearButton_.setBounds (topRow.removeFromRight (clearW));
            topRow.removeFromRight (4);
            captureButton_.setBounds (topRow.removeFromRight (capW));
            topRow.removeFromRight (8);

            const int gap = 4;
            const int srcW = juce::jmax (30, (topRow.getWidth() - gap * 2) / 3);
            for (int i = 0; i < 3; ++i)
            {
                sourceButtons_[static_cast<size_t> (i)].setBounds (topRow.removeFromLeft (srcW));
                if (i < 2)
                    topRow.removeFromLeft (gap);
            }
        }

        area.removeFromTop (8);

        // Bottom toggles row: [Mute] [Solo] [Euclid Gate]
        auto bottomRow = area.removeFromBottom (26);
        {
            const int gap = 6;
            const int gateW = juce::jmax (86, bottomRow.getWidth() * 44 / 100);
            gateToggle_.setBounds (bottomRow.removeFromRight (gateW));
            bottomRow.removeFromRight (gap);
            const int halfW = juce::jmax (40, (bottomRow.getWidth() - gap) / 2);
            muteToggle_.setBounds (bottomRow.removeFromLeft (halfW));
            bottomRow.removeFromLeft (gap);
            soloToggle_.setBounds (bottomRow);
        }

        area.removeFromBottom (6);

        // Compact Pan slider row
        {
            auto panRow = area.removeFromBottom (22);
            panTitle_.setBounds (panRow.removeFromLeft (30));
            panValue_.setBounds (panRow.removeFromRight (36));
            panSlider_.setBounds (panRow.reduced (2, 0));
        }

        area.removeFromBottom (6);

        // 2x3 Knob Grid
        const int colGap = 6;
        const int rowGap = 4;
        const int cellW = juce::jmax (40, (area.getWidth() - colGap * 2) / 3);
        const int cellH = juce::jmax (48, (area.getHeight() - rowGap) / 2);

        auto row1 = area.removeFromTop (cellH);
        area.removeFromTop (rowGap);
        auto row2 = area.removeFromTop (cellH);

        pitchKnob_->setBounds (row1.removeFromLeft (cellW));
        row1.removeFromLeft (colGap);
        grainKnob_->setBounds (row1.removeFromLeft (cellW));
        row1.removeFromLeft (colGap);
        driftKnob_->setBounds (row1);

        tiltKnob_->setBounds (row2.removeFromLeft (cellW));
        row2.removeFromLeft (colGap);
        gainKnob_->setBounds (row2.removeFromLeft (cellW));
        row2.removeFromLeft (colGap);
        widthKnob_->setBounds (row2);
    }

private:
    void updateSourceButtons (StreamSource s)
    {
        currentSource_ = s;
        const int activeIdx = static_cast<int> (s);
        for (int i = 0; i < 3; ++i)
            sourceButtons_[static_cast<size_t> (i)].setToggleState (i == activeIdx, juce::dontSendNotification);

        const auto accent = colourForSource (s);
        pitchKnob_->setAccentColour (accent);
        grainKnob_->setAccentColour (accent);
        driftKnob_->setAccentColour (accent);
        tiltKnob_->setAccentColour  (accent);
        gainKnob_->setAccentColour  (accent);
        widthKnob_->setAccentColour (accent);
        panSlider_.setColour (juce::Slider::thumbColourId, accent);
        panSlider_.setColour (juce::Slider::trackColourId, accent);
        repaint();
    }

    void updatePanReadout()
    {
        const double v = panSlider_.getValue();
        const int pct = juce::roundToInt (std::abs (v) * 100.0);
        if (pct < 2)
            panValue_.setText ("C", juce::dontSendNotification);
        else if (v < 0.0)
            panValue_.setText ("L" + juce::String (pct), juce::dontSendNotification);
        else
            panValue_.setText ("R" + juce::String (pct), juce::dontSendNotification);
    }

    juce::AudioProcessorValueTreeState& apvts_;
    const FossilTelemetry& telemetry_;

    int selectedLayer_ = 0;
    StreamSource currentSource_ = StreamSource::Tonal;
    bool cachedFrozen_ = false;

    juce::Rectangle<int> headerBounds_;

    std::array<juce::TextButton, 3> sourceButtons_;
    juce::TextButton captureButton_;
    juce::TextButton clearButton_;

    std::unique_ptr<KnobControl> pitchKnob_;
    std::unique_ptr<KnobControl> grainKnob_;
    std::unique_ptr<KnobControl> driftKnob_;
    std::unique_ptr<KnobControl> tiltKnob_;
    std::unique_ptr<KnobControl> gainKnob_;
    std::unique_ptr<KnobControl> widthKnob_;

    juce::Label  panTitle_;
    juce::Slider panSlider_;
    juce::Label  panValue_;

    juce::ToggleButton muteToggle_;
    juce::ToggleButton soloToggle_;
    juce::ToggleButton gateToggle_;

    std::unique_ptr<juce::ParameterAttachment> sourceAttach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> panAttach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAttach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloAttach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> gateAttach_;
};

} // namespace fossil::ui
