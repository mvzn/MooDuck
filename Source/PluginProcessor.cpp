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
    pThreshold = apvts.getRawParameterValue("threshold");
    pRatio     = apvts.getRawParameterValue("ratio");
    pAttack    = apvts.getRawParameterValue("attack");
    pRelease   = apvts.getRawParameterValue("release");
    pKnee      = apvts.getRawParameterValue("knee");
    pOutput    = apvts.getRawParameterValue("output");
    pScEnable  = apvts.getRawParameterValue("sidechainEnable");
    pSoftKnee  = apvts.getRawParameterValue("softKnee");
    pLimiter   = apvts.getRawParameterValue("limiter");
    for (int i = 0; i < StereoMultibandCrossover::numCrossoverPoints; ++i)
        pXover[i] = apvts.getRawParameterValue("xover" + juce::String(i));
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
        crossover.setCrossoverFrequencyImmediate(i, static_cast<double>(*pXover[i]));

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

    // --- Read dynamics parameters (atomic loads via cached pointers — no hash lookups)
    DynamicsProcessorParameters dynParams;
    dynParams.threshold_dB    = *pThreshold;
    dynParams.ratio           = *pRatio;
    dynParams.attack_ms       = *pAttack;
    dynParams.release_ms      = *pRelease;
    dynParams.knee_width_dB   = *pKnee;
    dynParams.output_gain_dB  = *pOutput;
    dynParams.enable_sc       = (*pScEnable  > 0.5f);
    dynParams.soft_knee       = (*pSoftKnee  > 0.5f);
    dynParams.hard_limit_gate = (*pLimiter   > 0.5f);
    dynParams.calculation     = dynamicsProcessorType::kCompressor;

    for (int b = 0; b < numBands; ++b)
    {
        bandProcessorsL[b].setParameters(dynParams);
        bandProcessorsR[b].setParameters(dynParams);
    }

    // --- Update crossover frequencies (smoother advances inside processBlock)
    for (int i = 0; i < StereoMultibandCrossover::numCrossoverPoints; ++i)
        crossover.setCrossoverFrequency(i, static_cast<double>(*pXover[i]));

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

    // Cache SC read pointers once (used for both metering and crossover)
    const float* scLPtr = useSidechain ? scInput.getReadPointer(0) : mainL;
    const float* scRPtr = useSidechain
                          ? (scInput.getNumChannels() > 1 ? scInput.getReadPointer(1) : scLPtr)
                          : mainR;

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
            const float scMono = 0.5f * (scLPtr[n] + scRPtr[n]);
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

    crossover.processBlock(mainL, mainR, scLPtr, scRPtr, mbL, mbR, sbL, sbR, numSamples);

    // --- Dynamics (band-outer/sample-inner for processor state cache locality)
    //     Accumulate directly into output buffer to merge the summation pass
    auto* outL = mainInput.getWritePointer(0);
    auto* outR = isStereo ? mainInput.getWritePointer(1) : nullptr;

    juce::FloatVectorOperations::fill(outL, 0.0f, numSamples);
    if (outR) juce::FloatVectorOperations::fill(outR, 0.0f, numSamples);

    for (int b = 0; b < numBands; ++b)
    {
        for (int n = 0; n < numSamples; ++n)
        {
            if (useSidechain)
            {
                bandProcessorsL[b].processSidechainInputSample(static_cast<double>(sbL[b][n]));
                bandProcessorsR[b].processSidechainInputSample(static_cast<double>(sbR[b][n]));
            }
            outL[n] += static_cast<float>(bandProcessorsL[b].processSample(static_cast<double>(mbL[b][n])));
            const float pR = static_cast<float>(bandProcessorsR[b].processSample(static_cast<double>(mbR[b][n])));
            if (outR) outR[n] += pR;
        }
    }

    // --- Output FFT push
    for (int n = 0; n < numSamples; ++n)
    {
        const float outMono = outR ? 0.5f * (outL[n] + outR[n]) : outL[n];
        pushNextSampleIntoFifo(outMono, outputFifo, outputFifoIndex, outputFftBuffer, outputBlockReady);
    }

    // --- Metering (direct field access avoids by-value struct copy)
    float totalGrL = 0.0f, totalGrR = 0.0f;
    for (int b = 0; b < numBands; ++b)
    {
        totalGrL += static_cast<float>(bandProcessorsL[b].getGainReductionDb());
        totalGrR += static_cast<float>(bandProcessorsR[b].getGainReductionDb());
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
