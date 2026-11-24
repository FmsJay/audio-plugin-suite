#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <cmath>

namespace sessionmate::ui {

struct Palette {
    static inline const juce::Colour bgDark          { 0xFF0F1218 };
    static inline const juce::Colour bgPanel         { 0xFF171C26 };
    static inline const juce::Colour bgCard          { 0xFF1E2533 };
    static inline const juce::Colour bgCardSelected  { 0xFF232C3E };
    static inline const juce::Colour bgControl       { 0xFF131822 };
    static inline const juce::Colour border          { 0xFF2C3548 };
    static inline const juce::Colour borderLight     { 0xFF3E4B66 };
    static inline const juce::Colour textPrimary     { 0xFFF1F5F9 };
    static inline const juce::Colour textMuted       { 0xFF94A3B8 };
    static inline const juce::Colour accentCyan      { 0xFF38BDF8 };
    static inline const juce::Colour accentAmber     { 0xFFF59E0B };
    static inline const juce::Colour accentEmerald   { 0xFF10B981 };
    static inline const juce::Colour accentCoral     { 0xFFF43F5E };
    static inline const juce::Colour accentViolet    { 0xFFA78BFA };
    static inline const juce::Colour accentBrass     { 0xFFFB923C };

    static juce::Colour slotAccent(int slotIndex) noexcept {
        switch (slotIndex) {
            case 0: return accentCoral;
            case 1: return accentAmber;
            case 2: return accentCyan;
            case 3: return accentEmerald;
            case 4: return accentViolet;
            case 5: return accentBrass;
            default: return accentCyan;
        }
    }

    static juce::Font font(float height, bool bold = false) {
        return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
    }
};

class SessionMateLookAndFeel : public juce::LookAndFeel_V4 {
public:
    SessionMateLookAndFeel() {
        setColour(juce::ResizableWindow::backgroundColourId, Palette::bgDark);

        setColour(juce::Label::textColourId, Palette::textPrimary);
        setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);

        setColour(juce::TextButton::buttonColourId, Palette::bgCard);
        setColour(juce::TextButton::buttonOnColourId, Palette::accentCyan);
        setColour(juce::TextButton::textColourOffId, Palette::textPrimary);
        setColour(juce::TextButton::textColourOnId, Palette::bgDark);

        setColour(juce::ToggleButton::textColourId, Palette::textPrimary);
        setColour(juce::ToggleButton::tickColourId, Palette::accentCyan);
        setColour(juce::ToggleButton::tickDisabledColourId, Palette::textMuted);

        setColour(juce::ComboBox::backgroundColourId, Palette::bgControl);
        setColour(juce::ComboBox::textColourId, Palette::textPrimary);
        setColour(juce::ComboBox::outlineColourId, Palette::border);
        setColour(juce::ComboBox::arrowColourId, Palette::textMuted);

        setColour(juce::PopupMenu::backgroundColourId, Palette::bgPanel);
        setColour(juce::PopupMenu::textColourId, Palette::textPrimary);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, Palette::accentCyan.withAlpha(0.22f));
        setColour(juce::PopupMenu::highlightedTextColourId, Palette::textPrimary);
        setColour(juce::PopupMenu::headerTextColourId, Palette::textMuted);

        setColour(juce::Slider::backgroundColourId, Palette::bgControl);
        setColour(juce::Slider::trackColourId, Palette::accentCyan);
        setColour(juce::Slider::thumbColourId, Palette::textPrimary);
        setColour(juce::Slider::rotarySliderFillColourId, Palette::accentCyan);
        setColour(juce::Slider::rotarySliderOutlineColourId, Palette::bgControl);
        setColour(juce::Slider::textBoxTextColourId, Palette::textPrimary);
        setColour(juce::Slider::textBoxBackgroundColourId, Palette::bgControl);
        setColour(juce::Slider::textBoxOutlineColourId, Palette::border);

        setColour(juce::TextEditor::backgroundColourId, Palette::bgControl);
        setColour(juce::TextEditor::textColourId, Palette::textPrimary);
        setColour(juce::TextEditor::outlineColourId, Palette::border);
        setColour(juce::TextEditor::focusedOutlineColourId, Palette::accentCyan);
        setColour(juce::TextEditor::highlightColourId, Palette::accentCyan.withAlpha(0.30f));

