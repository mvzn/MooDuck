/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "DynamicsCore.h"
#include "CrossoverCore.h"

//==============================================================================
/**
*/
class MaybeDuckAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    MaybeDuckAudioProcessor();
    ~MaybeDuckAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    // We only implement the float path; bring the base double overload into scope
    // so it isn't hidden (it falls back to AudioProcessor's default conversion).
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;
    
    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // --- Discrete step tables (single source of truth). The Ratio and Lookahead
    // params snap to these values; the editor reads the counts to draw one tick
    // per step. Counts derive from the array sizes so they can never drift.
    static constexpr float kRatioSteps[]      = { 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 8.0f, 16.0f, 20.0f, 40.0f, 60.0f };
    static constexpr int   kNumRatioSteps     = (int) (sizeof(kRatioSteps) / sizeof(kRatioSteps[0]));
    static constexpr float kLookaheadSteps[]  = { 0.0f, 3.0f, 6.0f, 10.0f, 16.0f };
    static constexpr int   kNumLookaheadSteps = (int) (sizeof(kLookaheadSteps) / sizeof(kLookaheadSteps[0]));

    juce::AudioProcessorValueTreeState apvts;

    // Cached raw parameter pointers — stable for plugin lifetime, safe on audio thread
    std::atomic<float>* pThreshold = nullptr;
    std::atomic<float>* pRatio     = nullptr;
    std::atomic<float>* pAttack    = nullptr;
    std::atomic<float>* pRelease   = nullptr;
    std::atomic<float>* pHold      = nullptr;
    std::atomic<float>* pLookahead = nullptr;
    std::atomic<float>* pKnee      = nullptr;
    std::atomic<float>* pOutput    = nullptr;
    std::atomic<float>* pScEnable  = nullptr;
    std::atomic<float>* pSoftKnee  = nullptr;
    std::atomic<float>* pLimiter   = nullptr;
    std::atomic<float>* pXover[StereoMultibandCrossover::numCrossoverPoints] = {};
    std::atomic<float>* pBandScale[StereoMultibandCrossover::numBands] = {};
    std::atomic<float>* pSidechainMode = nullptr;
    std::atomic<float>* pFftConv       = nullptr;
    std::atomic<float>* pFftConvSize   = nullptr;

    // Read-only output parameter: GR amount in dB, written from audio thread for host
    // automation recording. Not used to drive any DSP.
    juce::AudioParameterFloat* pGrOut = nullptr;

    // Post-ducking EQ
    std::atomic<float>* pEqLowFreq  = nullptr;
    std::atomic<float>* pEqLowGain  = nullptr;
    std::atomic<float>* pEqMidFreq  = nullptr;
    std::atomic<float>* pEqMidGain  = nullptr;
    std::atomic<float>* pEqMidQ     = nullptr;
    std::atomic<float>* pEqHighFreq = nullptr;
    std::atomic<float>* pEqHighGain = nullptr;

    // monitoring
    float getCpuUsagePercent() const noexcept      { return cpuUsagePercent.load(); }
    float getProcessTimeMs() const noexcept        { return processTimeMs.load(); }
    float getGainReductionDb() const noexcept      { return gainReductionDb.load(); }   // negative or zero internally
    float getGainReductionAmountDb() const noexcept{ return gainReductionAmountDb.load(); } // positive for meter
    float getInputLevelDb() const noexcept         { return inputLevelDb.load(); }
    float getSidechainLevelDb() const noexcept     { return sidechainLevelDb.load(); }
    double getCurrentSampleRateHz() const noexcept { return currentSampleRate.load(); }
    int getCurrentBlockSize() const noexcept       { return currentBlockSize.load(); }
    bool isSidechainConnected() const noexcept     { return sidechainConnected.load(); }

    // Latency reporting for FFT Conv mode (hopSize = fftSize/2 when active)
    int getConvLatencySamples() const noexcept;

