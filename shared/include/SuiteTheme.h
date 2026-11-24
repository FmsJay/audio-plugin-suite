#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace suite::ui {

// ============================================================================
// Dark Studio Palette
// ============================================================================
inline const juce::Colour bgDark        { 0xff0d0f14 };
inline const juce::Colour bgPanel       { 0xff151922 };
inline const juce::Colour bgCard        { 0xff1c222e };
inline const juce::Colour border        { 0xff2b3344 };
inline const juce::Colour textPrimary   { 0xffeef2f8 };
inline const juce::Colour textMuted     { 0xff8b95a8 };
inline const juce::Colour accentCyan    { 0xff22d3ee };
inline const juce::Colour accentAmber   { 0xfff59e0b };
inline const juce::Colour accentViolet  { 0xffa855f7 };
inline const juce::Colour accentEmerald { 0xff10b981 };
inline const juce::Colour accentCoral   { 0xfff43f5e };
inline const juce::Colour meterGreen    { 0xff22c55e };
inline const juce::Colour meterYellow   { 0xffeab308 };
inline const juce::Colour meterRed      { 0xffef4444 };

namespace palette {
    inline const juce::Colour& bgDark        = suite::ui::bgDark;
    inline const juce::Colour& bgPanel       = suite::ui::bgPanel;
    inline const juce::Colour& bgCard        = suite::ui::bgCard;
    inline const juce::Colour& border        = suite::ui::border;
    inline const juce::Colour& textPrimary   = suite::ui::textPrimary;
    inline const juce::Colour& textMuted     = suite::ui::textMuted;
    inline const juce::Colour& accentCyan    = suite::ui::accentCyan;
    inline const juce::Colour& accentAmber   = suite::ui::accentAmber;
    inline const juce::Colour& accentViolet  = suite::ui::accentViolet;
    inline const juce::Colour& accentEmerald = suite::ui::accentEmerald;
    inline const juce::Colour& accentCoral   = suite::ui::accentCoral;
    inline const juce::Colour& meterGreen    = suite::ui::meterGreen;
    inline const juce::Colour& meterYellow   = suite::ui::meterYellow;
    inline const juce::Colour& meterRed      = suite::ui::meterRed;
} // namespace palette

// ============================================================================
// SuiteLookAndFeel (Vector Studio Knobs, Pill Toggles, Dark Sliders/Combos)
// ============================================================================
class SuiteLookAndFeel : public juce::LookAndFeel_V4 {
public:
    explicit SuiteLookAndFeel(juce::Colour accent = accentCyan)
        : accent_(accent) {
        applyPalette();
    }

    void setAccentColour(juce::Colour accent) {
        accent_ = accent;
        applyPalette();
    }

