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

    static constexpr float kLookaheadSteps[]  = { 0.0f, 3.0f, 6.0f, 10.0f };
    static constexpr int   kNumLookaheadSteps = 4;
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
    pHold      = apvts.getRawParameterValue("hold");
    pLookahead = apvts.getRawParameterValue("lookahead");
    pKnee      = apvts.getRawParameterValue("knee");
    pOutput    = apvts.getRawParameterValue("output");
    pScEnable  = apvts.getRawParameterValue("sidechainEnable");
    pSoftKnee  = apvts.getRawParameterValue("softKnee");
    pLimiter   = apvts.getRawParameterValue("limiter");
    for (int i = 0; i < StereoMultibandCrossover::numCrossoverPoints; ++i)
        pXover[i] = apvts.getRawParameterValue("xover" + juce::String(i));

    for (int b = 0; b < StereoMultibandCrossover::numBands; ++b)
        pBandScale[b] = apvts.getRawParameterValue("bandScale" + juce::String(b));

    pSidechainMode = apvts.getRawParameterValue("sidechainMode");
    pFftConv       = apvts.getRawParameterValue("fftConvolution");
    pFftConvSize   = apvts.getRawParameterValue("fftConvSize");

    pGrOut = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("grOut"));

    pEqLowFreq  = apvts.getRawParameterValue("eqLowFreq");
    pEqLowGain  = apvts.getRawParameterValue("eqLowGain");
    pEqMidFreq  = apvts.getRawParameterValue("eqMidFreq");
    pEqMidGain  = apvts.getRawParameterValue("eqMidGain");
    pEqMidQ     = apvts.getRawParameterValue("eqMidQ");
    pEqHighFreq = apvts.getRawParameterValue("eqHighFreq");
    pEqHighGain = apvts.getRawParameterValue("eqHighGain");
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
        "hold", "Hold", juce::NormalisableRange<float>(0.0f, 2000.0f, 1.0f, 0.5f), 0.0f));

    // Lookahead is stepped: 0 / 3 / 6 / 10 ms
    juce::NormalisableRange<float> lookaheadRange(
        0.0f, 10.0f,
        [](float, float, float v) -> float {
            const int idx = juce::roundToInt(v * (float)(kNumLookaheadSteps - 1));
            return kLookaheadSteps[juce::jlimit(0, kNumLookaheadSteps - 1, idx)];
        },
        [](float, float, float v) -> float {
            int best = 0;
            float bestDist = std::abs(v - kLookaheadSteps[0]);
            for (int i = 1; i < kNumLookaheadSteps; ++i) {
                const float d = std::abs(v - kLookaheadSteps[i]);
                if (d < bestDist) { bestDist = d; best = i; }
            }
            return (float) best / (float)(kNumLookaheadSteps - 1);
        },
        [](float, float, float v) -> float {
            float best = kLookaheadSteps[0];
            float bestDist = std::abs(v - kLookaheadSteps[0]);
            for (int i = 1; i < kNumLookaheadSteps; ++i) {
                const float d = std::abs(v - kLookaheadSteps[i]);
                if (d < bestDist) { bestDist = d; best = kLookaheadSteps[i]; }
            }
            return best;
        });
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lookahead", "Lookahead", lookaheadRange, 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "knee", "Knee", juce::NormalisableRange<float>(0.0f, 24.0f, 0.1f), 6.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "output", "Output", juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "sidechainEnable", "Enable Sidechain", false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "softKnee", "Soft Knee", true));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "limiter", "Hard Duck", false));

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

    // Per-band duck scale: additive dB offset on the computed gain-reduction amount,
    // floored at 0 dB so a negative offset only undoes existing reduction (no boost).
    static const char* bandScaleNames[] = { "Band 1 Scale", "Band 2 Scale", "Band 3 Scale", "Band 4 Scale", "Band 5 Scale" };
    for (int b = 0; b < StereoMultibandCrossover::numBands; ++b)
    {
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "bandScale" + juce::String(b),
            bandScaleNames[b],
            juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
            0.0f));
    }

    // 0 = Wideband (bypass crossover), 1 = Multiband, 2 = Spectral
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        "sidechainMode", "Sidechain Mode", 0, 2, 1));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "fftConvolution", "FFT Convolution", false));

    // 0 = 512, 1 = 1024, 2 = 2048
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        "fftConvSize", "FFT Conv Size", 0, 2, 1));

    // Post-ducking EQ (low shelf + mid bell + high shelf), applied after band summing
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "eqLowFreq",  "EQ Low Freq",  juce::NormalisableRange<float>(20.0f,   500.0f, 1.0f, 0.5f),   100.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "eqLowGain",  "EQ Low Gain",  juce::NormalisableRange<float>(-12.0f,  12.0f,  0.1f),         0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "eqMidFreq",  "EQ Mid Freq",  juce::NormalisableRange<float>(100.0f,  10000.0f, 1.0f, 0.3f), 1000.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "eqMidGain",  "EQ Mid Gain",  juce::NormalisableRange<float>(-12.0f,  12.0f,  0.1f),         0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "eqMidQ",     "EQ Mid Q",     juce::NormalisableRange<float>(0.2f,    4.0f,   0.01f, 0.5f),  0.707f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "eqHighFreq", "EQ High Freq", juce::NormalisableRange<float>(2000.0f, 20000.0f, 1.0f, 0.3f), 8000.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "eqHighGain", "EQ High Gain", juce::NormalisableRange<float>(-12.0f,  12.0f,  0.1f),         0.0f));

    // Output-only parameter: current gain-reduction amount in dB (positive). Written from
    // the audio thread once per block so hosts can record it as automation. Reading or
    // editing it from the host has no effect on the DSP.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "grOut", "GR Output (dB)", juce::NormalisableRange<float>(0.0f, 60.0f, 0.01f), 0.0f));

    return {params.begin(), params.end()};
}

