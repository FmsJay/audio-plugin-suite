#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../dsp/MusicTheory.h"
#include "HudPalette.h"

#include <algorithm>
#include <cmath>

namespace harmonyhud::ui {

class ClashMonitorComponent : public juce::Component {
public:
    void updateSnapshot(const AnalysisSnapshot& snap) {
        snapshot_ = snap;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto bounds = getLocalBounds().toFloat();
        const bool bandLinked = snapshot_.bandLink.connected;
        const bool radarActive = bandLinked || snapshot_.sidechainActive;
        const bool hasClash = radarActive && (!snapshot_.clashes.empty() || snapshot_.clashSeverity > 0.15f);

        palette::drawPanelCard(g, bounds, 8.0f, palette::bgPanel,
                               hasClash ? palette::accentCoral.withAlpha(0.65f) : palette::border);

        auto area = bounds.reduced(10.0f, 8.0f);

        // 1. Header: Title + Radar/Sidechain Status Pill
        const auto header = area.removeFromTop(20.0f);
        g.setFont(palette::makeFont(10.5f, true));
        g.setColour(palette::textMuted);
        g.drawText(bandLinked ? "BAND CLASH RADAR" : "SIDECHAIN CLASH MONITOR",
                   header.withTrimmedRight(124.0f), juce::Justification::centredLeft, true);

        juce::String statusText = "NO SIDECHAIN";
        juce::Colour statusCol = palette::textMuted;
        if (bandLinked) {
            if (hasClash) {
                statusText = "BAND CLASH!";
                statusCol = palette::accentCoral;
            } else {
                statusText = "BAND RADAR: CLEAN";
                statusCol = palette::accentEmerald;
            }
        } else if (snapshot_.sidechainActive) {
            if (hasClash) {
                statusText = "CLASH!";
                statusCol = palette::accentCoral;
            } else {
                statusText = "SIDECHAIN: ACTIVE";
                statusCol = palette::accentEmerald;
            }
        }
        palette::drawStatusPill(g, header.withLeft(header.getRight() - 122.0f), statusText, statusCol, 9.0f);

        area.removeFromTop(5.0f);

        // 2. Mode pill + Band Auto-Avoid badge (when linked)
        const auto modeRow = area.removeFromTop(18.0f);
        drawModeAndAvoidRow(g, modeRow);

        area.removeFromTop(5.0f);

        // 3. Severity bar
        const auto meterRow = area.removeFromTop(13.0f);
        drawSeverityBar(g, meterRow);

        area.removeFromTop(4.0f);

        // 4. Band Instrument LEDs (when linked) OR Main/SC dB level readouts
        const auto infoRow = area.removeFromTop(16.0f);
        if (bandLinked)
            drawBandActivityLeds(g, infoRow);
        else
            drawLevelReadouts(g, infoRow);

        area.removeFromTop(5.0f);

        // 5. Reserve bottom for 12-bin Chroma Energy Strip
        const float chromaH = juce::jlimit(44.0f, 70.0f, area.getHeight() * 0.48f);
        const auto chromaArea = area.removeFromBottom(chromaH);
        area.removeFromBottom(5.0f);

        // 6. Middle: Active clashes list (up to 3)
        drawClashList(g, area);

        // 7. Bottom: 12-bin Chroma Energy graph with avoid-note tick marks
        drawChromaStrip(g, chromaArea);
    }

private:
    static juce::String formatDb(float db) {
        if (db <= -90.0f || !std::isfinite(db))
            return "-inf dB";
        return juce::String(db, 1) + " dB";
    }

    void drawModeAndAvoidRow(juce::Graphics& g, juce::Rectangle<float> area) const {
        const juce::String modeTxt = "Mode: " + juce::String(scaleModeName(snapshot_.activeMode));
        const bool showAvoid = snapshot_.bandLink.connected && snapshot_.bandLink.clashAvoidanceActive;

        if (showAvoid) {
            const float avoidW = 114.0f;
            const auto avoidRect = area.withLeft(area.getRight() - avoidW);
            palette::drawStatusPill(g, avoidRect, "Band Auto-Avoid ON", palette::accentEmerald, 9.0f);
            palette::drawStatusPill(g, area.withRight(avoidRect.getX() - 5.0f), modeTxt, palette::overlayScale, 9.2f);
        } else {
            palette::drawStatusPill(g, area, modeTxt, palette::overlayScale, 9.5f);
        }
    }

