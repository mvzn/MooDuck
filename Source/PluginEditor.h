/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MaybeDuckLookAndFeel.h"
#include "LevelMeterComponent.h"

//==============================================================================
class MaybeDuckAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                       private juce::Timer,
                                       private juce::AudioProcessorValueTreeState::Listener
{
public:
    MaybeDuckAudioProcessorEditor (MaybeDuckAudioProcessor&);
    ~MaybeDuckAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

    void timerCallback() override;

    void parameterChanged(const juce::String& paramID, float newValue) override;

    // Triple-click the engraved wordmark to flip the dark/light theme (process-wide).
    void mouseDown (const juce::MouseEvent& e) override;

private:
    MaybeDuckAudioProcessor& audioProcessor;

    // Pull the persisted dark/light state into the LookAndFeel, re-push the per-
    // slider value-box colours, and repaint. Called on load, on local toggle, and
    // when timerCallback() detects another window flipped the theme.
    void applyTheme();
    void styleValueBox (juce::Slider& s);  // pushes palette tones into a slider's value-box colour IDs

    // The static panel art (chassis, screws, rack, wordmark, engravings) is fixed
    // per window size + theme, so it's pre-rendered once into backgroundCache and
    // paint() just blits it — instead of re-running the expensive gradient/grain
    // render behind every transparent child repaint. Rebuilt in resized()/applyTheme().
    void renderBackground();
    juce::Image backgroundCache;
    bool backgroundDirty = true;   // rebuild the cache lazily in paint() (bounds guaranteed valid there)

    juce::Rectangle<int> wordmarkBounds;   // triple-click hot-spot (set in renderBackground())
    int lastThemeGen = -1;                 // last ThemeState generation we applied

    // timerCallback change-detection so we only repaint widgets that actually moved.
    int  prevBandState[5] = { -2, -2, -2, -2, -2 };
    bool prevBandEnabled  = false;
    int  prevLatSamples   = -1;
    juce::uint32 prevSatColour = 0;   // last Sat-button glow colour (audio-reactive)

    // Hard Duck freezes the ratio knob and relabels its read-out ∞:1 (the DSP
    // ignores the ratio value while Hard Duck is engaged).
    void applyLimiterRatioLock(bool limiterOn);

    // Xtreme Lookahead freezes the lookahead knob and relabels it 5000 ms (the DSP
    // overrides the lookahead value while engaged).
    void applyXtremeLookaheadLock(bool xtremeOn);

    MaybeDuckLookAndFeel mdLookAndFeel;
    juce::TooltipWindow  tooltipWindow { this, 700 };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    // --- Dynamics group (8 timing/gain knobs, 3 toggles, GR meter)
    juce::GroupComponent dynamicsGroup;
    juce::Slider thresholdSlider, ratioSlider, attackSlider, holdSlider, releaseSlider,
                 lookaheadSlider, kneeSlider, outputSlider;
    juce::Label thresholdLabel, ratioLabel, attackLabel, holdLabel, releaseLabel,
                lookaheadLabel, kneeLabel, outputLabel;
    juce::ToggleButton sidechainButton, softKneeButton, limiterButton, xtremeLookaheadButton;
    LevelMeterComponent levelMeter;
    std::unique_ptr<SliderAttachment> thresholdAttachment, ratioAttachment, attackAttachment,
                                      holdAttachment, releaseAttachment, lookaheadAttachment,
                                      kneeAttachment, outputAttachment;
    std::unique_ptr<ButtonAttachment> sidechainAttachment, softKneeAttachment, limiterAttachment,
                                      xtremeLookaheadAttachment;

    // --- Sidechain mode group
    juce::GroupComponent modeGroup;
    juce::TextButton   modeButtons[3];      // Wideband / Multiband / Spectral
    juce::ToggleButton fftConvButton;
    juce::TextButton   convSizeButtons[3];  // 512 / 1024 / 2048
    juce::Label        convLatencyLabel;
    std::unique_ptr<ButtonAttachment> fftConvAttachment;

    void setModeFromButton(int idx);
    void setConvSizeFromButton(int idx);

    // --- Crossover group
    juce::GroupComponent crossoverGroup;
    juce::Slider xoverSliders[4];
    juce::Label  xoverLabels[4];
    std::unique_ptr<SliderAttachment> xoverAttachments[4];
    // Engraved band ruler under the crossover knobs (geometry set in resized(),
    // drawn in paint()). xoverBandDivX = x of each crossover boundary (knob centre).
    juce::Rectangle<int> xoverBandStrip;   // row 2: band-switch row + boundary ticks
    juce::Rectangle<int> xoverKnobRow;     // row 1: knob row (big filter curves drawn in its gaps)
    int                  xoverBandDivX[4] {};

    // Per-band solo/mute switches (soft backlit momentary rectangles) sitting in
    // the ruler segments. Single click cycles Normal->Solo->Mute; backlight colour
    // = state (silver / green / red). Bound to the bandStateN choice params.
    juce::TextButton bandSwitches[5];
    void cycleBandState(int band);

    // --- Per-band duck scale group
    juce::GroupComponent bandScaleGroup;
    juce::Slider bandScaleSliders[5];
    juce::Label  bandScaleLabels[5];
    std::unique_ptr<SliderAttachment> bandScaleAttachments[5];

    // --- Pultec EQP-1A group (replaces the old 3-band post-ducking EQ)
    juce::GroupComponent pultecGroup;
    juce::Slider pultecSliders[8];   // LF Freq, LF Boost, LF Atten, HF Freq, HF Boost, HF BW, HF Atten Freq, HF Atten
    juce::Label  pultecLabels[8];
    std::unique_ptr<SliderAttachment> pultecAttachments[8];
    juce::ToggleButton pultecSatButton;
    std::unique_ptr<ButtonAttachment> pultecSatAttachment;
    juce::ToggleButton pultecBypassButton;
    std::unique_ptr<ButtonAttachment> pultecBypassAttachment;
    // Tube-saturation controls (P6): Drive, Mix, Trim — a third knob row in the group.
    juce::Slider pultecSatSliders[3];
    juce::Label  pultecSatLabels[3];
    std::unique_ptr<SliderAttachment> pultecSatSliderAttachments[3];
    // Engraved "LOW FREQUENCY" / "HIGH FREQUENCY" sub-section headers over the two
    // Pultec knob groups (authentic EQP-1A faceplate split). Set in resized(),
    // drawn into the cached background in renderBackground().
    juce::Rectangle<int> pultecLowHdr, pultecHighHdr;
    // Section rects for the recessed rack-unit modules (set in resized(), drawn into
    // the cached background). Stored explicitly so they don't depend on child
    // GroupComponent bounds being valid at render time.
    juce::Rectangle<int> rackBoundsDyn, rackBoundsEq;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MaybeDuckAudioProcessorEditor)
};
