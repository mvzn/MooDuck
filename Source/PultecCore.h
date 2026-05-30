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
#include <tuple>

namespace wdft = chowdsp::wdft;

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
    // Insertion-loss compensation.  The original 15.4882 figure inherited
    // from the ABSounds reference left a constant ~-0.65 dB offset across
    // the band at zero settings (see PULTEC_DEV.md, "Open issue - baseline
    // frequency response", root cause #2).  Combined with 4x oversampling
    // (which removes the HF Nyquist-warp droop) and a small downward trim
    // to centre the residual ~+0.24 dB tank bump at the 8 kHz boost corner,
    // 16.598 = 15.4882 * 10^(0.6/20) lands the baseline within +/- 0.2 dB
    // of 0 dB at 20 Hz / 200 Hz / 1 kHz / 8 kHz and +/- 1 dB out to 20 kHz.
    static constexpr double MAKEUP_GAIN  =  16.598;

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

    void prepare (double sr, int maxBlockSize)
    {
        // Run the WDF tree and HF-cut biquad at 4x the host sample rate so
        // bilinear Nyquist warping in the WDF capacitor/inductor primitives
        // sits well above the audio band (see PULTEC_DEV.md "Open issue -
        // baseline frequency response").
        sampleRate = sr * 4.0;

        oversampler.initProcessing (static_cast<size_t> (maxBlockSize));
        oversampler.reset();

        chanL.prepare (sampleRate);
        chanR.prepare (sampleRate);

        hfCutL.reset();
        hfCutR.reset();
        updateLfCoefficients();
        // Sync LF pot resistors with their default parameter values (both = 0 dB →
        // R = 1 Ω via the ABSounds convention).  The EQP1AChannel constructor
        // initialises them to RMAX; without this call they'd stay at RMAX and the
        // MAKEUP_GAIN calibration would be wrong whenever setLfFreqIndex() is called
        // before setLfBoostDb/setLfAttenDb.
        updateLfBoost();
        updateLfCut();
        updateHfBoostCoefficients();
        updateHfCutCoefficients();
    }

    // -----------------------------------------------------------------------
    //  Parameter setters — call from audio thread only
    //  Each setter checks for change before touching the WDF tree.
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
        if (bypass) return;   // pure passthrough

        // WDF EQ (LF + HF boost)
        double outL = chanL.processSample (static_cast<double> (left));
        double outR = chanR.processSample (static_cast<double> (right));

        // HF cut biquad (applied after WDF)
        left  = hfCutL.processSample (static_cast<float> (outL));
        right = hfCutR.processSample (static_cast<float> (outR));
    }

    /** Process a stereo block through the 4x-oversampled EQP-1A network.
     *  Signal flow: upsample -> WDF EQ + HF-cut biquad (per upsampled sample)
     *  -> downsample.  Audio-thread-safe: the oversampler is pre-allocated
     *  in prepare(); no allocation occurs here.
     */
    void processBlock (float* left, float* right, int numSamples) noexcept
    {
        if (bypass) return;   // pure passthrough — no oversampling overhead

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

            // HF cut biquad - coefficients are computed at upsampled rate too
            upL[i] = hfCutL.processSample (static_cast<float> (outL));
            upR[i] = hfCutR.processSample (static_cast<float> (outR));
        }

        // Downsample back to host rate (writes into block)
        oversampler.processSamplesDown (block);
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
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
            sampleRate, fc, 0.7071f, gainLinear);

        hfCutL.coefficients = coeffs;
        hfCutR.coefficients = coeffs;
    }
};
