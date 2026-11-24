#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <SuiteTheme.h>
#include "../VowelMorphParams.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace vowelmorph::ui {

// ============================================================================
// SyllableSeqBar
// 8-Step Syllable Sequencer strip with:
// - Header row: Title, [DAW SYNC] / glowing cyan [FOSSIL EUCLID LOCK] badge,
//               Rate ComboBox, Glide & Dip compact horizontal sliders.
// - Main row: 8 interactive step cards (left-click cycles phoneme 0..10,
//             right-click opens fast phoneme PopupMenu, bottom Accent pill).
// ============================================================================
class SyllableSeqBar : public juce::Component {
public:
    explicit SyllableSeqBar(juce::AudioProcessorValueTreeState& apvts)
        : apvts_(apvts) {
        setOpaque(false);

        // Rate ComboBox
        rateLabel_.setText("RATE", juce::dontSendNotification);
        rateLabel_.setJustificationType(juce::Justification::centredRight);
        rateLabel_.setColour(juce::Label::textColourId, suite::ui::textMuted);
        rateLabel_.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        addAndMakeVisible(rateLabel_);

        for (int i = 0; i < 7; ++i)
            rateCombo_.addItem(seqRateName(i), i + 1);
        addAndMakeVisible(rateCombo_);
        rateAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            apvts_, params::seqRate, rateCombo_);

        // Glide compact horizontal slider
        glideLabel_.setText("GLIDE", juce::dontSendNotification);
        glideLabel_.setJustificationType(juce::Justification::centredRight);
        glideLabel_.setColour(juce::Label::textColourId, suite::ui::textMuted);
        glideLabel_.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        addAndMakeVisible(glideLabel_);

        glideSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
        glideSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        glideSlider_.setColour(juce::Slider::trackColourId, suite::ui::accentViolet);
        addAndMakeVisible(glideSlider_);
        glideAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts_, params::seqGlide, glideSlider_);

        // Consonant Dip compact horizontal slider
        dipLabel_.setText("DIP", juce::dontSendNotification);
        dipLabel_.setJustificationType(juce::Justification::centredRight);
        dipLabel_.setColour(juce::Label::textColourId, suite::ui::textMuted);
        dipLabel_.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        addAndMakeVisible(dipLabel_);

        dipSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
        dipSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        dipSlider_.setColour(juce::Slider::trackColourId, suite::ui::accentCyan);
        addAndMakeVisible(dipSlider_);
        dipAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts_, params::consonantDip, dipSlider_);

        refreshStepsFromApvts();
    }

    void updateTelemetry(const vowelmorph::TelemetrySnapshot& snap) {
        const int clampedStep = std::clamp(snap.activeStep, 0, kNumSeqSteps - 1);
        const bool stepChanged = (clampedStep != activeStep_) || (snap.euclidLocked != euclidLocked_);
        activeStep_ = clampedStep;
        euclidLocked_ = snap.euclidLocked;

        const bool paramsChanged = refreshStepsFromApvts();
        if (stepChanged || paramsChanged)
            repaint();
    }

    void resized() override {
        auto area = getLocalBounds().reduced(10, 8);
        auto header = area.removeFromTop(24);

        // Reserve left side of header for painted title + sync badge
        const int controlsW = std::min(header.getWidth() - 260, 440);
        auto controls = header.removeFromRight(std::max(controlsW, 280));
        titleBadgeBounds_ = header;

        // Lay out Rate, Glide, Dip right-to-left or left-to-right cleanly
        const int rateLabelW = 34;
        const int rateComboW = 64;
        const int glideLabelW = 40;
        const int dipLabelW = 28;
        const int remaining = std::max(80, controls.getWidth() - (rateLabelW + rateComboW + glideLabelW + dipLabelW + 24));
        const int sliderW = remaining / 2;

        rateLabel_.setBounds(controls.removeFromLeft(rateLabelW));
        controls.removeFromLeft(4);
        rateCombo_.setBounds(controls.removeFromLeft(rateComboW).reduced(0, 1));
        controls.removeFromLeft(10);

        glideLabel_.setBounds(controls.removeFromLeft(glideLabelW));
        controls.removeFromLeft(4);
        glideSlider_.setBounds(controls.removeFromLeft(sliderW));
        controls.removeFromLeft(8);

        dipLabel_.setBounds(controls.removeFromLeft(dipLabelW));
        controls.removeFromLeft(4);
        dipSlider_.setBounds(controls);

        area.removeFromTop(6);

        // 8 equal step cards
        const int gap = 6;
        const int totalGap = gap * (kNumSeqSteps - 1);
        const int cardW = std::max(36, (area.getWidth() - totalGap) / kNumSeqSteps);
        int x = area.getX();
        for (int i = 0; i < kNumSeqSteps; ++i) {
            const int w = (i == kNumSeqSteps - 1) ? std::max(cardW, area.getRight() - x) : cardW;
            const juce::Rectangle<int> card(x, area.getY(), w, area.getHeight());
            stepBounds_[static_cast<size_t>(i)] = card;
            accentBounds_[static_cast<size_t>(i)] = card.reduced(6, 4).removeFromBottom(16);
            x += w + gap;
        }
    }

    void paint(juce::Graphics& g) override {
        const auto outer = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(suite::ui::bgCard);
        g.fillRoundedRectangle(outer, 8.0f);
        g.setColour(suite::ui::border);
        g.drawRoundedRectangle(outer, 8.0f, 1.0f);

        // Header title + accent bar
        auto hb = titleBadgeBounds_;
        if (hb.getWidth() > 40) {
            g.setColour(suite::ui::accentViolet);
            g.fillRoundedRectangle(static_cast<float>(hb.getX()),
                                   static_cast<float>(hb.getY() + 4),
                                   3.0f, static_cast<float>(hb.getHeight() - 8), 1.5f);
            hb.removeFromLeft(9);

            g.setColour(suite::ui::textMuted);
            g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            const int titleW = std::min(hb.getWidth(), 184);
            g.drawText("8-STEP SYLLABLE SEQUENCER", hb.removeFromLeft(titleW),
                       juce::Justification::centredLeft, true);

            // Sync badge pill
            if (hb.getWidth() > 54) {
                hb.removeFromLeft(6);
                const juce::String badgeText = euclidLocked_ ? "FOSSIL EUCLID LOCK" : "DAW SYNC";
                const int badgeW = std::min(hb.getWidth(), euclidLocked_ ? 132 : 76);
                const auto badgeRect = hb.removeFromLeft(badgeW).reduced(0, 2).toFloat();

                if (euclidLocked_) {
                    g.setColour(suite::ui::accentCyan.withAlpha(0.18f));
                    g.fillRoundedRectangle(badgeRect, 9.0f);
                    g.setColour(suite::ui::accentCyan);
                    g.drawRoundedRectangle(badgeRect, 9.0f, 1.1f);
                    g.setColour(suite::ui::accentCyan.brighter(0.25f));
                } else {
                    g.setColour(suite::ui::bgDark);
                    g.fillRoundedRectangle(badgeRect, 9.0f);
                    g.setColour(suite::ui::border);
                    g.drawRoundedRectangle(badgeRect, 9.0f, 1.0f);
                    g.setColour(suite::ui::textMuted);
                }

                g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
                g.drawFittedText(badgeText, badgeRect.toNearestInt().reduced(4, 0),
                                 juce::Justification::centred, 1);
            }
        }

        // Paint 8 step cards
        for (int i = 0; i < kNumSeqSteps; ++i) {
            const auto card = stepBounds_[static_cast<size_t>(i)].toFloat();
            const auto accRect = accentBounds_[static_cast<size_t>(i)].toFloat();
            const int stepVal = stepValues_[static_cast<size_t>(i)];
            const bool isRest = (stepVal >= kStepRest);
            const bool isActive = (i == activeStep_);
            const bool isAccent = stepAccents_[static_cast<size_t>(i)];
            const bool isHovered = (i == hoveredStep_);

            // Card fill
            juce::Colour fill = isActive ? suite::ui::bgPanel.interpolatedWith(suite::ui::accentCyan, 0.14f)
                                         : (isRest ? suite::ui::bgDark.brighter(0.02f) : suite::ui::bgPanel);
            if (isHovered)
                fill = fill.brighter(0.08f);

            g.setColour(fill);
            g.fillRoundedRectangle(card, 6.0f);

            // Active top glow bar
            if (isActive) {
                g.setColour(suite::ui::accentCyan.withAlpha(0.32f));
                g.fillRoundedRectangle(card.withHeight(6.0f), 3.0f);
                g.setColour(suite::ui::accentCyan);
                g.fillRoundedRectangle(card.reduced(6.0f, 0.0f).withHeight(3.0f), 1.5f);
            }

            // Card border
            g.setColour(isActive ? suite::ui::accentCyan
                                 : (isHovered ? suite::ui::accentViolet.withAlpha(0.75f) : suite::ui::border));
            g.drawRoundedRectangle(card.reduced(0.5f), 6.0f, isActive ? 1.6f : 1.0f);

            // Step number (1..8) top-left
            g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
            g.setColour(isActive ? suite::ui::accentCyan : suite::ui::textMuted.withAlpha(0.7f));
            g.drawText(juce::String(i + 1),
                       static_cast<int>(card.getX() + 6.0f), static_cast<int>(card.getY() + 4.0f),
                       16, 12, juce::Justification::centredLeft, false);

            // Main syllable label
            const auto bodyRect = card.withTrimmedTop(14.0f).withTrimmedBottom(22.0f);
            const juce::String sym = isRest ? "--" : juce::String(phonemeCode(stepVal));
            g.setFont(juce::FontOptions(17.0f, juce::Font::bold));
            g.setColour(isRest ? suite::ui::textMuted.withAlpha(0.55f)
                               : (isActive ? suite::ui::textPrimary : suite::ui::textPrimary.withAlpha(0.92f)));
            g.drawText(sym, bodyRect.toNearestInt(), juce::Justification::centred, false);

            // Bottom Accent pill
            g.setColour(isAccent ? suite::ui::accentAmber.withAlpha(0.24f) : suite::ui::bgDark);
            g.fillRoundedRectangle(accRect, 7.0f);
            g.setColour(isAccent ? suite::ui::accentAmber : suite::ui::border.withAlpha(0.7f));
            g.drawRoundedRectangle(accRect, 7.0f, 1.0f);

            const float dotD = 5.0f;
            g.setColour(isAccent ? suite::ui::accentAmber : suite::ui::textMuted.withAlpha(0.35f));
            g.fillEllipse(accRect.getX() + 6.0f, accRect.getCentreY() - dotD * 0.5f, dotD, dotD);

            g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
            g.setColour(isAccent ? suite::ui::accentAmber.brighter(0.2f) : suite::ui::textMuted.withAlpha(0.6f));
            g.drawText("ACC", accRect.withTrimmedLeft(12.0f).toNearestInt(), juce::Justification::centred, false);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override {
        int found = -1;
        for (int i = 0; i < kNumSeqSteps; ++i) {
            if (stepBounds_[static_cast<size_t>(i)].contains(e.getPosition())) {
                found = i;
                break;
            }
        }
        if (found != hoveredStep_) {
            hoveredStep_ = found;
            setMouseCursor(found >= 0 ? juce::MouseCursor::PointingHandCursor
                                      : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent& /*e*/) override {
        if (hoveredStep_ != -1) {
            hoveredStep_ = -1;
            repaint();
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        const auto pos = e.getPosition();
        for (int i = 0; i < kNumSeqSteps; ++i) {
            const size_t idx = static_cast<size_t>(i);
            if (!stepBounds_[idx].contains(pos))
                continue;

            if (e.mods.isPopupMenu()) {
                showPhonemeMenu(i);
                return;
            }

            if (accentBounds_[idx].contains(pos)) {
                setAccentParam(i, !stepAccents_[idx]);
                return;
            }

            const int nextVal = (stepValues_[idx] + 1) % (kStepRest + 1);
            setStepParam(i, nextVal);
            return;
        }
    }

private:
    bool refreshStepsFromApvts() {
        bool changed = false;
        for (int i = 0; i < kNumSeqSteps; ++i) {
            const size_t idx = static_cast<size_t>(i);
            int val = kStepRest;
            if (auto* raw = apvts_.getRawParameterValue(params::seqStepIds[i]))
                val = std::clamp(static_cast<int>(std::round(raw->load())), 0, kStepRest);

            bool acc = false;
            if (auto* rawAcc = apvts_.getRawParameterValue(params::seqAccentIds[i]))
                acc = (rawAcc->load() >= 0.5f);

            if (stepValues_[idx] != val || stepAccents_[idx] != acc) {
                stepValues_[idx] = val;
                stepAccents_[idx] = acc;
                changed = true;
            }
        }
        return changed;
    }

    void setStepParam(int stepIdx, int phonemeVal) {
        const int clamped = std::clamp(phonemeVal, 0, kStepRest);
        if (auto* param = apvts_.getParameter(params::seqStepIds[stepIdx])) {
            param->beginChangeGesture();
            param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(clamped)));
            param->endChangeGesture();
        }
        stepValues_[static_cast<size_t>(stepIdx)] = clamped;
        repaint();
    }

    void setAccentParam(int stepIdx, bool on) {
        if (auto* param = apvts_.getParameter(params::seqAccentIds[stepIdx])) {
            param->beginChangeGesture();
            param->setValueNotifyingHost(on ? 1.0f : 0.0f);
            param->endChangeGesture();
        }
        stepAccents_[static_cast<size_t>(stepIdx)] = on;
        repaint();
    }

    void showPhonemeMenu(int stepIdx) {
        juce::PopupMenu menu;
        const int current = stepValues_[static_cast<size_t>(stepIdx)];
        for (int p = 0; p < kNumPhonemes; ++p) {
            const juce::String label = juce::String(phonemeCode(p)) + "  " + phonemeIpa(p);
            menu.addItem(p + 1, label, true, current == p);
        }
        menu.addSeparator();
        menu.addItem(kStepRest + 1, "Rest (--)", true, current == kStepRest);

        juce::Component::SafePointer<SyllableSeqBar> safeThis(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                           [safeThis, stepIdx](int result) {
                               if (safeThis != nullptr && result >= 1 && result <= kStepRest + 1)
                                   safeThis->setStepParam(stepIdx, result - 1);
                           });
    }

    juce::AudioProcessorValueTreeState& apvts_;

    juce::Label rateLabel_;
    juce::ComboBox rateCombo_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> rateAtt_;

    juce::Label glideLabel_;
    juce::Slider glideSlider_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> glideAtt_;

    juce::Label dipLabel_;
    juce::Slider dipSlider_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dipAtt_;

    juce::Rectangle<int> titleBadgeBounds_;
    std::array<juce::Rectangle<int>, kNumSeqSteps> stepBounds_{};
    std::array<juce::Rectangle<int>, kNumSeqSteps> accentBounds_{};
    std::array<int, kNumSeqSteps> stepValues_ { 7, 4, 0, 10, 6, 4, 9, 10 };
    std::array<bool, kNumSeqSteps> stepAccents_ { true, false, true, false, true, false, false, false };

    int activeStep_ = 0;
    int hoveredStep_ = -1;
    bool euclidLocked_ = false;
};

} // namespace vowelmorph::ui
