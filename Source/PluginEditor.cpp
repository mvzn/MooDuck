/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

SpectrumComponent::SpectrumComponent(MaybeDuckAudioProcessor& p)
    : processor(p)
{
    const int numBins = MaybeDuckAudioProcessor::fftSize / 2;
    inputMagnitudes.resize(numBins, -100.0f);
    sidechainMagnitudes.resize(numBins, -100.0f);
    outputMagnitudes.resize(numBins, -100.0f);

    inputPeaks.resize(numBins, -100.0f);
    sidechainPeaks.resize(numBins, -100.0f);
    outputPeaks.resize(numBins, -100.0f);

    inputPeakHoldCounters.resize(numBins, 0);
    sidechainPeakHoldCounters.resize(numBins, 0);
    outputPeakHoldCounters.resize(numBins, 0);
}

void SpectrumComponent::update()
{
    bool changed = false;

    const bool sidechainEnabled = (*processor.apvts.getRawParameterValue("sidechainEnable") > 0.5f);

    const bool showSidechain = sidechainEnabled && processor.isSidechainConnected();

    if (processor.getInputFftData(scratchBuffer.data()))
    {
        processSpectrumFrame(scratchBuffer.data(), inputMagnitudes, inputPeaks, inputPeakHoldCounters);
        changed = true;
    }

    if (showSidechain && processor.getSidechainFftData(scratchBuffer.data()))
    {
        processSpectrumFrame(scratchBuffer.data(), sidechainMagnitudes, sidechainPeaks, sidechainPeakHoldCounters);
        changed = true;
    }
    else if (!showSidechain)
    {
        // Sidechain disabled — decay trace to silence
        for (size_t i = 0; i < sidechainMagnitudes.size(); ++i)
        {
            sidechainMagnitudes[i] = juce::jmax(-90.0f, sidechainMagnitudes[i] - 2.5f);
            sidechainPeaks[i]      = juce::jmax(-90.0f, sidechainPeaks[i] - 1.5f);
            sidechainPeakHoldCounters[i] = 0;
        }
        changed = true;
    }
    // showSidechain && no new frame ready → hold last frame, no repaint

    if (processor.getOutputFftData(scratchBuffer.data()))
    {
        processSpectrumFrame(scratchBuffer.data(), outputMagnitudes, outputPeaks, outputPeakHoldCounters);
        changed = true;
    }

    if (changed)
        repaint();
}

void SpectrumComponent::processSpectrumFrame(const float* fftData,
                                             std::vector<float>& magnitudes,
                                             std::vector<float>& peaks,
                                             std::vector<int>& peakHoldCounters)
{
    std::fill(fftBuffer.begin(), fftBuffer.end(), 0.0f);
    std::copy(fftData, fftData + MaybeDuckAudioProcessor::fftSize, fftBuffer.begin());

    window.multiplyWithWindowingTable(fftBuffer.data(), MaybeDuckAudioProcessor::fftSize);
    fft.performFrequencyOnlyForwardTransform(fftBuffer.data());

    const int numBins = MaybeDuckAudioProcessor::fftSize / 2;
    constexpr float minDb = -90.0f;
    constexpr float maxDb = 0.0f;
    constexpr int peakHoldFrames = 5;      // short hold
    constexpr float peakDecayPerFrame = 0.18f; // slower decay

    for (int i = 1; i < numBins; ++i)
    {
        const float mag = fftBuffer[(size_t) i] / (float) MaybeDuckAudioProcessor::fftSize;
        const float db = juce::jlimit(minDb, maxDb, juce::Decibels::gainToDecibels(mag, minDb));

        magnitudes[(size_t) i] = db;

        if (db >= peaks[(size_t) i])
        {
            peaks[(size_t) i] = db;
            peakHoldCounters[(size_t) i] = peakHoldFrames;
        }
        else
        {
            if (peakHoldCounters[(size_t) i] > 0)
            {
                --peakHoldCounters[(size_t) i];
            }
            else
            {
                peaks[(size_t) i] = juce::jmax(minDb, peaks[(size_t) i] - peakDecayPerFrame);
            }
        }
    }
}

