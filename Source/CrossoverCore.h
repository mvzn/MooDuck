#pragma once
#include <JuceHeader.h>
#include "FXFunctions.h"

// ============================================================
//  VA Filter  (Zavalishin SVF)
// ============================================================

enum class vaFilterAlgorithm
{
    kLPF1, kHPF1, kAPF1,
    kSVF_LP, kSVF_HP, kSVF_BP, kSVF_BS
};

struct ZVAFilterParameters
{
    ZVAFilterParameters& operator=(const ZVAFilterParameters& p)
    {
        if (this == &p) return *this;
        filterAlgorithm       = p.filterAlgorithm;
        fc                    = p.fc;
        Q                     = p.Q;
        filterOutputGain_dB   = p.filterOutputGain_dB;
        enableGainComp        = p.enableGainComp;
        matchAnalogNyquistLPF = p.matchAnalogNyquistLPF;
        selfOscillate         = p.selfOscillate;
        enableNLP             = p.enableNLP;
        return *this;
    }

    vaFilterAlgorithm filterAlgorithm = vaFilterAlgorithm::kSVF_LP;
    double fc                   = 1000.0;
    double Q                    = 0.7071067812;
    double filterOutputGain_dB  = 0.0;
    bool   enableGainComp       = false;
    bool   matchAnalogNyquistLPF = false;
    bool   selfOscillate        = false;
    bool   enableNLP            = false;
};

class ZVAFilter
{
public:
    ZVAFilter()  = default;
    ~ZVAFilter() = default;

    bool reset(double _sampleRate)
    {
        sampleRate      = _sampleRate;
        integrator_z[0] = 0.0;
        integrator_z[1] = 0.0;
        return true;
    }

    ZVAFilterParameters getParameters() const { return params; }

    void setParameters(const ZVAFilterParameters& p)
    {
        bool needsRecalc = (p.fc                    != params.fc                    ||
                            p.Q                     != params.Q                     ||
                            p.selfOscillate         != params.selfOscillate         ||
                            p.matchAnalogNyquistLPF != params.matchAnalogNyquistLPF);
        params = p;
        if (needsRecalc) calculateFilterCoeffs();
    }

    double processAudioSample(double xn)
    {
        const auto alg = params.filterAlgorithm;

        if (params.enableGainComp)
        {
            double peak_dB = dB_peak_gain_for_q(params.Q);
            if (peak_dB > 0.0)
                xn *= dB2raw(-peak_dB / 2.0);
        }

        // --- 1st-order path
        if (alg == vaFilterAlgorithm::kLPF1 ||
            alg == vaFilterAlgorithm::kHPF1 ||
            alg == vaFilterAlgorithm::kAPF1)
        {
            double vn  = (xn - integrator_z[0]) * alpha;
            double lpf = vn + integrator_z[0];
            integrator_z[0] = vn + lpf;

            double hpf = xn  - lpf;
            double apf = lpf - hpf;

            if (alg == vaFilterAlgorithm::kLPF1)
                return params.matchAnalogNyquistLPF ? lpf + alpha * hpf : lpf;
            if (alg == vaFilterAlgorithm::kHPF1) return hpf;
            return apf;
        }

        // --- SVF path
        double hpf = alpha0 * (xn - rho * integrator_z[0] - integrator_z[1]);
        double bpf = alpha  * hpf + integrator_z[0];
        if (params.enableNLP) bpf = softclip_waveshaper(bpf, 1.0);
        double lpf = alpha  * bpf + integrator_z[1];
        double bsf = hpf + lpf;
        double sn  = integrator_z[0];

        integrator_z[0] = alpha * hpf + bpf;
        integrator_z[1] = alpha * bpf + lpf;

        const double gain = dB2raw(params.filterOutputGain_dB);

        if (alg == vaFilterAlgorithm::kSVF_LP)
        {
            if (params.matchAnalogNyquistLPF) lpf += analogMatchSigma * sn;
            return gain * lpf;
        }
        if (alg == vaFilterAlgorithm::kSVF_HP) return gain * hpf;
        if (alg == vaFilterAlgorithm::kSVF_BP) return gain * bpf;
        if (alg == vaFilterAlgorithm::kSVF_BS) return gain * bsf;

        return gain * lpf;
    }

