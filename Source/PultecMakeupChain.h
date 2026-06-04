#pragma once

/*  PultecMakeupChain.h
    Coordinator that runs the Pultec EQ (PultecEQP1A) and the tube saturator
    (KorenTriode12AX7) inside a SINGLE shared 4x oversampling domain.

    Replaces the previous arrangement where each stage owned its own oversampler
    (two up/down round-trips => roughly double the OS filter cost, a stacked
    group delay of 4 + 4 = 8 host samples, and an intermediate down->up that
    re-aliased the signal between the two stages). Here the signal is upsampled
    once, run through EQ then tube at 4x, and downsampled once: ~half the OS
    cost, ~4-sample latency, and no inter-stage re-aliasing.

    The two sub-stages keep their own standalone processBlock()/oversampler (used
    by the unit tests and as a mono reference); this coordinator instead calls
    their pure oversampled-domain per-sample methods.

    Bypass / enable: each stage's toggle is independent. The EQ-bypass and
    saturation-disable crossfades are performed here at the upsampled rate by
    blending each stage's wet output against its (time-aligned) input — so no
    per-stage dry-delay ring is needed (the only latency is the single shared
    oversampler group delay, applied once to the final output). When BOTH the EQ
    and the saturator are settled-bypassed, a fast path emits the input delayed
    by exactly the reported latency (matching the OS group delay), so toggling
    never causes a time jump and the reported latency stays constant.

    Audio-thread safe: the oversampler, delay ring and mono scratch are all
    pre-allocated in prepare(); processBlock() does not allocate.
*/

#include <JuceHeader.h>
#include "PultecCore.h"
#include "KorenTriode.h"

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

class PultecMakeupChain
{
public:
    PultecMakeupChain (PultecEQP1A& eqRef, KorenTriode12AX7& triodeRef) noexcept
        : eq (eqRef), triode (triodeRef) {}

    PultecMakeupChain (const PultecMakeupChain&)            = delete;
    PultecMakeupChain& operator= (const PultecMakeupChain&) = delete;

    void prepare (double hostSampleRate, int maxBlockSize)
    {
        os.initProcessing (static_cast<size_t> (juce::jmax (0, maxBlockSize)));
        os.reset();

        latencySamples = static_cast<int> (std::lround (os.getLatencyInSamples()));
        latencySamples = juce::jmax (0, latencySamples);

        dryL.assign (static_cast<size_t> (latencySamples), 0.0f);
        dryR.assign (static_cast<size_t> (latencySamples), 0.0f);
        dryPos = 0;

        monoScratch.assign (static_cast<size_t> (juce::jmax (0, maxBlockSize)), 0.0f);

        // Crossfade mixes settle to the stages' current targets (1 = dry).
        eqMix  = eq.getBypass()      ? 1.0f : 0.0f;
        satMix = triode.getEnabled() ? (1.0f - satMixAmount) : 1.0f;

        // ~10 ms ramp, advanced once per UPSAMPLED sample (4x host rate) so the
        // wall-clock fade matches the per-stage 10 ms fades it replaces.
        const double upSr = hostSampleRate * 4.0;
        mixInc = 1.0f / juce::jmax (1.0f, static_cast<float> (0.010 * upSr));

        // Output DC blocker (one-pole HP @ 5 Hz) at host rate — matches the
        // triode's previous post-downsample blocker (asymmetric saturation can
        // introduce DC that would otherwise drift downstream IIR state).
        constexpr double kDcCutoffHz = 5.0;
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        dcAlpha   = std::exp (-twoPi * kDcCutoffHz / juce::jmax (hostSampleRate, 1.0));
        dcPrevIn  = { { 0.0f, 0.0f } };
        dcPrevOut = { { 0.0f, 0.0f } };
    }

    /** Constant processing latency (HOST-rate samples) = the single shared
     *  oversampler group delay. Valid after prepare(); reported once to the host. */
    int getLatencySamples() const noexcept { return latencySamples; }

    /** P6 — parallel dry/wet for the saturation: 1 = full tube (wet), 0 = clean
     *  post-EQ (dry). Blended in the oversampled domain against the time-aligned
     *  dry. Independent of the sat enable toggle (disabled => always full dry). */
    void setSatMix (float wet01) noexcept { satMixAmount = juce::jlimit (0.0f, 1.0f, wet01); }

    /** P6 — output trim applied to the wet/tube path (linear gain) for A/B
     *  loudness matching. */
    void setSatTrimGain (double g) noexcept { satTrimGain = g; }

