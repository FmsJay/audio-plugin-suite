#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <SuiteTheme.h>
#include "../VowelMorphParams.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>

namespace vowelmorph::ui {

// ============================================================================
// LinkBarComponent
// Compact two-row studio control ribbon:
// Row 1: Factory Preset selector (18 presets), Tract Profile ComboBox,
//        Mod Mode segmented toggle buttons (Env | LFO | StepSeq | MIDI).
// Row 2: TextureLink strip (Fossil <-> VowelMorph) with Bus, live status pill
//        (T/H/N mini-bars + Euclid badge) & 4 link pills; HarmonyLink strip
//        (SessionMate / HarmonyHUD) with Bus, live chord/section pill & Section Lift.
// ============================================================================
class LinkBarComponent : public juce::Component {
public:
    explicit LinkBarComponent(juce::AudioProcessorValueTreeState& apvts,
                              std::function<void(int)> onPresetSelected = {})
        : apvts_(apvts), onPresetSelected_(std::move(onPresetSelected)) {
        setOpaque(false);

        auto configureLabel = [this](juce::Label& lbl, const juce::String& text) {
            lbl.setText(text, juce::dontSendNotification);
            lbl.setJustificationType(juce::Justification::centredRight);
            lbl.setColour(juce::Label::textColourId, suite::ui::textMuted);
            lbl.setFont(juce::FontOptions(10.0f, juce::Font::bold));
            addAndMakeVisible(lbl);
        };

        auto configureTogglePill = [this](juce::TextButton& btn, const juce::String& text, juce::Colour onCol) {
            btn.setButtonText(text);
            btn.setClickingTogglesState(true);
            btn.setColour(juce::TextButton::buttonColourId, suite::ui::bgDark);
            btn.setColour(juce::TextButton::buttonOnColourId, onCol);
            btn.setColour(juce::TextButton::textColourOffId, suite::ui::textMuted);
            btn.setColour(juce::TextButton::textColourOnId, suite::ui::bgDark);
            addAndMakeVisible(btn);
        };

        // --- Row 1: Preset, Tract Profile, Mod Mode ---
        configureLabel(presetLabel_, "PRESET");
        const auto& presets = getFactoryPresets();
        for (int i = 0; i < kNumPresets; ++i) {
            const auto& pr = presets[static_cast<size_t>(i)];
            const juce::String item = juce::String(pr.name) + "  [" + pr.category + "]";
            presetCombo_.addItem(item, i + 1);
        }
        presetCombo_.setSelectedId(1, juce::dontSendNotification);
        presetCombo_.onChange = [this]() {
            if (suppressPresetCallback_)
                return;
            const int idx = presetCombo_.getSelectedId() - 1;
            if (idx >= 0 && idx < kNumPresets && onPresetSelected_)
                onPresetSelected_(idx);
        };
        addAndMakeVisible(presetCombo_);

        configureLabel(tractLabel_, "TRACT");
        for (int i = 0; i < kNumTracts; ++i)
            tractCombo_.addItem(tractName(i), i + 1);
        addAndMakeVisible(tractCombo_);
        tractAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            apvts_, params::tractProfile, tractCombo_);

        configureLabel(modLabel_, "MOD");
        for (int i = 0; i < 4; ++i) {
            auto& btn = modButtons_[static_cast<size_t>(i)];
            btn.setButtonText(modModeName(i));
            btn.setClickingTogglesState(false);
            btn.setColour(juce::TextButton::buttonColourId, suite::ui::bgDark);
            btn.setColour(juce::TextButton::buttonOnColourId, suite::ui::accentViolet);
            btn.setColour(juce::TextButton::textColourOffId, suite::ui::textMuted);
            btn.setColour(juce::TextButton::textColourOnId, suite::ui::textPrimary);
            btn.onClick = [this, i]() { setModModeParam(i); };
            addAndMakeVisible(btn);
        }

        // --- Row 2: TextureLink (Fossil) & HarmonyLink (SessionMate) ---
        const juce::StringArray busChoices { "Off", "A", "B", "C", "D", "E", "F", "G", "H" };