float SpectrumComponent::binToX(int bin, float width) const
{
    const auto sampleRate = juce::jmax(1.0, processor.getCurrentSampleRateHz());
    const float freq = (float) bin * (float) sampleRate / (float) MaybeDuckAudioProcessor::fftSize;

    constexpr float minFreq = 20.0f;
    constexpr float maxFreq = 20000.0f;

    const float clampedFreq = juce::jlimit(minFreq, maxFreq, freq);
    const float norm = std::log(clampedFreq / minFreq) / std::log(maxFreq / minFreq);
    return norm * width;
}

float SpectrumComponent::magnitudeToY(float db, float height) const
{
    return juce::jmap(db, 0.0f, -90.0f, 0.0f, height);
}

juce::Path SpectrumComponent::buildSpectrumPath(const std::vector<float>& values, juce::Rectangle<float> area) const
{
    juce::Path p;
    bool started = false;

    const int numBins = (int) values.size();
    for (int i = 1; i < numBins; ++i)
    {
        const float x = area.getX() + binToX(i, area.getWidth());
        const float y = area.getY() + magnitudeToY(values[(size_t) i], area.getHeight());

        if (!started)
        {
            p.startNewSubPath(x, y);
            started = true;
        }
        else
        {
            p.lineTo(x, y);
        }
    }

    return p;
}

juce::Path SpectrumComponent::buildFillPath(const juce::Path& linePath, juce::Rectangle<float> area) const
{
    juce::Path fill(linePath);

    if (!linePath.isEmpty())
    {
        fill.lineTo(area.getRight(), area.getBottom());
        fill.lineTo(area.getX(), area.getBottom());
        fill.closeSubPath();
    }

    return fill;
}

void SpectrumComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour(juce::Colour(0xff121212));
    g.fillRoundedRectangle(bounds, 8.0f);

    auto area = bounds.reduced(10.0f);

    // grid
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    for (float hz : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
    {
        const float norm = std::log(hz / 20.0f) / std::log(20000.0f / 20.0f);
        const float x = area.getX() + norm * area.getWidth();
        g.drawVerticalLine((int) x, area.getY(), area.getBottom());
    }

    for (float db : { -72.0f, -54.0f, -36.0f, -18.0f })
    {
        const float y = area.getY() + magnitudeToY(db, area.getHeight());
        g.drawHorizontalLine((int) y, area.getX(), area.getRight());
    }

    auto drawTrace = [&](const std::vector<float>& mags,
                         const std::vector<float>& peaks,
                         juce::Colour fillColour,
                         juce::Colour peakColour)
    {
        auto magPath = buildSpectrumPath(mags, area);
        auto fillPath = buildFillPath(magPath, area);
        auto peakPath = buildSpectrumPath(peaks, area);

        g.setColour(fillColour.withAlpha(0.27f));
        g.fillPath(fillPath);

        g.setColour(peakColour.withAlpha(0.8f));
        g.strokePath(peakPath, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };

    drawTrace(inputMagnitudes,     inputPeaks,     juce::Colour(0xff3cba54), juce::Colour(0xff7ee28f));
    drawTrace(sidechainMagnitudes, sidechainPeaks, juce::Colour(0xff4285f4), juce::Colour(0xff8ab4ff));
    drawTrace(outputMagnitudes,    outputPeaks,    juce::Colour(0xfffbbc04), juce::Colour(0xffffd86a));

    // legend
    auto legend = area.removeFromTop(16.0f);
    g.setFont(12.0f);

    g.setColour(juce::Colour(0xff7ee28f));
    g.drawText("Input", legend.removeFromLeft(60), juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xff8ab4ff));
    g.drawText("Sidechain", legend.removeFromLeft(80), juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xffffd86a));
    g.drawText("Output", legend.removeFromLeft(60), juce::Justification::centredLeft);
}

