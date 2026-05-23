/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
    constexpr int kWindowW       = 820;
    constexpr int kWindowH       = 760;
    constexpr int kOuterMargin   = 22;
    constexpr int kGroupGap      = 6;
    constexpr int kGroupTitleH   = 22;

    juce::Colour groupTextColour = juce::Colours::lightgrey;
}

//==============================================================================
MaybeDuckAudioProcessorEditor::MaybeDuckAudioProcessorEditor (MaybeDuckAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel(&mdLookAndFeel);

    auto setupSlider = [this](juce::Slider& s, const juce::String& name)
    {
        mdLookAndFeel.applyRotaryConfiguration(s);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
        s.setName(name);
        addAndMakeVisible(s);
    };

    auto setupLabel = [this](juce::Label& l, const juce::String& text)
    {
        l.setText(text, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(l);
    };

    auto setupGroup = [this](juce::GroupComponent& g, const juce::String& title)
    {
        g.setText(title);
        g.setColour(juce::GroupComponent::outlineColourId, juce::Colours::white.withAlpha(0.25f));
        g.setColour(juce::GroupComponent::textColourId,   groupTextColour);
        g.setTextLabelPosition(juce::Justification::centredLeft);
        addAndMakeVisible(g);
    };

    // ----- Dynamics group -----
    setupGroup(dynamicsGroup, "Dynamics");

    setupSlider(thresholdSlider, "Threshold");
    setupSlider(ratioSlider,     "Ratio");
    setupSlider(attackSlider,    "Attack");
    setupSlider(holdSlider,      "Hold");
    setupSlider(releaseSlider,   "Release");
    setupSlider(lookaheadSlider, "Lookahead");
    setupSlider(kneeSlider,      "Knee");
    setupSlider(outputSlider,    "Output");

    holdSlider.setTextValueSuffix(" ms");
    lookaheadSlider.setTextValueSuffix(" ms");
    lookaheadSlider.setNumDecimalPlacesToDisplay(0);

    setupLabel(thresholdLabel, "Threshold");
    setupLabel(ratioLabel,     "Ratio");
    setupLabel(attackLabel,    "Attack");
    setupLabel(holdLabel,      "Hold");
    setupLabel(releaseLabel,   "Release");
    setupLabel(lookaheadLabel, "Lookahead");
    setupLabel(kneeLabel,      "Knee");
    setupLabel(outputLabel,    "Output");

    sidechainButton.setButtonText("Sidechain");
    softKneeButton.setButtonText("Soft Knee");
    limiterButton.setButtonText("Hard Duck");
    addAndMakeVisible(sidechainButton);
    addAndMakeVisible(softKneeButton);
    addAndMakeVisible(limiterButton);

    addAndMakeVisible(vuMeter);

    // --- Tooltips
    thresholdSlider .setTooltip("Sidechain level above which ducking begins (dB).");
    ratioSlider     .setTooltip("Ducking slope above threshold. 4:1 = 0.75 dB GR per dB of sidechain excess.");
    attackSlider    .setTooltip("How fast the ducking ramps on (ms).");
    holdSlider      .setTooltip("Minimum time the gain reduction is held at its peak before release begins (ms).");
    releaseSlider   .setTooltip("How fast the ducking ramps off after the sidechain falls below threshold (ms).");
    lookaheadSlider .setTooltip("Delays the main signal so the ducking aligns with the sidechain transient (ms). Adds host latency.");
    kneeSlider      .setTooltip("Width of the soft transition around the threshold (dB).");
    outputSlider    .setTooltip("Make-up gain applied after the dynamics stage (dB).");

    sidechainButton .setTooltip("Use the external sidechain bus as the trigger. When off, ducking is driven by the main input.");
    softKneeButton  .setTooltip("Smooth the curve around the threshold instead of a hard corner.");
    limiterButton   .setTooltip("Force 1:1 sidechain-excess to gain reduction (steepest possible ducking). Overrides the Ratio knob.");

    thresholdAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "threshold", thresholdSlider);
    ratioAttachment     = std::make_unique<SliderAttachment>(audioProcessor.apvts, "ratio", ratioSlider);
    attackAttachment    = std::make_unique<SliderAttachment>(audioProcessor.apvts, "attack", attackSlider);
    holdAttachment      = std::make_unique<SliderAttachment>(audioProcessor.apvts, "hold", holdSlider);
    releaseAttachment   = std::make_unique<SliderAttachment>(audioProcessor.apvts, "release", releaseSlider);
    lookaheadAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "lookahead", lookaheadSlider);
    kneeAttachment      = std::make_unique<SliderAttachment>(audioProcessor.apvts, "knee", kneeSlider);
    outputAttachment    = std::make_unique<SliderAttachment>(audioProcessor.apvts, "output", outputSlider);

    sidechainAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "sidechainEnable", sidechainButton);
    softKneeAttachment  = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "softKnee", softKneeButton);
    limiterAttachment   = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "limiter", limiterButton);

    // ----- Sidechain Mode group -----
    setupGroup(modeGroup, "Sidechain Mode");

    const juce::String modeNames[3] = { "Wideband", "Multiband", "Spectral" };
    const juce::String modeTips [3] = {
        "Wideband: detect the whole sidechain spectrum at once.",
        "Multiband: split the sidechain at the crossovers and detect per band, then sum the per-band ducking.",
        "Spectral: frequency-domain detection over the sidechain FFT spectrum."
    };
    for (int i = 0; i < 3; ++i)
    {
        modeButtons[i].setButtonText(modeNames[i]);
        modeButtons[i].setClickingTogglesState(false);
        modeButtons[i].setRadioGroupId(0x101);
        modeButtons[i].setTooltip(modeTips[i]);
        addAndMakeVisible(modeButtons[i]);
        modeButtons[i].onClick = [this, i] { setModeFromButton(i); };
    }

    fftConvButton.setButtonText("FFT Conv");
    fftConvButton.setTooltip("Switch detection to FFT convolution. Adds latency proportional to the FFT size.");
    addAndMakeVisible(fftConvButton);
    fftConvAttachment = std::make_unique<ButtonAttachment>(
        audioProcessor.apvts, "fftConvolution", fftConvButton);

    const juce::String convSizeNames[3] = { "512", "1024", "2048" };
    const juce::String convSizeTips [3] = {
        "512 samples — lowest latency, coarsest frequency resolution.",
        "1024 samples — balanced latency/resolution.",
        "2048 samples — highest frequency resolution, highest latency."
    };
    for (int i = 0; i < 3; ++i)
    {
        convSizeButtons[i].setButtonText(convSizeNames[i]);
        convSizeButtons[i].setClickingTogglesState(false);
        convSizeButtons[i].setRadioGroupId(0x102);
        convSizeButtons[i].setTooltip(convSizeTips[i]);
        addAndMakeVisible(convSizeButtons[i]);
        convSizeButtons[i].onClick = [this, i] { setConvSizeFromButton(i); };
    }

    convLatencyLabel.setText("Latency: 0 samples (0.0 ms)", juce::dontSendNotification);
    convLatencyLabel.setJustificationType(juce::Justification::centredLeft);
    convLatencyLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    convLatencyLabel.setTooltip("Reported plugin latency in samples and milliseconds. Increases with FFT size.");
    addAndMakeVisible(convLatencyLabel);

    // ----- Crossover group -----
    setupGroup(crossoverGroup, "Crossover Frequencies");

    const juce::String xoverNames[] = { "Low", "Low-Mid", "High-Mid", "High" };
    const juce::String xoverIds[]   = { "xover0", "xover1", "xover2", "xover3" };
    const juce::String xoverTips[]  = {
        "Crossover frequency between band 1 and band 2.",
        "Crossover frequency between band 2 and band 3.",
        "Crossover frequency between band 3 and band 4.",
        "Crossover frequency between band 4 and band 5."
    };

    for (int i = 0; i < 4; ++i)
    {
        mdLookAndFeel.applyRotaryConfiguration(xoverSliders[i]);
        xoverSliders[i].setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
        xoverSliders[i].setTextValueSuffix(" Hz");
        xoverSliders[i].setNumDecimalPlacesToDisplay(0);
        xoverSliders[i].setTooltip(xoverTips[i]);
        addAndMakeVisible(xoverSliders[i]);

        xoverLabels[i].setText(xoverNames[i], juce::dontSendNotification);
        xoverLabels[i].setJustificationType(juce::Justification::centred);
        xoverLabels[i].setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(xoverLabels[i]);

        xoverAttachments[i] = std::make_unique<SliderAttachment>(
            audioProcessor.apvts, xoverIds[i], xoverSliders[i]);
    }

    // ----- Per-band duck scale group (hidden in UI but DSP attachments remain) -----
    // E3: section is invisible in this build; the APVTS attachments are kept so the
    // DSP keeps reading the band-scale params (which default to 0 dB = neutral).
    setupGroup(bandScaleGroup, "Per-Band Duck Scale");
    bandScaleGroup.setVisible(false);

    const juce::String bandNames[] = { "B1", "B2", "B3", "B4", "B5" };
    for (int i = 0; i < 5; ++i)
    {
        mdLookAndFeel.applyRotaryConfiguration(bandScaleSliders[i]);
        mdLookAndFeel.applyBipolarBehavior(bandScaleSliders[i], 0.0);
        bandScaleSliders[i].setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
        bandScaleSliders[i].setTextValueSuffix(" dB");
        bandScaleSliders[i].setNumDecimalPlacesToDisplay(1);
        bandScaleSliders[i].setTooltip("Per-band ducking depth offset (dB). Negative = less ducking in this band, positive = more.");
        addChildComponent(bandScaleSliders[i]);

        bandScaleLabels[i].setText(bandNames[i], juce::dontSendNotification);
        bandScaleLabels[i].setJustificationType(juce::Justification::centred);
        bandScaleLabels[i].setColour(juce::Label::textColourId, juce::Colours::white);
        addChildComponent(bandScaleLabels[i]);

        bandScaleAttachments[i] = std::make_unique<SliderAttachment>(
            audioProcessor.apvts, "bandScale" + juce::String(i), bandScaleSliders[i]);
    }

    // ----- Post-ducking EQ group -----
    setupGroup(eqGroup, "Post-Ducking EQ");

    const juce::String eqLabelsText[] = {
        "Low Hz", "Low dB", "Mid Hz", "Mid dB", "Mid Q", "Hi Hz", "Hi dB"
    };
    const juce::String eqIds[] = {
        "eqLowFreq", "eqLowGain", "eqMidFreq", "eqMidGain", "eqMidQ", "eqHighFreq", "eqHighGain"
    };
    const juce::String eqSuffix[] = { " Hz", " dB", " Hz", " dB", "", " Hz", " dB" };
    const int          eqDecimals[] = { 0, 1, 0, 1, 2, 0, 1 };

    // EQ gain knobs (low, mid, high) are bipolar in dB, centred at 0.
    const bool eqGainIndex[] = { false, true, false, true, false, false, true };
    const juce::String eqTips[] = {
        "Low-shelf corner frequency (Hz).",
        "Low-shelf gain (dB). Centred at 0; amber past extreme.",
        "Mid bell centre frequency (Hz).",
        "Mid bell gain (dB). Centred at 0; amber past extreme.",
        "Mid bell Q — narrower at higher values.",
        "High-shelf corner frequency (Hz).",
        "High-shelf gain (dB). Centred at 0; amber past extreme."
    };

    for (int i = 0; i < 7; ++i)
    {
        mdLookAndFeel.applyRotaryConfiguration(eqSliders[i]);
        if (eqGainIndex[i])
            mdLookAndFeel.applyBipolarBehavior(eqSliders[i], 0.0);
        eqSliders[i].setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
        eqSliders[i].setTextValueSuffix(eqSuffix[i]);
        eqSliders[i].setNumDecimalPlacesToDisplay(eqDecimals[i]);
        eqSliders[i].setTooltip(eqTips[i]);
        addAndMakeVisible(eqSliders[i]);

        eqLabels[i].setText(eqLabelsText[i], juce::dontSendNotification);
        eqLabels[i].setJustificationType(juce::Justification::centred);
        eqLabels[i].setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(eqLabels[i]);

        eqAttachments[i] = std::make_unique<SliderAttachment>(
            audioProcessor.apvts, eqIds[i], eqSliders[i]);
    }

    // E6: listen for Hard Duck changes so we can lock/unlock the ratio knob.
    audioProcessor.apvts.addParameterListener("limiter", this);

    // Initial sync of the lock state — only adjust the slider's enabled flag and
    // remember the current ratio. We deliberately don't re-write the param value
    // here so reloading a saved state with limiter ON keeps the ratio at 1:1
    // (which the DSP ignores anyway when limiter is on) and limiter OFF keeps
    // the user's stored ratio.
    {
        previousRatioValue = *audioProcessor.apvts.getRawParameterValue("ratio");
        const bool limiterOn = (*audioProcessor.apvts.getRawParameterValue("limiter") > 0.5f);
        ratioSlider.setEnabled(! limiterOn);
    }

    startTimerHz(30);
    setSize(kWindowW, kWindowH);

    setResizable(true, true);
    setResizeLimits(600, 500, 1400, 1100);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio(static_cast<double>(kWindowW) / static_cast<double>(kWindowH));
}

