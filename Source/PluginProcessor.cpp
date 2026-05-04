/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
    static constexpr float kRatioSteps[]  = { 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 8.0f, 16.0f, 20.0f, 40.0f, 60.0f };
    static constexpr int   kNumRatioSteps = 10;
}

//==============================================================================
MaybeDuckAudioProcessor::MaybeDuckAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if !JucePlugin_IsMidiEffect
#if !JucePlugin_IsSynth
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
#endif
                         ),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
#else
    : apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
#endif
{
}

MaybeDuckAudioProcessor::~MaybeDuckAudioProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout MaybeDuckAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "threshold", "Threshold", juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -10.0f));

    juce::NormalisableRange<float> ratioRange(
        1.0f, 60.0f,
        // from0to1 → normalised [0,1] to step value
        [](float, float, float v) -> float {
            int idx = juce::roundToInt(v * static_cast<float>(kNumRatioSteps - 1));
            return kRatioSteps[juce::jlimit(0, kNumRatioSteps - 1, idx)];
        },
        // to0to1 → step value to normalised [0,1]
        [](float, float, float v) -> float {
            int best = 0;
            float bestDist = std::abs(v - kRatioSteps[0]);
            for (int i = 1; i < kNumRatioSteps; ++i) {
                float d = std::abs(v - kRatioSteps[i]);
                if (d < bestDist) { bestDist = d; best = i; }
            }
            return static_cast<float>(best) / static_cast<float>(kNumRatioSteps - 1);
        },
        // snapToLegal → nearest step value
        [](float, float, float v) -> float {
            float best = kRatioSteps[0];
            float bestDist = std::abs(v - kRatioSteps[0]);
            for (int i = 1; i < kNumRatioSteps; ++i) {
                float d = std::abs(v - kRatioSteps[i]);
                if (d < bestDist) { bestDist = d; best = kRatioSteps[i]; }
            }
            return best;
        });

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "ratio", "Ratio", ratioRange, 4.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "attack", "Attack", juce::NormalisableRange<float>(0.0f, 5000.0f, 0.1f, 0.5f), 10.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "release", "Release", juce::NormalisableRange<float>(20.0f, 5000.0f, 1.0f, 0.5f), 100.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "knee", "Knee", juce::NormalisableRange<float>(0.0f, 24.0f, 0.1f), 6.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "output", "Output", juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "sidechainEnable", "Enable Sidechain", false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "softKnee", "Soft Knee", true));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "limiter", "Limiter", false));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "xover0", "Crossover 1",
        juce::NormalisableRange<float>(20.0f, 1000.0f, 1.0f, 0.3f), 120.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "xover1", "Crossover 2",
        juce::NormalisableRange<float>(100.0f, 4000.0f, 1.0f, 0.3f), 400.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "xover2", "Crossover 3",
        juce::NormalisableRange<float>(500.0f, 12000.0f, 1.0f, 0.3f), 2000.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "xover3", "Crossover 4",
        juce::NormalisableRange<float>(2000.0f, 20000.0f, 1.0f, 0.3f), 8000.0f));

    return {params.begin(), params.end()};
}

//==============================================================================
const juce::String MaybeDuckAudioProcessor::getName() const
{
    return "MaybeDuck";
}

bool MaybeDuckAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool MaybeDuckAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool MaybeDuckAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double MaybeDuckAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int MaybeDuckAudioProcessor::getNumPrograms()
{
    return 1;
}

int MaybeDuckAudioProcessor::getCurrentProgram()
{
    return 0;
}

void MaybeDuckAudioProcessor::setCurrentProgram(int index)
{
}

const juce::String MaybeDuckAudioProcessor::getProgramName(int index)
{
    return {};
}

void MaybeDuckAudioProcessor::changeProgramName(int index, const juce::String &newName)
{
}

//==============================================================================
void MaybeDuckAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    for (int b = 0; b < numBands; ++b)
    {
        bandProcessorsL[b].reset(sampleRate);
        bandProcessorsR[b].reset(sampleRate);
    }

    crossover.prepare(sampleRate);

    for (int i = 0; i < StereoMultibandCrossover::numCrossoverPoints; ++i)
    {
        const double fc = static_cast<double>(*apvts.getRawParameterValue("xover" + juce::String(i)));
        crossover.setCrossoverFrequencyImmediate(i, fc);
    }

    mainBandBufL.setSize(numBands, samplesPerBlock);
    mainBandBufR.setSize(numBands, samplesPerBlock);
    scBandBufL.setSize(numBands, samplesPerBlock);
    scBandBufR.setSize(numBands, samplesPerBlock);

    currentSampleRate.store(sampleRate);
    currentBlockSize.store(samplesPerBlock);
}

MaybeDuckAudioProcessor::BandControlValues
MaybeDuckAudioProcessor::applyBandLink(const BandControlValues& gr, float linkAmount)
{
    // Placeholder for future n-band link control.
    // Currently passes through unchanged; replace with neighbor-linking or
    // weighted-average linking when the speech detection modulation is wired up.
    juce::ignoreUnused(linkAmount);
    return gr;
}

