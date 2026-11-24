#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../dsp/MusicTheory.h"
#include "HudPalette.h"

#include <array>
#include <cmath>
#include <functional>
#include <vector>

namespace harmonyhud::ui {

class KeyboardComponent : public juce::Component {
public:
    static constexpr int kMinMidi = 36; // C2
    static constexpr int kMaxMidi = 84; // C6

    std::function<void(int midiNote)> onNoteClicked;

    KeyboardComponent() {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void updateSnapshot(const AnalysisSnapshot& snap) {
        snapshot_ = snap;
        repaint();
    }

    void setViewMode(ViewMode mode) {
        if (viewMode_ != mode) {
            viewMode_ = mode;
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

    void resized() override {
        rebuildKeyGeometry();
    }

    void paint(juce::Graphics& g) override {
        const auto bounds = getLocalBounds().toFloat();
        palette::drawPanelCard(g, bounds, 8.0f, palette::bgPanel, palette::border);

        const auto inner = bounds.reduced(10.0f, 8.0f);
        const auto header = inner.withHeight(22.0f);
        drawHeader(g, header);

        if (whiteKeys_.empty())
            rebuildKeyGeometry();

        const bool flats = snapshot_.currentKey.preferFlats();
        const ChordSymbol& displayChord = activeDisplayChord();
        const int chordRoot = displayChord.isValid() ? displayChord.root : -1;
        const uint16_t overlayMask = computeOverlayPitchClassMask(
            overlayMode_, displayChord, snapshot_.currentKey, snapshot_.activeMode);
        const uint16_t guideMask = displayChord.guideToneMask();

        const bool showUser = (viewMode_ == ViewMode::Input || viewMode_ == ViewMode::BothAndClash);
        const bool showBand = snapshot_.bandLink.connected
                              && (viewMode_ == ViewMode::Band || viewMode_ == ViewMode::BothAndClash);

        // Subtle pulse factor for clashing keys
        const float pulse = 0.72f + 0.28f * std::sin(static_cast<float>(juce::Time::getMillisecondCounterHiRes()) * 0.008f);

        if (!keyboardBounds_.isEmpty()) {
            g.setColour(palette::bgDark);
            g.fillRoundedRectangle(keyboardBounds_.expanded(1.5f), 5.0f);
        }

        // 1. White keys
        for (const auto& k : whiteKeys_) {
            const bool userActive = showUser && isUserNoteActive(k.midi);
            const auto bandCols = showBand ? getBandSlotColors(k.midi) : std::vector<juce::Colour>{};
            const bool bandActive = !bandCols.empty();
            const bool clashing = isNoteClashing(k.midi);
            const bool isRoot = (chordRoot >= 0 && (k.midi % 12) == chordRoot);
            const bool hovered = (k.midi == hoveredNote_);
            const bool pressed = (k.midi == pressedNote_);

            juce::Colour fill { 0xFFE2E8F0 };
            if (clashing) {
                fill = palette::accentCoral.interpolatedWith(juce::Colour(0xFFFF3B6B), pulse * 0.45f);
            } else if (userActive && bandActive) {
                fill = palette::userInput.interpolatedWith(bandCols.front(), 0.35f);
            } else if (userActive) {
                fill = (showBand ? palette::userInput.interpolatedWith(palette::accentCyan, 0.25f)
                                 : (isRoot ? palette::accentAmber : palette::accentCyan));
            } else if (bandActive) {
                fill = bandCols.front().interpolatedWith(juce::Colour(0xFFE2E8F0), 0.18f);
            } else if (pressed) {
                fill = palette::accentCyan.interpolatedWith(juce::Colour(0xFFE2E8F0), 0.35f);
            } else if (hovered) {
                fill = juce::Colour(0xFFF8FAFC);
            }

            const auto keyRect = k.rect.reduced(0.6f, 0.5f);
            g.setColour(fill);
            g.fillRoundedRectangle(keyRect, 3.0f);

            if (!userActive && !bandActive && !clashing) {
                juce::ColourGradient grad(juce::Colour(0x220B0E14), keyRect.getX(), keyRect.getY(),
                                          juce::Colour(0x000B0E14), keyRect.getX(), keyRect.getY() + 14.0f, false);
                g.setGradientFill(grad);
                g.fillRoundedRectangle(keyRect, 3.0f);
            }

            // Border / clash pulsing outline / user halo
            if (clashing) {
                g.setColour(palette::accentCoral.brighter(0.35f).withAlpha(0.75f + 0.25f * pulse));
                g.drawRoundedRectangle(keyRect.reduced(0.5f), 3.0f, 2.0f);
            } else if (userActive && showBand) {
                g.setColour(palette::accentCyan);
                g.drawRoundedRectangle(keyRect.reduced(0.6f), 3.0f, 1.5f);
            } else {
                g.setColour(palette::bgDark.withAlpha(0.65f));
                g.drawRoundedRectangle(keyRect, 3.0f, 0.8f);
            }

            // Scale / Guide-Tone / Tension overlay marker on white key
            if (overlayMode_ != OverlayMode::Off) {
                const int pc = k.midi % 12;
                if ((overlayMask & (1u << pc)) != 0)
                    drawOverlayMarker(g, keyRect, pc, chordRoot, guideMask, false);
            }

            // Active / Band X-Ray stacked micro-dots near bottom of white key
            const float baseDotY = keyRect.getBottom() - ((k.midi % 12 == 0) ? 20.5f : 11.0f);
            drawKeyActivityDots(g, keyRect.getCentreX(), baseDotY, keyRect.getWidth(),
                                userActive, isRoot, clashing, bandCols, false);

            // Octave label on C keys (C2..C6)
            if ((k.midi % 12) == 0) {
                const int oct = (k.midi / 12) - 1;
                g.setFont(palette::makeFont(9.5f, userActive || bandActive || clashing));
                g.setColour((userActive || bandActive || clashing) ? palette::bgDark : juce::Colour(0xFF475569));
                g.drawText("C" + juce::String(oct),
                           keyRect.withTop(keyRect.getBottom() - 14.0f),
                           juce::Justification::centred, false);
            }
        }

        // 2. Black keys (drawn on top)
        for (const auto& k : blackKeys_) {
            const bool userActive = showUser && isUserNoteActive(k.midi);
            const auto bandCols = showBand ? getBandSlotColors(k.midi) : std::vector<juce::Colour>{};
            const bool bandActive = !bandCols.empty();
            const bool clashing = isNoteClashing(k.midi);
            const bool isRoot = (chordRoot >= 0 && (k.midi % 12) == chordRoot);
            const bool hovered = (k.midi == hoveredNote_);
            const bool pressed = (k.midi == pressedNote_);

            if (userActive || bandActive || clashing) {
                const auto glow = clashing ? palette::accentCoral
                                           : (userActive ? (showBand ? palette::userInput : (isRoot ? palette::accentAmber : palette::accentCyan))
                                                         : bandCols.front());
                g.setColour(glow.withAlpha(clashing ? (0.30f + 0.20f * pulse) : 0.30f));
                g.fillRoundedRectangle(k.rect.expanded(2.0f, 1.5f), 4.0f);
            }

            juce::Colour fill { 0xFF141923 };
            if (clashing)
                fill = palette::accentCoral;
            else if (userActive && bandActive)
                fill = palette::userInput.interpolatedWith(bandCols.front(), 0.45f);
            else if (userActive)
                fill = showBand ? palette::userInput.interpolatedWith(palette::accentCyan, 0.35f)
                                : (isRoot ? palette::accentAmber : palette::accentCyan);
            else if (bandActive)
                fill = bandCols.front().darker(0.10f);
            else if (pressed)
                fill = palette::accentCyan.darker(0.35f);
            else if (hovered)
                fill = juce::Colour(0xFF232C3D);

            g.setColour(fill);
            g.fillRoundedRectangle(k.rect, 2.5f);

            if (clashing) {
                g.setColour(juce::Colours::white.withAlpha(0.85f));
                g.drawRoundedRectangle(k.rect.reduced(0.5f), 2.5f, 1.5f);
            } else {
                g.setColour(palette::bgDark);
                g.drawRoundedRectangle(k.rect, 2.5f, 1.0f);
            }

            if (overlayMode_ != OverlayMode::Off) {
                const int pc = k.midi % 12;
                if ((overlayMask & (1u << pc)) != 0)
                    drawOverlayMarker(g, k.rect, pc, chordRoot, guideMask, true);
            }

            drawKeyActivityDots(g, k.rect.getCentreX(), k.rect.getBottom() - 7.5f, k.rect.getWidth(),
                                userActive, isRoot, clashing, bandCols, true);
        }

        // Hover tooltip badge
        if (hoveredNote_ >= kMinMidi && hoveredNote_ <= kMaxMidi) {
            const auto name = juce::String(midiToNoteName(hoveredNote_, flats));
            const auto badge = juce::Rectangle<float>(keyboardBounds_.getRight() - 44.0f,
                                                      keyboardBounds_.getY() + 4.0f, 40.0f, 16.0f);
            g.setColour(palette::bgDark.withAlpha(0.86f));
            g.fillRoundedRectangle(badge, 4.0f);
            g.setColour(palette::accentCyan.withAlpha(0.75f));
            g.drawRoundedRectangle(badge, 4.0f, 1.0f);
            g.setColour(palette::textPrimary);
            g.setFont(palette::makeFont(10.0f, true));
            g.drawText(name, badge, juce::Justification::centred, false);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override {
        const int hit = hitTestNote(e.position);
        if (hit != hoveredNote_) {
            hoveredNote_ = hit;
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override {
        if (hoveredNote_ != -1 || pressedNote_ != -1) {
            hoveredNote_ = -1;
            pressedNote_ = -1;
            repaint();
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        const int hit = hitTestNote(e.position);
        if (hit >= kMinMidi && hit <= kMaxMidi) {
            pressedNote_ = hit;
            repaint();
            if (onNoteClicked)
                onNoteClicked(hit);
        }
    }

    void mouseUp(const juce::MouseEvent&) override {
        if (pressedNote_ != -1) {
            pressedNote_ = -1;
            repaint();
        }
    }

private:
    struct KeyRect {
        int midi = 0;
        juce::Rectangle<float> rect;
    };

    static bool isBlackKey(int midi) {
        const int pc = midi % 12;
        return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
    }

    const ChordSymbol& activeDisplayChord() const {
        if (viewMode_ == ViewMode::Band && snapshot_.bandLink.connected && snapshot_.bandLink.activeChord.isValid())
            return snapshot_.bandLink.activeChord;
        return snapshot_.currentChord;
    }

    bool isUserNoteActive(int midi) const {
        return (midi >= 0 && midi < 128) && snapshot_.activeNotes[static_cast<size_t>(midi)];
    }

    bool isNoteClashing(int midi) const {
        return (midi >= 0 && midi < 128) && snapshot_.clashingNotes[static_cast<size_t>(midi)];
    }

    std::vector<juce::Colour> getBandSlotColors(int midi) const {
        std::vector<juce::Colour> cols;
        if (midi < 0 || midi >= 128 || !snapshot_.bandLink.connected)
            return cols;
        const size_t n = static_cast<size_t>(midi);
        if (snapshot_.bandLink.slotNotes[1][n]) cols.push_back(palette::bandBass);
        if (snapshot_.bandLink.slotNotes[2][n]) cols.push_back(palette::bandKeys);
        if (snapshot_.bandLink.slotNotes[3][n]) cols.push_back(palette::bandGuitar);
        if (snapshot_.bandLink.slotNotes[5][n]) cols.push_back(palette::bandHorns);
        return cols;
    }

    static juce::String overlayLegendText(OverlayMode mode, ScaleMode activeMode) {
        switch (mode) {
            case OverlayMode::ChordTones: return "Overlay: Chord Tones";
            case OverlayMode::GuideTones: return "Overlay: Guide 3/7";
            case OverlayMode::Pentatonic: return "Overlay: Pentatonic";
            case OverlayMode::ActiveMode: return "Overlay: " + juce::String(scaleModeName(activeMode));
            case OverlayMode::Tensions:   return "Overlay: Tensions";
            case OverlayMode::Off:
            default:                      return {};
        }
    }

    void drawOverlayMarker(juce::Graphics& g, juce::Rectangle<float> keyRect, int pc,
                           int chordRoot, uint16_t guideMask, bool isBlack) const {
        const bool isRoot = (chordRoot >= 0 && pc == chordRoot);
        const bool isGuide = (guideMask & (1u << pc)) != 0;
        const float cx = keyRect.getCentreX();
        const float cy = isBlack ? (keyRect.getY() + keyRect.getHeight() * 0.42f)
                                 : (keyRect.getY() + keyRect.getHeight() * 0.68f);

        if (overlayMode_ == OverlayMode::GuideTones || (isGuide && overlayMode_ == OverlayMode::ChordTones)) {
            // Crisp diamond + "3"/"7" micro-label for Guide Tones
            const float r = isBlack ? 4.2f : 5.6f;
            juce::Path diamond;
            diamond.startNewSubPath(cx, cy - r);
            diamond.lineTo(cx + r, cy);
            diamond.lineTo(cx, cy + r);
            diamond.lineTo(cx - r, cy);
            diamond.closeSubPath();

            g.setColour(palette::overlayGuide.withAlpha(0.28f));
            g.fillPath(diamond);
            g.setColour(palette::overlayGuide);
            g.strokePath(diamond, juce::PathStrokeType(1.3f));

            if (!isBlack && chordRoot >= 0) {
                const int rel = (pc - chordRoot + 12) % 12;
                const char* degTxt = (rel == 3 || rel == 4) ? "3" : ((rel == 10 || rel == 11) ? "7" : "");
                if (degTxt[0] != '\0') {
                    g.setFont(palette::makeFont(7.8f, true));
                    g.setColour(palette::bgDark);
                    g.drawText(degTxt, juce::Rectangle<float>(cx - r, cy - r, r * 2.0f, r * 2.0f),
                               juce::Justification::centred, false);
                }
            }
            return;
        }

        const auto col = isRoot ? palette::accentAmber
                                : (isGuide ? palette::overlayGuide : palette::overlayScale);
        const float r = isBlack ? 3.2f : 4.2f;
        g.setColour(col.withAlpha(isRoot ? 0.36f : 0.22f));
        g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
        g.setColour(col.withAlpha(0.85f));
        g.drawEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f, 1.2f);
    }

    void drawKeyActivityDots(juce::Graphics& g, float cx, float baseY, float keyW,
                             bool userActive, bool isRoot, bool clashing,
                             const std::vector<juce::Colour>& bandCols, bool isBlack) const {
        if (!userActive && bandCols.empty() && !clashing)
            return;

        if (!bandCols.empty()) {
            // Stacked colored micro-dots for Band X-Ray (plus user dot if both active)
            std::vector<juce::Colour> dots = bandCols;
            if (userActive)
                dots.push_back(palette::userInput);

            const float dotR = isBlack ? 2.1f : juce::jlimit(2.3f, 3.2f, keyW * 0.14f);
            const float spacing = dotR * 2.35f;
            for (size_t i = 0; i < dots.size(); ++i) {
                const float dy = baseY - static_cast<float>(i) * spacing;
                g.setColour(palette::bgDark.withAlpha(0.85f));
                g.fillEllipse(cx - dotR - 0.8f, dy - dotR - 0.8f, (dotR + 0.8f) * 2.0f, (dotR + 0.8f) * 2.0f);
                g.setColour(dots[i]);
                g.fillEllipse(cx - dotR, dy - dotR, dotR * 2.0f, dotR * 2.0f);
            }
            return;
        }

        const float dotR = isBlack ? juce::jlimit(2.0f, 3.0f, keyW * 0.22f)
                                   : juce::jlimit(2.8f, 4.3f, keyW * 0.19f);
        const auto glow = clashing ? palette::accentCoral : (isRoot ? palette::accentAmber : palette::accentCyan);
        if (!isBlack) {
            g.setColour(glow.withAlpha(0.35f));
            g.fillEllipse(cx - dotR * 1.85f, baseY - dotR * 1.85f, dotR * 3.7f, dotR * 3.7f);
        }
        g.setColour(clashing ? juce::Colours::white : palette::bgDark);
        g.fillEllipse(cx - dotR, baseY - dotR, dotR * 2.0f, dotR * 2.0f);
    }

    void drawHeader(juce::Graphics& g, juce::Rectangle<float> area) const {
        const bool flats = snapshot_.currentKey.preferFlats();

        // Clash badge on far right
        const bool hasClash = !snapshot_.clashes.empty();
        juce::String clashText = hasClash
            ? ("[!] " + juce::String(snapshot_.clashes.front().description(flats)))
            : "Clash: None";
        const auto clashAccent = hasClash ? palette::accentCoral : palette::accentEmerald;

        const float clashW = juce::jlimit(96.0f, juce::jmax(100.0f, area.getWidth() * 0.36f),
                                          static_cast<float>(clashText.length()) * 6.4f + 20.0f);
        const auto clashBounds = area.withLeft(area.getRight() - clashW).reduced(0.0f, 1.0f);
        palette::drawStatusPill(g, clashBounds, clashText, clashAccent, 10.0f);

        float rightEdge = clashBounds.getX() - 6.0f;

        // Overlay legend pill (when active)
        if (overlayMode_ != OverlayMode::Off) {
            const auto ovText = overlayLegendText(overlayMode_, snapshot_.activeMode);
            const float ovW = juce::jlimit(104.0f, 178.0f, static_cast<float>(ovText.length()) * 6.2f + 18.0f);
            const auto ovBounds = juce::Rectangle<float>(rightEdge - ovW, area.getY() + 1.0f, ovW, area.getHeight() - 2.0f);
            const auto ovCol = (overlayMode_ == OverlayMode::GuideTones) ? palette::overlayGuide : palette::overlayScale;
            palette::drawStatusPill(g, ovBounds, ovText, ovCol, 10.0f);
            rightEdge = ovBounds.getX() - 6.0f;
        }

        // Compact Band X-Ray legend when connected and Band visible
        const bool showBand = snapshot_.bandLink.connected
                              && (viewMode_ == ViewMode::Band || viewMode_ == ViewMode::BothAndClash);
        if (showBand && rightEdge - area.getX() > 260.0f) {
            const float legW = 152.0f;
            const auto legBounds = juce::Rectangle<float>(rightEdge - legW, area.getY() + 1.0f, legW, area.getHeight() - 2.0f);
            g.setColour(palette::bgCard);
            g.fillRoundedRectangle(legBounds, legBounds.getHeight() * 0.5f);
            g.setColour(palette::border);
            g.drawRoundedRectangle(legBounds.reduced(0.5f), legBounds.getHeight() * 0.5f, 1.0f);

            struct Leg { const char* name; juce::Colour col; };
            const Leg items[4] = {
                { "B", palette::bandBass },
                { "K", palette::bandKeys },
                { "G", palette::bandGuitar },
                { "H", palette::bandHorns }
            };
            const float slotW = (legBounds.getWidth() - 12.0f) / 4.0f;
            g.setFont(palette::makeFont(9.5f, true));
            for (int i = 0; i < 4; ++i) {
                const float x = legBounds.getX() + 8.0f + static_cast<float>(i) * slotW;
                g.setColour(items[i].col);
                g.fillEllipse(x, legBounds.getCentreY() - 3.0f, 6.0f, 6.0f);
                g.setColour(palette::textMuted);
                g.drawText(items[i].name, juce::Rectangle<float>(x + 8.0f, legBounds.getY(), slotW - 8.0f, legBounds.getHeight()),
                           juce::Justification::centredLeft, false);
            }
            rightEdge = legBounds.getX() - 6.0f;
        }

        // Active notes pill on left
        juce::String activeStr = "Active: ";
        int count = 0;
        for (int n = 0; n < 128; ++n) {
            if (snapshot_.activeNotes[static_cast<size_t>(n)]) {
                if (count > 0) activeStr += " ";
                activeStr += juce::String(midiToNoteName(n, flats));
                if (++count >= 8) break;
            }
        }
        if (count == 0)
            activeStr += "--";

        const auto activeBounds = area.withRight(juce::jmax(area.getX() + 60.0f, rightEdge)).reduced(0.0f, 1.0f);
        g.setColour(palette::bgCard);
        g.fillRoundedRectangle(activeBounds, activeBounds.getHeight() * 0.5f);
        g.setColour(palette::border);
        g.drawRoundedRectangle(activeBounds.reduced(0.5f), activeBounds.getHeight() * 0.5f, 1.0f);

        g.setFont(palette::makeFont(10.5f, false));
        g.setColour(count > 0 ? palette::textPrimary : palette::textMuted);
        g.drawText(activeStr, activeBounds.reduced(10.0f, 0.0f), juce::Justification::centredLeft, true);
    }

    void rebuildKeyGeometry() {
        whiteKeys_.clear();
        blackKeys_.clear();

        const auto inner = getLocalBounds().toFloat().reduced(10.0f, 8.0f);
        keyboardBounds_ = inner.withTrimmedTop(28.0f);
        if (keyboardBounds_.getWidth() <= 10.0f || keyboardBounds_.getHeight() <= 10.0f)
            return;

        int numWhite = 0;
        for (int m = kMinMidi; m <= kMaxMidi; ++m)
            if (!isBlackKey(m))
                ++numWhite;

        const float whiteW = keyboardBounds_.getWidth() / static_cast<float>(numWhite);
        const float whiteH = keyboardBounds_.getHeight();
        const float blackW = whiteW * 0.62f;
        const float blackH = whiteH * 0.60f;

        int wIdx = 0;
        for (int m = kMinMidi; m <= kMaxMidi; ++m) {
            if (!isBlackKey(m)) {
                juce::Rectangle<float> r(keyboardBounds_.getX() + static_cast<float>(wIdx) * whiteW,
                                         keyboardBounds_.getY(), whiteW, whiteH);
                whiteKeys_.push_back({ m, r });
                ++wIdx;
            } else {
                const float boundaryX = keyboardBounds_.getX() + static_cast<float>(wIdx) * whiteW;
                const int pc = m % 12;
                float nudge = 0.0f;
                if (pc == 1 || pc == 6) nudge = -0.06f * whiteW;
                else if (pc == 3 || pc == 10) nudge = 0.06f * whiteW;

                juce::Rectangle<float> r(boundaryX - blackW * 0.5f + nudge,
                                         keyboardBounds_.getY(), blackW, blackH);
                blackKeys_.push_back({ m, r });
            }
        }
    }

    int hitTestNote(juce::Point<float> p) const {
        for (const auto& k : blackKeys_)
            if (k.rect.contains(p))
                return k.midi;
        for (const auto& k : whiteKeys_)
            if (k.rect.contains(p))
                return k.midi;
        return -1;
    }

    AnalysisSnapshot snapshot_;
    ViewMode viewMode_ = ViewMode::BothAndClash;
    OverlayMode overlayMode_ = OverlayMode::Off;
    juce::Rectangle<float> keyboardBounds_;
    std::vector<KeyRect> whiteKeys_;
    std::vector<KeyRect> blackKeys_;
    int hoveredNote_ = -1;
    int pressedNote_ = -1;
};

} // namespace harmonyhud::ui