    void calculateFilterCoeffs()
    {
        const double fc  = params.fc;
        const double Q   = params.Q;
        const auto   alg = params.filterAlgorithm;

        const double wd = juce::MathConstants<double>::pi * fc;
        const double T  = 1.0 / sampleRate;
        const double wa = (2.0 / T) * std::tan(wd * T / 2.0);
        const double g  = wa * T / 2.0;

        if (alg == vaFilterAlgorithm::kLPF1 ||
            alg == vaFilterAlgorithm::kHPF1 ||
            alg == vaFilterAlgorithm::kAPF1)
        {
            alpha = g / (1.0 + g);
        }
        else
        {
            const double R = params.selfOscillate ? 0.0 : 1.0 / (2.0 * Q);
            alpha0 = 1.0 / (1.0 + 2.0 * R * g + g * g);
            alpha  = g;
            rho    = 2.0 * R + g;

            const double f_o = (sampleRate / 2.0) / fc;
            analogMatchSigma = 1.0 / (alpha * f_o * f_o);
        }
    }

    void   setBeta(double b)  { beta = b; }
    double getBeta()  const   { return beta; }

protected:
    ZVAFilterParameters params;
    double sampleRate = 44100.0;

    double integrator_z[2]  = { 0.0, 0.0 };
    double alpha0           = 0.0;
    double alpha            = 0.0;
    double rho              = 0.0;
    double beta             = 0.0;
    double analogMatchSigma = 0.0;
};


// ============================================================
//  LR4 Crossover Stage
//  One split point: two cascaded SVF LP stages + two cascaded SVF HP stages.
//  Q is fixed at 1/sqrt(2) to guarantee flat power-summing (Linkwitz-Riley condition).
//  Cutoff is smoothed per-block so APVTS automation doesn't click.
// ============================================================

class LR4CrossoverStage
{
public:
    static constexpr double kLR4_Q = 0.7071067812;

    // smoothingTimeSec: ramp time for fc automation (default 50 ms)
    void prepare(double sr, double smoothingTimeSec = 0.05)
    {
        sampleRate = sr;
        for (auto& f : lpFilters) f.reset(sr);
        for (auto& f : hpFilters) f.reset(sr);
        smoothedFc.reset(sr, smoothingTimeSec);
        smoothedFc.setCurrentAndTargetValue(currentFc);
        applyFrequency(currentFc);
    }

    void reset()
    {
        for (auto& f : lpFilters) f.reset(sampleRate);
        for (auto& f : hpFilters) f.reset(sampleRate);
    }

    // Call from audio thread (e.g. reading APVTS raw value at start of processBlock).
    void setCrossoverFrequency(double fc)
    {
        smoothedFc.setTargetValue(juce::jlimit(20.0, 20000.0, fc));
    }

    void setCrossoverFrequencyImmediate(double fc)
    {
        fc = juce::jlimit(20.0, 20000.0, fc);
        smoothedFc.setCurrentAndTargetValue(fc);
        currentFc = fc;
        applyFrequency(fc);
    }

    // Advance smoother by one block and update coefficients if fc changed.
    // Must be called once per block before looping over samples.
    void advanceSmootherBlock(int numSamples)
    {
        const double newFc = smoothedFc.skip(numSamples);
        if (newFc != currentFc)
        {
            currentFc = newFc;
            applyFrequency(newFc);
        }
    }

    // Returns { lpOut, hpOut }
    std::pair<double, double> processSample(double xn)
    {
        const double lp = lpFilters[1].processAudioSample(lpFilters[0].processAudioSample(xn));
        const double hp = hpFilters[1].processAudioSample(hpFilters[0].processAudioSample(xn));
        return { lp, hp };
    }

private:
    void applyFrequency(double fc)
    {
        ZVAFilterParameters lp, hp;
        lp.filterAlgorithm = vaFilterAlgorithm::kSVF_LP;
        lp.fc = fc;  lp.Q = kLR4_Q;
        hp.filterAlgorithm = vaFilterAlgorithm::kSVF_HP;
        hp.fc = fc;  hp.Q = kLR4_Q;
        for (auto& f : lpFilters) f.setParameters(lp);
        for (auto& f : hpFilters) f.setParameters(hp);
    }

    ZVAFilter lpFilters[2];
    ZVAFilter hpFilters[2];

    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear> smoothedFc;
    double currentFc  = 1000.0;
    double sampleRate = 44100.0;
};


// ============================================================
//  Mono 5-Band LR4 Crossover
//  Cascaded topology: input → stage0 → {band0, residual}
//                              → stage1 → {band1, residual}
//                              → stage2 → {band2, residual}
//                              → stage3 → {band3, band4}
//  O(n) filter passes per sample; each sample traverses at most 4 stages.
// ============================================================

class MonoMultibandCrossover
{
public:
    static constexpr int numBands           = 5;
    static constexpr int numCrossoverPoints = 4;

