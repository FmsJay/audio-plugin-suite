#pragma once

#include "SessionMateLookAndFeel.h"
#include "../SessionMateTypes.h"
#include "../SessionMateUiBridge.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace sessionmate::ui {

class ArrangerAndChordStrip : public juce::Component,
                              private juce::TextEditor::Listener {
public:
    ArrangerAndChordStrip(juce::AudioProcessorValueTreeState& apvtsRef,
                          ISessionMateUiBridge& bridgeRef)
        : apvts(apvtsRef), bridge(bridgeRef) {
        // Row 1: 7 Song Arranger section buttons
        for (int i = 0; i < kNumSections; ++i) {
            auto& btn = sectionButtons[static_cast<size_t>(i)];
            btn.setButtonText(sectionName(static_cast<SongSection>(i)));
            btn.setClickingTogglesState(false);
            btn.setColour(juce::TextButton::buttonColourId, Palette::bgControl);
            btn.setColour(juce::TextButton::buttonOnColourId, Palette::accentCyan);
            btn.setColour(juce::TextButton::textColourOffId, Palette::textMuted);
            btn.setColour(juce::TextButton::textColourOnId, Palette::bgDark);
            btn.onClick = [this, i]() { setSectionParam(i); };
            addAndMakeVisible(btn);
        }

        if (auto* secParam = apvts.getParameter(ParamID::section)) {
            sectionAttachment = std::make_unique<juce::ParameterAttachment>(
                *secParam,
                [this](float newValue) {
                    const int idx = std::clamp(juce::roundToInt(newValue), 0, kNumSections - 1);
                    selectedSectionIndex = idx;
                    syncSectionButtons();
                },
                nullptr);
            sectionAttachment->sendInitialUpdate();
        }

        // [+Fill] trigger button
        fillBtn.setButtonText("+Fill");
        fillBtn.setColour(juce::TextButton::buttonColourId, Palette::bgControl);
        fillBtn.setColour(juce::TextButton::buttonOnColourId, Palette::accentAmber);
        fillBtn.setColour(juce::TextButton::textColourOffId, Palette::accentAmber);
        fillBtn.setColour(juce::TextButton::textColourOnId, Palette::bgDark);
        fillBtn.setTooltip("Trigger a musical drum/band fill now");
        fillBtn.onClick = [this]() { bridge.triggerFillNow(); };
        addAndMakeVisible(fillBtn);

        // [Auto-Sec] toggle
        autoSecBtn.setButtonText("Auto-Sec");
        autoSecBtn.setColour(juce::ToggleButton::tickColourId, Palette::accentViolet);
        autoSecBtn.setTooltip("Automatically transition song sections from your playing intensity");
        addAndMakeVisible(autoSecBtn);
        autoSecAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, ParamID::autoSection, autoSecBtn);

        // [1/2x] & [2x] feel toggles
        configureFeelToggle(halfTimeBtn, "1/2x", Palette::accentAmber, "Half-time groove feel");
        configureFeelToggle(doubleTimeBtn, "2x", Palette::accentCoral, "Double-time groove feel");
        addAndMakeVisible(halfTimeBtn);
        addAndMakeVisible(doubleTimeBtn);
        halfTimeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, ParamID::halfTime, halfTimeBtn);
        doubleTimeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, ParamID::doubleTime, doubleTimeBtn);

        // Row 2: Chord Track inline editor, Presets menu, Reharm pill & controls
        presetsBtn.setButtonText(juce::String::fromUTF8("Presets \xE2\x96\xBE"));
        presetsBtn.setColour(juce::TextButton::buttonColourId, Palette::bgControl);
        presetsBtn.setColour(juce::TextButton::textColourOffId, Palette::accentCyan);
        presetsBtn.setTooltip("Load a quick-start chord progression preset");
        presetsBtn.onClick = [this]() { openPresetsMenu(); };
        addAndMakeVisible(presetsBtn);

        reharmPillBtn.setButtonText(juce::String::fromUTF8("REHARM \xC3\x97"));
        reharmPillBtn.setColour(juce::TextButton::buttonColourId, Palette::accentAmber.withAlpha(0.24f));
        reharmPillBtn.setColour(juce::TextButton::buttonOnColourId, Palette::accentAmber);
        reharmPillBtn.setColour(juce::TextButton::textColourOffId, Palette::accentAmber.brighter(0.35f));
        reharmPillBtn.setColour(juce::TextButton::textColourOnId, Palette::bgDark);
        reharmPillBtn.setTooltip("Active HarmonyHUD reharmonization override — click to clear");
        reharmPillBtn.onClick = [this]() { bridge.clearActiveReharm(); };
        reharmPillBtn.setVisible(false);
        addChildComponent(reharmPillBtn);

        editChartBtn.setButtonText("Edit");
        editChartBtn.setColour(juce::TextButton::buttonColourId, Palette::bgControl);
        editChartBtn.setColour(juce::TextButton::buttonOnColourId, Palette::accentCyan);
        editChartBtn.setColour(juce::TextButton::textColourOffId, Palette::textMuted);
        editChartBtn.setColour(juce::TextButton::textColourOnId, Palette::bgDark);
        editChartBtn.setTooltip("Edit chord progression (e.g. | Em9 | A13 | Dmaj7 | B7#9 |)");
        editChartBtn.onClick = [this]() {
            if (isEditingChart) {
                commitChordEdit();
            } else {
                beginChordEdit();
            }
        };
        addAndMakeVisible(editChartBtn);

        chordEditor.setMultiLine(false);
        chordEditor.setReturnKeyStartsNewLine(false);
        chordEditor.setFont(Palette::font(12.5f, true));
        chordEditor.setTextToShowWhenEmpty("| Em9 | A13 | Dmaj7 | B7#9 |", Palette::textMuted.withAlpha(0.5f));
        chordEditor.addListener(this);
        chordEditor.setVisible(false);
        addChildComponent(chordEditor);

        // [Auto-Detect] chord toggle
        autoChordBtn.setButtonText("Auto-Detect");
        autoChordBtn.setColour(juce::ToggleButton::tickColourId, Palette::accentEmerald);
        autoChordBtn.setTooltip("Listen to incoming audio/MIDI and detect chords in real time");
        addAndMakeVisible(autoChordBtn);
        autoChordAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, ParamID::autoChord, autoChordBtn);

        refreshChordBars();
    }

    ~ArrangerAndChordStrip() override {
        chordEditor.removeListener(this);
    }

    void updateLiveState(const UiSnapshot& snap) {
        const int liveSec = std::clamp(static_cast<int>(snap.activeSection), 0, kNumSections - 1);
        if (liveSec != activeLiveSection) {
            activeLiveSection = liveSec;
            syncSectionButtons();
        }

        if (fillActive != snap.fillActive) {
            fillActive = snap.fillActive;
            fillBtn.setToggleState(fillActive, juce::dontSendNotification);
        }

        activeBarIndex = snap.activeChordBarIndex;
        detectedChordLabel = bridge.formatChordSymbol(snap.listener.detectedChord);
        chordConfidence = snap.listener.chordConfidence;
        nextChordLabel = snap.nextChord.valid ? bridge.formatChordSymbol(snap.nextChord) : std::string();

        const bool wasReharmVisible = reharmPillBtn.isVisible();
        reharmActive = snap.reharmActive;
        if (reharmActive) {
            const auto reharmStr = bridge.formatChordSymbol(snap.reharmChord);
            const juce::String desiredText =
                "REHARM: " + juce::String(reharmStr.empty() ? "---" : reharmStr) + juce::String::fromUTF8(" \xC3\x97");
            if (reharmPillBtn.getButtonText() != desiredText) {
                reharmPillBtn.setButtonText(desiredText);
            }
        }
        reharmPillBtn.setVisible(reharmActive);
        if (wasReharmVisible != reharmActive) {
            resized();
        }

        const bool autoOn = autoChordBtn.getToggleState();
        const juce::String desiredAutoText = autoOn ? "Auto-Detect ON" : "Auto-Detect";
        if (autoChordBtn.getButtonText() != desiredAutoText) {
            autoChordBtn.setButtonText(desiredAutoText);
        }

        if (!isEditingChart) {
            const auto currentText = bridge.getChordChartText();
            if (currentText != lastChartText) {
                lastChartText = currentText;
                barLabels = bridge.getChordBarLabels();
            }
        }

        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(Palette::bgPanel);
        g.fillRoundedRectangle(bounds, 8.0f);
        g.setColour(Palette::border);
        g.drawRoundedRectangle(bounds, 8.0f, 1.0f);

        // Subtle divider between Row 1 (Arranger) and Row 2 (Chord Track)
        const float midY = bounds.getCentreY();
        g.setColour(Palette::border.withAlpha(0.55f));
        g.drawHorizontalLine(juce::roundToInt(midY), bounds.getX() + 8.0f, bounds.getRight() - 8.0f);

        // Row labels
        g.setColour(Palette::textMuted);
        g.setFont(Palette::font(10.0f, true));
        g.drawFittedText("SONG ARRANGER", row1LabelArea, juce::Justification::centredLeft, 1);
        g.drawFittedText("CHORD TRACK", row2LabelArea, juce::Justification::centredLeft, 1);

        // Visual Chord Bar Cells (when not inline-editing)
        if (!isEditingChart && !chordCellsArea.isEmpty()) {
            drawChordCells(g, chordCellsArea.toFloat());
        }

        // Glowing amber aura behind active Reharm pill button
        if (reharmActive && reharmPillBtn.isVisible()) {
            const auto rb = reharmPillBtn.getBounds().toFloat();
            g.setColour(Palette::accentAmber.withAlpha(0.24f));
            g.fillRoundedRectangle(rb.expanded(2.0f), 7.0f);
            g.setColour(Palette::accentAmber.withAlpha(0.88f));
            g.drawRoundedRectangle(rb, 6.0f, 1.3f);
        }

        // Next-Chord Lookahead mini-badge
        if (!nextChordBadgeArea.isEmpty()) {
            drawNextChordBadge(g, nextChordBadgeArea.toFloat());
        }

        // Live detected chord badge (when Auto-Detect ON)
        if (!detectedBadgeArea.isEmpty()) {
            drawDetectedChordBadge(g, detectedBadgeArea.toFloat());
        }
    }

    void resized() override {
        auto r = getLocalBounds().reduced(10, 6);
        const int rowH = (r.getHeight() - 8) / 2;

        // Row 1: SONG ARRANGER
        auto row1 = r.removeFromTop(rowH);
        row1LabelArea = row1.removeFromLeft(96);
        row1.removeFromLeft(6);

        // Right-side controls on Row 1: [Auto-Sec] [1/2x] [2x]
        auto doubleArea = row1.removeFromRight(42);
        row1.removeFromRight(4);
        auto halfArea = row1.removeFromRight(42);
        row1.removeFromRight(6);
        auto autoSecArea = row1.removeFromRight(84);
        row1.removeFromRight(8);
        auto fillArea = row1.removeFromRight(58);
        row1.removeFromRight(8);

        doubleTimeBtn.setBounds(doubleArea);
        halfTimeBtn.setBounds(halfArea);
        autoSecBtn.setBounds(autoSecArea);
        fillBtn.setBounds(fillArea);

        // 7 Section buttons fill the remaining Row 1 space evenly
        const int secGap = 4;
        const int secW = std::max(40, (row1.getWidth() - secGap * (kNumSections - 1)) / kNumSections);
        for (int i = 0; i < kNumSections; ++i) {
            auto slot = (i == kNumSections - 1) ? row1 : row1.removeFromLeft(secW);
            sectionButtons[static_cast<size_t>(i)].setBounds(slot);
            if (i < kNumSections - 1) row1.removeFromLeft(secGap);
        }

        r.removeFromTop(8);

        // Row 2: CHORD TRACK
        auto row2 = r;
        row2LabelArea = row2.removeFromLeft(96);
        row2.removeFromLeft(6);

        detectedBadgeArea = row2.removeFromRight(78);
        row2.removeFromRight(5);
        autoChordBtn.setBounds(row2.removeFromRight(108));
        row2.removeFromRight(6);
        editChartBtn.setBounds(row2.removeFromRight(46));
        row2.removeFromRight(5);
        presetsBtn.setBounds(row2.removeFromRight(76));
        row2.removeFromRight(6);
        nextChordBadgeArea = row2.removeFromRight(86);
        row2.removeFromRight(6);

        if (reharmActive) {
            reharmPillBtn.setBounds(row2.removeFromRight(136));
            row2.removeFromRight(6);
        } else {
            reharmPillBtn.setBounds({});
        }

        chordCellsArea = row2;
        chordEditor.setBounds(chordCellsArea);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (!isEditingChart && chordCellsArea.contains(e.getPosition())) {
            beginChordEdit();
        }
    }