int MaybeDuckAudioProcessor::getConvLatencySamples() const noexcept
{
    int conv = 0;
    if (pSidechainMode && pFftConv && pFftConvSize)
    {
        const int mode = static_cast<int>(pSidechainMode->load());
        if (mode == 2 && pFftConv->load() > 0.5f)
        {
            static constexpr int sizes[] = { 512, 1024, 2048 };
            const int idx = juce::jlimit(0, 2, static_cast<int>(pFftConvSize->load()));
            conv = sizes[idx] / 2;
        }
    }

    int look = 0;
    if (pLookahead)
    {
        const double sr = juce::jmax(1.0, currentSampleRate.load());
        look = static_cast<int>(std::round(static_cast<double>(*pLookahead) * 0.001 * sr));
    }
    return conv + look;
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

    widebandProcessorL.reset(sampleRate);
    widebandProcessorR.reset(sampleRate);

    crossover.prepare(sampleRate);

    for (int i = 0; i < StereoMultibandCrossover::numCrossoverPoints; ++i)
        crossover.setCrossoverFrequencyImmediate(i, static_cast<double>(*pXover[i]));

    // Main band buffers need numBands channels; mainBandBufL[0] is reused as conv-mode input
    // temp buffer to avoid in-place aliasing with mainInput.
    mainBandBufL.setSize(numBands, samplesPerBlock);
    mainBandBufR.setSize(numBands, samplesPerBlock);
    scBandBufL.setSize(numBands, samplesPerBlock);
    scBandBufR.setSize(numBands, samplesPerBlock);

    // Lookahead delay line: max 12 ms at current sample rate (slider goes 0-10 ms).
    const int maxLookahead = static_cast<int>(std::ceil(0.012 * sampleRate));
    lookaheadDelayL.setMaximumDelayInSamples(maxLookahead);
    lookaheadDelayR.setMaximumDelayInSamples(maxLookahead);
    juce::dsp::ProcessSpec lookSpec { sampleRate, (juce::uint32) samplesPerBlock, 1 };
    lookaheadDelayL.prepare(lookSpec);
    lookaheadDelayR.prepare(lookSpec);
    lookaheadDelayL.reset();
    lookaheadDelayR.reset();
    lookaheadBufL.setSize(1, samplesPerBlock);
    lookaheadBufR.setSize(1, samplesPerBlock);
    prevLookaheadSamples = 0;

    // Post-ducking EQ filter prep
    juce::dsp::ProcessSpec eqSpec { sampleRate, (juce::uint32) samplesPerBlock, 1 };
    for (auto* f : { &eqLowL, &eqLowR, &eqMidL, &eqMidR, &eqHighL, &eqHighR })
    {
        f->prepare(eqSpec);
        f->reset();
    }
    // Force coefficient recomputation on first block by invalidating the cached values.
    prevEqLowFreq = prevEqMidFreq = prevEqHighFreq = -1.0f;
    prevEqLowGain = prevEqMidGain = prevEqHighGain = -999.0f;
    prevEqMidQ    = -1.0f;

    // FFT Conv objects — pre-allocate all three sizes (no audio-thread allocation later)
    static constexpr int kOrders[3] = { 9, 10, 11 };
    static constexpr int kSizes [3] = { 512, 1024, 2048 };
    for (int i = 0; i < 3; ++i)
    {
        convFftObjs[i] = std::make_unique<juce::dsp::FFT>(kOrders[i]);
        convWinObjs[i] = std::make_unique<juce::dsp::WindowingFunction<float>>(
            (size_t)kSizes[i], juce::dsp::WindowingFunction<float>::hann);
    }

    // Clear all conv state and prime FIFO with hopSize zeros (= reported latency)
    std::fill(std::begin(convInRingL), std::end(convInRingL), 0.0f);
    std::fill(std::begin(convInRingR), std::end(convInRingR), 0.0f);
    std::fill(std::begin(convScRing),  std::end(convScRing),  0.0f);
    std::fill(std::begin(convOvlL),    std::end(convOvlL),    0.0f);
    std::fill(std::begin(convOvlR),    std::end(convOvlR),    0.0f);
    std::fill(std::begin(convOutFifoL), std::end(convOutFifoL), 0.0f);
    std::fill(std::begin(convOutFifoR), std::end(convOutFifoR), 0.0f);
    convInWritePos = 0;
    convScWritePos = 0;
    convNewCount   = 0;

    const int initSizeIdx = pFftConvSize ? juce::jlimit(0, 2, (int)pFftConvSize->load()) : 1;
    const int initHop = kSizes[initSizeIdx] / 2;
    convOutReadPos  = 0;
    convOutWritePos = initHop;
    convOutCount    = initHop;
    prevConvSizeIdx = initSizeIdx;

    // Spectral SC analysis state
    std::fill(std::begin(scSpectralRing), std::end(scSpectralRing), 0.0f);
    std::fill(std::begin(spectralMags),   std::end(spectralMags),   0.0f);
    scSpectralHead = 0;

    // Latency: report initial value based on current params
    const int initLatency = getConvLatencySamples();
    setLatencySamples(initLatency);
    prevLatencyReported = initLatency;

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
    dynParams.hold_ms         = *pHold;
    dynParams.knee_width_dB   = *pKnee;
    dynParams.output_gain_dB  = *pOutput;
    dynParams.enable_sc       = (*pScEnable  > 0.5f);
    dynParams.soft_knee       = (*pSoftKnee  > 0.5f);
    dynParams.hard_limit_gate = (*pLimiter   > 0.5f);
    dynParams.calculation     = dynamicsProcessorType::kCompressor;

    // --- Read mode params
    const int  mode       = pSidechainMode ? juce::jlimit(0, 2, (int) pSidechainMode->load()) : 1;
    const bool useConv    = pFftConv && (*pFftConv > 0.5f);
    const int  sizeIdx    = pFftConvSize ? juce::jlimit(0, 2, (int) pFftConvSize->load()) : 1;
    static constexpr int kConvSizes[3] = { 512, 1024, 2048 };
    const int  activeConvFftSize = kConvSizes[sizeIdx];
    const int  hopSize           = activeConvFftSize / 2;

    // Reset conv state if user changed FFT size
    if (sizeIdx != prevConvSizeIdx)
    {
        std::fill(std::begin(convInRingL), std::end(convInRingL), 0.0f);
        std::fill(std::begin(convInRingR), std::end(convInRingR), 0.0f);
        std::fill(std::begin(convScRing),  std::end(convScRing),  0.0f);
        std::fill(std::begin(convOvlL),    std::end(convOvlL),    0.0f);
        std::fill(std::begin(convOvlR),    std::end(convOvlR),    0.0f);
        std::fill(std::begin(convOutFifoL), std::end(convOutFifoL), 0.0f);
        std::fill(std::begin(convOutFifoR), std::end(convOutFifoR), 0.0f);
        convInWritePos  = 0;
        convScWritePos  = 0;
        convNewCount    = 0;
        convOutReadPos  = 0;
        convOutWritePos = hopSize;
        convOutCount    = hopSize;
        prevConvSizeIdx = sizeIdx;
    }

    // --- Lookahead: clamp to allocated max, set tap, update latency report
    const double sr = juce::jmax(1.0, getSampleRate());
    const float lookaheadMs = pLookahead ? pLookahead->load() : 0.0f;
    const int lookaheadSamples = juce::jlimit(
        0, lookaheadDelayL.getMaximumDelayInSamples(),
        static_cast<int>(std::round(static_cast<double>(lookaheadMs) * 0.001 * sr)));
    lookaheadDelayL.setDelay(static_cast<float>(lookaheadSamples));
    lookaheadDelayR.setDelay(static_cast<float>(lookaheadSamples));
    prevLookaheadSamples = lookaheadSamples;

    // Update reported latency when mode/conv/lookahead changes
    const int newLatency = ((mode == 2 && useConv) ? hopSize : 0) + lookaheadSamples;
    if (newLatency != prevLatencyReported)
    {
        setLatencySamples(newLatency);
        prevLatencyReported = newLatency;
    }

    // Detector always reads sc_xn (the live SC source — external bus or live main alias).
    // The processor uses sc_xn unconditionally so lookahead is honored even when no external SC is connected.
    DynamicsProcessorParameters dynParamsForProc = dynParams;
    dynParamsForProc.enable_sc = true;
    for (int b = 0; b < numBands; ++b)
    {
        bandProcessorsL[b].setParameters(dynParamsForProc);
        bandProcessorsR[b].setParameters(dynParamsForProc);
    }
    widebandProcessorL.setParameters(dynParamsForProc);
    widebandProcessorR.setParameters(dynParamsForProc);

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

    const float* liveMainL = mainInput.getReadPointer(0);
    const float* liveMainR = isStereo ? mainInput.getReadPointer(1) : liveMainL;

    // SC always reads the LIVE signal so the detector sees future relative to the delayed main
    const float* scLPtr = useSidechain ? scInput.getReadPointer(0) : liveMainL;
    const float* scRPtr = useSidechain
                          ? (scInput.getNumChannels() > 1 ? scInput.getReadPointer(1) : scLPtr)
                          : liveMainR;

    // --- Lookahead delay: always run so toggling lookahead is glitch-free
    float* dL = lookaheadBufL.getWritePointer(0);
    float* dR = lookaheadBufR.getWritePointer(0);
    for (int n = 0; n < numSamples; ++n)
    {
        lookaheadDelayL.pushSample(0, liveMainL[n]);
        dL[n] = lookaheadDelayL.popSample(0);
        if (isStereo)
        {
            lookaheadDelayR.pushSample(0, liveMainR[n]);
            dR[n] = lookaheadDelayR.popSample(0);
        }
    }
    const float* mainL = dL;
    const float* mainR = isStereo ? dR : dL;

    // --- Input / sidechain metering (pre-dynamics, on LIVE signal)
    float blockPeakIn     = 0.0f;
    float sidechainPeakIn = 0.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        const float mono = 0.5f * (liveMainL[n] + liveMainR[n]);
        blockPeakIn = std::max(blockPeakIn, std::abs(mono));

        if (useSidechain)
        {
            const float scMono = 0.5f * (scLPtr[n] + scRPtr[n]);
            sidechainPeakIn = std::max(sidechainPeakIn, std::abs(scMono));
        }
    }

    auto* outL = mainInput.getWritePointer(0);
    auto* outR = isStereo ? mainInput.getWritePointer(1) : nullptr;

    float grAmountDb = 0.0f;

    if (mode == 0)
    {
        // === WIDEBAND ===
        // Bypass crossover, single compressor pair. mainL/outL may share storage,
        // but we read in[n] before writing out[n], which is safe.
        for (int n = 0; n < numSamples; ++n)
        {
            const double inL = static_cast<double>(mainL[n]);
            const double inR = isStereo ? static_cast<double>(mainR[n]) : inL;

            widebandProcessorL.processSidechainInputSample(static_cast<double>(scLPtr[n]));
            widebandProcessorR.processSidechainInputSample(static_cast<double>(scRPtr[n]));
            const double pL = widebandProcessorL.processSample(inL);
            const double pR = widebandProcessorR.processSample(inR);
            outL[n] = static_cast<float>(pL);
            if (outR) outR[n] = static_cast<float>(pR);
        }
        grAmountDb = std::max(
            -static_cast<float>(widebandProcessorL.getGainReductionDb()),
            -static_cast<float>(widebandProcessorR.getGainReductionDb()));
    }
    else if (mode == 1 || (mode == 2 && !useConv))
    {
        // === MULTIBAND (mode 1) or SPECTRAL without conv (mode 2 + !useConv) ===
        float* mbL[numBands], *mbR[numBands], *sbL[numBands], *sbR[numBands];
        for (int b = 0; b < numBands; ++b)
        {
            mbL[b] = mainBandBufL.getWritePointer(b);
            mbR[b] = mainBandBufR.getWritePointer(b);
            sbL[b] = scBandBufL.getWritePointer(b);
            sbR[b] = scBandBufR.getWritePointer(b);
        }
        crossover.processBlock(mainL, mainR, scLPtr, scRPtr, mbL, mbR, sbL, sbR, numSamples);

        juce::FloatVectorOperations::fill(outL, 0.0f, numSamples);
        if (outR) juce::FloatVectorOperations::fill(outR, 0.0f, numSamples);

        // Spectral mode: per-band SC energy via FFT (zero-latency, look-back window).
        // scLPtr/scRPtr alias liveMain when no external SC, so this always has valid data.
        float perBandScLevel[numBands] = {};
        if (mode == 2)
        {
            for (int n = 0; n < numSamples; ++n)
            {
                scSpectralRing[scSpectralHead] = 0.5f * (scLPtr[n] + scRPtr[n]);
                scSpectralHead = (scSpectralHead + 1) % kSpectralFftSize;
            }
            for (int i = 0; i < kSpectralFftSize; ++i)
                spectralWorkBuf[i] = scSpectralRing[(scSpectralHead + i) % kSpectralFftSize];
            std::fill(spectralWorkBuf + kSpectralFftSize,
                      spectralWorkBuf + kSpectralFftSize * 2, 0.0f);
            spectralWindow.multiplyWithWindowingTable(spectralWorkBuf, kSpectralFftSize);
            spectralFft.performRealOnlyForwardTransform(spectralWorkBuf);

            const float invN = 1.0f / (float) kSpectralFftSize;
            spectralMags[0] = std::abs(spectralWorkBuf[0]) * invN;
            for (int k = 1; k < kSpectralFftSize / 2; ++k)
            {
                const float re = spectralWorkBuf[2 * k];
                const float im = spectralWorkBuf[2 * k + 1];
                spectralMags[k] = std::sqrt(re * re + im * im) * invN;
            }

            const double sr = std::max(1.0, getSampleRate());
            double bandEdges[numBands + 1];
            bandEdges[0] = 20.0;
            for (int i = 0; i < StereoMultibandCrossover::numCrossoverPoints; ++i)
                bandEdges[i + 1] = static_cast<double>(*pXover[i]);
            bandEdges[numBands] = sr * 0.5;

            for (int b = 0; b < numBands; ++b)
            {
                const int kLow  = juce::jmax(1,
                    (int) std::floor(bandEdges[b]     * (double) kSpectralFftSize / sr));
                const int kHigh = juce::jmin(kSpectralFftSize / 2 - 1,
                    (int) std::ceil (bandEdges[b + 1] * (double) kSpectralFftSize / sr));
                float sumSq = 0.0f;
                int   count = 0;
                for (int k = kLow; k <= kHigh; ++k)
                {
                    sumSq += spectralMags[k] * spectralMags[k];
                    ++count;
                }
                // *2 compensates for one-sided spectrum + Hann window energy loss
                perBandScLevel[b] = count > 0 ? std::sqrt(sumSq / (float) count) * 2.0f : 0.0f;
            }
        }

        for (int b = 0; b < numBands; ++b)
        {
            const float offsetDb   = pBandScale[b] ? pBandScale[b]->load() : 0.0f;
            const bool  needsScale = std::abs(offsetDb) > 0.005f;

            for (int n = 0; n < numSamples; ++n)
            {
                if (mode == 2)
                {
                    const double scVal = static_cast<double>(perBandScLevel[b]);
                    bandProcessorsL[b].processSidechainInputSample(scVal);
                    bandProcessorsR[b].processSidechainInputSample(scVal);
                }
                else
                {
                    bandProcessorsL[b].processSidechainInputSample(static_cast<double>(sbL[b][n]));
                    bandProcessorsR[b].processSidechainInputSample(static_cast<double>(sbR[b][n]));
                }
                float pL = static_cast<float>(bandProcessorsL[b].processSample(static_cast<double>(mbL[b][n])));
                float pR = static_cast<float>(bandProcessorsR[b].processSample(static_cast<double>(mbR[b][n])));

                if (needsScale)
                {
                    // Re-scale the per-sample gain reduction by offsetDb (additive on duck amount, floor at 0).
                    // ratio = 10^((new_gr_dB - old_gr_dB)/20); new_gr_dB = -max(0, -old_gr_dB + offsetDb).
                    const float oldGrDbL = static_cast<float>(bandProcessorsL[b].getGainReductionDb());
                    const float oldGrDbR = static_cast<float>(bandProcessorsR[b].getGainReductionDb());
                    const float newGrDbL = -juce::jmax(0.0f, -oldGrDbL + offsetDb);
                    const float newGrDbR = -juce::jmax(0.0f, -oldGrDbR + offsetDb);
                    pL *= juce::dsp::FastMathApproximations::exp((newGrDbL - oldGrDbL) * 0.11512925f);
                    pR *= juce::dsp::FastMathApproximations::exp((newGrDbR - oldGrDbR) * 0.11512925f);
                }

                outL[n] += pL;
                if (outR) outR[n] += pR;
            }
        }

        for (int b = 0; b < numBands; ++b)
        {
            grAmountDb = std::max(grAmountDb, -static_cast<float>(bandProcessorsL[b].getGainReductionDb()));
            grAmountDb = std::max(grAmountDb, -static_cast<float>(bandProcessorsR[b].getGainReductionDb()));
        }
    }
    else
    {
        // === SPECTRAL + FFT CONV (mode 2 + useConv) ===
        // Copy input to scratch since mainInput buffer may alias output
        float* tmpL = mainBandBufL.getWritePointer(0);
        float* tmpR = mainBandBufR.getWritePointer(0);
        juce::FloatVectorOperations::copy(tmpL, mainL, numSamples);
        if (isStereo) juce::FloatVectorOperations::copy(tmpR, mainR, numSamples);

        for (int n = 0; n < numSamples; ++n)
        {
            const float inL = tmpL[n];
            const float inR = isStereo ? tmpR[n] : tmpL[n];

            convInRingL[convInWritePos] = inL;
            convInRingR[convInWritePos] = inR;
            convInWritePos = (convInWritePos + 1) % activeConvFftSize;

            convScRing[convScWritePos] = useSidechain
                ? 0.5f * (scLPtr[n] + scRPtr[n])
                : 0.0f;
            convScWritePos = (convScWritePos + 1) % activeConvFftSize;

            if (++convNewCount >= hopSize)
            {
                convNewCount = 0;
                processConvFrame(activeConvFftSize, sizeIdx,
                                 dynParams.threshold_dB, dynParams.ratio,
                                 useSidechain);
            }

            outL[n] = convOutFifoL[convOutReadPos];
            if (outR) outR[n] = convOutFifoR[convOutReadPos];
            convOutReadPos = (convOutReadPos + 1) % kConvOutFifoSize;
            if (convOutCount > 0) --convOutCount;
        }

        grAmountDb = convFrameGrDb;
    }

    // --- Post-ducking EQ (low shelf + mid bell + high shelf)
    {
        const float lowFreq  = pEqLowFreq  ? pEqLowFreq->load()  : 100.0f;
        const float lowGain  = pEqLowGain  ? pEqLowGain->load()  : 0.0f;
        const float midFreq  = pEqMidFreq  ? pEqMidFreq->load()  : 1000.0f;
        const float midGain  = pEqMidGain  ? pEqMidGain->load()  : 0.0f;
        const float midQ     = pEqMidQ     ? pEqMidQ->load()     : 0.707f;
        const float highFreq = pEqHighFreq ? pEqHighFreq->load() : 8000.0f;
        const float highGain = pEqHighGain ? pEqHighGain->load() : 0.0f;

        if (lowFreq != prevEqLowFreq || lowGain != prevEqLowGain)
        {
            auto c = juce::dsp::IIR::Coefficients<float>::makeLowShelf(
                sr, lowFreq, 0.707f, juce::Decibels::decibelsToGain(lowGain));
            *eqLowL.coefficients = *c;
            *eqLowR.coefficients = *c;
            prevEqLowFreq = lowFreq; prevEqLowGain = lowGain;
        }
        if (midFreq != prevEqMidFreq || midGain != prevEqMidGain || midQ != prevEqMidQ)
        {
            auto c = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                sr, midFreq, midQ, juce::Decibels::decibelsToGain(midGain));
            *eqMidL.coefficients = *c;
            *eqMidR.coefficients = *c;
            prevEqMidFreq = midFreq; prevEqMidGain = midGain; prevEqMidQ = midQ;
        }
        if (highFreq != prevEqHighFreq || highGain != prevEqHighGain)
        {
            auto c = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
                sr, highFreq, 0.707f, juce::Decibels::decibelsToGain(highGain));
            *eqHighL.coefficients = *c;
            *eqHighR.coefficients = *c;
            prevEqHighFreq = highFreq; prevEqHighGain = highGain;
        }

        // Skip the IIR pass when all bands are at 0 dB (flat) — saves cycles in the common case
        const bool eqActive = std::abs(lowGain) > 0.01f || std::abs(midGain) > 0.01f || std::abs(highGain) > 0.01f;
        if (eqActive)
        {
            for (int n = 0; n < numSamples; ++n)
            {
                outL[n] = eqHighL.processSample(eqMidL.processSample(eqLowL.processSample(outL[n])));
                if (outR)
                    outR[n] = eqHighR.processSample(eqMidR.processSample(eqLowR.processSample(outR[n])));
            }
        }
    }

    const float monPeakIn  = useSidechain ? sidechainPeakIn : blockPeakIn;
    const float blockPeakDb = monPeakIn > 0.000001f
                              ? juce::Decibels::gainToDecibels(monPeakIn)
                              : -100.0f;

    const float aboveThreshold = std::max(0.0f, blockPeakDb - static_cast<float>(dynParams.threshold_dB));

    inputLevelSmoothedDb    = juce::jmax(blockPeakDb, inputLevelSmoothedDb - 0.8f);
    gainReductionSmoothedDb = juce::jmax(grAmountDb,  gainReductionSmoothedDb - 0.35f);

    gainReductionDb.store(-grAmountDb);
    gainReductionAmountDb.store(gainReductionSmoothedDb);
    inputLevelDb.store(inputLevelSmoothedDb);

    // Surface the GR amount to the host as a recordable parameter. Skip the notify when
    // the value hasn't moved by at least 0.05 dB so steady-state doesn't spam listeners.
    if (pGrOut != nullptr)
    {
        const float reported = pGrOut->get();
        const float desired  = juce::jlimit(0.0f, 60.0f, grAmountDb);
        if (std::abs(reported - desired) > 0.05f)
            pGrOut->setValueNotifyingHost(pGrOut->convertTo0to1(desired));
    }

    const auto   endTime         = juce::Time::getHighResolutionTicks();
    const double elapsedMs       = 1000.0 * static_cast<double>(endTime - startTime) / static_cast<double>(ticksPerSecond);
    const double blockDurationMs = 1000.0 * static_cast<double>(numSamples) / std::max(1.0, getSampleRate());

    processTimeMs.store(static_cast<float>(elapsedMs));
    cpuUsagePercent.store(blockDurationMs > 0.0
        ? static_cast<float>(100.0 * elapsedMs / blockDurationMs)
        : 0.0f);

    aboveThresholdDb.store(aboveThreshold);
    currentBlockSize.store(numSamples);
    sidechainConnected.store(scConnected);
}

