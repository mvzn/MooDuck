#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <algorithm>
#include <array>
#include <vector>

/**
 * KorenTriode12AX7
 *
 * Norman Koren (1996) plate-current triode model tuned to 12AX7 curve-fit
 * parameters.  Operates as a soft-knee saturator with predominantly 2nd-harmonic
 * character, matching the behaviour of a class-A common-cathode stage (e.g.
 * the Pultec EQP-1A makeup gain triode).
 *
 * Phase 8 (Tier 1 + Tier 2) — character + alias-fix upgrade:
 *  - Hard rail clip at +/-1 replaced with std::tanh (Tier 1).
 *  - Asymmetric grid-current soft-limit applied on the positive half cycle
 *    of the input (models grid-current onset; primary source of even-order
 *    distortion).
 *  - Per-sample anti-derivative anti-aliasing (ADAA, first-order Parker /
 *    Esqueda / Bilbao DAFx 2016) around the full Koren + tanh + asymmetric
 *    pipeline, using a precomputed LUT of the antiderivative F(x) sampled
 *    over the input range.
 *  - Output DC blocker (one-pole high-pass at 5 Hz) on the downsampled
 *    output, preventing accumulation of DC offset that an asymmetric
 *    nonlinearity introduces.
 *
 * Internally uses JUCE's polyphase IIR 4x oversampler so the non-linearity
 * runs at >=176.4 kHz; ADAA on top suppresses aliasing further.
 *
 * Usage:
 *   KorenTriode12AX7 triode;
 *   triode.prepare(sampleRate, maxBlockSize);
 *   triode.setDrive(1.0f);   // 1.0 = authentic operating point
 *   triode.setEnabled(true);
 *   triode.processBlock(leftData, rightData, numSamples);
 */
class KorenTriode12AX7
{
public:
    KorenTriode12AX7()  = default;
    ~KorenTriode12AX7() = default;

    // Non-copyable — the oversampler holds internal state
    KorenTriode12AX7(const KorenTriode12AX7&)            = delete;
    KorenTriode12AX7& operator=(const KorenTriode12AX7&) = delete;

    // -------------------------------------------------------------------------
    // Call once before processing begins (or whenever the stream format changes)
    // -------------------------------------------------------------------------
    void prepare(double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        oversampler.initProcessing(static_cast<size_t>(maxBlockSize));
        oversampler.reset();
        computeLinearGain();
        buildAntiderivativeLut();

        // Constant-latency support: size the dry-path delay ring to the
        // oversampler's group delay (reported at host rate). When the stage is
        // disabled we run the dry signal through this ring so its latency
        // EXACTLY matches the enabled path (4.432673 -> lround = 4 samples at
        // host rate), making the stage's reported latency independent of the
        // enable toggle. This lets PluginProcessor fold the value into the
        // latency reported once at prepareToPlay, with no mid-stream
        // setLatencySamples churn (which many
        // hosts treat as a buffer-flush -> click) when the sat toggle changes.
        // This is the SINGLE source of truth for the triode's reported latency.
        latencySamples = static_cast<int>(std::lround(oversampler.getLatencyInSamples()));
        latencySamples = std::max(0, latencySamples);
        for (auto& v : dryDelay)
            v.assign(static_cast<size_t>(latencySamples), 0.0f);
        dryDelayPos = 0;

        // DC blocker coefficient: alpha = exp(-2 pi fc / fs_host).
        // fc = 5 Hz; fs_host is the (un-oversampled) host rate.
        constexpr double kDcCutoffHz = 5.0;
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        dcAlpha = std::exp(-twoPi * kDcCutoffHz / std::max(sampleRate, 1.0));

        // Reset per-channel state
        for (auto& s : adaaPrevIn) s = 0.0;
        for (auto& s : dcPrevIn)   s = 0.0f;
        for (auto& s : dcPrevOut)  s = 0.0f;
    }

    // -------------------------------------------------------------------------
    // drive: 1.0 = nominal (authentic EQP-1A operating point)
    //        2.0 = pushed hard; scales the grid excursion, deepening saturation
    // -------------------------------------------------------------------------
    void setDrive(float newDrive) noexcept
    {
        drive = newDrive;
        computeLinearGain();
        // LUT depends on drive (via the saturation pipeline); rebuild.
        buildAntiderivativeLut();
    }

    // -------------------------------------------------------------------------
    // enabled: false → processBlock is a zero-copy passthrough
    // -------------------------------------------------------------------------
    void setEnabled(bool shouldBeEnabled) noexcept
    {
        enabled = shouldBeEnabled;
    }

