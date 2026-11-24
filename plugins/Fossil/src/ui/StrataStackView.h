#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../FossilParams.h"
#include "FossilPalette.h"

#include <array>
#include <cmath>
#include <functional>
#include <memory>

namespace fossil::ui
{

// ============================================================================
// StrataStackView — 4 vertical geological strata (L4 top .. L1 bottom)
// with live waveform ribbons, playheads, drag-to-swap, M/S/Clear, and XFade rail
// ============================================================================
class StrataStackView : public juce::Component
{
public:
    StrataStackView (juce::AudioProcessorValueTreeState& apvts,
                     const FossilTelemetry& telemetry)
        : apvts_ (apvts), telemetry_ (telemetry)
    {
        for (int slot = 0; slot < kNumStrata; ++slot)
        {
            auto& row = rowControls_[static_cast<size_t> (slot)];

            row.muteBtn.setButtonText ("M");
            row.muteBtn.setClickingTogglesState (true);
            row.muteBtn.setColour (juce::TextButton::buttonOnColourId, accentT);
            row.muteBtn.setColour (juce::TextButton::textColourOnId, textPrimary);
            row.muteBtn.setTooltip ("Mute Stratum L" + juce::String (slot + 1));
            addAndMakeVisible (row.muteBtn);

            row.soloBtn.setButtonText ("S");
            row.soloBtn.setClickingTogglesState (true);
            row.soloBtn.setColour (juce::TextButton::buttonOnColourId, accentEmerald);
            row.soloBtn.setColour (juce::TextButton::textColourOnId, textPrimary);
            row.soloBtn.setTooltip ("Solo Stratum L" + juce::String (slot + 1));
            addAndMakeVisible (row.soloBtn);

            row.clearBtn.setButtonText (juce::String::fromUTF8 ("\xc3\x97")); // ×
            row.clearBtn.setColour (juce::TextButton::textColourOffId, accentCoral.withAlpha (0.85f));
            row.clearBtn.setTooltip ("Clear Stratum L" + juce::String (slot + 1));
            row.clearBtn.onClick = [this, slot]()
            {
                if (onClearLayer)
                    onClearLayer (slot);
            };
            addAndMakeVisible (row.clearBtn);

            if (apvts_.getParameter (param_ids::layerMute[static_cast<size_t> (slot)]) != nullptr)
                row.muteAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                    apvts_, param_ids::layerMute[static_cast<size_t> (slot)], row.muteBtn);

            if (apvts_.getParameter (param_ids::layerSolo[static_cast<size_t> (slot)]) != nullptr)
                row.soloAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                    apvts_, param_ids::layerSolo[static_cast<size_t> (slot)], row.soloBtn);
        }

        // Crossfade Focus vertical rail on right edge
        xfadeTitle_.setText ("XFADE", juce::dontSendNotification);
        xfadeTitle_.setJustificationType (juce::Justification::centred);
        xfadeTitle_.setFont (makeFont (9.5f, true));
        xfadeTitle_.setColour (juce::Label::textColourId, textMuted);
        addAndMakeVisible (xfadeTitle_);

        xfadePosSlider_.setSliderStyle (juce::Slider::SliderStyle::LinearVertical);
        xfadePosSlider_.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        xfadePosSlider_.setColour (juce::Slider::thumbColourId, accentH);
        xfadePosSlider_.setColour (juce::Slider::trackColourId, accentH);
        xfadePosSlider_.setTooltip ("Crossfade Focus Position (L1 bottom to L4 top)");
        addAndMakeVisible (xfadePosSlider_);

