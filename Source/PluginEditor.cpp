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
    lookaheadSlider.setTextValueSuffix(" ms");
    lookaheadSlider.setNumDecimalPlacesToDisplay(0);

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
    addAndMakeVisible(sidechainButton);
    addAndMakeVisible(softKneeButton);
    addAndMakeVisible(limiterButton);

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

    sidechainButton .setTooltip("Use the external sidechain bus as the trigger. When off, ducking is driven by the main input.");
    softKneeButton  .setTooltip("Smooth the curve around the threshold instead of a hard corner.");
    limiterButton   .setTooltip("Hard Duck: brick-wall ducking (" + juce::String(juce::CharPointer_UTF8("\xE2\x88\x9E")) + ":1). Every dB the sidechain exceeds the threshold removes 1 dB from the main signal. Overrides the Ratio knob, which reads " + juce::String(juce::CharPointer_UTF8("\xE2\x88\x9E")) + ":1 while engaged.");

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
        styleValueBox(eqSliders[i]);
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

    // Initial sync of the Hard Duck state — freeze + relabel the ratio knob if it
    // loads with Hard Duck already engaged. The ratio param is never re-written
    // (the DSP ignores it under Hard Duck), so the user's stored ratio is kept.
    applyLimiterRatioLock(*audioProcessor.apvts.getRawParameterValue("limiter") > 0.5f);

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
    sidechainButton.setBounds(toggles.removeFromLeft(170));                   // <- Sidechain button width
    softKneeButton.setBounds (toggles.removeFromLeft(120));                   // <- Soft Knee button width
    limiterButton.setBounds  (toggles.removeFromLeft(120));                   // <- Hard Duck button width

    area.removeFromTop(kGroupGap);

    // --- Crossover group: 4 knobs ------------------------------------------
    constexpr int xoverRowH   = 108;  // knob-row height -> also the knob size
    constexpr int xoverGroupH = kGroupTitleH + xoverRowH + 14;
    auto xoverArea = area.removeFromTop(xoverGroupH);
    crossoverGroup.setBounds(xoverArea);

    auto xoverInner = xoverArea.reduced(8, 6);
    xoverInner.removeFromTop(kGroupTitleH - 8);
    auto xoverKnobs = xoverInner.removeFromTop(xoverRowH);
    const int xoverColW = xoverKnobs.getWidth() / 4;   // 4 columns -> width derived
    for (int i = 0; i < 4; ++i)
    {
        auto col = xoverKnobs.removeFromLeft(xoverColW).reduced(4);
        xoverLabels[i].setBounds(col.removeFromTop(kKnobLabelH));  // <- kKnobLabelH (caption height)
        xoverSliders[i].setBounds(col);
    }

    area.removeFromTop(kGroupGap);

    // --- Per-band duck scale group: hidden in this UI pass (E3). -----------
    // We don't claim any vertical space — the EQ section moves up to fill the gap.
    // Children remain invisible so the DSP keeps reading their (neutral) values.

    // --- Post-ducking EQ group: 7 knobs, MAAG-style zig-zag --------------------
    constexpr int eqRowH    = 96;   // per-knob block height (caption + knob); also the knob size
    constexpr int eqStagger = 34;   // zig-zag offset: alternate knobs drop by this
    constexpr int eqGroupH  = kGroupTitleH + eqRowH + eqStagger + 14;
    auto eqArea = area.removeFromTop(eqGroupH);
    eqGroup.setBounds(eqArea);

    auto eqInner = eqArea.reduced(8, 6);
    eqInner.removeFromTop(kGroupTitleH - 8);
    auto eqKnobs = eqInner.removeFromTop(eqRowH + eqStagger);   // band including the zig-zag room
    const int eqColW = eqKnobs.getWidth() / 7;   // 7 columns -> width derived
    for (int i = 0; i < 7; ++i)
    {
        auto col = eqKnobs.removeFromLeft(eqColW).reduced(4);
        // Zig-zag: odd columns drop by eqStagger, even columns ride high.
        col.removeFromTop((i % 2 == 1) ? eqStagger : 0);
        col = col.removeFromTop(eqRowH);
        eqLabels[i].setBounds(col.removeFromTop(kKnobLabelH));  // <- kKnobLabelH (caption height)
        eqSliders[i].setBounds(col);
    }

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
