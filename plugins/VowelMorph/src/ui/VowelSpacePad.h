#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <SuiteTheme.h>
#include "../VowelMorphParams.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

namespace vowelmorph::ui {

// ============================================================================
// VowelSpacePad
// Top ~62%: Interactive 2D IPA Vowel Triangle XY Pad with 10 phoneme nodes,
//           24-point modulation trail, base ring, live cursor & vector line.
// Bottom ~38%: Animated 5-Formant log-frequency response curve (80 Hz - 8 kHz)
//              with F1..F5 peak markers, Hz readout bar & active syllable pill.
// ============================================================================
class VowelSpacePad : public juce::Component {
public:
    struct PhonemeNode {
        int id;
        float x; // 0 = Front (left), 1 = Back (right)
        float y; // 0 = Open (bottom), 1 = Close (top)
    };

    static constexpr std::array<PhonemeNode, kNumPhonemes> kPhonemeNodes {{
        { static_cast<int>(PhonemeId::EE), 0.10f, 0.92f },
        { static_cast<int>(PhonemeId::IH), 0.22f, 0.76f },
        { static_cast<int>(PhonemeId::EH), 0.26f, 0.52f },
        { static_cast<int>(PhonemeId::AE), 0.24f, 0.24f },
        { static_cast<int>(PhonemeId::AH), 0.52f, 0.12f },
        { static_cast<int>(PhonemeId::AW), 0.78f, 0.28f },
        { static_cast<int>(PhonemeId::OH), 0.86f, 0.58f },
        { static_cast<int>(PhonemeId::OO), 0.90f, 0.88f },
        { static_cast<int>(PhonemeId::UH), 0.52f, 0.48f },
        { static_cast<int>(PhonemeId::MM), 0.88f, 0.96f }
    }};

    std::function<void(float, float)> onVowelXYChanged;

    VowelSpacePad() {
        setOpaque(false);
    }

    void setBaseVowelXY(float x, float y) {
        snap_.baseVowelX = std::clamp(x, 0.0f, 1.0f);
        snap_.baseVowelY = std::clamp(y, 0.0f, 1.0f);
        repaint();
    }

    void updateTelemetry(const vowelmorph::TelemetrySnapshot& snap) {
        const float draggedX = snap_.baseVowelX;
        const float draggedY = snap_.baseVowelY;
        snap_ = snap;
        if (isDragging_) {
            snap_.baseVowelX = draggedX;
            snap_.baseVowelY = draggedY;
        }

        const float lx = std::clamp(snap_.liveVowelX, 0.0f, 1.0f);
        const float ly = std::clamp(snap_.liveVowelY, 0.0f, 1.0f);
        trail_[static_cast<size_t>(trailWriteIdx_)] = { lx, ly };
        trailWriteIdx_ = (trailWriteIdx_ + 1) % kTrailLen;
        if (trailCount_ < kTrailLen)
            ++trailCount_;

        repaint();
    }

    void resized() override {
        auto area = getLocalBounds().reduced(8);
        const int totalH = area.getHeight();
        const int padH = std::max(100, static_cast<int>(std::round(static_cast<float>(totalH) * 0.62f)) - 4);
        xyPadBounds_ = area.removeFromTop(padH).toFloat();
        area.removeFromTop(6);
        formantBounds_ = area.toFloat();
    }

    void paint(juce::Graphics& g) override {
        const auto outer = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(suite::ui::bgCard);
        g.fillRoundedRectangle(outer, 8.0f);
        g.setColour(suite::ui::border);
        g.drawRoundedRectangle(outer, 8.0f, 1.0f);

        paintXYPad(g, xyPadBounds_);
        paintFormantCurve(g, formantBounds_);
    }