    // -------------------------------------------------------------------------
    // Constant processing latency (HOST-rate samples) introduced by the 4x
    // oversampler group delay. Single source of truth: the same value sizes the
    // dry-path delay ring used when disabled, so the stage's latency is the same
    // whether enabled or not. Valid only after prepare(); 0 before.
    // -------------------------------------------------------------------------
    int getLatencySamples() const noexcept { return latencySamples; }

    // -------------------------------------------------------------------------
    // Process stereo block in-place.
    // When enabled, upsamples 4x, runs Koren + asymmetric + ADAA per sample,
    // downsamples, applies DC blocker.
    // When disabled, output == input (no oversampling overhead).
    // -------------------------------------------------------------------------
    void processBlock(float* left, float* right, int numSamples)
    {
        if (!enabled)
        {
            // Disabled: pass the dry signal through a latency-matched delay so
            // the stage's latency is CONSTANT (== enabled-path group delay),
            // independent of the enable toggle. The ring is fed input[n] and
            // emits input[n-L] in place. With L == 0 this is a no-op.
            captureDelayedDry(left, right, numSamples);
            return;
        }

        // Keep the dry-delay ring primed with this block's input BEFORE the
        // oversampler consumes it in place, so that if the stage is later
        // disabled the ring already holds valid recent history (no zero-burst).
        advanceDryRing(left, right, numSamples);

        // Wrap raw pointers in a non-owning AudioBlock
        float* channels[2] = { left, right };
        juce::dsp::AudioBlock<float> block(channels, 2, static_cast<size_t>(numSamples));

        // Upsample to 4x rate
        auto upBlock = oversampler.processSamplesUp(block);

        // Process the upsampled block sample-by-sample per channel.
        // ADAA wraps the full pipeline at the upsampled rate; the previous-input
        // state is per channel and carried across blocks for continuity.
        const size_t upSamples = upBlock.getNumSamples();
        for (size_t ch = 0; ch < 2; ++ch)
        {
            float* data = upBlock.getChannelPointer(ch);
            double prev = adaaPrevIn[ch];
            for (size_t i = 0; i < upSamples; ++i)
            {
                const double x = static_cast<double>(data[i]);
                data[i] = static_cast<float>(processSampleAdaa(x, prev));
                prev = x;
            }
            adaaPrevIn[ch] = prev;
        }

        // Downsample back to original rate (writes into block)
        oversampler.processSamplesDown(block);

        // Output DC blocker — one-pole HP @ 5 Hz, per channel.
        for (int ch = 0; ch < 2; ++ch)
        {
            float* data = channels[ch];
            float xPrev = dcPrevIn[ch];
            float yPrev = dcPrevOut[ch];
            const float a = static_cast<float>(dcAlpha);
            for (int i = 0; i < numSamples; ++i)
            {
                const float x = data[i];
                const float y = a * (yPrev + x - xPrev);
                data[i] = y;
                xPrev = x;
                yPrev = y;
            }
            dcPrevIn[ch]  = xPrev;
            dcPrevOut[ch] = yPrev;
        }
    }

private:
    // -------------------------------------------------------------------------
    // Dry-path latency compensation (used when disabled).
    // captureDelayedDry: push input[n], emit input[n-L] in place (disabled path).
    // advanceDryRing:    push input[n], discard output (keeps history fresh when
    //                    enabled so a later disable resumes without a zero-burst).
    // Both share a single ring cursor and step both channels per iteration, so
    // the two channels stay phase-locked. `right` may alias `left` (mono
    // fallback); the per-sample read-then-write order keeps that case correct.
    // -------------------------------------------------------------------------
    void captureDelayedDry(float* left, float* right, int numSamples) noexcept
    {
        if (latencySamples == 0) return;   // no oversampler delay to match

        const bool mono = (right == left);
        int pos = dryDelayPos;
        for (int n = 0; n < numSamples; ++n)
        {
            const size_t p = static_cast<size_t>(pos);
            const float inL = left[n];
            const float inR = mono ? inL : right[n];
            left[n] = dryDelay[0][p];
            if (! mono) right[n] = dryDelay[1][p];
            dryDelay[0][p] = inL;
            dryDelay[1][p] = inR;
            pos = (pos + 1) % latencySamples;
        }
        dryDelayPos = pos;
    }

    void advanceDryRing(const float* left, const float* right, int numSamples) noexcept
    {
        if (latencySamples == 0) return;

        const bool mono = (right == left);
        int pos = dryDelayPos;
        for (int n = 0; n < numSamples; ++n)
        {
            const size_t p = static_cast<size_t>(pos);
            dryDelay[0][p] = left[n];
            dryDelay[1][p] = mono ? left[n] : right[n];
            pos = (pos + 1) % latencySamples;
        }
        dryDelayPos = pos;
    }