        configureLabel(texBusLabel_, "TEX");
        for (int i = 0; i < busChoices.size(); ++i)
            texBusCombo_.addItem(busChoices[i], i + 1);
        addAndMakeVisible(texBusCombo_);
        texBusAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            apvts_, params::textureBus, texBusCombo_);

        configureTogglePill(euclidBtn_, "Euclid", suite::ui::accentEmerald);
        configureTogglePill(airBreathBtn_, "Air->Breath", suite::ui::accentCyan);
        configureTogglePill(transJawBtn_, "Trans->Jaw", suite::ui::accentViolet);
        configureTogglePill(freezeSweepBtn_, "Freeze Sweep", suite::ui::accentAmber);

        euclidAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts_, params::linkEuclidSync, euclidBtn_);
        airBreathAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts_, params::linkNoiseBreath, airBreathBtn_);
        transJawAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts_, params::linkTransientJaw, transJawBtn_);
        freezeSweepAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts_, params::linkFreezeSweep, freezeSweepBtn_);

        configureLabel(harmBusLabel_, "HARM");
        for (int i = 0; i < busChoices.size(); ++i)
            harmBusCombo_.addItem(busChoices[i], i + 1);
        addAndMakeVisible(harmBusCombo_);
        harmBusAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            apvts_, params::harmonyBus, harmBusCombo_);

        configureTogglePill(sectionLiftBtn_, "Section Lift", suite::ui::accentCyan);
        sectionLiftAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts_, params::harmonySectionLift, sectionLiftBtn_);

        syncModModeButtonsFromApvts();
    }

    void setSelectedPresetIndex(int idx) {
        if (idx < 0 || idx >= kNumPresets)
            return;
        suppressPresetCallback_ = true;
        presetCombo_.setSelectedId(idx + 1, juce::dontSendNotification);
        suppressPresetCallback_ = false;
    }

    void updateTelemetry(const vowelmorph::TelemetrySnapshot& snap) {
        snap_ = snap;
        syncModModeButtonsFromApvts();
        repaint();
    }

    void resized() override {
        auto area = getLocalBounds().reduced(10, 6);
        const int rowGap = 6;
        const int rowH = std::max(22, (area.getHeight() - rowGap) / 2);

        // --- Row 1 ---
        auto row1 = area.removeFromTop(rowH);
        const int presetLblW = 46;
        const int tractLblW = 42;
        const int modLblW = 32;
        const int modGroupW = std::min(248, std::max(196, row1.getWidth() / 4));
        const int tractComboW = std::min(156, std::max(118, row1.getWidth() / 6));

        presetLabel_.setBounds(row1.removeFromLeft(presetLblW));
        row1.removeFromLeft(4);

        auto modSection = row1.removeFromRight(modLblW + 4 + modGroupW);
        row1.removeFromRight(12);
        auto tractSection = row1.removeFromRight(tractLblW + 4 + tractComboW);
        row1.removeFromRight(12);

        presetCombo_.setBounds(row1);

        tractLabel_.setBounds(tractSection.removeFromLeft(tractLblW));
        tractSection.removeFromLeft(4);
        tractCombo_.setBounds(tractSection);

        modLabel_.setBounds(modSection.removeFromLeft(modLblW));
        modSection.removeFromLeft(4);
        const int btnGap = 3;
        const int btnW = (modSection.getWidth() - btnGap * 3) / 4;
        for (int i = 0; i < 4; ++i) {
            const int w = (i == 3) ? modSection.getWidth() : btnW;
            modButtons_[static_cast<size_t>(i)].setBounds(modSection.removeFromLeft(w));
            if (i < 3)
                modSection.removeFromLeft(btnGap);
        }

        area.removeFromTop(rowGap);

        // --- Row 2 ---
        auto row2 = area;
        const int totalW = row2.getWidth();
        const int dividerW = 12;
        const int texTotalW = static_cast<int>(std::round(static_cast<float>(totalW - dividerW) * 0.635f));

        auto texRow = row2.removeFromLeft(texTotalW);
        dividerBounds_ = row2.removeFromLeft(dividerW);
        auto harmRow = row2;

        // TextureLink strip
        texBusLabel_.setBounds(texRow.removeFromLeft(26));
        texRow.removeFromLeft(4);
        texBusCombo_.setBounds(texRow.removeFromLeft(54));
        texRow.removeFromLeft(6);

        // 4 compact toggle pills on the right of TextureLink strip
        const float texScale = std::clamp(static_cast<float>(texRow.getWidth()) / 490.0f, 0.78f, 1.0f);
        const int wEuclid = static_cast<int>(std::round(54.0f * texScale));
        const int wAir    = static_cast<int>(std::round(78.0f * texScale));
        const int wTrans  = static_cast<int>(std::round(78.0f * texScale));
        const int wFreeze = static_cast<int>(std::round(86.0f * texScale));
        const int pillGap = 4;

        freezeSweepBtn_.setBounds(texRow.removeFromRight(wFreeze));
        texRow.removeFromRight(pillGap);
        transJawBtn_.setBounds(texRow.removeFromRight(wTrans));
        texRow.removeFromRight(pillGap);
        airBreathBtn_.setBounds(texRow.removeFromRight(wAir));
        texRow.removeFromRight(pillGap);
        euclidBtn_.setBounds(texRow.removeFromRight(wEuclid));
        texRow.removeFromRight(6);

        fossilStatusBounds_ = texRow;

        // HarmonyLink strip
        harmBusLabel_.setBounds(harmRow.removeFromLeft(36));
        harmRow.removeFromLeft(4);
        harmBusCombo_.setBounds(harmRow.removeFromLeft(54));
        harmRow.removeFromLeft(6);

        const int wLift = std::min(86, std::max(68, harmRow.getWidth() / 3));
        sectionLiftBtn_.setBounds(harmRow.removeFromRight(wLift));
        harmRow.removeFromRight(6);

        harmonyStatusBounds_ = harmRow;
    }

    void paint(juce::Graphics& g) override {
        const auto outer = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(suite::ui::bgPanel);
        g.fillRoundedRectangle(outer, 8.0f);
        g.setColour(suite::ui::border);
        g.drawRoundedRectangle(outer, 8.0f, 1.0f);

        // Subtle horizontal divider between Row 1 and Row 2
        const float midY = static_cast<float>(getLocalBounds().getCentreY());
        g.setColour(suite::ui::border.withAlpha(0.55f));
        g.drawLine(10.0f, midY, static_cast<float>(getWidth() - 10), midY, 0.75f);

        // Vertical divider between TextureLink and HarmonyLink in Row 2
        if (!dividerBounds_.isEmpty()) {
            const float dx = static_cast<float>(dividerBounds_.getCentreX());
            g.setColour(suite::ui::border.withAlpha(0.75f));
            g.drawLine(dx, static_cast<float>(dividerBounds_.getY() + 2),
                       dx, static_cast<float>(dividerBounds_.getBottom() - 2), 1.0f);
        }

        paintFossilStatusPill(g, fossilStatusBounds_.toFloat());
        paintHarmonyStatusPill(g, harmonyStatusBounds_.toFloat());
    }

