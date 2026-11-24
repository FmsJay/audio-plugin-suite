#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../dsp/MusicTheory.h"
#include "HudPalette.h"

#include <array>
#include <functional>
#include <vector>

namespace harmonyhud::ui {

class FretboardComponent : public juce::Component {
public:
    static constexpr int kNumFrets = 12;

    std::function<void(const std::vector<int>& midiNotes)> onStrumClicked;

    FretboardComponent() {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void updateSnapshot(const AnalysisSnapshot& snap) {
        const auto prevChord = activeDisplayChord();
        snapshot_ = snap;
        const auto nextChord = activeDisplayChord();
        if (!nextChord.sameHarmony(prevChord) || (nextChord.isValid() != prevChord.isValid()))
            recomputeVoicing();
        repaint();
    }

    void setTuning(GuitarTuning tuning) {
        if (tuning_ != tuning) {
            tuning_ = tuning;
            recomputeVoicing();
            repaint();
        }
    }

    GuitarTuning getTuning() const { return tuning_; }

    void setViewMode(ViewMode mode) {
        if (viewMode_ != mode) {
            viewMode_ = mode;
            recomputeVoicing();
            repaint();
        }
    }

    ViewMode getViewMode() const { return viewMode_; }

    void setOverlayMode(OverlayMode mode) {
        if (overlayMode_ != mode) {
            overlayMode_ = mode;
            repaint();
        }
    }

    OverlayMode getOverlayMode() const { return overlayMode_; }

    void setPositionFilter(FretPositionFilter pos) {
        if (posFilter_ != pos) {
            posFilter_ = pos;
            recomputeVoicing();
            repaint();
        }
    }

    FretPositionFilter getPositionFilter() const { return posFilter_; }

    void paint(juce::Graphics& g) override {
        const auto bounds = getLocalBounds().toFloat();
        palette::drawPanelCard(g, bounds, 8.0f, palette::bgPanel,
                               isHovered_ ? palette::accentCyan.withAlpha(0.55f) : palette::border);

        const auto inner = bounds.reduced(10.0f, 7.0f);
        const auto header = inner.withHeight(20.0f);
        drawHeader(g, header);

        const auto tabArea = inner.withTop(inner.getBottom() - 20.0f);
        drawTabStrip(g, tabArea);

        const auto boardOuter = inner.withTrimmedTop(24.0f).withTrimmedBottom(24.0f);
        if (boardOuter.getHeight() < 36.0f || boardOuter.getWidth() < 80.0f)
            return;

        drawFretboard(g, boardOuter);
    }

    void mouseEnter(const juce::MouseEvent&) override {
        isHovered_ = true;
        repaint();
    }

    void mouseExit(const juce::MouseEvent&) override {
        isHovered_ = false;
        isPressed_ = false;
        repaint();
    }

    void mouseDown(const juce::MouseEvent&) override {
        isPressed_ = true;
        repaint();

        if (!onStrumClicked || !voicing_.valid)
            return;

        const auto openMidi = guitarOpenStrings(tuning_);
        std::vector<int> notes;
        notes.reserve(6);
        for (int s = 0; s < 6; ++s) {
            if (voicing_.frets[static_cast<size_t>(s)] >= 0)
                notes.push_back(openMidi[static_cast<size_t>(s)] + voicing_.frets[static_cast<size_t>(s)]);
        }
        if (!notes.empty())
            onStrumClicked(notes);
    }

    void mouseUp(const juce::MouseEvent&) override {
        isPressed_ = false;
        repaint();
    }

private:
    ChordSymbol activeDisplayChord() const {
        if (viewMode_ == ViewMode::Band && snapshot_.bandLink.connected && snapshot_.bandLink.activeChord.isValid())
            return snapshot_.bandLink.activeChord;
        return snapshot_.currentChord;
    }

    void recomputeVoicing() {
        voicing_ = solveGuitarVoicing(activeDisplayChord(), tuning_, posFilter_);
    }

    bool isMidiClashing(int midi) const {
        if (midi < 0 || midi >= 128)
            return false;
        if (snapshot_.clashingNotes[static_cast<size_t>(midi)])
            return true;
        const int pc = midi % 12;
        for (const auto& c : snapshot_.clashes) {
            if (c.mainMidi >= 0 && (c.mainMidi % 12) == pc)
                return true;
        }
        return false;
    }

    static juce::String positionFilterLabel(FretPositionFilter pos) {
        switch (pos) {
            case FretPositionFilter::Open: return "Open";
            case FretPositionFilter::Pos5: return "Pos 5";
            case FretPositionFilter::Pos7: return "Pos 7";
            case FretPositionFilter::Pos9: return "Pos 9";
            case FretPositionFilter::Auto:
            default:                       return {};
        }
    }

    void drawHeader(juce::Graphics& g, juce::Rectangle<float> area) const {
        g.setFont(palette::makeFont(11.0f, true));
        g.setColour(palette::textMuted);
        g.drawText("GUITAR FRETBOARD", area.withWidth(128.0f), juce::Justification::centredLeft, false);

        const float strumW = 92.0f;
        const auto strumRect = area.withLeft(area.getRight() - strumW);
        palette::drawStatusPill(g, strumRect,
                                isPressed_ ? "Strumming..." : "Click to Strum",
                                isHovered_ ? palette::accentCyan : palette::textMuted, 10.0f);

        float rightEdge = strumRect.getX() - 6.0f;
        const auto posLbl = positionFilterLabel(posFilter_);
        if (posLbl.isNotEmpty()) {
            const float posW = 48.0f;
            const auto posRect = juce::Rectangle<float>(rightEdge - posW, area.getY(), posW, area.getHeight());
            palette::drawStatusPill(g, posRect, posLbl, palette::accentAmber, 9.5f);
            rightEdge = posRect.getX() - 6.0f;
        }

        const auto tuningText = juce::String(guitarTuningName(tuning_));
        const auto tuningRect = area.withTrimmedLeft(130.0f).withRight(juce::jmax(140.0f, rightEdge));
        g.setFont(palette::makeFont(10.5f, false));
        g.setColour(palette::accentViolet);
        g.drawText(tuningText, tuningRect, juce::Justification::centredLeft, true);
    }

    void drawFretboard(juce::Graphics& g, juce::Rectangle<float> area) const {
        const float leftLabelW = 38.0f;
        const float bottomFretNumH = 14.0f;
        const auto neck = juce::Rectangle<float>(area.getX() + leftLabelW,
                                                 area.getY() + 3.0f,
                                                 area.getWidth() - leftLabelW - 4.0f,
                                                 area.getHeight() - bottomFretNumH - 4.0f);

        g.setColour(juce::Colour(0xFF0E131D));
        g.fillRoundedRectangle(neck, 4.0f);
        g.setColour(palette::border);
        g.drawRoundedRectangle(neck, 4.0f, 1.0f);

        const float fretW = neck.getWidth() / static_cast<float>(kNumFrets);

        // Fretboard inlay dots
        static constexpr int kSingleDots[] = { 3, 5, 7, 9 };
        g.setColour(palette::textMuted.withAlpha(0.18f));
        for (int f : kSingleDots) {
            const float cx = neck.getX() + (static_cast<float>(f) - 0.5f) * fretW;
            const float cy = neck.getCentreY();
            g.fillEllipse(cx - 3.5f, cy - 3.5f, 7.0f, 7.0f);
        }
        {
            const float cx = neck.getX() + (12.0f - 0.5f) * fretW;
            const float offset = neck.getHeight() * 0.24f;
            g.fillEllipse(cx - 3.2f, neck.getCentreY() - offset - 3.2f, 6.4f, 6.4f);
            g.fillEllipse(cx - 3.2f, neck.getCentreY() + offset - 3.2f, 6.4f, 6.4f);
        }

        // Nut
        g.setColour(juce::Colour(0xFFCBD5E1));
        g.fillRect(neck.getX() - 2.0f, neck.getY(), 3.5f, neck.getHeight());

        // Fret wires 1..12
        for (int f = 1; f <= kNumFrets; ++f) {
            const float fx = neck.getX() + static_cast<float>(f) * fretW;
            g.setColour(palette::border.brighter(0.25f));
            g.drawVerticalLine(static_cast<int>(std::round(fx)), neck.getY() + 1.0f, neck.getBottom() - 1.0f);

            if (f == 1 || f == 3 || f == 5 || f == 7 || f == 9 || f == 12) {
                g.setFont(palette::makeFont(9.5f, f == 12));
                g.setColour(palette::textMuted);
                g.drawText(juce::String(f),
                           juce::Rectangle<float>(fx - fretW, neck.getBottom() + 1.0f, fretW, bottomFretNumH),
                           juce::Justification::centred, false);
            }
        }

        const auto labels = guitarStringLabels(tuning_);
        const auto openMidi = guitarOpenStrings(tuning_);
        const bool flats = snapshot_.currentKey.preferFlats();
        const ChordSymbol displayChord = activeDisplayChord();
        const int chordRoot = displayChord.isValid() ? displayChord.root : -1;

        const uint16_t overlayMask = computeOverlayPitchClassMask(
            overlayMode_, displayChord, snapshot_.currentKey, snapshot_.activeMode);
        const uint16_t guideMask = displayChord.guideToneMask();

        const bool showBand = snapshot_.bandLink.connected
                              && (viewMode_ == ViewMode::Band || viewMode_ == ViewMode::BothAndClash);

        const float padY = juce::jlimit(6.0f, 12.0f, neck.getHeight() * 0.10f);
        const float usableH = neck.getHeight() - 2.0f * padY;
        const float stringStep = usableH / 5.0f;

        // Pass 1: Draw strings + subtle Scale / Guide-Tone / Band X-Ray overlays across frets 0..12
        for (int row = 0; row < 6; ++row) {
            const int sIdx = 5 - row;
            const float sy = neck.getY() + padY + static_cast<float>(row) * stringStep;
            const int fret = voicing_.valid ? voicing_.frets[static_cast<size_t>(sIdx)] : -1;
            const bool sounding = (fret >= 0);

            const float thickness = 0.9f + static_cast<float>(row) * 0.24f;
            g.setColour(sounding ? juce::Colour(0xFF64748B) : juce::Colour(0xFF334155).withAlpha(0.55f));
            g.drawLine(neck.getX(), sy, neck.getRight(), sy, thickness);

            // Overlay rings & Band X-Ray (Guitar/Bass) live fret markers on frets 1..12
            for (int f = 1; f <= kNumFrets; ++f) {
                const int midi = openMidi[static_cast<size_t>(sIdx)] + f;
                const int pc = midi % 12;
                const float cx = neck.getX() + (static_cast<float>(f) - 0.5f) * fretW;

                if (overlayMode_ != OverlayMode::Off && (overlayMask & (1u << pc)) != 0) {
                    const bool isRoot = (chordRoot >= 0 && pc == chordRoot);
                    const bool isGuide = (guideMask & (1u << pc)) != 0;
                    const auto ovCol = isRoot ? palette::accentAmber
                                              : (isGuide ? palette::overlayGuide : palette::overlayScale);
                    const float r = juce::jlimit(4.0f, 6.0f, stringStep * 0.29f);
                    g.setColour(ovCol.withAlpha(isRoot ? 0.24f : 0.14f));
                    g.fillEllipse(cx - r, sy - r, r * 2.0f, r * 2.0f);
                    g.setColour(ovCol.withAlpha(0.62f));
                    g.drawEllipse(cx - r, sy - r, r * 2.0f, r * 2.0f, 1.1f);
                }

                if (showBand && midi >= 0 && midi < 128) {
                    const bool bandGtr = snapshot_.bandLink.slotNotes[3][static_cast<size_t>(midi)];
                    const bool bandBass = snapshot_.bandLink.slotNotes[1][static_cast<size_t>(midi)];
                    const bool clash = isMidiClashing(midi);
                    if (bandGtr || bandBass) {
                        const auto col = clash ? palette::accentCoral
                                               : (bandGtr ? palette::bandGuitar : palette::bandBass);
                        const float r = juce::jlimit(5.2f, 7.6f, stringStep * 0.36f);
                        g.setColour(col.withAlpha(0.28f));
                        g.fillEllipse(cx - r - 1.5f, sy - r - 1.5f, (r + 1.5f) * 2.0f, (r + 1.5f) * 2.0f);
                        g.setColour(col);
                        g.drawEllipse(cx - r, sy - r, r * 2.0f, r * 2.0f, 1.6f);
                    }
                }
            }
        }

        // Pass 2: Draw string labels, nut x/O badges, and bold active chord voicing dots on top
        for (int row = 0; row < 6; ++row) {
            const int sIdx = 5 - row;
            const float sy = neck.getY() + padY + static_cast<float>(row) * stringStep;
            const int fret = voicing_.valid ? voicing_.frets[static_cast<size_t>(sIdx)] : -1;
            const bool sounding = (fret >= 0);

            g.setFont(palette::makeMonoFont(11.0f, true));
            g.setColour(sounding ? palette::textPrimary : palette::textMuted.withAlpha(0.6f));
            g.drawText(labels[static_cast<size_t>(sIdx)],
                       juce::Rectangle<float>(area.getX(), sy - 7.0f, 14.0f, 14.0f),
                       juce::Justification::centred, false);

            const float badgeX = area.getX() + 24.0f;
            if (!voicing_.valid || fret < 0) {
                g.setFont(palette::makeFont(10.5f, true));
                g.setColour(palette::textMuted.withAlpha(0.55f));
                g.drawText("x", juce::Rectangle<float>(badgeX - 7.0f, sy - 7.0f, 14.0f, 14.0f),
                           juce::Justification::centred, false);
            } else if (fret == 0) {
                const int midi = openMidi[static_cast<size_t>(sIdx)];
                const bool clash = isMidiClashing(midi);
                const bool isRoot = (chordRoot >= 0 && (midi % 12) == chordRoot);
                const auto col = clash ? palette::accentCoral
                                       : (isRoot ? palette::accentAmber : palette::accentCyan);
                const float r = 5.0f;
                g.setColour(col.withAlpha(0.25f));
                g.fillEllipse(badgeX - r - 2.0f, sy - r - 2.0f, (r + 2.0f) * 2.0f, (r + 2.0f) * 2.0f);
                g.setColour(col);
                g.drawEllipse(badgeX - r, sy - r, r * 2.0f, r * 2.0f, 1.8f);
            } else if (fret >= 1 && fret <= kNumFrets) {
                const int midi = openMidi[static_cast<size_t>(sIdx)] + fret;
                const bool clash = isMidiClashing(midi);
                const bool isRoot = (chordRoot >= 0 && (midi % 12) == chordRoot);
                const auto col = clash ? palette::accentCoral
                                       : (isRoot ? palette::accentAmber : palette::accentCyan);

                const float cx = neck.getX() + (static_cast<float>(fret) - 0.5f) * fretW;
                const float dotR = juce::jlimit(6.0f, 9.0f, stringStep * 0.42f);

                g.setColour(col.withAlpha(0.30f));
                g.fillEllipse(cx - dotR * 1.45f, sy - dotR * 1.45f, dotR * 2.9f, dotR * 2.9f);

                g.setColour(col);
                g.fillEllipse(cx - dotR, sy - dotR, dotR * 2.0f, dotR * 2.0f);

                g.setColour(palette::bgDark);
                g.setFont(palette::makeFont(juce::jlimit(8.0f, 10.0f, dotR * 1.15f), true));
                g.drawText(pitchClassName(midi % 12, flats),
                           juce::Rectangle<float>(cx - dotR, sy - dotR, dotR * 2.0f, dotR * 2.0f),
                           juce::Justification::centred, false);
            }
        }
    }

    void drawTabStrip(juce::Graphics& g, juce::Rectangle<float> area) const {
        g.setColour(palette::bgCard);
        g.fillRoundedRectangle(area, 5.0f);
        g.setColour(palette::border);
        g.drawRoundedRectangle(area.reduced(0.5f), 5.0f, 1.0f);

        const auto labels = guitarStringLabels(tuning_);
        juce::String tab;
        for (int s = 5; s >= 0; --s) {
            if (s < 5) tab += "   ";
            const int f = voicing_.valid ? voicing_.frets[static_cast<size_t>(s)] : -1;
            const juce::String val = (f < 0) ? "x" : juce::String(f);
            tab += juce::String(labels[static_cast<size_t>(s)]) + "|--" + val + "--|";
        }

        g.setFont(palette::makeMonoFont(11.0f, false));
        g.setColour(voicing_.valid ? palette::textPrimary : palette::textMuted);
        g.drawText(tab, area.reduced(8.0f, 0.0f), juce::Justification::centred, true);
    }

    AnalysisSnapshot snapshot_;
    GuitarTuning tuning_ = GuitarTuning::Standard;
    ViewMode viewMode_ = ViewMode::BothAndClash;
    OverlayMode overlayMode_ = OverlayMode::Off;
    FretPositionFilter posFilter_ = FretPositionFilter::Auto;
    GuitarVoicing voicing_;
    bool isHovered_ = false;
    bool isPressed_ = false;
};

} // namespace harmonyhud::ui