//==============================================================================
MaybeDuckAudioProcessorEditor::MaybeDuckAudioProcessorEditor (MaybeDuckAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), spectrumComponent(p)
{
    auto setupSlider = [this](juce::Slider& s, const juce::String& name)
    {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
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

    setupSlider(thresholdSlider, "Threshold");
    setupSlider(ratioSlider,     "Ratio");
    setupSlider(attackSlider,    "Attack");
    setupSlider(holdSlider,      "Hold");
    setupSlider(releaseSlider,   "Release");
    setupSlider(kneeSlider,      "Knee");
    setupSlider(outputSlider,    "Output");

    holdSlider.setTextValueSuffix(" ms");

    setupLabel(thresholdLabel, "Threshold");
    setupLabel(ratioLabel,     "Ratio");
    setupLabel(attackLabel,    "Attack");
    setupLabel(holdLabel,      "Hold");
    setupLabel(releaseLabel,   "Release");
    setupLabel(kneeLabel,      "Knee");
    setupLabel(outputLabel,    "Output");

    setupLabel(aboveThresholdLabel, "Above Thr: -- dB");
    setupLabel(grLabel,             "GR: -- dB");

    setupLabel(cpuLabel,       "CPU: --");
    setupLabel(blockLabel,     "Block: --");
    setupLabel(sampleRateLabel,"SR: --");

    sidechainButton.setButtonText("Enable Sidechain");
    softKneeButton.setButtonText("Soft Knee");
    limiterButton.setButtonText("Limiter");

    addAndMakeVisible(sidechainButton);
    addAndMakeVisible(softKneeButton);
    addAndMakeVisible(limiterButton);

    addAndMakeVisible(compressorMeter);
    addAndMakeVisible(spectrumComponent);

    thresholdAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "threshold", thresholdSlider);
    ratioAttachment     = std::make_unique<SliderAttachment>(audioProcessor.apvts, "ratio", ratioSlider);
    attackAttachment    = std::make_unique<SliderAttachment>(audioProcessor.apvts, "attack", attackSlider);
    holdAttachment      = std::make_unique<SliderAttachment>(audioProcessor.apvts, "hold", holdSlider);
    releaseAttachment   = std::make_unique<SliderAttachment>(audioProcessor.apvts, "release", releaseSlider);
    kneeAttachment      = std::make_unique<SliderAttachment>(audioProcessor.apvts, "knee", kneeSlider);
    outputAttachment    = std::make_unique<SliderAttachment>(audioProcessor.apvts, "output", outputSlider);

    sidechainAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "sidechainEnable", sidechainButton);
    softKneeAttachment  = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "softKnee", softKneeButton);
    limiterAttachment   = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "limiter", limiterButton);

    // --- Crossover section
    crossoverSectionLabel.setText("Multiband Crossover", juce::dontSendNotification);
    crossoverSectionLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(13.0f).withStyle("Bold")));
    crossoverSectionLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    crossoverSectionLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(crossoverSectionLabel);

    const juce::String xoverNames[] = { "Low", "Low-Mid", "High-Mid", "High" };
    const juce::String xoverIds[]   = { "xover0", "xover1", "xover2", "xover3" };

    for (int i = 0; i < 4; ++i)
    {
        xoverSliders[i].setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        xoverSliders[i].setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
        xoverSliders[i].setTextValueSuffix(" Hz");
        xoverSliders[i].setNumDecimalPlacesToDisplay(0);
        addAndMakeVisible(xoverSliders[i]);

        xoverLabels[i].setText(xoverNames[i], juce::dontSendNotification);
        xoverLabels[i].setJustificationType(juce::Justification::centred);
        xoverLabels[i].setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(xoverLabels[i]);

        xoverAttachments[i] = std::make_unique<SliderAttachment>(
            audioProcessor.apvts, xoverIds[i], xoverSliders[i]);
    }

    // --- Sidechain mode section
    modeSectionLabel.setText("Sidechain Mode", juce::dontSendNotification);
    modeSectionLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(13.0f).withStyle("Bold")));
    modeSectionLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    modeSectionLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(modeSectionLabel);

    const juce::String modeNames[3] = { "Wideband", "Multiband", "Spectral" };
    for (int i = 0; i < 3; ++i)
    {
        modeButtons[i].setButtonText(modeNames[i]);
        modeButtons[i].setClickingTogglesState(false);
        modeButtons[i].setRadioGroupId(0x101); // visual grouping only; logic in click handler
        addAndMakeVisible(modeButtons[i]);
        modeButtons[i].onClick = [this, i] { setModeFromButton(i); };
    }

    fftConvButton.setButtonText("FFT Conv");
    addAndMakeVisible(fftConvButton);
    fftConvAttachment = std::make_unique<ButtonAttachment>(
        audioProcessor.apvts, "fftConvolution", fftConvButton);

    const juce::String convSizeNames[3] = { "512", "1024", "2048" };
    for (int i = 0; i < 3; ++i)
    {
        convSizeButtons[i].setButtonText(convSizeNames[i]);
        convSizeButtons[i].setClickingTogglesState(false);
        convSizeButtons[i].setRadioGroupId(0x102);
        addAndMakeVisible(convSizeButtons[i]);
        convSizeButtons[i].onClick = [this, i] { setConvSizeFromButton(i); };
    }

    convLatencyLabel.setText("Latency: 0 samples (0.0 ms)", juce::dontSendNotification);
    convLatencyLabel.setJustificationType(juce::Justification::centredLeft);
    convLatencyLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(convLatencyLabel);

    startTimerHz(30);

    setSize (820, 740);
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

