#pragma once

#include "SessionMateLookAndFeel.h"
#include "../SessionMateTypes.h"
#include "../SessionMateUiBridge.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

namespace sessionmate::ui {

class BandSlotComponent : public juce::Component {
public:
    BandSlotComponent(int slotIdx,
                      juce::AudioProcessorValueTreeState& apvtsRef,
                      ISessionMateUiBridge& bridgeRef)
        : slotIndex(slotIdx),
          apvts(apvtsRef),
          bridge(bridgeRef),
          accentColour(Palette::slotAccent(slotIdx)) {
        // Source selector button ([Built-In / VST: ...])
        sourceBtn.setButtonText(bridge.getSlotSourceLabel(slotIndex));
        sourceBtn.setColour(juce::TextButton::buttonColourId, Palette::bgControl);
        sourceBtn.setColour(juce::TextButton::textColourOffId, Palette::textPrimary);
        sourceBtn.setTooltip("Select sound source: Built-In, SFZ/WAV sample, or Hosted VST3");
        sourceBtn.onClick = [this]() { showSourceMenu(); };
        addAndMakeVisible(sourceBtn);

        // Hosted VST3 GUI open button
        guiBtn.setButtonText("GUI");
        guiBtn.setColour(juce::TextButton::buttonColourId, Palette::bgControl);
        guiBtn.setColour(juce::TextButton::textColourOffId, accentColour);
        guiBtn.setEnabled(bridge.hasHostedPluginInSlot(slotIndex));
        guiBtn.setTooltip("Open hosted VST3 plugin window");
        guiBtn.onClick = [this]() {
            selectThisSlot();
            bridge.openSlotPluginGui(slotIndex);
        };
        addAndMakeVisible(guiBtn);

        // Built-In Patch selector (all slots 0..5)
        const int numPatches = numPatchesForSlot(slotIndex);
        for (int i = 0; i < numPatches; ++i) {
            patchBox.addItem(patchNameForSlot(slotIndex, i), i + 1);
        }
        patchBox.setTooltip(slotIndex == 0 ? "Built-In Drum Kit" : "Built-In Instrument Patch");
        addAndMakeVisible(patchBox);

        if (auto* patchParam = apvts.getParameter(ParamID::slotParam(slotIndex, "patch"))) {
            patchAttachment = std::make_unique<juce::ParameterAttachment>(
                *patchParam,
                [this](float newValue) {
                    const int idx = juce::roundToInt(newValue);
                    patchBox.setSelectedId(idx + 1, juce::dontSendNotification);
                },
                nullptr);
            patchAttachment->sendInitialUpdate();
            patchBox.onChange = [this]() {
                if (patchAttachment != nullptr) {
                    const int idx = std::max(0, patchBox.getSelectedId() - 1);
                    patchAttachment->setValueAsCompleteGesture(static_cast<float>(idx));
                }
            };
        }

        // Drum-Map Translator ComboBox (Drums slot 0 only)
        if (slotIndex == 0) {
            for (int m = 0; m < kNumDrumMaps; ++m) {
                drumMapBox.addItem(drumMapName(static_cast<DrumMapPreset>(m)), m + 1);
            }
            drumMapBox.setTooltip("Drum-Map & Articulation Translator for Hosted VST3 / MIDI Out");
            addAndMakeVisible(drumMapBox);
            drumMapAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
                apvts, ParamID::slotParam(0, "drummap"), drumMapBox);
        }

        // Busy rotary knob
        configureRotary(busySlider, accentColour, false);
        busySlider.setTextValueSuffix("%");
        busySlider.textFromValueFunction = [](double v) {
            return juce::String(juce::roundToInt(v * 100.0)) + "%";
        };
        busySlider.setTooltip("Note density / busyness for this player");
        addAndMakeVisible(busySlider);
        busyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts, ParamID::slotParam(slotIndex, "busy"), busySlider);

