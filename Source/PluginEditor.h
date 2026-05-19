/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
*/
class CompressorMeterComponent : public juce::Component
{
public:
    void setLevels(float newInputDb, float newGrDb)
    {
        inputDb = newInputDb;
        grDb = newGrDb;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        g.setColour(juce::Colour(0xff161616));
        g.fillRoundedRectangle(r, 6.0f);

        auto meter = r.reduced(10.0f, 8.0f);

        g.setColour(juce::Colour(0xff262626));
        g.fillRoundedRectangle(meter, 4.0f);

        // input meter: map -60..0 dB bottom->top
        const float inputNorm = juce::jlimit(0.0f, 1.0f, juce::jmap(inputDb, -60.0f, 0.0f, 0.0f, 1.0f));
        auto inputFill = meter;
        inputFill.removeFromTop(meter.getHeight() * (1.0f - inputNorm));

        g.setColour(juce::Colour(0xff34a853)); // green
        g.fillRoundedRectangle(inputFill, 4.0f);

        // GR meter: 0..24 dB from top downward
        const float grNorm = juce::jlimit(0.0f, 1.0f, grDb / 24.0f);
        auto grFill = meter.withHeight(meter.getHeight() * grNorm);

        g.setColour(juce::Colour(0xfffbbc04)); // Ableton-like amber
        g.fillRoundedRectangle(grFill, 4.0f);

        g.setColour(juce::Colours::white.withAlpha(0.10f));
        for (float db : { -48.0f, -36.0f, -24.0f, -12.0f, -6.0f, 0.0f })
        {
            const float y = juce::jmap(db, 0.0f, -60.0f, meter.getY(), meter.getBottom());
            g.drawHorizontalLine((int) y, meter.getX(), meter.getRight());
        }

        g.setColour(juce::Colours::white.withAlpha(0.75f));
        g.setFont(12.0f);
        g.drawFittedText("IN", getLocalBounds().removeFromBottom(18), juce::Justification::centredLeft, 1);
        g.drawFittedText("GR", getLocalBounds().removeFromBottom(18), juce::Justification::centredRight, 1);
    }

private:
    float inputDb = -100.0f;
    float grDb = 0.0f;
};

class MaybeDuckAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                       private juce::Timer
{
public:
    MaybeDuckAudioProcessorEditor (MaybeDuckAudioProcessor&);
    ~MaybeDuckAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

    void timerCallback() override;

private:
    MaybeDuckAudioProcessor& audioProcessor;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    // --- Dynamics group (8 timing/gain knobs, 3 toggles, GR meter)
    juce::GroupComponent dynamicsGroup;
    juce::Slider thresholdSlider, ratioSlider, attackSlider, holdSlider, releaseSlider,
                 lookaheadSlider, kneeSlider, outputSlider;
    juce::Label thresholdLabel, ratioLabel, attackLabel, holdLabel, releaseLabel,
                lookaheadLabel, kneeLabel, outputLabel;
    juce::ToggleButton sidechainButton, softKneeButton, limiterButton;
    CompressorMeterComponent compressorMeter;
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