MaybeDuckAudioProcessorEditor::~MaybeDuckAudioProcessorEditor()
{
    audioProcessor.apvts.removeParameterListener("limiter", this);
    setLookAndFeel(nullptr);
}

void MaybeDuckAudioProcessorEditor::parameterChanged(const juce::String& paramID, float newValue)
{
    if (paramID == "limiter")
    {
        // Hop to the message thread — parameter listeners may fire from the
        // audio thread, and we touch component state below.
        const bool limiterOn = newValue > 0.5f;
        juce::MessageManager::callAsync([this, limiterOn]
        {
            applyLimiterRatioLock(limiterOn);
        });
    }
}

void MaybeDuckAudioProcessorEditor::applyLimiterRatioLock(bool limiterOn)
{
    auto* ratioParam = audioProcessor.apvts.getParameter("ratio");
    if (ratioParam == nullptr)
        return;

    if (limiterOn)
    {
        // Cache the user's chosen ratio, then snap to 1:1.
        const float currentRatio = *audioProcessor.apvts.getRawParameterValue("ratio");
        if (std::abs(currentRatio - 1.0f) > 0.01f)
            previousRatioValue = currentRatio;

        ratioParam->beginChangeGesture();
        ratioParam->setValueNotifyingHost(ratioParam->convertTo0to1(1.0f));
        ratioParam->endChangeGesture();
        ratioSlider.setEnabled(false);
    }
    else
    {
        ratioSlider.setEnabled(true);
        // Restore the cached ratio for a friendlier UX.
        ratioParam->beginChangeGesture();
        ratioParam->setValueNotifyingHost(ratioParam->convertTo0to1(previousRatioValue));
        ratioParam->endChangeGesture();
    }
}

