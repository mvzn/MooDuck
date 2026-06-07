/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "DynamicsCore.h"
#include "CrossoverCore.h"
#include "KorenTriode.h"   // Pultec EQP-1A tube saturation stage (not yet wired up)
#include "PultecCore.h"   // Pultec EQP-1A WDF filter network (Phase 2)
#include "PultecMakeupChain.h"   // EQ + tube saturator in one shared 4x oversampling domain

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
    static constexpr float kLookaheadSteps[]  = { 0.0f, 3.0f, 6.0f, 10.0f, 16.0f, 32.0f, 50.0f };
    static constexpr int   kNumLookaheadSteps = (int) (sizeof(kLookaheadSteps) / sizeof(kLookaheadSteps[0]));

    // "Xtreme Lookahead" switches the lookahead knob to this wide creative range
    // (up to 10 s). Off = the normal kLookaheadSteps above.
    static constexpr float kXtremeLookaheadSteps[]  = { 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f };
    static constexpr int   kNumXtremeLookaheadSteps = (int) (sizeof(kXtremeLookaheadSteps) / sizeof(kXtremeLookaheadSteps[0]));

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
    std::atomic<float>* pXtremeLookahead  = nullptr;
    std::atomic<float>* pLookaheadXtreme  = nullptr;   // the wide-range lookahead value (used when Xtreme is on)
    std::atomic<float>* pXover[StereoMultibandCrossover::numCrossoverPoints] = {};
    std::atomic<float>* pBandScale[StereoMultibandCrossover::numBands] = {};
    std::atomic<float>* pBandState[StereoMultibandCrossover::numBands] = {}; // 0 Normal / 1 Solo / 2 Mute
    std::atomic<float>* pSidechainMode  = nullptr;
    std::atomic<float>* pFftConv        = nullptr;
    std::atomic<float>* pFftConvSize    = nullptr;
    std::atomic<float>* pCrossoverClean = nullptr;

    // Read-only output parameter: GR amount in dB, written from audio thread for host
    // automation recording. Not used to drive any DSP.
    juce::AudioParameterFloat* pGrOut = nullptr;

    // Pultec EQP-1A
    std::atomic<float>* pPultecLfFreq      = nullptr;  // 0-4 → 20/30/40/60/100 Hz
    std::atomic<float>* pPultecLfBoost     = nullptr;  // 0-16 dB
    std::atomic<float>* pPultecLfAtten     = nullptr;  // 0-16 dB
    std::atomic<float>* pPultecHfFreq      = nullptr;  // 0-6 → 3/4/5/8/10/12/16 kHz
    std::atomic<float>* pPultecHfBoost     = nullptr;  // 0-16 dB
    std::atomic<float>* pPultecHfBw        = nullptr;  // 0-1 bandwidth (0=narrow, 1=wide)
    std::atomic<float>* pPultecHfAttenFreq = nullptr;  // 0-2 → 5/10/20 kHz
    std::atomic<float>* pPultecHfAtten     = nullptr;  // 0-14 dB
    std::atomic<float>* pPultecSatEnable   = nullptr;  // bool: tube saturation on/off
    std::atomic<float>* pPultecSatDrive    = nullptr;  // 0.5-4.0: 12AX7 grid drive (1=authentic)
    std::atomic<float>* pPultecSatMix      = nullptr;  // 0-100%: parallel dry/wet
    std::atomic<float>* pPultecSatTrim     = nullptr;  // -12..+12 dB: wet-path output trim
    std::atomic<float>* pPultecBypass      = nullptr;  // bool: EQ passthrough (skips WDF + HF-cut)

    // monitoring
    float getCpuUsagePercent() const noexcept      { return cpuUsagePercent.load(); }
    float getProcessTimeMs() const noexcept        { return processTimeMs.load(); }
    float getGainReductionDb() const noexcept      { return gainReductionDb.load(); }   // negative or zero internally
    float getGainReductionAmountDb() const noexcept{ return gainReductionAmountDb.load(); } // positive for meter
    float getInputLevelDb() const noexcept         { return inputLevelDb.load(); }
    float getSidechainLevelDb() const noexcept     { return sidechainLevelDb.load(); }
    float getOutputLevelDb() const noexcept        { return outputLevelDb.load(); }
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

    PultecEQP1A      pultec;
    KorenTriode12AX7 triode;
    // Runs `pultec` then `triode` in one shared 4x oversampling domain (single
    // up/down round-trip). Declared after both so its reference members bind to
    // already-constructed stages.
    PultecMakeupChain chain { pultec, triode };

    // Lookahead delay line on the main path (sidechain is undelayed → detector sees future)
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> lookaheadDelayL;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> lookaheadDelayR;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> lookaheadDelaySmoothed;
    juce::AudioBuffer<float> lookaheadBufL, lookaheadBufR;
    int prevLookaheadSamples = 0;

    // Lookahead change handling: SMALL changes glide the delay tap smoothly; LARGE
    // jumps (e.g. the Xtreme range, up to 10 s) would sweep the tap across seconds of
    // buffer (warble/time-warp), so they crossfade the output instead — fade out,
    // snap the tap silently, fade back in. Rapid changes keep the output muted until
    // they settle, then snap + fade in once.
    float lookaheadFadeGain   = 1.0f;  // output gain of the lookahead stage during a crossfade
    int   lookaheadFadeState  = 0;     // 0 = idle, 1 = fading out, 2 = fading in
    float lookaheadFadeInc    = 1.0f;  // per-sample fade step (~12 ms)
    int   lookaheadPendingDelay = 0;   // tap target to snap to at the fade bottom (samples)
    int   lookaheadBigJumpThresh = 0;  // samples; changes above this crossfade instead of glide

    // Mode-change click suppression. Switching Wideband/Multiband/Spectral or toggling
    // FFT-Conv swaps the whole processing path (different gain/phase/latency) -> a click.
    // We defer the actual path switch until the output has faded to zero, then fade the
    // new path back in. modeFadeActive* = the path currently being processed (lags the
    // parameter target until the fade bottom). -1 = uninitialised (adopt target, no fade).
    int   modeFadeActiveEffMode = -1;
    bool  modeFadeActiveUseConv = false;
    int   modeFadeActiveSizeIdx = 1;    // FFT-conv size currently being processed (lags
                                        // the param so a size change crossfades, like
                                        // conv on/off — no flush click + no latency jump)
    float modeFadeGain          = 1.0f; // output gain during a mode crossfade
    int   modeFadeState         = 0;    // 0 = idle, 1 = fading out, 2 = fading in
    float modeFadeInc           = 1.0f; // per-sample fade step (~5 ms)
    int   modeFadeHoldSamples   = 0;    // stay muted this many samples before fading in
                                        // (covers the FFT-conv priming latency on entry)

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
    std::unique_ptr<juce::dsp::WindowingFunction<float>> convWinObjs[3]; // sidechain analysis (Hann)
    // Main-audio analysis+synthesis window: periodic SQRT-Hann. Applied on BOTH the
    // analysis (pre-FFT) and synthesis (post-IFFT) sides; √H·√H = Hann, whose 50%-hop
    // COLA = 1, so reconstruction stays unity AND frame edges are tapered (WOLA) to
    // suppress the block-edge discontinuities a per-bin gain mask would otherwise make.
    std::vector<float> convSqrtHann[3];

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

    // Per-bin gain-reduction envelope (linear, mono SC). Smoothed across frames with
    // the attack/release coefficients so the spectral duck has real timing and the
    // per-frame gain can't jump (the "birdies"/musical-noise fix). Index 0..N/2.
    float convBinGainEnv[kMaxConvFftSize / 2 + 1];

    int   prevLatencyReported = 0;
    int   eqTriodeLatency     = 0; // constant Pultec EQ + triode oversampler delay (host-rate samples), set in prepareToPlay
    float convFrameGrDb       = 0.0f; // worst-case dB reduction from last conv frame

    void processConvFrame (int fftSize, int sizeIdx,
                           double thresholdDb, double ratio,
                           double attackMs, double releaseMs, double outGainDb,
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
    std::atomic<float> outputLevelDb { -100.0f };   // post-EQ/saturation output peak (drives Sat glow)
    std::atomic<double> currentSampleRate { 44100.0 };
    std::atomic<int> currentBlockSize { 0 };
    std::atomic<bool> sidechainConnected { false };

    float inputLevelSmoothedDb = -100.0f;
    float sidechainLevelSmoothedDb = -100.0f;
    float outputLevelSmoothedDb = -100.0f;
    float gainReductionSmoothedDb = 0.0f;
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MaybeDuckAudioProcessor)
};
