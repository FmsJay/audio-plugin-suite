#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../FossilParams.h"
#include "FossilPalette.h"

#include <cmath>
#include <memory>
#include <vector>

namespace fossil::ui
{

// ============================================================================
// EuclideanRingView — Interactive circular Euclidean rhythm ring + APVTS
// controls (Steps, Hits, Rot, Smooth, Depth, Rate, Enable)
// ============================================================================
class EuclideanRingView : public juce::Component
{
public:
    EuclideanRingView (juce::AudioProcessorValueTreeState& apvts,
                       const FossilTelemetry& telemetry)
        : apvts_ (apvts), telemetry_ (telemetry)
    {
        // [EUCLID ON] pill toggle button
        enableButton_.setButtonText ("EUCLID ON");
        enableButton_.setClickingTogglesState (true);
        enableButton_.setColour (juce::TextButton::buttonOnColourId, accentH);
        enableButton_.setColour (juce::TextButton::textColourOnId, textPrimary);
        addAndMakeVisible (enableButton_);

        if (apvts_.getParameter (param_ids::euclidEnable) != nullptr)
            enableAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                apvts_, param_ids::euclidEnable, enableButton_);

        // 5 compact knobs
        stepsKnob_  = std::make_unique<KnobControl> (apvts_, param_ids::euclidSteps,  "STEPS",  accentH, "");
        hitsKnob_   = std::make_unique<KnobControl> (apvts_, param_ids::euclidHits,   "HITS",   accentT, "");
        rotKnob_    = std::make_unique<KnobControl> (apvts_, param_ids::euclidRot,    "ROT",    accentN, "");
        smoothKnob_ = std::make_unique<KnobControl> (apvts_, param_ids::euclidSmooth, "SMOOTH", accentH, "%");
        depthKnob_  = std::make_unique<KnobControl> (apvts_, param_ids::euclidDepth,  "DEPTH",  accentT, "%");

        auto intFmt = [] (double v) { return juce::String (juce::roundToInt (v)); };
        stepsKnob_->setValueFormatter (intFmt);
        hitsKnob_->setValueFormatter  (intFmt);
        rotKnob_->setValueFormatter   (intFmt);

        addAndMakeVisible (*stepsKnob_);
        addAndMakeVisible (*hitsKnob_);
        addAndMakeVisible (*rotKnob_);
        addAndMakeVisible (*smoothKnob_);
        addAndMakeVisible (*depthKnob_);

        // Rate ComboBox (1/4, 1/8, 1/16, 1/8T, 1/32)
        rateLabel_.setText ("RATE", juce::dontSendNotification);
        rateLabel_.setJustificationType (juce::Justification::centred);
        rateLabel_.setFont (makeFont (10.5f, true));
        rateLabel_.setColour (juce::Label::textColourId, textMuted);
        addAndMakeVisible (rateLabel_);

        rateBox_.addItem ("1/4",  1);
        rateBox_.addItem ("1/8",  2);
        rateBox_.addItem ("1/16", 3);
        rateBox_.addItem ("1/8T", 4);
        rateBox_.addItem ("1/32", 5);
        rateBox_.setSelectedId (3, juce::dontSendNotification);
        addAndMakeVisible (rateBox_);

        if (apvts_.getParameter (param_ids::euclidRate) != nullptr)
            rateAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                apvts_, param_ids::euclidRate, rateBox_);