void MaybeDuckAudioProcessorEditor::setModeFromButton(int idx)
{
    if (auto* p = audioProcessor.apvts.getParameter("sidechainMode"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(idx)));
        p->endChangeGesture();
    }
}

void MaybeDuckAudioProcessorEditor::setConvSizeFromButton(int idx)
{
    if (auto* p = audioProcessor.apvts.getParameter("fftConvSize"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(idx)));
        p->endChangeGesture();
    }
}

//==============================================================================
void MaybeDuckAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();

    mdLookAndFeel.drawBrushedChassis(g, area);

    // Four corner screws — varied slot angles so they don't look stamped.
    const float screwR = 7.0f;
    const float inset  = 12.0f;
    static constexpr float kScrewAngles[4] = {
        0.42f, 1.18f, 2.05f, 2.71f
    };
    const juce::Point<float> screwCentres[4] = {
        { area.getX() + inset,         area.getY() + inset },
        { area.getRight() - inset,     area.getY() + inset },
        { area.getX() + inset,         area.getBottom() - inset },
        { area.getRight() - inset,     area.getBottom() - inset }
    };
    for (int i = 0; i < 4; ++i)
        mdLookAndFeel.drawScrew(g, screwCentres[i], screwR, kScrewAngles[i]);

    // Wordmark + version at bottom-right, engraved into the chassis.
    const juce::Font wordmarkFont(mdLookAndFeel.getMediumTypeface());
    const juce::Font versionFont (mdLookAndFeel.getRegularTypeface());

    juce::Rectangle<float> wordmarkArea(area.getRight() - 130.0f,
                                        area.getBottom() - 36.0f, 110.0f, 16.0f);
    juce::Rectangle<float> versionArea (area.getRight() - 130.0f,
                                        area.getBottom() - 20.0f, 110.0f, 12.0f);

    mdLookAndFeel.drawEngravedText(g, "MAYBEDUCK", wordmarkArea,
                                 juce::Justification::centredRight,
                                 wordmarkFont.withHeight(14.0f));
    mdLookAndFeel.drawEngravedText(g, "v1.0",       versionArea,
                                 juce::Justification::centredRight,
                                 versionFont.withHeight(9.5f));
}

void MaybeDuckAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(kOuterMargin);

    // --- Mode group (moved to the top of the editor) -----------------------
    // 2 rows: mode select + conv config. Buttons are bigger now (E10).
    constexpr int modeRowH   = 44;
    constexpr int modeGroupH = kGroupTitleH + modeRowH * 2 + 14;
    auto modeArea = area.removeFromTop(modeGroupH);
    modeGroup.setBounds(modeArea);

    auto modeInner = modeArea.reduced(8, 6);
    modeInner.removeFromTop(kGroupTitleH - 8);

    auto modeRow = modeInner.removeFromTop(modeRowH);
    constexpr int modeBtnW = 150;
    for (int i = 0; i < 3; ++i)
        modeButtons[i].setBounds(modeRow.removeFromLeft(modeBtnW).reduced(2));

    // The conv row's bounds are allocated unconditionally so the section height
    // is stable even when the conv controls are hidden (see timerCallback for
    // the spectral-mode visibility logic — E9).
    auto convRow = modeInner.removeFromTop(modeRowH);
    fftConvButton.setBounds(convRow.removeFromLeft(140).reduced(2));
    for (int i = 0; i < 3; ++i)
        convSizeButtons[i].setBounds(convRow.removeFromLeft(70).reduced(2));
    convLatencyLabel.setBounds(convRow.reduced(4));

    area.removeFromTop(kGroupGap);

    // --- Dynamics group: 8-knob row + toggles row, GR meter on the right ---
    constexpr int dynamicsKnobsH  = 130;
    constexpr int dynamicsToggleH = 32;
    constexpr int dynamicsGroupH  = kGroupTitleH + dynamicsKnobsH + dynamicsToggleH + 14;

    auto dynamicsArea = area.removeFromTop(dynamicsGroupH);
    dynamicsGroup.setBounds(dynamicsArea);

    auto dynamicsInner = dynamicsArea.reduced(8, 6);
    dynamicsInner.removeFromTop(kGroupTitleH - 8);   // leave space for group title

    // Three-strip meter (IN / SC / GR) lives in the wider slot on the right.
    auto meterArea = dynamicsInner.removeFromRight(230);
    vuMeter.setBounds(meterArea);

    auto dynKnobs = dynamicsInner.removeFromTop(dynamicsKnobsH);
    const int colW = dynKnobs.getWidth() / 8;
    auto layoutKnob = [&](juce::Slider& s, juce::Label& l)
    {
        auto col = dynKnobs.removeFromLeft(colW).reduced(4);
        l.setBounds(col.removeFromTop(18));
        s.setBounds(col);
    };
    layoutKnob(thresholdSlider, thresholdLabel);
    layoutKnob(ratioSlider,     ratioLabel);
    layoutKnob(attackSlider,    attackLabel);
    layoutKnob(holdSlider,      holdLabel);
    layoutKnob(releaseSlider,   releaseLabel);
    layoutKnob(lookaheadSlider, lookaheadLabel);
    layoutKnob(kneeSlider,      kneeLabel);
    layoutKnob(outputSlider,    outputLabel);

    auto toggles = dynamicsInner.removeFromTop(dynamicsToggleH);
    sidechainButton.setBounds(toggles.removeFromLeft(170));
    softKneeButton.setBounds (toggles.removeFromLeft(120));
    limiterButton.setBounds  (toggles.removeFromLeft(100));

    area.removeFromTop(kGroupGap);

    // --- Crossover group: 4 knobs ------------------------------------------
    constexpr int xoverRowH   = 108;
    constexpr int xoverGroupH = kGroupTitleH + xoverRowH + 14;
    auto xoverArea = area.removeFromTop(xoverGroupH);
    crossoverGroup.setBounds(xoverArea);

    auto xoverInner = xoverArea.reduced(8, 6);
    xoverInner.removeFromTop(kGroupTitleH - 8);
    auto xoverKnobs = xoverInner.removeFromTop(xoverRowH);
    const int xoverColW = xoverKnobs.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
    {
        auto col = xoverKnobs.removeFromLeft(xoverColW).reduced(4);
        xoverLabels[i].setBounds(col.removeFromTop(18));
        xoverSliders[i].setBounds(col);
    }

    area.removeFromTop(kGroupGap);

    // --- Per-band duck scale group: hidden in this UI pass (E3). -----------
    // We don't claim any vertical space — the EQ section moves up to fill the gap.
    // Children remain invisible so the DSP keeps reading their (neutral) values.

    // --- Post-ducking EQ group: 7 knobs ------------------------------------
    constexpr int eqRowH   = 96;
    constexpr int eqGroupH = kGroupTitleH + eqRowH + 14;
    auto eqArea = area.removeFromTop(eqGroupH);
    eqGroup.setBounds(eqArea);

    auto eqInner = eqArea.reduced(8, 6);
    eqInner.removeFromTop(kGroupTitleH - 8);
    auto eqKnobs = eqInner.removeFromTop(eqRowH);
    const int eqColW = eqKnobs.getWidth() / 7;
    for (int i = 0; i < 7; ++i)
    {
        auto col = eqKnobs.removeFromLeft(eqColW).reduced(4);
        eqLabels[i].setBounds(col.removeFromTop(18));
        eqSliders[i].setBounds(col);
    }
}

