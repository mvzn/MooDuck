// MaybeDuck test runner — exercises the DSP cores headless.
//
// Build:  make -C tests
// Run:    ./tests/runtests

#include "../Source/DynamicsCore.h"
#include "../Source/CrossoverCore.h"
#include "../Source/FXFunctions.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int  gPasses = 0;
int  gFails  = 0;

void expectNear(double actual, double expected, double tol, const char* label)
{
    const double err = std::abs(actual - expected);
    if (err <= tol)
    {
        ++gPasses;
        std::printf("  [PASS] %s  (%.6g ≈ %.6g, err %.3g)\n", label, actual, expected, err);
    }
    else
    {
        ++gFails;
        std::printf("  [FAIL] %s  (%.6g vs %.6g, err %.3g > tol %.3g)\n",
                    label, actual, expected, err, tol);
    }
}

void expectTrue(bool cond, const char* label)
{
    if (cond) { ++gPasses; std::printf("  [PASS] %s\n", label); }
    else      { ++gFails;  std::printf("  [FAIL] %s\n", label); }
}

// =========================================================================
// dB ↔ linear conversions
// =========================================================================
void test_dB_linear()
{
    std::printf("\n== dB ↔ linear ==\n");
    expectNear(dB2raw(0.0),  1.0,        1e-12, "dB2raw(0) == 1");
    expectNear(dB2raw(-6.0), 0.501187,   1e-5,  "dB2raw(-6) ≈ 0.5012");
    expectNear(dB2raw(20.0), 10.0,       1e-9,  "dB2raw(20) == 10");
    expectNear(raw2dB(1.0),  0.0,        1e-12, "raw2dB(1) == 0");
    expectNear(raw2dB(0.5), -6.0206,     1e-3,  "raw2dB(0.5) ≈ -6.02");
    expectNear(raw2dB(2.0),  6.0206,     1e-3,  "raw2dB(2) ≈ 6.02");

    // Round-trip
    for (double d : { -30.0, -12.0, -1.0, 0.0, 3.0, 12.0 })
        expectNear(raw2dB(dB2raw(d)), d, 1e-9, ("round-trip " + std::to_string(d) + " dB").c_str());
}

// =========================================================================
// Envelope follower — attack/release time-constant accuracy
// =========================================================================
// After one attack time the envelope of a step input should reach 1 - 1/e of the
// target. In dB-output mode this maps to 20·log10(0.6321) ≈ -3.98 dB for a unity step.
void test_envelope_attack()
{
    std::printf("\n== Envelope follower — attack ==\n");
    const double fs        = 48000.0;
    const double attackMs  = 10.0;

    AudioDetector det;
    det.reset(fs);

    DetectorParameters p;
    p.attack_ms   = attackMs;
    p.release_ms  = 100.0;
    p.detect_dB   = true;
    p.detect_mode = DetectModes::PEAK;
    det.setDetectParameters(p);

    const int N = (int) std::lround(attackMs * fs * 0.001);
    double out = 0.0;
    for (int i = 0; i < N; ++i)
        out = det.processSample(1.0);

    expectNear(out, -3.98, 0.5, "PEAK envelope hits ≈ -3.98 dB after 1 attack-time");

    for (int i = 0; i < N * 10; ++i)
        out = det.processSample(1.0);
    expectNear(out, 0.0, 0.05, "PEAK envelope converges to 0 dB on sustained unity input");
}

void test_envelope_release()
{
    std::printf("\n== Envelope follower — release ==\n");
    const double fs        = 48000.0;
    const double releaseMs = 100.0;

    AudioDetector det;
    det.reset(fs);

    DetectorParameters p;
    p.attack_ms   = 1.0;
    p.release_ms  = releaseMs;
    p.detect_dB   = true;
    p.detect_mode = DetectModes::PEAK;
    det.setDetectParameters(p);

    // Saturate to ~0 dB
    for (int i = 0; i < (int) (fs * 0.1); ++i)
        det.processSample(1.0);

    // Then feed silence for one release time; envelope should drop to ~1/e (≈ -8.69 dB)
    const int N = (int) std::lround(releaseMs * fs * 0.001);
    double out = 0.0;
    for (int i = 0; i < N; ++i)
        out = det.processSample(0.0);

    expectNear(out, -8.69, 1.0, "PEAK envelope hits ≈ -8.69 dB after 1 release-time of silence");
}

