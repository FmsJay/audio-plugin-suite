#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace
{
void drawMiniMeterBar (juce::Graphics& g,
                       juce::Rectangle<float> bounds,
                       float level,
                       juce::Colour fillColour,
                       bool vertical)
{
    g.setColour (fossil::ui::bgDeep);
    g.fillRoundedRectangle (bounds, 2.5f);

    const float clamped = std::clamp (level, 0.0f, 1.0f);
    if (clamped > 0.002f)
    {
        auto filled = bounds;
        if (vertical)
            filled = bounds.removeFromBottom (bounds.getHeight() * clamped);
        else
            filled = bounds.removeFromLeft (bounds.getWidth() * clamped);

        g.setColour (fillColour);
        g.fillRoundedRectangle (filled, 2.0f);
    }

    g.setColour (fossil::ui::border);
    g.drawRoundedRectangle (bounds, 2.5f, 1.0f);
}

template <size_t N>
juce::String readAtomicString (const std::array<std::atomic<char>, N>& src)
{
    char buf[N] {};
    for (size_t i = 0; i + 1 < N; ++i)
    {
        buf[i] = src[i].load (std::memory_order_relaxed);
        if (buf[i] == '\0')
            break;
    }
    buf[N - 1] = '\0';
    return juce::String::fromUTF8 (buf).trim();
}
} // namespace