private:
    void setModModeParam(int modeIdx) {
        const int clamped = std::clamp(modeIdx, 0, 3);
        if (auto* param = apvts_.getParameter(params::modMode)) {
            param->beginChangeGesture();
            param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(clamped)));
            param->endChangeGesture();
        }
        applyModModeToggleVisual(clamped);
    }

    void syncModModeButtonsFromApvts() {
        int mode = snap_.modMode;
        if (auto* raw = apvts_.getRawParameterValue(params::modMode))
            mode = std::clamp(static_cast<int>(std::round(raw->load())), 0, 3);
        applyModModeToggleVisual(mode);
    }

    void applyModModeToggleVisual(int activeMode) {
        if (activeMode == cachedModMode_)
            return;
        cachedModMode_ = activeMode;
        for (int i = 0; i < 4; ++i)
            modButtons_[static_cast<size_t>(i)].setToggleState(i == activeMode, juce::dontSendNotification);
    }

    void paintFossilStatusPill(juce::Graphics& g, juce::Rectangle<float> bounds) const {
        if (bounds.getWidth() < 40.0f)
            return;

        const bool linked = snap_.fossilConnected;
        const float corner = std::min(bounds.getHeight() * 0.5f, 8.0f);

        g.setColour(linked ? suite::ui::accentEmerald.withAlpha(0.14f) : suite::ui::bgDark);
        g.fillRoundedRectangle(bounds, corner);
        g.setColour(linked ? suite::ui::accentEmerald.withAlpha(0.75f) : suite::ui::border);
        g.drawRoundedRectangle(bounds.reduced(0.5f), corner, 1.0f);

        auto inner = bounds.reduced(7.0f, 2.0f);
        const float dotD = 6.0f;
        const float dotY = inner.getCentreY() - dotD * 0.5f;

        if (linked) {
            g.setColour(suite::ui::accentEmerald.withAlpha(0.32f));
            g.fillEllipse(inner.getX() - 2.0f, dotY - 2.0f, dotD + 4.0f, dotD + 4.0f);
            g.setColour(suite::ui::accentEmerald);
        } else {
            g.setColour(suite::ui::textMuted.withAlpha(0.45f));
        }
        g.fillEllipse(inner.getX(), dotY, dotD, dotD);
        inner.removeFromLeft(dotD + 5.0f);

        if (!linked) {
            g.setFont(juce::FontOptions(10.5f, juce::Font::plain));
            g.setColour(suite::ui::textMuted);
            g.drawFittedText("Fossil Standby", inner.toNearestInt(), juce::Justification::centredLeft, 1);
            return;
        }

        // Right-aligned Euclid badge (e.g. "5/16") if wide enough
        if (inner.getWidth() > 118.0f) {
            const juce::String eucStr = "Euc " + juce::String(snap_.fossilEuclidHits)
                                        + "/" + juce::String(std::max(1, snap_.fossilEuclidSteps));
            const auto eucRect = inner.removeFromRight(48.0f).reduced(0.0f, 1.0f);
            g.setColour(suite::ui::accentCyan.withAlpha(0.16f));
            g.fillRoundedRectangle(eucRect, 4.0f);
            g.setColour(suite::ui::accentCyan.withAlpha(0.85f));
            g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
            g.drawFittedText(eucStr, eucRect.toNearestInt(), juce::Justification::centred, 1);
            inner.removeFromRight(4.0f);
        }

        // Tiny live T/H/N energy bar indicators
        if (inner.getWidth() > 80.0f) {
            const auto metersRect = inner.removeFromRight(22.0f).reduced(0.0f, 2.0f);
            const std::array<float, 3> vals {
                std::clamp(snap_.fossilTransient, 0.0f, 1.0f),
                std::clamp(snap_.fossilTonal, 0.0f, 1.0f),
                std::clamp(snap_.fossilNoise, 0.0f, 1.0f)
            };
            const std::array<juce::Colour, 3> cols {
                suite::ui::accentAmber,
                suite::ui::accentViolet,
                suite::ui::accentCyan
            };

            const float bw = 5.0f;
            const float gap = 2.0f;
            for (int i = 0; i < 3; ++i) {
                const float bx = metersRect.getX() + static_cast<float>(i) * (bw + gap);
                const juce::Rectangle<float> track(bx, metersRect.getY(), bw, metersRect.getHeight());
                g.setColour(suite::ui::bgDark.withAlpha(0.85f));
                g.fillRoundedRectangle(track, 1.5f);
                const float fillH = std::max(2.0f, track.getHeight() * vals[static_cast<size_t>(i)]);
                g.setColour(cols[static_cast<size_t>(i)]);
                g.fillRoundedRectangle(track.withTop(track.getBottom() - fillH), 1.5f);
            }
            inner.removeFromRight(5.0f);
        }

        g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        g.setColour(suite::ui::accentEmerald.brighter(0.25f));
        g.drawFittedText("Fossil Linked", inner.toNearestInt(), juce::Justification::centredLeft, 1);
    }

    void paintHarmonyStatusPill(juce::Graphics& g, juce::Rectangle<float> bounds) const {
        if (bounds.getWidth() < 40.0f)
            return;

        const bool linked = snap_.harmonyConnected;
        const float corner = std::min(bounds.getHeight() * 0.5f, 8.0f);

        g.setColour(linked ? suite::ui::accentCyan.withAlpha(0.14f) : suite::ui::bgDark);
        g.fillRoundedRectangle(bounds, corner);
        g.setColour(linked ? suite::ui::accentCyan.withAlpha(0.75f) : suite::ui::border);
        g.drawRoundedRectangle(bounds.reduced(0.5f), corner, 1.0f);

        auto inner = bounds.reduced(7.0f, 2.0f);
        const float dotD = 6.0f;
        const float dotY = inner.getCentreY() - dotD * 0.5f;

        if (linked) {
            g.setColour(suite::ui::accentCyan.withAlpha(0.32f));
            g.fillEllipse(inner.getX() - 2.0f, dotY - 2.0f, dotD + 4.0f, dotD + 4.0f);
            g.setColour(suite::ui::accentCyan);
        } else {
            g.setColour(suite::ui::textMuted.withAlpha(0.45f));
        }
        g.fillEllipse(inner.getX(), dotY, dotD, dotD);
        inner.removeFromLeft(dotD + 5.0f);

        if (!linked) {
            g.setFont(juce::FontOptions(10.5f, juce::Font::plain));
            g.setColour(suite::ui::textMuted);
            g.drawFittedText("Harmony Standby", inner.toNearestInt(), juce::Justification::centredLeft, 1);
            return;
        }

        const juce::String chord = juce::String::fromUTF8(snap_.harmonyChordLabel,
                                                          static_cast<int>(sizeof(snap_.harmonyChordLabel))).trim();
        const juce::String section = juce::String::fromUTF8(snap_.harmonySectionLabel,
                                                            static_cast<int>(sizeof(snap_.harmonySectionLabel))).trim();
        juce::String label = chord.isNotEmpty() ? chord : "N.C.";
        if (section.isNotEmpty())
            label += " " + juce::String::charToString(0x00b7) + " " + section;

        g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        g.setColour(suite::ui::accentCyan.brighter(0.25f));
        g.drawFittedText(label, inner.toNearestInt(), juce::Justification::centredLeft, 1);
    }

    juce::AudioProcessorValueTreeState& apvts_;
    std::function<void(int)> onPresetSelected_;
    bool suppressPresetCallback_ = false;
    int cachedModMode_ = -1;

    // Row 1
    juce::Label presetLabel_;
    juce::ComboBox presetCombo_;

    juce::Label tractLabel_;
    juce::ComboBox tractCombo_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tractAtt_;

    juce::Label modLabel_;
    std::array<juce::TextButton, 4> modButtons_;

    // Row 2 - TextureLink
    juce::Label texBusLabel_;
    juce::ComboBox texBusCombo_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> texBusAtt_;

    juce::TextButton euclidBtn_;
    juce::TextButton airBreathBtn_;
    juce::TextButton transJawBtn_;
    juce::TextButton freezeSweepBtn_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> euclidAtt_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> airBreathAtt_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> transJawAtt_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeSweepAtt_;

    // Row 2 - HarmonyLink
    juce::Label harmBusLabel_;
    juce::ComboBox harmBusCombo_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> harmBusAtt_;

    juce::TextButton sectionLiftBtn_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> sectionLiftAtt_;

    juce::Rectangle<int> dividerBounds_;
    juce::Rectangle<int> fossilStatusBounds_;
    juce::Rectangle<int> harmonyStatusBounds_;

    vowelmorph::TelemetrySnapshot snap_{};
};

} // namespace vowelmorph::ui