        configureCaptionLabel(busyLabel, "BUSY");
        addAndMakeVisible(busyLabel);

        // Secondary musical knob (Kick Lock / Space / Call&Resp / Tightness)
        configureRotary(secondarySlider, accentColour.interpolatedWith(Palette::accentCyan, 0.25f), false);
        secondarySlider.textFromValueFunction = [](double v) {
            return juce::String(juce::roundToInt(v * 100.0)) + "%";
        };
        addAndMakeVisible(secondarySlider);
        secondaryAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts, ParamID::slotParam(slotIndex, "kicklock"), secondarySlider);

        configureCaptionLabel(secondaryLabel, secondaryKnobName(slotIndex));
        addAndMakeVisible(secondaryLabel);

        // Slot 5 (Horns) Call & Response toggle button
        if (slotIndex == 5) {
            callRespBtn.setButtonText("Call&Resp");
            callRespBtn.setColour(juce::ToggleButton::tickColourId, accentColour);
            callRespBtn.setTooltip("Play horn/string fills in the spaces between user phrases");
            addAndMakeVisible(callRespBtn);
            callRespAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                apvts, ParamID::slotParam(5, "callresp"), callRespBtn);
        }

        // Mute / Solo / Pattern Lock compact pill toggles
        configurePillToggle(muteBtn, "M", Palette::accentCoral, "Mute player");
        configurePillToggle(soloBtn, "S", Palette::accentAmber, "Solo player");
        configurePillToggle(lockBtn, "Lock", Palette::accentCyan, "Freeze / lock this player's current groove");
        addAndMakeVisible(muteBtn);
        addAndMakeVisible(soloBtn);
        addAndMakeVisible(lockBtn);

        muteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, ParamID::slotParam(slotIndex, "mute"), muteBtn);
        soloAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, ParamID::slotParam(slotIndex, "solo"), soloBtn);
        lockAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, ParamID::slotParam(slotIndex, "lock"), lockBtn);

        // Volume rotary knob with dB readout
        configureRotary(volSlider, accentColour, false);
        volSlider.setTooltip("Channel output volume (dB)");
        addAndMakeVisible(volSlider);
        volAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts, ParamID::slotParam(slotIndex, "vol"), volSlider);

        volReadout.setJustificationType(juce::Justification::centred);
        volReadout.setFont(Palette::font(10.5f, true));
        volReadout.setColour(juce::Label::textColourId, Palette::textPrimary);
        addAndMakeVisible(volReadout);

        // Pan mini-knob (bipolar)
        configureRotary(panSlider, Palette::textMuted.brighter(0.25f), true);
        panSlider.setTooltip("Stereo pan position");
        addAndMakeVisible(panSlider);
        panAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts, ParamID::slotParam(slotIndex, "pan"), panSlider);

        configureCaptionLabel(panLabel, "PAN");
        addAndMakeVisible(panLabel);

        // Fallback warning badge tooltip helper
        fallbackBadge.setInterceptsMouseClicks(true, false);
        fallbackBadge.setTooltip("Hosted VST3 missing on this machine — using Built-In fallback");
        fallbackBadge.setVisible(false);
        addChildComponent(fallbackBadge);

        updateVolReadout();
    }

    void updateLiveState(float peakL, float peakR, bool noteActive, bool fallbackActive, bool selected) {
        // Smooth ballistic decay on meters and activity LED
        meterL = (peakL > meterL) ? peakL : meterL * 0.84f;
        meterR = (peakR > meterR) ? peakR : meterR * 0.84f;
        if (meterL < 0.001f) meterL = 0.0f;
        if (meterR < 0.001f) meterR = 0.0f;

        if (noteActive) {
            noteLedGlow = 1.0f;
        } else {
            noteLedGlow *= 0.78f;
            if (noteLedGlow < 0.02f) noteLedGlow = 0.0f;
        }

        isFallback = fallbackActive;
        fallbackBadge.setVisible(isFallback);
        isSelected = selected;

        const bool hasHosted = bridge.hasHostedPluginInSlot(slotIndex);
        if (guiBtn.isEnabled() != hasHosted) {
            guiBtn.setEnabled(hasHosted);
        }

        const auto srcLabel = bridge.getSlotSourceLabel(slotIndex);
        if (sourceBtn.getButtonText().toStdString() != srcLabel) {
            sourceBtn.setButtonText(srcLabel);
        }

        updateVolReadout();
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        const float corner = 8.0f;

        // Card background
        g.setColour(isSelected ? Palette::bgCardSelected : Palette::bgCard);
        g.fillRoundedRectangle(bounds, corner);

        // Selection glow or subtle border
        if (isSelected) {
            g.setColour(accentColour.withAlpha(0.22f));
            g.drawRoundedRectangle(bounds.expanded(0.5f), corner + 0.5f, 2.5f);
            g.setColour(accentColour.withAlpha(0.90f));
            g.drawRoundedRectangle(bounds, corner, 1.4f);
        } else {
            g.setColour(Palette::border);
            g.drawRoundedRectangle(bounds, corner, 1.0f);
        }

        // Top colored accent bar (clipped cleanly to card header)
        {
            juce::Graphics::ScopedSaveState state(g);
            juce::Path clipPath;
            clipPath.addRoundedRectangle(bounds, corner);
            g.reduceClipRegion(clipPath);

            g.setColour(accentColour);
            g.fillRect(bounds.getX(), bounds.getY(), bounds.getWidth(), 3.5f);

            // Subtle header tint
            g.setColour(Palette::bgPanel.withAlpha(0.65f));
            g.fillRect(bounds.getX(), bounds.getY() + 3.5f, bounds.getWidth(), 24.5f);

            g.setColour(Palette::border.withAlpha(0.75f));
            g.drawHorizontalLine(juce::roundToInt(bounds.getY() + 28.0f), bounds.getX(), bounds.getRight());
        }

        // Slot Title
        const auto titleArea = juce::Rectangle<int>(static_cast<int>(bounds.getX()) + 8,
                                                    static_cast<int>(bounds.getY()) + 5,
                                                    static_cast<int>(bounds.getWidth()) - 42,
                                                    21);
        g.setColour(Palette::textPrimary);
        g.setFont(Palette::font(11.5f, true));
        g.drawFittedText(slotShortName(static_cast<SlotId>(slotIndex)),
                         titleArea, juce::Justification::centredLeft, 1);

        // Live Note Activity LED (top-right)
        const float ledX = bounds.getRight() - 16.0f;
        const float ledY = bounds.getY() + 12.0f;
        if (noteLedGlow > 0.02f) {
            g.setColour(accentColour.withAlpha(0.40f * noteLedGlow));
            g.fillEllipse(ledX - 3.5f, ledY - 3.5f, 14.0f, 14.0f);
            g.setColour(accentColour.interpolatedWith(juce::Colours::white, 0.35f * noteLedGlow));
            g.fillEllipse(ledX, ledY, 7.0f, 7.0f);
        } else {
            g.setColour(Palette::bgDark);
            g.fillEllipse(ledX, ledY, 7.0f, 7.0f);
            g.setColour(Palette::borderLight);
            g.drawEllipse(ledX, ledY, 7.0f, 7.0f, 0.8f);
        }

        // Fallback warning amber dot (if hosted VST3 unavailable)
        if (isFallback) {
            const float fbX = ledX - 13.0f;
            g.setColour(Palette::accentAmber.withAlpha(0.35f));
            g.fillEllipse(fbX - 2.5f, ledY - 2.5f, 12.0f, 12.0f);
            g.setColour(Palette::accentAmber);
            g.fillEllipse(fbX, ledY, 7.0f, 7.0f);
        }

        // Stereo LED Level Meter
        if (!meterBounds.isEmpty()) {
            drawStereoMeter(g, meterBounds.toFloat());
        }
    }

    void resized() override {
        auto r = getLocalBounds().reduced(8, 6);

        // Header space (top 26px)
        const auto header = r.removeFromTop(24);
        fallbackBadge.setBounds(header.getRight() - 28, header.getY() + 3, 12, 14);
        r.removeFromTop(5);

        // Source selector row: [Built-In / VST...] [GUI]
        auto srcRow = r.removeFromTop(24);
        const int guiW = 36;
        guiBtn.setBounds(srcRow.removeFromRight(guiW));
        srcRow.removeFromRight(4);
        sourceBtn.setBounds(srcRow);
        r.removeFromTop(5);

        // Patch selector (all slots)
        patchBox.setBounds(r.removeFromTop(23));
        r.removeFromTop(5);

        // Optional second row: Drum-Map (slot 0) or Call&Resp toggle (slot 5)
        if (slotIndex == 0) {
            drumMapBox.setBounds(r.removeFromTop(23));
            r.removeFromTop(5);
        } else if (slotIndex == 5) {
            callRespBtn.setBounds(r.removeFromTop(23));
            r.removeFromTop(5);
        } else {
            // Subtle spacer so knobs align nicely across all 6 rack cards
            r.removeFromTop(28);
        }

        // Bottom section: M/S/Lock row + Vol/Pan + Stereo Meter
        auto bottomMix = r.removeFromBottom(86);
        r.removeFromBottom(4);

        auto mslRow = r.removeFromBottom(23);
        const int gap = 4;
        const int mW = (mslRow.getWidth() - gap * 2) * 26 / 100;
        const int sW = mW;
        muteBtn.setBounds(mslRow.removeFromLeft(mW));
        mslRow.removeFromLeft(gap);
        soloBtn.setBounds(mslRow.removeFromLeft(sW));
        mslRow.removeFromLeft(gap);
        lockBtn.setBounds(mslRow);
        r.removeFromBottom(6);

        // Middle area: Busy & Secondary musical knobs side-by-side
        auto knobRow = r;
        const int halfW = knobRow.getWidth() / 2;
        auto leftKnobArea = knobRow.removeFromLeft(halfW).reduced(2, 0);
        auto rightKnobArea = knobRow.reduced(2, 0);

        busyLabel.setBounds(leftKnobArea.removeFromBottom(14));
        busySlider.setBounds(leftKnobArea);

        secondaryLabel.setBounds(rightKnobArea.removeFromBottom(14));
        secondarySlider.setBounds(rightKnobArea);

        // Bottom mixer area: [Vol knob + dB] [Pan mini-knob] [Stereo LED Meter]
        meterBounds = bottomMix.removeFromRight(18).reduced(1, 3);
        bottomMix.removeFromRight(5);

        auto panArea = bottomMix.removeFromRight(46);
        panLabel.setBounds(panArea.removeFromBottom(13));
        panSlider.setBounds(panArea.reduced(3, 6));

        bottomMix.removeFromRight(4);
        volReadout.setBounds(bottomMix.removeFromBottom(14));
        volSlider.setBounds(bottomMix.reduced(2, 1));
    }

    void mouseDown(const juce::MouseEvent&) override {
        selectThisSlot();
    }

    std::function<void(int)> onSlotSelected;

