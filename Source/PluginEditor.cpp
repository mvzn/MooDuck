/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ThemeState.h"

// ============================ HOW TO RE-LAYOUT ==============================
// These are the global layout knobs. The window opens at kWindowW x kWindowH
// (and that ratio is locked in the constructor via setFixedAspectRatio, so the
// window only ever scales proportionally). kOuterMargin is the dead border
// around everything; kGroupGap is the vertical space between sections;
// kGroupTitleH is the height reserved for each section's engraved title row.
// Per-section heights (knob-row heights etc.) live as local constexprs inside
// resized() — see that method to resize or reorder individual sections.
// ===========================================================================
namespace {
    // >>> PLUGIN WINDOW SIZE <<< — the UI is fixed-size (resizing is disabled in the
    // constructor). The sections below are fixed pixels summing to ~605 tall plus
    // margins; keep kWindowH comfortably above that so nothing crowds the bottom.
    // To change the overall size, retune the section heights too.
    constexpr int kWindowW       = 820;
    constexpr int kWindowH       = 858;   // Pultec rack + (taller) Saturation rack + top wordmark band
    constexpr int kOuterMargin   = 22;
    constexpr int kGroupGap      = 8;    // breathing room between sections
    constexpr int kGroupTitleH   = 22;
    constexpr int kKnobLabelH    = 18;   // height of the caption above every knob

    juce::Colour groupTextColour = juce::Colours::lightgrey;
}