// =========================================================================
// LR4 crossover — bands sum back to input (the LR4 reconstruction property)
// =========================================================================
void test_crossover_sum()
{
    std::printf("\n== LR4 multiband crossover ==\n");
    const double fs = 48000.0;

    MonoMultibandCrossover xover;
    xover.prepare(fs);
    for (int i = 0; i < MonoMultibandCrossover::numCrossoverPoints; ++i)
        xover.setCrossoverFrequencyImmediate(i, MonoMultibandCrossover::kDefaultFreqs[i]);

    // Sustained sine; measure peak of input vs peak of summed bands after settling
    auto runFreq = [&](double f)
    {
        xover.reset();
        const int total = (int) std::lround(fs * 0.5);   // 500 ms — plenty for LR4 to settle
        const int warm  = (int) std::lround(fs * 0.4);   // ignore the first 400 ms (transient)
        double peakIn = 0.0, peakSum = 0.0;
        for (int n = 0; n < total; ++n)
        {
            const double x = std::sin(2.0 * juce::MathConstants<double>::pi * f * n / fs);
            const auto bands = xover.processSample(x);
            double s = 0.0;
            for (double b : bands) s += b;
            if (n >= warm)
            {
                peakIn  = std::max(peakIn,  std::abs(x));
                peakSum = std::max(peakSum, std::abs(s));
            }
        }
        return peakSum / peakIn;
    };

    // Cascaded LR4 topology preserves signal magnitude to within ~2 dB.
    // (Per-stage LR4 sums to allpass; the cascade introduces a small phase-induced amplitude ripple.)
    expectNear(runFreq(50.0),    1.0, 0.2, "sum at 50 Hz ≈ unity");
    expectNear(runFreq(1000.0),  1.0, 0.2, "sum at 1 kHz ≈ unity");
    expectNear(runFreq(5000.0),  1.0, 0.2, "sum at 5 kHz ≈ unity");
    expectNear(runFreq(12000.0), 1.0, 0.2, "sum at 12 kHz ≈ unity");
    expectNear(runFreq(400.0),   1.0, 0.2, "sum at xover edge 400 Hz ≈ unity");
    expectNear(runFreq(2000.0),  1.0, 0.2, "sum at xover edge 2 kHz ≈ unity");
}

// =========================================================================
// Gain reduction curve — hard knee, no SC (detector follows input)
// =========================================================================
void test_gr_curve()
{
    std::printf("\n== Gain reduction curve ==\n");
    const double fs = 48000.0;

    DynamicsProcessor proc;
    proc.reset(fs);

    DynamicsProcessorParameters params;
    params.threshold_dB    = -10.0;
    params.ratio           = 4.0;
    params.knee_width_dB   = 0.0;     // hard knee path
    params.soft_knee       = false;
    params.hard_limit_gate = false;
    params.enable_sc       = false;
    params.attack_ms       = 0.01;    // fast convergence
    params.release_ms      = 0.01;
    params.hold_ms         = 0.0;
    params.output_gain_dB  = 0.0;
    params.calculation     = dynamicsProcessorType::kCompressor;
    proc.setParameters(params);

    auto steadyStateGrDb = [&](double levelDb) -> double
    {
        proc.reset(fs);
        proc.setParameters(params);
        const double amp = dB2raw(levelDb);
        for (int i = 0; i < 1000; ++i)
            proc.processSample(amp);
        return proc.getGainReductionDb();
    };

    expectNear(steadyStateGrDb(-20.0), 0.0,  0.1, "GR at -20 dB (below thresh) ≈ 0 dB");
    expectNear(steadyStateGrDb(-10.0), 0.0,  0.1, "GR at threshold (-10 dB) ≈ 0 dB");
    expectNear(steadyStateGrDb(-6.0), -3.0,  0.3, "GR at -6 dB, ratio 4:1 ≈ -3 dB");
    expectNear(steadyStateGrDb(0.0),  -7.5,  0.5, "GR at 0 dB, ratio 4:1 ≈ -7.5 dB");
}