MaybeDuckAudioProcessorEditor::~MaybeDuckAudioProcessorEditor()
{
}

//==============================================================================
void MaybeDuckAudioProcessorEditor::paint (juce::Graphics& g)
{
    // (Our component is opaqu, so we must completely fill the background with a solid colour)
    g.fillAll (juce::Colours::black);
    g.setColour (juce::Colours::white);
    g.setFont (18.0f);
    g.drawFittedText ("Sidechain Compressor", getLocalBounds().removeFromTop(30),
                      juce::Justification::centred, 1);
}

void MaybeDuckAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(10);
    auto titleArea = area.removeFromTop(30);
    juce::ignoreUnused(titleArea);

    auto topArea = area.removeFromTop(210);
    auto meterArea = topArea.removeFromRight(80).reduced(8);
    auto spectrumArea = topArea.reduced(4);

    spectrumComponent.setBounds(spectrumArea);
    compressorMeter.setBounds(meterArea);

    auto sliderArea = area.removeFromTop(160);
    auto columnWidth = sliderArea.getWidth() / 7;

    auto layoutSliderWithLabel = [&](juce::Slider& slider, juce::Label& label)
    {
        auto col = sliderArea.removeFromLeft(columnWidth).reduced(4);
        auto labelArea = col.removeFromTop(20);
        label.setBounds(labelArea);
        slider.setBounds(col);
    };

    layoutSliderWithLabel(thresholdSlider, thresholdLabel);
    layoutSliderWithLabel(ratioSlider,     ratioLabel);
    layoutSliderWithLabel(attackSlider,    attackLabel);
    layoutSliderWithLabel(holdSlider,      holdLabel);
    layoutSliderWithLabel(releaseSlider,   releaseLabel);
    layoutSliderWithLabel(kneeSlider,      kneeLabel);
    layoutSliderWithLabel(outputSlider,    outputLabel);

    // Crossover section
    crossoverSectionLabel.setBounds(area.removeFromTop(22));
    auto xoverArea   = area.removeFromTop(138);
    auto xoverColW   = xoverArea.getWidth() / 4;

    for (int i = 0; i < 4; ++i)
    {
        auto col = xoverArea.removeFromLeft(xoverColW).reduced(4);
        xoverLabels[i].setBounds(col.removeFromTop(20));
        xoverSliders[i].setBounds(col);
    }

    auto buttonArea = area.removeFromTop(36);
    sidechainButton.setBounds(buttonArea.removeFromLeft(170));
    softKneeButton.setBounds (buttonArea.removeFromLeft(120));
    limiterButton.setBounds  (buttonArea.removeFromLeft(100));

    // --- Mode + FFT Conv section
    modeSectionLabel.setBounds(area.removeFromTop(22));

    auto modeRow = area.removeFromTop(32);
    const int modeBtnW = 110;
    for (int i = 0; i < 3; ++i)
        modeButtons[i].setBounds(modeRow.removeFromLeft(modeBtnW).reduced(2));

    auto convRow = area.removeFromTop(32);
    fftConvButton.setBounds(convRow.removeFromLeft(110).reduced(2));
    for (int i = 0; i < 3; ++i)
        convSizeButtons[i].setBounds(convRow.removeFromLeft(60).reduced(2));
    convLatencyLabel.setBounds(convRow.reduced(4));

    auto statsArea = area.removeFromTop(28);
    auto statW = statsArea.getWidth() / 5;
    aboveThresholdLabel.setBounds(statsArea.removeFromLeft(statW));
    grLabel.setBounds(statsArea.removeFromLeft(statW));
    cpuLabel.setBounds(statsArea.removeFromLeft(statW));
    blockLabel.setBounds(statsArea.removeFromLeft(statW));
    sampleRateLabel.setBounds(statsArea.removeFromLeft(statW));
}