    void drawSeverityBar(juce::Graphics& g, juce::Rectangle<float> area) const {
        const float sev = juce::jlimit(0.0f, 1.0f, snapshot_.clashSeverity);
        const auto pctArea = area.withLeft(area.getRight() - 36.0f);
        const auto barArea = area.withRight(pctArea.getX() - 6.0f).reduced(0.0f, 2.0f);

        g.setColour(palette::bgDark);
        g.fillRoundedRectangle(barArea, 4.0f);
        g.setColour(palette::border);
        g.drawRoundedRectangle(barArea, 4.0f, 0.8f);

        if (sev > 0.005f) {
            const auto fillW = juce::jmax(6.0f, barArea.getWidth() * sev);
            const auto fillRect = barArea.withWidth(fillW);
            juce::ColourGradient grad(palette::accentEmerald, barArea.getX(), barArea.getY(),
                                      palette::accentCoral, barArea.getRight(), barArea.getY(), false);
            grad.addColour(0.52, palette::accentAmber);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(fillRect, 4.0f);
        }

        const auto pctCol = (sev > 0.55f) ? palette::accentCoral
                                          : (sev > 0.20f ? palette::accentAmber : palette::textMuted);
        g.setFont(palette::makeMonoFont(10.0f, true));
        g.setColour(pctCol);
        g.drawText(juce::String(static_cast<int>(std::round(sev * 100.0f))) + "%",
                   pctArea, juce::Justification::centredRight, false);
    }

    void drawBandActivityLeds(juce::Graphics& g, juce::Rectangle<float> area) const {
        struct SlotLed { const char* label; int slotIdx; juce::Colour col; };
        const SlotLed leds[4] = {
            { "BASS", 1, palette::bandBass },
            { "KEYS", 2, palette::bandKeys },
            { "GTR",  3, palette::bandGuitar },
            { "HRN",  5, palette::bandHorns }
        };

        // Leave right side for compact MAIN dB
        const auto dbArea = area.withLeft(area.getRight() - 78.0f);
        const auto ledStrip = area.withRight(dbArea.getX() - 4.0f);
        const float slotW = ledStrip.getWidth() / 4.0f;

        g.setFont(palette::makeMonoFont(9.0f, true));
        for (int i = 0; i < 4; ++i) {
            const bool active = snapshot_.bandLink.slotActive[static_cast<size_t>(leds[i].slotIdx)];
            const auto cell = juce::Rectangle<float>(ledStrip.getX() + static_cast<float>(i) * slotW,
                                                     ledStrip.getY(), slotW - 3.0f, ledStrip.getHeight());
            const float ledR = 3.2f;
            const float lx = cell.getX() + 4.0f;
            const float ly = cell.getCentreY();

            if (active) {
                g.setColour(leds[i].col.withAlpha(0.32f));
                g.fillEllipse(lx - ledR - 1.5f, ly - ledR - 1.5f, (ledR + 1.5f) * 2.0f, (ledR + 1.5f) * 2.0f);
                g.setColour(leds[i].col);
            } else {
                g.setColour(palette::border);
            }
            g.fillEllipse(lx - ledR, ly - ledR, ledR * 2.0f, ledR * 2.0f);

            g.setColour(active ? palette::textPrimary : palette::textMuted.withAlpha(0.6f));
            g.drawText(leds[i].label, cell.withTrimmedLeft(10.0f), juce::Justification::centredLeft, false);
        }

        g.setFont(palette::makeMonoFont(9.5f, false));
        g.setColour(palette::textMuted);
        g.drawText("IN " + formatDb(snapshot_.inputLevelDb), dbArea, juce::Justification::centredRight, true);
    }

    void drawLevelReadouts(juce::Graphics& g, juce::Rectangle<float> area) const {
        const float halfW = area.getWidth() * 0.5f;
        g.setFont(palette::makeMonoFont(10.0f, false));

        g.setColour(palette::textMuted);
        g.drawText("MAIN: " + formatDb(snapshot_.inputLevelDb),
                   area.withWidth(halfW), juce::Justification::centredLeft, true);

        g.setColour(snapshot_.sidechainActive ? palette::accentCyan : palette::textMuted.withAlpha(0.75f));
        g.drawText("SC: " + formatDb(snapshot_.sidechainLevelDb),
                   area.withTrimmedLeft(halfW), juce::Justification::centredRight, true);
    }