    void mouseMove(const juce::MouseEvent& e) override {
        const auto pos = e.position;
        int hovered = -1;
        if (xyPadBounds_.contains(pos)) {
            hovered = findNearestNodeInPixels(pos, 15.0f);
        }
        if (hovered != hoveredNode_) {
            hoveredNode_ = hovered;
            setMouseCursor(xyPadBounds_.contains(pos) ? juce::MouseCursor::CrosshairCursor
                                                      : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent& /*e*/) override {
        if (hoveredNode_ != -1) {
            hoveredNode_ = -1;
            repaint();
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (!xyPadBounds_.contains(e.position))
            return;

        isDragging_ = true;
        const int hitNode = findNearestNodeInPixels(e.position, 14.0f);
        if (hitNode >= 0) {
            const auto& node = kPhonemeNodes[static_cast<size_t>(hitNode)];
            commitXY(node.x, node.y);
        } else {
            const auto pt = pixelToXY(e.position);
            commitXY(pt.x, pt.y);
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (!isDragging_)
            return;

        const auto pt = pixelToXY(e.position);
        commitXY(pt.x, pt.y);
    }

    void mouseUp(const juce::MouseEvent& /*e*/) override {
        isDragging_ = false;
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override {
        if (!xyPadBounds_.contains(e.position))
            return;

        isDragging_ = false;
        commitXY(0.42f, 0.35f);
    }

private:
    static constexpr int kTrailLen = 24;
    static constexpr float kMinFreqHz = 80.0f;
    static constexpr float kMaxFreqHz = 8000.0f;
    static constexpr float kMinDb = -36.0f;
    static constexpr float kMaxDb = 12.0f;

    juce::Rectangle<float> getXYInnerPlot() const noexcept {
        return xyPadBounds_.reduced(24.0f, 18.0f);
    }

    juce::Point<float> xyToPixel(float x, float y) const noexcept {
        const auto plot = getXYInnerPlot();
        const float cx = std::clamp(x, 0.0f, 1.0f);
        const float cy = std::clamp(y, 0.0f, 1.0f);
        return {
            plot.getX() + cx * plot.getWidth(),
            plot.getY() + (1.0f - cy) * plot.getHeight()
        };
    }

    juce::Point<float> pixelToXY(juce::Point<float> p) const noexcept {
        const auto plot = getXYInnerPlot();
        const float w = std::max(1.0f, plot.getWidth());
        const float h = std::max(1.0f, plot.getHeight());
        const float x = std::clamp((p.x - plot.getX()) / w, 0.0f, 1.0f);
        const float y = std::clamp(1.0f - (p.y - plot.getY()) / h, 0.0f, 1.0f);
        return { x, y };
    }

    int findNearestNodeInPixels(juce::Point<float> p, float maxRadiusPx) const noexcept {
        int bestIdx = -1;
        float bestDistSq = maxRadiusPx * maxRadiusPx;
        for (int i = 0; i < kNumPhonemes; ++i) {
            const auto& n = kPhonemeNodes[static_cast<size_t>(i)];
            const auto np = xyToPixel(n.x, n.y);
            const float dx = p.x - np.x;
            const float dy = p.y - np.y;
            const float dSq = dx * dx + dy * dy;
            if (dSq <= bestDistSq) {
                bestDistSq = dSq;
                bestIdx = i;
            }
        }
        return bestIdx;
    }

    int findNearestPhonemeToXY(float x, float y) const noexcept {
        int bestIdx = 4; // AH fallback
        float bestDistSq = 1.0e9f;
        for (int i = 0; i < kNumPhonemes; ++i) {
            const auto& n = kPhonemeNodes[static_cast<size_t>(i)];
            const float dx = x - n.x;
            const float dy = y - n.y;
            const float dSq = dx * dx + dy * dy;
            if (dSq < bestDistSq) {
                bestDistSq = dSq;
                bestIdx = i;
            }
        }
        return bestIdx;
    }

    void commitXY(float x, float y) {
        snap_.baseVowelX = std::clamp(x, 0.0f, 1.0f);
        snap_.baseVowelY = std::clamp(y, 0.0f, 1.0f);
        if (onVowelXYChanged)
            onVowelXYChanged(snap_.baseVowelX, snap_.baseVowelY);
        repaint();
    }

    // Analytical parallel 5-formant bandpass magnitude response in dB
    float evalFormantResponseDb(float freqHz) const noexcept {
        float powerSum = 0.0f;
        for (int k = 0; k < kNumFormants; ++k) {
            const float fk = std::clamp(snap_.formantFreqHz[static_cast<size_t>(k)], 60.0f, 14000.0f);
            const float qk = std::clamp(snap_.formantQ[static_cast<size_t>(k)], 0.5f, 40.0f);
            const float gk = std::max(0.0f, snap_.formantGain[static_cast<size_t>(k)]);
            const float detune = (freqHz / fk) - (fk / freqHz);
            const float denom = 1.0f + (qk * qk) * (detune * detune);
            powerSum += (gk * gk) / denom;
        }
        const float mag = std::sqrt(std::max(powerSum, 1.0e-8f));
        const float db = 20.0f * std::log10(mag) + 3.0f;
        return std::clamp(db, kMinDb, kMaxDb);
    }

    void paintXYPad(juce::Graphics& g, juce::Rectangle<float> bounds) const {
        if (bounds.isEmpty())
            return;

        // Canvas background with subtle violet radial studio glow
        juce::ColourGradient bgGrad(suite::ui::bgPanel.brighter(0.04f), bounds.getCentreX(), bounds.getCentreY(),
                                    suite::ui::bgDark, bounds.getX(), bounds.getBottom(), true);
        g.setGradientFill(bgGrad);
        g.fillRoundedRectangle(bounds, 6.0f);

        g.setColour(suite::ui::border);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

        const auto plot = getXYInnerPlot();

        // Subtle grid lines
        g.setColour(suite::ui::border.withAlpha(0.32f));
        for (int i = 1; i <= 3; ++i) {
            const float t = static_cast<float>(i) * 0.25f;
            const float gx = plot.getX() + t * plot.getWidth();
            const float gy = plot.getY() + t * plot.getHeight();
            g.drawLine(gx, plot.getY(), gx, plot.getBottom(), 0.75f);
            g.drawLine(plot.getX(), gy, plot.getRight(), gy, 0.75f);
        }

        // Axis edge labels
        g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        g.setColour(suite::ui::textMuted.withAlpha(0.55f));
        g.drawText("FRONT", static_cast<int>(bounds.getX() + 8.0f), static_cast<int>(bounds.getCentreY() - 7.0f),
                   44, 14, juce::Justification::centredLeft, false);
        g.drawText("BACK", static_cast<int>(bounds.getRight() - 52.0f), static_cast<int>(bounds.getCentreY() - 7.0f),
                   44, 14, juce::Justification::centredRight, false);
        g.drawText("CLOSE", static_cast<int>(bounds.getCentreX() - 28.0f), static_cast<int>(bounds.getY() + 2.0f),
                   56, 12, juce::Justification::centred, false);
        g.drawText("OPEN", static_cast<int>(bounds.getCentreX() - 28.0f), static_cast<int>(bounds.getBottom() - 14.0f),
                   56, 12, juce::Justification::centred, false);

        // IPA vowel trapezoid/triangle guide lines
        const auto pEE = xyToPixel(kPhonemeNodes[0].x, kPhonemeNodes[0].y);
        const auto pEH = xyToPixel(kPhonemeNodes[2].x, kPhonemeNodes[2].y);
        const auto pAE = xyToPixel(kPhonemeNodes[3].x, kPhonemeNodes[3].y);
        const auto pAH = xyToPixel(kPhonemeNodes[4].x, kPhonemeNodes[4].y);
        const auto pAW = xyToPixel(kPhonemeNodes[5].x, kPhonemeNodes[5].y);
        const auto pOH = xyToPixel(kPhonemeNodes[6].x, kPhonemeNodes[6].y);
        const auto pOO = xyToPixel(kPhonemeNodes[7].x, kPhonemeNodes[7].y);
        const auto pUH = xyToPixel(kPhonemeNodes[8].x, kPhonemeNodes[8].y);
        const auto pMM = xyToPixel(kPhonemeNodes[9].x, kPhonemeNodes[9].y);

        juce::Path trap;
        trap.startNewSubPath(pEE);
        trap.lineTo(pEH);
        trap.lineTo(pAE);
        trap.lineTo(pAH);
        trap.lineTo(pAW);
        trap.lineTo(pOH);
        trap.lineTo(pOO);
        trap.closeSubPath();

        g.setColour(suite::ui::accentViolet.withAlpha(0.055f));
        g.fillPath(trap);
        g.setColour(suite::ui::accentViolet.withAlpha(0.28f));
        g.strokePath(trap, juce::PathStrokeType(1.25f));

        // Inner structural guide lines through UH & nasal branch to MM
        g.setColour(suite::ui::border.withAlpha(0.45f));
        g.drawLine(pEH.x, pEH.y, pOH.x, pOH.y, 0.85f);
        g.drawLine(pUH.x, pUH.y, pAH.x, pAH.y, 0.85f);

        const float nasalDash[2] = { 3.0f, 3.0f };
        g.setColour(suite::ui::accentViolet.withAlpha(0.35f));
        g.drawDashedLine({ pOO, pMM }, nasalDash, 2, 1.0f);

        // Determine nearest active phoneme
        const int nearestPhoneme = (snap_.activePhoneme >= 0 && snap_.activePhoneme < kNumPhonemes)
                                       ? snap_.activePhoneme
                                       : findNearestPhonemeToXY(snap_.liveVowelX, snap_.liveVowelY);

        // Draw 10 phoneme anchor badges
        for (int i = 0; i < kNumPhonemes; ++i) {
            const auto& node = kPhonemeNodes[static_cast<size_t>(i)];
            const auto pt = xyToPixel(node.x, node.y);
            const bool isActive = (i == nearestPhoneme);
            const bool isHovered = (i == hoveredNode_);

            if (isActive) {
                g.setColour(suite::ui::accentViolet.withAlpha(0.24f));
                g.fillEllipse(pt.x - 17.0f, pt.y - 17.0f, 34.0f, 34.0f);
                g.setColour(suite::ui::accentCyan.withAlpha(0.45f));
                g.drawEllipse(pt.x - 14.0f, pt.y - 14.0f, 28.0f, 28.0f, 1.2f);
            } else if (isHovered) {
                g.setColour(suite::ui::accentViolet.withAlpha(0.22f));
                g.fillEllipse(pt.x - 14.0f, pt.y - 14.0f, 28.0f, 28.0f);
            }

            const float badgeW = 24.0f;
            const float badgeH = 16.0f;
            const juce::Rectangle<float> badge(pt.x - badgeW * 0.5f, pt.y - badgeH * 0.5f, badgeW, badgeH);

            g.setColour(isActive ? suite::ui::bgCard.brighter(0.18f) : suite::ui::bgPanel.withAlpha(0.92f));
            g.fillRoundedRectangle(badge, 4.0f);
            g.setColour(isActive ? suite::ui::accentCyan
                                 : (isHovered ? suite::ui::accentViolet : suite::ui::border.brighter(0.2f)));
            g.drawRoundedRectangle(badge, 4.0f, isActive ? 1.3f : 0.9f);

            g.setFont(juce::FontOptions(10.0f, isActive ? juce::Font::bold : juce::Font::plain));
            g.setColour(isActive ? suite::ui::textPrimary : suite::ui::textMuted);
            g.drawText(phonemeCode(i), badge.toNearestInt(), juce::Justification::centred, false);
        }

        // 24-point fading modulation trail
        if (trailCount_ > 1) {
            const int startIdx = (trailWriteIdx_ - trailCount_ + kTrailLen) % kTrailLen;
            for (int i = 1; i < trailCount_; ++i) {
                const int idx0 = (startIdx + i - 1) % kTrailLen;
                const int idx1 = (startIdx + i) % kTrailLen;
                const auto p0 = xyToPixel(trail_[static_cast<size_t>(idx0)].x, trail_[static_cast<size_t>(idx0)].y);
                const auto p1 = xyToPixel(trail_[static_cast<size_t>(idx1)].x, trail_[static_cast<size_t>(idx1)].y);
                const float frac = static_cast<float>(i) / static_cast<float>(trailCount_);
                const float alpha = frac * frac * 0.72f;
                const float thickness = 1.0f + frac * 2.2f;
                g.setColour(suite::ui::accentCyan.withAlpha(alpha));
                g.drawLine(p0.x, p0.y, p1.x, p1.y, thickness);
            }
        }

        const auto basePt = xyToPixel(snap_.baseVowelX, snap_.baseVowelY);
        const auto livePt = xyToPixel(snap_.liveVowelX, snap_.liveVowelY);

        // Dashed vector connecting base -> live modulated cursor
        if (basePt.getDistanceFrom(livePt) > 2.0f) {
            const float vecDash[2] = { 4.0f, 3.0f };
            g.setColour(suite::ui::accentCyan.withAlpha(0.65f));
            g.drawDashedLine({ basePt, livePt }, vecDash, 2, 1.3f);
        }

        // Base user draggable target ring (violet)
        g.setColour(suite::ui::accentViolet.withAlpha(0.22f));
        g.fillEllipse(basePt.x - 11.0f, basePt.y - 11.0f, 22.0f, 22.0f);
        g.setColour(suite::ui::accentViolet);
        g.drawEllipse(basePt.x - 8.5f, basePt.y - 8.5f, 17.0f, 17.0f, 2.0f);
        g.setColour(suite::ui::textPrimary.withAlpha(0.85f));
        g.fillEllipse(basePt.x - 2.0f, basePt.y - 2.0f, 4.0f, 4.0f);

        // Live modulated cursor (cyan glow)
        g.setColour(suite::ui::accentCyan.withAlpha(0.24f));
        g.fillEllipse(livePt.x - 12.0f, livePt.y - 12.0f, 24.0f, 24.0f);
        g.setColour(suite::ui::accentCyan.withAlpha(0.55f));
        g.fillEllipse(livePt.x - 6.5f, livePt.y - 6.5f, 13.0f, 13.0f);
        g.setColour(suite::ui::textPrimary);
        g.fillEllipse(livePt.x - 3.0f, livePt.y - 3.0f, 6.0f, 6.0f);

        // Top-left title tag & Top-right active syllable pill
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.setColour(suite::ui::textMuted.withAlpha(0.75f));
        g.drawText("IPA VOWEL SPACE", static_cast<int>(bounds.getX() + 10.0f),
                   static_cast<int>(bounds.getY() + 4.0f), 110, 14, juce::Justification::centredLeft, false);

        const juce::String pillText = juce::String("[") + phonemeCode(nearestPhoneme)
                                      + " " + phonemeIpa(nearestPhoneme) + "]";
        const juce::Rectangle<float> pill(bounds.getRight() - 78.0f, bounds.getY() + 4.0f, 70.0f, 18.0f);
        g.setColour(suite::ui::accentViolet.withAlpha(0.22f));
        g.fillRoundedRectangle(pill, 9.0f);
        g.setColour(suite::ui::accentViolet.withAlpha(0.75f));
        g.drawRoundedRectangle(pill, 9.0f, 1.0f);
        g.setColour(suite::ui::textPrimary);
        g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        g.drawText(pillText, pill.toNearestInt(), juce::Justification::centred, false);
    }

    void paintFormantCurve(juce::Graphics& g, juce::Rectangle<float> bounds) const {
        if (bounds.isEmpty())
            return;

        g.setColour(suite::ui::bgDark);
        g.fillRoundedRectangle(bounds, 6.0f);
        g.setColour(suite::ui::border);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

        // Reserve bottom 18px for live F1..F5 Hz readout bar
        const float readoutH = 18.0f;
        const auto plot = bounds.reduced(8.0f, 6.0f).withTrimmedBottom(readoutH);
        const auto readoutRect = bounds.reduced(8.0f, 2.0f).removeFromBottom(readoutH);

        if (plot.getWidth() < 20.0f || plot.getHeight() < 20.0f)
            return;

        const float logMin = std::log10(kMinFreqHz);
        const float logMax = std::log10(kMaxFreqHz);
        const auto freqToX = [&](float f) {
            const float lf = std::log10(std::clamp(f, kMinFreqHz, kMaxFreqHz));
            return plot.getX() + ((lf - logMin) / (logMax - logMin)) * plot.getWidth();
        };
        const auto dbToY = [&](float db) {
            const float norm = (std::clamp(db, kMinDb, kMaxDb) - kMinDb) / (kMaxDb - kMinDb);
            return plot.getBottom() - norm * plot.getHeight();
        };

        // Horizontal dB grid lines
        g.setColour(suite::ui::border.withAlpha(0.28f));
        constexpr std::array<float, 3> kDbGrid { 0.0f, -12.0f, -24.0f };
        for (float db : kDbGrid) {
            const float gy = dbToY(db);
            g.drawLine(plot.getX(), gy, plot.getRight(), gy, 0.75f);
        }

        // Vertical log-frequency grid lines
        constexpr std::array<float, 6> kFreqGrid { 100.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f };
        constexpr std::array<const char*, 6> kFreqLabels { "100", "250", "500", "1k", "2k", "4k" };
        g.setFont(juce::FontOptions(9.0f, juce::Font::plain));
        for (size_t i = 0; i < kFreqGrid.size(); ++i) {
            const float gx = freqToX(kFreqGrid[i]);
            g.setColour(suite::ui::border.withAlpha(0.28f));
            g.drawLine(gx, plot.getY(), gx, plot.getBottom(), 0.75f);
            g.setColour(suite::ui::textMuted.withAlpha(0.55f));
            g.drawText(kFreqLabels[i], static_cast<int>(gx) - 12, static_cast<int>(plot.getBottom() - 11.0f),
                       24, 10, juce::Justification::centred, false);
        }

        // 128-point smooth log-frequency formant curve
        constexpr int kNumPoints = 128;
        juce::Path curvePath;
        juce::Path fillPath;

        for (int i = 0; i < kNumPoints; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(kNumPoints - 1);
            const float freq = std::pow(10.0f, logMin + t * (logMax - logMin));
            const float db = evalFormantResponseDb(freq);
            const float px = plot.getX() + t * plot.getWidth();
            const float py = dbToY(db);

            if (i == 0) {
                curvePath.startNewSubPath(px, py);
                fillPath.startNewSubPath(px, plot.getBottom());
                fillPath.lineTo(px, py);
            } else {
                curvePath.lineTo(px, py);
                fillPath.lineTo(px, py);
            }
        }
        fillPath.lineTo(plot.getRight(), plot.getBottom());
        fillPath.closeSubPath();

        juce::ColourGradient fillGrad(suite::ui::accentViolet.withAlpha(0.32f), plot.getCentreX(), plot.getY(),
                                      suite::ui::accentCyan.withAlpha(0.03f), plot.getCentreX(), plot.getBottom(), false);
        g.setGradientFill(fillGrad);
        g.fillPath(fillPath);

        // Glow stroke + crisp curve stroke
        g.setColour(suite::ui::accentCyan.withAlpha(0.28f));
        g.strokePath(curvePath, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(suite::ui::accentCyan);
        g.strokePath(curvePath, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Glowing F1..F5 vertical peak pins
        const std::array<juce::Colour, kNumFormants> kPinColours {
            suite::ui::accentViolet,
            suite::ui::accentCyan,
            suite::ui::accentEmerald,
            suite::ui::accentAmber,
            suite::ui::accentCoral
        };

        g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        for (int k = 0; k < kNumFormants; ++k) {
            const float fk = std::clamp(snap_.formantFreqHz[static_cast<size_t>(k)], kMinFreqHz, kMaxFreqHz);
            const float px = freqToX(fk);
            const float py = dbToY(evalFormantResponseDb(fk));
            const auto col = kPinColours[static_cast<size_t>(k)];

            g.setColour(col.withAlpha(0.35f));
            g.drawLine(px, py, px, plot.getBottom(), 1.2f);
            g.setColour(col.withAlpha(0.30f));
            g.fillEllipse(px - 6.0f, py - 6.0f, 12.0f, 12.0f);
            g.setColour(col);
            g.fillEllipse(px - 3.0f, py - 3.0f, 6.0f, 6.0f);

            const float labelY = std::max(plot.getY(), py - 14.0f);
            g.setColour(col.brighter(0.25f));
            g.drawText("F" + juce::String(k + 1), static_cast<int>(px) - 10, static_cast<int>(labelY),
                       20, 11, juce::Justification::centred, false);
        }

        // Bottom live Hz readout bar
        g.setColour(suite::ui::border.withAlpha(0.5f));
        g.drawLine(readoutRect.getX(), readoutRect.getY(), readoutRect.getRight(), readoutRect.getY(), 0.75f);

        const juce::String sep = "  " + juce::String::charToString(0x00b7) + "  ";
        juce::String readout;
        for (int k = 0; k < kNumFormants; ++k) {
            if (k > 0)
                readout += sep;
            const int hz = static_cast<int>(std::round(snap_.formantFreqHz[static_cast<size_t>(k)]));
            readout += "F" + juce::String(k + 1) + " " + juce::String(hz) + "Hz";
        }

        g.setFont(juce::FontOptions(10.0f, juce::Font::plain));
        g.setColour(suite::ui::textMuted);
        g.drawFittedText(readout, readoutRect.toNearestInt(), juce::Justification::centredLeft, 1);
    }

    vowelmorph::TelemetrySnapshot snap_{};
    std::array<juce::Point<float>, kTrailLen> trail_{};
    int trailWriteIdx_ = 0;
    int trailCount_ = 0;
    int hoveredNode_ = -1;
    bool isDragging_ = false;

    juce::Rectangle<float> xyPadBounds_;
    juce::Rectangle<float> formantBounds_;
};

} // namespace vowelmorph::ui