    static constexpr std::array<double, numCrossoverPoints> kDefaultFreqs
        = { 120.0, 400.0, 2000.0, 8000.0 };

    void prepare(double sr)
    {
        for (int i = 0; i < numCrossoverPoints; ++i)
        {
            stages[i].prepare(sr);
            stages[i].setCrossoverFrequencyImmediate(kDefaultFreqs[i]);
        }
    }

    void reset()
    {
        for (auto& s : stages) s.reset();
    }

    // Call from audio thread only (not thread-safe).
    void setCrossoverFrequency(int index, double fc)
    {
        jassert(index >= 0 && index < numCrossoverPoints);
        stages[index].setCrossoverFrequency(fc);
    }

    // Sets frequency with no smoothing ramp — use in prepareToPlay only.
    void setCrossoverFrequencyImmediate(int index, double fc)
    {
        jassert(index >= 0 && index < numCrossoverPoints);
        stages[index].setCrossoverFrequencyImmediate(fc);
    }

    // Must be called once per block before processSample loop.
    void advanceSmootherBlock(int numSamples)
    {
        for (auto& s : stages) s.advanceSmootherBlock(numSamples);
    }

    std::array<double, numBands> processSample(double xn)
    {
        std::array<double, numBands> bands{};
        double residual = xn;

        for (int i = 0; i < numCrossoverPoints; ++i)
        {
            auto [lp, hp] = stages[i].processSample(residual);
            bands[i] = lp;
            residual  = hp;
        }
        bands[numBands - 1] = residual;
        return bands;
    }

    // bandOutputs[b] must point to a buffer of at least numSamples floats.
    void processBlock(const float* input, float* bandOutputs[numBands], int numSamples)
    {
        advanceSmootherBlock(numSamples);
        for (int n = 0; n < numSamples; ++n)
        {
            const auto bands = processSample(static_cast<double>(input[n]));
            for (int b = 0; b < numBands; ++b)
                bandOutputs[b][n] = static_cast<float>(bands[b]);
        }
    }

private:
    LR4CrossoverStage stages[numCrossoverPoints];
};


// ============================================================
//  Stereo 5-Band Crossover  (JUCE-ready)
//
//  Usage in PluginProcessor:
//    prepare:  crossover.prepare(sampleRate);  // call directly from prepareToPlay
//    params:   crossover.setCrossoverFrequency(0, freq_hz);  // audio thread
//    process:  crossover.processBlock(mainL, mainR, scL, scR,
//                                     mainBandL, mainBandR,
//                                     scBandL,   scBandR,
//                                     numSamples);
//
//  Output buffers (mainBandL[b], mainBandR[b], scBandL[b], scBandR[b])
//  are owned and allocated by PluginProcessor in prepareToPlay.
//  No heap allocation occurs on the audio thread.
// ============================================================

class StereoMultibandCrossover
{
public:
    static constexpr int numBands           = MonoMultibandCrossover::numBands;
    static constexpr int numCrossoverPoints = MonoMultibandCrossover::numCrossoverPoints;

    void prepare(double sampleRate)
    {
        for (auto& xover : crossovers)
            xover.prepare(sampleRate);
    }

    void reset()
    {
        for (auto& xover : crossovers) xover.reset();
    }

    // Applies to all four internal crossovers. Call from audio thread.
    void setCrossoverFrequency(int index, double fc)
    {
        for (auto& xover : crossovers)
            xover.setCrossoverFrequency(index, fc);
    }

    // Sets frequency with no smoothing ramp — use in prepareToPlay only.
    void setCrossoverFrequencyImmediate(int index, double fc)
    {
        for (auto& xover : crossovers)
            xover.setCrossoverFrequencyImmediate(index, fc);
    }

    // All pointer arrays must be pre-allocated to at least numSamples floats each.
    void processBlock(
        const float* mainL, const float* mainR,
        const float* scL,   const float* scR,
        float* mainBandL[numBands], float* mainBandR[numBands],
        float* scBandL[numBands],   float* scBandR[numBands],
        int numSamples)
    {
        crossovers[0].processBlock(mainL, mainBandL, numSamples);
        crossovers[1].processBlock(mainR, mainBandR, numSamples);
        crossovers[2].processBlock(scL,   scBandL,   numSamples);
        crossovers[3].processBlock(scR,   scBandR,   numSamples);
    }

private:
    // [0]=mainL, [1]=mainR, [2]=scL, [3]=scR
    MonoMultibandCrossover crossovers[4];
};