        if (apvts_.getParameter (param_ids::crossfadePos) != nullptr)
            xfadePosAttach_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts_, param_ids::crossfadePos, xfadePosSlider_);

        focusKnob_ = std::make_unique<KnobControl> (
            apvts_, param_ids::crossfadeDepth, "FOCUS", accentH, "%");
        addAndMakeVisible (*focusKnob_);

        refreshFromTelemetry();
    }

    std::function<void (int)>      onLayerSelected;
    std::function<void (int, int)> onSwapLayers;
    std::function<void (int)>      onClearLayer;

    void setSelectedLayer (int slot)
    {
        const int clamped = juce::jlimit (0, kNumStrata - 1, slot);
        if (selectedLayer_ != clamped)
        {
            selectedLayer_ = clamped;
            repaint();
        }
    }

    int getSelectedLayer() const noexcept { return selectedLayer_; }

    void refreshFromTelemetry()
    {
        shimmerPhase_ += 0.14f;
        if (shimmerPhase_ > juce::MathConstants<float>::twoPi)
            shimmerPhase_ -= juce::MathConstants<float>::twoPi;

        for (int slot = 0; slot < kNumStrata; ++slot)
        {
            const auto& src = telemetry_.strata[static_cast<size_t> (slot)];
            auto& dst = cached_[static_cast<size_t> (slot)];

            dst.frozen          = src.frozen.load (std::memory_order_relaxed);
            dst.source          = static_cast<StreamSource> (
                juce::jlimit (0, 2, src.source.load (std::memory_order_relaxed)));
            dst.outputLevel     = juce::jlimit (0.0f, 1.5f, src.outputLevel.load (std::memory_order_relaxed));
            dst.playheadNorm    = juce::jlimit (0.0f, 1.0f, src.playheadNorm.load (std::memory_order_relaxed));
            dst.effectiveWeight = juce::jlimit (0.0f, 1.0f, src.effectiveWeight.load (std::memory_order_relaxed));

            for (int i = 0; i < kWavePreviewBins; ++i)
                dst.wave[static_cast<size_t> (i)] = juce::jlimit (
                    0.0f, 1.0f, src.wavePreview[static_cast<size_t> (i)].load (std::memory_order_relaxed));
        }

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();

        // Outer container card
        g.setColour (bgPanel);
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (border);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

        // Header
        if (! headerBounds_.isEmpty())
        {
            g.setFont (makeFont (11.0f, true));
            g.setColour (textMuted);
            g.drawText ("GEOLOGICAL STRATA STACK  (DRAG TO REORDER)",
                        headerBounds_.reduced (4, 0), juce::Justification::centredLeft, false);
        }

        // Draw crossfade rail backdrop
        if (! railBounds_.isEmpty())
        {
            const auto rb = railBounds_.toFloat();
            g.setColour (bgDeep.withAlpha (0.72f));
            g.fillRoundedRectangle (rb, 6.0f);
            g.setColour (border.withAlpha (0.75f));
            g.drawRoundedRectangle (rb.reduced (0.5f), 6.0f, 1.0f);

            // Visual crossfade focus window highlight behind slider
            const float depth = readParam (param_ids::crossfadeDepth, 0.0f);
            if (depth > 0.01f && ! xfadePosSlider_.getBounds().isEmpty())
            {
                const auto sb = xfadePosSlider_.getBounds().toFloat();
                const float pos = juce::jlimit (0.0f, 1.0f, readParam (param_ids::crossfadePos, 0.5f));
                const float cy = sb.getBottom() - pos * sb.getHeight();
                const float halfSpan = sb.getHeight() * juce::jmap (depth, 0.0f, 1.0f, 0.50f, 0.14f);
                const auto focusRect = juce::Rectangle<float> (
                    sb.getX() + 4.0f,
                    juce::jmax (sb.getY(), cy - halfSpan),
                    sb.getWidth() - 8.0f,
                    juce::jmin (sb.getBottom(), cy + halfSpan) - juce::jmax (sb.getY(), cy - halfSpan));

                g.setColour (accentH.withAlpha (0.14f + 0.14f * depth));
                g.fillRoundedRectangle (focusRect, 4.0f);
            }
        }

        // Draw the 4 strata rows (visualRow 0 = L4/slot 3 down to visualRow 3 = L1/slot 0)
        for (int visualRow = 0; visualRow < kNumStrata; ++visualRow)
        {
            const int slot = (kNumStrata - 1) - visualRow;
            drawStratumRow (g, slot, rowBounds_[static_cast<size_t> (slot)]);
        }
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (isDragging_ && dragFromSlot_ >= 0 && dropTargetSlot_ >= 0 && dropTargetSlot_ != dragFromSlot_)
        {
            const auto targetRect = rowBounds_[static_cast<size_t> (dropTargetSlot_)].toFloat();
            g.setColour (accentH.withAlpha (0.16f));
            g.fillRoundedRectangle (targetRect, 6.0f);
            g.setColour (accentH);
            g.drawRoundedRectangle (targetRect.reduced (1.0f), 6.0f, 2.2f);

            // Bright swap indicator bar
            const bool movingUp = (dropTargetSlot_ > dragFromSlot_);
            const float barY = movingUp ? targetRect.getY() + 1.5f : targetRect.getBottom() - 1.5f;
            g.setColour (accentH.brighter (0.25f));
            g.drawLine (targetRect.getX() + 8.0f, barY, targetRect.getRight() - 8.0f, barY, 3.0f);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8);

        headerBounds_ = area.removeFromTop (18);
        area.removeFromTop (4);

        // Right-edge Crossfade Focus rail
        const int railW = juce::jlimit (50, 62, area.getWidth() / 9);
        railBounds_ = area.removeFromRight (railW);
        area.removeFromRight (6);

        // Layout rail contents
        {
            auto r = railBounds_.reduced (4, 5);
            xfadeTitle_.setBounds (r.removeFromTop (14));
            r.removeFromTop (2);
            const int knobH = juce::jmin (64, r.getHeight() / 3);
            focusKnob_->setBounds (r.removeFromBottom (knobH));
            r.removeFromBottom (4);
            xfadePosSlider_.setBounds (r);
        }

        // Stack 4 strata rows vertically: L4 (slot 3) at top .. L1 (slot 0) at bottom
        const int gap = 5;
        const int totalGap = gap * (kNumStrata - 1);
        const int rowH = juce::jmax (34, (area.getHeight() - totalGap) / kNumStrata);

        for (int visualRow = 0; visualRow < kNumStrata; ++visualRow)
        {
            const int slot = (kNumStrata - 1) - visualRow;
            auto rowRect = area.removeFromTop (rowH);
            if (visualRow < kNumStrata - 1)
                area.removeFromTop (gap);

            rowBounds_[static_cast<size_t> (slot)] = rowRect;

            // Place [M] [S] [×] buttons on right side of row
            auto& ctrl = rowControls_[static_cast<size_t> (slot)];
            const int btnSize = juce::jlimit (20, 24, rowRect.getHeight() - 14);
            const int btnY = rowRect.getCentreY() - btnSize / 2;
            int rightX = rowRect.getRight() - 8;

            ctrl.clearBtn.setBounds (rightX - btnSize, btnY, btnSize, btnSize);
            rightX -= (btnSize + 3);
            ctrl.soloBtn.setBounds (rightX - btnSize, btnY, btnSize, btnSize);
            rightX -= (btnSize + 3);
            ctrl.muteBtn.setBounds (rightX - btnSize, btnY, btnSize, btnSize);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int slot = slotAtPoint (e.getPosition());
        if (slot < 0)
            return;

        dragFromSlot_   = slot;
        dropTargetSlot_ = slot;
        isDragging_     = false;

        setSelectedLayer (slot);
        if (onLayerSelected)
            onLayerSelected (slot);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragFromSlot_ < 0)
            return;

        if (! isDragging_ && e.getDistanceFromDragStart() > 6)
            isDragging_ = true;

        if (isDragging_)
        {
            const int hovered = slotAtY (e.getPosition().y);
            if (hovered != dropTargetSlot_)
            {
                dropTargetSlot_ = hovered;
                repaint();
            }
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (isDragging_ && dragFromSlot_ >= 0 && dropTargetSlot_ >= 0 && dropTargetSlot_ != dragFromSlot_)
        {
            const int from = dragFromSlot_;
            const int to   = dropTargetSlot_;
            isDragging_     = false;
            dragFromSlot_   = -1;
            dropTargetSlot_ = -1;

            setSelectedLayer (to);
            if (onSwapLayers)
                onSwapLayers (from, to);
            if (onLayerSelected)
                onLayerSelected (to);

            repaint();
            return;
        }

        isDragging_     = false;
        dragFromSlot_   = -1;
        dropTargetSlot_ = -1;
        repaint();
    }

private:
    struct CachedStratum
    {
        bool frozen = false;
        StreamSource source = StreamSource::Tonal;
        float outputLevel = 0.0f;
        float playheadNorm = 0.0f;
        float effectiveWeight = 1.0f;
        std::array<float, kWavePreviewBins> wave {};
    };

    struct RowControls
    {
        juce::TextButton muteBtn;
        juce::TextButton soloBtn;
        juce::TextButton clearBtn;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAttach;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloAttach;
    };

    float readParam (const char* paramId, float fallback) const noexcept
    {
        if (auto* raw = apvts_.getRawParameterValue (paramId))
            return raw->load (std::memory_order_relaxed);
        return fallback;
    }

    int slotAtPoint (juce::Point<int> pt) const noexcept
    {
        for (int slot = 0; slot < kNumStrata; ++slot)
            if (rowBounds_[static_cast<size_t> (slot)].contains (pt))
                return slot;
        return -1;
    }

    int slotAtY (int y) const noexcept
    {
        int bestSlot = -1;
        int bestDist = 1000000;
        for (int slot = 0; slot < kNumStrata; ++slot)
        {
            const int cy = rowBounds_[static_cast<size_t> (slot)].getCentreY();
            const int dist = std::abs (y - cy);
            if (dist < bestDist)
            {
                bestDist = dist;
                bestSlot = slot;
            }
        }
        return bestSlot;
    }

    juce::String buildMetadataText (int slot) const
    {
        const auto idx = static_cast<size_t> (slot);
        const float pitch = readParam (param_ids::layerPitch[idx], 0.0f);
        const float grain = readParam (param_ids::layerGrain[idx], 180.0f);
        const float tilt  = readParam (param_ids::layerTilt[idx], 0.0f);
        const float width = readParam (param_ids::layerWidth[idx], 1.0f);
        const bool gated  = readParam (param_ids::layerGate[idx], 0.0f) >= 0.5f;

        juce::StringArray parts;

        // Pitch
        const int pitchInt = juce::roundToInt (pitch);
        const juce::String pitchSign = (pitch > 0.05f) ? "+" : "";
        if (std::abs (pitch - static_cast<float> (pitchInt)) < 0.08f)
            parts.add (pitchSign + juce::String (pitchInt) + " st");
        else
            parts.add (pitchSign + juce::String (pitch, 1) + " st");

        // Grain or character tag
        parts.add (juce::String (juce::roundToInt (grain)) + " ms");

        if (gated)
            parts.add ("Gate");
        else if (width > 1.35f)
            parts.add ("Wide");
        else if (tilt < -0.25f)
            parts.add ("Warm");
        else if (tilt > 0.25f)
            parts.add ("Bright");

        return parts.joinIntoString (juce::String::fromUTF8 (" \xc2\xb7 "));
    }

    void drawStratumRow (juce::Graphics& g, int slot, juce::Rectangle<int> boundsInt)
    {
        if (boundsInt.isEmpty())
            return;

        const auto idx = static_cast<size_t> (slot);
        const auto& st = cached_[idx];
        const bool isSelected = (slot == selectedLayer_);
        const bool isMuted = readParam (param_ids::layerMute[idx], 0.0f) >= 0.5f;
        const bool isSolo  = readParam (param_ids::layerSolo[idx], 0.0f) >= 0.5f;

        // Use frozen source if frozen, otherwise APVTS configured source
        const StreamSource displaySource = st.frozen
            ? st.source
            : static_cast<StreamSource> (juce::jlimit (0, 2, juce::roundToInt (readParam (param_ids::layerSource[idx], 1.0f))));
        const auto accent = colourForSource (displaySource);

        const auto bounds = boundsInt.toFloat();
        const float alphaMod = isMuted ? 0.42f : juce::jlimit (0.45f, 1.0f, 0.45f + 0.55f * st.effectiveWeight);

        // Card fill
        g.setColour (isSelected ? bgCardSelected : bgCard);
        g.fillRoundedRectangle (bounds, 6.0f);

        // Subtle source tint when frozen
        if (st.frozen)
        {
            g.setColour (accent.withAlpha ((isSelected ? 0.11f : 0.05f) * alphaMod));
            g.fillRoundedRectangle (bounds, 6.0f);
        }

        // Card border
        g.setColour (isSelected ? accent.withAlpha (0.85f) : (isSolo ? accentEmerald.withAlpha (0.7f) : border));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, isSelected ? 1.5f : 1.0f);

        // Left selection accent bar
        if (isSelected)
        {
            g.setColour (accent);
            g.fillRoundedRectangle (bounds.getX() + 1.5f, bounds.getY() + 6.0f,
                                    3.0f, juce::jmax (4.0f, bounds.getHeight() - 12.0f), 1.5f);
        }

        // Layout regions inside row:
        // [Badge (L1 + Source)] | [Waveform Ribbon] | [Metadata] | [M S × buttons]
        auto inner = bounds.reduced (8.0f, 5.0f);
        const auto& ctrl = rowControls_[idx];
        const float buttonsW = bounds.getRight() - static_cast<float> (ctrl.muteBtn.getX()) + 6.0f;
        inner.removeFromRight (juce::jmax (74.0f, buttonsW));

        // Left pill badge
        const float badgeW = 68.0f;
        auto badgeRect = inner.removeFromLeft (badgeW);
        inner.removeFromLeft (8.0f);

        {
            auto pill = badgeRect.withSizeKeepingCentre (badgeW, juce::jmin (26.0f, badgeRect.getHeight()));
            g.setColour (bgDeep.withAlpha (0.85f));
            g.fillRoundedRectangle (pill, 5.0f);
            g.setColour (accent.withAlpha (st.frozen ? 0.75f : 0.35f));
            g.drawRoundedRectangle (pill, 5.0f, 1.0f);

            // L1..L4 text
            auto leftPart = pill.removeFromLeft (32.0f);
            g.setFont (makeFont (11.5f, true));
            g.setColour (isSelected ? textPrimary : textMuted);
            g.drawText ("L" + juce::String (slot + 1) + (isSelected ? "*" : ""),
                        leftPart.toNearestInt(), juce::Justification::centred, false);

            // Colored source tag pill (T / H / N)
            auto tagPill = pill.reduced (3.0f, 3.5f);
            g.setColour (accent.withAlpha (st.frozen ? 0.28f : 0.14f));
            g.fillRoundedRectangle (tagPill, 3.5f);
            g.setColour (accent.withAlpha (alphaMod));
            g.setFont (makeFont (11.0f, true));
            g.drawText (sourceShortName (displaySource), tagPill.toNearestInt(),
                        juce::Justification::centred, false);
        }

        // Right inline metadata tag (only when wide enough)
        const float metaW = (inner.getWidth() > 210.0f) ? 128.0f : 0.0f;
        juce::Rectangle<float> metaRect;
        if (metaW > 0.0f)
        {
            metaRect = inner.removeFromRight (metaW);
            inner.removeFromRight (6.0f);
        }

        // Center live geological waveform ribbon
        auto waveRect = inner;
        if (st.frozen && waveRect.getWidth() > 16.0f)
        {
            drawWaveformRibbon (g, waveRect, st, accent, alphaMod);
        }
        else if (waveRect.getWidth() > 16.0f)
        {
            // Muted dashed outline for empty stratum
            juce::Path outline;
            outline.addRoundedRectangle (waveRect.reduced (1.0f), 4.0f);
            juce::Path dashed;
            const float dashLengths[2] = { 5.0f, 4.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashLengths, 2);
            g.setColour (borderBright.withAlpha (0.55f));
            g.fillPath (dashed);

            g.setFont (makeFont (10.5f, false));
            g.setColour (textMuted.withAlpha (0.72f));
            g.drawFittedText (juce::String::fromUTF8 ("Empty \xe2\x80\x94 Tap Freeze T / H / N to Capture"),
                              waveRect.reduced (6.0f, 2.0f).toNearestInt(),
                              juce::Justification::centred, 1);
        }

        // Metadata readout
        if (metaW > 0.0f)
        {
            g.setFont (makeFont (10.0f, false));
            g.setColour (st.frozen ? textPrimary.withAlpha (0.85f * alphaMod) : textMuted.withAlpha (0.55f));
            g.drawFittedText (st.frozen ? buildMetadataText (slot) : sourceLongName (displaySource),
                              metaRect.toNearestInt(), juce::Justification::centredRight, 1);
        }
    }

    void drawWaveformRibbon (juce::Graphics& g,
                             juce::Rectangle<float> rect,
                             const CachedStratum& st,
                             juce::Colour accent,
                             float alphaMod) const
    {
        g.setColour (bgDeep.withAlpha (0.75f));
        g.fillRoundedRectangle (rect, 4.0f);

        const float midY = rect.getCentreY();
        const float maxAmp = rect.getHeight() * 0.44f;
        const float shimmer = 0.88f + 0.12f * std::sin (shimmerPhase_ + st.playheadNorm * 6.28318f);
        const float levelBoost = juce::jlimit (0.65f, 1.25f, 0.72f + st.outputLevel * 0.55f);

        // Build symmetrical geological strata waveform path
        juce::Path fillPath;
        juce::Path topEdge;
        const float x0 = rect.getX() + 2.0f;
        const float w  = juce::jmax (1.0f, rect.getWidth() - 4.0f);
        const float stepX = w / static_cast<float> (kWavePreviewBins - 1);

        for (int i = 0; i < kWavePreviewBins; ++i)
        {
            const float x = x0 + static_cast<float> (i) * stepX;
            const float sample = juce::jmax (0.06f, st.wave[static_cast<size_t> (i)]);
            const float amp = juce::jmin (maxAmp, sample * maxAmp * levelBoost * shimmer);
            const float yTop = midY - amp;
            if (i == 0)
            {
                fillPath.startNewSubPath (x, yTop);
                topEdge.startNewSubPath (x, yTop);
            }
            else
            {
                fillPath.lineTo (x, yTop);
                topEdge.lineTo (x, yTop);
            }
        }

        for (int i = kWavePreviewBins - 1; i >= 0; --i)
        {
            const float x = x0 + static_cast<float> (i) * stepX;
            const float sample = juce::jmax (0.06f, st.wave[static_cast<size_t> (i)]);
            const float amp = juce::jmin (maxAmp, sample * maxAmp * levelBoost * shimmer);
            fillPath.lineTo (x, midY + amp);
        }
        fillPath.closeSubPath();

        // Translucent geological gradient fill
        juce::ColourGradient grad (
            accent.withAlpha (0.50f * alphaMod), rect.getCentreX(), rect.getY(),
            accent.withAlpha (0.14f * alphaMod), rect.getCentreX(), rect.getBottom(), false);
        g.setGradientFill (grad);
        g.fillPath (fillPath);

        // Crisp upper contour stroke
        g.setColour (accent.withAlpha (0.85f * alphaMod));
        g.strokePath (topEdge, juce::PathStrokeType (1.25f));

        // Subtle centerline
        g.setColour (accent.withAlpha (0.18f * alphaMod));
        g.drawHorizontalLine (juce::roundToInt (midY), x0, x0 + w);

        // Moving playhead glow line
        const float phX = x0 + st.playheadNorm * w;
        g.setColour (accent.withAlpha (0.28f * alphaMod));
        g.drawLine (phX, rect.getY() + 2.0f, phX, rect.getBottom() - 2.0f, 6.0f);
        g.setColour (textPrimary.withAlpha (0.92f * alphaMod));
        g.drawLine (phX, rect.getY() + 2.0f, phX, rect.getBottom() - 2.0f, 1.5f);

        // Ribbon subtle border
        g.setColour (border.withAlpha (0.7f));
        g.drawRoundedRectangle (rect, 4.0f, 0.8f);
    }

    juce::AudioProcessorValueTreeState& apvts_;
    const FossilTelemetry& telemetry_;

    int selectedLayer_ = 0;
    float shimmerPhase_ = 0.0f;

    int dragFromSlot_ = -1;
    int dropTargetSlot_ = -1;
    bool isDragging_ = false;

    juce::Rectangle<int> headerBounds_;
    juce::Rectangle<int> railBounds_;
    std::array<juce::Rectangle<int>, kNumStrata> rowBounds_;
    std::array<RowControls, kNumStrata> rowControls_;
    std::array<CachedStratum, kNumStrata> cached_;

    juce::Label xfadeTitle_;
    juce::Slider xfadePosSlider_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> xfadePosAttach_;
    std::unique_ptr<KnobControl> focusKnob_;
};

} // namespace fossil::ui
