#pragma once

/*  PultecCore.h
    WDF model of the Pultec EQP-1A passive filter network.

    Topology from: Barrera, A., Lizarraga-Seijas, X., and Font, F.
    "Modeling the Pultec EQP-1A with Wave Digital Filters."
    SMC 2024, pp. 445–452.
    Reference C++ implementation: github.com/ABSounds/EQP-WDF-1A (GPL-3)

    Architecture:
      - All four filter sections (LF boost, LF cut, HF boost, HF cut) share a
        single WDF tree rooted at an IdealVoltageSourceT.  The LF cut and boost
        networks share junction nodes; the simultaneous boost+cut interaction
        ("Pultec trick") emerges naturally from the shared impedance.
      - HF cut is a simple 1st-order RC shelf with no inductor — implemented
        with a juce::dsp::IIR biquad (sufficient; no WDF needed here).
      - Stereo: two independent WDF tree instances (L/R) sharing the same
        coefficient state.
      - updateCoefficients() must be called from the audio thread only when
        a parameter changes.  processSample() is per-sample and lock-free.

    Component value sources:
      - LF section: LoFreqC2 values from ABSounds reference; C1 = C2/30 fixed.
      - HF boost: RATIO_L1_C1 = L/C = 4.5e6 H/F (Z0≈2121Ω); C and L derived analytically.
      - HF cut capacitors: measured values (270/135/68 nF for 5/10/20 kHz).
      - Variable resistor maxima: RBOOSTHI_MAX=12kΩ, RCUTHI_MAX=1kΩ,
        RBOOSTLO_MAX=8.2kΩ, RCUTLO_MAX=110kΩ, RQ_MAX from reference.
*/

#include <JuceHeader.h>
#include "chowdsp_wdf/chowdsp_wdf.h"

#include <cmath>
#include <cstring>
#include <tuple>
#include <vector>

namespace wdft = chowdsp::wdft;

//==============================================================================
//  Ideal transformer WDF adaptor (Phase 7, Tier 2)
//==============================================================================
//  A two-port ideal transformer with voltage turns ratio n (secondary:primary):
//      v2 = n * v1,   i1 = -n * i2   (lossless: v1*i1 + v2*i2 = 0)
//  Up-facing port = primary (faces the parent / source side).
//  Down-facing child = the secondary subtree (e.g. the makeup-amp grid load).
//
//  Wave-domain scattering (derived with R2 = n^2 * R1 for adaptation):
//      b1 = a2 / n           (reflected toward parent  = secondary wave / n)
//      b2 = n  * a1          (sent into secondary       = primary wave * n)
//      R1 = R2 / n^2         (primary port impedance = secondary impedance / n^2)
//
//  The magnetising (primary) inductance Lp is NOT part of this element; model it
//  explicitly as a one-port inductor in parallel with this adaptor's up-port.
//  That parallel Lp is what produces the LF roll-off / impedance isolation that
//  the real inter-stage transformer provides.
template <typename T, typename SecondaryType>
class IdealTransformerT : public wdft::BaseWDF
{
public:
    explicit IdealTransformerT (SecondaryType& sec, T turnsRatio = (T) 1)
        : secondary (sec), n (turnsRatio)
    {
        secondary.connectToParent (this);
        calcImpedance();
    }

    /** Sets the voltage turns ratio n (secondary:primary). */
    void setTurnsRatio (T turnsRatio)
    {
        if (n == turnsRatio) return;
        n = turnsRatio;
        propagateImpedanceChange();
    }

    inline void calcImpedance() override
    {
        wdf.R = secondary.wdf.R / (n * n);
        wdf.G = (T) 1.0 / wdf.R;
    }

    /** Incident wave a1 into the primary -> b2 = n*a1 sent down to secondary. */
    inline void incident (T a1) noexcept
    {
        wdf.a = a1;
        secondary.incident (n * a1);
    }

    /** Reflected wave toward the parent: b1 = a2 / n (a2 = secondary reflected). */
    inline T reflected() noexcept
    {
        wdf.b = secondary.reflected() / n;
        return wdf.b;
    }

    wdft::WDFMembers<T> wdf;

private:
    SecondaryType& secondary;
    T n;
};

//==============================================================================
//  Inter-stage transformer stage (Phase 7, Tier 2) — Triad HS-29
//==============================================================================
//  Models the EQP-1A's inter-stage transformer that sits between the passive
//  EQ output and the 12AX7 grid:
//      Vsrc(EQ out) --[Rs = EQ output Z]--+-- Lp (600 H magnetising) --gnd
//                                         +-- xfmr 1:n -- Rgrid (12AX7 grid) -gnd
//      output = secondary voltage = voltage(Rgrid)
//
//  Datasheet (Triad HS-29): turns ratio 1:2, primary inductance Lp = 600 H.
//  With Lp that high and the EQ's low output impedance, the LF corner
//  (Rs||R1)/(2*pi*Lp) sits well below 1 Hz, so the transformer is essentially
//  transparent in band (a ~unity, ~+6 dB-step-up element with a sub-1 Hz DC
//  block).  It is therefore a LINEAR POST-ELEMENT on the EQ output: by design
//  it cannot alter the HF-Boost LF-coupling differential (see Tier-1 proof).
//  Included for structural fidelity + empirical confirmation, not as a fix.
struct InterstageXfmr
{
    static constexpr double GRID_R = 1.0e6;   // 12AX7 grid input impedance (Ω)
    static constexpr double SRC_R  = 600.0;   // EQ output / design source Z (Ω)

