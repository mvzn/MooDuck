#pragma once
#include <JuceHeader.h>

enum class BiquadFilterType
{
  kLPF, kHPF
};

struct AudioFilterParameters
{
    AudioFilterParameters()= default;
    AudioFilterParameters& operator=(const AudioFilterParameters& params)
    {
        if (this != &params)
        {
          algorithm = params.algorithm;
          fc = params.fc;
          Q = params.Q;
        }
        return *this;
    }
    BiquadFilterType algorithm = BiquadFilterType::kLPF;
    double fc = 700.;
    double Q = 0.70710678118;
};

class FilterBiquad
{
public:
  FilterBiquad() {reset();};
    ~FilterBiquad() = default;
    void setParameters(const AudioFilterParameters& params) {
        m_params = params;
        calculateCoefficients();
        return;
    }

    AudioFilterParameters getParameters() const
    {
        return m_params;
    }

    bool reset()
    {
        state.fill(0.);
        return true;
    }

    double processSample(const double& input)
    {
      return processDF2T(input);
    }
    
    void prepare(double _sampleRate)
    {
      fs = (_sampleRate > 0.) ? _sampleRate : 44100.;
      calculateCoefficients();
      return;
    }
private:
    double processDF2T(double input)
    {
      double output = input * coeffs[0] + state[0];
      state[0] = input * coeffs[1] - output * coeffs[3] + state[1];
      state[1] = input * coeffs[2] - output * coeffs[4];
      return output;
    }

    void calculateCoefficients()
    {
      coeffs.fill(0.0);

      const double fc = juce::jlimit(20.0, 20000.0, m_params.fc);
      const double q = juce::jmax(0.5, m_params.Q);

      double w0 = 2.0 * juce::MathConstants<double>::pi * fc / fs;
      double alpha = sin(w0) / (2.0 * std::max(m_params.Q, 0.5));
      double cos_w0 = cos(w0);
      const double d = 1.0 / q;
      const double beta = 0.5 * ((1.0 - (d / 2.0 * std::sin(w0))) / (1.0 + (d / 2.0 * std::sin(w0))));
      const double g = (0.5 + beta) * cos_w0;

      switch (m_params.algorithm)
      {
        // 2nd order LPF
        case BiquadFilterType::kLPF:
        {
          coeffs[0] = ((0.5 + beta - g ) / 2.0);
          coeffs[1] = (0.5 + beta - g);
          coeffs[2] = coeffs[0];
          coeffs[3] = -2. * g;
          coeffs[4] = 2. * beta;
          break;
        }
        // 2nd order HPF
        case BiquadFilterType::kHPF:
        {
          coeffs[0] = ((0.5 + beta + g ) / 2.0);
          coeffs[1] = -(0.5 + beta + g);
          coeffs[2] = coeffs[0];
          coeffs[3] = (-2. * g);
          coeffs[4] = (2. * beta);
          break;
        }
      }
    }

    AudioFilterParameters m_params;
    std::array<double, 5> coeffs {};
    std::array<double, 2> state {};
    double fs = 44100.0;
    size_t order = 0;
};

struct ThreeBandSample
{
    float low  = 0.0f;
    float mid  = 0.0f;
    float high = 0.0f;
};

class ThreeBandSplitterMono
{
public:
    void prepare(double sampleRate)
    {
        fs = sampleRate;

        for (auto* f : getAllFilters())
            f->prepare(sampleRate);

        updateFilters();
        reset();
    }

    void reset()
    {
        for (auto* f : getAllFilters())
            f->reset();
    }

    void setCutoffs(float lowMidHz, float midHighHz)
    {
        const float minGapHz = 20.0f;
        xoLowMid  = juce::jlimit(40.0f, 18000.0f, lowMidHz);
        xoMidHigh = juce::jlimit(xoLowMid + minGapHz, 20000.0f, midHighHz);
        updateFilters();
    }

    ThreeBandSample processSample(float x)
    {
        ThreeBandSample out {};

        double low = x;
        low = lp1a.processSample(low);
        low = lp1b.processSample(low);

        double mid = x;
        mid = hp1a.processSample(mid);
        mid = hp1b.processSample(mid);
        mid = lp2a.processSample(mid);
        mid = lp2b.processSample(mid);

        double high = x;
        high = hp2a.processSample(high);
        high = hp2b.processSample(high);

        out.low  = (float) low;
        out.mid  = (float) mid;
        out.high = (float) high;
        return out;
    }

private:
    void updateFilters()
    {
        AudioFilterParameters lpLowMid;
        lpLowMid.algorithm = BiquadFilterType::kLPF;
        lpLowMid.fc = xoLowMid;
        lpLowMid.Q = Q;

        AudioFilterParameters hpLowMid = lpLowMid;
        hpLowMid.algorithm = BiquadFilterType::kHPF;

        AudioFilterParameters lpMidHigh;
        lpMidHigh.algorithm = BiquadFilterType::kLPF;
        lpMidHigh.fc = xoMidHigh;
        lpMidHigh.Q = Q;

        AudioFilterParameters hpMidHigh = lpMidHigh;
        hpMidHigh.algorithm = BiquadFilterType::kHPF;

        lp1a.setParameters(lpLowMid);
        lp1b.setParameters(lpLowMid);
        hp1a.setParameters(hpLowMid);
        hp1b.setParameters(hpLowMid);

        lp2a.setParameters(lpMidHigh);
        lp2b.setParameters(lpMidHigh);
        hp2a.setParameters(hpMidHigh);
        hp2b.setParameters(hpMidHigh);
    }

    std::array<FilterBiquad*, 8> getAllFilters()
    {
        return { &lp1a, &lp1b, &hp1a, &hp1b, &lp2a, &lp2b, &hp2a, &hp2b };
    }

    double fs = 44100.0;
    double Q = 0.70710678118;
    float xoLowMid = 200.0f;
    float xoMidHigh = 2500.0f;

    FilterBiquad lp1a, lp1b;
    FilterBiquad hp1a, hp1b;
    FilterBiquad lp2a, lp2b;
    FilterBiquad hp2a, hp2b;
};

struct ThreeBandStereoSample
{
    ThreeBandSample left;
    ThreeBandSample right;
};

class ThreeBandCrossover
{
public:
    void prepare(double sampleRate)
    {
        audioL.prepare(sampleRate);
        audioR.prepare(sampleRate);
        sidechainL.prepare(sampleRate);
        sidechainR.prepare(sampleRate);
    }

    void reset()
    {
        audioL.reset();
        audioR.reset();
        sidechainL.reset();
        sidechainR.reset();
    }

    void setCutoffs(float lowMidHz, float midHighHz)
    {
        audioL.setCutoffs(lowMidHz, midHighHz);
        audioR.setCutoffs(lowMidHz, midHighHz);
        sidechainL.setCutoffs(lowMidHz, midHighHz);
        sidechainR.setCutoffs(lowMidHz, midHighHz);
    }

    ThreeBandStereoSample splitAudio(float inL, float inR)
    {
        return { audioL.processSample(inL), audioR.processSample(inR) };
    }

    ThreeBandStereoSample splitSidechain(float inL, float inR)
    {
        return { sidechainL.processSample(inL), sidechainR.processSample(inR) };
    }

private:
    ThreeBandSplitterMono audioL, audioR;
    ThreeBandSplitterMono sidechainL, sidechainR;
};