//==============================================================================
MaybeDuckAudioProcessorEditor::MaybeDuckAudioProcessorEditor (MaybeDuckAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // setLookAndFeel installs our custom MaybeDuckLookAndFeel on this editor and
    // every child below it — that's what makes the knobs/buttons/labels render
    // with the brushed-metal style instead of JUCE's stock grey widgets.
    setLookAndFeel(&mdLookAndFeel);

    // The cached background fills the whole window opaquely — tell JUCE so it can
    // skip compositing anything behind the editor.
    setOpaque(true);

    // ---- Shared "setup" helpers -------------------------------------------
    // Every knob, text label, and section header is created through one of these
    // three lambdas, so they're the one place to change defaults that apply to
    // ALL of that widget type. Want every knob's value box wider, or every label
    // a different colour? Edit it here once rather than per-control. addAndMakeVisible
    // registers the component as a child so it actually draws and receives mouse input.

    // Restyle a slider's value read-out box: dark matte plate, with the border and
    // the number both in the engraved off-white. Applied per-slider because a
    // slider bakes its value-box colours at setTextBoxStyle time — before it is
    // parented to our LookAndFeel — so setting them on the LookAndFeel alone leaves
    // the JUCE default (blue-grey) border in place.
    // styleValueBox() is a member (defined below) so applyTheme() can re-run it on
    // every slider when the palette flips.

    // A rotary knob: applies the 270° rotary config + a value read-out box below it.
    auto setupSlider = [this](juce::Slider& s, const juce::String& name)
    {
        mdLookAndFeel.applyRotaryConfiguration(s);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
        styleValueBox(s);
        s.setName(name);
        addAndMakeVisible(s);
    };

    // The static caption sitting above each knob (the LookAndFeel draws it engraved).
    auto setupLabel = [this](juce::Label& l, const juce::String& text)
    {
        l.setText(text, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(l);
    };

    // A section header: title text + the engraved divider line drawn by the LookAndFeel.
    auto setupGroup = [this](juce::GroupComponent& g, const juce::String& title)
    {
        g.setText(title);
        g.setColour(juce::GroupComponent::outlineColourId, juce::Colours::white.withAlpha(0.25f));
        g.setColour(juce::GroupComponent::textColourId,   groupTextColour);
        g.setTextLabelPosition(juce::Justification::centredLeft);
        addAndMakeVisible(g);
    };

    // ======================= HOW TO ADD/EDIT A CONTROL =======================
    // The rest of this constructor BUILDS the controls (text, tooltips, suffixes,
    // ranges). resized() further down POSITIONS them. A control has three parts:
    //   1. Declare the member (juce::Slider / ToggleButton / Label) in PluginEditor.h
    //   2. Configure it here (setupSlider/Label/Group, .setButtonText, .setTooltip...)
    //   3. Bind it to a parameter with a *Attachment so moving it updates the DSP
    //      (the parameter ID string, e.g. "threshold", must match one registered in
    //       PluginProcessor.cpp's createParameterLayout).
    // To just rename a knob or change its tooltip/units, only step 2 below matters.
    // setTextValueSuffix adds the unit shown in the value box (e.g. " ms", " Hz").
    // =========================================================================

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
    // Lookahead's " ms" + integer formatting come from the param's stringFromValue
    // (PluginProcessor), so no slider-side suffix here (that would double it).

    // Ratio and Lookahead are stepped — give them a tick at each step position
    // (where the dot lands) instead of the uniform Moog graduation. The counts come
    // straight from the processor's step tables, so they can never fall out of sync.
    ratioSlider.getProperties().set("stepTicks", MaybeDuckAudioProcessor::kNumRatioSteps);
    lookaheadSlider.getProperties().set("stepTicks", MaybeDuckAudioProcessor::kNumLookaheadSteps);

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
    xtremeLookaheadButton.setButtonText("Xtreme Lookahead");
    addAndMakeVisible(sidechainButton);
    addAndMakeVisible(softKneeButton);
    addAndMakeVisible(limiterButton);
    addAndMakeVisible(xtremeLookaheadButton);

    addAndMakeVisible(levelMeter);

    // --- Tooltips
    thresholdSlider .setTooltip("Sidechain level above which ducking begins (dB).");
    ratioSlider     .setTooltip("Ducking slope above threshold (e.g. 4:1 = 0.75 dB gain reduction per dB of sidechain over threshold). Hard Duck overrides this with brick-wall " + juce::String(juce::CharPointer_UTF8("\xE2\x88\x9E")) + ":1.");
    attackSlider    .setTooltip("How fast the ducking ramps on (ms).");
    holdSlider      .setTooltip("Minimum time the gain reduction is held at its peak before release begins (ms).");
    releaseSlider   .setTooltip("How fast the ducking ramps off after the sidechain falls below threshold (ms).");
    lookaheadSlider .setTooltip("Delays the main signal so the ducking aligns with the sidechain transient (ms). Adds host latency.");
    kneeSlider      .setTooltip("Width of the soft transition around the threshold (dB).");
    outputSlider    .setTooltip("Make-up gain applied after the dynamics stage (dB).");

    sidechainButton .setTooltip("Use the external sidechain bus as the trigger. When off, the compressor self-detects the main input (spectral mode falls back to wideband).");
    softKneeButton  .setTooltip("Smooth the curve around the threshold instead of a hard corner.");
    limiterButton   .setTooltip("Hard Duck: brick-wall ducking (" + juce::String(juce::CharPointer_UTF8("\xE2\x88\x9E")) + ":1). Every dB the sidechain exceeds the threshold removes 1 dB from the main signal. Overrides the Ratio knob, which reads " + juce::String(juce::CharPointer_UTF8("\xE2\x88\x9E")) + ":1 while engaged.");
    xtremeLookaheadButton.setTooltip("Xtreme Lookahead: switches the Lookahead knob to a wide creative range (50 ms - 10 s) instead of the normal 0-16 ms. Adds large host latency — for offline/mix use, not tracking.");

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
    xtremeLookaheadAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "xtremeLookahead", xtremeLookaheadButton);

    // ----- Sidechain Mode group -----
    setupGroup(modeGroup, "Modes");

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
    setupGroup(crossoverGroup, "Crossover");

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
        styleValueBox(xoverSliders[i]);
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

    // Band solo/mute switches — soft backlit momentary rectangles in the ruler.
    // Single click cycles Normal -> Solo -> Mute (see cycleBandState); the timer
    // syncs each switch's backlight colour + enabled state from its bandState param.
    const juce::String bandSwitchNames[5] = { "Low", "Lo-Mid", "Mid", "Hi-Mid", "High" };
    for (int i = 0; i < 5; ++i)
    {
        bandSwitches[i].setButtonText(bandSwitchNames[i]);
        bandSwitches[i].setClickingTogglesState(false);
        bandSwitches[i].getProperties().set("softSwitch", true);   // low-relief momentary LookAndFeel variant
        bandSwitches[i].getProperties().set("alwaysLit", true);    // backlight always on; colour = state
        bandSwitches[i].setTooltip("Band " + juce::String(i + 1)
            + " — click to cycle Normal / Solo (blue) / Mute (red). Console-style on the band output; Multiband & Spectral (not FFT-Conv).");
        bandSwitches[i].onClick = [this, i] { cycleBandState(i); };
        addAndMakeVisible(bandSwitches[i]);
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
        styleValueBox(bandScaleSliders[i]);
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

    // ----- Pultec EQP-1A group -----
    setupGroup(pultecGroup, "Post PulEQ");
    setupGroup(pultecSatGroup, "Saturation");

    // Top "MaybeDuck" wordmark hotspot: tooltip + triple-click theme toggle.
    wordmarkHotspot.setTooltip("Triple-click to toggle light / dark theme");
    wordmarkHotspot.onMouseDown = [this](const juce::MouseEvent& e)
    {
        if (e.getNumberOfClicks() >= 3)
        {
            ThemeState::get().toggle();
            applyTheme();
        }
    };
    addAndMakeVisible(wordmarkHotspot);

    // Real EQP-1A front-panel terminology: LF Sel / LF Boost / LF Atten / HF Sel /
    // HF Boost / Bandwidth / Atten Sel / HF Atten. Avoids the prior "HF Atten Hz"
    // ambiguity (the Atten Sel switch picks the cut shelf corner, not the boost band).
    const juce::String pultecLabelsText[8] = {
        "LF Sel", "LF Boost", "LF Atten", "HF Sel", "HF Boost", "Bandwidth", "Atten Sel", "HF Atten"
    };
    const juce::String pultecIds[8] = {
        "pultecLfFreq", "pultecLfBoost", "pultecLfAtten",
        "pultecHfFreq", "pultecHfBoost", "pultecHfBw", "pultecHfAttenFreq", "pultecHfAtten"
    };
    // LF/HF/Atten Sel are stepped Int params (Hz selector indices). Boost/Atten are dB.
    // Bandwidth is a 0–1 ratio (0=narrow, 1=wide).
    const juce::String pultecSuffix[8]  = { "", " dB", " dB", "", " dB", "", "", " dB" };
    const int          pultecDecimals[8] = { 0,  1,     1,    0,  1,     2,  0,  1     };
    const juce::String pultecTips[8] = {
        "LF Selector (CPS): 20 / 30 / 40 / 60 / 100.",
        "LF passive shelf boost (0–16 dB).",
        "LF cut amount (0–16 dB). Combine with LF Boost for the Pultec ‘low end trick’.",
        "HF Selector (KCPS): 3 / 4 / 5 / 8 / 10 / 12 / 16.",
        "HF passive resonant peak boost (0–16 dB).",
        "Bandwidth of HF peak: 0 = narrow (high-Q), 1 = wide (broad shelf).",
        "Atten Selector (KCPS): 5 / 10 / 20. Picks the HF cut shelf corner. "
        "Note: even at HF Atten = 0 dB this selector colours the passive network — "
        "20 KCPS is flattest, lower positions add gentle HF roll-off.",
        "HF shelf cut amount (0–14 dB)."
    };

    for (int i = 0; i < 8; ++i)
    {
        mdLookAndFeel.applyRotaryConfiguration(pultecSliders[i]);
        pultecSliders[i].setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
        styleValueBox(pultecSliders[i]);
        pultecSliders[i].setTextValueSuffix(pultecSuffix[i]);
        pultecSliders[i].setNumDecimalPlacesToDisplay(pultecDecimals[i]);
        pultecSliders[i].setTooltip(pultecTips[i]);
        addAndMakeVisible(pultecSliders[i]);

        pultecLabels[i].setText(pultecLabelsText[i], juce::dontSendNotification);
        pultecLabels[i].setJustificationType(juce::Justification::centred);
        pultecLabels[i].setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(pultecLabels[i]);

        pultecAttachments[i] = std::make_unique<SliderAttachment>(
            audioProcessor.apvts, pultecIds[i], pultecSliders[i]);
    }

    // Bypass + Sat use the soft-switch style (like the crossover band switches),
    // lit only when engaged. Sat glows blood-orange (tube-drive); Bypass glows red.
    pultecSatButton.setButtonText("Sat");
    pultecSatButton.setTooltip("Two-tube EQP-1A makeup-amp saturation (4× oversampled). Glows dim green, brightening toward red with output level.");
    pultecSatButton.getProperties().set("softSwitch", true);
    // Initial lit colour: dim green. timerCallback() drives it green->red by level.
    pultecSatButton.getProperties().set("litColour", static_cast<juce::int64>(
        static_cast<juce::uint32>(juce::Colour::fromHSV(0.33f, 0.85f, 0.40f, 1.0f).getARGB())));
    addAndMakeVisible(pultecSatButton);
    pultecSatAttachment = std::make_unique<ButtonAttachment>(
        audioProcessor.apvts, "pultecSatEnable", pultecSatButton);

    pultecBypassButton.setButtonText("Bypass");
    pultecBypassButton.setTooltip("Bypass the Pultec EQ entirely (pure passthrough — no oversampling, no WDF).");
    // Drawn as a vintage Pultec bat toggle switch (handle up = bypassed).
    pultecBypassButton.getProperties().set("pultecToggle", true);
    pultecBypassButton.getProperties().set("litColour", static_cast<juce::int64>(MaybeDuckLookAndFeel::kBandMute)); // glow when bypassed
    addAndMakeVisible(pultecBypassButton);
    pultecBypassAttachment = std::make_unique<ButtonAttachment>(
        audioProcessor.apvts, "pultecBypass", pultecBypassButton);

    // Tube-saturation controls (P6): Drive / Mix / Trim — rotary knobs matching
    // the Pultec EQ knob styling, on a third row of the "Post PulEQ" group.
    const juce::String satLabelsText[3] = { "Drive", "Mix", "Trim" };
    const juce::String satIds[3]        = { "pultecSatDrive", "pultecSatMix", "pultecSatTrim" };
    const juce::String satSuffix[3]     = { "", " %", " dB" };
    const int          satDecimals[3]   = { 2, 0, 1 };
    const juce::String satTips[3] = {
        "Saturation drive — scales the 12AX7 grid excursion (1.0 = authentic EQP-1A operating point).",
        "Parallel dry/wet for the tube saturation (100% = full tube, 0% = clean post-EQ).",
        "Output trim on the saturated path — for loudness-matched A/B."
    };
    for (int i = 0; i < 3; ++i)
    {
        mdLookAndFeel.applyRotaryConfiguration(pultecSatSliders[i]);
        pultecSatSliders[i].setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
        styleValueBox(pultecSatSliders[i]);
        pultecSatSliders[i].setTextValueSuffix(satSuffix[i]);
        pultecSatSliders[i].setNumDecimalPlacesToDisplay(satDecimals[i]);
        pultecSatSliders[i].setTooltip(satTips[i]);
        addAndMakeVisible(pultecSatSliders[i]);

        pultecSatLabels[i].setText(satLabelsText[i], juce::dontSendNotification);
        pultecSatLabels[i].setJustificationType(juce::Justification::centred);
        pultecSatLabels[i].setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(pultecSatLabels[i]);

        pultecSatSliderAttachments[i] = std::make_unique<SliderAttachment>(
            audioProcessor.apvts, satIds[i], pultecSatSliders[i]);
    }

    // Listen for Hard Duck / Xtreme Lookahead so we can freeze + relabel the knobs
    // they override (ratio and lookahead respectively).
    audioProcessor.apvts.addParameterListener("limiter", this);
    audioProcessor.apvts.addParameterListener("xtremeLookahead", this);

    // Initial sync — freeze + relabel if loaded with either already engaged. The
    // underlying params are never re-written (the DSP ignores them while engaged),
    // so the user's stored ratio / lookahead are kept.
    applyLimiterRatioLock(*audioProcessor.apvts.getRawParameterValue("limiter") > 0.5f);
    applyXtremeLookaheadLock(*audioProcessor.apvts.getRawParameterValue("xtremeLookahead") > 0.5f);

    startTimerHz(30);
    setSize(kWindowW, kWindowH);

    // Fixed-size UI: the layout is hand-tuned to one window size, so resizing is
    // disabled outright (no resize limits or aspect constrainer needed).
    setResizable(false, false);

    // Sync to the persisted dark/light theme. Must run AFTER every slider is built
    // (it re-pushes their value-box colours from the active palette).
    applyTheme();
}

MaybeDuckAudioProcessorEditor::~MaybeDuckAudioProcessorEditor()
{
    audioProcessor.apvts.removeParameterListener("limiter", this);
    audioProcessor.apvts.removeParameterListener("xtremeLookahead", this);
    setLookAndFeel(nullptr);
}

void MaybeDuckAudioProcessorEditor::parameterChanged(const juce::String& paramID, float newValue)
{
    // Hop to the message thread — parameter listeners may fire from the audio
    // thread, and we touch component state below.
    const bool on = newValue > 0.5f;
    if (paramID == "limiter")
        juce::MessageManager::callAsync([this, on] { applyLimiterRatioLock(on); });
    else if (paramID == "xtremeLookahead")
        juce::MessageManager::callAsync([this, on] { applyXtremeLookaheadLock(on); });
}

void MaybeDuckAudioProcessorEditor::applyLimiterRatioLock(bool limiterOn)
{
    auto* ratioParam = audioProcessor.apvts.getParameter("ratio");
    if (ratioParam == nullptr)
        return;

    if (limiterOn)
    {
        // Hard Duck replaces the ratio curve with brick-wall (∞:1) ducking and the
        // DSP ignores the ratio value entirely. So we DON'T touch the param (the
        // user's ratio is preserved for when they disengage) — we just freeze the
        // knob and relabel its read-out ∞:1, instead of the misleading old "1:1".
        ratioSlider.textFromValueFunction = [](double)
        {
            return juce::String(juce::CharPointer_UTF8("\xE2\x88\x9E:1")); // ∞:1
        };
    }
    else
    {
        // Restore the parameter's own "N:1" formatter (what the attachment installs).
        ratioSlider.textFromValueFunction = [ratioParam](double value)
        {
            return ratioParam->getText(ratioParam->convertTo0to1(static_cast<float>(value)), 0);
        };
    }

    ratioSlider.setEnabled(! limiterOn);
    ratioSlider.updateText();
}

void MaybeDuckAudioProcessorEditor::applyXtremeLookaheadLock(bool xtremeOn)
{
    // Xtreme swaps the knob between the normal and wide lookahead PARAMS, so the
    // knob stays a live selector in both — its range, value, value-box text and
    // tick count all follow whichever param is attached.
    lookaheadAttachment.reset();   // detach before re-attaching to the other param
    lookaheadAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.apvts,
        xtremeOn ? "lookaheadXtreme" : "lookahead",
        lookaheadSlider);

    lookaheadSlider.getProperties().set("stepTicks",
        xtremeOn ? MaybeDuckAudioProcessor::kNumXtremeLookaheadSteps
                 : MaybeDuckAudioProcessor::kNumLookaheadSteps);
    lookaheadSlider.repaint();
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

void MaybeDuckAudioProcessorEditor::styleValueBox (juce::Slider& s)
{
    // Per-slider because JUCE bakes the value-box colours at setTextBoxStyle time
    // (before the slider is parented to our LookAndFeel), so the LAF default alone
    // won't take. Re-pull from the active palette so a theme flip updates them.
    const auto& p = mdLookAndFeel.palette();
    s.setColour(juce::Slider::textBoxBackgroundColourId, p.knobMatteBlack);
    s.setColour(juce::Slider::textBoxOutlineColourId,    p.engravedText);
    s.setColour(juce::Slider::textBoxTextColourId,       p.engravedText);
    s.setColour(juce::Slider::textBoxHighlightColourId,  p.accentAmber.withAlpha(0.35f));
}

void MaybeDuckAudioProcessorEditor::applyTheme()
{
    // Push the persisted dark/light state into the LookAndFeel (also re-applies its
    // colour IDs + regenerates the brushed grain), then re-style every slider's
    // value box from the new palette and repaint.
    mdLookAndFeel.setDarkMode(ThemeState::get().isDark());

    for (auto* c : getChildren())
        if (auto* s = dynamic_cast<juce::Slider*>(c))
            styleValueBox(*s);

    lastThemeGen = ThemeState::get().generation();
    backgroundDirty = true;   // palette changed -> rebuild the cached panel art on next paint
    repaint();
}

void MaybeDuckAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    // Triple-click the wordmark to flip dark/light (process-wide, persisted).
    if (e.getNumberOfClicks() >= 3 && wordmarkBounds.contains(e.getPosition()))
    {
        ThemeState::get().toggle();
        applyTheme();  // this editor flips instantly; siblings catch up on the next timer tick
        return;
    }

    juce::AudioProcessorEditor::mouseDown(e);
}

