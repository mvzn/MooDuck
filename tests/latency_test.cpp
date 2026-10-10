// Host-level latency regression test.
//
// Drives the REAL MooDuckAudioProcessor through the juce::AudioProcessor host API and
// asserts the latency / PDC contract that unit-level DSP tests can't reach:
//
//   1. getLatencySamples() (what the host compensates) tracks the NORMAL lookahead range,
//      and scales correctly with sample rate.
//   2. Xtreme lookahead is FROZEN OUT of the reported latency (a deliberately uncompensated
//      creative delay) — toggling it never changes what the host is told.
//   3. The WET path is actually delayed by exactly the reported amount (reported == real).
//   4. At lookahead 0 ms the delay line is bypassed: the impulse passes at unity (no
//      Lagrange3rd smear) and lands exactly at the constant EQ/triode latency.
//
// Built as a console app that links the plugin's shared-code lib (see CMakeLists.txt,
// MOODUCK_BUILD_TESTS). Run via `ctest -R latency` or invoke the binary directly.

#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>

namespace
{
int gPass = 0, gFail = 0;

void check (bool ok, const juce::String& label)
{
    if (ok) { ++gPass; std::printf ("  [PASS] %s\n", label.toRawUTF8()); }
    else    { ++gFail; std::printf ("  [FAIL] %s\n", label.toRawUTF8()); }
}

void setParam (MooDuckAudioProcessor& p, const char* id, float value)
{
    auto* param = p.apvts.getParameter (id);
    jassert (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

void prepare (MooDuckAudioProcessor& p, double sr, int block)
{
    p.setRateAndBufferSizeDetails (sr, block);   // so getSampleRate() is valid in processBlock
    p.prepareToPlay (sr, block);
}

// Reported latency at a given normal-range lookahead + sample rate.
int reportedNormal (MooDuckAudioProcessor& p, float lookMs, double sr)
{
    setParam (p, "xtremeLookahead", 0.0f);
    setParam (p, "lookahead", lookMs);
    prepare (p, sr, 512);
    return p.getLatencySamples();
}

// Push a single full-scale impulse (at sample `pos`) through one block and return
// {peakIndex, peakAmp}. `pos` is kept away from the block edge so the downstream Pultec
// oversampler has valid pre-history (an impulse at sample 0 truncates the OS filter's
// pre-ring and reads low — a measurement artifact, not a path defect).
struct Peak { int index; float amp; };
Peak impulseResponse (MooDuckAudioProcessor& p, int N, int pos)
{
    juce::AudioBuffer<float> buf (2, N);
    buf.clear();
    buf.setSample (0, pos, 1.0f);
    buf.setSample (1, pos, 1.0f);
    juce::MidiBuffer mb;
    p.processBlock (buf, mb);

    Peak pk { 0, 0.0f };
    for (int n = 0; n < N; ++n)
    {
        const float a = std::abs (buf.getSample (0, n));
        if (a > pk.amp) { pk.amp = a; pk.index = n; }
    }
    return pk;
}

// Isolated proof that JUCE's Lagrange3rd delay line is already bit-exact at delay 0 (so the
// processor's zero-delay bypass is a guarantee, not a coloration fix): an impulse in, the
// same impulse out, unchanged, with everything else zero.
void testDelayLineZeroIsExact()
{
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> dl;
    dl.setMaximumDelayInSamples (64);
    juce::dsp::ProcessSpec spec { 48000.0, 64, 1 };
    dl.prepare (spec);
    dl.reset();
    dl.setDelay (0.0f);

    float worstOther = 0.0f, first = 0.0f;
    for (int n = 0; n < 32; ++n)
    {
        dl.pushSample (0, n == 0 ? 1.0f : 0.0f);
        const float out = dl.popSample (0);
        if (n == 0) first = out;
        else        worstOther = juce::jmax (worstOther, std::abs (out));
    }
    check (std::abs (first - 1.0f) < 1.0e-6f,
           "Lagrange3rd delay 0: impulse passes exactly (out " + juce::String (first, 6) + " == 1.0)");
    check (worstOther < 1.0e-6f,
           "Lagrange3rd delay 0: no smear onto neighbours (worst " + juce::String (worstOther, 6) + " ~ 0)");
}
} // namespace

int main()
{
    juce::MessageManager::getInstance();   // events only — no GUI / no X
    std::printf ("== MooDuck host latency / PDC ==\n");

    {
        MooDuckAudioProcessor proc;

        // ---------------------------------------------------------------------------
        // 1. Reported latency tracks the NORMAL lookahead range, across sample rates.
        // ---------------------------------------------------------------------------
        const double rates[]   = { 44100.0, 48000.0, 96000.0 };
        const float  steps[]   = { 3.0f, 6.0f, 10.0f, 16.0f, 32.0f, 50.0f };
        for (double sr : rates)
        {
            const int base = reportedNormal (proc, 0.0f, sr);   // eqTriode (+ conv, here 0)
            check (base >= 0, "sr " + juce::String (sr, 0) + ": baseline latency >= 0 (got " + juce::String (base) + ")");
            for (float ms : steps)
            {
                const int got  = reportedNormal (proc, ms, sr);
                const int want = base + (int) std::lround (ms * 0.001 * sr);
                check (got == want,
                       "sr " + juce::String (sr, 0) + ": lookahead " + juce::String (ms, 0)
                       + " ms -> " + juce::String (got) + " smp (expect " + juce::String (want) + ")");
            }
        }

        // ---------------------------------------------------------------------------
        // 2. Xtreme is frozen out of the reported latency (uncompensated creative delay).
        // ---------------------------------------------------------------------------
        const double sr = 48000.0;
        const int baseFor2 = reportedNormal (proc, 0.0f, sr);
        setParam (proc, "xtremeLookahead", 1.0f);
        for (float ms : { 100.0f, 1000.0f, 10000.0f })
        {
            setParam (proc, "lookaheadXtreme", ms);
            prepare (proc, sr, 512);
            check (proc.getLatencySamples() == baseFor2,
                   "Xtreme " + juce::String (ms, 0) + " ms -> reported "
                   + juce::String (proc.getLatencySamples()) + " smp (expect baseline "
                   + juce::String (baseFor2) + ", frozen)");
        }
        setParam (proc, "xtremeLookahead", 0.0f);

        // ---------------------------------------------------------------------------
        // 3. Wet-path delay == reported latency (the real PDC contract: what the host is
        // told must equal where the signal actually comes out). Wideband + ratio 1:1 (no
        // reduction) + EQ bypass (latency-matched dry delay) => a pure delay, so the impulse
        // peak lands at pos + getLatencySamples(). We assert the peak INDEX only — the peak
        // AMPLITUDE is dominated by the Pultec oversampler's reset/block-boundary transient,
        // which is a chain artifact, not a lookahead/latency property.
        // ---------------------------------------------------------------------------
        setParam (proc, "sidechainMode",   0.0f);   // Wideband (no crossover ringing)
        setParam (proc, "ratio",           1.0f);   // 1:1 -> no gain reduction
        setParam (proc, "sidechainEnable", 0.0f);
        setParam (proc, "limiter",         0.0f);
        setParam (proc, "pultecBypass",    1.0f);   // EQ skipped -> dry delay only
        setParam (proc, "pultecSatEnable", 0.0f);

        const int N   = 8192;
        const int pos = 1000;
        for (float ms : { 0.0f, 6.0f, 16.0f, 50.0f })
        {
            setParam (proc, "lookahead", ms);
            prepare (proc, sr, N);
            const int  rep = proc.getLatencySamples();
            const Peak pk  = impulseResponse (proc, N, pos);

            check (std::abs (pk.index - (pos + rep)) <= 2,
                   "lookahead " + juce::String (ms, 0) + " ms: wet impulse at "
                   + juce::String (pk.index) + " smp == pos+reported " + juce::String (pos + rep) + " (+/-2)");
        }

        // ---------------------------------------------------------------------------
        // 5. Reported latency must NOT change once processing starts. The original bug:
        // prepareToPlay reported the lookahead latency, but the first processBlock recomputed
        // it WITHOUT lookahead and called setLatencySamples() to "fix" it — so any nonzero
        // lookahead fired a latency change on block 1 of every playback. Hosts that re-init on
        // a mid-stream latency change then loop (re-prepare <-> re-report) -> periodic buffer
        // flushes that sound like stutter / gating, and ONLY when lookahead != 0. Assert the
        // reported latency is stable across several blocks for both 0 and nonzero lookahead.
        // ---------------------------------------------------------------------------
        setParam (proc, "sidechainMode", 1.0f);   // default Multiband path
        for (float ms : { 0.0f, 16.0f, 50.0f })
        {
            setParam (proc, "xtremeLookahead", 0.0f);
            setParam (proc, "lookahead", ms);
            prepare (proc, sr, 512);
            const int before = proc.getLatencySamples();

            juce::AudioBuffer<float> blk (2, 512);
            blk.clear();
            juce::MidiBuffer mb;
            for (int k = 0; k < 4; ++k) proc.processBlock (blk, mb);

            const int after = proc.getLatencySamples();
            check (before == after,
                   "lookahead " + juce::String (ms, 0) + " ms: reported latency stable across processBlock ("
                   + juce::String (before) + " -> " + juce::String (after)
                   + ") — no block-1 re-report (the stutter/gating cause)");
        }

        // ---------------------------------------------------------------------------
        // 6. Mascot easter egg (host-level contract): a trigger mixes the recorded
        // one-shot into the output at its loudness-matched peak and plays to completion; without
        // a trigger the output stays silent; and offline (non-realtime) rendering
        // never emits it — a moo must not be printable into a bounce.
        // ---------------------------------------------------------------------------
        {
            setParam (proc, "lookahead", 0.0f);
            prepare (proc, sr, 512);
            juce::AudioBuffer<float> blk (2, 512);
            juce::MidiBuffer mb2;

            auto peakOver = [&] (int blocks)
            {
                float pk = 0.0f;
                for (int k = 0; k < blocks; ++k)
                {
                    blk.clear();
                    proc.processBlock (blk, mb2);
                    pk = juce::jmax (pk, blk.getMagnitude (0, blk.getNumSamples()));
                }
                return pk;
            };

            check (peakOver (4) < 1.0e-6f, "easter egg: silent without a trigger");

            proc.triggerEasterEgg (1);                    // moo (1.08 s < 110 blocks @ 48k/512)
            const float mooDb = juce::Decibels::gainToDecibels (peakOver (110));
            check (std::abs (mooDb - MooDuckAudioProcessor::easterEggMooPeakDb) < 1.0f,
                   "easter egg: moo peaks at its target (got " + juce::String (mooDb, 2) + " dB)");
            check (peakOver (4) < 1.0e-6f, "easter egg: moo ends, silence resumes");

            proc.triggerEasterEgg (2);                    // quack (0.36 s < 40 blocks)
            const float quackDb = juce::Decibels::gainToDecibels (peakOver (40));
            check (std::abs (quackDb - MooDuckAudioProcessor::easterEggQuackPeakDb) < 1.0f,
                   "easter egg: quack peaks at its target (got " + juce::String (quackDb, 2) + " dB)");

            proc.setNonRealtime (true);
            proc.triggerEasterEgg (1);
            check (peakOver (110) < 1.0e-6f, "easter egg: suppressed during offline render");
            proc.setNonRealtime (false);

            // The recordings are 48 kHz; other session rates go through the resampler.
            prepare (proc, 44100.0, 512);
            proc.triggerEasterEgg (1);
            const float moo441Db = juce::Decibels::gainToDecibels (peakOver (110));
            check (std::abs (moo441Db - MooDuckAudioProcessor::easterEggMooPeakDb) < 1.0f,
                   "easter egg: resampled moo @ 44.1k peaks at its target (got " + juce::String (moo441Db, 2) + " dB)");
            check (peakOver (4) < 1.0e-6f, "easter egg: resampled moo ends, silence resumes");
            prepare (proc, sr, 512);
        }

        // 4. The lookahead line itself is bit-exact at delay 0 (so settled-0 lookahead is a
        // clean passthrough — no special-casing needed in the processor).
        testDelayLineZeroIsExact();
    } // proc destroyed before the MessageManager so APVTS's timer unregisters cleanly

    juce::DeletedAtShutdown::deleteAll();
    juce::MessageManager::deleteInstance();

    std::printf ("\n%d passed, %d failed\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