    void drawClashList(juce::Graphics& g, juce::Rectangle<float> area) const {
        g.setColour(palette::bgCard);
        g.fillRoundedRectangle(area, 6.0f);
        g.setColour(palette::border);
        g.drawRoundedRectangle(area.reduced(0.5f), 6.0f, 1.0f);

        const auto inner = area.reduced(8.0f, 4.0f);
        if (!snapshot_.bandLink.connected && !snapshot_.sidechainActive) {
            g.setFont(palette::makeFont(10.5f, false));
            g.setColour(palette::textMuted);
            g.drawText("No Harmonic Clashes Detected (Link Band or Route Sidechain)", inner,
                       juce::Justification::centred, true);
            return;
        }

        if (snapshot_.clashes.empty()) {
            g.setFont(palette::makeFont(11.0f, true));
            g.setColour(palette::accentEmerald);
            g.drawText("No Harmonic Clashes Detected", inner, juce::Justification::centred, true);
            return;
        }

        const bool flats = snapshot_.currentKey.preferFlats();
        const int maxItems = juce::jmin(3, static_cast<int>(snapshot_.clashes.size()));
        const float rowH = inner.getHeight() / static_cast<float>(juce::jmax(1, maxItems));

        for (int i = 0; i < maxItems; ++i) {
            const auto& c = snapshot_.clashes[static_cast<size_t>(i)];
            const auto row = juce::Rectangle<float>(inner.getX(),
                                                    inner.getY() + static_cast<float>(i) * rowH,
                                                    inner.getWidth(), rowH);
            const auto col = c.isMinorSecond() ? palette::accentCoral : palette::accentAmber;

            g.setColour(col);
            g.fillEllipse(row.getX(), row.getCentreY() - 3.0f, 6.0f, 6.0f);

            g.setFont(palette::makeFont(10.5f, true));
            g.setColour(col);
            g.drawText(juce::String(c.description(flats)),
                       row.withTrimmedLeft(11.0f), juce::Justification::centredLeft, true);
        }
    }

    void drawChromaStrip(juce::Graphics& g, juce::Rectangle<float> area) const {
        g.setColour(palette::bgCard);
        g.fillRoundedRectangle(area, 6.0f);
        g.setColour(palette::border);
        g.drawRoundedRectangle(area.reduced(0.5f), 6.0f, 1.0f);

        const auto inner = area.reduced(6.0f, 4.0f);
        const float tickH = 4.0f;
        const float labelH = 13.0f;
        const auto barsArea = inner.withTrimmedTop(tickH + 1.0f).withTrimmedBottom(labelH + 2.0f);
        const auto labelsArea = inner.withTop(inner.getBottom() - labelH);

        const uint16_t chordMask = snapshot_.currentChord.pitchClassMask();
        const int chordRoot = snapshot_.currentChord.isValid() ? snapshot_.currentChord.root : -1;
        const bool flats = snapshot_.currentKey.preferFlats();

        uint16_t clashMask = 0;
        for (const auto& c : snapshot_.clashes) {
            if (c.mainMidi >= 0) clashMask |= static_cast<uint16_t>(1u << (c.mainMidi % 12));
            if (c.sidechainMidi >= 0) clashMask |= static_cast<uint16_t>(1u << (c.sidechainMidi % 12));
        }

        float maxVal = 0.001f;
        for (float v : snapshot_.chroma)
            maxVal = std::max(maxVal, v);
        const float scale = (maxVal > 1.0f) ? (1.0f / maxVal) : 1.0f;

        const float slotW = barsArea.getWidth() / 12.0f;
        const float barW = juce::jmax(4.0f, slotW - 4.0f);

        for (int pc = 0; pc < 12; ++pc) {
            const float x = barsArea.getX() + static_cast<float>(pc) * slotW + (slotW - barW) * 0.5f;
            const float energy = juce::jlimit(0.0f, 1.0f, snapshot_.chroma[static_cast<size_t>(pc)] * scale);

            const bool isClash = (clashMask & (1u << pc)) != 0;
            const bool isAvoid = (snapshot_.avoidPitchClassMask & (1u << pc)) != 0;
            const bool isRoot = (pc == chordRoot);
            const bool isTone = (chordMask & (1u << pc)) != 0;

            const auto col = isClash ? palette::accentCoral
                                     : (isRoot ? palette::accentAmber
                                               : (isTone ? palette::accentCyan
                                                         : palette::textMuted.withAlpha(0.45f)));

            // Avoid pitch-class tick mark at top of column
            if (isAvoid) {
                g.setColour(isClash ? palette::accentCoral : palette::accentAmber.withAlpha(0.80f));
                g.fillRoundedRectangle(x, inner.getY(), barW, 2.5f, 1.0f);
            }

            // Track slot
            g.setColour(palette::bgDark.withAlpha(0.75f));
            g.fillRoundedRectangle(x, barsArea.getY(), barW, barsArea.getHeight(), 2.0f);

            // Live energy bar
            const float bh = juce::jmax(2.0f, barsArea.getHeight() * energy);
            g.setColour(col);
            g.fillRoundedRectangle(x, barsArea.getBottom() - bh, barW, bh, 2.0f);

            // Pitch class label
            g.setFont(palette::makeFont(9.0f, isRoot || isTone || isClash));
            g.setColour((isRoot || isTone || isClash) ? col : palette::textMuted);
            g.drawText(pitchClassName(pc, flats),
                       juce::Rectangle<float>(barsArea.getX() + static_cast<float>(pc) * slotW,
                                              labelsArea.getY(), slotW, labelH),
                       juce::Justification::centred, false);
        }
    }

    AnalysisSnapshot snapshot_;
};

} // namespace harmonyhud::ui
