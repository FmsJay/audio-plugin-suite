#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../dsp/MusicTheory.h"
#include "HudPalette.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace harmonyhud::ui {

class ChordRibbonComponent : public juce::Component,
                             private juce::Timer {
public:
    std::function<void(const SubstitutionSuggestion&)> onAuditionSubstitution;
    std::function<void(const SubstitutionSuggestion&, ReharmPushTiming)> onPushSubToBand;
    std::function<void(const ChordHistoryEntry&)> onAuditionHistory;
    std::function<void(size_t, const ChordSymbol&)> onEditHistoryChord;
    std::function<void(size_t)> onDeleteHistoryChord;
    std::function<void(const ChordSymbol&)> onAppendChord;
    std::function<void()> onSendChartToBand;
    std::function<void()> onPullBandChart;
    std::function<void()> onClearHistory;

    ChordRibbonComponent() {
        timingPill_ = std::make_unique<TimingSelectorPill>();
        timingPill_->onChanged = [this](ReharmPushTiming t) { pushTiming_ = t; };
        addAndMakeVisible(*timingPill_);

        for (size_t i = 0; i < subChips_.size(); ++i) {
            subChips_[i] = std::make_unique<SubChipButton>();
            subChips_[i]->onAudition = [this, i] {
                if (onAuditionSubstitution && snapshot_.substitutions[i].chord.isValid())
                    onAuditionSubstitution(snapshot_.substitutions[i]);
            };
            subChips_[i]->onPushToBand = [this, i] {
                if (onPushSubToBand && snapshot_.substitutions[i].chord.isValid()) {
                    onPushSubToBand(snapshot_.substitutions[i], pushTiming_);
                    showTransientHint("Pushed " + juce::String(snapshot_.substitutions[i].label) + " -> Band");
                }
            };
            addAndMakeVisible(*subChips_[i]);
        }

        sendBandBtn_.setButtonText("Send -> Band");
        sendBandBtn_.onClick = [this] {
            if (onSendChartToBand) {
                onSendChartToBand();
                showTransientHint("Sent Chart -> Band");
            }
        };
        addAndMakeVisible(sendBandBtn_);

        pullBandBtn_.setButtonText("Pull Band");
        pullBandBtn_.onClick = [this] {
            if (onPullBandChart) {
                onPullBandChart();
                showTransientHint("Pulled Band Chart");
            }
        };
        addAndMakeVisible(pullBandBtn_);

        midiDragBtn_ = std::make_unique<MidiDragButton>();
        midiDragBtn_->onPrepareMidiFile = [this] { return exportTempMidiFile(); };
        midiDragBtn_->onFeedback = [this](const juce::String& msg) { showTransientHint(msg); };
        addAndMakeVisible(*midiDragBtn_);

        copyChartBtn_.setButtonText("Copy Chart");
        copyChartBtn_.onClick = [this] {
            auto entries = getExportHistory();
            const auto chart = buildChordChartText(entries, snapshot_.currentKey);
            juce::SystemClipboard::copyTextToClipboard(juce::String(chart));
            showTransientHint("Chart Copied!");
        };
        addAndMakeVisible(copyChartBtn_);

        clearBtn_.setButtonText("Clear");
        clearBtn_.onClick = [this] {
            if (onClearHistory)
                onClearHistory();
        };
        addAndMakeVisible(clearBtn_);

        historyStrip_ = std::make_unique<HistoryStrip>();
        historyStrip_->onEntryClicked = [this](const ChordHistoryEntry& entry) {
            if (onAuditionHistory)
                onAuditionHistory(entry);
        };
        historyStrip_->onEntryRightClicked = [this](size_t idx, juce::Point<int> screenPos) {
            showChordEditorMenu(static_cast<int>(idx), screenPos);
        };
        historyStrip_->onEntryDeleteClicked = [this](size_t idx) {
            if (onDeleteHistoryChord)
                onDeleteHistoryChord(idx);
        };
        historyStrip_->onAppendClicked = [this](juce::Point<int> screenPos) {
            showChordEditorMenu(-1, screenPos);
        };

        viewport_.setViewedComponent(historyStrip_.get(), false);
        viewport_.setScrollBarsShown(false, true, false, true);
        viewport_.setScrollBarThickness(6);
        addAndMakeVisible(viewport_);
    }

    ~ChordRibbonComponent() override {
        stopTimer();
    }

    void updateSnapshot(const AnalysisSnapshot& snap, double bpm) {
        const size_t prevCount = snapshot_.history.size();
        const bool prevBanner = hasLookaheadBanner();
        snapshot_ = snap;
        currentBpm_ = (bpm > 10.0) ? bpm : 120.0;

        for (size_t i = 0; i < subChips_.size(); ++i)
            subChips_[i]->setSuggestion(snapshot_.substitutions[i], snapshot_.bandLink.connected);

        sendBandBtn_.setEnabled(snapshot_.bandLink.connected);
        pullBandBtn_.setEnabled(snapshot_.bandLink.connected);

        const bool flats = snapshot_.currentKey.preferFlats();
        historyStrip_->setHistory(snapshot_.history, flats, snapshot_.bandLink.activeChartBarIndex);

        if (hasLookaheadBanner() != prevBanner)
            resized();
        else
            layoutHistoryStrip(snapshot_.history.size() > prevCount);

        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto bounds = getLocalBounds().toFloat();
        palette::drawPanelCard(g, bounds, 8.0f, palette::bgPanel, palette::border);

        const auto inner = bounds.reduced(10.0f, 6.0f);
        float y = inner.getY();

        // Optional Next-Chord Lookahead Banner when linked to SessionMate
        if (hasLookaheadBanner()) {
            const auto banner = juce::Rectangle<float>(inner.getX(), y, inner.getWidth(), 20.0f);
            drawLookaheadBanner(g, banner);
            y += 24.0f;
        }

        // Row 1 label: "REHARM"
        g.setFont(palette::makeFont(10.0f, true));
        g.setColour(palette::textMuted);
        g.drawText("REHARM", juce::Rectangle<float>(inner.getX(), y, 54.0f, 16.0f),
                   juce::Justification::centredLeft, false);

        // Divider above history ribbon
        const float divY = y + 40.0f;
        g.setColour(palette::border.withAlpha(0.65f));
        g.drawHorizontalLine(static_cast<int>(std::round(divY)), inner.getX(), inner.getRight());

        // Transient status feedback toast
        if (hintText_.isNotEmpty()) {
            const auto toastBounds = juce::Rectangle<float>(inner.getRight() - 178.0f,
                                                            divY + 3.0f, 178.0f, 17.0f);
            palette::drawStatusPill(g, toastBounds, hintText_, palette::accentEmerald, 9.5f);
        }
    }

    void resized() override {
        auto area = getLocalBounds().reduced(10, 6);
        if (hasLookaheadBanner())
            area.removeFromTop(24);

        // Row 1: Reharm header + timing pill (left) + 5 Categorized Substitution Chips
        auto reharmRow = area.removeFromTop(38);
        auto leftCol = reharmRow.removeFromLeft(116);
        leftCol.removeFromTop(16); // Space for "REHARM" title painted above
        timingPill_->setBounds(leftCol.reduced(0, 1));

        reharmRow.removeFromLeft(6);
        const int chipGap = 5;
        const int chipW = juce::jmax(56, (reharmRow.getWidth() - chipGap * 4) / 5);
        for (size_t i = 0; i < subChips_.size(); ++i) {
            subChips_[i]->setBounds(reharmRow.removeFromLeft(chipW));
            if (i < 4) reharmRow.removeFromLeft(chipGap);
        }

        area.removeFromTop(6);

        // Row 2: Scrollable Chord History Ribbon + Right-aligned Action Buttons
        auto bottomRow = area;
        const int actionsW = juce::jmin(410, bottomRow.getWidth() / 2);
        auto actions = bottomRow.removeFromRight(actionsW);
        bottomRow.removeFromRight(8);

        viewport_.setBounds(bottomRow);
        layoutHistoryStrip(false);

        // Pack 5 action buttons neatly in `actions`
        const int gap = 4;
        const int btnH = juce::jmin(26, actions.getHeight());
        auto btnRow = actions.withSizeKeepingCentre(actions.getWidth(), btnH);

        const int clearW = 46;
        const int copyW = 74;
        const int midiW = 78;
        const int pullW = 68;
        clearBtn_.setBounds(btnRow.removeFromRight(clearW));
        btnRow.removeFromRight(gap);
        copyChartBtn_.setBounds(btnRow.removeFromRight(copyW));
        btnRow.removeFromRight(gap);
        midiDragBtn_->setBounds(btnRow.removeFromRight(midiW));
        btnRow.removeFromRight(gap);
        pullBandBtn_.setBounds(btnRow.removeFromRight(pullW));
        btnRow.removeFromRight(gap);
        sendBandBtn_.setBounds(btnRow);
    }

private:
    class TimingSelectorPill : public juce::Component {
    public:
        std::function<void(ReharmPushTiming)> onChanged;

        TimingSelectorPill() {
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        }

        void paint(juce::Graphics& g) override {
            const auto b = getLocalBounds().toFloat().reduced(0.5f);
            g.setColour(palette::bgDark);
            g.fillRoundedRectangle(b, 6.0f);
            g.setColour(palette::border);
            g.drawRoundedRectangle(b, 6.0f, 1.0f);

            static constexpr const char* kLabels[3] = { "Now", "Beat", "Bar" };
            const float segW = b.getWidth() / 3.0f;
            for (int i = 0; i < 3; ++i) {
                const auto seg = juce::Rectangle<float>(b.getX() + static_cast<float>(i) * segW,
                                                        b.getY(), segW, b.getHeight()).reduced(1.5f);
                const bool active = (static_cast<int>(timing_) == i);
                if (active) {
                    g.setColour(palette::accentCyan.withAlpha(0.24f));
                    g.fillRoundedRectangle(seg, 4.5f);
                    g.setColour(palette::accentCyan.withAlpha(0.7f));
                    g.drawRoundedRectangle(seg, 4.5f, 0.9f);
                }
                g.setFont(palette::makeFont(9.5f, active));
                g.setColour(active ? palette::textPrimary : palette::textMuted);
                g.drawText(kLabels[i], seg, juce::Justification::centred, false);
            }
        }

        void mouseDown(const juce::MouseEvent& e) override {
            const int idx = juce::jlimit(0, 2, static_cast<int>(e.position.x / (static_cast<float>(getWidth()) / 3.0f)));
            const auto next = static_cast<ReharmPushTiming>(idx);
            if (next != timing_) {
                timing_ = next;
                repaint();
                if (onChanged)
                    onChanged(timing_);
            }
        }

    private:
        ReharmPushTiming timing_ = ReharmPushTiming::NextBar;
    };

    class SubChipButton : public juce::Component {
    public:
        std::function<void()> onAudition;
        std::function<void()> onPushToBand;

        SubChipButton() {
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        }

        void setSuggestion(const SubstitutionSuggestion& s, bool bandConnected) {
            suggestion_ = s;
            bandConnected_ = bandConnected;
            repaint();
        }

        void paint(juce::Graphics& g) override {
            const auto b = getLocalBounds().toFloat().reduced(0.5f);
            const bool valid = suggestion_.chord.isValid();
            const float r = 6.5f;

            const auto catCol = categoryColour(suggestion_.category);
            auto bg = palette::bgCard;
            if (isDown_ && valid)
                bg = catCol.withAlpha(0.28f);
            else if (isHovered_ && valid)
                bg = catCol.withAlpha(0.15f);

            g.setColour(bg);
            g.fillRoundedRectangle(b, r);
            g.setColour((isHovered_ && valid) ? catCol.withAlpha(0.85f) : palette::border);
            g.drawRoundedRectangle(b, r, 1.0f);

            if (!valid) {
                g.setColour(palette::textMuted.withAlpha(0.5f));
                g.setFont(palette::makeFont(11.0f, false));
                g.drawText("--", b, juce::Justification::centred, false);
                return;
            }

            // Right-side [->] push-to-band mini button
            const auto pushRect = getPushRect(b);
            g.setColour(bandConnected_
                            ? (hoverPush_ ? palette::accentEmerald.withAlpha(0.35f) : palette::accentEmerald.withAlpha(0.16f))
                            : palette::bgDark.withAlpha(0.55f));
            g.fillRoundedRectangle(pushRect, 4.0f);
            g.setColour(bandConnected_ ? palette::accentEmerald.withAlpha(0.70f) : palette::border);
            g.drawRoundedRectangle(pushRect, 4.0f, 0.8f);
            g.setFont(palette::makeMonoFont(9.5f, true));
            g.setColour(bandConnected_ ? palette::accentEmerald : palette::textMuted.withAlpha(0.55f));
            g.drawText("->", pushRect, juce::Justification::centred, false);

            // Left content: category mini-tag + bold chord label
            const auto content = b.withRight(pushRect.getX() - 3.0f).reduced(5.0f, 2.0f);
            const auto topTag = content.withHeight(12.0f);
            const auto mainLbl = content.withTrimmedTop(11.0f);

            g.setFont(palette::makeFont(8.5f, true));
            g.setColour(catCol);
            g.drawText(shortCategoryTag(suggestion_.category), topTag, juce::Justification::centredLeft, true);

            g.setFont(palette::makeFont(12.5f, true));
            g.setColour(palette::textPrimary);
            g.drawText(suggestion_.label, mainLbl, juce::Justification::centredLeft, true);
        }

        void mouseMove(const juce::MouseEvent& e) override {
            const bool hp = getPushRect(getLocalBounds().toFloat().reduced(0.5f)).contains(e.position);
            if (hp != hoverPush_) {
                hoverPush_ = hp;
                repaint();
            }
        }

        void mouseEnter(const juce::MouseEvent&) override { isHovered_ = true; repaint(); }
        void mouseExit(const juce::MouseEvent&) override  { isHovered_ = false; hoverPush_ = false; isDown_ = false; repaint(); }

        void mouseDown(const juce::MouseEvent& e) override {
            isDown_ = true;
            repaint();
            const bool hitPush = getPushRect(getLocalBounds().toFloat().reduced(0.5f)).contains(e.position);
            if (hitPush || e.mods.isRightButtonDown() || e.mods.isShiftDown()) {
                if (onPushToBand) onPushToBand();
            } else {
                if (onAudition) onAudition();
            }
        }

        void mouseUp(const juce::MouseEvent&) override { isDown_ = false; repaint(); }

    private:
        static juce::Rectangle<float> getPushRect(juce::Rectangle<float> b) {
            return b.withLeft(b.getRight() - 23.0f).reduced(2.5f, 4.0f);
        }

        static juce::String shortCategoryTag(const std::string& cat) {
            if (cat == "Tritone Sub")  return "TRITONE";
            if (cat == "Sec. Dom")     return "SEC DOM";
            if (cat == "Modal Borrow") return "BORROW";
            if (cat == "Color / Slash")return "COLOR";
            return "FUNC";
        }

        static juce::Colour categoryColour(const std::string& cat) {
            if (cat == "Tritone Sub")   return palette::accentCoral;
            if (cat == "Sec. Dom")      return palette::accentAmber;
            if (cat == "Modal Borrow")  return palette::accentViolet;
            if (cat == "Color / Slash") return palette::accentEmerald;
            return palette::accentCyan;
        }

        SubstitutionSuggestion suggestion_;
        bool bandConnected_ = false;
        bool isHovered_ = false;
        bool hoverPush_ = false;
        bool isDown_ = false;
    };

    class MidiDragButton : public juce::Component {
    public:
        std::function<juce::File()> onPrepareMidiFile;
        std::function<void(const juce::String&)> onFeedback;

        MidiDragButton() {
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        }

        void paint(juce::Graphics& g) override {
            const auto b = getLocalBounds().toFloat().reduced(0.5f);
            const float r = juce::jmin(b.getHeight() * 0.5f, 12.0f);

            auto fill = palette::accentCyan.withAlpha(isDown_ ? 0.32f : (isHovered_ ? 0.24f : 0.16f));
            g.setColour(fill);
            g.fillRoundedRectangle(b, r);
            g.setColour(palette::accentCyan.withAlpha(isHovered_ ? 0.95f : 0.65f));
            g.drawRoundedRectangle(b, r, 1.1f);

            g.setColour(palette::textPrimary);
            g.setFont(palette::makeFont(10.5f, true));
            g.drawText("Drag MIDI", b.reduced(4.0f, 0.0f), juce::Justification::centred, true);
        }

        void mouseEnter(const juce::MouseEvent&) override { isHovered_ = true; repaint(); }
        void mouseExit(const juce::MouseEvent&) override  { isHovered_ = false; isDown_ = false; repaint(); }

        void mouseDown(const juce::MouseEvent&) override {
            isDown_ = true;
            dragStarted_ = false;
            repaint();
        }

        void mouseDrag(const juce::MouseEvent& e) override {
            if (dragStarted_ || e.getDistanceFromDragStart() <= 5)
                return;
            dragStarted_ = true;

            if (onPrepareMidiFile) {
                const auto file = onPrepareMidiFile();
                if (file.existsAsFile()) {
                    juce::DragAndDropContainer::performExternalDragDropOfFiles(
                        { file.getFullPathName() }, false);
                    if (onFeedback)
                        onFeedback("MIDI Ready for DAW!");
                }
            }
        }

        void mouseUp(const juce::MouseEvent&) override {
            isDown_ = false;
            if (!dragStarted_ && onPrepareMidiFile) {
                const auto file = onPrepareMidiFile();
                if (file.existsAsFile() && onFeedback)
                    onFeedback("Drag to DAW or Saved!");
            }
            dragStarted_ = false;
            repaint();
        }

    private:
        bool isHovered_ = false;
        bool isDown_ = false;
        bool dragStarted_ = false;
    };

    class HistoryStrip : public juce::Component {
    public:
        static constexpr int kItemW = 98;
        static constexpr int kItemGap = 5;
        static constexpr int kAddBtnW = 28;

        std::function<void(const ChordHistoryEntry&)> onEntryClicked;
        std::function<void(size_t, juce::Point<int>)> onEntryRightClicked;
        std::function<void(size_t)> onEntryDeleteClicked;
        std::function<void(juce::Point<int>)> onAppendClicked;

        HistoryStrip() {
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        }

        void setHistory(const std::vector<ChordHistoryEntry>& entries, bool preferFlats, int bandBarIdx) {
            entries_ = entries;
            preferFlats_ = preferFlats;
            bandBarIdx_ = bandBarIdx;
            repaint();
        }

        int getRequiredWidth(int minWidth) const {
            const int w = static_cast<int>(entries_.size()) * (kItemW + kItemGap) + kAddBtnW + 10;
            return juce::jmax(minWidth, w);
        }

        void paint(juce::Graphics& g) override {
            const auto bounds = getLocalBounds().toFloat();
            const float itemH = juce::jmax(22.0f, bounds.getHeight() - 4.0f);
            const float y = (bounds.getHeight() - itemH) * 0.5f;

            if (entries_.empty()) {
                const auto addR = juce::Rectangle<float>(0.0f, y, static_cast<float>(kAddBtnW), itemH);
                drawAddButton(g, addR, hoverAdd_);
                g.setColour(palette::textMuted.withAlpha(0.65f));
                g.setFont(palette::makeFont(11.0f, false));
                g.drawText("Play chords or click + to build chart (right-click to edit)...",
                           bounds.withTrimmedLeft(static_cast<float>(kAddBtnW) + 8.0f),
                           juce::Justification::centredLeft, true);
                return;
            }

            for (size_t i = 0; i < entries_.size(); ++i) {
                const auto& e = entries_[i];
                const bool isBandActive = (bandBarIdx_ >= 0 && static_cast<int>(i) == bandBarIdx_);
                const bool isLatest = (i + 1 == entries_.size());
                const bool isHover = (static_cast<int>(i) == hoveredIdx_);

                const float x = static_cast<float>(i) * static_cast<float>(kItemW + kItemGap);
                const juce::Rectangle<float> card(x, y, static_cast<float>(kItemW), itemH);

                if (isBandActive) {
                    g.setColour(palette::accentAmber.withAlpha(0.22f));
                    g.fillRoundedRectangle(card, 6.0f);
                    g.setColour(palette::accentAmber.withAlpha(0.90f));
                    g.drawRoundedRectangle(card.reduced(0.5f), 6.0f, 1.4f);
                } else if (isLatest) {
                    g.setColour(palette::accentCyan.withAlpha(isHover ? 0.28f : 0.18f));
                    g.fillRoundedRectangle(card, 6.0f);
                    g.setColour(palette::accentCyan.withAlpha(0.85f));
                    g.drawRoundedRectangle(card.reduced(0.5f), 6.0f, 1.3f);
                } else {
                    g.setColour(isHover ? palette::bgCard.brighter(0.12f) : palette::bgCard);
                    g.fillRoundedRectangle(card, 6.0f);
                    g.setColour(isHover ? palette::accentCyan.withAlpha(0.55f) : palette::border);
                    g.drawRoundedRectangle(card.reduced(0.5f), 6.0f, 1.0f);
                }

                juce::String label = e.chord.name(preferFlats_);
                if (!e.roman.empty() && e.roman != "-")
                    label += " (" + juce::String(e.roman) + ")";

                const auto textRect = isHover ? card.withTrimmedRight(16.0f).reduced(5.0f, 0.0f)
                                              : card.reduced(5.0f, 0.0f);
                g.setColour(palette::textPrimary);
                g.setFont(palette::makeFont(11.0f, isLatest || isBandActive));
                g.drawText(label, textRect, juce::Justification::centred, true);

                // Subtle 'x' delete badge on hover
                if (isHover) {
                    const auto delR = getDeleteRect(card);
                    g.setColour(hoverDelete_ ? palette::accentCoral.withAlpha(0.30f) : palette::bgDark.withAlpha(0.55f));
                    g.fillRoundedRectangle(delR, 3.0f);
                    g.setColour(hoverDelete_ ? palette::accentCoral : palette::textMuted);
                    g.setFont(palette::makeFont(10.0f, true));
                    g.drawText("x", delR, juce::Justification::centred, false);
                }
            }

            // Trailing [+] append chord button
            const float addX = static_cast<float>(entries_.size()) * static_cast<float>(kItemW + kItemGap);
            drawAddButton(g, juce::Rectangle<float>(addX, y, static_cast<float>(kAddBtnW), itemH), hoverAdd_);
        }

        void mouseMove(const juce::MouseEvent& e) override {
            const float itemH = juce::jmax(22.0f, static_cast<float>(getHeight()) - 4.0f);
            const float y = (static_cast<float>(getHeight()) - itemH) * 0.5f;
            const float addX = static_cast<float>(entries_.size()) * static_cast<float>(kItemW + kItemGap);
            const bool ha = juce::Rectangle<float>(addX, y, static_cast<float>(kAddBtnW), itemH).contains(e.position);

            const int idx = hitTestEntry(e.position.x);
            bool hd = false;
            if (idx >= 0) {
                const float x = static_cast<float>(idx) * static_cast<float>(kItemW + kItemGap);
                hd = getDeleteRect(juce::Rectangle<float>(x, y, static_cast<float>(kItemW), itemH)).contains(e.position);
            }

            if (idx != hoveredIdx_ || ha != hoverAdd_ || hd != hoverDelete_) {
                hoveredIdx_ = idx;
                hoverAdd_ = ha;
                hoverDelete_ = hd;
                repaint();
            }
        }

        void mouseExit(const juce::MouseEvent&) override {
            if (hoveredIdx_ != -1 || hoverAdd_ || hoverDelete_) {
                hoveredIdx_ = -1;
                hoverAdd_ = false;
                hoverDelete_ = false;
                repaint();
            }
        }

        void mouseDown(const juce::MouseEvent& e) override {
            const float itemH = juce::jmax(22.0f, static_cast<float>(getHeight()) - 4.0f);
            const float y = (static_cast<float>(getHeight()) - itemH) * 0.5f;
            const float addX = static_cast<float>(entries_.size()) * static_cast<float>(kItemW + kItemGap);
            if (juce::Rectangle<float>(addX, y, static_cast<float>(kAddBtnW), itemH).contains(e.position)) {
                if (onAppendClicked)
                    onAppendClicked(e.getScreenPosition());
                return;
            }

            const int idx = hitTestEntry(e.position.x);
            if (idx >= 0 && static_cast<size_t>(idx) < entries_.size()) {
                if (e.mods.isRightButtonDown()) {
                    if (onEntryRightClicked)
                        onEntryRightClicked(static_cast<size_t>(idx), e.getScreenPosition());
                    return;
                }
                const float x = static_cast<float>(idx) * static_cast<float>(kItemW + kItemGap);
                if (getDeleteRect(juce::Rectangle<float>(x, y, static_cast<float>(kItemW), itemH)).contains(e.position)) {
                    if (onEntryDeleteClicked)
                        onEntryDeleteClicked(static_cast<size_t>(idx));
                    return;
                }
                if (onEntryClicked)
                    onEntryClicked(entries_[static_cast<size_t>(idx)]);
            } else if (e.mods.isRightButtonDown() && onAppendClicked) {
                onAppendClicked(e.getScreenPosition());
            }
        }

    private:
        static juce::Rectangle<float> getDeleteRect(juce::Rectangle<float> card) {
            return juce::Rectangle<float>(card.getRight() - 16.0f, card.getY() + 3.0f, 13.0f, card.getHeight() - 6.0f);
        }

        static void drawAddButton(juce::Graphics& g, juce::Rectangle<float> r, bool hover) {
            g.setColour(hover ? palette::accentCyan.withAlpha(0.24f) : palette::bgCard);
            g.fillRoundedRectangle(r, 6.0f);
            g.setColour(hover ? palette::accentCyan : palette::border);
            g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);
            g.setColour(hover ? palette::accentCyan : palette::textMuted);
            g.setFont(palette::makeFont(13.0f, true));
            g.drawText("+", r, juce::Justification::centred, false);
        }

        int hitTestEntry(float x) const {
            if (entries_.empty() || x < 0.0f)
                return -1;
            const int stride = kItemW + kItemGap;
            const int idx = static_cast<int>(x) / stride;
            const int rem = static_cast<int>(x) % stride;
            if (rem <= kItemW && idx >= 0 && static_cast<size_t>(idx) < entries_.size())
                return idx;
            return -1;
        }

        std::vector<ChordHistoryEntry> entries_;
        bool preferFlats_ = false;
        int bandBarIdx_ = -1;
        int hoveredIdx_ = -1;
        bool hoverAdd_ = false;
        bool hoverDelete_ = false;
    };

    bool hasLookaheadBanner() const {
        return snapshot_.bandLink.connected && snapshot_.bandLink.nextChord.isValid();
    }

    void drawLookaheadBanner(juce::Graphics& g, juce::Rectangle<float> area) const {
        const bool flats = snapshot_.currentKey.preferFlats();
        const auto& bl = snapshot_.bandLink;

        g.setColour(palette::accentCyan.withAlpha(0.11f));
        g.fillRoundedRectangle(area, 5.0f);
        g.setColour(palette::accentCyan.withAlpha(0.45f));
        g.drawRoundedRectangle(area.reduced(0.5f), 5.0f, 1.0f);

        juce::String leftTxt = "BAND: " + juce::String(bl.activeChord.name(flats))
                               + "  ->  NEXT: " + juce::String(bl.nextChord.name(flats))
                               + " in " + juce::String(bl.beatsUntilNextChord, 1) + " beats";

        juce::String rightTxt;
        if (!bl.sectionLabel.empty())
            rightTxt += juce::String(bl.sectionLabel) + " · ";
        rightTxt += "Bar " + juce::String(bl.currentBar);
        if (!bl.styleName.empty())
            rightTxt += " · " + juce::String(bl.styleName);

        const auto inner = area.reduced(8.0f, 0.0f);
        g.setFont(palette::makeFont(10.5f, true));
        g.setColour(palette::accentCyan);
        g.drawText(leftTxt, inner.withWidth(inner.getWidth() * 0.62f), juce::Justification::centredLeft, true);

        g.setFont(palette::makeFont(10.0f, false));
        g.setColour(palette::textMuted);
        g.drawText("[" + rightTxt + "]", inner.withTrimmedLeft(inner.getWidth() * 0.62f),
                   juce::Justification::centredRight, true);
    }

    void showChordEditorMenu(int editIndex, juce::Point<int> screenPos) {
        static constexpr ChordQuality kMenuQualities[] = {
            ChordQuality::Major, ChordQuality::Minor, ChordQuality::Dom7, ChordQuality::Maj7,
            ChordQuality::Min7, ChordQuality::Maj9, ChordQuality::Min9, ChordQuality::Dom9,
            ChordQuality::Dom13, ChordQuality::Min7b5, ChordQuality::Dim7, ChordQuality::Sus4,
            ChordQuality::Add9, ChordQuality::Min6, ChordQuality::Six
        };
        const bool flats = snapshot_.currentKey.preferFlats();

        juce::PopupMenu menu;
        menu.addSectionHeader(editIndex >= 0 ? "Edit Bar Chord" : "Append Chord");

        for (int root = 0; root < 12; ++root) {
            juce::PopupMenu sub;
            for (ChordQuality q : kMenuQualities) {
                ChordSymbol cs;
                cs.root = root;
                cs.quality = q;
                cs.confidence = 1.0f;
                sub.addItem(cs.name(flats), [this, editIndex, cs] {
                    if (editIndex >= 0) {
                        if (onEditHistoryChord)
                            onEditHistoryChord(static_cast<size_t>(editIndex), cs);
                    } else {
                        if (onAppendChord)
                            onAppendChord(cs);
                    }
                });
            }
            menu.addSubMenu(pitchClassName(root, flats), sub);
        }

        if (editIndex >= 0) {
            menu.addSeparator();
            menu.addItem("Delete Chord", [this, editIndex] {
                if (onDeleteHistoryChord)
                    onDeleteHistoryChord(static_cast<size_t>(editIndex));
            });
        }

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
            juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)));
    }

    std::vector<ChordHistoryEntry> getExportHistory() const {
        if (!snapshot_.history.empty())
            return snapshot_.history;
        if (snapshot_.currentChord.isValid()) {
            ChordHistoryEntry single;
            single.chord = snapshot_.currentChord;
            single.roman = snapshot_.currentRoman;
            single.barPosition = 1.0;
            single.durationBeats = 4.0;
            single.midiNotes = snapshot_.currentChord.voicingMidiNotes(3);
            return { single };
        }
        return {};
    }

    juce::File exportTempMidiFile() const {
        auto entries = getExportHistory();
        auto bytes = buildMidiFileBytes(entries, currentBpm_, snapshot_.currentKey.preferFlats());
        auto tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("HarmonyHUD_Chords.mid");
        tempFile.replaceWithData(bytes.data(), bytes.size());
        return tempFile;
    }

    void layoutHistoryStrip(bool scrollToEnd) {
        const int viewW = juce::jmax(10, viewport_.getWidth());
        const int viewH = juce::jmax(24, viewport_.getHeight() - 8);
        const int reqW = historyStrip_->getRequiredWidth(viewW);
        historyStrip_->setSize(reqW, viewH);
        if (scrollToEnd && reqW > viewW)
            viewport_.setViewPosition(reqW - viewW, 0);
    }

    void showTransientHint(const juce::String& text) {
        hintText_ = text;
        repaint();
        startTimer(2000);
    }

    void timerCallback() override {
        stopTimer();
        hintText_.clear();
        repaint();
    }

    AnalysisSnapshot snapshot_;
    double currentBpm_ = 120.0;
    ReharmPushTiming pushTiming_ = ReharmPushTiming::NextBar;
    juce::String hintText_;

    std::unique_ptr<TimingSelectorPill> timingPill_;
    std::array<std::unique_ptr<SubChipButton>, 5> subChips_;
    juce::TextButton sendBandBtn_;
    juce::TextButton pullBandBtn_;
    std::unique_ptr<MidiDragButton> midiDragBtn_;
    juce::TextButton copyChartBtn_;
    juce::TextButton clearBtn_;
    juce::Viewport viewport_;
    std::unique_ptr<HistoryStrip> historyStrip_;
};

} // namespace harmonyhud::ui