void MaybeDuckAudioProcessorEditor::cycleBandState(int band)
{
    // Advance the band's 3-state choice: Normal(0) -> Solo(1) -> Mute(2) -> Normal.
    if (auto* p = audioProcessor.apvts.getParameter("bandState" + juce::String(band)))
    {
        const int cur  = static_cast<int>(*audioProcessor.apvts.getRawParameterValue("bandState" + juce::String(band)));
        const int next = (cur + 1) % 3;
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(next)));
        p->endChangeGesture();
    }
}

//==============================================================================
// paint() draws the fixed BACKGROUND decoration only — the brushed-metal panel,
// the four corner screws, and the engraved wordmark. The interactive widgets
// (knobs, buttons, meters) paint themselves on top via the LookAndFeel; you do
// NOT draw them here. Add static artwork (logos, panel labels, lines) in this
// method. The actual textures live in MaybeDuckLookAndFeel.cpp (drawBrushedChassis,
// drawScrew, drawEngravedText), so tweak their look there; tweak placement here.
void MaybeDuckAudioProcessorEditor::paint (juce::Graphics& g)
{
    // The static background is cached and painting is just a blit (so the cheap,
    // frequently-repainting child widgets don't force the expensive chassis/grain
    // render behind them every frame). The cache is (re)built lazily HERE, the first
    // time it's needed after resize/theme change — paint() always runs with a valid
    // layout, so the section/rack bounds are guaranteed set (unlike the constructor,
    // where a fixed-size plugin may not have had resized() called yet).
    if (backgroundDirty || ! backgroundCache.isValid()
        || backgroundCache.getWidth() != getWidth() || backgroundCache.getHeight() != getHeight())
    {
        renderBackground();
        backgroundDirty = false;
    }
    if (backgroundCache.isValid())
        g.drawImageAt(backgroundCache, 0, 0);

    // Engraved TEXT is drawn live (not baked into the cached image) so it stays
    // crisp on hi-DPI — a logical-res cache scaled up blurs text. These are cheap
    // (a few short strings) and get clipped to the dirty region on partial repaints.
    const auto area = getLocalBounds().toFloat();

    // Wordmark across the TOP, carved like the crossover filter-curve engravings
    // (a dark groove with a lit lower-right lip), not the subtle group-title style.
    {
        const auto wm = topWordmarkBounds.toFloat();
        g.setFont(juce::Font(mdLookAndFeel.getMediumTypeface()).withHeight(18.0f).withExtraKerningFactor(0.14f));
        g.setColour(mdLookAndFeel.palette().dividerHighlight.withAlpha(0.55f));  // lit lower-right lip
        g.drawText("MAYBEDUCK", wm.translated(0.8f, 1.0f), juce::Justification::centred, false);
        g.setColour(mdLookAndFeel.palette().dividerShadow.withAlpha(0.95f));     // dark carved groove
        g.drawText("MAYBEDUCK", wm, juce::Justification::centred, false);
    }
    wordmarkBounds = topWordmarkBounds.expanded(2.0f);   // triple-click theme-flip hot-spot

    juce::Rectangle<float> versionArea (area.getRight() - 150.0f, area.getBottom() - 17.0f, 110.0f, 12.0f);
    mdLookAndFeel.drawEngravedText(g, "v1.2", versionArea, juce::Justification::centredRight,
                                   juce::Font(mdLookAndFeel.getRegularTypeface()).withHeight(9.5f));

    juce::Font hdr(mdLookAndFeel.getMediumTypeface());
    hdr.setHeight(11.0f);
    hdr.setExtraKerningFactor(0.12f);
    if (! pultecLowHdr.isEmpty())
        mdLookAndFeel.drawEngravedText(g, "LOW FREQUENCY", pultecLowHdr.toFloat(),
                                       juce::Justification::centred, hdr);
    if (! pultecHighHdr.isEmpty())
        mdLookAndFeel.drawEngravedText(g, "HIGH FREQUENCY", pultecHighHdr.toFloat(),
                                       juce::Justification::centred, hdr);
}