private:
    static const char* secondaryKnobName(int slotIdx) noexcept {
        switch (slotIdx) {
            case 1: return "KICK LOCK";
            case 2: return "SPACE";
            case 5: return "CALL&RESP";
            default: return "TIGHTNESS";
        }
    }

    static void configureRotary(juce::Slider& s, juce::Colour accent, bool bipolar) {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.setColour(juce::Slider::rotarySliderFillColourId, accent);
        s.getProperties().set("bipolar", bipolar);
        s.setDoubleClickReturnValue(true, bipolar ? 0.0 : 0.65);
    }

    static void configureCaptionLabel(juce::Label& l, const juce::String& text) {
        l.setText(text, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setFont(Palette::font(9.5f, true));
        l.setColour(juce::Label::textColourId, Palette::textMuted);
    }

    static void configurePillToggle(juce::TextButton& b, const juce::String& text,
                                    juce::Colour onColour, const juce::String& tooltip) {
        b.setButtonText(text);
        b.setClickingTogglesState(true);
        b.setColour(juce::TextButton::buttonColourId, Palette::bgControl);
        b.setColour(juce::TextButton::buttonOnColourId, onColour);
        b.setColour(juce::TextButton::textColourOffId, Palette::textMuted);
        b.setColour(juce::TextButton::textColourOnId, Palette::bgDark);
        b.setTooltip(tooltip);
    }

    void selectThisSlot() {
        if (onSlotSelected) {
            onSlotSelected(slotIndex);
        }
    }

    void updateVolReadout() {
        const double db = volSlider.getValue();
        juce::String text;
        if (db <= -59.5) {
            text = "-inf dB";
        } else {
            text = (db > 0.05 ? "+" : "") + juce::String(db, 1) + " dB";
        }
        if (volReadout.getText() != text) {
            volReadout.setText(text, juce::dontSendNotification);
        }
    }

    void drawStereoMeter(juce::Graphics& g, juce::Rectangle<float> area) const {
        g.setColour(Palette::bgDark);
        g.fillRoundedRectangle(area, 3.0f);
        g.setColour(Palette::border);
        g.drawRoundedRectangle(area, 3.0f, 0.8f);

        const auto inner = area.reduced(2.0f);
        const float colW = (inner.getWidth() - 2.0f) * 0.5f;
        drawSingleMeterBar(g, { inner.getX(), inner.getY(), colW, inner.getHeight() }, meterL);
        drawSingleMeterBar(g, { inner.getX() + colW + 2.0f, inner.getY(), colW, inner.getHeight() }, meterR);
    }

    static void drawSingleMeterBar(juce::Graphics& g, juce::Rectangle<float> bar, float linearPeak) {
        // Map linear peak to 0..1 dB scale (-48dB .. 0dB)
        const float db = (linearPeak > 1e-5f) ? (20.0f * std::log10(linearPeak)) : -60.0f;
        const float norm = std::clamp((db + 48.0f) / 48.0f, 0.0f, 1.0f);

        constexpr int numSegments = 14;
        const float segGap = 1.5f;
        const float segH = (bar.getHeight() - segGap * (numSegments - 1)) / numSegments;
        const int litSegments = static_cast<int>(std::round(norm * numSegments));

        for (int i = 0; i < numSegments; ++i) {
            const float y = bar.getBottom() - (i + 1) * segH - i * segGap;
            const auto segRect = juce::Rectangle<float>(bar.getX(), y, bar.getWidth(), std::max(1.5f, segH));
            const bool lit = (i < litSegments);

            juce::Colour col = Palette::accentEmerald;
            if (i >= 12) col = Palette::accentCoral;
            else if (i >= 9) col = Palette::accentAmber;

            g.setColour(lit ? col : Palette::bgCard.withAlpha(0.55f));
            g.fillRoundedRectangle(segRect, 1.0f);
        }
    }

    void showSourceMenu() {
        selectThisSlot();

        juce::PopupMenu menu;
        const auto currentMode = bridge.getSlotSourceMode(slotIndex);

        menu.addItem(1, "Built-In Engine", true, currentMode == SlotSourceMode::BuiltIn);
        menu.addItem(2, "Load SFZ / WAV Sample...", true, currentMode == SlotSourceMode::CustomSFZ);
        menu.addSeparator();

        const auto plugins = bridge.getAvailableVst3Plugins();
        juce::PopupMenu vstMenu;
        if (plugins.empty()) {
            vstMenu.addItem(3, "Scan VST3 Plugins...");
        } else {
            for (size_t i = 0; i < plugins.size(); ++i) {
                const auto& p = plugins[i];
                juce::String label = p.name;
                if (!p.manufacturer.empty()) {
                    label += juce::String("  (" + p.manufacturer + ")");
                }
                vstMenu.addItem(100 + static_cast<int>(i), label);
            }
            vstMenu.addSeparator();
            vstMenu.addItem(3, "Rescan VST3 Plugins...");
        }
        menu.addSubMenu("Hosted VST3 Instruments", vstMenu);

        juce::Component::SafePointer<BandSlotComponent> safeThis(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&sourceBtn),
                           [safeThis, plugins](int result) {
            if (safeThis == nullptr || result == 0) return;

            if (result == 1) {
                safeThis->bridge.selectBuiltInForSlot(safeThis->slotIndex);
            } else if (result == 2) {
                safeThis->launchSampleChooser();
            } else if (result == 3) {
                safeThis->bridge.startVst3ScanAsync();
            } else if (result >= 100) {
                const size_t idx = static_cast<size_t>(result - 100);
                if (idx < plugins.size()) {
                    std::string err;
                    safeThis->bridge.selectVst3ForSlot(safeThis->slotIndex, plugins[idx].identifier, err);
                }
            }
            safeThis->sourceBtn.setButtonText(safeThis->bridge.getSlotSourceLabel(safeThis->slotIndex));
            safeThis->guiBtn.setEnabled(safeThis->bridge.hasHostedPluginInSlot(safeThis->slotIndex));
        });
    }

    void launchSampleChooser() {
        activeChooser = std::make_unique<juce::FileChooser>(
            "Load SFZ or WAV Sample for " + juce::String(slotName(static_cast<SlotId>(slotIndex))),
            juce::File(),
            "*.sfz;*.wav;*.aiff;*.flac");

        juce::Component::SafePointer<BandSlotComponent> safeThis(this);
        constexpr int chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        activeChooser->launchAsync(chooserFlags, [safeThis](const juce::FileChooser& chooser) {
            if (safeThis == nullptr) return;
            const auto file = chooser.getResult();
            if (file.existsAsFile()) {
                std::string err;
                safeThis->bridge.loadSfzForSlot(safeThis->slotIndex, file, err);
                safeThis->sourceBtn.setButtonText(safeThis->bridge.getSlotSourceLabel(safeThis->slotIndex));
                safeThis->guiBtn.setEnabled(safeThis->bridge.hasHostedPluginInSlot(safeThis->slotIndex));
            }
        });
    }

    int slotIndex = 0;
    juce::AudioProcessorValueTreeState& apvts;
    ISessionMateUiBridge& bridge;
    juce::Colour accentColour;

    juce::TextButton sourceBtn;
    juce::TextButton guiBtn;
    juce::ComboBox patchBox;
    juce::ComboBox drumMapBox;

    juce::Slider busySlider;
    juce::Label busyLabel;
    juce::Slider secondarySlider;
    juce::Label secondaryLabel;
    juce::ToggleButton callRespBtn;

    juce::TextButton muteBtn;
    juce::TextButton soloBtn;
    juce::TextButton lockBtn;

    juce::Slider volSlider;
    juce::Label volReadout;
    juce::Slider panSlider;
    juce::Label panLabel;

    struct TooltipBadge : public juce::Component, public juce::SettableTooltipClient {};
    TooltipBadge fallbackBadge;
    juce::Rectangle<int> meterBounds;

    std::unique_ptr<juce::ParameterAttachment> patchAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> drumMapAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> busyAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> secondaryAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> callRespAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lockAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> panAttachment;

    std::unique_ptr<juce::FileChooser> activeChooser;

    float meterL = 0.0f;
    float meterR = 0.0f;
    float noteLedGlow = 0.0f;
    bool isFallback = false;
    bool isSelected = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BandSlotComponent)
};

} // namespace sessionmate::ui
