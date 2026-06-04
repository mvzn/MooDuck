#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <algorithm>
#include <array>
#include <vector>

/**
 * KorenTriode12AX7  (P2: now the two-tube EQP-1A makeup amp)
 *
 * Despite the legacy class name, this models the full EQP-1A makeup-gain
 * amplifier as a Norman-Koren (1996) hybrid of TWO cascaded tube stages plus a
 * global negative-feedback loop, tuned to the SPICE oracle in
 * eq-research/spice/eqp1a_makeup_amp.cir (targets in saturation-v12-spec.md §9.1):
 *
 *   x ─► [− β·y(n-1)] ─► 12AX7 (asymmetric, even-rich) ─► 12AU7 (odd-symmetric,
 *                                                          push-pull) ─► makeup ─► y
 *                  ▲                                                         │
 *                  └──────────────── global NFB (1-sample delay @ 4×) ◄──────┘
 *
 * Stage 1 — 12AX7 single-ended common-cathode (Koren MU≈100). Asymmetric grid-
 *   current soft-limit on the positive half cycle ⇒ predominantly 2nd-harmonic
 *   "warmth" (the schematic's single-ended front stage; oracle H2 ≫ H3).
 * Stage 2 — 12AU7 push-pull output (Koren MU≈21.5), modelled as the ODD-SYMMETRIC
 *   differential plate current Ip(bias+v) − Ip(bias−v). Odd symmetry cancels the
 *   stage's own even harmonics (the push-pull effect), so the front-stage evens
 *   survive while the output stage holds odds down — matching the oracle.
 * NFB — global loop with a 1-sample delay at the oversampled rate (the implicit
 *   solve is deferred to the v1.3 MNA). Depth calibrated to the oracle's
 *   ≈ +1.94 dB (open- vs closed-loop), which linearises small signal and shapes
 *   the harmonic onset; the makeup scale restores unity small-signal gain so the
 *   sat-enable crossfade stays level-matched.
 *
 * Anti-aliasing: each static tube curve is wrapped in first-order ADAA
 * (Parker/Esqueda/Bilbao DAFx 2016) via its own antiderivative LUT, and the whole
 * amp runs at ≥176.4 kHz inside the shared 4× oversampling domain
 * (PultecMakeupChain). A 5 Hz output DC blocker mops up the asymmetric stage's DC.
 *
 * Usage:
 *   KorenTriode12AX7 amp;
 *   amp.prepare(sampleRate, maxBlockSize);
 *   amp.setDrive(1.0f);   // 1.0 = authentic operating point
 *   amp.setEnabled(true);
 *   amp.processBlock(leftData, rightData, numSamples);
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

        computeStageNorms();
        buildLuts();

        // Global NFB depth from the oracle (+1.94 dB open- vs closed-loop):
        //   1 + β = 10^(depthDb/20).  y small-signal restored to unity by makeup.
        nfbBeta   = std::pow(10.0, kNfbDepthDb / 20.0) - 1.0;
        nfbMakeup = 1.0 + nfbBeta;

        // P3 dynamic cathode bias one-pole coefficient at the OVERSAMPLED rate
        // (the per-sample path runs at 4x host). tau = Rk*Ck.
        const double tauCathode = kCathodeRk * kCathodeCk;
        const double osRate     = std::max(sampleRate, 1.0) * 4.0;
        cathodeCoeff = 1.0 - std::exp(-1.0 / (tauCathode * osRate));

        // Constant-latency support: size the dry-path delay ring to the
        // oversampler's group delay (reported at host rate). When the stage is
        // disabled we run the dry signal through this ring so its latency
        // EXACTLY matches the enabled path, making the stage's reported latency
        // independent of the enable toggle. SINGLE source of truth for latency.
        latencySamples = static_cast<int>(std::lround(oversampler.getLatencyInSamples()));
        latencySamples = std::max(0, latencySamples);
        for (auto& v : dryDelay)
            v.assign(static_cast<size_t>(latencySamples), 0.0f);
        dryDelayPos = 0;

        // DC blocker coefficient: alpha = exp(-2 pi fc / fs_host), fc = 5 Hz.
        constexpr double kDcCutoffHz = 5.0;
        const double twoPi = 2.0 * juce::MathConstants<double>::pi;
        dcAlpha = std::exp(-twoPi * kDcCutoffHz / std::max(sampleRate, 1.0));

        // Reset per-channel state (both stages + NFB + DC blocker).
        for (auto& s : s1PrevIn)    s = 0.0;
        for (auto& s : s2PrevIn)    s = 0.0;
        for (auto& s : nfbPrevOut)  s = 0.0;
        for (auto& s : cathodeEnv)  s = 0.0;
        for (auto& s : dcPrevIn)   s = 0.0f;
        for (auto& s : dcPrevOut)  s = 0.0f;
    }

    // -------------------------------------------------------------------------
    // drive: 1.0 = nominal (authentic EQP-1A operating point)
    //        2.0 = pushed hard; scales the 12AX7 grid excursion, deepening
    //        saturation. As both stages compress the NFB loop gain falls, so
    //        distortion rises faster than linearly with drive (as in the real amp).
    // -------------------------------------------------------------------------
    void setDrive(float newDrive) noexcept
    {
        drive = newDrive;
        computeStageNorms();   // stage-1 normalisation tracks drive
        buildLuts();           // both LUTs depend on the saturation pipeline
    }

    // -------------------------------------------------------------------------
    // enabled: false → processBlock passes a latency-matched dry signal.
    // -------------------------------------------------------------------------
    void setEnabled(bool shouldBeEnabled) noexcept { enabled = shouldBeEnabled; }
    bool getEnabled() const noexcept { return enabled; }

    // -------------------------------------------------------------------------
    // Constant processing latency (HOST-rate samples) from the 4× oversampler.
    // -------------------------------------------------------------------------
    int getLatencySamples() const noexcept { return latencySamples; }

    // -------------------------------------------------------------------------
    // Process stereo block in-place (standalone path: own oversampler; used by
    // the unit tests and as a mono reference). When disabled, emits a
    // latency-matched dry signal so reported latency is constant.
    // -------------------------------------------------------------------------
    void processBlock(float* left, float* right, int numSamples)
    {
        if (!enabled)
        {
            captureDelayedDry(left, right, numSamples);
            return;
        }

        advanceDryRing(left, right, numSamples);

        float* channels[2] = { left, right };
        juce::dsp::AudioBlock<float> block(channels, 2, static_cast<size_t>(numSamples));

        auto upBlock = oversampler.processSamplesUp(block);

        const size_t upSamples = upBlock.getNumSamples();
        for (size_t ch = 0; ch < 2; ++ch)
        {
            float* data = upBlock.getChannelPointer(ch);
            for (size_t i = 0; i < upSamples; ++i)
                data[i] = static_cast<float>(processOversampledSample(static_cast<int>(ch),
                                                                      static_cast<double>(data[i])));
        }

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

    // -------------------------------------------------------------------------
    // Pure per-sample amp at the (already-upsampled) rate, for the shared
    // oversampling domain (PultecMakeupChain). Runs the full two-stage + NFB
    // pipeline, carrying per-channel stage/feedback state, WITHOUT this stage's
    // own oversampler, enable gate, DC blocker, or crossfade — the caller owns
    // those. The NFB uses a 1-sample delay at THIS (oversampled) rate.
    // -------------------------------------------------------------------------
    inline double processOversampledSample(int ch, double x) noexcept
    {
        const size_t c = static_cast<size_t>(ch);

        // Global negative feedback: subtract the (1-sample-delayed) output.
        const double xin = x - nfbBeta * nfbPrevOut[c];

        // Stage 1 — 12AX7 (asymmetric, even-rich), ADAA-wrapped.
        const double a = adaaStep(xin, s1PrevIn[c], fLut1,
                                  [this](double u) noexcept { return stage1F(u); });
        s1PrevIn[c] = xin;

        // P3 — dynamic cathode bias on the 12AU7 stage. Track a slow rectified
        // envelope of the stage drive (proxy for the rising average cathode
        // current) and pull a time-varying pre-gain g<=1 around the fixed curve:
        // sustained drive => bias toward cutoff => gain droops over tau, then
        // recovers (the "breathing"). g multiplies the curve INPUT, so the stage-2
        // antiderivative LUT stays valid (it is still the same static curve).
        cathodeEnv[c] += (std::abs(a) - cathodeEnv[c]) * cathodeCoeff;
        const double g       = 1.0 / (1.0 + kCathodeBiasDepth * cathodeEnv[c]);
        const double aBiased = g * a;

        // Stage 2 — 12AU7 push-pull (odd-symmetric), ADAA-wrapped.
        const double b = adaaStep(aBiased, s2PrevIn[c], fLut2,
                                  [this](double u) noexcept { return stage2F(u); });
        s2PrevIn[c] = aBiased;

        // Feedback taps the stage-2 output (pre-makeup); makeup restores unity.
        nfbPrevOut[c] = b;
        return nfbMakeup * b;
    }

private:
    // -------------------------------------------------------------------------
    // Dry-path latency compensation (used when disabled). See P1 commit for the
    // constant-latency rationale.
    // -------------------------------------------------------------------------
    void captureDelayedDry(float* left, float* right, int numSamples) noexcept
    {
        if (latencySamples == 0) return;
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
    // Koren plate-current equation (arbitrary units), parameterised per tube.
    // Simplified form (no KVB term), matching the original 12AX7 model.
    // -------------------------------------------------------------------------
    static double korenIp(double vGrid, double vPlate,
                          double mu, double ex, double kg1, double kp) noexcept
    {
        if (vPlate <= 0.0)
            return 0.0;
        const double E1 = (vPlate / kp) *
                          std::log(1.0 + std::exp(kp * (1.0 / mu + vGrid / vPlate)));
        return std::pow(std::max(0.0, E1), ex) / kg1;
    }

    // ---- Stage 1: 12AX7 single-ended common-cathode (asymmetric) ----
    static constexpr double MU1  = 100.0, EX1 = 1.4, KG1_1 = 1060.0, KP1 = 600.0;
    static constexpr double V_GRID_BIAS_1  = -2.0;
    static constexpr double V_PLATE_BIAS_1 = 200.0;
    static constexpr double R_LOAD_1       = 100000.0;
    static constexpr double GRID_SCALE_1   = 1.0;     // input +-1 -> +-1 V grid excursion
    // Asymmetric grid-current soft-limit (V). HIGHER = the positive half expands
    // further (Koren grows) before capping, vs the negative half compressing into
    // cutoff -> stronger expand/compress asymmetry -> more 2nd harmonic. Kept so
    // max grid (bias + limit) stays below ~0 V (no runaway grid-current spike).
    static constexpr double V_GRID_LIMIT   = 1.5;

    // ---- Stage 2: 12AU7 push-pull output (odd-symmetric) ----
    static constexpr double MU2  = 21.5, EX2 = 1.3, KG1_2 = 1180.0, KP2 = 84.0;
    static constexpr double V_GRID_BIAS_2  = -8.0;
    static constexpr double V_PLATE_BIAS_2 = 200.0;
    // How hard the 12AU7 is driven by the stage-1 output (sets odd-harmonic
    // onset). Tuned so H3 stays well below H2 at hard drive (oracle: H3 ~ -27 dB).
    static constexpr double GRID_SCALE_2   = 0.8;

    // ---- Global NFB ----
    static constexpr double kNfbDepthDb = 1.94;       // oracle open- vs closed-loop

    // ---- P3: dynamic cathode bias (12AU7 self-bias, the "breathing") ----
    // The 12AU7 output cathodes are 4700 ohm || 50 uF (schematic): a bypassed
    // cathode whose cap voltage tracks the slow AVERAGE cathode current, so under
    // sustained drive the bias drifts toward cutoff and the stage gain droops with
    // tau = Rk*Ck, then recovers — level-dependent compression / low-end bloom.
    // (The 12AX7 front cathode is UNbypassed, so it has no such time constant.)
    // Modelled as a slow rectified envelope of the output-stage drive that pulls a
    // time-varying pre-gain around the FIXED stage-2 curve (keeps the ADAA LUT
    // valid). kCathodeBiasDepth sets how far the bias moves (≈ -1.5 dB at full
    // drive); intentionally gentle — this is "movement", not a compressor.
    static constexpr double kCathodeRk        = 4700.0;
    static constexpr double kCathodeCk        = 50.0e-6;     // -> tau = 0.235 s
    static constexpr double kCathodeBiasDepth = 0.18;

    static constexpr double DIFF_EPS = 0.001;         // finite-diff step for slope norm

    // ADAA LUT range (input-sample units, drive folded in)
    static constexpr double LUT_X_MIN = -3.0;
    static constexpr double LUT_X_MAX =  3.0;
    static constexpr int    LUT_SIZE  = 1024;
    static constexpr double ADAA_NUMERIC_EPS = 1.0e-6;

    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------
    juce::dsp::Oversampling<float> oversampler {
        2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true
    };

    bool   enabled    = true;
    float  drive      = 1.0f;
    double sampleRate = 44100.0;

    double stage1Norm = 1.0;   // small-signal normaliser for stage 1 (tracks drive)
    double stage2Norm = 1.0;   // small-signal normaliser for stage 2

    double nfbBeta   = 0.0;    // global feedback coefficient
    double nfbMakeup = 1.0;    // = 1 + nfbBeta (restores unity small-signal gain)

    // P3 dynamic cathode bias: slow rectified envelope per channel + its one-pole
    // coefficient (tau = Rk*Ck, evaluated at the OVERSAMPLED rate).
    double cathodeCoeff = 0.0;
    std::array<double, 2> cathodeEnv {};

    // ADAA LUTs + per-channel previous-input state, per stage.
    std::array<double, LUT_SIZE> fLut1 {};
    std::array<double, LUT_SIZE> fLut2 {};
    std::array<double, 2>        s1PrevIn   {};
    std::array<double, 2>        s2PrevIn   {};
    std::array<double, 2>        nfbPrevOut {};   // 1-sample-delayed output (feedback)

    // DC blocker state per channel.
    double dcAlpha = 1.0;
    std::array<float, 2> dcPrevIn  {};
    std::array<float, 2> dcPrevOut {};

    // Dry-path latency-comp ring (used when disabled).
    int                latencySamples = 0;
    int                dryDelayPos    = 0;
    std::array<std::vector<float>, 2> dryDelay {};

    // -------------------------------------------------------------------------
    // Asymmetric grid-current soft-limit on the positive half cycle (stage 1).
    // -------------------------------------------------------------------------
    static double applyGridCurrentLimit(double vGridIn) noexcept
    {
        if (vGridIn > 0.0)
            return V_GRID_LIMIT * std::tanh(vGridIn / V_GRID_LIMIT);
        return vGridIn;
    }

    // -------------------------------------------------------------------------
    // Stage 1 — 12AX7 asymmetric common-cathode. Inverting; normalised to unit
    // small-signal slope by stage1Norm; tanh soft-clip at the output.
    // -------------------------------------------------------------------------
    double stage1F(double x) const noexcept
    {
        const double d = static_cast<double>(drive);
        double vGridIn = applyGridCurrentLimit(x * GRID_SCALE_1 * d);

        const double ipBias   = korenIp(V_GRID_BIAS_1,          V_PLATE_BIAS_1, MU1, EX1, KG1_1, KP1);
        const double ipSignal = korenIp(V_GRID_BIAS_1 + vGridIn, V_PLATE_BIAS_1, MU1, EX1, KG1_1, KP1);
        // Inverting; normalised to unit small-signal slope. NO symmetric output
        // clip: the Koren delta is already ASYMMETRICALLY bounded (positive grid
        // capped by the grid-current limit, negative toward cutoff), so this is
        // an even-harmonic-rich one-sided saturation. A symmetric tanh here would
        // clip both halves and inject odd (H3/H5) harmonics the push-pull oracle
        // does not show.
        return (-(ipSignal - ipBias) * R_LOAD_1) / stage1Norm;
    }

    // -------------------------------------------------------------------------
    // Stage 2 — 12AU7 push-pull, ODD-SYMMETRIC differential plate current
    // Ip(bias+v) − Ip(bias−v) (even harmonics self-cancel). Inverting; normalised
    // to unit small-signal slope by stage2Norm; tanh soft-clip.
    // -------------------------------------------------------------------------
    double stage2Raw(double v) const noexcept
    {
        const double vg   = v * GRID_SCALE_2;
        const double ipP  = korenIp(V_GRID_BIAS_2 + vg, V_PLATE_BIAS_2, MU2, EX2, KG1_2, KP2);
        const double ipM  = korenIp(V_GRID_BIAS_2 - vg, V_PLATE_BIAS_2, MU2, EX2, KG1_2, KP2);
        return ipP - ipM;   // odd-symmetric in v
    }

    double stage2F(double v) const noexcept
    {
        return -std::tanh(stage2Raw(v) / stage2Norm);   // inverting; unit small-signal slope
    }

    // -------------------------------------------------------------------------
    // Compute per-stage small-signal normalisers so each stage has unit-magnitude
    // slope at its operating point (=> unity cascade gain before NFB; NFB then
    // sets the −1.94 dB closed-loop, restored by nfbMakeup). Stage 1 tracks drive.
    // -------------------------------------------------------------------------
    void computeStageNorms() noexcept
    {
        const double d    = static_cast<double>(drive);
        // Stage 1: gm * R_LOAD at the bias point with the current drive.
        const double step = GRID_SCALE_1 * std::max(d, 1.0e-6) * DIFF_EPS;
        const double ipP  = korenIp(V_GRID_BIAS_1 + step, V_PLATE_BIAS_1, MU1, EX1, KG1_1, KP1);
        const double ipM  = korenIp(V_GRID_BIAS_1 - step, V_PLATE_BIAS_1, MU1, EX1, KG1_1, KP1);
        const double g1   = ((ipP - ipM) / (2.0 * step)) * R_LOAD_1;
        stage1Norm = (g1 > 1.0e-12) ? g1 : 1.0;

        // Stage 2: slope of the odd-symmetric raw differential at v = 0.
        const double rP = stage2Raw(+DIFF_EPS);
        const double rM = stage2Raw(-DIFF_EPS);
        const double g2 = (rP - rM) / (2.0 * DIFF_EPS);
        stage2Norm = (std::abs(g2) > 1.0e-18) ? std::abs(g2) : 1.0;
    }

    // -------------------------------------------------------------------------
    // Antiderivative LUT for a static curve f: F(x)=∫ f over [LUT_X_MIN, x].
    // -------------------------------------------------------------------------
    template <typename Fn>
    void buildLut(std::array<double, LUT_SIZE>& lut, Fn&& f) noexcept
    {
        const double dx = (LUT_X_MAX - LUT_X_MIN) / static_cast<double>(LUT_SIZE - 1);
        double prevF = f(LUT_X_MIN);
        lut[0] = 0.0;
        double acc = 0.0;
        for (int i = 1; i < LUT_SIZE; ++i)
        {
            const double x = LUT_X_MIN + static_cast<double>(i) * dx;
            const double fx = f(x);
            acc += 0.5 * (prevF + fx) * dx;   // trapezoidal
            lut[i] = acc;
            prevF  = fx;
        }
    }

    void buildLuts() noexcept
    {
        buildLut(fLut1, [this](double u) noexcept { return stage1F(u); });
        buildLut(fLut2, [this](double u) noexcept { return stage2F(u); });
    }

    static double lookupF(double v, const std::array<double, LUT_SIZE>& lut) noexcept
    {
        const double vClamped = std::min(std::max(v, LUT_X_MIN), LUT_X_MAX);
        const double dx       = (LUT_X_MAX - LUT_X_MIN) / static_cast<double>(LUT_SIZE - 1);
        const double idx      = (vClamped - LUT_X_MIN) / dx;
        const int    i0       = static_cast<int>(std::floor(idx));
        const int    i1       = std::min(i0 + 1, LUT_SIZE - 1);
        const double frac     = idx - static_cast<double>(i0);
        return lut[i0] + frac * (lut[i1] - lut[i0]);
    }

    // -------------------------------------------------------------------------
    // First-order ADAA for one static curve, with small-|dx| fallback to direct
    // midpoint evaluation (numerical + LUT-resolution guard).
    // -------------------------------------------------------------------------
    template <typename Fn>
    double adaaStep(double xn, double xPrev,
                    const std::array<double, LUT_SIZE>& lut, Fn&& f) const noexcept
    {
        const double dx    = xn - xPrev;
        const double lutDx = (LUT_X_MAX - LUT_X_MIN) / static_cast<double>(LUT_SIZE - 1);
        if (std::abs(dx) < std::max(ADAA_NUMERIC_EPS, lutDx))
            return f(0.5 * (xn + xPrev));
        return (lookupF(xn, lut) - lookupF(xPrev, lut)) / dx;
    }
};