    // -------------------------------------------------------------------------
    // Koren 12AX7 model constants (from published curve fits, Norman Koren 1996)
    // -------------------------------------------------------------------------
    static constexpr double MU           = 100.0;    // amplification factor
    static constexpr double EX           = 1.4;      // plate-current exponent
    static constexpr double KG1          = 1060.0;   // grid-plate parameter
    static constexpr double KP           = 600.0;    // plate parameter
    static constexpr double KVB          = 300.0;    // knee voltage parameter (unused in simplified model)
    static constexpr double V_GRID_BIAS  = -2.0;     // class-A grid bias (V)
    static constexpr double V_PLATE_BIAS = 200.0;    // class-A plate voltage (V)
    static constexpr double R_LOAD       = 100000.0; // plate load resistance (Ohm, 100 kOhm)
    static constexpr double GRID_SCALE   = 1.0;      // input +-1 -> grid +-1 V excursion
    static constexpr double DIFF_EPS     = 0.001;    // finite-difference step for linearGain (V)

    // Asymmetric grid-current soft-limit threshold on the input side (volts).
    // Positive-half-cycle excursions above this are tanh-compressed; the
    // negative half cycle passes through unchanged (tube cuts off softly via
    // the Koren curve itself).
    static constexpr double V_GRID_LIMIT = 0.5;

    // ADAA LUT range (in input-sample units, after drive scaling is folded in)
    static constexpr double LUT_X_MIN = -3.0;
    static constexpr double LUT_X_MAX =  3.0;
    static constexpr int    LUT_SIZE  = 1024;
    // Difference threshold below which we fall back to direct f((x_n+x_{n-1})/2)
    // evaluation. Two reasons to fall back:
    //   1) Numerical: |dx| -> 0 makes the divide ill-conditioned (per the
    //      Parker-Esqueda-Bilbao paper).
    //   2) LUT-resolution: when |dx| is smaller than the LUT step, the
    //      antiderivative is essentially piecewise-linear over the interval
    //      and the difference quotient degenerates to a staircase reading.
    //      Direct evaluation gives a much cleaner small-signal response, and
    //      slow inputs don't need anti-aliasing anyway (their bandwidth is
    //      already well below Nyquist).
    static constexpr double ADAA_NUMERIC_EPS = 1.0e-6;

    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------
    // 2 channels, factor 2^2 = 4x, polyphase IIR half-band, maximum quality
    juce::dsp::Oversampling<float> oversampler {
        2,
        2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true
    };

    bool   enabled    = true;
    float  drive      = 1.0f;
    double sampleRate = 44100.0;
    double linearGain = 1.0;   // small-signal normalisation, computed in prepare()

    // ADAA: F lookup table over [LUT_X_MIN, LUT_X_MAX], previous-input state per channel.
    std::array<double, LUT_SIZE> fLut {};
    std::array<double, 2>        adaaPrevIn {};

    // DC blocker state per channel.
    double dcAlpha = 1.0;
    std::array<float, 2> dcPrevIn  {};
    std::array<float, 2> dcPrevOut {};

    // Dry-path latency-comp ring (used when disabled) — constant 4-sample delay
    // matching the oversampler group delay. Sized in prepare(). latencySamples is
    // the single source of truth for the stage's reported host latency.
    int                latencySamples = 0;
    int                dryDelayPos    = 0;
    std::array<std::vector<float>, 2> dryDelay {};

    // -------------------------------------------------------------------------
    // Koren plate-current equation (amps).
    // -------------------------------------------------------------------------
    double computePlateCurrentKoren(double vGrid, double vPlate) const noexcept
    {
        if (vPlate <= 0.0)
            return 0.0;

        const double E1 = (vPlate / KP) *
                          std::log(1.0 + std::exp(KP * (1.0 / MU + vGrid / vPlate)));
        return std::pow(std::max(0.0, E1), EX) / KG1;
    }

    // -------------------------------------------------------------------------
    // Small-signal transconductance / gain, computed at the bias point with the
    // current drive setting. (We compute the symmetric difference of the full
    // pipeline so it correctly reflects the asymmetric grid-current limit at
    // the operating point too. At V_GRID_BIAS = -2 V the limit is inactive,
    // so the result is dominated by the Koren curve.)
    // -------------------------------------------------------------------------
    void computeLinearGain() noexcept
    {
        const double d    = static_cast<double>(drive);
        const double step = GRID_SCALE * std::max(d, 1.0e-6) * DIFF_EPS;

        const double ipPlus  = computePlateCurrentKoren(V_GRID_BIAS + step, V_PLATE_BIAS);
        const double ipMinus = computePlateCurrentKoren(V_GRID_BIAS - step, V_PLATE_BIAS);

        const double gm   = (ipPlus - ipMinus) / (2.0 * step);
        const double gain = gm * R_LOAD;

        linearGain = (gain > 1.0e-12) ? gain : 1.0;
    }