        refreshFromTelemetry();
    }

    void refreshFromTelemetry()
    {
        currentStep_ = telemetry_.currentEuclidStep.load (std::memory_order_relaxed);
        currentEnv_  = juce::jlimit (0.0f, 1.0f, telemetry_.currentEuclidEnv.load (std::memory_order_relaxed));
        patternMask_ = telemetry_.euclidPatternMask.load (std::memory_order_relaxed);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        const bool enabled = enableButton_.getToggleState();

        const int steps = juce::jlimit (1, kMaxEuclidSteps, juce::roundToInt (readParam (param_ids::euclidSteps, 16.0f)));
        const int hits  = juce::jlimit (0, steps, juce::roundToInt (readParam (param_ids::euclidHits, 5.0f)));

        // Outer panel card
        g.setColour (bgPanel);
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (enabled ? accentH.withAlpha (0.55f) : border);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

        // Header text & live readout ("Hits: 5 / 16")
        if (! headerBounds_.isEmpty())
        {
            g.setFont (makeFont (11.5f, true));
            g.setColour (textPrimary);
            g.drawText ("EUCLIDEAN GATE", headerBounds_, juce::Justification::centredLeft, false);

            const juce::String readout = "Hits: " + juce::String (hits) + " / " + juce::String (steps);
            auto readoutRect = headerBounds_.withTrimmedRight (enableButton_.getWidth() + 10);
            g.setFont (makeFont (10.5f, true));
            g.setColour (enabled ? accentH : textMuted);
            g.drawText (readout, readoutRect, juce::Justification::centredRight, false);
        }

        // Interactive circular Euclidean ring
        drawEuclideanRing (g, ringBounds_.toFloat(), steps, hits, enabled);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10, 8);

        // Header row
        headerBounds_ = area.removeFromTop (22);
        enableButton_.setBounds (headerBounds_.withWidth (82).withRightX (headerBounds_.getRight()));
        area.removeFromTop (6);

        // Split left (circular ring) and right (knobs + rate)
        const int ringW = juce::jlimit (96, juce::jmin (area.getHeight() + 8, area.getWidth() * 44 / 100),
                                        area.getWidth() * 40 / 100);
        ringBounds_ = area.removeFromLeft (ringW);
        area.removeFromLeft (8);

        // Right-side 2x3 control grid
        const int colGap = 5;
        const int rowGap = 4;
        const int cellW = juce::jmax (38, (area.getWidth() - colGap * 2) / 3);
        const int cellH = juce::jmax (46, (area.getHeight() - rowGap) / 2);

        auto topRow = area.removeFromTop (cellH);
        area.removeFromTop (rowGap);
        auto botRow = area.removeFromTop (cellH);

        stepsKnob_->setBounds (topRow.removeFromLeft (cellW));
        topRow.removeFromLeft (colGap);
        hitsKnob_->setBounds (topRow.removeFromLeft (cellW));
        topRow.removeFromLeft (colGap);
        rotKnob_->setBounds (topRow);

        smoothKnob_->setBounds (botRow.removeFromLeft (cellW));
        botRow.removeFromLeft (colGap);
        depthKnob_->setBounds (botRow.removeFromLeft (cellW));
        botRow.removeFromLeft (colGap);

        // Rate ComboBox occupies bottom-right cell
        auto rateCell = botRow;
        const int labelH = juce::jlimit (12, 16, rateCell.getHeight() / 5);
        rateLabel_.setBounds (rateCell.removeFromTop (labelH));
        rateBox_.setBounds (rateCell.withSizeKeepingCentre (
            juce::jmax (44, rateCell.getWidth() - 2),
            juce::jlimit (22, 26, rateCell.getHeight() - 8)));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (isPointInRing (e.getPosition().toFloat()))
            enableButton_.triggerClick();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const bool nowHovered = isPointInRing (e.getPosition().toFloat());
        if (nowHovered != ringHovered_)
        {
            ringHovered_ = nowHovered;
            setMouseCursor (ringHovered_ ? juce::MouseCursor::PointingHandCursor
                                         : juce::MouseCursor::NormalCursor);
            repaint (ringBounds_);
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (ringHovered_)
        {
            ringHovered_ = false;
            setMouseCursor (juce::MouseCursor::NormalCursor);
            repaint (ringBounds_);
        }
    }