void MaybeDuckAudioProcessor::processConvFrame(int fftSize, int sizeIdx,
                                                double thresholdDb, double ratio,
                                                bool useSidechain)
{
    const int hopSize = fftSize / 2;
    auto& fft = *convFftObjs[sizeIdx];
    auto& win = *convWinObjs[sizeIdx];

    // Assemble frames from ring buffers (oldest → newest)
    for (int i = 0; i < fftSize; ++i)
        convWorkL[i] = convInRingL[(convInWritePos + i) % fftSize];
    std::fill(convWorkL + fftSize, convWorkL + 2 * fftSize, 0.0f);

    for (int i = 0; i < fftSize; ++i)
        convWorkR[i] = convInRingR[(convInWritePos + i) % fftSize];
    std::fill(convWorkR + fftSize, convWorkR + 2 * fftSize, 0.0f);

    for (int i = 0; i < fftSize; ++i)
        convScWork[i] = convScRing[(convScWritePos + i) % fftSize];
    std::fill(convScWork + fftSize, convScWork + 2 * fftSize, 0.0f);

    win.multiplyWithWindowingTable(convWorkL,  fftSize);
    win.multiplyWithWindowingTable(convWorkR,  fftSize);
    win.multiplyWithWindowingTable(convScWork, fftSize);

    fft.performRealOnlyForwardTransform(convWorkL);
    fft.performRealOnlyForwardTransform(convWorkR);
    fft.performRealOnlyForwardTransform(convScWork);

    const float invN      = 1.0f / (float) fftSize;
    const float threshLin = juce::Decibels::decibelsToGain(static_cast<float>(thresholdDb));
    const float invRatio  = 1.0f / static_cast<float>(juce::jmax(0.001, ratio));
    float worstGainDb     = 0.0f;

    auto binGain = [&](float scMag) -> float
    {
        if (!useSidechain) return 1.0f;
        if (scMag <= threshLin) return 1.0f;
        const float overDb = juce::Decibels::gainToDecibels(scMag / threshLin);
        const float gainDb = (invRatio - 1.0f) * overDb;  // negative
        if (gainDb < worstGainDb) worstGainDb = gainDb;
        return juce::Decibels::decibelsToGain(gainDb);
    };

    // DC bin (real-only at index 0)
    {
        const float scMag = std::abs(convScWork[0]) * invN;
        const float g = binGain(scMag);
        convWorkL[0] *= g;
        convWorkR[0] *= g;
    }
    // Nyquist (real-only, packed at index 1)
    {
        const float scMag = std::abs(convScWork[1]) * invN;
        const float g = binGain(scMag);
        convWorkL[1] *= g;
        convWorkR[1] *= g;
    }
    // Mid bins (Re, Im interleaved)
    for (int k = 1; k < fftSize / 2; ++k)
    {
        const float re = convScWork[2 * k];
        const float im = convScWork[2 * k + 1];
        const float scMag = std::sqrt(re * re + im * im) * invN;
        const float g = binGain(scMag);
        convWorkL[2 * k    ] *= g;
        convWorkL[2 * k + 1] *= g;
        convWorkR[2 * k    ] *= g;
        convWorkR[2 * k + 1] *= g;
    }

    fft.performRealOnlyInverseTransform(convWorkL);
    fft.performRealOnlyInverseTransform(convWorkR);

    // OLA: emit first half + overlap, save second half as next overlap
    for (int i = 0; i < hopSize; ++i)
    {
        convOutFifoL[convOutWritePos] = convWorkL[i] + convOvlL[i];
        convOutFifoR[convOutWritePos] = convWorkR[i] + convOvlR[i];
        convOutWritePos = (convOutWritePos + 1) % kConvOutFifoSize;
        if (convOutCount < kConvOutFifoSize) ++convOutCount;
    }
    for (int i = 0; i < hopSize; ++i)
    {
        convOvlL[i] = convWorkL[hopSize + i];
        convOvlR[i] = convWorkR[hopSize + i];
    }

    convFrameGrDb = -worstGainDb;
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