    // -------------------------------------------------------------------------
    // Apply asymmetric grid-current soft-limit on the positive half cycle.
    // vGridIn is the (drive-scaled) input voltage that would be summed onto
    // V_GRID_BIAS to form the instantaneous grid voltage.
    // -------------------------------------------------------------------------
    static double applyGridCurrentLimit(double vGridIn) noexcept
    {
        if (vGridIn > 0.0)
            return V_GRID_LIMIT * std::tanh(vGridIn / V_GRID_LIMIT);
        return vGridIn;
    }

    // -------------------------------------------------------------------------
    // Map raw input sample x in audio units to output sample, applying:
    //   1) drive scaling -> grid voltage excursion
    //   2) asymmetric grid-current soft-limit on the positive half cycle
    //   3) Koren plate-current delta -> output voltage
    //   4) normalisation by small-signal gain
    //   5) tanh soft-clip at the output (replacing the old hard rail)
    // This is the nonlinearity f(x) that ADAA wraps.
    // -------------------------------------------------------------------------
    double koreanSaturatorF(double x) const noexcept
    {
        const double d         = static_cast<double>(drive);
        double vGridIn         = x * GRID_SCALE * d;
        vGridIn                = applyGridCurrentLimit(vGridIn);

        const double ipBias    = computePlateCurrentKoren(V_GRID_BIAS, V_PLATE_BIAS);
        const double ipSignal  = computePlateCurrentKoren(V_GRID_BIAS + vGridIn, V_PLATE_BIAS);
        const double deltaIp   = ipSignal - ipBias;

        // Inverting common-cathode stage; normalise by small-signal gain.
        const double vOut = (-deltaIp * R_LOAD) / linearGain;

        // Soft clip (replaces previous hard rail at +-1) — smooth, no fizz.
        return std::tanh(vOut);
    }

    // -------------------------------------------------------------------------
    // Build the antiderivative LUT F(x) where F(LUT_X_MIN) = 0 and
    //   F(x) = integral_{LUT_X_MIN}^{x} f(u) du
    // Computed by forward trapezoidal accumulation on LUT_SIZE evenly-spaced
    // sample points across [LUT_X_MIN, LUT_X_MAX].
    // -------------------------------------------------------------------------
    void buildAntiderivativeLut() noexcept
    {
        const double dx = (LUT_X_MAX - LUT_X_MIN) / static_cast<double>(LUT_SIZE - 1);
        double prevF    = koreanSaturatorF(LUT_X_MIN);
        fLut[0]         = 0.0;
        double acc      = 0.0;
        for (int i = 1; i < LUT_SIZE; ++i)
        {
            const double x = LUT_X_MIN + static_cast<double>(i) * dx;
            const double f = koreanSaturatorF(x);
            // Trapezoidal rule
            acc += 0.5 * (prevF + f) * dx;
            fLut[i] = acc;
            prevF   = f;
        }
    }

    // -------------------------------------------------------------------------
    // Linear-interpolated lookup of F(v); v is clamped to the LUT domain.
    // -------------------------------------------------------------------------
    double lookupF(double v) const noexcept
    {
        const double vClamped = std::min(std::max(v, LUT_X_MIN), LUT_X_MAX);
        const double dx       = (LUT_X_MAX - LUT_X_MIN) / static_cast<double>(LUT_SIZE - 1);
        const double idx      = (vClamped - LUT_X_MIN) / dx;
        const int    i0       = static_cast<int>(std::floor(idx));
        const int    i1       = std::min(i0 + 1, LUT_SIZE - 1);
        const double frac     = idx - static_cast<double>(i0);
        return fLut[i0] + frac * (fLut[i1] - fLut[i0]);
    }

    // -------------------------------------------------------------------------
    // First-order ADAA (Parker-Esqueda-Bilbao 2016):
    //   y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1])
    // with fallback to f((x[n] + x[n-1]) / 2) when |dx| is too small.
    // -------------------------------------------------------------------------
    double processSampleAdaa(double xn, double xPrev) const noexcept
    {
        const double dx = xn - xPrev;
        const double absDx = std::abs(dx);
        // Combined threshold: bail on numerically tiny dx (paper requirement),
        // AND on dx smaller than the LUT step (where ADAA would degenerate to
        // a piecewise-linear staircase from the LUT itself).
        const double lutDx = (LUT_X_MAX - LUT_X_MIN) / static_cast<double>(LUT_SIZE - 1);
        if (absDx < std::max(ADAA_NUMERIC_EPS, lutDx))
            return koreanSaturatorF(0.5 * (xn + xPrev));
        return (lookupF(xn) - lookupF(xPrev)) / dx;
    }
};