    /** Process a stereo block in place. Pass right == nullptr for the mono
     *  fallback (left is processed; the second OS channel uses internal scratch). */
    void processBlock (float* left, float* right, int numSamples)
    {
        const bool mono = (right == nullptr);
        float* r = mono ? monoScratch.data() : right;
        if (mono)
            std::memcpy (r, left, sizeof (float) * static_cast<size_t> (numSamples));

        const float eqTarget  = eq.getBypass() ? 1.0f : 0.0f;       // 1 = dry
        // Sat dry amount: disabled => full dry; enabled => 1 - mix (mix=1 -> full wet).
        const float satTarget = triode.getEnabled() ? (1.0f - satMixAmount) : 1.0f;

        const bool eqSettledDry  = (eqMix  == eqTarget  && eqTarget  == 1.0f);
        const bool satSettledDry = (satMix == satTarget && satTarget == 1.0f);

        // Fully bypassed + settled: emit the input delayed by the reported
        // latency (no oversampling). Keeps the reported latency constant.
        if (eqSettledDry && satSettledDry)
        {
            runDryDelay (left, r, numSamples);
            return;
        }

        // Keep the dry ring primed so a later full-bypass starts time-aligned.
        primeDryRing (left, r, numSamples);

        float* ch[2] = { left, r };
        juce::dsp::AudioBlock<float> block (ch, 2, static_cast<size_t> (numSamples));
        auto up = os.processSamplesUp (block);

        const size_t m  = up.getNumSamples();
        float* uL = up.getChannelPointer (0);
        float* uR = up.getChannelPointer (1);

        for (size_t i = 0; i < m; ++i)
        {
            const double xL = uL[i];
            const double xR = uR[i];

            // EQ stage (+ bypass crossfade, time-aligned at the upsampled rate)
            double eL = xL, eR = xR;
            eq.processOversampledSample (eL, eR);
            eqMix = approach (eqMix, eqTarget);
            const double oeL = (1.0 - eqMix) * eL + eqMix * xL;
            const double oeR = (1.0 - eqMix) * eR + eqMix * xR;

            // Tube saturator (+ output trim on the wet path) (+ mix/disable crossfade)
            const double sL = triode.processOversampledSample (0, oeL) * satTrimGain;
            const double sR = triode.processOversampledSample (1, oeR) * satTrimGain;
            satMix = approach (satMix, satTarget);
            uL[i] = static_cast<float> ((1.0 - satMix) * sL + satMix * oeL);
            uR[i] = static_cast<float> ((1.0 - satMix) * sR + satMix * oeR);
        }

        os.processSamplesDown (block);

        // DC blocker @ host rate (channel 0 always; channel 1 only if stereo).
        dcBlockChannel (left, numSamples, 0);
        if (! mono)
            dcBlockChannel (right, numSamples, 1);
    }

private:
    inline float approach (float mix, float target) const noexcept
    {
        return (target > mix) ? juce::jmin (target, mix + mixInc)
                              : juce::jmax (target, mix - mixInc);
    }

    // Push input[n], emit input[n-L] in place on both channels.
    void runDryDelay (float* l, float* r, int n) noexcept
    {
        if (latencySamples == 0) return;   // no OS delay to match -> passthrough
        int pos = dryPos;
        for (int i = 0; i < n; ++i)
        {
            const size_t p   = static_cast<size_t> (pos);
            const float  inL = l[i];
            const float  inR = r[i];
            l[i] = dryL[p];
            r[i] = dryR[p];
            dryL[p] = inL;
            dryR[p] = inR;
            pos = (pos + 1) % latencySamples;
        }
        dryPos = pos;
    }

    // Push input[n] into the ring (keep history), discard output (wet active).
    void primeDryRing (const float* l, const float* r, int n) noexcept
    {
        if (latencySamples == 0) return;
        int pos = dryPos;
        for (int i = 0; i < n; ++i)
        {
            const size_t p = static_cast<size_t> (pos);
            dryL[p] = l[i];
            dryR[p] = r[i];
            pos = (pos + 1) % latencySamples;
        }
        dryPos = pos;
    }

    void dcBlockChannel (float* data, int n, int ch) noexcept
    {
        const size_t c = static_cast<size_t> (ch);
        const float  a = static_cast<float> (dcAlpha);
        float xPrev = dcPrevIn[c];
        float yPrev = dcPrevOut[c];
        for (int i = 0; i < n; ++i)
        {
            const float x = data[i];
            const float y = a * (yPrev + x - xPrev);
            data[i] = y;
            xPrev = x;
            yPrev = y;
        }
        dcPrevIn[c]  = xPrev;
        dcPrevOut[c] = yPrev;
    }

    PultecEQP1A&      eq;
    KorenTriode12AX7& triode;

    // Single shared 4x oversampler: 2 channels, factor 2^2, polyphase IIR
    // half-band, maximum quality (same config the two stages used individually).
    juce::dsp::Oversampling<float> os {
        2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true
    };

    int latencySamples = 0;

    // Single dry-path delay ring (fully-bypassed fast path) + mono scratch.
    int                dryPos = 0;
    std::vector<float> dryL, dryR;
    std::vector<float> monoScratch;

    // Crossfade mixes (1 = dry). Ramped at the upsampled rate.
    float eqMix  = 0.0f;
    float satMix = 0.0f;
    float mixInc = 1.0f;

    // P6 controls: parallel dry/wet amount (1 = full wet) + wet-path output trim.
    float  satMixAmount = 1.0f;
    double satTrimGain  = 1.0;

    // Output DC blocker (host rate).
    double dcAlpha = 1.0;
    std::array<float, 2> dcPrevIn  { { 0.0f, 0.0f } };
    std::array<float, 2> dcPrevOut { { 0.0f, 0.0f } };
};