void MaybeDuckAudioProcessor::pushNextSampleIntoFifo(float sample,
                                                     float *fifo,
                                                     int &index,
                                                     float *fftBuffer,
                                                     std::atomic<bool> &readyFlag)
{
    if (index == fftSize)
        return;

    fifo[index++] = sample;

    if (index == fftSize)
    {
        const juce::ScopedLock sl(fftDataLock);
        std::copy(fifo, fifo + fftSize, fftBuffer);
        readyFlag.store(true);
        index = 0;
    }
}

bool MaybeDuckAudioProcessor::getInputFftData(float *dest)
{
    if (!inputBlockReady.exchange(false))
        return false;

    const juce::ScopedLock sl(fftDataLock);
    std::copy(inputFftBuffer, inputFftBuffer + fftSize, dest);
    return true;
}

bool MaybeDuckAudioProcessor::getSidechainFftData(float *dest)
{
    if (!sidechainBlockReady.exchange(false))
        return false;

    const juce::ScopedLock sl(fftDataLock);
    std::copy(sidechainFftBuffer, sidechainFftBuffer + fftSize, dest);
    return true;
}

bool MaybeDuckAudioProcessor::getOutputFftData(float *dest)
{
    if (!outputBlockReady.exchange(false))
        return false;

    const juce::ScopedLock sl(fftDataLock);
    std::copy(outputFftBuffer, outputFftBuffer + fftSize, dest);
    return true;
}

void MaybeDuckAudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool MaybeDuckAudioProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
#else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono() && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

#if !JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
#endif

    return true;
#endif
}
#endif

void MaybeDuckAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    const auto startTime      = juce::Time::getHighResolutionTicks();
    const auto ticksPerSecond = juce::Time::getHighResolutionTicksPerSecond();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // --- Read dynamics parameters
    DynamicsProcessorParameters dynParams;
    dynParams.threshold_dB    = *apvts.getRawParameterValue("threshold");
    dynParams.ratio           = *apvts.getRawParameterValue("ratio");
    dynParams.attack_ms       = *apvts.getRawParameterValue("attack");
    dynParams.release_ms      = *apvts.getRawParameterValue("release");
    dynParams.knee_width_dB   = *apvts.getRawParameterValue("knee");
    dynParams.output_gain_dB  = *apvts.getRawParameterValue("output");
    dynParams.enable_sc       = (*apvts.getRawParameterValue("sidechainEnable") > 0.5f);
    dynParams.soft_knee       = (*apvts.getRawParameterValue("softKnee") > 0.5f);
    dynParams.hard_limit_gate = (*apvts.getRawParameterValue("limiter") > 0.5f);
    dynParams.calculation     = dynamicsProcessorType::kCompressor;

    for (int b = 0; b < numBands; ++b)
    {
        bandProcessorsL[b].setParameters(dynParams);
        bandProcessorsR[b].setParameters(dynParams);
    }

    // --- Update crossover frequencies (smoother advances inside processBlock)
    for (int i = 0; i < StereoMultibandCrossover::numCrossoverPoints; ++i)
    {
        const double fc = static_cast<double>(*apvts.getRawParameterValue("xover" + juce::String(i)));
        crossover.setCrossoverFrequency(i, fc);
    }

    // --- Bus pointers
    juce::AudioBuffer<float> mainInput = getBusBuffer(buffer, true, 0);
    juce::AudioBuffer<float> scInput   = getBusBuffer(buffer, true, 1);

    const bool hasSidechainBus = getBusCount(true) > 1;
    const bool scConnected     = hasSidechainBus && scInput.getNumChannels() > 0;
    const bool useSidechain    = dynParams.enable_sc && scConnected;

    const int  numSamples = buffer.getNumSamples();
    const bool isStereo   = mainInput.getNumChannels() > 1;

    const float* mainL = mainInput.getReadPointer(0);
    const float* mainR = isStereo ? mainInput.getReadPointer(1) : mainL;

    // --- Input / sidechain metering (pre-dynamics)
    float blockPeakIn     = 0.0f;
    float sidechainPeakIn = 0.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        const float mono = 0.5f * (mainL[n] + mainR[n]);
        blockPeakIn = std::max(blockPeakIn, std::abs(mono));
        pushNextSampleIntoFifo(mono, inputFifo, inputFifoIndex, inputFftBuffer, inputBlockReady);

        if (useSidechain)
        {
            const float scL    = scInput.getReadPointer(0)[n];
            const float scR    = scInput.getNumChannels() > 1 ? scInput.getReadPointer(1)[n] : scL;
            const float scMono = 0.5f * (scL + scR);
            sidechainPeakIn = std::max(sidechainPeakIn, std::abs(scMono));
            pushNextSampleIntoFifo(scMono, sidechainFifo, sidechainFifoIndex, sidechainFftBuffer, sidechainBlockReady);
        }
    }

    // --- Crossover split into per-band buffers
    float* mbL[numBands], *mbR[numBands], *sbL[numBands], *sbR[numBands];
    for (int b = 0; b < numBands; ++b)
    {
        mbL[b] = mainBandBufL.getWritePointer(b);
        mbR[b] = mainBandBufR.getWritePointer(b);
        sbL[b] = scBandBufL.getWritePointer(b);
        sbR[b] = scBandBufR.getWritePointer(b);
    }

    // When sidechain is unused, pass main signal as SC input (results ignored by processors)
    const float* scLPtr = useSidechain ? scInput.getReadPointer(0) : mainL;
    const float* scRPtr = useSidechain
                          ? (scInput.getNumChannels() > 1 ? scInput.getReadPointer(1) : scLPtr)
                          : mainR;

    crossover.processBlock(mainL, mainR, scLPtr, scRPtr, mbL, mbR, sbL, sbR, numSamples);

    // --- Per-band dynamics processing
    for (int n = 0; n < numSamples; ++n)
    {
        for (int b = 0; b < numBands; ++b)
        {
            if (useSidechain)
            {
                bandProcessorsL[b].processSidechainInputSample(static_cast<double>(sbL[b][n]));
                bandProcessorsR[b].processSidechainInputSample(static_cast<double>(sbR[b][n]));
            }
            mbL[b][n] = static_cast<float>(bandProcessorsL[b].processSample(static_cast<double>(mbL[b][n])));
            mbR[b][n] = static_cast<float>(bandProcessorsR[b].processSample(static_cast<double>(mbR[b][n])));
        }
    }

    // --- Sum bands back to output + output FFT
    auto* outL = mainInput.getWritePointer(0);
    auto* outR = isStereo ? mainInput.getWritePointer(1) : nullptr;

    for (int n = 0; n < numSamples; ++n)
    {
        float sumL = 0.0f, sumR = 0.0f;
        for (int b = 0; b < numBands; ++b)
        {
            sumL += mbL[b][n];
            sumR += mbR[b][n];
        }
        outL[n] = sumL;
        if (outR) outR[n] = sumR;

        const float outMono = outR ? 0.5f * (sumL + sumR) : sumL;
        pushNextSampleIntoFifo(outMono, outputFifo, outputFifoIndex, outputFftBuffer, outputBlockReady);
    }

    // --- Metering
    float totalGrL = 0.0f, totalGrR = 0.0f;
    for (int b = 0; b < numBands; ++b)
    {
        totalGrL += static_cast<float>(bandProcessorsL[b].getParameters().gain_reduction_dB);
        totalGrR += static_cast<float>(bandProcessorsR[b].getParameters().gain_reduction_dB);
    }
    const float avgGR      = 0.5f * (totalGrL + totalGrR) / static_cast<float>(numBands);
    const float grAmountDb = std::max(0.0f, -avgGR);

    const float monPeakIn  = useSidechain ? sidechainPeakIn : blockPeakIn;
    const float blockPeakDb = monPeakIn > 0.000001f
                              ? juce::Decibels::gainToDecibels(monPeakIn)
                              : -100.0f;

    const float aboveThreshold = std::max(0.0f, blockPeakDb - static_cast<float>(dynParams.threshold_dB));

    inputLevelSmoothedDb    = juce::jmax(blockPeakDb, inputLevelSmoothedDb - 0.8f);
    gainReductionSmoothedDb = juce::jmax(grAmountDb,  gainReductionSmoothedDb - 0.35f);

    gainReductionDb.store(avgGR);
    gainReductionAmountDb.store(gainReductionSmoothedDb);
    inputLevelDb.store(inputLevelSmoothedDb);

    const auto   endTime       = juce::Time::getHighResolutionTicks();
    const double elapsedMs     = 1000.0 * static_cast<double>(endTime - startTime) / static_cast<double>(ticksPerSecond);
    const double blockDurationMs = 1000.0 * static_cast<double>(numSamples) / std::max(1.0, getSampleRate());

    processTimeMs.store(static_cast<float>(elapsedMs));
    cpuUsagePercent.store(blockDurationMs > 0.0
        ? static_cast<float>(100.0 * elapsedMs / blockDurationMs)
        : 0.0f);

    aboveThresholdDb.store(aboveThreshold);
    currentBlockSize.store(numSamples);
    sidechainConnected.store(scConnected);
}

//==============================================================================
bool MaybeDuckAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor *MaybeDuckAudioProcessor::createEditor()
{
    return new MaybeDuckAudioProcessorEditor(*this);
}

//==============================================================================
void MaybeDuckAudioProcessor::getStateInformation(juce::MemoryBlock &destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void MaybeDuckAudioProcessor::setStateInformation(const void *data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState != nullptr)
        if (xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

//==============================================================================
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter()
{
    return new MaybeDuckAudioProcessor();
}