private:
    float readParam (const char* paramId, float fallback) const noexcept
    {
        if (auto* raw = apvts_.getRawParameterValue (paramId))
            return raw->load (std::memory_order_relaxed);
        return fallback;
    }

    bool isPointInRing (juce::Point<float> pt) const noexcept
    {
        if (ringBounds_.isEmpty())
            return false;
        const auto rb = ringBounds_.toFloat();
        const float r = juce::jmin (rb.getWidth(), rb.getHeight()) * 0.5f;
        return pt.getDistanceFrom (rb.getCentre()) <= r;
    }

    void drawEuclideanRing (juce::Graphics& g,
                            juce::Rectangle<float> area,
                            int steps,
                            int hits,
                            bool enabled) const
    {
        const float diameter = juce::jmin (area.getWidth(), area.getHeight()) - 8.0f;
        if (diameter <= 24.0f)
            return;

        const auto centre = area.getCentre();
        const float outerR = diameter * 0.5f;
        const float ringR  = outerR - 8.0f;

        // Subtle circular disc backdrop
        g.setColour (bgDeep.withAlpha (0.85f));
        g.fillEllipse (centre.x - outerR, centre.y - outerR, outerR * 2.0f, outerR * 2.0f);

        g.setColour (ringHovered_ ? borderBright : border.withAlpha (0.75f));
        g.drawEllipse (centre.x - outerR, centre.y - outerR, outerR * 2.0f, outerR * 2.0f, 1.0f);

        // Main track circle
        g.setColour (border.withAlpha (0.75f));
        g.drawEllipse (centre.x - ringR, centre.y - ringR, ringR * 2.0f, ringR * 2.0f, 1.25f);

        // Envelope glow at centre when enabled
        if (enabled && currentEnv_ > 0.01f)
        {
            const float glowR = ringR * (0.28f + 0.30f * currentEnv_);
            juce::ColourGradient envGrad (
                accentH.withAlpha (0.24f * currentEnv_), centre.x, centre.y,
                accentH.withAlpha (0.0f), centre.x + glowR, centre.y, true);
            g.setGradientFill (envGrad);
            g.fillEllipse (centre.x - glowR, centre.y - glowR, glowR * 2.0f, glowR * 2.0f);
        }

        // Compute step positions starting at 12 o'clock (-pi/2) clockwise
        std::vector<juce::Point<float>> activePts;
        activePts.reserve (static_cast<size_t> (steps));

        const float twoPi = juce::MathConstants<float>::twoPi;
        const float startAngle = -juce::MathConstants<float>::halfPi;

        for (int i = 0; i < steps; ++i)
        {
            const float angle = startAngle + twoPi * (static_cast<float> (i) / static_cast<float> (steps));
            const juce::Point<float> pt (centre.x + std::cos (angle) * ringR,
                                         centre.y + std::sin (angle) * ringR);
            const bool isHit = ((patternMask_ >> static_cast<uint32_t> (i)) & 1u) != 0u;
            if (isHit)
                activePts.push_back (pt);
        }

        // Geometric polygon connecting active hits
        if (activePts.size() >= 2)
        {
            juce::Path poly;
            poly.startNewSubPath (activePts[0]);
            for (size_t k = 1; k < activePts.size(); ++k)
                poly.lineTo (activePts[k]);
            if (activePts.size() >= 3)
                poly.closeSubPath();

            const auto polyCol = enabled ? accentH : textMuted;
            if (activePts.size() >= 3)
            {
                g.setColour (polyCol.withAlpha (enabled ? (0.10f + 0.12f * currentEnv_) : 0.05f));
                g.fillPath (poly);
            }

            if (enabled)
            {
                g.setColour (polyCol.withAlpha (0.22f));
                g.strokePath (poly, juce::PathStrokeType (4.0f, juce::PathStrokeType::mitered,
                                                          juce::PathStrokeType::rounded));
            }
            g.setColour (polyCol.withAlpha (enabled ? 0.85f : 0.35f));
            g.strokePath (poly, juce::PathStrokeType (1.5f, juce::PathStrokeType::mitered,
                                                      juce::PathStrokeType::rounded));
        }

        // Draw step nodes
        const int liveStep = (steps > 0) ? (currentStep_ % steps) : 0;
        for (int i = 0; i < steps; ++i)
        {
            const float angle = startAngle + twoPi * (static_cast<float> (i) / static_cast<float> (steps));
            const juce::Point<float> pt (centre.x + std::cos (angle) * ringR,
                                         centre.y + std::sin (angle) * ringR);
            const bool isHit  = ((patternMask_ >> static_cast<uint32_t> (i)) & 1u) != 0u;
            const bool isLive = (i == liveStep);

            // Pulsing halo for live step
            if (isLive && enabled)
            {
                const float haloR = 7.0f + 4.0f * currentEnv_;
                const auto haloCol = isHit ? accentT : accentH;
                g.setColour (haloCol.withAlpha (0.25f + 0.30f * currentEnv_));
                g.fillEllipse (pt.x - haloR, pt.y - haloR, haloR * 2.0f, haloR * 2.0f);
            }

            if (isHit)
            {
                const float nodeR = isLive ? 4.8f : 4.0f;
                const auto fillCol = enabled ? ((i == 0 || isLive) ? accentT : accentH) : borderBright;

                if (enabled)
                {
                    g.setColour (fillCol.withAlpha (0.30f));
                    g.fillEllipse (pt.x - nodeR - 2.0f, pt.y - nodeR - 2.0f,
                                   (nodeR + 2.0f) * 2.0f, (nodeR + 2.0f) * 2.0f);
                }

                g.setColour (fillCol);
                g.fillEllipse (pt.x - nodeR, pt.y - nodeR, nodeR * 2.0f, nodeR * 2.0f);

                if (isLive && enabled)
                {
                    g.setColour (textPrimary);
                    g.drawEllipse (pt.x - nodeR, pt.y - nodeR, nodeR * 2.0f, nodeR * 2.0f, 1.2f);
                }
            }
            else
            {
                const float nodeR = isLive ? 3.5f : 2.6f;
                g.setColour (bgDeep);
                g.fillEllipse (pt.x - nodeR, pt.y - nodeR, nodeR * 2.0f, nodeR * 2.0f);
                g.setColour (isLive && enabled ? textPrimary : borderBright.withAlpha (0.7f));
                g.drawEllipse (pt.x - nodeR, pt.y - nodeR, nodeR * 2.0f, nodeR * 2.0f, isLive ? 1.4f : 1.0f);
            }
        }

        // Center badge text
        auto centerBox = juce::Rectangle<float> (centre.x - 28.0f, centre.y - 16.0f, 56.0f, 32.0f);
        g.setFont (makeFont (11.0f, true));
        g.setColour (enabled ? textPrimary : textMuted);
        g.drawText (juce::String (hits) + "/" + juce::String (steps),
                    centerBox.removeFromTop (17.0f).toNearestInt(), juce::Justification::centred, false);

        g.setFont (makeFont (9.0f, true));
        g.setColour (enabled ? accentH : textMuted.withAlpha (0.65f));
        g.drawText (enabled ? "ACTIVE" : "BYPASS",
                    centerBox.toNearestInt(), juce::Justification::centred, false);
    }

    juce::AudioProcessorValueTreeState& apvts_;
    const FossilTelemetry& telemetry_;

    int currentStep_ = 0;
    float currentEnv_ = 1.0f;
    uint32_t patternMask_ = 0x1111u;
    bool ringHovered_ = false;

    juce::Rectangle<int> headerBounds_;
    juce::Rectangle<int> ringBounds_;

    juce::TextButton enableButton_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableAttach_;

    std::unique_ptr<KnobControl> stepsKnob_;
    std::unique_ptr<KnobControl> hitsKnob_;
    std::unique_ptr<KnobControl> rotKnob_;
    std::unique_ptr<KnobControl> smoothKnob_;
    std::unique_ptr<KnobControl> depthKnob_;

    juce::Label rateLabel_;
    juce::ComboBox rateBox_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> rateAttach_;
};

} // namespace fossil::ui
