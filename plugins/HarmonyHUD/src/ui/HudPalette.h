#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

namespace harmonyhud::ui {

namespace palette {

inline const juce::Colour bgDark        { 0xFF0B0E14 };
inline const juce::Colour bgPanel       { 0xFF131823 };
inline const juce::Colour bgCard        { 0xFF1A2232 };
inline const juce::Colour border        { 0xFF283449 };
inline const juce::Colour textPrimary   { 0xFFF1F5F9 };
inline const juce::Colour textMuted     { 0xFF94A3B8 };
inline const juce::Colour accentCyan    { 0xFF38BDF8 };
inline const juce::Colour accentAmber   { 0xFFFBBF24 };
inline const juce::Colour accentViolet  { 0xFFA78BFA };
inline const juce::Colour accentEmerald { 0xFF34D399 };
inline const juce::Colour accentCoral   { 0xFFFB7185 };

// SessionMate Band X-Ray & Improvisation Overlay palette
inline const juce::Colour bandBass      { 0xFFF59E0B };
inline const juce::Colour bandKeys      { 0xFF38BDF8 };
inline const juce::Colour bandGuitar    { 0xFFA78BFA };
inline const juce::Colour bandHorns     { 0xFF34D399 };
inline const juce::Colour userInput     { 0xFFF8FAFC };
inline const juce::Colour overlayGuide  { 0xFF2DD4BF };
inline const juce::Colour overlayScale  { 0xFF60A5FA };

inline juce::Font makeFont(float size, bool bold = false) {
    return bold ? juce::Font(juce::FontOptions(size, juce::Font::bold))
                : juce::Font(juce::FontOptions(size));
}

inline juce::Font makeMonoFont(float size, bool bold = false) {
    auto opts = juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), size,
                                  bold ? juce::Font::bold : juce::Font::plain);
    return juce::Font(opts);
}

inline void drawPanelCard(juce::Graphics& g, juce::Rectangle<float> bounds,
                          float cornerRadius = 8.0f,
                          juce::Colour fillColour = bgPanel,
                          juce::Colour borderColour = border) {
    g.setColour(fillColour);
    g.fillRoundedRectangle(bounds, cornerRadius);
    g.setColour(borderColour);
    g.drawRoundedRectangle(bounds.reduced(0.5f), cornerRadius, 1.0f);
}

inline void drawStatusPill(juce::Graphics& g, juce::Rectangle<float> bounds,
                           const juce::String& text, juce::Colour accent,
                           float fontSize = 11.0f) {
    const float r = bounds.getHeight() * 0.5f;
    g.setColour(accent.withAlpha(0.16f));
    g.fillRoundedRectangle(bounds, r);
    g.setColour(accent.withAlpha(0.55f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), r, 1.0f);
    g.setColour(accent);
    g.setFont(makeFont(fontSize, true));
    g.drawText(text, bounds.reduced(6.0f, 0.0f), juce::Justification::centred, true);
}

} // namespace palette

class HarmonyLookAndFeel : public juce::LookAndFeel_V4 {
public:
    HarmonyLookAndFeel() {
        setColour(juce::ResizableWindow::backgroundColourId, palette::bgDark);
        setColour(juce::Label::textColourId, palette::textPrimary);
        setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);

        setColour(juce::TextButton::buttonColourId, palette::bgCard);
        setColour(juce::TextButton::buttonOnColourId, palette::accentCyan.withAlpha(0.25f));
        setColour(juce::TextButton::textColourOffId, palette::textPrimary);
        setColour(juce::TextButton::textColourOnId, palette::accentCyan);

        setColour(juce::ComboBox::backgroundColourId, palette::bgCard);
        setColour(juce::ComboBox::textColourId, palette::textPrimary);
        setColour(juce::ComboBox::outlineColourId, palette::border);
        setColour(juce::ComboBox::arrowColourId, palette::textMuted);

