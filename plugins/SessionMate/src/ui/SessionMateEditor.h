#pragma once

#include "SessionMateLookAndFeel.h"
#include "BandSlotComponent.h"
#include "ArrangerAndChordStrip.h"
#include "../SessionMateTypes.h"
#include "../SessionMateParams.h"
#include "../SessionMateUiBridge.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <functional>
#include <memory>
#include <string>

namespace sessionmate::ui {

// Draggable studio pill button that initiates native OS file drag-and-drop into any DAW,
// or falls back to a Save dialog when clicked without dragging.
class MidiDragButton : public juce::Component,
                       public juce::SettableTooltipClient {
public:
    MidiDragButton(const juce::String& initialLabel, juce::Colour accent)
        : labelText(initialLabel), accentColour(accent) {
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    }

    void setLabel(const juce::String& text) {
        if (labelText != text) {
            labelText = text;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override {
        const auto bounds = getLocalBounds().toFloat().reduced(0.75f);
        const float corner = 6.0f;

        auto fill = isDragging
            ? accentColour.withAlpha(0.32f)
            : (isHovered ? accentColour.withAlpha(0.18f) : Palette::bgCard);

        g.setColour(fill);
        g.fillRoundedRectangle(bounds, corner);

        g.setColour(isHovered || isDragging ? accentColour : accentColour.withAlpha(0.55f));
        g.drawRoundedRectangle(bounds, corner, isDragging ? 1.5f : 1.0f);

        // 6-dot drag grip icon on left
        const float gx = bounds.getX() + 9.0f;
        const float gy = bounds.getCentreY() - 4.0f;
        g.setColour(accentColour.withAlpha(0.85f));
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 2; ++col) {
                g.fillEllipse(gx + col * 4.0f, gy + row * 3.8f, 2.0f, 2.0f);
            }
        }

        // Label text
        const auto textArea = bounds.withTrimmedLeft(22.0f).withTrimmedRight(8.0f);
        g.setColour(Palette::textPrimary);
        g.setFont(Palette::font(11.5f, true));
        g.drawFittedText(labelText, textArea.toNearestInt(), juce::Justification::centred, 1);
    }

    void mouseEnter(const juce::MouseEvent&) override {
        isHovered = true;
        repaint();
    }

    void mouseExit(const juce::MouseEvent&) override {
        isHovered = false;
        isDragging = false;
        repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override {
        dragStartPos = e.getPosition();
        didStartDrag = false;
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (!didStartDrag && e.getPosition().getDistanceFrom(dragStartPos) >= 6) {
            didStartDrag = true;
            isDragging = true;
            repaint();

            if (onCreateDragFile) {
                const auto file = onCreateDragFile();
                if (file.existsAsFile()) {
                    juce::DragAndDropContainer::performExternalDragDropOfFiles(
                        juce::StringArray { file.getFullPathName() }, false, this);
                }
            }

            isDragging = false;
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent& e) override {
        if (!didStartDrag && getLocalBounds().contains(e.getPosition())) {
            if (onClickFallback) {
                onClickFallback();
            }
        }
        didStartDrag = false;
        isDragging = false;
        repaint();
    }

    std::function<juce::File()> onCreateDragFile;
    std::function<void()> onClickFallback;

private:
    juce::String labelText;
    juce::Colour accentColour;
    juce::Point<int> dragStartPos;
    bool isHovered = false;
    bool isDragging = false;
    bool didStartDrag = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiDragButton)
};

class SessionMateEditor : public juce::AudioProcessorEditor,
                          public juce::DragAndDropContainer,
                          private juce::Timer {
public:
    SessionMateEditor(juce::AudioProcessor& proc,
                      juce::AudioProcessorValueTreeState& apvts,
                      ISessionMateUiBridge& bridge);
    ~SessionMateEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void selectSlot(int slotIndex);
    void openSaveMidiChooser(int slotFilter);
    void openSavePresetChooser();
    void openLoadPresetChooser();
    void drawMasterMeter(juce::Graphics& g, juce::Rectangle<float> area) const;
    void drawActivityOverlayBar(juce::Graphics& g, juce::Rectangle<float> area,
                                float level, juce::Colour colour, const juce::String& tag) const;
    void drawHudLinkStatusPill(juce::Graphics& g, juce::Rectangle<float> area) const;

    juce::AudioProcessorValueTreeState& apvts;
    ISessionMateUiBridge& bridge;
    SessionMateLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 450 };

    // Header Row 1: Transport, Sync, BPM, Key, Live Chord, Time Sig, Presets
    juce::TextButton playStopBtn;
    juce::ComboBox syncModeBox;
    juce::Slider bpmSlider;
    juce::TextButton tapBtn;
    juce::ComboBox keyRootBox;
    juce::TextButton keyModeBtn;
    juce::ComboBox timeSigBox;
    juce::TextButton savePresetBtn;
    juce::TextButton loadPresetBtn;

    // Header Row 2: Style A <-> Style B DNA Crossfader
    juce::ComboBox styleABox;
    juce::Slider styleMorphSlider;
    juce::ComboBox styleBBox;

    // Header Row 3: HarmonyHUD Link & Keys Voicing Strip
    juce::ComboBox harmonicSourceBox;
    juce::ComboBox linkBusBox;
    juce::ToggleButton clashAvoidBtn;
    juce::ComboBox keysVoicingBox;

    // Middle Top: Song Arranger + Chord Track
    std::unique_ptr<ArrangerAndChordStrip> arrangerStrip;

    // Main Center: 6-Player Band Rack
    std::array<std::unique_ptr<BandSlotComponent>, kNumSlots> bandSlots;
    int selectedSlotIndex = 0;

    // Footer Performance Controls
    juce::Slider energySlider;
    juce::Slider swingSlider;
    juce::Slider pushPullSlider;
    juce::Slider leaveSpaceSlider;
    juce::Slider masterVolSlider;

    juce::Label energyLabel;
    juce::Label swingLabel;
    juce::Label pushPullLabel;
    juce::Label leaveSpaceLabel;
    juce::Label masterLabel;

    // Footer Action & Export Controls
    std::unique_ptr<MidiDragButton> dragAllMidiBtn;
    std::unique_ptr<MidiDragButton> dragSlotMidiBtn;
    juce::TextButton saveMidiBtn;
    juce::TextButton scanVstBtn;

    // APVTS Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> syncModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bpmAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyRootAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> keyModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> timeSigAttachment;
    std::unique_ptr<juce::ParameterAttachment> styleAAttachment;
    std::unique_ptr<juce::ParameterAttachment> styleBAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> styleMorphAttachment;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> energyAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> swingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pushPullAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> leaveSpaceAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterVolAttachment;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> harmonicSourceAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> linkBusAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> clashAvoidAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keysVoicingAttachment;

    // Keeps JUCE 8 async FileChooser alive until completion
    std::unique_ptr<juce::FileChooser> activeChooser;

    // Layout rects painted directly in paint()
    juce::Rectangle<int> headerPanelBounds;
    juce::Rectangle<int> titleBounds;
    juce::Rectangle<int> liveBpmBounds;
    juce::Rectangle<int> liveChordPillBounds;
    juce::Rectangle<int> morphLabelBounds;
    juce::Rectangle<int> hudStripLabelBounds;
    juce::Rectangle<int> hudLinkPillBounds;
    juce::Rectangle<int> keysVoicingLabelBounds;
    juce::Rectangle<int> footerPanelBounds;
    juce::Rectangle<int> energyMeterBounds;
    juce::Rectangle<int> userActivityMeterBounds;
    juce::Rectangle<int> masterMeterBounds;
    juce::Rectangle<int> scanStatusBounds;

    // Cached live UI state updated at 30Hz
    UiSnapshot latestSnapshot {};
    std::string activeChordText { "Em9" };
    std::string hudDetectedChordText;
    std::string hudModeText { "Aeolian" };
    int clashPulseTicks = 0;
    std::string scanStatusText;
    std::string toastStatusText;
    int toastCountdownTicks = 0;
    bool isScanningVst = false;
    float masterMeterL = 0.0f;
    float masterMeterR = 0.0f;
    float smoothListenerEnergy = 0.0f;
    float smoothUserActivity = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SessionMateEditor)
};

} // namespace sessionmate::ui
