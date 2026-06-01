/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    // >>> PLUGIN WINDOW SIZE <<< — change these two to resize the whole UI. The
    // aspect ratio is locked to kWindowW:kWindowH in the constructor. The sections
    // below are fixed pixels, so there's a content floor (~660 tall at the current
    // section heights). To go shorter, trim dynamicsToggleH / xoverRowH / eqRowH.
    constexpr int kWindowW       = 820;
    constexpr int kWindowH       = 665;   // was 760
    constexpr int kOuterMargin   = 22;
    constexpr int kGroupGap      = 6;
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
    auto styleValueBox = [](juce::Slider& s)
    {
        s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(MaybeDuckLookAndFeel::kKnobMatteBlack));
        s.setColour(juce::Slider::textBoxOutlineColourId,    juce::Colour(MaybeDuckLookAndFeel::kEngravedText));
        s.setColour(juce::Slider::textBoxTextColourId,       juce::Colour(MaybeDuckLookAndFeel::kEngravedText));
        s.setColour(juce::Slider::textBoxHighlightColourId,  juce::Colour(MaybeDuckLookAndFeel::kAccentAmber).withAlpha(0.35f));
    };

    // A rotary knob: applies the 270° rotary config + a value read-out box below it.
    auto setupSlider = [this, &styleValueBox](juce::Slider& s, const juce::String& name)
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

    sidechainButton .setTooltip("Use the external sidechain bus as the trigger. When off, ducking is disabled and the main passes through unaffected.");
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
    setupGroup(pultecGroup, "Post-Ducking PulEQ");

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

    pultecSatButton.setButtonText("Sat");
    pultecSatButton.setTooltip("12AX7 triode saturation on the Pultec makeup-amp stage (4× oversampled).");
    addAndMakeVisible(pultecSatButton);
    pultecSatAttachment = std::make_unique<ButtonAttachment>(
        audioProcessor.apvts, "pultecSatEnable", pultecSatButton);

    pultecBypassButton.setButtonText("Bypass");
    pultecBypassButton.setTooltip("Bypass the Pultec EQ entirely (pure passthrough — no oversampling, no WDF).");
    addAndMakeVisible(pultecBypassButton);
    pultecBypassAttachment = std::make_unique<ButtonAttachment>(
        audioProcessor.apvts, "pultecBypass", pultecBypassButton);

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

    setResizable(true, true);
    setResizeLimits(600, 500, 1400, 1100);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio(static_cast<double>(kWindowW) / static_cast<double>(kWindowH));
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