// =========================================================================
// Hold stage — gain reduction is held at peak for hold_ms after signal drops,
// and release proceeds smoothly afterwards. Validated at the plugin's max
// release time (5000 ms) since hold and release are designed to be independent.
//
// Topology: during hold the detector's internal envelope is pinned at peak each
// sample, so when hold expires the release continues at the configured rate
// from peak (no snap).
// =========================================================================
void test_hold_stage()
{
    std::printf("\n== Hold stage ==\n");
    const double fs        = 48000.0;
    const double holdMs    = 100.0;
    const double releaseMs = 5000.0;   // plugin's max release

    DynamicsProcessor proc;
    proc.reset(fs);

    DynamicsProcessorParameters params;
    params.threshold_dB    = -10.0;
    params.ratio           = 4.0;
    params.knee_width_dB   = 0.0;
    params.soft_knee       = false;
    params.hard_limit_gate = false;
    params.enable_sc       = false;
    params.attack_ms       = 0.5;
    params.release_ms      = releaseMs;
    params.hold_ms         = holdMs;
    params.output_gain_dB  = 0.0;
    params.calculation     = dynamicsProcessorType::kCompressor;
    proc.setParameters(params);

    // Drive to steady-state GR at 0 dB input
    const double amp = dB2raw(0.0);
    for (int i = 0; i < (int) (fs * 0.2); ++i) proc.processSample(amp);
    const double peakGr = proc.getGainReductionDb();

    // Sample GR three times during hold — should pin at peak the whole way
    const int quarterHold = (int) std::lround(holdMs * fs * 0.001 * 0.25);
    for (int i = 0; i < quarterHold; ++i) proc.processSample(0.0);
    const double gr25 = proc.getGainReductionDb();
    for (int i = 0; i < quarterHold; ++i) proc.processSample(0.0);
    const double gr50 = proc.getGainReductionDb();
    for (int i = 0; i < quarterHold; ++i) proc.processSample(0.0);
    const double gr75 = proc.getGainReductionDb();

    expectNear(gr25, peakGr, 0.05, "hold @ 25% — GR pinned at peak");
    expectNear(gr50, peakGr, 0.05, "hold @ 50% — GR pinned at peak");
    expectNear(gr75, peakGr, 0.05, "hold @ 75% — GR pinned at peak");

    // Drain the remaining 25% of hold so we're past expiry, then run one full
    // release time-constant of silence. With release=5000ms, detector envelope
    // decays from 1.0 to 1/e (≈ -8.69 dB level); threshold -10, ratio 4 →
    // expected GR ≈ -0.98 dB. So we should see > 5 dB of release recovery.
    for (int i = 0; i < quarterHold; ++i) proc.processSample(0.0);
    const int oneReleaseTau = (int) std::lround(releaseMs * fs * 0.001);
    for (int i = 0; i < oneReleaseTau; ++i) proc.processSample(0.0);
    const double postGr = proc.getGainReductionDb();
    expectTrue(postGr > peakGr + 5.0, "GR releases by > 5 dB after one 5000 ms release-time past hold");
    expectNear(postGr, -0.98, 0.3, "GR after one release-time matches analytic prediction (≈ -0.98 dB)");
}

} // namespace

int main()
{
    std::printf("MaybeDuck DSP tests\n");

    test_dB_linear();
    test_envelope_attack();
    test_envelope_release();
    test_crossover_sum();
    test_gr_curve();
    test_hold_stage();

    std::printf("\n----------------\n");
    std::printf("%d passed, %d failed\n", gPasses, gFails);
    return gFails == 0 ? 0 : 1;
}