    wdft::ResistorT<double>  Rgrid { GRID_R };
    IdealTransformerT<double, decltype(Rgrid)> Xfmr { Rgrid, 2.0 };   // 1:2 step-up
    wdft::InductorT<double>  Lp { 600.0 };                            // HS-29 primary L
    wdft::WDFParallelT<double, decltype(Lp), decltype(Xfmr)> load { Lp, Xfmr };
    wdft::ResistorT<double>  Rs { SRC_R };
    wdft::WDFSeriesT<double, decltype(Rs), decltype(load)> top { Rs, load };
    wdft::IdealVoltageSourceT<double, decltype(top)> Vsrc { top };

    double turns = 2.0;

    void prepare (double fs) { Lp.prepare (fs); }

    /** In-band (HP >> corner) voltage gain: n * R1 / (Rs + R1), R1 = Rgrid/n^2.
        Used to normalise the stage so flat response stays at unity. */
    double nominalGain() const noexcept
    {
        const double R1 = GRID_R / (turns * turns);
        return turns * R1 / (SRC_R + R1);
    }

    inline double process (double vIn) noexcept
    {
        Vsrc.setVoltage (vIn);
        Vsrc.incident (top.reflected());
        top.incident (Vsrc.reflected());
        return wdft::voltage<double> (Rgrid);
    }

    void setTurnsRatio (double n) { turns = n; Xfmr.setTurnsRatio (n); }
    void setPrimaryL   (double H) { Lp.setInductanceValue (H); }
};

//==============================================================================
//  Internal: one channel of the full EQP-1A WDF tree
//==============================================================================

struct EQP1AChannel
{
    // -----------------------------------------------------------------------
    //  Circuit constants (from ABSounds reference / SMC 2024)
    // -----------------------------------------------------------------------
    static constexpr double RBOOSTHI_MAX = 12.0e3;   // HF boost pot max (Ω)
    static constexpr double RCUTHI_MAX   =  1.0e3;   // HF cut  pot max (Ω)
    static constexpr double RBOOSTLO_MAX =  8.2e3;   // LF boost pot max (Ω)
    static constexpr double RCUTLO_MAX   = 110.0e3;  // LF cut  pot max (Ω)
    static constexpr double RQ_MAX       =  2.0e3;   // HF BQ pot max (Ω)
    static constexpr double RQ_OFFSET    =  100.0;   // fixed series offset (Ω)
    static constexpr double RATIO_L1_C1  =  4.5e6;   // L/C ratio for HF LC pair (H/F = Ω²); Z0=√RATIO≈2121Ω
    // Insertion-loss compensation.  Once all the HF pots are correctly
    // initialised in prepare() (RBoostHi2 / RQ / RCutHi2 — see the note there),
    // the passive insertion loss is dead-flat across the band (verified against
    // the ngspice oracle in eq-research/spice/: a constant ~-23.8 dB).  The prior
    // 16.598 figure had been trimmed +0.6 dB to compensate the *buggy* baseline
    // (RBoostHi2 stuck at 12 kOhm); with the baseline corrected, 15.47 re-centres
    // it to 0 dB (within +/- 0.2 dB at 20 Hz / 200 Hz / 1 kHz / 8 kHz, +/- 1 dB to
    // 20 kHz).  Note this is essentially the original ABSounds 15.4882 value.
    static constexpr double MAKEUP_GAIN  =  15.47;

    // -----------------------------------------------------------------------
    //  One-port WDF elements
    // -----------------------------------------------------------------------

    // HF boost sub-network
    wdft::ResistorT<double> RBoostHi1 { RBOOSTHI_MAX };
    wdft::ResistorT<double> RBoostHi2 { RBOOSTHI_MAX };
    wdft::ResistorT<double> RQ        { RQ_MAX };
    wdft::ResistorT<double> RQoffset  { RQ_OFFSET };
    wdft::CapacitorT<double> C1       { 1.0e-9 };    // freq-switched
    wdft::InductorT<double>  L1       { 1.0e-3 };    // freq-switched

    // HF cut sub-network
    wdft::ResistorT<double>  RCutHi1  { RCUTHI_MAX };
    wdft::ResistorT<double>  RCutHi2  { RCUTHI_MAX };
    wdft::CapacitorT<double> CHiCut   { 135.0e-9 };  // freq-switched

    // LF sub-network
    wdft::CapacitorT<double> LoFreqC1 { 1.0e-6 };   // freq-switched
    wdft::CapacitorT<double> LoFreqC2 { 30.0e-6 };  // freq-switched
    wdft::ResistorT<double>  RBoostLo { RBOOSTLO_MAX };
    wdft::ResistorT<double>  RCutLo   { RCUTLO_MAX };
    wdft::ResistorT<double>  R2       { 1000.0 };
    wdft::ResistorT<double>  R3       { 10000.0 };

    // -----------------------------------------------------------------------
    //  Adaptor tree — declared in bottom-up order (leaves first, root last)
    //  Naming follows the filter-topology.md table.
    // -----------------------------------------------------------------------

    // HF boost: series(C1, L1) → SCLc
    wdft::WDFSeriesT<double, decltype(C1), decltype(L1)>
        SCLc { C1, L1 };

    // HF boost: series(RQ, RQoffset) → SRQ
    wdft::WDFSeriesT<double, decltype(RQ), decltype(RQoffset)>
        SRQ { RQ, RQoffset };

    // HF boost: series(SCLc, SRQ) → C_adaptor
    wdft::WDFSeriesT<double, decltype(SCLc), decltype(SRQ)>
        C_adaptor { SCLc, SRQ };

    // HF boost: parallel(C_adaptor, RBoostHi2) → pCD
    wdft::WDFParallelT<double, decltype(C_adaptor), decltype(RBoostHi2)>
        pCD { C_adaptor, RBoostHi2 };

    // HF boost: series(RBoostHi1, pCD) → S1
    wdft::WDFSeriesT<double, decltype(RBoostHi1), decltype(pCD)>
        S1 { RBoostHi1, pCD };