    juce::Colour getAccentColour() const noexcept { return accent_; }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override {
        const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(4.0f);
        const float radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();
        const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        const auto knobAccent = slider.isColourSpecified(juce::Slider::rotarySliderFillColourId)
                                    ? slider.findColour(juce::Slider::rotarySliderFillColourId)
                                    : accent_;

        const float arcThickness = std::clamp(radius * 0.14f, 2.5f, 5.0f);
        const float arcRadius = radius - arcThickness * 0.5f;

        // Background track arc
        juce::Path trackArc;
        trackArc.addCentredArc(cx, cy, arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(border.brighter(0.15f));
        g.strokePath(trackArc, juce::PathStrokeType(arcThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Active value arc
        if (sliderPos > 0.001f) {
            juce::Path valueArc;
            valueArc.addCentredArc(cx, cy, arcRadius, arcRadius, 0.0f, rotaryStartAngle, angle, true);
            g.setColour(knobAccent.withAlpha(0.22f));
            g.strokePath(valueArc, juce::PathStrokeType(arcThickness + 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(knobAccent);
            g.strokePath(valueArc, juce::PathStrokeType(arcThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Dial cap body
        const float capRadius = std::max(4.0f, radius - arcThickness - 3.5f);
        const auto capRect = juce::Rectangle<float>(cx - capRadius, cy - capRadius, capRadius * 2.0f, capRadius * 2.0f);
        juce::ColourGradient capGrad(bgCard.brighter(0.18f), cx, cy - capRadius,
                                     bgDark.brighter(0.06f), cx, cy + capRadius, false);
        g.setGradientFill(capGrad);
        g.fillEllipse(capRect);

        g.setColour(border.brighter(0.25f));
        g.drawEllipse(capRect, 1.0f);

        // Pointer indicator
        juce::Path pointer;
        const float ptrLen = capRadius * 0.58f;
        const float ptrThickness = std::clamp(capRadius * 0.14f, 1.8f, 3.0f);
        pointer.addRoundedRectangle(-ptrThickness * 0.5f, -capRadius + 3.0f, ptrThickness, ptrLen, ptrThickness * 0.5f);
        pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(cx, cy));
        g.setColour(slider.isEnabled() ? textPrimary : textMuted);
        g.fillPath(pointer);
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          const juce::Slider::SliderStyle style, juce::Slider& slider) override {
        if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearVertical) {
            juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
            return;
        }

        const auto trackAccent = slider.isColourSpecified(juce::Slider::trackColourId)
                                     ? slider.findColour(juce::Slider::trackColourId)
                                     : accent_;
        const bool isHoriz = (style == juce::Slider::LinearHorizontal);
        const float trackThickness = 5.0f;
        const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();

        if (isHoriz) {
            const float cy = bounds.getCentreY();
            const juce::Rectangle<float> track(bounds.getX() + 4.0f, cy - trackThickness * 0.5f,
                                               bounds.getWidth() - 8.0f, trackThickness);
            g.setColour(bgDark);
            g.fillRoundedRectangle(track, trackThickness * 0.5f);
            g.setColour(border);
            g.drawRoundedRectangle(track, trackThickness * 0.5f, 1.0f);

            const float fillW = std::clamp(sliderPos - track.getX(), 0.0f, track.getWidth());
            g.setColour(trackAccent);
            g.fillRoundedRectangle(track.withWidth(fillW), trackThickness * 0.5f);

            const float thumbR = 6.5f;
            g.setColour(textPrimary);
            g.fillEllipse(sliderPos - thumbR, cy - thumbR, thumbR * 2.0f, thumbR * 2.0f);
            g.setColour(trackAccent);
            g.drawEllipse(sliderPos - thumbR, cy - thumbR, thumbR * 2.0f, thumbR * 2.0f, 1.5f);
        } else {
            const float cx = bounds.getCentreX();
            const juce::Rectangle<float> track(cx - trackThickness * 0.5f, bounds.getY() + 4.0f,
                                               trackThickness, bounds.getHeight() - 8.0f);
            g.setColour(bgDark);
            g.fillRoundedRectangle(track, trackThickness * 0.5f);
            g.setColour(border);
            g.drawRoundedRectangle(track, trackThickness * 0.5f, 1.0f);

            const float fillTop = std::clamp(sliderPos, track.getY(), track.getBottom());
            g.setColour(trackAccent);
            g.fillRoundedRectangle(juce::Rectangle<float>(track.getX(), fillTop, track.getWidth(), track.getBottom() - fillTop),
                                   trackThickness * 0.5f);

            const float thumbR = 6.5f;
            g.setColour(textPrimary);
            g.fillEllipse(cx - thumbR, sliderPos - thumbR, thumbR * 2.0f, thumbR * 2.0f);
            g.setColour(trackAccent);
            g.drawEllipse(cx - thumbR, sliderPos - thumbR, thumbR * 2.0f, thumbR * 2.0f, 1.5f);
        }
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override {
        const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        const float corner = std::min(bounds.getHeight() * 0.5f, 8.0f);
        const bool toggled = button.getToggleState();

        auto fill = toggled ? (button.isColourSpecified(juce::TextButton::buttonOnColourId)
                                   ? button.findColour(juce::TextButton::buttonOnColourId)
                                   : accent_)
                            : backgroundColour;

        if (shouldDrawButtonAsDown)
            fill = fill.darker(0.18f);
        else if (shouldDrawButtonAsHighlighted)
            fill = fill.brighter(0.12f);

        g.setColour(fill);
        g.fillRoundedRectangle(bounds, corner);

        g.setColour(toggled ? fill.brighter(0.35f) : border.brighter(0.15f));
        g.drawRoundedRectangle(bounds, corner, 1.0f);
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override {
        const auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
        const bool on = button.getToggleState();
        const float pillH = std::min(bounds.getHeight() - 4.0f, 18.0f);
        const float pillW = pillH * 1.85f;
        const float py = bounds.getCentreY() - pillH * 0.5f;
        const juce::Rectangle<float> pill(bounds.getX() + 2.0f, py, pillW, pillH);

        auto trackCol = on ? accent_ : bgDark.brighter(0.08f);
        if (shouldDrawButtonAsDown) trackCol = trackCol.darker(0.15f);
        else if (shouldDrawButtonAsHighlighted) trackCol = trackCol.brighter(0.12f);

        g.setColour(trackCol);
        g.fillRoundedRectangle(pill, pillH * 0.5f);
        g.setColour(on ? accent_.brighter(0.25f) : border);
        g.drawRoundedRectangle(pill, pillH * 0.5f, 1.0f);

        const float knobD = pillH - 4.0f;
        const float knobX = on ? (pill.getRight() - knobD - 2.0f) : (pill.getX() + 2.0f);
        g.setColour(textPrimary);
        g.fillEllipse(knobX, pill.getY() + 2.0f, knobD, knobD);

        g.setColour(button.isEnabled() ? (on ? textPrimary : textMuted) : textMuted.withAlpha(0.5f));
        g.setFont(juce::FontOptions(13.0f, juce::Font::plain));
        const auto textRect = bounds.withTrimmedLeft(pillW + 8.0f);
        g.drawFittedText(button.getButtonText(), textRect.toNearestInt(), juce::Justification::centredLeft, 1);
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/,
                      juce::ComboBox& box) override {
        const auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat().reduced(0.5f);
        const float corner = 6.0f;

        g.setColour(isButtonDown ? bgCard.brighter(0.1f) : bgDark.brighter(0.06f));
        g.fillRoundedRectangle(bounds, corner);
        g.setColour(box.hasKeyboardFocus(true) ? accent_ : border.brighter(0.15f));
        g.drawRoundedRectangle(bounds, corner, 1.0f);

        // Chevron
        const float arrowX = bounds.getRight() - 14.0f;
        const float arrowY = bounds.getCentreY();
        juce::Path chevron;
        chevron.startNewSubPath(arrowX - 4.0f, arrowY - 2.0f);
        chevron.lineTo(arrowX, arrowY + 2.5f);
        chevron.lineTo(arrowX + 4.0f, arrowY - 2.0f);
        g.setColour(textMuted);
        g.strokePath(chevron, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

private:
    void applyPalette() {
        setColour(juce::ResizableWindow::backgroundColourId, bgDark);
        setColour(juce::Label::textColourId, textPrimary);
        setColour(juce::TextButton::buttonColourId, bgCard);
        setColour(juce::TextButton::buttonOnColourId, accent_);
        setColour(juce::TextButton::textColourOffId, textPrimary);
        setColour(juce::TextButton::textColourOnId, bgDark);
        setColour(juce::ComboBox::backgroundColourId, bgDark);
        setColour(juce::ComboBox::textColourId, textPrimary);
        setColour(juce::ComboBox::outlineColourId, border);
        setColour(juce::PopupMenu::backgroundColourId, bgPanel);
        setColour(juce::PopupMenu::textColourId, textPrimary);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, accent_.withAlpha(0.28f));
        setColour(juce::PopupMenu::highlightedTextColourId, textPrimary);
        setColour(juce::Slider::textBoxTextColourId, textMuted);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::rotarySliderFillColourId, accent_);
        setColour(juce::Slider::trackColourId, accent_);
        setColour(juce::ScrollBar::thumbColourId, border.brighter(0.35f));
    }

    juce::Colour accent_;
};

// ============================================================================
// HeaderBar (Studio Plugin Top Banner)
// ============================================================================
class HeaderBar : public juce::Component {
public:
    HeaderBar(const juce::String& title, const juce::String& subtitle = {}, juce::Colour accent = accentCyan)
        : title_(title), subtitle_(subtitle), accent_(accent) {}

    void setTitle(const juce::String& title)       { title_ = title; repaint(); }
    void setSubtitle(const juce::String& subtitle) { subtitle_ = subtitle; repaint(); }
    void setAccentColour(juce::Colour accent)      { accent_ = accent; repaint(); }

    juce::Rectangle<int> getRightArea(int width = 260) const noexcept {
        return getLocalBounds().reduced(12, 6).removeFromRight(width);
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(bgPanel);
        g.fillRect(bounds);

        // Top accent hairline
        g.setColour(accent_);
        g.fillRect(bounds.withHeight(2.5f));

        // Bottom separator
        g.setColour(border);
        g.fillRect(bounds.removeFromBottom(1.0f));

        auto textArea = getLocalBounds().reduced(14, 4);
        // Glowing accent dot
        const float dotSize = 8.0f;
        g.setColour(accent_.withAlpha(0.28f));
        g.fillEllipse(static_cast<float>(textArea.getX()) - 2.0f,
                      static_cast<float>(textArea.getCentreY()) - dotSize * 0.5f - 2.0f,
                      dotSize + 4.0f, dotSize + 4.0f);
        g.setColour(accent_);
        g.fillEllipse(static_cast<float>(textArea.getX()),
                      static_cast<float>(textArea.getCentreY()) - dotSize * 0.5f,
                      dotSize, dotSize);

        textArea.removeFromLeft(16);
        g.setColour(textPrimary);
        g.setFont(juce::FontOptions(17.0f, juce::Font::bold));
        const int titleWidth = std::min(textArea.getWidth(), 260);
        g.drawText(title_, textArea.removeFromLeft(titleWidth), juce::Justification::centredLeft, true);

        if (subtitle_.isNotEmpty() && textArea.getWidth() > 40) {
            g.setColour(textMuted);
            g.setFont(juce::FontOptions(12.5f, juce::Font::plain));
            g.drawText(subtitle_, textArea, juce::Justification::centredLeft, true);
        }
    }

private:
    juce::String title_;
    juce::String subtitle_;
    juce::Colour accent_;
};

// ============================================================================
// RotaryControl (Labeled Vector Knob + Optional APVTS Attachment)
// ============================================================================
class RotaryControl : public juce::Component {
public:
    explicit RotaryControl(const juce::String& labelText = {}, juce::Colour accent = accentCyan) {
        slider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 16);
        slider_.setColour(juce::Slider::rotarySliderFillColourId, accent);
        addAndMakeVisible(slider_);

        label_.setText(labelText, juce::dontSendNotification);
        label_.setJustificationType(juce::Justification::centred);
        label_.setColour(juce::Label::textColourId, textMuted);
        label_.setFont(juce::FontOptions(11.5f, juce::Font::bold));
        addAndMakeVisible(label_);
    }

    RotaryControl(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId,
                  const juce::String& labelText, juce::Colour accent = accentCyan)
        : RotaryControl(labelText, accent) {
        attach(apvts, paramId);
    }

    void attach(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId) {
        attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, paramId, slider_);
    }

    void setAccentColour(juce::Colour accent) {
        slider_.setColour(juce::Slider::rotarySliderFillColourId, accent);
        slider_.repaint();
    }

    void setTextValueSuffix(const juce::String& suffix) {
        slider_.setTextValueSuffix(suffix);
    }

    juce::Slider& getSlider() noexcept { return slider_; }
    juce::Label&  getLabel() noexcept  { return label_; }

    void resized() override {
        auto area = getLocalBounds();
        label_.setBounds(area.removeFromTop(16));
        slider_.setBounds(area);
    }

private:
    juce::Slider slider_;
    juce::Label  label_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment_;
};

// ============================================================================
// LevelMeter (Stereo/Mono Peak Meter with Hold & Studio Colour Zones)
// ============================================================================
class LevelMeter : public juce::Component {
public:
    explicit LevelMeter(bool stereo = true, bool horizontal = false)
        : stereo_(stereo), horizontal_(horizontal) {}

    void setStereo(bool stereo) noexcept         { stereo_ = stereo; repaint(); }
    void setHorizontal(bool horizontal) noexcept { horizontal_ = horizontal; repaint(); }

    void setLevelsDb(float leftDb, float rightDb) {
        leftDb_  = std::clamp(leftDb,  -60.0f, 6.0f);
        rightDb_ = std::clamp(rightDb, -60.0f, 6.0f);
        peakL_   = std::max(leftDb_,  peakL_ - 0.35f);
        peakR_   = std::max(rightDb_, peakR_ - 0.35f);
        repaint();
    }

    void setLevelDb(float db) {
        setLevelsDb(db, db);
    }

    void setLinearLevels(float leftLinear, float rightLinear) {
        const auto toDb = [](float v) {
            return (v <= 1.0e-5f) ? -60.0f : std::clamp(20.0f * std::log10(v), -60.0f, 6.0f);
        };
        setLevelsDb(toDb(leftLinear), toDb(rightLinear));
    }

    void resetPeaks() {
        peakL_ = -60.0f;
        peakR_ = -60.0f;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(bgDark);
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(border);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

        auto inner = bounds.reduced(3.0f);
        if (!stereo_) {
            drawBar(g, inner, leftDb_, peakL_);
        } else if (horizontal_) {
            const float gap = 2.0f;
            const float h = (inner.getHeight() - gap) * 0.5f;
            drawBar(g, inner.withHeight(h), leftDb_, peakL_);
            drawBar(g, inner.withTrimmedTop(h + gap), rightDb_, peakR_);
        } else {
            const float gap = 2.0f;
            const float w = (inner.getWidth() - gap) * 0.5f;
            drawBar(g, inner.withWidth(w), leftDb_, peakL_);
            drawBar(g, inner.withTrimmedLeft(w + gap), rightDb_, peakR_);
        }
    }

private:
    static float dbToNorm(float db) noexcept {
        return std::clamp((db + 60.0f) / 66.0f, 0.0f, 1.0f);
    }

    void drawBar(juce::Graphics& g, juce::Rectangle<float> bar, float db, float peakDb) const {
        g.setColour(bgPanel);
        g.fillRoundedRectangle(bar, 2.0f);

        const float norm = dbToNorm(db);
        const float peakNorm = dbToNorm(peakDb);

        if (horizontal_) {
            juce::ColourGradient grad(meterGreen, bar.getX(), bar.getCentreY(),
                                      meterRed,   bar.getRight(), bar.getCentreY(), false);
            grad.addColour(0.72, meterYellow);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * norm), 2.0f);

            if (peakNorm > 0.02f) {
                const float px = bar.getX() + bar.getWidth() * peakNorm;
                g.setColour(peakDb > -0.5f ? meterRed : textPrimary);
                g.fillRect(px - 1.0f, bar.getY(), 2.0f, bar.getHeight());
            }
        } else {
            juce::ColourGradient grad(meterGreen, bar.getCentreX(), bar.getBottom(),
                                      meterRed,   bar.getCentreX(), bar.getY(), false);
            grad.addColour(0.72, meterYellow);
            g.setGradientFill(grad);
            const float fillH = bar.getHeight() * norm;
            g.fillRoundedRectangle(bar.withTop(bar.getBottom() - fillH), 2.0f);

            if (peakNorm > 0.02f) {
                const float py = bar.getBottom() - bar.getHeight() * peakNorm;
                g.setColour(peakDb > -0.5f ? meterRed : textPrimary);
                g.fillRect(bar.getX(), py - 1.0f, bar.getWidth(), 2.0f);
            }
        }
    }

    bool  stereo_     = true;
    bool  horizontal_ = false;
    float leftDb_     = -60.0f;
    float rightDb_    = -60.0f;
    float peakL_      = -60.0f;
    float peakR_      = -60.0f;
};

// ============================================================================
// SectionCard (Rounded Dark Panel with Title & Accent Indicator)
// ============================================================================
class SectionCard : public juce::Component {
public:
    explicit SectionCard(const juce::String& title = {}, juce::Colour accent = accentCyan)
        : title_(title), accent_(accent) {}

    void setTitle(const juce::String& title)  { title_ = title; repaint(); }
    void setAccentColour(juce::Colour accent) { accent_ = accent; repaint(); }

    juce::Rectangle<int> getContentBounds() const noexcept {
        auto r = getLocalBounds().reduced(10);
        if (title_.isNotEmpty()) {
            r.removeFromTop(22);
        }
        return r;
    }

    void paint(juce::Graphics& g) override {
        const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(bgCard);
        g.fillRoundedRectangle(bounds, 8.0f);

        g.setColour(border);
        g.drawRoundedRectangle(bounds, 8.0f, 1.0f);

        if (title_.isNotEmpty()) {
            auto header = getLocalBounds().reduced(10, 6).removeFromTop(18);
            g.setColour(accent_);
            g.fillRoundedRectangle(static_cast<float>(header.getX()),
                                   static_cast<float>(header.getY() + 3),
                                   3.0f, static_cast<float>(header.getHeight() - 6), 1.5f);

            header.removeFromLeft(9);
            g.setColour(textMuted);
            g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
            g.drawText(title_.toUpperCase(), header, juce::Justification::centredLeft, true);
        }
    }

private:
    juce::String title_;
    juce::Colour accent_;
};

} // namespace suite::ui