FossilAudioProcessorEditor::FossilAudioProcessorEditor (FossilAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor_ (p),
      strataStack_ (p.getAPVTS(), p.getTelemetry()),
      layerInspector_ (p.getAPVTS(), p.getTelemetry()),
      euclidRing_ (p.getAPVTS(), p.getTelemetry())
{
    setLookAndFeel (&lookAndFeel_);

    auto& apvts = processor_.getAPVTS();

    // Presets combo
    const auto& presets = fossil::getFactoryPresets();
    for (int i = 0; i < static_cast<int> (presets.size()); ++i)
        presetBox_.addItem (presets[static_cast<size_t> (i)].name, i + 1);
    presetBox_.setSelectedId (processor_.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox_.onChange = [this]()
    {
        const int idx = presetBox_.getSelectedId() - 1;
        if (idx >= 0)
            processor_.loadFactoryPreset (idx);
    };
    addAndMakeVisible (presetBox_);

    // Prominent FREEZE T / H / N buttons
    auto setupFreezeBtn = [this] (FreezeButton& btn, const juce::String& text,
                                  fossil::StreamSource src, juce::Colour accent)
    {
        btn.setButtonText (text);
        btn.accentColour = accent;
        btn.setColour (juce::TextButton::buttonColourId, accent.withAlpha (0.16f));
        btn.setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.45f));
        btn.setColour (juce::TextButton::textColourOffId, accent.brighter (0.25f));
        btn.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        btn.onPressDown = [this, src]() { handleFreezeButtonDown (src); };
        btn.onReleaseUp = [this]() { handleFreezeButtonUp(); };
        addAndMakeVisible (btn);
    };

    setupFreezeBtn (freezeTBtn_, "FREEZE T", fossil::StreamSource::Transient, fossil::ui::accentT);
    setupFreezeBtn (freezeHBtn_, "FREEZE H", fossil::StreamSource::Tonal,     fossil::ui::accentH);
    setupFreezeBtn (freezeNBtn_, "FREEZE N", fossil::StreamSource::Noise,     fossil::ui::accentN);

    // Clear all
    clearAllBtn_.setColour (juce::TextButton::buttonColourId, fossil::ui::accentCoral.withAlpha (0.15f));
    clearAllBtn_.setColour (juce::TextButton::textColourOffId, fossil::ui::accentCoral.brighter (0.2f));
    clearAllBtn_.onClick = [this]() { processor_.clearAllLayers(); };
    addAndMakeVisible (clearAllBtn_);

    // Freeze mode (Latch / Momentary)
    freezeModeBox_.addItem ("Latch", 1);
    freezeModeBox_.addItem ("Momentary", 2);
    addAndMakeVisible (freezeModeBox_);
    freezeModeAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        apvts, fossil::param_ids::freezeMode, freezeModeBox_);

    // Internal BPM slider + host sync label
    bpmSlider_.setSliderStyle (juce::Slider::LinearBar);
    bpmSlider_.setTextValueSuffix (" BPM");
    bpmSlider_.setNumDecimalPlacesToDisplay (1);
    bpmSlider_.setColour (juce::Slider::trackColourId, fossil::ui::accentH.withAlpha (0.22f));
    bpmSlider_.setColour (juce::Slider::textBoxTextColourId, fossil::ui::textPrimary);
    bpmSlider_.setColour (juce::Slider::textBoxOutlineColourId, fossil::ui::border);
    addAndMakeVisible (bpmSlider_);
    bpmAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, fossil::param_ids::internalBpm, bpmSlider_);

    bpmStatusLabel_.setText ("INT", juce::dontSendNotification);
    bpmStatusLabel_.setJustificationType (juce::Justification::centred);
    bpmStatusLabel_.setColour (juce::Label::textColourId, fossil::ui::textMuted);
    bpmStatusLabel_.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    addAndMakeVisible (bpmStatusLabel_);

    // ========================================================================
    // Suite Link Bar (SuiteTextureLink + SuiteHarmonyLink)
    // ========================================================================
    const juce::StringArray busNames { "Off", "Bus A", "Bus B", "Bus C", "Bus D", "Bus E", "Bus F", "Bus G", "Bus H" };

    texLinkLabel_.setText ("TEX LINK", juce::dontSendNotification);
    texLinkLabel_.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    texLinkLabel_.setColour (juce::Label::textColourId, fossil::ui::accentN);
    texLinkLabel_.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (texLinkLabel_);

    for (int i = 0; i < busNames.size(); ++i)
        texLinkBusBox_.addItem (busNames[i], i + 1);
    addAndMakeVisible (texLinkBusBox_);
    texLinkBusAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        apvts, fossil::param_ids::textureLinkBus, texLinkBusBox_);

    vowelModSlider_.setSliderStyle (juce::Slider::LinearBar);
    vowelModSlider_.setColour (juce::Slider::trackColourId, fossil::ui::accentN.withAlpha (0.55f));
    vowelModSlider_.setColour (juce::Slider::backgroundColourId, fossil::ui::bgDeep);
    vowelModSlider_.setColour (juce::Slider::textBoxTextColourId, fossil::ui::textPrimary);
    vowelModSlider_.setColour (juce::Slider::textBoxOutlineColourId, fossil::ui::border);
    vowelModSlider_.textFromValueFunction = [] (double v) { return "Vowel Mod " + juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    addAndMakeVisible (vowelModSlider_);
    vowelModAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, fossil::param_ids::vowelModDepth, vowelModSlider_);

    harmLinkLabel_.setText ("HARM LINK", juce::dontSendNotification);
    harmLinkLabel_.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    harmLinkLabel_.setColour (juce::Label::textColourId, fossil::ui::accentH);
    harmLinkLabel_.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (harmLinkLabel_);

    for (int i = 0; i < busNames.size(); ++i)
        harmLinkBusBox_.addItem (busNames[i], i + 1);
    addAndMakeVisible (harmLinkBusBox_);
    harmLinkBusAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        apvts, fossil::param_ids::harmonyLinkBus, harmLinkBusBox_);

    harmQuantizeBtn_.setColour (juce::ToggleButton::tickColourId, fossil::ui::accentH);
    addAndMakeVisible (harmQuantizeBtn_);
    harmQuantizeAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        apvts, fossil::param_ids::harmQuantize, harmQuantizeBtn_);

    // Center: Strata Stack & Layer Inspector
    strataStack_.onLayerSelected = [this] (int slot)
    {
        layerInspector_.setSelectedLayer (slot);
    };
    strataStack_.onSwapLayers = [this] (int slotA, int slotB)
    {
        processor_.swapLayers (slotA, slotB);
        strataStack_.setSelectedLayer (slotB);
        layerInspector_.setSelectedLayer (slotB);
    };
    strataStack_.onClearLayer = [this] (int slot)
    {
        processor_.clearLayer (slot);
    };
    addAndMakeVisible (strataStack_);

    layerInspector_.onCaptureIntoLayer = [this] (int slot, fossil::StreamSource src)
    {
        processor_.triggerFreeze (src, slot);
    };
    layerInspector_.onClearLayer = [this] (int slot)
    {
        processor_.clearLayer (slot);
    };
    layerInspector_.setSelectedLayer (0);
    strataStack_.setSelectedLayer (0);
    addAndMakeVisible (layerInspector_);

    // Footer: Dry Transient Pass bar + Dry H/N + HPSS separation knobs
    dryTSlider_.setSliderStyle (juce::Slider::LinearBar);
    dryTSlider_.setColour (juce::Slider::trackColourId, fossil::ui::accentT.withAlpha (0.75f));
    dryTSlider_.setColour (juce::Slider::backgroundColourId, fossil::ui::bgDeep);
    dryTSlider_.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    dryTSlider_.setColour (juce::Slider::textBoxOutlineColourId, fossil::ui::borderBright);
    dryTSlider_.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    addAndMakeVisible (dryTSlider_);
    dryTAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, fossil::param_ids::dryT, dryTSlider_);

    dryTLabel_.setText ("DRY TRANSIENT PASS", juce::dontSendNotification);
    dryTLabel_.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    dryTLabel_.setColour (juce::Label::textColourId, fossil::ui::accentT);
    addAndMakeVisible (dryTLabel_);

    dryHKnob_      = std::make_unique<fossil::ui::KnobControl> (apvts, fossil::param_ids::dryH,        "DRY H",  fossil::ui::accentH, "%");
    dryNKnob_      = std::make_unique<fossil::ui::KnobControl> (apvts, fossil::param_ids::dryN,        "DRY N",  fossil::ui::accentN, "%");
    hpssSepKnob_   = std::make_unique<fossil::ui::KnobControl> (apvts, fossil::param_ids::hpssSep,     "SEP",    fossil::ui::accentT, "%");
    noiseSensKnob_ = std::make_unique<fossil::ui::KnobControl> (apvts, fossil::param_ids::noiseThresh, "AIR",    fossil::ui::accentN, "%");
    addAndMakeVisible (*dryHKnob_);
    addAndMakeVisible (*dryNKnob_);
    addAndMakeVisible (*hpssSepKnob_);
    addAndMakeVisible (*noiseSensKnob_);

    // Footer: Master Strata & Shimmer knobs
    strataBlendKnob_  = std::make_unique<fossil::ui::KnobControl> (apvts, fossil::param_ids::strataBlend,  "STRATA",  fossil::ui::accentH,       "%");
    shimmerSmearKnob_ = std::make_unique<fossil::ui::KnobControl> (apvts, fossil::param_ids::shimmerSmear, "SHIMMER", fossil::ui::accentN,       "%");
    shimmerPitchKnob_ = std::make_unique<fossil::ui::KnobControl> (apvts, fossil::param_ids::shimmerPitch, "+12 OCT", fossil::ui::accentEmerald, "%");
    outputGainKnob_   = std::make_unique<fossil::ui::KnobControl> (apvts, fossil::param_ids::outputGain,   "OUTPUT",  fossil::ui::textPrimary,   " dB");
    addAndMakeVisible (*strataBlendKnob_);
    addAndMakeVisible (*shimmerSmearKnob_);
    addAndMakeVisible (*shimmerPitchKnob_);
    addAndMakeVisible (*outputGainKnob_);

    // Footer: Euclidean Gate Ring
    addAndMakeVisible (euclidRing_);

    setResizable (true, true);
    setResizeLimits (880, 590, 1440, 960);
    setSize (990, 666);

    startTimerHz (30);
}