        setColour(juce::TooltipWindow::backgroundColourId, Palette::bgPanel);
        setColour(juce::TooltipWindow::textColourId, Palette::textPrimary);
        setColour(juce::TooltipWindow::outlineColourId, Palette::borderLight);
    }

    void drawRotarySlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPosProportional,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override {
        const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                   static_cast<float>(width), static_cast<float>(height)).reduced(2.5f);
        const float radius = std::max(8.0f, std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f);
        const auto centre = bounds.getCentre();
        const float arcThickness = std::clamp(radius * 0.16f, 2.2f, 4.2f);
        const float arcRadius = radius - arcThickness * 0.6f;

        const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
        const auto accent = slider.findColour(juce::Slider::rotarySliderFillColourId);
        const bool enabled = slider.isEnabled();
        const auto activeAccent = enabled ? accent : Palette::textMuted.withAlpha(0.4f);

        // Dark recessed outer track
        juce::Path trackPath;
        trackPath.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(Palette::bgDark);
        g.strokePath(trackPath, juce::PathStrokeType(arcThickness + 1.5f,
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(Palette::border.withAlpha(0.75f));
        g.strokePath(trackPath, juce::PathStrokeType(arcThickness * 0.72f,
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Glowing value arc (supports bipolar knobs via "bipolar" property)
        const bool isBipolar = static_cast<bool>(slider.getProperties().getWithDefault("bipolar", false));
        const float startA = isBipolar ? (rotaryStartAngle + rotaryEndAngle) * 0.5f : rotaryStartAngle;
        const float fromA = std::min(startA, angle);
        const float toA   = std::max(startA, angle);

        if (std::abs(toA - fromA) > 0.005f) {
            juce::Path valueArc;
            valueArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, fromA, toA, true);

            // Soft outer glow
            g.setColour(activeAccent.withAlpha(0.24f));
            g.strokePath(valueArc, juce::PathStrokeType(arcThickness + 3.6f,
                         juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // Crisp bright arc
            g.setColour(activeAccent);
            g.strokePath(valueArc, juce::PathStrokeType(arcThickness,
                         juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Inner knob body
        const float bodyRadius = std::max(4.0f, arcRadius - arcThickness * 1.15f);
        const auto bodyBounds = juce::Rectangle<float>(centre.x - bodyRadius, centre.y - bodyRadius,
                                                       bodyRadius * 2.0f, bodyRadius * 2.0f);

        juce::ColourGradient bodyGrad(Palette::bgCardSelected.brighter(0.08f), centre.x, bodyBounds.getY(),
                                      Palette::bgDark, centre.x, bodyBounds.getBottom(), false);
        g.setGradientFill(bodyGrad);
        g.fillEllipse(bodyBounds);

        g.setColour(slider.isMouseOverOrDragging() ? activeAccent.withAlpha(0.65f) : Palette::borderLight.withAlpha(0.55f));
        g.drawEllipse(bodyBounds, 1.0f);

        // Pointer indicator line + glowing tip dot
        const float dotDist = bodyRadius * 0.62f;
        const float px = centre.x + std::sin(angle) * dotDist;
        const float py = centre.y - std::cos(angle) * dotDist;

        g.setColour(activeAccent.withAlpha(0.35f));
        g.fillEllipse(px - 3.2f, py - 3.2f, 6.4f, 6.4f);

        g.setColour(enabled ? Palette::textPrimary : Palette::textMuted);
        g.fillEllipse(px - 1.9f, py - 1.9f, 3.8f, 3.8f);
    }

    void drawLinearSlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPos,
                          float /*minSliderPos*/,
                          float /*maxSliderPos*/,
                          const juce::Slider::SliderStyle style,
                          juce::Slider& slider) override {
        if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar) {
            LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, 0.0f, 0.0f, style, slider);
            return;
        }

        const auto area = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                 static_cast<float>(width), static_cast<float>(height));
        const float trackH = std::clamp(area.getHeight() * 0.34f, 5.0f, 8.0f);
        const auto trackRect = juce::Rectangle<float>(area.getX() + 4.0f,
                                                      area.getCentreY() - trackH * 0.5f,
                                                      std::max(10.0f, area.getWidth() - 8.0f),
                                                      trackH);
        const float corner = trackH * 0.5f;

        // Recessed track
        g.setColour(Palette::bgDark);
        g.fillRoundedRectangle(trackRect, corner);
        g.setColour(Palette::border);
        g.drawRoundedRectangle(trackRect, corner, 1.0f);

        const auto accent = slider.findColour(juce::Slider::trackColourId);
        const bool isBipolar = static_cast<bool>(slider.getProperties().getWithDefault("bipolar", false));
        const bool isCrossfader = static_cast<bool>(slider.getProperties().getWithDefault("crossfader", false));

        const float clampedPos = std::clamp(sliderPos, trackRect.getX(), trackRect.getRight());

        if (isCrossfader) {
            // Subtle center tick + dual-color morph glow (Cyan A <-> Violet B)
            const float cx = trackRect.getCentreX();
            g.setColour(Palette::borderLight.withAlpha(0.7f));
            g.drawVerticalLine(juce::roundToInt(cx), trackRect.getY() - 3.0f, trackRect.getBottom() + 3.0f);

            juce::ColourGradient morphGrad(Palette::accentCyan.withAlpha(0.65f), trackRect.getX(), trackRect.getCentreY(),
                                           Palette::accentViolet.withAlpha(0.65f), trackRect.getRight(), trackRect.getCentreY(), false);
            g.setGradientFill(morphGrad);
            g.fillRoundedRectangle(trackRect.reduced(0.5f), corner);
        } else {
            const float fillStart = isBipolar ? trackRect.getCentreX() : trackRect.getX();
            const float fillLeft  = std::min(fillStart, clampedPos);
            const float fillRight = std::max(fillStart, clampedPos);
            if (fillRight - fillLeft > 1.0f) {
                const auto fillRect = juce::Rectangle<float>(fillLeft, trackRect.getY(), fillRight - fillLeft, trackRect.getHeight());
                g.setColour(accent.withAlpha(0.28f));
                g.fillRoundedRectangle(fillRect.expanded(0.0f, 1.8f), corner + 1.5f);
                g.setColour(accent);
                g.fillRoundedRectangle(fillRect, corner);
            }
            if (isBipolar) {
                g.setColour(Palette::borderLight);
                g.drawVerticalLine(juce::roundToInt(trackRect.getCentreX()), trackRect.getY() - 2.0f, trackRect.getBottom() + 2.0f);
            }
        }

        // Thumb pill
        const float thumbW = isCrossfader ? 14.0f : 11.0f;
        const float thumbH = std::clamp(area.getHeight() * 0.72f, 12.0f, 18.0f);
        const auto thumbRect = juce::Rectangle<float>(clampedPos - thumbW * 0.5f,
                                                      area.getCentreY() - thumbH * 0.5f,
                                                      thumbW, thumbH);
        const float thumbCorner = thumbW * 0.45f;

        if (slider.isMouseOverOrDragging()) {
            g.setColour(accent.withAlpha(0.28f));
            g.fillRoundedRectangle(thumbRect.expanded(2.5f), thumbCorner + 2.0f);
        }

        g.setColour(Palette::textPrimary);
        g.fillRoundedRectangle(thumbRect, thumbCorner);
        g.setColour(Palette::bgDark.withAlpha(0.85f));
        g.drawRoundedRectangle(thumbRect, thumbCorner, 1.0f);

        // Center grip line on thumb
        g.setColour(accent);
        g.drawVerticalLine(juce::roundToInt(thumbRect.getCentreX()),
                           thumbRect.getY() + 3.0f, thumbRect.getBottom() - 3.0f);
    }

    void drawButtonBackground(juce::Graphics& g,
                              juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override {
        const auto bounds = button.getLocalBounds().toFloat().reduced(0.75f);
        const bool isPill = static_cast<bool>(button.getProperties().getWithDefault("pill", true));
        const float corner = isPill ? std::min(6.0f, bounds.getHeight() * 0.42f) : 4.0f;

        const bool toggled = button.getToggleState();
        const bool enabled = button.isEnabled();

        juce::Colour fill = backgroundColour;
        if (toggled) {
            fill = button.findColour(juce::TextButton::buttonOnColourId);
        }
        if (shouldDrawButtonAsDown) {
            fill = fill.darker(0.18f);
        } else if (shouldDrawButtonAsHighlighted) {
            fill = toggled ? fill.brighter(0.10f) : fill.brighter(0.14f);
        }
        if (!enabled) {
            fill = fill.withAlpha(0.38f);
        }

        if (toggled && enabled) {
            g.setColour(fill.withAlpha(0.24f));
            g.fillRoundedRectangle(bounds.expanded(1.2f), corner + 1.0f);
        }

        g.setColour(fill);
        g.fillRoundedRectangle(bounds, corner);

        const auto borderCol = toggled
            ? fill.brighter(0.28f)
            : (shouldDrawButtonAsHighlighted ? Palette::borderLight : Palette::border);
        g.setColour(enabled ? borderCol : Palette::border.withAlpha(0.4f));
        g.drawRoundedRectangle(bounds, corner, 1.0f);
    }

    void drawToggleButton(juce::Graphics& g,
                          juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override {
        const auto bounds = button.getLocalBounds().toFloat().reduced(0.75f);
        const float corner = std::min(6.0f, bounds.getHeight() * 0.45f);
        const bool on = button.getToggleState();
        const auto accent = button.findColour(juce::ToggleButton::tickColourId);

        auto bg = on ? accent.withAlpha(0.18f) : Palette::bgControl;
        if (shouldDrawButtonAsDown) bg = bg.darker(0.15f);
        else if (shouldDrawButtonAsHighlighted) bg = bg.brighter(0.12f);

        g.setColour(bg);
        g.fillRoundedRectangle(bounds, corner);

        g.setColour(on ? accent.withAlpha(0.85f)
                       : (shouldDrawButtonAsHighlighted ? Palette::borderLight : Palette::border));
        g.drawRoundedRectangle(bounds, corner, 1.0f);

        // Status LED dot on left
        const float ledSize = 6.0f;
        const float ledX = bounds.getX() + 7.0f;
        const float ledY = bounds.getCentreY() - ledSize * 0.5f;
        if (on) {
            g.setColour(accent.withAlpha(0.35f));
            g.fillEllipse(ledX - 2.0f, ledY - 2.0f, ledSize + 4.0f, ledSize + 4.0f);
            g.setColour(accent);
        } else {
            g.setColour(Palette::borderLight);
        }
        g.fillEllipse(ledX, ledY, ledSize, ledSize);

        // Label text
        const auto textBounds = bounds.withTrimmedLeft(17.0f).withTrimmedRight(5.0f);
        g.setColour(on ? Palette::textPrimary : Palette::textMuted);
        g.setFont(Palette::font(11.5f, on));
        g.drawFittedText(button.getButtonText(), textBounds.toNearestInt(),
                         juce::Justification::centredLeft, 1);
    }

    void drawComboBox(juce::Graphics& g,
                      int width, int height,
                      bool isButtonDown,
                      int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/,
                      juce::ComboBox& box) override {
        const auto bounds = juce::Rectangle<float>(0.0f, 0.0f,
                                                   static_cast<float>(width),
                                                   static_cast<float>(height)).reduced(0.75f);
        const float corner = 5.0f;

        auto bg = box.findColour(juce::ComboBox::backgroundColourId);
        if (isButtonDown) bg = bg.brighter(0.08f);
        else if (box.isMouseOver()) bg = bg.brighter(0.05f);

        g.setColour(bg);
        g.fillRoundedRectangle(bounds, corner);

        g.setColour(box.isMouseOver() ? Palette::borderLight : box.findColour(juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle(bounds, corner, 1.0f);

        // Crisp vector chevron
        const float chevronW = 7.0f;
        const float chevronH = 4.0f;
        const float cx = bounds.getRight() - 11.0f;
        const float cy = bounds.getCentreY();

        juce::Path chevron;
        chevron.startNewSubPath(cx - chevronW * 0.5f, cy - chevronH * 0.4f);
        chevron.lineTo(cx, cy + chevronH * 0.6f);
        chevron.lineTo(cx + chevronW * 0.5f, cy - chevronH * 0.4f);

        g.setColour(box.isEnabled() ? Palette::textMuted : Palette::textMuted.withAlpha(0.35f));
        g.strokePath(chevron, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
        label.setBounds(6, 1, std::max(10, box.getWidth() - 24), box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override {
        return Palette::font(11.5f, false);
    }

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override {
        return Palette::font(std::clamp(static_cast<float>(buttonHeight) * 0.46f, 10.5f, 12.5f), true);
    }

    juce::Font getPopupMenuFont() override {
        return Palette::font(12.0f, false);
    }

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override {
        const auto bounds = juce::Rectangle<float>(0.0f, 0.0f,
                                                   static_cast<float>(width),
                                                   static_cast<float>(height));
        g.setColour(Palette::bgPanel);
        g.fillRoundedRectangle(bounds, 5.0f);
        g.setColour(Palette::borderLight);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 5.0f, 1.0f);
    }
};

} // namespace sessionmate::ui
