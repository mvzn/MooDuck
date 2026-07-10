#pragma once
#include <JuceHeader.h>

const double TLD_AUDIO_ENVELOPE_ANALOG_TC = -0.99967234081320612357829304641019;
inline double peak_gain_for_q(double Q)
{
    if (Q <= 0.707)
        return 1.0;

    const double denom = Q * Q - 0.25;
    if (denom <= 0.0)
        return 1.0;

    return (Q * Q) / std::sqrt(denom);
}
inline double raw2dB(double raw) {return (raw > 0.0) ? 20.0 * log10(raw) : -std::numeric_limits<double>::infinity();}
inline double dB2raw(double dB) {return pow(10, dB / 20.0);}
inline double dB_peak_gain_for_q(double Q) {return raw2dB(peak_gain_for_q(Q));}
inline double sgn(double xn) {return (xn > 0.0) - (xn < 0);}
inline double softclip_waveshaper(double xn, double saturation) {return sgn(xn) * (1.0 - std::exp(-fabs(saturation*xn)));}