    // HF cut: parallel(RCutHi2, CHiCut) → pFG
    wdft::WDFParallelT<double, decltype(RCutHi2), decltype(CHiCut)>
        pFG { RCutHi2, CHiCut };

    // HF cut: series(RCutHi1, pFG) → sEFG
    wdft::WDFSeriesT<double, decltype(RCutHi1), decltype(pFG)>
        sEFG { RCutHi1, pFG };

    // LF: parallel(LoFreqC1, RCutLo) → H
    wdft::WDFParallelT<double, decltype(LoFreqC1), decltype(RCutLo)>
        H { LoFreqC1, RCutLo };

    // LF: series(R2, R3) → I
    wdft::WDFSeriesT<double, decltype(R2), decltype(R3)>
        I { R2, R3 };

    // LF: parallel(RBoostLo, LoFreqC2) → J
    wdft::WDFParallelT<double, decltype(RBoostLo), decltype(LoFreqC2)>
        J { RBoostLo, LoFreqC2 };

    // Combine HF cut and LF-H-I: series(H, I) → sHI
    wdft::WDFSeriesT<double, decltype(H), decltype(I)>
        sHI { H, I };

    // parallel(sEFG, sHI) → pEFGHI
    wdft::WDFParallelT<double, decltype(sEFG), decltype(sHI)>
        pEFGHI { sEFG, sHI };

    // Full: series(S1, pEFGHI) → S2
    wdft::WDFSeriesT<double, decltype(S1), decltype(pEFGHI)>
        S2 { S1, pEFGHI };

    // Full: series(S2, J) → S3  (root child)
    wdft::WDFSeriesT<double, decltype(S2), decltype(J)>
        S3 { S2, J };

    // Root: ideal voltage source driving S3
    wdft::IdealVoltageSourceT<double, decltype(S3)>
        Vin { S3 };

    // -----------------------------------------------------------------------
    //  Initialise stateful elements at a new sample rate
    // -----------------------------------------------------------------------
    void prepare (double sampleRate)
    {
        C1.prepare       (sampleRate);
        L1.prepare       (sampleRate);
        LoFreqC1.prepare (sampleRate);
        LoFreqC2.prepare (sampleRate);
        CHiCut.prepare   (sampleRate);
    }

    // -----------------------------------------------------------------------
    //  Per-sample processing
    // -----------------------------------------------------------------------
    inline double processSample (double xn) noexcept
    {
        Vin.setVoltage (xn);
        Vin.incident (S3.reflected());
        S3.incident (Vin.reflected());
        // Compensate passive insertion loss (matches 12AX7 makeup amplifier at unity)
        return (-wdft::voltage<double> (J) + wdft::voltage<double> (R3)) * MAKEUP_GAIN;
    }

    // Diagnostic accessors (valid after processSample).
    //  VJ/VR3 : the two synthetic-readout terms (-V(J)+V(R3)).
    //  Vrout  : voltage across the real output element Rout (= I = R2+R3) = V6-V7.
    //  Vnode6 : single-ended output node 6 referenced to ground (V6) = V(I)+V(J).
    //  Vnode7 : single-ended output node 7 referenced to ground (V7) = V(J).
    double VJ    () const noexcept { return wdft::voltage<double> (J); }
    double VR3   () const noexcept { return wdft::voltage<double> (R3); }
    double Vrout () const noexcept { return wdft::voltage<double> (I); }
    double Vnode6() const noexcept { return wdft::voltage<double> (I) + wdft::voltage<double> (J); }
    double Vnode7() const noexcept { return wdft::voltage<double> (J); }
    // Node-to-ground voltages walking up the series loop (gnd at J's bottom):
    double Vnode4() const noexcept { return wdft::voltage<double> (pEFGHI) + wdft::voltage<double> (J); }
    double Vnode3() const noexcept { return wdft::voltage<double> (pCD) + Vnode4(); }

    // Debug: dump adapted port impedances (wdf.R) of every element/adaptor.
    void dbgDumpR (const char* tag) const
    {
        std::printf("  [%s] RBH1=%.4g RBH2=%.4g RQ=%.4g RCH1=%.4g RCH2=%.4g CHiCut=%.4g "
                    "C1=%.4g L1=%.4g | pCD=%.4g pFG=%.4g sEFG=%.4g H=%.4g I=%.4g sHI=%.4g "
                    "pEFGHI=%.4g S1=%.4g S2=%.4g S3=%.4g J=%.4g RBLo=%.4g RCLo=%.4g\n",
                    tag, RBoostHi1.wdf.R, RBoostHi2.wdf.R, RQ.wdf.R, RCutHi1.wdf.R, RCutHi2.wdf.R,
                    CHiCut.wdf.R, C1.wdf.R, L1.wdf.R, pCD.wdf.R, pFG.wdf.R, sEFG.wdf.R, H.wdf.R,
                    I.wdf.R, sHI.wdf.R, pEFGHI.wdf.R, S1.wdf.R, S2.wdf.R, S3.wdf.R, J.wdf.R,
                    RBoostLo.wdf.R, RCutLo.wdf.R);
    }
};

//==============================================================================
//  Public class: PultecEQP1A
//==============================================================================

class PultecEQP1A
{
public:
    PultecEQP1A() = default;

    // Non-copyable - the oversampler holds internal state
    PultecEQP1A(const PultecEQP1A&)            = delete;
    PultecEQP1A& operator=(const PultecEQP1A&) = delete;

