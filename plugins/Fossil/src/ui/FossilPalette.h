#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../FossilParams.h"

#include <cmath>
#include <functional>
#include <memory>

namespace fossil::ui
{

// High-contrast dark geological studio palette
namespace palette
{
    inline const juce::Colour bgDeep         { 0xff0b0e14 };
    inline const juce::Colour bgPanel        { 0xff121722 };
    inline const juce::Colour bgCard         { 0xff181f2e };
    inline const juce::Colour bgCardSelected { 0xff1e293b };
    inline const juce::Colour border         { 0xff2a3548 };
    inline const juce::Colour borderBright   { 0xff3d4f6b };
    inline const juce::Colour textPrimary    { 0xffeef2f6 };
    inline const juce::Colour textMuted      { 0xff8b9bb4 };

    // Stream identity accents
    inline const juce::Colour accentT        { 0xfff59e0b }; // Transient: warm amber
    inline const juce::Colour accentH        { 0xff22d3ee }; // Tonal: electric cyan
    inline const juce::Colour accentN        { 0xffa78bfa }; // Air/Noise: violet

    // Functional accents
    inline const juce::Colour accentEmerald  { 0xff10b981 }; // Freeze / Active
    inline const juce::Colour accentCoral    { 0xfff43f5e }; // Danger / Clear
} // namespace palette

// Direct aliases in fossil::ui for concise access
using palette::bgDeep;
using palette::bgPanel;
using palette::bgCard;
using palette::bgCardSelected;
using palette::border;
using palette::borderBright;
using palette::textPrimary;
using palette::textMuted;
using palette::accentT;
using palette::accentH;
using palette::accentN;
using palette::accentEmerald;
using palette::accentCoral;

inline juce::Colour colourForSource (StreamSource s) noexcept
{
    switch (s)
    {
        case StreamSource::Transient: return accentT;
        case StreamSource::Tonal:     return accentH;
        case StreamSource::Noise:     return accentN;
    }
    return accentH;
}

inline juce::Font makeFont (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

// ============================================================================
// FossilLookAndFeel — Crisp dark geological studio vector styling (JUCE 8)
// ============================================================================
class FossilLookAndFeel : public juce::LookAndFeel_V4
{
public:
    FossilLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, bgDeep);
        setColour (juce::Label::textColourId, textPrimary);
        setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);

        setColour (juce::Slider::rotarySliderFillColourId, accentH);
        setColour (juce::Slider::rotarySliderOutlineColourId, border);
        setColour (juce::Slider::thumbColourId, accentH);
        setColour (juce::Slider::trackColourId, accentH);
        setColour (juce::Slider::backgroundColourId, bgDeep);
        setColour (juce::Slider::textBoxTextColourId, textPrimary);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

        setColour (juce::TextButton::buttonColourId, bgCard);
        setColour (juce::TextButton::buttonOnColourId, accentH);
        setColour (juce::TextButton::textColourOffId, textMuted);
        setColour (juce::TextButton::textColourOnId, textPrimary);

        setColour (juce::ToggleButton::textColourId, textPrimary);
        setColour (juce::ToggleButton::tickColourId, accentH);
        setColour (juce::ToggleButton::tickDisabledColourId, borderBright);

        setColour (juce::ComboBox::backgroundColourId, bgCard);
        setColour (juce::ComboBox::textColourId, textPrimary);
        setColour (juce::ComboBox::outlineColourId, border);
        setColour (juce::ComboBox::arrowColourId, textMuted);