void MaybeDuckAudioProcessorEditor::timerCallback()
{
    aboveThresholdLabel.setText("Above Thr: " + juce::String(audioProcessor.getAboveThresholdDb(), 1) + " dB", juce::dontSendNotification);

    grLabel.setText("GR: " + juce::String(audioProcessor.getGainReductionAmountDb(), 1) + " dB", juce::dontSendNotification);
    {
        const float raw = audioProcessor.getCpuUsagePercent();
        // Snap on spikes > 8% above smoothed; otherwise ~300 ms decay at 30 Hz (alpha ≈ 0.89)
        if (raw > smoothedCpuPercent + 8.0f)
            smoothedCpuPercent = raw;
        else
            smoothedCpuPercent = 0.89f * smoothedCpuPercent + 0.11f * raw;
        cpuLabel.setText("CPU: " + juce::String(smoothedCpuPercent, 1) + " %", juce::dontSendNotification);
    }
    blockLabel.setText("Block: " + juce::String(audioProcessor.getCurrentBlockSize()), juce::dontSendNotification);
    sampleRateLabel.setText("SR: " + juce::String(audioProcessor.getCurrentSampleRateHz(), 0) + " Hz", juce::dontSendNotification);

    compressorMeter.setLevels(audioProcessor.getInputLevelDb(), audioProcessor.getGainReductionAmountDb());

    spectrumComponent.update();

    // --- Sync mode + size button visual state from APVTS
    const int mode = juce::jlimit(0, 2,
        static_cast<int>(*audioProcessor.apvts.getRawParameterValue("sidechainMode")));
    const int sizeIdx = juce::jlimit(0, 2,
        static_cast<int>(*audioProcessor.apvts.getRawParameterValue("fftConvSize")));
    const bool fftConvOn = (*audioProcessor.apvts.getRawParameterValue("fftConvolution") > 0.5f);
    const bool spectralActive = (mode == 2);

    for (int i = 0; i < 3; ++i)
    {
        const bool selected = (i == mode);
        modeButtons[i].setColour(juce::TextButton::buttonColourId,
            selected ? juce::Colour(0xff4285f4) : juce::Colour(0xff262626));
        modeButtons[i].setColour(juce::TextButton::textColourOffId,
            selected ? juce::Colours::white : juce::Colours::lightgrey);
    }

    fftConvButton.setEnabled(spectralActive);
    for (int i = 0; i < 3; ++i)
    {
        const bool selected = (i == sizeIdx);
        convSizeButtons[i].setEnabled(spectralActive && fftConvOn);
        convSizeButtons[i].setColour(juce::TextButton::buttonColourId,
            selected ? juce::Colour(0xfffbbc04) : juce::Colour(0xff262626));
        convSizeButtons[i].setColour(juce::TextButton::textColourOffId,
            selected ? juce::Colours::black : juce::Colours::lightgrey);
    }

    // Crossover sliders are not used in Wideband mode
    for (int i = 0; i < 4; ++i)
        xoverSliders[i].setEnabled(mode != 0);

    const int latSamples = audioProcessor.getConvLatencySamples();
    const double sr = juce::jmax(1.0, audioProcessor.getCurrentSampleRateHz());
    const double latMs = 1000.0 * latSamples / sr;
    convLatencyLabel.setText("Latency: " + juce::String(latSamples) + " smp ("
                              + juce::String(latMs, 1) + " ms)",
                              juce::dontSendNotification);
}