        setColour(juce::PopupMenu::backgroundColourId, palette::bgPanel);
        setColour(juce::PopupMenu::textColourId, palette::textPrimary);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, palette::accentCyan.withAlpha(0.22f));
        setColour(juce::PopupMenu::highlightedTextColourId, palette::textPrimary);

        setColour(juce::Slider::rotarySliderFillColourId, palette::accentCyan);
        setColour(juce::Slider::rotarySliderOutlineColourId, palette::border);
        setColour(juce::Slider::thumbColourId, palette::textPrimary);
        setColour(juce::Slider::textBoxTextColourId, palette::textPrimary);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

        setColour(juce::ScrollBar::thumbColourId, palette::border.brighter(0.25f));
        setColour(juce::ScrollBar::backgroundColourId, palette::bgDark);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& slider) override {
        const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                   static_cast<float>(width), static_cast<float>(height)).reduced(4.0f);
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float arcRadius = radius - 3.0f;
        const float lineW = juce::jlimit(2.0f, 3.8f, radius * 0.14f);
        const float toAngle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        juce::Path bgArc;
        bgArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                            rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(palette::border);
        g.strokePath(bgArc, juce::PathStrokeType(lineW, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        const float capR = juce::jmax(4.0f, arcRadius - lineW - 2.0f);
        g.setColour(palette::bgCard);
        g.fillEllipse(centre.x - capR, centre.y - capR, capR * 2.0f, capR * 2.0f);
        g.setColour(slider.isMouseOverOrDragging() ? palette::accentCyan.withAlpha(0.55f) : palette::border);
        g.drawEllipse(centre.x - capR, centre.y - capR, capR * 2.0f, capR * 2.0f, 1.0f);

        if (slider.isEnabled()) {
            const auto fillCol = slider.findColour(juce::Slider::rotarySliderFillColourId);
            juce::Path valArc;
            valArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                 rotaryStartAngle, toAngle, true);
            g.setColour(fillCol.withAlpha(0.22f));
            g.strokePath(valArc, juce::PathStrokeType(lineW + 3.0f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
            g.setColour(fillCol);
            g.strokePath(valArc, juce::PathStrokeType(lineW, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
        }

        const float pointerLen = capR * 0.62f;
        const float pointerStart = capR * 0.22f;
        const float angle = toAngle - juce::MathConstants<float>::halfPi;
        const juce::Point<float> p1(centre.x + std::cos(angle) * pointerStart,
                                    centre.y + std::sin(angle) * pointerStart);
        const juce::Point<float> p2(centre.x + std::cos(angle) * (pointerStart + pointerLen),
                                    centre.y + std::sin(angle) * (pointerStart + pointerLen));
        g.setColour(palette::textPrimary);
        g.drawLine(juce::Line<float>(p1, p2), 2.0f);
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/,
                      juce::ComboBox& box) override {
        const auto bounds = juce::Rectangle<float>(0.0f, 0.0f,
                                                   static_cast<float>(width),
                                                   static_cast<float>(height)).reduced(0.5f);
        const float corner = 6.0f;
        const bool hot = box.isMouseOver() || isButtonDown;

        g.setColour(hot ? palette::bgCard.brighter(0.08f) : palette::bgCard);
        g.fillRoundedRectangle(bounds, corner);
        g.setColour(hot ? palette::accentCyan.withAlpha(0.65f) : palette::border);
        g.drawRoundedRectangle(bounds, corner, 1.0f);

        const float cx = static_cast<float>(width) - 13.0f;
        const float cy = static_cast<float>(height) * 0.5f;
        juce::Path arrow;
        arrow.startNewSubPath(cx - 4.0f, cy - 2.0f);
        arrow.lineTo(cx, cy + 2.5f);
        arrow.lineTo(cx + 4.0f, cy - 2.0f);
        g.setColour(hot ? palette::accentCyan : palette::textMuted);
        g.strokePath(arrow, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override {
        const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        const float corner = juce::jmin(bounds.getHeight() * 0.5f, 12.0f);

        auto base = backgroundColour;
        if (button.getToggleState())
            base = palette::accentCyan.withAlpha(0.24f);
        if (shouldDrawButtonAsDown)
            base = base.brighter(0.18f);
        else if (shouldDrawButtonAsHighlighted)
            base = base.brighter(0.10f);

        g.setColour(base);
        g.fillRoundedRectangle(bounds, corner);

        const auto borderCol = button.getToggleState()
                                   ? palette::accentCyan.withAlpha(0.75f)
                                   : (shouldDrawButtonAsHighlighted
                                          ? palette::accentCyan.withAlpha(0.55f)
                                          : palette::border);
        g.setColour(borderCol);
        g.drawRoundedRectangle(bounds, corner, 1.0f);
    }

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override {
        return palette::makeFont(juce::jlimit(10.5f, 13.0f, static_cast<float>(buttonHeight) * 0.48f), true);
    }

    juce::Font getComboBoxFont(juce::ComboBox& box) override {
        return palette::makeFont(juce::jlimit(11.0f, 13.0f, static_cast<float>(box.getHeight()) * 0.5f), false);
    }

    juce::Font getPopupMenuFont() override {
        return palette::makeFont(12.5f, false);
    }
};

} // namespace harmonyhud::ui