//==============================================================================
// paint() draws the fixed BACKGROUND decoration only — the brushed-metal panel,
// the four corner screws, and the engraved wordmark. The interactive widgets
// (knobs, buttons, meters) paint themselves on top via the LookAndFeel; you do
// NOT draw them here. Add static artwork (logos, panel labels, lines) in this
// method. The actual textures live in MaybeDuckLookAndFeel.cpp (drawBrushedChassis,
// drawScrew, drawEngravedText), so tweak their look there; tweak placement here.
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

    juce::Rectangle<float> wordmarkArea(area.getRight() - 150.0f,
                                        area.getBottom() - 33.0f, 110.0f, 16.0f);
    juce::Rectangle<float> versionArea (area.getRight() - 150.0f,
                                        area.getBottom() - 17.0f, 110.0f, 12.0f);

    mdLookAndFeel.drawEngravedText(g, "MAYBEDUCK", wordmarkArea,
                                 juce::Justification::centredRight,
                                 wordmarkFont.withHeight(14.0f));
    mdLookAndFeel.drawEngravedText(g, "v1.0",       versionArea,
                                 juce::Justification::centredRight,
                                 versionFont.withHeight(9.5f));

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
        auto drawFilterGlyph = [&g](juce::Rectangle<float> r, int type)
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
            g.setColour(juce::Colour(MaybeDuckLookAndFeel::kDividerShadow).withAlpha(0.9f));
            g.strokePath(p, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(juce::Colour(MaybeDuckLookAndFeel::kDividerHighlight).withAlpha(0.4f));
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
            g.setColour(juce::Colour(MaybeDuckLookAndFeel::kDividerShadow).withAlpha(0.8f));
            g.drawLine(bx, top, bx, bot, 1.0f);
            g.setColour(juce::Colour(MaybeDuckLookAndFeel::kDividerHighlight).withAlpha(0.35f));
            g.drawLine(bx + 0.6f, top, bx + 0.6f, bot, 0.6f);
        }

        // Row 2: band names, centred in each segment.
        juce::Font bandFont(mdLookAndFeel.getRegularTypeface());
        bandFont.setHeight(9.5f);
        for (int i = 0; i < 5; ++i)
        {
            juce::Rectangle<float> seg(edges[i], top, edges[i + 1] - edges[i], bot - top);
            mdLookAndFeel.drawEngravedText(g, names[i], seg.reduced(3.0f, 1.0f),
                                           juce::Justification::centred, bandFont);
        }
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
    auto area = getLocalBounds().reduced(kOuterMargin);

    // --- Sidechain Mode group: ONE row --------------------------------------
    // Vertical layout is pad-driven: [title][modeTopPad][button row][modeBotPad],
    // so the buttons sit with equal breathing room above and below the title bar.
    // Mode-select buttons hug the left; the smaller FFT-conv toggle + size buttons
    // hug the right (shown only in Spectral mode — see timerCallback) and read as
    // secondary. The latency readout sits at the editor's bottom-left, not here.
    constexpr int modeTopPad     = 12;  // gap under the group title (buttons were cramped to it)
    constexpr int modeBotPad     = 12;  // gap below the buttons (matches the top)
    constexpr int modeBtnH       = 40;  // main mode-button height
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
    constexpr int dynamicsToggleH = 55;   // toggle-row slot  -> vertical position of the toggle buttons
    constexpr int dynamicsGroupH  = kGroupTitleH + dynamicsKnobsH + dynamicsToggleH;

    auto dynamicsArea = area.removeFromTop(dynamicsGroupH);
    dynamicsGroup.setBounds(dynamicsArea);

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
    constexpr int toggleBtnH = 40;                                           // <- toggle button height (size)
    toggles.removeFromTop(juce::jmax(0, dynamicsToggleH - toggleBtnH));      // spare slot height pushes buttons DOWN (needs slot >= toggleBtnH)
    toggles = toggles.removeFromTop(toggleBtnH);                             // lock the row to the button height
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
    constexpr int xoverRowH   = 96;   // knob-row height (trimmed from 108 to make room for the band ruler)
    constexpr int xoverBandH  = 18;   // engraved band ruler under the knobs (drawn in paint())
    constexpr int xoverGroupH = kGroupTitleH + xoverRowH + xoverBandH + 8;
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
    xoverBandStrip = xoverInner.removeFromTop(xoverBandH);  // ruler area, full inner width

    area.removeFromTop(kGroupGap);

    // --- Per-band duck scale group: hidden in this UI pass (E3). -----------
    // We don't claim any vertical space — the EQ section moves up to fill the gap.
    // Children remain invisible so the DSP keeps reading their (neutral) values.

    // --- Pultec EQP-1A group: 8 knobs in a straight row + Sat toggle ----------
    // Layout L→R: LF Freq · LF Boost · LF Atten · HF Freq · HF Boost · HF BW · HF Atten Hz · HF Atten · [Sat]
    constexpr int pultecRowH   = 96;
    constexpr int pultecGroupH = kGroupTitleH + pultecRowH + 14;
    auto pultecArea = area.removeFromTop(pultecGroupH);
    pultecGroup.setBounds(pultecArea);

    auto pultecInner = pultecArea.reduced(8, 6);
    pultecInner.removeFromTop(kGroupTitleH - 8);
    auto pultecRow = pultecInner.removeFromTop(pultecRowH);

    // Reserve a 72 px slot at the right for the two toggle buttons (Bypass + Sat),
    // stacked vertically. Remaining width is split across the 8 knobs.
    constexpr int togglesSlotW = 72;
    auto togglesCol = pultecRow.removeFromRight(togglesSlotW).reduced(4);
    const int pultecColW = pultecRow.getWidth() / 8;
    for (int i = 0; i < 8; ++i)
    {
        auto col = pultecRow.removeFromLeft(pultecColW).reduced(4);
        pultecLabels[i].setBounds(col.removeFromTop(kKnobLabelH));
        pultecSliders[i].setBounds(col);
    }
    // Stack: Bypass on top, Sat below. 24 px tall each with 6 px gap, centred vertically.
    const int btnH = 24;
    const int gap  = 6;
    auto btnArea = togglesCol.withSizeKeepingCentre(togglesCol.getWidth(), btnH * 2 + gap);
    pultecBypassButton.setBounds(btnArea.removeFromTop(btnH));
    btnArea.removeFromTop(gap);
    pultecSatButton   .setBounds(btnArea.removeFromTop(btnH));

    // --- Latency readout: bottom-left corner of the editor, engraved into the
    // chassis (mirrors the wordmark at the bottom-right). Offset right of the
    // corner screw so the two don't overlap.
    auto full = getLocalBounds();
    convLatencyLabel.setBounds(full.getX() + 28, full.getBottom() - 20, 340, 16); // <- bottom-left, vertically centred on the corner screw
}

void MaybeDuckAudioProcessorEditor::timerCallback()
{
    levelMeter.setLevels(audioProcessor.getInputLevelDb(),
                         audioProcessor.getSidechainLevelDb(),
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