private:
    static constexpr int numBands = StereoMultibandCrossover::numBands;

    DynamicsProcessor bandProcessorsL[numBands];
    DynamicsProcessor bandProcessorsR[numBands];
    StereoMultibandCrossover crossover;

    juce::AudioBuffer<float> mainBandBufL, mainBandBufR;
    juce::AudioBuffer<float> scBandBufL,   scBandBufR;

    // Post-ducking EQ — low shelf + mid bell + high shelf, per-channel filter state
    juce::dsp::IIR::Filter<float> eqLowL,  eqLowR;
    juce::dsp::IIR::Filter<float> eqMidL,  eqMidR;
    juce::dsp::IIR::Filter<float> eqHighL, eqHighR;
    float prevEqLowFreq  = 0.0f, prevEqLowGain  = 0.0f;
    float prevEqMidFreq  = 0.0f, prevEqMidGain  = 0.0f, prevEqMidQ = 0.0f;
    float prevEqHighFreq = 0.0f, prevEqHighGain = 0.0f;

    // Lookahead delay line on the main path (sidechain is undelayed → detector sees future)
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> lookaheadDelayL;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> lookaheadDelayR;
    juce::AudioBuffer<float> lookaheadBufL, lookaheadBufR;
    int prevLookaheadSamples = 0;

    // Wideband mode (bypass crossover)
    DynamicsProcessor widebandProcessorL, widebandProcessorR;

    // Spectral SC analysis (1024-pt fixed, zero-latency)
    static constexpr int kSpectralFftOrder = 10;
    static constexpr int kSpectralFftSize  = 1 << kSpectralFftOrder;
    juce::dsp::FFT spectralFft { kSpectralFftOrder };
    juce::dsp::WindowingFunction<float> spectralWindow {
        (size_t)kSpectralFftSize, juce::dsp::WindowingFunction<float>::hann };
    float scSpectralRing[kSpectralFftSize] = {};
    int   scSpectralHead = 0;
    float spectralWorkBuf[kSpectralFftSize * 2] = {};
    float spectralMags   [kSpectralFftSize / 2] = {};

    // FFT Conv (overlap-add STFT, user-selectable: 512 / 1024 / 2048)
    static constexpr int kMaxConvFftSize = 2048;
    std::unique_ptr<juce::dsp::FFT>                      convFftObjs[3]; // orders 9,10,11
    std::unique_ptr<juce::dsp::WindowingFunction<float>> convWinObjs[3]; // sizes 512,1024,2048

    float convInRingL [kMaxConvFftSize]     = {};
    float convInRingR [kMaxConvFftSize]     = {};
    float convScRing  [kMaxConvFftSize]     = {};   // mono SC mix
    int   convInWritePos = 0;
    int   convScWritePos = 0;
    int   convNewCount   = 0;

    float convOvlL[kMaxConvFftSize / 2]    = {};
    float convOvlR[kMaxConvFftSize / 2]    = {};

    static constexpr int kConvOutFifoSize = kMaxConvFftSize * 2;
    float convOutFifoL[kConvOutFifoSize]   = {};
    float convOutFifoR[kConvOutFifoSize]   = {};
    int   convOutReadPos  = 0;
    int   convOutWritePos = 0;
    int   convOutCount    = 0;

    float convWorkL [kMaxConvFftSize * 2]  = {};
    float convWorkR [kMaxConvFftSize * 2]  = {};
    float convScWork[kMaxConvFftSize * 2]  = {};

    int   prevConvSizeIdx     = 1; // 0=512, 1=1024, 2=2048
    int   prevLatencyReported = 0;
    float convFrameGrDb       = 0.0f; // worst-case dB reduction from last conv frame

    void processConvFrame (int fftSize, int sizeIdx,
                           double thresholdDb, double ratio,
                           bool useSidechain);

    struct BandControlValues
    {
        std::array<float, StereoMultibandCrossover::numBands> db {};
    };

    BandControlValues applyBandLink(const BandControlValues& gr, float linkAmount);

    // monitoring
    std::atomic<float> cpuUsagePercent { 0.0f };
    std::atomic<float> processTimeMs   { 0.0f };
    std::atomic<float> gainReductionDb { 0.0f };
    std::atomic<float> gainReductionAmountDb { 0.0f };
    std::atomic<float> inputLevelDb { -100.0f };
    std::atomic<float> sidechainLevelDb { -100.0f };
    std::atomic<double> currentSampleRate { 44100.0 };
    std::atomic<int> currentBlockSize { 0 };
    std::atomic<bool> sidechainConnected { false };

    float inputLevelSmoothedDb = -100.0f;
    float sidechainLevelSmoothedDb = -100.0f;
    float gainReductionSmoothedDb = 0.0f;
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MaybeDuckAudioProcessor)
};