    // -----------------------------------------------------------------------
    //  Prepare (call from prepareToPlay; resets all filter state)
    // -----------------------------------------------------------------------
    /** EQ bypass — when true, processBlock() and processSample() are pure passthroughs
     *  (no oversampling, no WDF, no biquad). Use as an A/B reference or as an off switch. */
    void setBypass (bool shouldBypass) noexcept { bypass = shouldBypass; }
    bool getBypass () const noexcept             { return bypass; }

    // -----------------------------------------------------------------------
    //  Inter-stage transformer (HS-29) controls — for A/B + tuning.
    // -----------------------------------------------------------------------
    void setTransformerEnabled (bool on) noexcept { transformerEnabled = on; }
    bool getTransformerEnabled () const noexcept   { return transformerEnabled; }

    // -----------------------------------------------------------------------
    //  Host latency reporting
    // -----------------------------------------------------------------------
    /** Constant processing latency (in HOST-rate samples) introduced by the 4x
     *  oversampler's group delay. This is the SINGLE source of truth: the same
     *  value drives the internal dry-path latency-comp delay ring (so the bypass
     *  crossfade stays time-aligned) and is what PluginProcessor folds into the
     *  latency reported to the host via setLatencySamples().
     *
     *  Valid only after prepare(); 0 before. The value is constant regardless of
     *  the bypass state — when bypassed, the dry path is delayed by exactly this
     *  many samples (see processBlock) — so it can be reported once at
     *  prepareToPlay without any mid-stream setLatencySamples churn. */
    int getLatencySamples () const noexcept { return latencySamples; }

    // Diagnostic: 0 = normal output; 1 = raw V(J); 2 = raw V(R3) (bypasses
    // HF-cut + transformer + makeup).  Used to localise the LF-coupling artifact.
    void setDiagOutput (int mode) noexcept { diagOutput = mode; }

    void dbgDumpChanL (const char* tag) const { chanL.dbgDumpR (tag); }

    void setTransformerPrimaryL (double henries) noexcept
    {
        xfmrL.setPrimaryL (henries);
        xfmrR.setPrimaryL (henries);
    }

    void setTransformerTurns (double n) noexcept
    {
        xfmrL.setTurnsRatio (n);
        xfmrR.setTurnsRatio (n);
        xfmrNorm = xfmrL.nominalGain();
    }

    void prepare (double sr, int maxBlockSize)
    {
        // Run the WDF tree and HF-cut biquad at 4x the host sample rate so
        // bilinear Nyquist warping in the WDF capacitor/inductor primitives
        // sits well above the audio band (see PULTEC_DEV.md "Open issue -
        // baseline frequency response").
        sampleRate = sr * 4.0;

        oversampler.initProcessing (static_cast<size_t> (maxBlockSize));
        oversampler.reset();

        // Dry-path latency compensation: size a delay ring to the oversampler's
        // group delay (reported at host rate) so the bypass crossfade is time-
        // aligned. Reset the mix to match the current bypass target (settled).
        latencySamples = static_cast<int> (std::lround (oversampler.getLatencyInSamples()));
        latencySamples = juce::jmax (0, latencySamples);
        dryDelayL.assign (static_cast<size_t> (latencySamples), 0.0f);
        dryDelayR.assign (static_cast<size_t> (latencySamples), 0.0f);
        dryDelayPos = 0;
        dryScratchL.assign (static_cast<size_t> (maxBlockSize), 0.0f);
        dryScratchR.assign (static_cast<size_t> (maxBlockSize), 0.0f);
        bypassMix = bypass ? 1.0f : 0.0f;
        bypassInc = 1.0f / juce::jmax (1.0f, static_cast<float> (0.010 * sr)); // ~10 ms

        chanL.prepare (sampleRate);
        chanR.prepare (sampleRate);

        xfmrL.prepare (sampleRate);
        xfmrR.prepare (sampleRate);
        xfmrNorm = xfmrL.nominalGain();   // normalise so flat response stays at unity

        hfCutL.reset();
        hfCutR.reset();
        updateLfCoefficients();
        // Sync every variable pot resistor with its default (zero-setting) value.
        // The EQP1AChannel constructor initialises each pot resistor to its RMAX,
        // which is NOT the 0-dB value for the complementary pots; without these
        // calls a pot whose setter never runs (or early-returns because the value
        // already equals its default) stays at RMAX and corrupts the baseline.
        //
        // This previously bit the HF section (root-caused 2026-05-30 against the
        // ngspice oracle in eq-research/spice/): at HF-boost = 0, setHfBoostDb(0)
        // early-returns, so RBoostHi2 stayed at RBOOSTHI_MAX (12 kΩ) instead of ~0 Ω,
        // distorting the boost=0 baseline and producing a spurious ~+5.5 dB 60 Hz
        // rise the instant any boost was dialled in.  RCutHi2 had the same latent
        // problem (stuck at 1 kΩ instead of ~0).  Initialising all of them makes the
        // passive baseline dead-flat (per the oracle), which is why MAKEUP_GAIN was
        // re-tuned to match the corrected insertion loss.
        updateLfBoost();
        updateLfCut();
        updateHfBoost();
        updateHfBQ();
        // RCutHi1/RCutHi2 are not driven by a pot setter (the HF cut shelf is applied
        // by the hfCut biquad); the WDF cut branch is held at its 0-dB reference, so
        // sync RCutHi2 to its 0-dB value (~0 Ω) rather than the RMAX default.
        chanL.RCutHi2.setResistanceValue (1.0);
        chanR.RCutHi2.setResistanceValue (1.0);
        updateHfBoostCoefficients();
        updateHfCutCoefficients();
    }

    // -----------------------------------------------------------------------
    //  Parameter setters — call from audio thread only
    //  Each setter checks for change before touching the WDF tree.
    //  NOTE: the continuous pots apply instantly; the plugin glides the values
    //  it feeds these setters (see PluginProcessor's Pultec pot smoothing) so
    //  moving-fader automation doesn't zipper. Offline callers (tests) get the
    //  immediate semantics they expect.
    // -----------------------------------------------------------------------