        setColour (juce::PopupMenu::backgroundColourId, bgPanel);
        setColour (juce::PopupMenu::textColourId, textPrimary);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, bgCardSelected);
        setColour (juce::PopupMenu::highlightedTextColourId, accentH);
    }

    void drawRotarySlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float sliderPosProportional,
                           float rotaryStartAngle,
                           float rotaryEndAngle,
                           juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<float> (
            static_cast<float> (x), static_cast<float> (y),
            static_cast<float> (width), static_cast<float> (height)).reduced (3.0f);

        const float diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
        if (diameter <= 6.0f)
            return;

        const float radius = diameter * 0.5f;
        const auto centre = bounds.getCentre();
        const float arcRadius = radius - 2.5f;
        const float dialRadius = radius - 6.5f;

        const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
        const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);
        const bool enabled = slider.isEnabled();
        const auto activeAccent = enabled ? accent : borderBright;

        // Track background arc
        {
            juce::Path bgArc;
            bgArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                 rotaryStartAngle, rotaryEndAngle, true);
            g.setColour (border.withAlpha (enabled ? 0.9f : 0.4f));
            g.strokePath (bgArc, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        // Glowing value arc
        if (sliderPosProportional > 0.001f)
        {
            juce::Path valArc;
            valArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                  rotaryStartAngle, angle, true);

            // Soft glow halo
            g.setColour (activeAccent.withAlpha (enabled ? 0.24f : 0.08f));
            g.strokePath (valArc, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));

            // Crisp value arc
            g.setColour (activeAccent);
            g.strokePath (valArc, juce::PathStrokeType (2.8f, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
        }

        // Dark circular dial body with subtle inner bevel
        if (dialRadius > 3.0f)
        {
            const auto dialRect = juce::Rectangle<float> (
                centre.x - dialRadius, centre.y - dialRadius,
                dialRadius * 2.0f, dialRadius * 2.0f);

            juce::ColourGradient bodyGrad (
                bgCardSelected.brighter (0.06f), centre.x, dialRect.getY(),
                bgDeep, centre.x, dialRect.getBottom(), false);
            g.setGradientFill (bodyGrad);
            g.fillEllipse (dialRect);

            // Outer bevel rim
            g.setColour (borderBright.withAlpha (enabled ? 0.65f : 0.25f));
            g.drawEllipse (dialRect, 1.0f);

            // Subtle inner highlight bevel
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.drawEllipse (dialRect.reduced (1.5f), 0.75f);

            // Indicator line
            const float indInner = dialRadius * 0.22f;
            const float indOuter = dialRadius * 0.82f;
            const float sinA = std::sin (angle);
            const float cosA = -std::cos (angle);

            const juce::Point<float> p1 (centre.x + sinA * indInner, centre.y + cosA * indInner);
            const juce::Point<float> p2 (centre.x + sinA * indOuter, centre.y + cosA * indOuter);

            g.setColour (activeAccent.withAlpha (0.35f));
            g.drawLine (juce::Line<float> (p1, p2), 3.4f);

            g.setColour (enabled ? textPrimary : textMuted);
            g.drawLine (juce::Line<float> (p1, p2), 1.8f);
        }
    }

    void drawLinearSlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float sliderPos,
                           float /*minSliderPos*/,
                           float /*maxSliderPos*/,
                           juce::Slider::SliderStyle style,
                           juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<float> (
            static_cast<float> (x), static_cast<float> (y),
            static_cast<float> (width), static_cast<float> (height));

        const auto accent = slider.findColour (juce::Slider::thumbColourId);
        const bool isVert = (style == juce::Slider::LinearVertical || style == juce::Slider::LinearBarVertical);
        const bool enabled = slider.isEnabled();
        const auto activeAccent = enabled ? accent : borderBright;

        if (style == juce::Slider::LinearBar)
        {
            const auto barBounds = bounds.reduced (0.5f);
            const float corner = 5.0f;
            const auto fillCol = slider.findColour (juce::Slider::trackColourId);
            const auto bgCol = slider.findColour (juce::Slider::backgroundColourId);

            g.setColour (bgCol);
            g.fillRoundedRectangle (barBounds, corner);

            const float fillW = juce::jlimit (0.0f, barBounds.getWidth(), sliderPos - barBounds.getX());
            if (fillW > 1.0f)
            {
                g.setColour (enabled ? fillCol : borderBright.withAlpha (0.4f));
                g.fillRoundedRectangle (barBounds.withWidth (fillW), corner);
            }

            g.setColour (slider.findColour (juce::Slider::textBoxOutlineColourId));
            g.drawRoundedRectangle (barBounds, corner, 1.0f);
            return;
        }

        if (isVert)
        {
            const float trackW = juce::jlimit (4.0f, 8.0f, bounds.getWidth() * 0.32f);
            const auto trackRect = juce::Rectangle<float> (
                bounds.getCentreX() - trackW * 0.5f,
                bounds.getY() + 4.0f,
                trackW,
                juce::jmax (4.0f, bounds.getHeight() - 8.0f));

            g.setColour (bgDeep);
            g.fillRoundedRectangle (trackRect, trackW * 0.5f);
            g.setColour (border);
            g.drawRoundedRectangle (trackRect, trackW * 0.5f, 1.0f);

            const float clampedPos = juce::jlimit (trackRect.getY(), trackRect.getBottom(), sliderPos);
            const auto fillRect = juce::Rectangle<float> (
                trackRect.getX(), clampedPos, trackRect.getWidth(), trackRect.getBottom() - clampedPos);
            g.setColour (activeAccent.withAlpha (0.75f));
            g.fillRoundedRectangle (fillRect, trackW * 0.5f);

            // Thumb pill
            const float thumbW = juce::jmin (bounds.getWidth() - 2.0f, 16.0f);
            const float thumbH = 8.0f;
            const auto thumbRect = juce::Rectangle<float> (
                bounds.getCentreX() - thumbW * 0.5f, clampedPos - thumbH * 0.5f, thumbW, thumbH);

            g.setColour (activeAccent.withAlpha (0.28f));
            g.fillRoundedRectangle (thumbRect.expanded (2.0f), 5.0f);
            g.setColour (textPrimary);
            g.fillRoundedRectangle (thumbRect, 4.0f);
            g.setColour (activeAccent);
            g.drawRoundedRectangle (thumbRect, 4.0f, 1.2f);
        }
        else
        {
            const float trackH = juce::jlimit (4.0f, 6.0f, bounds.getHeight() * 0.30f);
            const auto trackRect = juce::Rectangle<float> (
                bounds.getX() + 5.0f,
                bounds.getCentreY() - trackH * 0.5f,
                juce::jmax (4.0f, bounds.getWidth() - 10.0f),
                trackH);

            g.setColour (bgDeep);
            g.fillRoundedRectangle (trackRect, trackH * 0.5f);
            g.setColour (border);
            g.drawRoundedRectangle (trackRect, trackH * 0.5f, 1.0f);

            const float clampedPos = juce::jlimit (trackRect.getX(), trackRect.getRight(), sliderPos);

            // Bipolar fill if range spans negative to positive
            const bool bipolar = (slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0);
            if (bipolar)
            {
                const float zeroX = trackRect.getCentreX();
                const float x0 = juce::jmin (zeroX, clampedPos);
                const float x1 = juce::jmax (zeroX, clampedPos);
                g.setColour (activeAccent.withAlpha (0.8f));
                g.fillRoundedRectangle (x0, trackRect.getY(), x1 - x0, trackRect.getHeight(), trackH * 0.5f);
            }
            else
            {
                g.setColour (activeAccent.withAlpha (0.8f));
                g.fillRoundedRectangle (trackRect.getX(), trackRect.getY(),
                                        clampedPos - trackRect.getX(), trackRect.getHeight(), trackH * 0.5f);
            }

            const float thumbR = 5.5f;
            const auto thumbRect = juce::Rectangle<float> (
                clampedPos - thumbR, bounds.getCentreY() - thumbR, thumbR * 2.0f, thumbR * 2.0f);
            g.setColour (activeAccent.withAlpha (0.30f));
            g.fillEllipse (thumbRect.expanded (2.5f));
            g.setColour (textPrimary);
            g.fillEllipse (thumbRect);
            g.setColour (activeAccent);
            g.drawEllipse (thumbRect, 1.3f);
        }
    }

    void drawButtonBackground (juce::Graphics& g,
                               juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced (0.75f);
        const float corner = juce::jmin (6.0f, bounds.getHeight() * 0.42f);
        const bool isOn = button.getToggleState();

        if (isOn)
        {
            const auto onCol = button.findColour (juce::TextButton::buttonOnColourId);
            // Subtle glow
            g.setColour (onCol.withAlpha (0.20f));
            g.fillRoundedRectangle (bounds.expanded (0.5f), corner + 0.5f);

            g.setColour (onCol.withAlpha (shouldDrawButtonAsDown ? 0.42f : 0.28f));
            g.fillRoundedRectangle (bounds, corner);

            g.setColour (onCol.brighter (shouldDrawButtonAsHighlighted ? 0.15f : 0.0f));
            g.drawRoundedRectangle (bounds, corner, 1.3f);
        }
        else
        {
            auto base = backgroundColour;
            if (shouldDrawButtonAsDown)
                base = base.brighter (0.14f);
            else if (shouldDrawButtonAsHighlighted)
                base = base.brighter (0.07f);

            g.setColour (base);
            g.fillRoundedRectangle (bounds, corner);

            g.setColour (shouldDrawButtonAsHighlighted ? borderBright : border);
            g.drawRoundedRectangle (bounds, corner, 1.0f);
        }
    }

    void drawButtonText (juce::Graphics& g,
                         juce::TextButton& button,
                         bool shouldDrawButtonAsHighlighted,
                         bool /*shouldDrawButtonAsDown*/) override
    {
        const float fontH = juce::jlimit (9.5f, 12.5f, button.getHeight() * 0.52f);
        g.setFont (makeFont (fontH, button.getToggleState()));

        const auto colour = button.getToggleState()
            ? button.findColour (juce::TextButton::textColourOnId)
            : (shouldDrawButtonAsHighlighted ? textPrimary : button.findColour (juce::TextButton::textColourOffId));

        g.setColour (colour.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.45f));
        g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (4, 1),
                          juce::Justification::centred, 1);
    }

    void drawToggleButton (juce::Graphics& g,
                           juce::ToggleButton& button,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced (0.75f);
        const float corner = juce::jmin (6.0f, bounds.getHeight() * 0.42f);
        const bool isOn = button.getToggleState();
        const auto accent = button.findColour (juce::ToggleButton::tickColourId);

        if (isOn)
        {
            g.setColour (accent.withAlpha (shouldDrawButtonAsDown ? 0.34f : 0.22f));
            g.fillRoundedRectangle (bounds, corner);
            g.setColour (accent.withAlpha (0.95f));
            g.drawRoundedRectangle (bounds, corner, 1.25f);
        }
        else
        {
            auto fill = bgCard;
            if (shouldDrawButtonAsDown) fill = fill.brighter (0.12f);
            else if (shouldDrawButtonAsHighlighted) fill = fill.brighter (0.06f);
            g.setColour (fill);
            g.fillRoundedRectangle (bounds, corner);
            g.setColour (shouldDrawButtonAsHighlighted ? borderBright : border);
            g.drawRoundedRectangle (bounds, corner, 1.0f);
        }

        // Small status LED on left
        const float ledR = 3.2f;
        const float ledX = bounds.getX() + 9.0f;
        const float ledY = bounds.getCentreY();
        if (isOn)
        {
            g.setColour (accent.withAlpha (0.35f));
            g.fillEllipse (ledX - ledR - 2.0f, ledY - ledR - 2.0f, (ledR + 2.0f) * 2.0f, (ledR + 2.0f) * 2.0f);
            g.setColour (accent);
        }
        else
        {
            g.setColour (borderBright.withAlpha (0.7f));
        }
        g.fillEllipse (ledX - ledR, ledY - ledR, ledR * 2.0f, ledR * 2.0f);

        // Label text
        const float fontH = juce::jlimit (9.5f, 12.0f, bounds.getHeight() * 0.50f);
        g.setFont (makeFont (fontH, isOn));
        g.setColour ((isOn ? textPrimary : textMuted).withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.45f));

        auto textRect = bounds.withTrimmedLeft (16.0f).withTrimmedRight (4.0f);
        g.drawFittedText (button.getButtonText(), textRect.toNearestInt(),
                          juce::Justification::centredLeft, 1);
    }

    void drawComboBox (juce::Graphics& g,
                       int width, int height,
                       bool isButtonDown,
                       int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/,
                       juce::ComboBox& box) override
    {
        const auto bounds = juce::Rectangle<float> (
            0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height)).reduced (0.75f);
        const float corner = 5.0f;

        g.setColour (isButtonDown ? bgCardSelected : box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (bounds, corner);

        g.setColour (box.hasKeyboardFocus (true) ? accentH : box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (bounds, corner, 1.0f);

        // Sleek chevron
        const float arrowX = bounds.getRight() - 13.0f;
        const float arrowY = bounds.getCentreY() - 2.0f;
        juce::Path chevron;
        chevron.startNewSubPath (arrowX - 3.5f, arrowY);
        chevron.lineTo (arrowX, arrowY + 4.0f);
        chevron.lineTo (arrowX + 3.5f, arrowY);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override
    {
        return makeFont (11.5f, true);
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (6, 1, juce::jmax (1, box.getWidth() - 22), juce::jmax (1, box.getHeight() - 2));
        label.setFont (getComboBoxFont (box));
    }
};

// ============================================================================
// KnobControl — Compact labeled rotary knob with reattachable APVTS binding
// ============================================================================
class KnobControl : public juce::Component
{
public:
    KnobControl (juce::AudioProcessorValueTreeState& apvts,
                 const juce::String& paramId,
                 const juce::String& labelText,
                 juce::Colour accent,
                 const juce::String& suffix = "")
        : suffix_ (suffix.trim())
    {
        titleLabel_.setText (labelText, juce::dontSendNotification);
        titleLabel_.setJustificationType (juce::Justification::centred);
        titleLabel_.setFont (makeFont (10.5f, true));
        titleLabel_.setColour (juce::Label::textColourId, textMuted);
        addAndMakeVisible (titleLabel_);

        slider_.setSliderStyle (juce::Slider::SliderStyle::RotaryHorizontalVerticalDrag);
        slider_.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider_.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                     juce::MathConstants<float>::pi * 2.75f,
                                     true);
        slider_.onValueChange = [this]() { updateValueReadout(); };
        addAndMakeVisible (slider_);

        valueLabel_.setJustificationType (juce::Justification::centred);
        valueLabel_.setFont (makeFont (10.5f, false));
        valueLabel_.setColour (juce::Label::textColourId, textPrimary);
        addAndMakeVisible (valueLabel_);

        setAccentColour (accent);
        reattach (apvts, paramId);
    }

    void reattach (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId)
    {
        // Always release old attachment before creating a new one
        attachment_.reset();
        if (apvts.getParameter (paramId) != nullptr)
            attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts, paramId, slider_);
        updateValueReadout();
    }

    void setAccentColour (juce::Colour accent)
    {
        slider_.setColour (juce::Slider::rotarySliderFillColourId, accent);
        slider_.setColour (juce::Slider::thumbColourId, accent);
        repaint();
    }

    void setLabelText (const juce::String& text)
    {
        titleLabel_.setText (text, juce::dontSendNotification);
    }

    void setValueFormatter (std::function<juce::String (double)> formatter)
    {
        customFormatter_ = std::move (formatter);
        updateValueReadout();
    }

    juce::Slider& getSlider() noexcept { return slider_; }

    void resized() override
    {
        auto area = getLocalBounds();
        const int labelH = juce::jlimit (12, 16, area.getHeight() / 5);
        titleLabel_.setBounds (area.removeFromTop (labelH));
        valueLabel_.setBounds (area.removeFromBottom (labelH));
        slider_.setBounds (area);
    }

