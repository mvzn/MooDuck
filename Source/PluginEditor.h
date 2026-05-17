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

class SpectrumComponent : public juce::Component
{
public:
    explicit SpectrumComponent(MaybeDuckAudioProcessor& p);

    void update();

    void paint(juce::Graphics& g) override;
    void resized() override {}

private:
    void processSpectrumFrame(const float* fftData,
                          std::vector<float>& magnitudes,
                          std::vector<float>& peaks,
                          std::vector<int>& peakHoldCounters);
    juce::Path buildSpectrumPath(const std::vector<float>& values, juce::Rectangle<float> area) const;
    juce::Path buildFillPath(const juce::Path& linePath, juce::Rectangle<float> area) const;
    float binToX(int bin, float width) const;
    float magnitudeToY(float db, float height) const;

    MaybeDuckAudioProcessor& processor;

    juce::dsp::FFT fft { MaybeDuckAudioProcessor::fftOrder };
    juce::dsp::WindowingFunction<float> window { MaybeDuckAudioProcessor::fftSize, juce::dsp::WindowingFunction<float>::hann };

    std::array<float, MaybeDuckAudioProcessor::fftSize * 2> fftBuffer {};
    std::array<float, MaybeDuckAudioProcessor::fftSize> scratchBuffer {};

    std::vector<float> inputMagnitudes;
    std::vector<float> sidechainMagnitudes;
    std::vector<float> outputMagnitudes;

    std::vector<float> inputPeaks;
    std::vector<float> sidechainPeaks;
    std::vector<float> outputPeaks;

    std::vector<int> inputPeakHoldCounters;
    std::vector<int> sidechainPeakHoldCounters;
    std::vector<int> outputPeakHoldCounters;
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
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    MaybeDuckAudioProcessor& audioProcessor;

    juce::Slider thresholdSlider, ratioSlider, attackSlider, releaseSlider, kneeSlider, outputSlider;
    juce::Label thresholdLabel, ratioLabel, attackLabel, releaseLabel, kneeLabel, outputLabel;

    juce::ToggleButton sidechainButton, softKneeButton, limiterButton;
    juce::Label cpuLabel, blockLabel, sampleRateLabel, aboveThresholdLabel, grLabel;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    CompressorMeterComponent compressorMeter;
    SpectrumComponent spectrumComponent;

    std::unique_ptr<SliderAttachment> thresholdAttachment, ratioAttachment, attackAttachment,
                                      releaseAttachment, kneeAttachment, outputAttachment;

    std::unique_ptr<ButtonAttachment> sidechainAttachment, softKneeAttachment, limiterAttachment;

    float smoothedCpuPercent = 0.0f;

    // Crossover section
    juce::Label crossoverSectionLabel;
    juce::Slider xoverSliders[4];
    juce::Label  xoverLabels[4];
    std::unique_ptr<SliderAttachment> xoverAttachments[4];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MaybeDuckAudioProcessorEditor)
};