FossilAudioProcessorEditor::~FossilAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void FossilAudioProcessorEditor::handleFreezeButtonDown (fossil::StreamSource source)
{
    processor_.triggerFreeze (source, -1);
}

void FossilAudioProcessorEditor::handleFreezeButtonUp()
{
    if (auto* modeParam = processor_.getAPVTS().getRawParameterValue (fossil::param_ids::freezeMode))
    {
        if (modeParam->load (std::memory_order_relaxed) >= 0.5f)
            processor_.releaseMomentaryFreeze (-1);
    }
}

void FossilAudioProcessorEditor::timerCallback()
{
    const auto& t = processor_.getTelemetry();

    auto smoothMeter = [] (float current, float incoming)
    {
        const float scaled = std::clamp (std::sqrt (std::max (0.0f, incoming)), 0.0f, 1.0f);
        return scaled > current ? scaled : current * 0.84f + scaled * 0.16f;
    };

    uiEnergyT_  = smoothMeter (uiEnergyT_,  t.liveEnergyT.load (std::memory_order_relaxed) * 2.2f);
    uiEnergyH_  = smoothMeter (uiEnergyH_,  t.liveEnergyH.load (std::memory_order_relaxed) * 2.2f);
    uiEnergyN_  = smoothMeter (uiEnergyN_,  t.liveEnergyN.load (std::memory_order_relaxed) * 2.2f);
    uiInPeakL_  = smoothMeter (uiInPeakL_,  t.inputPeakL.load (std::memory_order_relaxed));
    uiInPeakR_  = smoothMeter (uiInPeakR_,  t.inputPeakR.load (std::memory_order_relaxed));
    uiOutPeakL_ = smoothMeter (uiOutPeakL_, t.outputPeakL.load (std::memory_order_relaxed));
    uiOutPeakR_ = smoothMeter (uiOutPeakR_, t.outputPeakR.load (std::memory_order_relaxed));

    const bool synced = t.hostSynced.load (std::memory_order_relaxed);
    const double bpm  = t.currentBpm.load (std::memory_order_relaxed);
    if (synced)
    {
        bpmStatusLabel_.setText ("SYNC " + juce::String (bpm, 1), juce::dontSendNotification);
        bpmStatusLabel_.setColour (juce::Label::textColourId, fossil::ui::accentEmerald);
    }
    else
    {
        bpmStatusLabel_.setText ("INT BPM", juce::dontSendNotification);
        bpmStatusLabel_.setColour (juce::Label::textColourId, fossil::ui::textMuted);
    }

    const int activeSrc = t.activeFreezeSource.load (std::memory_order_relaxed);
    freezeTBtn_.setToggleState (activeSrc == 0, juce::dontSendNotification);
    freezeHBtn_.setToggleState (activeSrc == 1, juce::dontSendNotification);
    freezeNBtn_.setToggleState (activeSrc == 2, juce::dontSendNotification);

    // Update SuiteTextureLink peer badge
    uiVowelAlive_ = t.vowelPeerAlive.load (std::memory_order_relaxed);
    const int texBus = processor_.getTextureLinkBusIndex();
    if (uiVowelAlive_)
    {
        const auto syl   = readAtomicString (t.vowelSyllableLabel);
        const auto tract = readAtomicString (t.vowelTractLabel);
        juce::String detail;
        if (syl.isNotEmpty() && tract.isNotEmpty())
            detail = " [" + syl + juce::String::fromUTF8 (" \xc2\xb7 ") + tract + "]";
        else if (syl.isNotEmpty())
            detail = " [" + syl + "]";
        uiVowelBadgeText_ = "Linked: VowelMorph" + detail;
    }
    else if (texBus >= 0)
    {
        uiVowelBadgeText_ = "Waiting Bus " + juce::String (suite::texlink::busLabel (texBus)) + "...";
    }
    else
    {
        uiVowelBadgeText_ = "Texture Link Off";
    }

    // Update SuiteHarmonyLink chord badge
    uiHarmonyAlive_ = t.harmonyPeerAlive.load (std::memory_order_relaxed);
    const int harmBus = processor_.getHarmonyLinkBusIndex();
    if (uiHarmonyAlive_)
    {
        const auto chord = readAtomicString (t.harmonyChordLabel);
        uiHarmonyBadgeText_ = chord.isNotEmpty() ? ("Chord: " + chord) : "Harmony Linked";
    }
    else if (harmBus >= 0)
    {
        uiHarmonyBadgeText_ = "Bus " + juce::String (suite::harmony_link::busLabel (harmBus)) + " Idle";
    }
    else
    {
        uiHarmonyBadgeText_ = "Harmony Off";
    }

    strataStack_.refreshFromTelemetry();
    layerInspector_.refreshFromTelemetry();
    euclidRing_.refreshFromTelemetry();

    repaint (liveEnergyArea_);
    repaint (meterArea_);
    repaint (linkBarArea_);
}

void FossilAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Background geological gradient
    juce::ColourGradient bgGrad (fossil::ui::bgDeep.brighter (0.04f), 0.0f, 0.0f,
                                 fossil::ui::bgDeep, 0.0f, static_cast<float> (getHeight()), false);
    g.setGradientFill (bgGrad);
    g.fillAll();

    // Top Header panel
    g.setColour (fossil::ui::bgPanel);
    g.fillRect (headerArea_);
    g.setColour (fossil::ui::border);
    g.drawHorizontalLine (headerArea_.getBottom() - 1, 0.0f, static_cast<float> (getWidth()));

    // Suite Link Bar strip
    if (! linkBarArea_.isEmpty())
    {
        g.setColour (fossil::ui::bgDeep.brighter (0.025f));
        g.fillRect (linkBarArea_);
        g.setColour (fossil::ui::border);
        g.drawHorizontalLine (linkBarArea_.getBottom() - 1, 0.0f, static_cast<float> (getWidth()));

        // VowelMorph peer status pill
        if (! vowelPeerBadgeArea_.isEmpty())
        {
            const auto r = vowelPeerBadgeArea_.toFloat().reduced (0.0f, 3.0f);
            const auto col = uiVowelAlive_ ? fossil::ui::accentEmerald
                                           : (processor_.getTextureLinkBusIndex() >= 0 ? fossil::ui::accentT : fossil::ui::textMuted);
            g.setColour (col.withAlpha (uiVowelAlive_ ? 0.16f : 0.08f));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (col.withAlpha (uiVowelAlive_ ? 0.85f : 0.40f));
            g.drawRoundedRectangle (r, 5.0f, 1.0f);

            // Live LED dot
            const float dotR = 3.2f;
            g.setColour (col);
            g.fillEllipse (r.getX() + 7.0f, r.getCentreY() - dotR, dotR * 2.0f, dotR * 2.0f);

            g.setFont (juce::FontOptions (10.5f, uiVowelAlive_ ? juce::Font::bold : juce::Font::plain));
            g.setColour (uiVowelAlive_ ? fossil::ui::textPrimary : col);
            g.drawFittedText (uiVowelBadgeText_, r.withTrimmedLeft (17.0f).withTrimmedRight (5.0f).toNearestInt(),
                              juce::Justification::centredLeft, 1);
        }

        // HarmonyLink chord status pill
        if (! harmonyBadgeArea_.isEmpty())
        {
            const auto r = harmonyBadgeArea_.toFloat().reduced (0.0f, 3.0f);
            const auto col = uiHarmonyAlive_ ? fossil::ui::accentH
                                             : (processor_.getHarmonyLinkBusIndex() >= 0 ? fossil::ui::accentT : fossil::ui::textMuted);
            g.setColour (col.withAlpha (uiHarmonyAlive_ ? 0.18f : 0.08f));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (col.withAlpha (uiHarmonyAlive_ ? 0.85f : 0.40f));
            g.drawRoundedRectangle (r, 5.0f, 1.0f);

            g.setFont (juce::FontOptions (10.5f, uiHarmonyAlive_ ? juce::Font::bold : juce::Font::plain));
            g.setColour (uiHarmonyAlive_ ? fossil::ui::textPrimary : col);
            g.drawFittedText (uiHarmonyBadgeText_, r.reduced (5.0f, 0.0f).toNearestInt(),
                              juce::Justification::centred, 1);
        }
    }

    // Brand Title
    auto brandArea = headerArea_.withWidth (122).reduced (12, 8);
    g.setColour (fossil::ui::textPrimary);
    g.setFont (juce::FontOptions (21.0f, juce::Font::bold));
    g.drawText ("FOSSIL", brandArea.removeFromTop (26), juce::Justification::centredLeft);
    g.setColour (fossil::ui::textMuted);
    g.setFont (juce::FontOptions (9.5f, juce::Font::plain));
    g.drawText ("SPECTRAL STRATA", brandArea, juce::Justification::centredLeft);

    // Live 3-way T/H/N energy strips in header
    {
        auto ea = liveEnergyArea_.toFloat();
        const float rowH = ea.getHeight() / 3.0f;
        const std::array<std::tuple<const char*, float, juce::Colour>, 3> rows = {{
            { "T", uiEnergyT_, fossil::ui::accentT },
            { "H", uiEnergyH_, fossil::ui::accentH },
            { "N", uiEnergyN_, fossil::ui::accentN }
        }};
        for (int i = 0; i < 3; ++i)
        {
            auto r = ea.withY (ea.getY() + rowH * static_cast<float> (i)).withHeight (rowH).reduced (0.0f, 1.5f);
            g.setColour (std::get<2> (rows[static_cast<size_t> (i)]));
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawText (std::get<0> (rows[static_cast<size_t> (i)]),
                        r.removeFromLeft (12.0f), juce::Justification::centredLeft);
            drawMiniMeterBar (g, r, std::get<1> (rows[static_cast<size_t> (i)]),
                              std::get<2> (rows[static_cast<size_t> (i)]), false);
        }
    }

    // Footer Cards
    auto drawCard = [&g] (juce::Rectangle<int> r, const juce::String& title)
    {
        g.setColour (fossil::ui::bgPanel);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (fossil::ui::border);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);

        if (title.isNotEmpty())
        {
            g.setColour (fossil::ui::textMuted);
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawText (title, r.withHeight (20).reduced (10, 2), juce::Justification::centredLeft);
        }
    };

    drawCard (dryCardArea_,    "DECOMPOSITION & DRY PASS");
    drawCard (masterCardArea_, "STRATA BLEND & SHIMMER");
    drawCard (euclidCardArea_, "");

    // Stereo IN / OUT meters inside masterCardArea_
    if (! meterArea_.isEmpty())
    {
        auto ma = meterArea_.toFloat().reduced (2.0f, 4.0f);
        auto labelRow = ma.removeFromBottom (14.0f);
        const float halfW = ma.getWidth() * 0.5f;

        auto inRect  = ma.removeFromLeft (halfW).reduced (2.0f, 0.0f);
        auto outRect = ma.reduced (2.0f, 0.0f);

        const float barW = (inRect.getWidth() - 2.0f) * 0.5f;
        drawMiniMeterBar (g, inRect.withWidth (barW), uiInPeakL_, fossil::ui::accentH.withAlpha (0.8f), true);
        drawMiniMeterBar (g, inRect.withTrimmedLeft (barW + 2.0f), uiInPeakR_, fossil::ui::accentH.withAlpha (0.8f), true);

        drawMiniMeterBar (g, outRect.withWidth (barW), uiOutPeakL_, fossil::ui::accentEmerald, true);
        drawMiniMeterBar (g, outRect.withTrimmedLeft (barW + 2.0f), uiOutPeakR_, fossil::ui::accentEmerald, true);

        g.setColour (fossil::ui::textMuted);
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText ("IN",  labelRow.removeFromLeft (halfW), juce::Justification::centred);
        g.drawText ("OUT", labelRow, juce::Justification::centred);
    }
}

void FossilAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    // Header (62px)
    headerArea_ = area.removeFromTop (62);
    {
        auto h = headerArea_.reduced (10, 10);
        h.removeFromLeft (114); // brand title
        liveEnergyArea_ = h.removeFromLeft (72).reduced (2, 2);
        h.removeFromLeft (8);

        presetBox_.setBounds (h.removeFromLeft (158).reduced (0, 6));
        h.removeFromLeft (10);

        // Right cluster: Clear, Mode, BPM
        clearAllBtn_.setBounds (h.removeFromRight (66).reduced (0, 5));
        h.removeFromRight (6);
        freezeModeBox_.setBounds (h.removeFromRight (92).reduced (0, 6));
        h.removeFromRight (8);

        auto bpmArea = h.removeFromRight (92);
        bpmStatusLabel_.setBounds (bpmArea.removeFromTop (14));
        bpmSlider_.setBounds (bpmArea.reduced (0, 1));
        h.removeFromRight (10);

        // Center Freeze T / H / N buttons
        const int btnGap = 8;
        const int btnW = std::max (78, (h.getWidth() - btnGap * 2) / 3);
        freezeTBtn_.setBounds (h.removeFromLeft (btnW).reduced (0, 3));
        h.removeFromLeft (btnGap);
        freezeHBtn_.setBounds (h.removeFromLeft (btnW).reduced (0, 3));
        h.removeFromLeft (btnGap);
        freezeNBtn_.setBounds (h.removeFromLeft (btnW).reduced (0, 3));
    }

    // Suite Link Bar (34px)
    linkBarArea_ = area.removeFromTop (34);
    {
        auto lb = linkBarArea_.reduced (10, 4);

        // Left: Texture Link Bus + Vowel Mod slider + Live VowelMorph Badge
        texLinkLabel_.setBounds (lb.removeFromLeft (56));
        texLinkBusBox_.setBounds (lb.removeFromLeft (76).reduced (0, 1));
        lb.removeFromLeft (6);
        vowelModSlider_.setBounds (lb.removeFromLeft (118).reduced (0, 2));
        lb.removeFromLeft (8);

        // Right: Harmony Link Bus + Harm Quantize toggle + Live Chord Badge
        harmonyBadgeArea_ = lb.removeFromRight (114);
        lb.removeFromRight (6);
        harmQuantizeBtn_.setBounds (lb.removeFromRight (112).reduced (0, 1));
        lb.removeFromRight (6);
        harmLinkBusBox_.setBounds (lb.removeFromRight (76).reduced (0, 1));
        lb.removeFromRight (4);
        harmLinkLabel_.setBounds (lb.removeFromRight (64));
        lb.removeFromRight (10);

        // Remaining center-left space -> VowelMorph Peer Status Pill
        vowelPeerBadgeArea_ = lb;
    }

    auto body = area.reduced (10, 8);

    // Footer (200px)
    auto footer = body.removeFromBottom (198);
    body.removeFromBottom (8);

    // Middle: Strata Stack (57%) + Layer Inspector (43%)
    const int stackW = static_cast<int> (body.getWidth() * 0.57f);
    strataStack_.setBounds (body.removeFromLeft (stackW));
    body.removeFromLeft (8);
    layerInspector_.setBounds (body);

    // Footer split: Dry Card (31%) | Master Shimmer Card (34%) | Euclid Ring (35%)
    const int dryW    = static_cast<int> (footer.getWidth() * 0.31f);
    const int masterW = static_cast<int> (footer.getWidth() * 0.34f);

    dryCardArea_ = footer.removeFromLeft (dryW);
    footer.removeFromLeft (8);
    masterCardArea_ = footer.removeFromLeft (masterW);
    footer.removeFromLeft (8);
    euclidCardArea_ = footer;

    // Layout inside Dry Card
    {
        auto r = dryCardArea_.reduced (10, 6);
        r.removeFromTop (18); // title
        auto barRow = r.removeFromTop (38);
        dryTLabel_.setBounds (barRow.removeFromTop (15));
        dryTSlider_.setBounds (barRow.reduced (0, 1));
        r.removeFromTop (8);

        const int kw = r.getWidth() / 4;
        dryHKnob_->setBounds      (r.removeFromLeft (kw));
        dryNKnob_->setBounds      (r.removeFromLeft (kw));
        hpssSepKnob_->setBounds   (r.removeFromLeft (kw));
        noiseSensKnob_->setBounds (r);
    }

    // Layout inside Master Card
    {
        auto r = masterCardArea_.reduced (10, 6);
        r.removeFromTop (18);
        meterArea_ = r.removeFromRight (52);
        r.removeFromRight (6);

        const int kw = r.getWidth() / 2;
        const int kh = r.getHeight() / 2;
        auto topRow = r.removeFromTop (kh);
        strataBlendKnob_->setBounds  (topRow.removeFromLeft (kw));
        shimmerSmearKnob_->setBounds (topRow);
        shimmerPitchKnob_->setBounds (r.removeFromLeft (kw));
        outputGainKnob_->setBounds   (r);
    }

    // Layout Euclidean Ring Card
    euclidRing_.setBounds (euclidCardArea_);
}