private:
    void updateValueReadout()
    {
        const double val = slider_.getValue();
        if (customFormatter_)
        {
            valueLabel_.setText (customFormatter_ (val), juce::dontSendNotification);
            return;
        }

        juce::String text;
        if (suffix_ == "%")
        {
            // Handle both normalized (0..1 / 0..2) and 0..100 parameter ranges cleanly
            const double scaled = (slider_.getMaximum() <= 2.01) ? (val * 100.0) : val;
            text = juce::String (juce::roundToInt (scaled)) + "%";
        }
        else if (suffix_ == "st")
        {
            const bool isNearInt = std::abs (val - std::round (val)) < 0.05;
            const juce::String sign = (val > 0.04) ? "+" : "";
            text = sign + (isNearInt ? juce::String (juce::roundToInt (val)) : juce::String (val, 1)) + " st";
        }
        else if (suffix_ == "ms")
        {
            text = juce::String (juce::roundToInt (val)) + " ms";
        }
        else if (suffix_ == "dB")
        {
            const juce::String sign = (val > 0.05) ? "+" : "";
            text = sign + juce::String (val, 1) + " dB";
        }
        else if (suffix_.isNotEmpty())
        {
            const bool isNearInt = std::abs (val - std::round (val)) < 0.05;
            text = (isNearInt ? juce::String (juce::roundToInt (val)) : juce::String (val, 1)) + " " + suffix_;
        }
        else
        {
            const bool isNearInt = std::abs (val - std::round (val)) < 0.05;
            text = isNearInt ? juce::String (juce::roundToInt (val)) : juce::String (val, 2);
        }

        valueLabel_.setText (text, juce::dontSendNotification);
    }

    juce::Label titleLabel_;
    juce::Slider slider_;
    juce::Label valueLabel_;
    juce::String suffix_;
    std::function<juce::String (double)> customFormatter_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment_;
};

} // namespace fossil::ui