void MaybeDuckAudioProcessorEditor::renderBackground()
{
    const int w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0)
        return;

    backgroundCache = juce::Image(juce::Image::ARGB, w, h, true);
    juce::Graphics g(backgroundCache);

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

    // Dynamics section reads as a rack-unit module dropped into the chassis. The
    // module spans the FULL plugin width (mounting rails at the case sides), with
    // the controls sitting in its middle — so it takes the whole horizontal body
    // rather than being inset. Drawn behind the section's controls (paint on top).
    auto fullWidthRack = [this, &g](juce::Rectangle<int> groupBounds, float toneShift = 0.0f)
    {
        if (groupBounds.isEmpty()) return;
        auto rb = groupBounds.toFloat();
        rb.setX(0.0f);
        rb.setWidth((float) getWidth());   // full edge-to-edge, rails at the case sides
        // Pass the chassis tone at this rack's vertical centre so the panel is
        // darkened RELATIVE to its surroundings -> consistently recessed whether the
        // rack sits high (light chassis) or low (dark chassis). toneShift tweaks a
        // specific rack's face: +ve brighter (Pultec EQ), -ve darker (Saturation).
        const float yNorm = rb.getCentreY() / (float) juce::jmax(1, getHeight());
        auto tone = mdLookAndFeel.chassisToneAt(yNorm);
        tone = (toneShift >= 0.0f) ? tone.brighter(toneShift) : tone.darker(-toneShift);
        mdLookAndFeel.drawRackInset(g, rb, tone);
    };
    fullWidthRack(rackBoundsDyn);

    // Pultec EQ is its own rack-unit module too — drawn a bit brighter than the
    // dynamics rack. The "LOW/HIGH FREQUENCY" sub-headers and the wordmark are TEXT,
    // so they're drawn live in paint() (caching text in the logical-res background
    // image blurs it on hi-DPI) — not here.
    fullWidthRack(rackBoundsEq, mdLookAndFeel.isDarkMode() ? 0.18f : 0.06f);

    // Tube Saturation rack — its own module, only a hair darker than the others.
    fullWidthRack(rackBoundsSat, -0.08f);

    // --- Crossover filter-band engravings ----------------------------------
    // Row 1 (the knob row): BIG engraved filter-response curves in the gaps
    // between the knob circles — LP | BP | BP | BP | HP, one per band, showing
    // through the transparent areas of the slider components.
    // Row 2 (the strip below): boundary ticks under each knob + the band names.
    if (! xoverBandStrip.isEmpty())
    {
        const auto strip = xoverBandStrip.toFloat();
        const float top = strip.getY(), bot = strip.getBottom();
        const float innerL = strip.getX(), innerR = strip.getRight();
        const float edges[6] = { innerL,
                                 (float) xoverBandDivX[0], (float) xoverBandDivX[1],
                                 (float) xoverBandDivX[2], (float) xoverBandDivX[3],
                                 innerR };
        static const char* const names[5] = { "LOW", "LO-MID", "MID", "HI-MID", "HIGH" };
        static const int          ftype[5] = { 0, 1, 1, 1, 2 };  // LP, BP, BP, BP, HP

        // Engraved filter-response glyph (0=LP, 1=BP, 2=HP) filling rect r.
        auto drawFilterGlyph = [&g, this](juce::Rectangle<float> r, int type)
        {
            auto X = [&](float fx) { return r.getX() + fx * r.getWidth();  };
            auto Y = [&](float fy) { return r.getY() + fy * r.getHeight(); };
            juce::Path p;
            if (type == 0) {            // low pass: flat lows, roll off highs
                p.startNewSubPath(X(0.0f),  Y(0.16f));
                p.lineTo         (X(0.46f), Y(0.16f));
                p.quadraticTo    (X(0.72f), Y(0.26f), X(1.0f), Y(0.96f));
            } else if (type == 2) {     // high pass: roll up lows, flat highs
                p.startNewSubPath(X(0.0f),  Y(0.96f));
                p.quadraticTo    (X(0.28f), Y(0.26f), X(0.54f), Y(0.16f));
                p.lineTo         (X(1.0f),  Y(0.16f));
            } else {                    // band pass: hump
                p.startNewSubPath(X(0.0f),  Y(0.96f));
                p.quadraticTo    (X(0.27f), Y(0.52f), X(0.5f), Y(0.10f));
                p.quadraticTo    (X(0.73f), Y(0.52f), X(1.0f), Y(0.96f));
            }
            // Recessed/engraved look (carved INTO the panel): a dark groove line is
            // the main stroke, with a thin lit edge just below-right (the lower lip of
            // the groove catching light) — so it reads as inset, not popping out.
            g.setColour(mdLookAndFeel.palette().dividerShadow.withAlpha(0.9f));
            g.strokePath(p, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(mdLookAndFeel.palette().dividerHighlight.withAlpha(0.4f));
            g.strokePath(p, juce::PathStrokeType(0.9f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                         juce::AffineTransform::translation(0.6f, 0.8f));
        };

        // Row 1: big curves centred in each knob-row gap, at the knob circles' height.
        if (! xoverKnobRow.isEmpty())
        {
            const auto kr = xoverKnobRow.toFloat();
            const float cellW   = kr.getWidth() / 5.0f;   // knobs evenly distributed: 4 knobs, 5 cells
            const float rotaryH = kr.getHeight() - (float) kKnobLabelH - 20.0f;  // minus caption + value box
            const float knobR   = juce::jmin(cellW - 8.0f, rotaryH) * 0.5f;      // matches drawRotarySlider
            const float cy      = kr.getY() + (float) kKnobLabelH + rotaryH * 0.5f;
            const float kc[4]   = { (float) xoverBandDivX[0], (float) xoverBandDivX[1],
                                    (float) xoverBandDivX[2], (float) xoverBandDivX[3] };

            // Curves centred in each gap: LP left of knob 1, BP between knobs, HP
            // right of knob 4 — symmetric, MID dead-centre. One uniform size, fit
            // to the smallest gap so LP/HP read the same as BP.
            const float gapL[5] = { innerL, kc[0] + knobR, kc[1] + knobR, kc[2] + knobR, kc[3] + knobR };
            const float gapR[5] = { kc[0] - knobR, kc[1] - knobR, kc[2] - knobR, kc[3] - knobR, innerR };
            float minGap = gapR[0] - gapL[0];
            for (int b = 1; b < 5; ++b) minGap = juce::jmin(minGap, gapR[b] - gapL[b]);
            const float curveW = juce::jlimit(28.0f, 56.0f, minGap - 6.0f);
            const float curveH = curveW * 0.42f;
            for (int b = 0; b < 5; ++b)
            {
                const float cx = (gapL[b] + gapR[b]) * 0.5f;
                drawFilterGlyph(juce::Rectangle<float>(curveW, curveH).withCentre({ cx, cy }), ftype[b]);
            }
        }

        // Row 2: boundary ticks under each crossover knob (dark groove + highlight).
        for (int i = 0; i < 4; ++i)
        {
            const float bx = (float) xoverBandDivX[i];
            g.setColour(mdLookAndFeel.palette().dividerShadow.withAlpha(0.8f));
            g.drawLine(bx, top, bx, bot, 1.0f);
            g.setColour(mdLookAndFeel.palette().dividerHighlight.withAlpha(0.35f));
            g.drawLine(bx + 0.6f, top, bx + 0.6f, bot, 0.6f);
        }

        // Row 2: band names are now interactive solo/mute switches (bandSwitches[],
        // positioned in resized(), painted on top) — no engraved text drawn here.
        juce::ignoreUnused(names, edges);
    }
}

// resized() is called by JUCE every time the window opens or is resized. It is
// where every component gets its position + size. We never use fixed pixel
// coordinates; instead we start from the full window and slice pieces off a
// shrinking `area` rectangle:
//   removeFromTop(h)  -> chops an h-tall strip off the top and RETURNS it,
//                        leaving `area` shorter for the next section.
//   removeFromLeft(w) -> same, but a w-wide column off the left edge.
//   reduced(n)        -> insets a rectangle by n px on all sides (padding).
// So sections are laid out top-to-bottom in the ORDER they appear below — to
// reorder sections on screen, reorder these blocks. To make a section taller,
// bump its local *GroupH / *RowH constant. Each section: claim its rectangle,
// inset it, drop the title row, then lay knobs left-to-right inside what's left.
void MaybeDuckAudioProcessorEditor::resized()
{
    auto winBounds = getLocalBounds();
    juce::Rectangle<int> area(winBounds.getX() + kOuterMargin, winBounds.getY() + 3,
                              winBounds.getWidth() - 2 * kOuterMargin,
                              winBounds.getHeight() - 3 - kOuterMargin);
    // Engraved "MaybeDuck" wordmark, centred (H+V) in a band across the top.
    topWordmarkBounds = area.removeFromTop(24);
    wordmarkHotspot.setBounds(topWordmarkBounds);   // tooltip + triple-click theme toggle
    area.removeFromTop(2);

    // --- Sidechain Mode group: ONE row --------------------------------------
    // Vertical layout is pad-driven: [title][modeTopPad][button row][modeBotPad],
    // so the buttons sit with equal breathing room above and below the title bar.
    // Mode-select buttons hug the left; the smaller FFT-conv toggle + size buttons
    // hug the right (shown only in Spectral mode — see timerCallback) and read as
    // secondary. The latency readout sits at the editor's bottom-left, not here.
    constexpr int modeTopPad     = 4;   // gap under the group title (buttons were cramped to it)
    constexpr int modeBotPad     = 4;   // gap below the buttons (matches the top)
    constexpr int modeBtnH       = 36;  // main mode-button height
    constexpr int convBtnH       = 30;  // conv controls height -> shorter so they read as secondary
    constexpr int modeBtnW       = 112; // mode-select button width
    constexpr int convBtnW       = 120; // "FFT Conv" toggle button width
    constexpr int convSizeW      = 84;  // buffer-size button width (wider so 1024/2048 aren't cramped)
    constexpr int convRightInset = 6;   // keep the conv cluster off the group's right edge
    constexpr int modeGroupH     = kGroupTitleH + modeTopPad + modeBtnH + modeBotPad;
    auto modeArea = area.removeFromTop(modeGroupH);
    modeGroup.setBounds(modeArea);

    auto modeInner = modeArea.reduced(8, 0);   // horizontal inset only; vertical handled by the pads
    modeInner.removeFromTop(kGroupTitleH);     // clear the engraved title row
    modeInner.removeFromTop(modeTopPad);       // <- modeTopPad (gap under the title)
    auto modeRow = modeInner.removeFromTop(modeBtnH); // <- modeBtnH (main row height)

    // Main mode buttons hug the left.
    auto mainRow = modeRow;
    for (int i = 0; i < 3; ++i)
        modeButtons[i].setBounds(mainRow.removeFromLeft(modeBtnW).reduced(2, 0)); // <- modeBtnW (width)

    // Conv controls hug the right: shorter (convBtnH, vertically centred against
    // the main row) + a right inset, so the cluster reads as secondary. Peeled
    // right-to-left so the on-screen order is [FFT Conv][512][1024][2048].
    auto convCluster = modeRow.withSizeKeepingCentre(modeRow.getWidth(), convBtnH); // <- convBtnH
    convCluster.removeFromRight(convRightInset);                                    // <- convRightInset
    for (int i = 2; i >= 0; --i)
        convSizeButtons[i].setBounds(convCluster.removeFromRight(convSizeW).reduced(2, 0)); // <- convSizeW
    fftConvButton.setBounds(convCluster.removeFromRight(convBtnW).reduced(2, 0));            // <- convBtnW

    area.removeFromTop(kGroupGap);

    // --- Dynamics group: 8-knob row + toggles row, GR meter on the right ---
    constexpr int dynamicsKnobsH  = 100;  // knob-row height (knobs are width-limited by the 8-col split, so this just sets the row's vertical air)
    constexpr int dynamicsToggleH = 50;   // toggle-row slot  -> vertical position of the toggle buttons
    constexpr int dynamicsGroupH  = kGroupTitleH + dynamicsKnobsH + dynamicsToggleH;

    auto dynamicsArea = area.removeFromTop(dynamicsGroupH);
    dynamicsGroup.setBounds(dynamicsArea);
    rackBoundsDyn = dynamicsArea;

    auto dynamicsInner = dynamicsArea.reduced(8, 6);
    dynamicsInner.removeFromTop(kGroupTitleH - 8);   // leave space for group title

    // Three-strip meter (IN / SC / GR) lives in the wider slot on the right.
    auto meterArea = dynamicsInner.removeFromRight(60);   // <- 230 = meter width (height spans both rows)
    levelMeter.setBounds(meterArea);

    auto dynKnobs = dynamicsInner.removeFromTop(dynamicsKnobsH); // <- dynamicsKnobsH (knob-row height)
    const int colW = dynKnobs.getWidth() / 8;                   // 8 columns -> width derived, not fixed
    auto layoutKnob = [&](juce::Slider& s, juce::Label& l)
    {
        auto col = dynKnobs.removeFromLeft(colW).reduced(4);
        l.setBounds(col.removeFromTop(kKnobLabelH));  // <- kKnobLabelH (caption height)
        s.setBounds(col);                             // knob fills the rest of the column
    };
    layoutKnob(thresholdSlider, thresholdLabel);
    layoutKnob(ratioSlider,     ratioLabel);
    layoutKnob(attackSlider,    attackLabel);
    layoutKnob(holdSlider,      holdLabel);
    layoutKnob(releaseSlider,   releaseLabel);
    layoutKnob(lookaheadSlider, lookaheadLabel);
    layoutKnob(kneeSlider,      kneeLabel);
    layoutKnob(outputSlider,    outputLabel);

    auto toggles = dynamicsInner.removeFromTop(dynamicsToggleH);              // <- dynamicsToggleH (row slot)
    constexpr int toggleBtnH = 34;                                           // <- toggle button height (smaller caps)
    toggles.removeFromTop(juce::jmax(0, dynamicsToggleH - toggleBtnH));      // spare slot height pushes buttons DOWN (needs slot >= toggleBtnH)
    toggles = toggles.removeFromTop(toggleBtnH);                             // lock the row to the button height
    toggles.removeFromRight(18);                                            // gap so the last toggle doesn't crowd the meter
    {
        juce::FlexBox fb;
        fb.flexDirection  = juce::FlexBox::Direction::row;
        fb.justifyContent = juce::FlexBox::JustifyContent::flexStart;
        fb.alignItems     = juce::FlexBox::AlignItems::stretch;
        const juce::FlexItem::Margin m(0.0f, 2.0f, 0.0f, 2.0f);
        fb.items.add(juce::FlexItem(sidechainButton)      .withFlex(1.0f).withMargin(m));
        fb.items.add(juce::FlexItem(softKneeButton)       .withFlex(1.0f).withMargin(m));
        fb.items.add(juce::FlexItem(limiterButton)        .withFlex(1.0f).withMargin(m));
        fb.items.add(juce::FlexItem(xtremeLookaheadButton).withFlex(1.0f).withMargin(m));
        fb.performLayout(toggles.toFloat());
    }

    area.removeFromTop(kGroupGap);

    // --- Crossover group: 4 knobs + engraved 5-band ruler -------------------
    constexpr int xoverRowH    = 96;  // knob-row height (trimmed from 108 to make room for the band ruler)
    constexpr int xoverBandH   = 28;  // band-switch row under the knobs (solo/mute switches)
    constexpr int xoverBandGap = 6;   // gap pushing the band switches down, away from the freq knobs
    constexpr int xoverGroupH  = kGroupTitleH + xoverRowH + xoverBandGap + xoverBandH + 4;
    auto xoverArea = area.removeFromTop(xoverGroupH);
    crossoverGroup.setBounds(xoverArea);

    auto xoverInner = xoverArea.reduced(8, 6);
    xoverInner.removeFromTop(kGroupTitleH - 8);
    auto xoverKnobs = xoverInner.removeFromTop(xoverRowH);
    xoverKnobRow = xoverKnobs;   // store the knob row; big filter curves are drawn in its gaps (paint())
    // 4 knobs spread symmetrically: the outer two sit `margin` from the edges, the
    // rest evenly between, so the whole row (knobs + the filter curves between them)
    // is left-right symmetric with the MID band-pass curve dead centre. Smaller
    // margin = wider spread (the 1/5 even distribution would be margin = 0.2 W).
    const int xoverCellW = xoverKnobs.getWidth() / 4;  // 4 equal cells spanning the full inner width
    auto xoverTrack = xoverKnobs;
    for (int i = 0; i < 4; ++i)
    {
        auto cellRect = xoverTrack.removeFromLeft(xoverCellW);
        xoverBandDivX[i] = cellRect.getCentreX();
        auto col = cellRect.reduced(4);
        xoverLabels[i].setBounds(col.removeFromTop(kKnobLabelH));
        xoverSliders[i].setBounds(col);
    }
    xoverInner.removeFromTop(xoverBandGap);                 // breathing room below the knobs
    xoverBandStrip = xoverInner.removeFromTop(xoverBandH);  // band-switch row, full inner width

    // Band solo/mute switches fill the ruler segments (one per band, between the
    // crossover-boundary knob centres), matching the boundary ticks drawn in paint().
    {
        const int edges[6] = { xoverBandStrip.getX(), xoverBandDivX[0], xoverBandDivX[1],
                               xoverBandDivX[2], xoverBandDivX[3], xoverBandStrip.getRight() };
        for (int i = 0; i < 5; ++i)
        {
            juce::Rectangle<int> seg(edges[i], xoverBandStrip.getY(),
                                     edges[i + 1] - edges[i], xoverBandStrip.getHeight());
            bandSwitches[i].setBounds(seg.reduced(2, 1));
        }
    }

    area.removeFromTop(kGroupGap);

    // --- Per-band duck scale group: hidden in this UI pass (E3). -----------
    // We don't claim any vertical space — the EQ section moves up to fill the gap.
    // Children remain invisible so the DSP keeps reading their (neutral) values.

    // --- Pultec EQP-1A rack: authentic faceplate split into a LOW FREQUENCY group
    // (LF Sel · Boost · Atten) and a HIGH FREQUENCY group (HF Sel · Boost · Bandwidth
    // · Atten Sel · Atten), with the Bypass/Sat toggles stacked at the right.
    constexpr int pultecHdrH    = 14;  // engraved "LOW/HIGH FREQUENCY" sub-header row
    constexpr int pultecKnobH   = 96;  // per-knob block height (caption + knob)
    constexpr int pultecStagger = 60;  // selectors (bottom row) drop well below boost/atten (top row)
    constexpr int pultecGroupH  = kGroupTitleH + pultecHdrH + pultecKnobH + pultecStagger + 14;
    auto pultecArea = area.removeFromTop(pultecGroupH);
    pultecGroup.setBounds(pultecArea);
    rackBoundsEq = pultecArea;

    auto pultecInner = pultecArea.reduced(8, 6);
    pultecInner.removeFromTop(kGroupTitleH - 8);

    auto pultecHdrRow   = pultecInner.removeFromTop(pultecHdrH);
    auto pultecKnobArea = pultecInner;

    // Spread the EQ knobs to span the SAME horizontal extent as the dynamics knob
    // row above, so the leftmost/rightmost EQ knobs line up with the leftmost/
    // rightmost dynamics knobs. Dynamics lays 8 columns across the inner width minus
    // its meter slot (kMeterW); its knob centres run colW*0.5 .. colW*7.5. Place the
    // 5 EQ top knobs evenly across that same centre span, at the same knob size.
    constexpr int kMeterW = 60;   // must match the dynamics meter slot width
    const float eqColW = (pultecKnobArea.getWidth() - kMeterW) / 8.0f;
    const int   x0   = pultecKnobArea.getX();
    const int   yTop = pultecKnobArea.getY();
    const int   yBot = yTop + pultecStagger;
    // m in [0..4] across the 5 top knobs -> centre x aligned to the dynamics span.
    auto eqCx = [&](float m) { return juce::roundToInt(x0 + eqColW * (0.5f + 1.75f * m)); };
    const int kw = juce::roundToInt(eqColW);

    auto placeKnob = [&](int idx, float m, int y)
    {
        juce::Rectangle<int> col(eqCx(m) - kw / 2, y, kw, pultecKnobH);
        col = col.reduced(4);
        pultecLabels[idx].setBounds(col.removeFromTop(kKnobLabelH));
        pultecSliders[idx].setBounds(col);
    };
    // pultecIds order: 0 LF Sel, 1 LF Boost, 2 LF Atten, 3 HF Sel, 4 HF Boost,
    //                  5 Bandwidth, 6 Atten Sel, 7 HF Atten
    placeKnob(1, 0.0f, yTop);  // LF Boost   (top)
    placeKnob(2, 1.0f, yTop);  // LF Atten   (top)
    placeKnob(4, 2.0f, yTop);  // HF Boost   (top)
    placeKnob(7, 3.0f, yTop);  // HF Atten   (top)
    placeKnob(6, 4.0f, yTop);  // Atten Sel  (top)
    placeKnob(0, 0.5f, yBot);  // LF Sel     (bottom, under LF pair)
    placeKnob(5, 1.5f, yBot);  // Bandwidth  (bottom, centre)
    placeKnob(3, 2.5f, yBot);  // HF Sel     (bottom, under HF pair)

    // EQ Bypass: vintage bat toggle switch, centre-RIGHT — in the slot above the
    // dynamics meter column (right of the rightmost knob), vertically centred over
    // the knob rows.
    {
        const int togW = 54, togH = 78;
        const int slotCx = pultecKnobArea.getRight() - kMeterW / 2;
        pultecBypassButton.setBounds(slotCx - togW / 2,
                                     pultecKnobArea.getCentreY() - togH / 2, togW, togH);
    }

    {
        const int loL = eqCx(0) - kw / 2, loR = eqCx(1) + kw / 2;
        const int hiL = eqCx(2) - kw / 2, hiR = eqCx(4) + kw / 2;
        pultecLowHdr  = pultecHdrRow.withX(loL).withWidth(loR - loL);
        pultecHighHdr = pultecHdrRow.withX(hiL).withWidth(hiR - hiL);
    }

    // --- Tube Saturation: its OWN rack-unit group, with a DARKER faceplate, below
    // the EQ. Drive / Mix / Trim knobs (spread) + the Sat enable button at the right.
    area.removeFromTop(kGroupGap);
    constexpr int satRowH   = 118;  // taller row -> bigger saturation knobs
    constexpr int satGroupH = kGroupTitleH + satRowH + 6;
    auto satArea = area.removeFromTop(satGroupH);
    pultecSatGroup.setBounds(satArea);
    rackBoundsSat = satArea;

    auto satInner = satArea.reduced(8, 6);
    satInner.removeFromTop(kGroupTitleH - 8);
    {
        const float su = satInner.getWidth() / 5.0f;
        auto placeSat = [&](int idx, float centreU)
        {
            juce::Rectangle<int> col(juce::roundToInt(satInner.getX() + centreU * su - su * 0.5f),
                                     satInner.getY(), juce::roundToInt(su), satInner.getHeight());
            col = col.reduced(4);
            pultecSatLabels[idx].setBounds(col.removeFromTop(kKnobLabelH));
            pultecSatSliders[idx].setBounds(col);
        };
        placeSat(0, 0.75f);  // Drive
        placeSat(1, 2.0f);   // Mix
        placeSat(2, 3.25f);  // Trim

        // Sat enable button at the right of its own group.
        const int btnH = 34;
        const int btnW = juce::roundToInt(su * 0.7f);
        pultecSatButton.setBounds(juce::roundToInt(satInner.getX() + 4.4f * su) - btnW / 2,
                                  satInner.getCentreY() - btnH / 2, btnW, btnH);
    }

    // --- Latency readout: bottom-left corner of the editor, engraved into the
    // chassis (mirrors the wordmark at the bottom-right). Offset right of the
    // corner screw so the two don't overlap.
    auto full = getLocalBounds();
    convLatencyLabel.setBounds(full.getX() + 28, full.getBottom() - 20, 340, 16); // <- bottom-left, vertically centred on the corner screw

    // Layout changed — mark the cached panel art for a lazy rebuild on the next paint
    // (where the bounds are guaranteed valid).
    backgroundDirty = true;
}

void MaybeDuckAudioProcessorEditor::timerCallback()
{
    // Detect a theme flip from another editor instance (or a fresh load) and apply.
    if (ThemeState::get().generation() != lastThemeGen)
        applyTheme();

    levelMeter.setLevels(audioProcessor.getInputLevelDb(),
                         audioProcessor.getSidechainLevelDb(),
                         audioProcessor.getGainReductionAmountDb());

    // Tube-sat button: a level-reactive tube glow. Idle = dim green; as the
    // post-EQ/saturation output gets hotter the lit colour brightens and shifts
    // green -> amber -> red. (The glow only shows while Sat is engaged.)
    {
        // Smooth the per-block output peak with an asymmetric one-pole — quick to
        // brighten, gentle to fade (tube-like), so the colour doesn't flicker at the
        // 30 Hz tick rate. Coefficients are per-tick (~33 ms): attack ~3 ticks,
        // release ~12 ticks.
        const float lvlRaw = audioProcessor.getOutputLevelDb();      // post-sat output peak (dBFS)
        const float coeff  = (lvlRaw > satGlowLevelDb) ? 0.35f : 0.08f;
        satGlowLevelDb += (lvlRaw - satGlowLevelDb) * coeff;
        const float lvl = satGlowLevelDb;
        const float t   = juce::jlimit(0.0f, 1.0f, juce::jmap(lvl, -40.0f, -1.0f, 0.0f, 1.0f));
        const float hue = juce::jmap(t, 0.0f, 1.0f, 0.33f, 0.0f);   // 0.33 green -> 0.0 red
        const float bri = 0.40f + 0.60f * t;                       // dim -> bright
        const juce::int64 argb = (juce::int64) (juce::uint32)
            juce::Colour::fromHSV(hue, 0.85f, bri, 1.0f).getARGB();
        if ((juce::int64) pultecSatButton.getProperties()["litColour"] != argb)
        {
            pultecSatButton.getProperties().set("litColour", argb);
            pultecSatButton.repaint();
        }
    }

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

    // Conv config controls stay visible but grey out (disabled) when they don't
    // apply: FFT Conv only in Spectral mode; the size buttons only when FFT Conv
    // is also on. The greyed look matches the disabled knobs.
    fftConvButton.setEnabled(spectralActive);
    for (int i = 0; i < 3; ++i)
    {
        convSizeButtons[i].setEnabled(spectralActive && fftConvOn);
        convSizeButtons[i].setToggleState(i == sizeIdx, juce::dontSendNotification);
    }
    // Latency label stays visible — relevant for lookahead even when FFT is off.

    // The crossover bands are used by the processor's band branch: Multiband (mode 1)
    // or Spectral with FFT-Conv OFF (mode 2 && !fftConv). Spectral FFT-Conv is per-bin
    // and uses NO crossover bands, so the crossover, band-scale AND band-routing
    // controls are all inert there (as in Wideband) — grey them out to match.
    const bool bandEnabled = (mode == 1) || (mode == 2 && !fftConvOn);
    for (int i = 0; i < 4; ++i) xoverSliders[i].setEnabled(bandEnabled);
    for (int i = 0; i < 5; ++i) bandScaleSliders[i].setEnabled(bandEnabled);

    // Band solo/mute switches: backlight colour follows the state (silver = normal,
    // blue = solo, red = mute); console-style band output only exists in Multiband.
    // Only touch (and repaint) a switch when its state or enablement actually changed
    // — these used to repaint every tick, forcing the panel to redraw behind them.
    for (int i = 0; i < 5; ++i)
    {
        const int st = juce::jlimit(0, 2,
            static_cast<int>(*audioProcessor.apvts.getRawParameterValue("bandState" + juce::String(i))));
        if (st == prevBandState[i] && bandEnabled == prevBandEnabled)
            continue;

        const juce::uint32 lit = (st == 1) ? MaybeDuckLookAndFeel::kBandSolo
                               : (st == 2) ? MaybeDuckLookAndFeel::kBandMute
                                           : 0xffCBD0D4 /* cool silver = normal/active */;
        bandSwitches[i].getProperties().set("litColour", static_cast<juce::int64>(lit));
        bandSwitches[i].setEnabled(bandEnabled);   // Multiband + Spectral (FFT-Conv off)
        bandSwitches[i].repaint();
        prevBandState[i] = st;
    }
    prevBandEnabled = bandEnabled;

    // Sat button glows green, brightening with the audio passing through the
    // saturation stage (dim green idle -> bright green when driven hard).
    {
        const bool satOn = *audioProcessor.apvts.getRawParameterValue("pultecSatEnable") > 0.5f;
        const float outDb = audioProcessor.getOutputLevelDb();
        const float t  = juce::jlimit(0.0f, 1.0f, (outDb + 40.0f) / 40.0f);  // -40 dB..0 dB -> 0..1
        const float bri = juce::jmap(t, 0.55f, 1.0f);    // dim -> bright with level
        const juce::uint32 col = satOn ? juce::Colour::fromHSV(0.33f, 0.80f, bri, 1.0f).getARGB()
                                       : 0xff3FB36B;       // idle green base
        if (col != prevSatColour)
        {
            pultecSatButton.getProperties().set("litColour", static_cast<juce::int64>(col));
            pultecSatButton.repaint();
            prevSatColour = col;
        }
    }

    const int latSamples = audioProcessor.getConvLatencySamples();
    if (latSamples != prevLatSamples)   // the text rarely changes; skip the per-tick String build/repaint
    {
        prevLatSamples = latSamples;
        const double sr = juce::jmax(1.0, audioProcessor.getCurrentSampleRateHz());
        const double latMs = 1000.0 * latSamples / sr;
        convLatencyLabel.setText("Latency: " + juce::String(latSamples) + " smp ("
                                  + juce::String(latMs, 1) + " ms)",
                                  juce::dontSendNotification);
    }
}