private:
    static void configureFeelToggle(juce::TextButton& b, const juce::String& text,
                                    juce::Colour onColour, const juce::String& tooltip) {
        b.setButtonText(text);
        b.setClickingTogglesState(true);
        b.setColour(juce::TextButton::buttonColourId, Palette::bgControl);
        b.setColour(juce::TextButton::buttonOnColourId, onColour);
        b.setColour(juce::TextButton::textColourOffId, Palette::textMuted);
        b.setColour(juce::TextButton::textColourOnId, Palette::bgDark);
        b.setTooltip(tooltip);
    }

    void setSectionParam(int idx) {
        if (sectionAttachment != nullptr) {
            sectionAttachment->setValueAsCompleteGesture(static_cast<float>(idx));
        } else if (auto* p = apvts.getParameter(ParamID::section)) {
            p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(idx)));
        }
    }

    void syncSectionButtons() {
        const int highlighted = autoSecBtn.getToggleState() ? activeLiveSection : selectedSectionIndex;
        for (int i = 0; i < kNumSections; ++i) {
            sectionButtons[static_cast<size_t>(i)].setToggleState(i == highlighted, juce::dontSendNotification);
        }
    }

    void refreshChordBars() {
        lastChartText = bridge.getChordChartText();
        barLabels = bridge.getChordBarLabels();
    }

    void openPresetsMenu() {
        const auto presets = bridge.getChordProgressionPresets();
        if (presets.empty()) return;

        juce::PopupMenu menu;
        menu.addSectionHeader("Quick-Start Chord Progressions");
        for (size_t i = 0; i < presets.size(); ++i) {
            menu.addItem(static_cast<int>(i + 1), juce::String(presets[i].first));
        }

        juce::Component::SafePointer<ArrangerAndChordStrip> safeThis(this);
        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(&presetsBtn),
            [safeThis, presets](int chosenId) {
                if (safeThis == nullptr || chosenId <= 0) return;
                const size_t idx = static_cast<size_t>(chosenId - 1);
                if (idx >= presets.size()) return;
                if (safeThis->isEditingChart) {
                    safeThis->cancelChordEdit();
                }
                safeThis->bridge.setChordChartText(presets[idx].second);
                safeThis->refreshChordBars();
                safeThis->repaint();
            });
    }

    void beginChordEdit() {
        isEditingChart = true;
        editChartBtn.setToggleState(true, juce::dontSendNotification);
        editChartBtn.setButtonText("Done");
        chordEditor.setText(bridge.getChordChartText(), juce::dontSendNotification);
        chordEditor.setVisible(true);
        chordEditor.grabKeyboardFocus();
        chordEditor.selectAll();
        repaint();
    }

    void commitChordEdit() {
        if (!isEditingChart) return;
        isEditingChart = false;
        editChartBtn.setToggleState(false, juce::dontSendNotification);
        editChartBtn.setButtonText("Edit");
        const auto text = chordEditor.getText().trim().toStdString();
        chordEditor.setVisible(false);
        if (!text.empty()) {
            bridge.setChordChartText(text);
        }
        refreshChordBars();
        repaint();
    }

    void cancelChordEdit() {
        if (!isEditingChart) return;
        isEditingChart = false;
        editChartBtn.setToggleState(false, juce::dontSendNotification);
        editChartBtn.setButtonText("Edit");
        chordEditor.setVisible(false);
        repaint();
    }

    void textEditorReturnKeyPressed(juce::TextEditor&) override { commitChordEdit(); }
    void textEditorEscapeKeyPressed(juce::TextEditor&) override { cancelChordEdit(); }
    void textEditorFocusLost(juce::TextEditor&) override { commitChordEdit(); }

    void drawChordCells(juce::Graphics& g, juce::Rectangle<float> area) const {
        g.setColour(Palette::bgControl);
        g.fillRoundedRectangle(area, 5.0f);
        g.setColour(Palette::border);
        g.drawRoundedRectangle(area, 5.0f, 1.0f);

        const int numBars = static_cast<int>(barLabels.size());
        if (numBars <= 0) {
            g.setColour(Palette::textMuted);
            g.setFont(Palette::font(11.5f, false));
            g.drawFittedText("Click to enter chord progression (e.g. | Em9 | A13 | Dmaj7 | B7#9 |)",
                             area.toNearestInt().reduced(8, 2), juce::Justification::centredLeft, 1);
            return;
        }

        // Display up to 8 bars per page so cells stay crisp and legible
        constexpr int maxVisible = 8;
        const int visibleCount = std::min(numBars, maxVisible);
        const int wrappedActive = (activeBarIndex >= 0) ? (activeBarIndex % numBars) : 0;
        const int pageStart = (wrappedActive / maxVisible) * maxVisible;
        const int countOnPage = std::min(visibleCount, numBars - pageStart);
        const float cellW = area.getWidth() / static_cast<float>(std::max(1, countOnPage));

        for (int i = 0; i < countOnPage; ++i) {
            const int barIdx = pageStart + i;
            const auto cell = juce::Rectangle<float>(area.getX() + i * cellW, area.getY(), cellW, area.getHeight());
            const bool isActive = (barIdx == wrappedActive);

            if (isActive) {
                g.setColour(Palette::accentCyan.withAlpha(0.18f));
                g.fillRoundedRectangle(cell.reduced(1.5f), 4.0f);
                g.setColour(Palette::accentCyan.withAlpha(0.90f));
                g.drawRoundedRectangle(cell.reduced(1.5f), 4.0f, 1.3f);
            }

            if (i > 0) {
                g.setColour(Palette::border.withAlpha(0.85f));
                g.drawVerticalLine(juce::roundToInt(cell.getX()), area.getY() + 4.0f, area.getBottom() - 4.0f);
            }

            g.setColour(isActive ? Palette::accentCyan.brighter(0.25f) : Palette::textPrimary);
            g.setFont(Palette::font(12.5f, isActive));
            g.drawFittedText(barLabels[static_cast<size_t>(barIdx)],
                             cell.toNearestInt().reduced(4, 2),
                             juce::Justification::centred, 1);
        }
    }

    void drawNextChordBadge(juce::Graphics& g, juce::Rectangle<float> area) const {
        const bool hasNext = !nextChordLabel.empty();
        g.setColour(Palette::bgControl.withAlpha(0.85f));
        g.fillRoundedRectangle(area, 5.0f);
        g.setColour(hasNext ? Palette::accentViolet.withAlpha(0.55f) : Palette::border.withAlpha(0.55f));
        g.drawRoundedRectangle(area, 5.0f, 1.0f);

        const juce::String text = hasNext ? ("Next: " + juce::String(nextChordLabel)) : "Next: ---";
        g.setColour(hasNext ? Palette::accentViolet.brighter(0.25f) : Palette::textMuted.withAlpha(0.65f));
        g.setFont(Palette::font(11.0f, hasNext));
        g.drawFittedText(text, area.toNearestInt().reduced(5, 2), juce::Justification::centred, 1);
    }

    void drawDetectedChordBadge(juce::Graphics& g, juce::Rectangle<float> area) const {
        const bool active = autoChordBtn.getToggleState();
        g.setColour(active ? Palette::accentEmerald.withAlpha(0.14f) : Palette::bgControl.withAlpha(0.6f));
        g.fillRoundedRectangle(area, 5.0f);
        g.setColour(active ? Palette::accentEmerald.withAlpha(0.75f) : Palette::border.withAlpha(0.55f));
        g.drawRoundedRectangle(area, 5.0f, 1.0f);

        const juce::String text = active
            ? (detectedChordLabel.empty() ? "---" : juce::String(detectedChordLabel))
            : "Chart";
        g.setColour(active ? Palette::accentEmerald.brighter(0.25f) : Palette::textMuted.withAlpha(0.65f));
        g.setFont(Palette::font(11.5f, active));
        g.drawFittedText(text, area.toNearestInt().reduced(4, 2), juce::Justification::centred, 1);

        if (active && chordConfidence > 0.02f) {
            const float barW = (area.getWidth() - 10.0f) * std::clamp(chordConfidence, 0.0f, 1.0f);
            g.setColour(Palette::accentEmerald);
            g.fillRoundedRectangle(area.getX() + 5.0f, area.getBottom() - 3.5f, barW, 2.0f, 1.0f);
        }
    }

    juce::AudioProcessorValueTreeState& apvts;
    ISessionMateUiBridge& bridge;

    std::array<juce::TextButton, kNumSections> sectionButtons;
    juce::TextButton fillBtn;
    juce::ToggleButton autoSecBtn;
    juce::TextButton halfTimeBtn;
    juce::TextButton doubleTimeBtn;

    juce::TextButton presetsBtn;
    juce::TextButton reharmPillBtn;
    juce::TextButton editChartBtn;
    juce::TextEditor chordEditor;
    juce::ToggleButton autoChordBtn;

    std::unique_ptr<juce::ParameterAttachment> sectionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoSecAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> halfTimeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> doubleTimeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoChordAttachment;

    juce::Rectangle<int> row1LabelArea;
    juce::Rectangle<int> row2LabelArea;
    juce::Rectangle<int> chordCellsArea;
    juce::Rectangle<int> nextChordBadgeArea;
    juce::Rectangle<int> detectedBadgeArea;

    int selectedSectionIndex = 3;
    int activeLiveSection = 3;
    int activeBarIndex = 0;
    bool fillActive = false;
    bool reharmActive = false;
    bool isEditingChart = false;
    float chordConfidence = 0.0f;
    std::string lastChartText;
    std::string detectedChordLabel;
    std::string nextChordLabel;
    std::vector<std::string> barLabels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArrangerAndChordStrip)
};

} // namespace sessionmate::ui