void MaybeDuckAudioProcessorEditor::timerCallback()
{
    // Derive an absolute-dB sidechain level for the IN/SC strips on the meter.
    const float threshDb = audioProcessor.apvts.getRawParameterValue("threshold")->load();
    const float scDb     = threshDb + audioProcessor.getAboveThresholdDb();

    vuMeter.setLevels(audioProcessor.getInputLevelDb(),
                      scDb,
                      audioProcessor.getGainReductionAmountDb());

    // --- Sync mode + conv-size button visual state from APVTS via toggle state
    // so the LookAndFeel-drawn LED dot reflects the active radio.
    const int mode = juce::jlimit(0, 2,
        static_cast<int>(*audioProcessor.apvts.getRawParameterValue("sidechainMode")));
    const int sizeIdx = juce::jlimit(0, 2,
        static_cast<int>(*audioProcessor.apvts.getRawParameterValue("fftConvSize")));
    const bool fftConvOn = (*audioProcessor.apvts.getRawParameterValue("fftConvolution") > 0.5f);
    const bool spectralActive = (mode == 2);

    for (int i = 0; i < 3; ++i)
        modeButtons[i].setToggleState(i == mode, juce::dontSendNotification);

    // E9: collapse the conv config controls when not in Spectral mode. The
    // convRow's space stays allocated in resized() so the section height
    // doesn't churn — just the controls themselves disappear.
    fftConvButton.setVisible(spectralActive);
    for (int i = 0; i < 3; ++i)
    {
        convSizeButtons[i].setVisible(spectralActive && fftConvOn);
        convSizeButtons[i].setToggleState(i == sizeIdx, juce::dontSendNotification);
    }
    // Latency label stays visible — relevant for lookahead even when FFT is off.

    // Crossover, band-scale, and band-routing controls are inert in Wideband
    for (int i = 0; i < 4; ++i) xoverSliders[i].setEnabled(mode != 0);
    for (int i = 0; i < 5; ++i) bandScaleSliders[i].setEnabled(mode != 0);

    const int latSamples = audioProcessor.getConvLatencySamples();
    const double sr = juce::jmax(1.0, audioProcessor.getCurrentSampleRateHz());
    const double latMs = 1000.0 * latSamples / sr;
    convLatencyLabel.setText("Latency: " + juce::String(latSamples) + " smp ("
                              + juce::String(latMs, 1) + " ms)",
                              juce::dontSendNotification);
}