    void setLfFreqIndex (int idx)
    {
        idx = juce::jlimit (0, 4, idx);
        if (idx == lfFreqIdx) return;
        lfFreqIdx = idx;
        updateLfCoefficients();
    }

    void setLfBoostDb (float db)
    {
        db = juce::jlimit (0.0f, 16.0f, db);
        if (db == lfBoostDb) return;
        lfBoostDb = db;
        updateLfBoost();
    }

    void setLfAttenDb (float db)
    {
        db = juce::jlimit (0.0f, 16.0f, db);
        if (db == lfAttenDb) return;
        lfAttenDb = db;
        updateLfCut();
    }

    void setHfFreqIndex (int idx)
    {
        idx = juce::jlimit (0, 6, idx);
        if (idx == hfFreqIdx) return;
        hfFreqIdx = idx;
        updateHfBoostCoefficients();
    }

    void setHfBoostDb (float db)
    {
        db = juce::jlimit (0.0f, 16.0f, db);
        if (db == hfBoostDb) return;
        hfBoostDb = db;
        updateHfBoost();
    }

    void setHfBandwidth (float bw)
    {
        bw = juce::jlimit (0.0f, 1.0f, bw);
        if (bw == hfBw) return;
        hfBw = bw;
        updateHfBQ();
    }

    void setHfAttenFreqIndex (int idx)
    {
        idx = juce::jlimit (0, 2, idx);
        if (idx == hfAttenFreqIdx) return;
        hfAttenFreqIdx = idx;
        updateHfCutCoefficients();
    }

    void setHfAttenDb (float db)
    {
        db = juce::jlimit (0.0f, 14.0f, db);
        if (db == hfAttenDb) return;
        hfAttenDb = db;
        updateHfCutCoefficients();
    }

    // -----------------------------------------------------------------------
    //  Processing
    // -----------------------------------------------------------------------

    /** Process one stereo sample through the full EQP-1A network.
     *  NOTE: this single-sample path bypasses the 4x oversampler and runs
     *  the WDF tree at its prepared (upsampled) rate directly.  It is
     *  retained only for the mono fallback in PluginProcessor and will
     *  exhibit the same WDF Nyquist warping the oversampler is designed to
     *  hide.  Prefer processBlock() for accurate frequency response.
     */
    void processSample (float& left, float& right) noexcept
    {
        const float target = bypass ? 1.0f : 0.0f;

        // Fully bypassed and settled: pure passthrough. (Mono path has no
        // oversampler, hence no latency to compensate.)
        if (target == 1.0f && bypassMix == target)
            return;

        const float dryL = left;
        const float dryR = right;

        // WDF EQ (LF + HF boost)
        double outL = chanL.processSample (static_cast<double> (left));
        double outR = chanR.processSample (static_cast<double> (right));

        // HF cut biquad (applied after WDF)
        outL = static_cast<double> (hfCutL.processSample (static_cast<float> (outL)));
        outR = static_cast<double> (hfCutR.processSample (static_cast<float> (outR)));

        // Inter-stage transformer (HS-29), normalised to unity in-band gain
        if (transformerEnabled)
        {
            outL = xfmrL.process (outL) / xfmrNorm;
            outR = xfmrR.process (outR) / xfmrNorm;
        }

        // Crossfade dry/wet so a bypass toggle ramps over ~10 ms instead of
        // stepping. When settled active (bypassMix == 0) this is a no-op blend.
        bypassMix = (target > bypassMix) ? juce::jmin (target, bypassMix + bypassInc)
                                         : juce::jmax (target, bypassMix - bypassInc);
        const float wet = 1.0f - bypassMix;
        left  = wet * static_cast<float> (outL) + bypassMix * dryL;
        right = wet * static_cast<float> (outR) + bypassMix * dryR;
    }

    /** Process a stereo block through the 4x-oversampled EQP-1A network.
     *  Signal flow: upsample -> WDF EQ + HF-cut biquad (per upsampled sample)
     *  -> downsample.  Audio-thread-safe: the oversampler is pre-allocated
     *  in prepare(); no allocation occurs here.
     */
    void processBlock (float* left, float* right, int numSamples) noexcept
    {
        const float target          = bypass ? 1.0f : 0.0f;
        const bool  transitioning   = (bypassMix != target);

        // Always push the incoming dry signal through the latency-comp delay
        // ring so its history stays valid; capture the delayed dry we'll need
        // for the bypassed / crossfade output. Reads input[n-L] without yet
        // touching left/right (the wet path still needs the raw input below).
        if (transitioning || target == 1.0f)
            captureDelayedDry (left, right, numSamples);
        else
            advanceDelayRing  (left, right, numSamples);

        // Fully bypassed and settled: output the latency-matched dry signal.
        // (Same L-sample delay as the wet path, so toggling causes no time jump.)
        if (target == 1.0f && ! transitioning)
        {
            std::memcpy (left,  dryScratchL.data(), sizeof (float) * static_cast<size_t> (numSamples));
            std::memcpy (right, dryScratchR.data(), sizeof (float) * static_cast<size_t> (numSamples));
            return;
        }

        // Run the wet (oversampled EQ) path in place on left/right.
        processWetBlock (left, right, numSamples);

        // Fully active and settled: wet output is already in place.
        if (target == 0.0f && ! transitioning)
            return;

        // Crossfade: ramp bypassMix toward target, blending delayed dry vs wet.
        for (int n = 0; n < numSamples; ++n)
        {
            bypassMix = (target > bypassMix) ? juce::jmin (target, bypassMix + bypassInc)
                                             : juce::jmax (target, bypassMix - bypassInc);
            const float wet = 1.0f - bypassMix;
            left[n]  = wet * left[n]  + bypassMix * dryScratchL[static_cast<size_t> (n)];
            right[n] = wet * right[n] + bypassMix * dryScratchR[static_cast<size_t> (n)];
        }
    }

