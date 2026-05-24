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

private:
    MaybeDuckAudioProcessor& audioProcessor;

    // E6: cache the user's chosen ratio so it can be restored when Hard Duck releases.
    float previousRatioValue = 4.0f;
    void applyLimiterRatioLock(bool limiterOn);

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
    juce::ToggleButton sidechainButton, softKneeButton, limiterButton;
    LevelMeterComponent levelMeter;
    std::unique_ptr<SliderAttachment> thresholdAttachment, ratioAttachment, attackAttachment,
                                      holdAttachment, releaseAttachment, lookaheadAttachment,
                                      kneeAttachment, outputAttachment;
    std::unique_ptr<ButtonAttachment> sidechainAttachment, softKneeAttachment, limiterAttachment;

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

    // --- Per-band duck scale group
    juce::GroupComponent bandScaleGroup;
    juce::Slider bandScaleSliders[5];
    juce::Label  bandScaleLabels[5];
    std::unique_ptr<SliderAttachment> bandScaleAttachments[5];

    // --- Post-ducking EQ group
    juce::GroupComponent eqGroup;
    juce::Slider eqSliders[7];      // low freq, low gain, mid freq, mid gain, mid Q, high freq, high gain
    juce::Label  eqLabels[7];
    std::unique_ptr<SliderAttachment> eqAttachments[7];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MaybeDuckAudioProcessorEditor)
};
