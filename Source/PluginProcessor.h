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

    juce::AudioProcessorValueTreeState apvts;

    // Cached raw parameter pointers — stable for plugin lifetime, safe on audio thread
    std::atomic<float>* pThreshold = nullptr;
    std::atomic<float>* pRatio     = nullptr;
    std::atomic<float>* pAttack    = nullptr;
    std::atomic<float>* pRelease   = nullptr;
    std::atomic<float>* pKnee      = nullptr;
    std::atomic<float>* pOutput    = nullptr;
    std::atomic<float>* pScEnable  = nullptr;
    std::atomic<float>* pSoftKnee  = nullptr;
    std::atomic<float>* pLimiter   = nullptr;
    std::atomic<float>* pXover[StereoMultibandCrossover::numCrossoverPoints] = {};

    // monitoring
    float getCpuUsagePercent() const noexcept      { return cpuUsagePercent.load(); }
    float getProcessTimeMs() const noexcept        { return processTimeMs.load(); }
    float getGainReductionDb() const noexcept      { return gainReductionDb.load(); }   // negative or zero internally
    float getGainReductionAmountDb() const noexcept{ return gainReductionAmountDb.load(); } // positive for meter
    float getInputLevelDb() const noexcept         { return inputLevelDb.load(); }
    float getAboveThresholdDb() const noexcept     { return aboveThresholdDb.load(); }
    double getCurrentSampleRateHz() const noexcept { return currentSampleRate.load(); }
    int getCurrentBlockSize() const noexcept       { return currentBlockSize.load(); }
    bool isSidechainConnected() const noexcept     { return sidechainConnected.load(); }

    static constexpr int fftOrder = 11;
    static constexpr int fftSize  = 1 << fftOrder;

    void pushNextSampleIntoFifo(float sample, float* fifo, int& index, float* fftBuffer, std::atomic<bool>& readyFlag);

    bool getInputFftData(float* dest);
    bool getSidechainFftData(float* dest);
    bool getOutputFftData(float* dest);
private:
    static constexpr int numBands = StereoMultibandCrossover::numBands;

    DynamicsProcessor bandProcessorsL[numBands];
    DynamicsProcessor bandProcessorsR[numBands];
    StereoMultibandCrossover crossover;

    juce::AudioBuffer<float> mainBandBufL, mainBandBufR;
    juce::AudioBuffer<float> scBandBufL,   scBandBufR;

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
    std::atomic<float> aboveThresholdDb { 0.0f };
    std::atomic<double> currentSampleRate { 44100.0 };
    std::atomic<int> currentBlockSize { 0 };
    std::atomic<bool> sidechainConnected { false };

    float inputFifo[fftSize] = {};
    float sidechainFifo[fftSize] = {};
    float outputFifo[fftSize] = {};

    float inputFftBuffer[fftSize] = {};
    float sidechainFftBuffer[fftSize] = {};
    float outputFftBuffer[fftSize] = {};

    int inputFifoIndex = 0;
    int sidechainFifoIndex = 0;
    int outputFifoIndex = 0;

    std::atomic<bool> inputBlockReady { false };
    std::atomic<bool> sidechainBlockReady { false };
    std::atomic<bool> outputBlockReady { false };

    juce::CriticalSection fftDataLock;

    float inputLevelSmoothedDb = -100.0f;
    float gainReductionSmoothedDb = 0.0f;
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MaybeDuckAudioProcessor)
};