    // -----------------------------------------------------------------------
    //  Pure per-sample EQ at the (already-upsampled) rate, for a shared
    //  oversampling domain (PultecMakeupChain): WDF tree + HF-cut biquad +
    //  inter-stage transformer, WITHOUT this stage's own oversampler or bypass
    //  crossfade (the caller owns those). Mutates l/r in place.
    // -----------------------------------------------------------------------
    inline void processOversampledSample (double& l, double& r) noexcept
    {
        double outL = chanL.processSample (l);
        double outR = chanR.processSample (r);

        outL = static_cast<double> (hfCutL.processSample (static_cast<float> (outL)));
        outR = static_cast<double> (hfCutR.processSample (static_cast<float> (outR)));

        if (transformerEnabled)
        {
            outL = xfmrL.process (outL) / xfmrNorm;
            outR = xfmrR.process (outR) / xfmrNorm;
        }

        l = outL;
        r = outR;
    }

private:
    // Wet EQ path: 4x oversample -> WDF + HF-cut biquad + transformer -> down.
    void processWetBlock (float* left, float* right, int numSamples) noexcept
    {
        // Wrap raw pointers in a non-owning AudioBlock
        float* channels[2] = { left, right };
        juce::dsp::AudioBlock<float> block (channels, 2, static_cast<size_t> (numSamples));

        // Upsample to 4x rate
        auto upBlock = oversampler.processSamplesUp (block);

        const size_t upSamples = upBlock.getNumSamples();
        float* upL = upBlock.getChannelPointer (0);
        float* upR = upBlock.getChannelPointer (1);

        for (size_t i = 0; i < upSamples; ++i)
        {
            // WDF EQ (LF + HF boost) - runs at upsampled rate per prepare()
            double outL = chanL.processSample (static_cast<double> (upL[i]));
            double outR = chanR.processSample (static_cast<double> (upR[i]));

            if (diagOutput != 0)   // raw readout/node probe (artifact localisation)
            {
                upL[i] = static_cast<float> (diagProbe (chanL));
                upR[i] = static_cast<float> (diagProbe (chanR));
                continue;
            }

            // HF cut biquad - coefficients are computed at upsampled rate too
            outL = static_cast<double> (hfCutL.processSample (static_cast<float> (outL)));
            outR = static_cast<double> (hfCutR.processSample (static_cast<float> (outR)));

            // Inter-stage transformer (HS-29), normalised to unity in-band gain
            if (transformerEnabled)
            {
                outL = xfmrL.process (outL) / xfmrNorm;
                outR = xfmrR.process (outR) / xfmrNorm;
            }

            upL[i] = static_cast<float> (outL);
            upR[i] = static_cast<float> (outR);
        }

        // Downsample back to host rate (writes into block)
        oversampler.processSamplesDown (block);
    }

    // Push the block's dry samples through the latency-comp ring, writing the
    // delayed output (input[n-L]) into dryScratch. With L == 0 the dry signal
    // passes straight through (no oversampler latency to compensate).
    void captureDelayedDry (const float* left, const float* right, int numSamples) noexcept
    {
        if (latencySamples == 0)
        {
            std::memcpy (dryScratchL.data(), left,  sizeof (float) * static_cast<size_t> (numSamples));
            std::memcpy (dryScratchR.data(), right, sizeof (float) * static_cast<size_t> (numSamples));
            return;
        }

        for (int n = 0; n < numSamples; ++n)
        {
            const size_t p = static_cast<size_t> (dryDelayPos);
            dryScratchL[static_cast<size_t> (n)] = dryDelayL[p];
            dryScratchR[static_cast<size_t> (n)] = dryDelayR[p];
            dryDelayL[p] = left[n];
            dryDelayR[p] = right[n];
            dryDelayPos = (dryDelayPos + 1) % latencySamples;
        }
    }

    // Keep the delay ring primed when the dry output isn't needed (fully wet),
    // so a later bypass fade starts with correct history. Cheap; no scratch write.
    void advanceDelayRing (const float* left, const float* right, int numSamples) noexcept
    {
        if (latencySamples == 0) return;

        for (int n = 0; n < numSamples; ++n)
        {
            const size_t p = static_cast<size_t> (dryDelayPos);
            dryDelayL[p] = left[n];
            dryDelayR[p] = right[n];
            dryDelayPos = (dryDelayPos + 1) % latencySamples;
        }
    }

private:
    // -----------------------------------------------------------------------
    //  State
    // -----------------------------------------------------------------------
    // `sampleRate` is the UPSAMPLED rate (host fs * 4). All WDF + biquad
    // coefficient calculations downstream read this value.
    double sampleRate = 44100.0;

    // 2 channels, factor 2^2 = 4x, polyphase IIR half-band, maximum quality
    juce::dsp::Oversampling<float> oversampler {
        2,
        2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true
    };

    EQP1AChannel chanL, chanR;

    juce::dsp::IIR::Filter<float> hfCutL, hfCutR;

    // Persistent shelf coefficients, allocated once and mutated in place by
    // updateHfCutCoefficients() so HF Atten automation never allocates on the
    // audio thread. Shared by both channel filters. Initialised to identity.
    juce::dsp::IIR::Coefficients<float>::Ptr hfCutCoeffs
        { new juce::dsp::IIR::Coefficients<float> (1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f) };

    // Inter-stage transformer (Triad HS-29) — one per channel.  Applied as a
    // normalised post-element on the EQ output (see InterstageXfmr docs).
    InterstageXfmr xfmrL, xfmrR;
    bool   transformerEnabled = true;
    double xfmrNorm           = 1.0;   // in-band normalisation = nominalGain()
    int    diagOutput         = 0;     // 0 normal; 1 V(J), 2 V(R3), 3 Rout, 4 node6, 5 node7

    // --- Bypass crossfade with dry-path latency compensation -----------------
    // A hard bypass toggle hard-switches between the oversampler-delayed wet
    // output and the raw dry input — two discontinuities at once (an amplitude
    // /phase step AND an L-sample time jump from the oversampler group delay),
    // which lands as a pop/click. Instead we:
    //   1. delay the dry signal by the oversampler latency L so dry and wet are
    //      time-aligned (both represent input[n-L]); and
    //   2. crossfade a dry/wet mix over ~10 ms on every toggle.
    // bypassMix: 0 = fully wet (active), 1 = fully dry (bypassed). `bypass` is
    // the target the mix ramps toward. The dry delay ring is fed every block so
    // its history is always valid the instant a fade begins.
    float              bypassMix      = 0.0f;   // current mix (0 settled = active)
    float              bypassInc      = 1.0f;   // per host-sample ramp step
    int                latencySamples = 0;      // oversampler group delay (host rate)
    int                dryDelayPos    = 0;      // ring write/read cursor
    std::vector<float> dryDelayL, dryDelayR;    // dry latency-comp ring (size L)
    std::vector<float> dryScratchL, dryScratchR; // per-block delayed-dry output

    inline double diagProbe (const EQP1AChannel& ch) const noexcept
    {
        switch (diagOutput)
        {
            case 1:  return ch.VJ();
            case 2:  return ch.VR3();
            case 3:  return ch.Vrout();
            case 4:  return ch.Vnode6();
            case 5:  return ch.Vnode7();
            case 6:  return ch.Vnode4();
            case 7:  return ch.Vnode3();
            default: return 0.0;
        }
    }

    // Cached parameter values
    bool  bypass         = false;
    int   lfFreqIdx      = 2;    // default: 40 Hz
    float lfBoostDb      = 0.0f;
    float lfAttenDb      = 0.0f;
    int   hfFreqIdx      = 3;    // default: 8 kHz
    float hfBoostDb      = 0.0f;
    float hfBw           = 0.3f;
    int   hfAttenFreqIdx = 1;    // default: 10 kHz
    float hfAttenDb      = 0.0f;

    // -----------------------------------------------------------------------
    //  LF switched frequencies
    //  EQP-1A positions: 20 / 30 / 40 / 60 / 100 Hz
    //  (PULTEC_DEV.md specifies index 0–4; note circuit-topology.md lists
    //   original as 20/30/60/100, but PRD adds 40 Hz as index 2.)
    // -----------------------------------------------------------------------
    static constexpr double kLfFreqs[5] = { 20.0, 30.0, 40.0, 60.0, 100.0 };

    // LoFreqC2 values (from ABSounds reference get_CapValuesLoFreq):
    //   20 Hz → 2.2 µF, 30 Hz → 1.1 µF, 60 Hz → 560 nF, 100 Hz → 330 nF
    // For 40 Hz (added in PRD): interpolate geometrically between 30 and 60 Hz.
    //   geometric mean of 1.1µF and 560nF ≈ 785 nF
    static constexpr double kLoFreqC2[5] = {
        2.2e-6,   // 20 Hz
        1.1e-6,   // 30 Hz
        785.0e-9, // 40 Hz (interpolated)
        560.0e-9, // 60 Hz
        330.0e-9  // 100 Hz
    };

    // -----------------------------------------------------------------------
    //  HF boost switched frequencies
    //  EQP-1A positions: 3/4/5/8/10/12/16 kHz
    // -----------------------------------------------------------------------
    static constexpr double kHfFreqs[7] = {
        3000.0, 4000.0, 5000.0, 8000.0, 10000.0, 12000.0, 16000.0
    };

    // -----------------------------------------------------------------------
    //  HF cut switched frequencies and their capacitor values
    //  (from ABSounds reference)
    // -----------------------------------------------------------------------
    static constexpr double kHfCutFreqs[3]   = { 5000.0, 10000.0, 20000.0 };
    static constexpr double kHfCutCaps[3]    = { 270.0e-9, 135.0e-9, 68.0e-9 };

    // -----------------------------------------------------------------------
    //  Coefficient update helpers
    // -----------------------------------------------------------------------

    /** Apply LF frequency switch: update both C1/C2 capacitors in both channels. */
    void updateLfCoefficients()
    {
        double c2 = kLoFreqC2[lfFreqIdx];
        double c1 = c2 / 30.0;

        chanL.LoFreqC2.setCapacitanceValue (c2);
        chanL.LoFreqC1.setCapacitanceValue (c1);
        chanR.LoFreqC2.setCapacitanceValue (c2);
        chanR.LoFreqC1.setCapacitanceValue (c1);
    }

    /**
     * Map LF boost dB (0–16) → RBoostLo resistance.
     * At maximum boost (16 dB), RBoostLo approaches 0 (full engagement).
     * At 0 dB, RBoostLo = RBOOSTLO_MAX (circuit bypassed).
     * The mapping follows the pot-wiper convention of the reference:
     *   normalised = boostDb / 16.0;  R = normalised * RBOOSTLO_MAX
     * (higher normalised value = more resistance = less boost; invert)
     * The reference uses: R = (1 - amount) * MAX where amount = 0→1.
     * We need: amount = 0 → no boost, amount = 1 → full boost.
     * So: normalised = lfBoostDb / 16.0
     *     R = (1 - normalised) * RBOOSTLO_MAX + 1.0   (1 Ω floor avoids zero)
     */
    void updateLfBoost()
    {
        // ABSounds convention: norm=0 → R≈0 (near-short shunts J, no bypass of PAD),
        // norm=1 → R=MAX (high-Z at J, low-freq content bypasses PAD → apparent boost).
        double norm = static_cast<double> (lfBoostDb) / 16.0;
        double R    = std::max (1.0, norm * EQP1AChannel::RBOOSTLO_MAX);
        chanL.RBoostLo.setResistanceValue (R);
        chanR.RBoostLo.setResistanceValue (R);
    }

    void updateLfCut()
    {
        double norm = static_cast<double> (lfAttenDb) / 16.0;
        double R    = std::max (1.0, norm * EQP1AChannel::RCUTLO_MAX);
        chanL.RCutLo.setResistanceValue (R);
        chanR.RCutLo.setResistanceValue (R);
    }

    /**
     * Apply HF boost frequency switch: compute C1/L1 from RATIO_L1_C1.
     * Formula (from ABSounds reference get_CompValuesHiBoost):
     *   C1 = sqrt(1 / (RATIO * (2π·fc)²))
     *   L1 = RATIO * C1
     * Verified: fc=5 kHz → C1≈15 nF, L1≈67.5 mH (matches SMC 2024 table).
     */
    void updateHfBoostCoefficients()
    {
        double fc    = kHfFreqs[hfFreqIdx];
        double omega = 2.0 * juce::MathConstants<double>::pi * fc;
        double c1    = std::sqrt (1.0 / (EQP1AChannel::RATIO_L1_C1 * omega * omega));
        double l1    = EQP1AChannel::RATIO_L1_C1 * c1;

        chanL.C1.setCapacitanceValue (c1);
        chanL.L1.setInductanceValue  (l1);
        chanR.C1.setCapacitanceValue (c1);
        chanR.L1.setInductanceValue  (l1);
    }

    /**
     * Map HF boost dB (0–16) → RBoostHi1/RBoostHi2 (complementary pot).
     * Reference: RBoostHi1 = (1 - norm) * MAX,  RBoostHi2 = norm * MAX.
     */
    void updateHfBoost()
    {
        double norm = static_cast<double> (hfBoostDb) / 16.0;
        double R1   = std::max (1.0, (1.0 - norm) * EQP1AChannel::RBOOSTHI_MAX);
        double R2   = std::max (1.0,        norm   * EQP1AChannel::RBOOSTHI_MAX);
        chanL.RBoostHi1.setResistanceValue (R1);
        chanL.RBoostHi2.setResistanceValue (R2);
        chanR.RBoostHi1.setResistanceValue (R1);
        chanR.RBoostHi2.setResistanceValue (R2);
    }

    /**
     * Map HF bandwidth (0–1) → RQ resistance.
     * bw=0 → widest → highest RQ (most damping); bw=1 → narrowest → RQ=0 (highest Q).
     * Reference: R = bw * RQ_MAX.
     * (Higher bw value = narrower bandwidth = lower resistance = less damping = higher Q)
     */
    void updateHfBQ()
    {
        // In the reference: set_HiBQ sets R = new_HiBQ * RQ_MAX
        // But Q = f / bandwidth, and lower resistance = narrower peak.
        // The research doc says bw=0 → Q=0.3 (wide), bw=1 → Q=3.0 (narrow).
        // Map: norm=0 → maximum resistance (most damping, wide), norm=1 → min.
        double norm = static_cast<double> (hfBw);
        double R    = std::max (1.0, (1.0 - norm) * EQP1AChannel::RQ_MAX);
        chanL.RQ.setResistanceValue (R);
        chanR.RQ.setResistanceValue (R);
    }

    /** Update HF cut CHiCut capacitor and refresh HF cut biquad coefficients. */
    void updateHfCutCoefficients()
    {
        // Update WDF element (CHiCut is in the main WDF tree — it contributes
        // to the passive network even when hfAttenDb = 0).
        double cap = kHfCutCaps[hfAttenFreqIdx];
        chanL.CHiCut.setCapacitanceValue (cap);
        chanR.CHiCut.setCapacitanceValue (cap);

        // Additionally update the biquad HF cut for the RC shelf model.
        // The WDF tree already handles the loading interaction; this biquad
        // adds the explicit attenuation shelf at the user-selected level.
        double fc        = kHfCutFreqs[hfAttenFreqIdx];
        float  gainLinear = juce::Decibels::decibelsToGain (-hfAttenDb);

        // Use makeLowShelf to cut HF content: it attenuates frequencies above
        // the corner. "Low shelf" here means "keep the low part, cut the high"
        // — use makeHighShelf with a gain < 1 (attenuation above corner).
        //
        // Runs on the audio thread while HF Atten automation moves, so the
        // allocating Coefficients::makeHighShelf (new + ref-count free of the
        // old object every call) is off limits: compute with the allocation-free
        // ArrayCoefficients and write into the one persistent Coefficients
        // object both filters share.
        const auto arr = juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (
            sampleRate, fc, 0.7071f, gainLinear);

        const float a0inv = 1.0f / arr[3];
        float* raw = hfCutCoeffs->getRawCoefficients();   // { b0, b1, b2, a1, a2 } normalised
        raw[0] = arr[0] * a0inv;
        raw[1] = arr[1] * a0inv;
        raw[2] = arr[2] * a0inv;
        raw[3] = arr[4] * a0inv;
        raw[4] = arr[5] * a0inv;

        if (hfCutL.coefficients != hfCutCoeffs)
        {
            hfCutL.coefficients = hfCutCoeffs;
            hfCutR.coefficients = hfCutCoeffs;
        }
    }
};
