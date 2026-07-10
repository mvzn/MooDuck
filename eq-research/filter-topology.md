# Pultec EQP-1A Digital Filter Topology — Technical Research

**Date:** 2026-05-28  
**Scope:** JUCE 8.0.6 C++ implementation on Linux x86_64  
**Target:** Stereo VST3 equalizer plugin modeled on the Pultec EQP-1A passive program equalizer

---

## 1. Local JUCE DSP Module — WDF Audit

### 1.1 Local fork (`/home/mokram/JUCE/modules/juce_dsp/`)

Bash search was blocked by the sandbox, but the cmake build-dep tree was inspectable.

### 1.2 CMake stock JUCE 8.0.6 (`build-cmake/_deps/juce-src/modules/juce_dsp/`)

The full directory listing of JUCE 8's `juce_dsp` module is:

```
containers/   filter_design/   frequency/   maths/   native/   processors/   widgets/
juce_dsp.cpp  juce_dsp.h  juce_dsp.mm
```

Headers present in `processors/`:
- `juce_IIRFilter.h`, `juce_IIRFilter_Impl.h`
- `juce_StateVariableTPTFilter.h`
- `juce_StateVariableFilter.h` (deprecated)
- `juce_FirstOrderTPTFilter.h`
- `juce_LinkwitzRileyFilter.h`
- `juce_Oversampling.h` (supports 2x/4x/8x/16x, FIR or IIR half-band)
- `juce_BallisticsFilter.h`, `juce_Compressor.h`, `juce_LadderFilter.h`, etc.

**Conclusion: JUCE 8 has zero WDF classes.** No `WDF`, `wave_digital`, or `WaveDigital` symbols appear anywhere in `juce_dsp`. The module provides only biquad IIR, TPT state-variable, and convolution primitives. WDF must be provided by a third-party library.

---

## 2. chowdsp_wdf — Library Assessment

**Repository:** https://github.com/Chowdhury-DSP/chowdsp_wdf  
**Documentation:** https://ccrma.stanford.edu/~jatin/chowdsp/chowdsp_wdf/  
**Paper:** "chowdsp_wdf: An Advanced C++ Library for Wave Digital Circuit Modelling" (Chowdhury, DAFx 2022) — https://www.researchgate.net/publication/364689194

### 2.1 Integration

- **Header-only:** Yes. Include `include/chowdsp_wdf/chowdsp_wdf.h` and it compiles as part of the project. No linking step needed.
- **CMake path (preferred):** `FetchContent_Declare(chowdsp_wdf GIT_REPOSITORY https://github.com/Chowdhury-DSP/chowdsp_wdf.git GIT_TAG v1.0.0)` then `target_link_libraries(MooDuck PRIVATE chowdsp_wdf)`. The CMakeLists.txt already uses FetchContent for JUCE, so the pattern is established.
- **Manual vendoring:** Copy `include/chowdsp_wdf/` into `Source/` or `ThirdParty/`, add it to include paths in the `.jucer`.

### 2.2 License

**BSD 3-Clause.** Compatible with proprietary and GPL projects alike. No copyleft propagation. Safe to vendor.

### 2.3 Maintenance status

Last tagged release: v1.0.0 (December 17, 2022). Documentation regenerated October 2024. The library is stable and not under active feature development — this is a positive signal for a stable DSP library; the WDF formalism itself is mature.

### 2.4 SIMD support

Optional integration with xsimd 8.0.0+. When enabled, `RtypeAdaptor` matrix multiplies use SIMD intrinsics. For scalar `double` processing (as in MooDuck's existing `ZVAFilter`), xsimd is not required.

### 2.5 JUCE 8 compatibility

The library has no JUCE dependency whatsoever. It is pure C++17 template headers. Works alongside any JUCE version. The EQP-WDF-1A reference plugin uses `juce::dsp::Oversampling` from JUCE alongside chowdsp_wdf, confirming the combination is functional.

### 2.6 API classes (namespace `chowdsp::wdft`)

The compile-time (fixed-topology) namespace `chowdsp::wdft` provides:

| Class | Description |
|---|---|
| `ResistorT<T>` | One-port resistor |
| `CapacitorT<T>` | One-port capacitor (stateful, needs `prepare(fs)`) |
| `CapacitorAlphaT<T>` | Capacitor with alpha-transform anti-imaging |
| `InductorT<T>` | One-port inductor (stateful, needs `prepare(fs)`) |
| `InductorAlphaT<T>` | Inductor with alpha-transform |
| `WDFSeriesT<T, P1, P2>` | Two-port series adaptor |
| `WDFParallelT<T, P1, P2>` | Two-port parallel adaptor |
| `PolarityInverterT<T, P1>` | Sign inversion |
| `IdealVoltageSourceT<T, P1>` | Root: ideal voltage source |
| `ResistiveVoltageSourceT<T>` | Non-root voltage source with source resistance |
| `RtypeAdaptor<T, N, ImpedanceFunc>` | N-port R-type for non-series/parallel junctions |
| `RootRtypeAdaptor<T, N, ImpedanceFunc>` | Root R-type adaptor |
| `DiodeT<T, P1>`, `DiodePairT<T, P1>` | Nonlinear diode one-ports |
| `SwitchT<T, P1>` | Ideal switch |

The runtime-flexible namespace `chowdsp::wdf` has analogous non-template classes for dynamic circuit construction.

**Reading voltage across any element:** `wdft::voltage<double>(element)`  
**Reading current through any element:** `wdft::current<double>(element)`

**The core per-sample loop (fixed topology):**
```cpp
Vin.setVoltage(xn);
Vin.incident(root.reflected());
root.incident(Vin.reflected());
double yn = wdft::voltage<double>(outputElement);
```

---

## 3. Pultec EQP-1A Circuit Topology

### 3.1 Physical circuit overview

The EQP-1A is a passive LC equalizer with a downstream tube makeup-gain amplifier. The passive filter network imposes insertion loss (roughly 20–25 dB); the 12AX7 triode stage restores unity gain. The filter uses **bridge-T and parallel-resonant LC networks** — not biquad topologies.

**Four control sections:**

| Section | Frequencies | Controls |
|---|---|---|
| LF Boost | 20, 30, 60, 100 Hz | Boost amount (shelf+resonance) |
| LF Cut | 20, 30, 60, 100 Hz | Attenuation amount (shelf) |
| HF Boost | 3, 4, 5, 8, 10, 12, 16 kHz | Boost amount + bandwidth (Q) |
| HF Cut | 5, 10, 20 kHz | Attenuation amount |

### 3.2 The simultaneous boost+cut interaction ("Pultec Trick")

This is the single most important feature to model correctly. When both LF Boost and LF Cut are set to the same frequency:

- The boost control drives a **wider, resonant shelf** centered at the selected frequency.
- The cut control drives a **narrower notch** slightly above the same frequency.
- Because the boost network has a broader Q than the cut network, simultaneous operation does not produce a flat response. Instead, it produces a **sub-bass boost with a tight mid-bass dip** — which is why engineers use it on kick drums to add weight while removing mud.

**This interaction is impossible to reproduce with two independent biquad filters in series.** The boost and cut networks share impedance through the same passive junction. Only a circuit-level model (WDF) or a derived combined H(s) transfer function captures this correctly.

### 3.3 WDF element graph (from EQP-WDF-1A, Barrera & Eijas, SMC 2024)

The reference implementation at https://github.com/ABSounds/EQP-WDF-1A maps the full passive network to the following WDF tree. Variable resistors implement potentiometer wiper positions (normalized 0–1). Variable capacitors and inductors implement the switched frequency bands.

**High-frequency boost sub-network (ports B–D):**
```
RBoostHi1 (variable, max 12 kΩ)
  in series with [
    C1 (variable, freq-dependent) in series with L1 (variable, freq-dependent)
    in parallel with
    (RQ in series with RQ_offset) in parallel with RBoostHi2 (variable)
  ]
```

**High-frequency cut sub-network (ports E–G):**
```
RCutHi1 (variable, max 1 kΩ)
  in series with [
    RCutHi2 (variable, max 1 kΩ)
    in parallel with
    CHiCut (variable, switched: 270 nF @ 5 kHz, 135 nF @ 10 kHz, 68 nF @ 20 kHz)
  ]
with fixed R1 = 91 Ω
```

**Low-frequency sub-network (ports H–J):**
```
[LoFreqC1 (variable) in parallel with RCutLo (variable, max 110 kΩ)]
  in series with [R2 (1 kΩ) in series with R3 (10 kΩ)]
  as one branch, in parallel with [
    RBoostLo (variable, max 8.2 kΩ) in parallel with LoFreqC2 (variable)
  ]
```

**Full topology (adaptor tree):**

| Adaptor | Type | Children |
|---|---|---|
| `SCLc` | Series | C1, L1 |
| `SRQ` | Series | RQ, RQ_offset |
| `C` | Series | SCLc, SRQ |
| `pCD` | Parallel | C, RBoostHi2 |
| `H` | Parallel | LoFreqC1, RCutLo |
| `I` | Series | R2, R3 |
| `J` | Parallel | RBoostLo, LoFreqC2 |
| `pFG` | Parallel | RCutHi2, CHiCut |
| `sEFG` | Series | RCutHi1, pFG |
| `sHI` | Series | H, I |
| `pEFGHI` | Parallel | sEFG, sHI |
| `S1` | Series | RBoostHi1, pCD |
| `S2` | Series | S1, pEFGHI |
| `S3` | Series | S2, J |
| `Vin` | IdealVoltageSource (root) | S3 |

The output is taken as a weighted sum of voltages at two nodes:
```cpp
return (-wdft::voltage<double>(J) + wdft::voltage<double>(R3)) * 15.4882;
// 15.4882 compensates the passive insertion loss to approximate unity gain
```

The 15.4882 scale factor models the makeup gain of the 12AX7 triode amplifier as a linear gain stage. A nonlinear triode model (see Section 5) can replace this constant.

**No R-type adaptors are required** in the optimized version. The circuit was reorganized to decompose all junctions into binary series/parallel pairs, enabling the efficient compile-time tree traversal in `chowdsp::wdft`.

> **Known limitation of this output formula (2026-05-29):** Because `voltage(J)` is in the LF sub-network's parallel node and `voltage(R3)` is in the LF-cut leg, any change to the HF Boost pot (which redistributes impedance across S3) shifts the divider ratio seen at both nodes — even at frequencies far below the HF Selector. The result is a parasitic **~5.5 dB LF shelf rise whenever the HF Boost section is engaged, independent of boost amount, BW, or Atten Sel**. The real EQP-1A is immune because its inter-stage transformer presents a high primary impedance to the EQ output, decoupling the HF Boost pot from the makeup-amp loading. The fix path (R-type transformer adaptor or output-stage HP) is tracked in `transformer-planning.md`.

### 3.4 Component value formulas

**HF boost (L1/C1 pair, resonant at fc_hi):**
```
RATIO_L1_C1 = constant; ABSounds reference uses 12e6 (H·F units; Z0 = √RATIO ≈ 3464 Ω)
C1 = sqrt(1 / (RATIO_L1_C1 * (2π·fc_hi)²))
L1 = RATIO_L1_C1 * C1
```

> **Implementation deviation (2026-05-29):** Our `PultecCore.h` currently uses `RATIO_L1_C1 = 4.5e6` (Z0 ≈ 2121 Ω). This was derived from a partial draft of this document that read "~160" — an early placeholder that never tracked the ABSounds reference value. Testing both values against the HF Boost shape diagnostic showed identical LF coupling behavior, so the constant is not the root cause of the open issue documented in `PULTEC_DEV.md`. The deviation is noted here for completeness; before changing it, re-tune `MAKEUP_GAIN` (the constant offset to baseline scales with `RATIO_L1_C1`) and re-run `test_pultec_frequency_response::T1`.

**LF capacitors (switched by frequency selector):**
```
20 Hz:  LoFreqC2 = X µF,   LoFreqC1 = LoFreqC2 / 30
30 Hz:  LoFreqC2 = Y µF,   LoFreqC1 = LoFreqC2 / 30
60 Hz:  LoFreqC2 = Z µF,   LoFreqC1 = LoFreqC2 / 30
100 Hz: LoFreqC2 = W µF,   LoFreqC1 = LoFreqC2 / 30
```
(Exact values are in EQP1A.cpp `get_CapValuesLoFreq()`. The ratio LoFreqC1 = LoFreqC2/30 is fixed by the circuit.)

**HF cut capacitor (switched):**
```
5 kHz:  CHiCut = 270 nF
10 kHz: CHiCut = 135 nF
20 kHz: CHiCut = 68 nF
```

---

## 4. Topology Evaluation

### 4a. WDF — Recommended for HF Boost and LF sections

**Accuracy:** Models the exact passive LC network. The simultaneous boost+cut interaction emerges automatically from the shared junction impedances — no special-casing required. Comparison against LTSpice and the 2019 EQP-1A hardware reissue confirms marginal error at sample rates ≥ 88.2 kHz.

**Complexity:** Moderate. The tree structure is established at compile time via C++ templates (`WDFSeriesT<double, decltype(r1), decltype(c1)>`). Parameter changes are handled by calling `r.setResistanceValue(newR)` on variable resistors, which triggers automatic port-resistance re-propagation up the tree.

**Limitations:**
- Stateful elements (`CapacitorT`, `InductorT`) must call `prepare(sampleRate)` before processing. This already matches the MooDuck plugin pattern.
- At 44.1 kHz, the HF boost LC resonator accuracy degrades near Nyquist — 2x oversampling restores it. The EQP-WDF-1A plugin confirms this using `juce::dsp::Oversampling`.
- Variable resistors are updated per-parameter-change (not per-sample), so this is cheap.

**Verdict:** WDF is the only topology that models the simultaneous boost+cut interaction correctly. It is the recommended approach for the LF boost/cut section and the HF boost section.

### 4b. Bilinear-transform IIR biquads

**Current MooDuck approach.** Two `juce::dsp::IIR::Filter<float>` instances (or hand-rolled biquad) per band.

**Limitations for Pultec:**
1. Frequency warping: the analog prototype pole/zero locations are mapped nonlinearly to digital. For a 20 Hz shelf at 44.1 kHz the error is negligible, but at 10+ kHz it becomes audible.
2. The shelf shape is approximated. The Pultec LF boost produces a resonant peak at the shelf knee (from the LC resonator) that a standard `highShelf` bilinear biquad cannot reproduce.
3. Simultaneous boost+cut requires deriving a combined analog H(s) for each combination of boost/cut pot settings and band selection — combinatorially expensive and produces the wrong pole/zero positions unless the passive-network interaction is manually modeled.

**Where it is acceptable:** HF cut section only. The HF cut is a simple RC shelf (no inductor, no resonance). A standard `lowShelf` or `highShelf` biquad with bilinear prewarping gives adequate results for this one section.

### 4c. Orfanidis shelving filters

**Reference:** Orfanidis, S.J., "Digital Parametric Equalizer Design With Prescribed Nyquist-Frequency Gain," JAES, Vol. 45, pp. 444–455, 1997. — https://www.semanticscholar.org/paper/Digital-Parametric-Equalizer-Design-with-Prescribed-Orfanidis/b98f2ac93a6ef9b60e109960c6f2f00edde9bf64

**Key improvement over bilinear biquads:** The Orfanidis design constrains the filter gain at both DC and Nyquist independently, producing a shelf shape that matches the analog prototype at both endpoints and does not suffer from the bilinear gain-at-Nyquist distortion.

**Formula sketch for low shelf:**
```
Given: f0 (shelf midpoint), G0 (DC gain dB), G (shelf gain dB), GB (bandwidth gain dB = G - 3dB)
1. Compute analog bandwidth Omega_B from f0 using bilinear prewarping
2. Solve Chebyshev or Butterworth prototype in s-domain constrained at DC=G0, s=jΩ_B → G_B, s→∞ → G_inf
3. Apply bilinear to get digital biquad: H(z) = (b0 + b1·z^-1 + b2·z^-2) / (1 + a1·z^-1 + a2·z^-2)
```

The coefficient derivation is more involved than RBJ cookbook but produces correct Nyquist behavior. MATLAB's `shelvingEqualizerDesign` implements this.

**Verdict for Pultec:** Better than basic bilinear for the HF cut shelf, but still cannot model the resonant peak in the LF boost or the simultaneous interaction. Use Orfanidis for HF cut only if WDF integration is not desired; WDF remains superior for the LC sections.

### 4d. TPT / State-Variable (Zavalishin) — existing ZVAFilter

MooDuck already has `ZVAFilter` in `CrossoverCore.h` implementing the Zavalishin TPT SVF. This filter uses the TPT integrator:
```
g = tan(π·fc/fs)          // prewarp
alpha0 = 1 / (1 + 2R·g + g²)
alpha  = g
rho    = 2R + g
```
where `R = 1/(2Q)`.

**Strengths:** Numerically stable, zero-delay-feedback solved, no frequency warping at the tuned frequency. Well-suited for parametric bell curves and shelves.

**Limitations for Pultec:** The TPT SVF can implement a peak (bell) filter but cannot capture the Pultec LF boost's resonant shelf shape, which combines a low-shelf with a superimposed resonant peak at the shelf knee. The simultaneous boost+cut interaction is absent. The `ZVAFilter` would be a reasonable fallback for a CPU-minimal "inspired by Pultec" EQ that does not claim circuit-level accuracy.

**Possible reuse:** The HF boost section of the Pultec is a peaking/bell EQ (`kSVF_BP` type or bell mode). If WDF is not implemented, two ZVAFilters in combination (one shelf, one peak) can approximate it.

### 4e. Matched Z-transform

Maps analog poles and zeros directly to the z-plane: `z = e^(sT)`. Produces better pole-zero placement than bilinear for high-frequency poles, but:
- Does not preserve the DC or Nyquist gain of the analog filter.
- Adds zeros that the analog prototype does not have.
- The group-delay behavior is worse than TPT or Orfanidis for audio shelves.

**Verdict:** Not recommended for the Pultec. Bilinear with prewarping is superior for shelves; WDF is superior for the LC resonators.

---

## 5. Saturation / Tube Model

### 5.1 Koren triode model

Norman Koren's 1997/2003 compact SPICE model is the standard for audio tube triode simulation.

**Plate current formula:**

```
E1  = (Vpk / Kp) * ln(1 + exp(Kp * (1/mu + (Vgk + Vct) / sqrt(Kvb + Vpk²))))
Ip  = (E1^Ex * (1 + sign(E1))) / Kg1    [Amps]
```

where:
- `Vpk` — plate-to-cathode voltage
- `Vgk` — grid-to-cathode voltage (the signal input)
- `Vct` — contact potential (~0.33 V for 12AX7)
- `mu` — amplification factor (e.g. 100 for 12AX7)
- `Kp` — plate-voltage bowing factor (~600 for 12AX7)
- `Kvb` — knee voltage (~300 for 12AX7)
- `Kg1` — scale factor (~1060 for 12AX7)
- `Ex` — exponent (~1.4 for 12AX7)

The `sign(E1)` term prevents plate current from flowing when the grid is driven too positive (cutoff clamping).

**Per-sample C++ cost:** Three `std::exp`, one `std::log`, one `std::sqrt`, one `std::pow`. At 44.1 kHz with 2x oversampling that is ~88 200 evaluations/second/channel. With fast-math and a lookup table for `log(1 + exp(x))` (softplus), this is feasible in a real-time context. Reference: https://hal.science/hal-00811215/document

**Parameters for 12AX7 (ECC83, the Pultec tube):**

| Parameter | Value |
|---|---|
| mu | 100 |
| Ex | 1.4 |
| Kg1 | 1060 |
| Kp | 600 |
| Kvb | 300 |
| Vct | 0.33 |

### 5.2 Simplified even-harmonic waveshaper

For a CPU-minimal tube approximation, a composite waveshaper adding 2nd-harmonic content:

```cpp
// f(x) = tanh(x) + alpha * x^2
// alpha ~ 0.05–0.15 controls even-harmonic content
// DC offset correction needed: subtract mean
inline double tubeApprox(double x, double alpha) {
    return std::tanh(x) + alpha * x * x;
}
```

This adds even-harmonic distortion (characteristic of class-A tube stages) without modeling the exact operating point. It is adequate for "vibe" but will not produce the correct harmonic spectrum at different drive levels.

### 5.3 Transformer hysteresis

The EQP-1A's input and output transformers contribute low-order harmonic distortion and a slight low-frequency phase shift. A simplified model:

```cpp
// Simplified Preisach (saturable core): tanh with memory
// x_h[n] = tanh(x[n] + beta * x_h[n-1])   // where beta ~ 0.1
double transformerSat(double xn, double& state, double beta) {
    state = std::tanh(xn + beta * state);
    return state;
}
```

This captures soft-knee saturation with hysteresis memory but is not physically derived. For a first pass, omit transformer modeling and focus on the triode stage.

### 5.4 Oversampling for saturation

**Requirement:** `tanh(x)` is a smooth nonlinearity generating odd harmonics. Even with moderate drive (input amplitude ~1.0), harmonics extend to ~7–10× the input frequency. At 44.1 kHz the Nyquist limit folds these harmonics back into the audible band as aliasing.

**Minimum oversampling:** 2x is sufficient for mild saturation (drive < 6 dB above unity). At 2x the effective Nyquist is 44.1 kHz — harmonics above 44.1 kHz are filtered by the anti-imaging lowpass before downsampling.

**Practical recommendation:** 4x oversampling for the complete saturation chain (WDF passive network at native rate, nonlinearity at 4x). JUCE's `juce::dsp::Oversampling<double>` supports this natively:

```cpp
juce::dsp::Oversampling<double> oversampler { numChannels,
    2,  // 2 = 4x (factor = 2^order)
    juce::dsp::Oversampling<double>::filterHalfBandPolyphaseIIR,
    true,   // maximumQuality
    false }; // isInterleavedSamples
```

The EQP-WDF-1A reference plugin uses this approach. At 96 kHz native rate, 2x oversampling is unnecessary as the fundamental and harmonics are already well below Nyquist.

---

## 6. Concrete Per-Section Recommendation

### Section A: LF Boost (20/30/60/100 Hz) — WDF

**Topology:** `WDFParallelT` of `RBoostLo` with `LoFreqC2`, fed through the series `R2 + R3` junction.

**C++ structure:**

```cpp
#include "chowdsp_wdf/chowdsp_wdf.h"
namespace wdft = chowdsp::wdft;

struct LFBoostSection {
    // Variable elements (updated on parameter change)
    wdft::ResistorT<double> RBoostLo { 8200.0 };   // 0 to 8.2 kΩ
    wdft::ResistorT<double> RCutLo   { 110'000.0 }; // 0 to 110 kΩ

    // Frequency-switched capacitors (updated on band selection)
    wdft::CapacitorT<double> LoFreqC1 { 1.0e-6 };  // set in prepare()
    wdft::CapacitorT<double> LoFreqC2 { 30.0e-6 }; // set in prepare()

    // Fixed elements
    wdft::ResistorT<double> R2 { 1000.0 };
    wdft::ResistorT<double> R3 { 10'000.0 };

    // Adaptor tree (types inferred by compiler from element types)
    wdft::WDFParallelT<double, decltype(LoFreqC1), decltype(RCutLo)> H { LoFreqC1, RCutLo };
    wdft::WDFSeriesT<double,   decltype(R2),        decltype(R3)>     I { R2, R3 };
    wdft::WDFParallelT<double, decltype(RBoostLo),  decltype(LoFreqC2)> J { RBoostLo, LoFreqC2 };
    wdft::WDFSeriesT<double,   decltype(H),          decltype(I)>      sHI { H, I };
    wdft::WDFSeriesT<double,   decltype(sHI),        decltype(J)>      S_lf { sHI, J };
    // ... continues into full tree combined with HF sections

    void prepare(double sampleRate) {
        LoFreqC1.prepare(sampleRate);
        LoFreqC2.prepare(sampleRate);
    }

    void setLoBoost(float amount) {  // amount: 0.0–1.0
        RBoostLo.setResistanceValue(amount * 8200.0 + 1.0); // avoid zero
    }

    void setLoCut(float amount) {
        RCutLo.setResistanceValue((1.0f - amount) * 110'000.0 + 1.0);
    }

    void setLoFreq(float freqHz) {
        // Values from circuit: ratio C1 = C2/30 is fixed
        double C2 = computeC2(freqHz);  // lookup table: 20→X, 30→Y, 60→Z, 100→W
        LoFreqC2.setCapacitanceValue(C2);
        LoFreqC1.setCapacitanceValue(C2 / 30.0);
    }
};
```

**Why WDF here:** The LF cut network (RCutLo + LoFreqC1) and LF boost network (RBoostLo + LoFreqC2) share the same output node via R2/R3. The simultaneous boost+cut interaction is captured by the shared junction — the wider boost Q vs. narrower cut Q interaction falls out of the impedance arithmetic automatically.

### Section B: LF Cut (20/30/60/100 Hz) — WDF (unified with LF Boost)

The cut section is not an independent filter. It is the parallel branch `H = WDFParallelT(LoFreqC1, RCutLo)` in the same WDF tree as the boost. Setting `RCutLo` to a low value pulls down that branch, notching the frequency. Because `LoFreqC1` and `LoFreqC2` share the same switched frequency selector, they track together.

**No separate implementation needed.** The LF boost and cut are the same `LFSection` WDF tree with two independently settable variable resistors.

### Section C: HF Boost (3–16 kHz, variable Q) — WDF

**Topology:** LC resonator (`C1` series `L1`) in parallel with Q-control resistors, in series with wiper resistor `RBoostHi1`. This is a parallel-resonant bandpass section whose center frequency tracks `fc_hi`.

```cpp
struct HFBoostSection {
    wdft::ResistorT<double>  RBoostHi1 { 6000.0 }; // wiper, 0–12kΩ
    wdft::ResistorT<double>  RBoostHi2 { 6000.0 }; // other wiper end
    wdft::ResistorT<double>  RQ        { 1000.0 }; // Q control
    wdft::ResistorT<double>  RQ_offset { 100.0 };

    wdft::CapacitorT<double> C1 { 1.0e-9 };  // set per frequency
    wdft::InductorT<double>  L1 { 1.0e-3 };  // set per frequency

    wdft::WDFSeriesT<double, decltype(C1),    decltype(L1)>    SCLc { C1, L1 };
    wdft::WDFSeriesT<double, decltype(RQ),    decltype(RQ_offset)> SRQ { RQ, RQ_offset };
    wdft::WDFSeriesT<double, decltype(SCLc),  decltype(SRQ)>   C_adaptor { SCLc, SRQ };
    wdft::WDFParallelT<double, decltype(C_adaptor), decltype(RBoostHi2)> pCD { C_adaptor, RBoostHi2 };
    wdft::WDFSeriesT<double, decltype(RBoostHi1),   decltype(pCD)>       S1  { RBoostHi1, pCD };

    void prepare(double sampleRate) {
        C1.prepare(sampleRate);
        L1.prepare(sampleRate);
    }

    void setHiBoostFreq(float fcHz) {
        // RATIO_L1_C1 derived from circuit (~160 for EQP-1A)
        constexpr double RATIO = 160.0;
        double omega = 2.0 * M_PI * fcHz;
        double c1_val = std::sqrt(1.0 / (RATIO * omega * omega));
        double l1_val = RATIO * c1_val;
        C1.setCapacitanceValue(c1_val);
        L1.setInductanceValue(l1_val);
    }

    void setHiBoost(float amount) {  // 0–1 maps wiper position
        RBoostHi1.setResistanceValue((1.0 - amount) * 12'000.0 + 1.0);
        RBoostHi2.setResistanceValue(amount * 12'000.0 + 1.0);
    }

    void setHiBQ(float q) {
        // Higher Q = lower RQ value (shaper resonance)
        RQ.setResistanceValue(juce::jmap(q, 0.0f, 1.0f, 2000.0f, 200.0f));
    }
};
```

### Section D: HF Cut (5/10/20 kHz) — Orfanidis High Shelf (or bilinear biquad)

The HF cut section is a simple RC shelf with no inductor. There is no resonance and no simultaneous-interaction effect. A standard biquad is adequate here.

**Recommended approach:** Bilinear-transform low-shelf biquad with prewarping, using RBJ cookbook coefficients. Or, for better Nyquist behavior, an Orfanidis shelf.

**RBJ low-shelf (attenuating, applied after the HF cut pot):**

```cpp
// From Audio EQ Cookbook: https://webaudio.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html
// Low shelf, G_dB = cut amount (negative), fc = selected frequency
void computeLowShelfCoeffs(double fc, double Gdb, double sampleRate,
                            double* b, double* a) {
    double A  = std::pow(10.0, Gdb / 40.0);
    double w0 = 2.0 * M_PI * fc / sampleRate;
    double cw = std::cos(w0), sw = std::sin(w0);
    double S  = 1.0;  // shelf slope = 1
    double alpha = sw / 2.0 * std::sqrt((A + 1.0/A) * (1.0/S - 1.0) + 2.0);

    double b0 =   A * ((A+1) - (A-1)*cw + 2*std::sqrt(A)*alpha);
    double b1 = 2*A * ((A-1) - (A+1)*cw);
    double b2 =   A * ((A+1) - (A-1)*cw - 2*std::sqrt(A)*alpha);
    double a0 =        (A+1) + (A-1)*cw + 2*std::sqrt(A)*alpha;
    double a1 =  -2 * ((A-1) + (A+1)*cw);
    double a2 =        (A+1) + (A-1)*cw - 2*std::sqrt(A)*alpha;

    b[0] = b0/a0;  b[1] = b1/a0;  b[2] = b2/a0;
    a[0] = 1.0;    a[1] = a1/a0;  a[2] = a2/a0;
}
```

**Alternative using juce::dsp::IIR:**
```cpp
auto coeffs = juce::dsp::IIR::Coefficients<double>::makeLowShelf(
    sampleRate, fc, 0.7071, juce::Decibels::decibelsToGain(Gdb));
filter.coefficients = coeffs;
```

Note: `makeLowShelf` applies a boost-at-low frequencies. For the Pultec HF cut, the shelf should attenuate highs — use `makeHighShelf` with a negative gain, or `makeLowShelf` with gain > 1 (if cutting above the shelf, consider `makePeakFilter` instead). Verify against LTSpice simulation for the specific network.

### Section E: Makeup Gain / Tube Stage — Linear or Nonlinear

**Minimum viable (linear):** Multiply by 15.4882 (as in the reference implementation). This is the passive insertion-loss compensation and approximates the tube at unity drive.

**Nonlinear (Koren model):** Implement as a per-sample waveshaper at 2x or 4x oversampling. A simplified version operating around the 12AX7 quiescent point:

```cpp
// Koren 12AX7 parameters
constexpr double MU  = 100.0, EX = 1.4, KG1 = 1060.0;
constexpr double KP  = 600.0, KVB = 300.0, VCT = 0.33;
constexpr double VPK_QUIESCENT = 250.0;   // plate supply voltage (V)
constexpr double VGK_QUIESCENT = -1.5;   // grid bias (V)

double korenTriodeIp(double Vgk, double Vpk) {
    double inner = KP * (1.0/MU + (Vgk + VCT) / std::sqrt(KVB + Vpk * Vpk));
    double E1    = (Vpk / KP) * std::log1p(std::exp(inner)); // log1p(exp(x)) = softplus
    double Ip    = std::pow(std::max(E1, 0.0), EX) / KG1;
    return Ip;
}

// Waveshaper: input is audio signal scaled to represent delta-Vgk around bias point
// Requires a load-line analysis to convert Ip → Vout
// For simplicity: approximate as nonlinear gain around operating point
double tubeSaturator(double xn, double& Ip_prev, double RL) {
    double Vgk = VGK_QUIESCENT + xn * 1.5; // scale input to grid-voltage range
    double Ip  = korenTriodeIp(Vgk, VPK_QUIESCENT - Ip_prev * RL);
    Ip_prev    = Ip;
    return -(Ip * RL - 250.0 * (1.0 - VGK_QUIESCENT / MU)); // simplified output
}
```

For a production-quality implementation, the load-line interaction (plate voltage droops as Ip increases through RL) requires solving a nonlinear equation per sample (Newton-Raphson, typically 2–3 iterations). The WDF framework can model this too using `DiodeT` as a building block.

**Even-harmonic shortcut** (drop-in replacement, adequate for "vibe"):
```cpp
// f(x) = tanh(drive * x) / tanh(drive) + alpha * x^2
// drive = 1.5–3.0, alpha = 0.08 (adds 2nd harmonic characteristic of class-A)
inline double evenHarmonicSat(double x, double drive, double alpha) {
    const double tanhDrive = std::tanh(drive);
    return std::tanh(drive * x) / tanhDrive + alpha * x * x;
}
```

---

## 7. Complete Implementation Plan

### Recommended stack

| Component | Topology | Library |
|---|---|---|
| LF Boost + LF Cut (unified) | WDF passive network | chowdsp_wdf v1.0.0 (vendored, BSD-3) |
| HF Boost (LC resonator) | WDF passive network | chowdsp_wdf |
| HF Cut (RC shelf) | Bilinear biquad (RBJ) | JUCE `juce::dsp::IIR` |
| Makeup gain | Koren 12AX7 waveshaper OR linear scale | chowdsp_wdf (load-line) or hand-rolled |
| Oversampling | 2x or 4x | `juce::dsp::Oversampling<double>` |

### Integration with MooDuck style

MooDuck already uses:
- `double` precision processing throughout (`ZVAFilter::processAudioSample(double xn)`)
- Per-parameter `calculateFilterCoeffs()` callbacks
- `juce::SmoothedValue` for automation-safe ramping

The chowdsp_wdf API matches this style exactly: element values are set outside the audio loop (on parameter change), `prepare(sampleRate)` is called from `prepareToPlay`, and `processSample()` is a simple 3-line function:

```cpp
// Full EQP-1A process sample (after combining all sections into one WDF tree):
double EQP1A::processSample(double xn) {
    Vin.setVoltage(xn);
    Vin.incident(S3.reflected());
    S3.incident(Vin.reflected());
    return (-wdft::voltage<double>(J) + wdft::voltage<double>(R3)) * gainScalar;
}
```

### Oversampling wrapper

```cpp
// In PluginProcessor:
juce::dsp::Oversampling<double> oversampler {
    2,   // numChannels
    2,   // 2 stages → 4x factor
    juce::dsp::Oversampling<double>::filterHalfBandPolyphaseIIR };

// prepareToPlay:
oversampler.initProcessing(maxBlockSize);
eqp1a_L.prepare(sampleRate * 4.0);
eqp1a_R.prepare(sampleRate * 4.0);

// processBlock:
auto block = juce::dsp::AudioBlock<double>(mainBuffer);
auto osBlock = oversampler.processSamplesUp(block);
// ... process osBlock sample-by-sample with EQP1A ...
oversampler.processSamplesDown(block);
```

### Vendoring chowdsp_wdf

```bash
# From MooDuck/
mkdir -p Source/ThirdParty
cd Source/ThirdParty
git clone --depth 1 --branch v1.0.0 https://github.com/Chowdhury-DSP/chowdsp_wdf.git
```

Then in `MooDuck.jucer`, add `Source/ThirdParty/chowdsp_wdf/include` as a header search path. In CMakeLists.txt, add `include_directories(Source/ThirdParty/chowdsp_wdf/include)`.

In source files: `#include "chowdsp_wdf/chowdsp_wdf.h"`

---

## 8. Summary Recommendation

**Use WDF (chowdsp_wdf) for the LF boost/cut and HF boost sections.** These are the three sections whose character arises from LC resonance and passive network interactions. Only WDF models these correctly. The EQP-WDF-1A plugin (GPLv3, SMC 2024) provides a validated reference implementation using the exact same library, confirming the approach works in a JUCE context at production quality.

**Use bilinear biquad for HF cut.** It is a passive RC shelf with no inductor. JUCE's `juce::dsp::IIR::Coefficients<double>::makeLowShelf` or the RBJ cookbook formula is sufficient.

**Use 2x oversampling at 44.1 kHz, 1x at 88.2+ kHz.** Use `juce::dsp::Oversampling<double>` with polyphase IIR for lowest latency. 4x if a full Koren nonlinear tube stage is included; 2x if only even-harmonic waveshaping is used.

**For the tube stage, start with the linear gain scalar (15.4882).** Defer the Koren model to a second pass — it requires load-line analysis and Newton-Raphson iteration. An even-harmonic waveshaper (`tanh(kx) + α·x²`) is a good intermediate milestone before implementing the full Koren model.

---

## References

- Chowdhury DSP chowdsp_wdf library: https://github.com/Chowdhury-DSP/chowdsp_wdf
- chowdsp_wdf documentation (Stanford CCRMA): https://ccrma.stanford.edu/~jatin/chowdsp/chowdsp_wdf/
- chowdsp_wdf DAFx paper: https://www.researchgate.net/publication/364689194_chowdsp_wdf_An_Advanced_C_Library_for_Wave_Digital_Circuit_Modelling
- EQP-WDF-1A reference plugin (GPL-3): https://github.com/ABSounds/EQP-WDF-1A
- SMC 2024 paper "Modeling the Pultec EQP-1A with Wave Digital Filters": https://smcnetwork.org/smc2024/papers/SMC2024_paper_id132.pdf
- EQP-WDF-1A Zenodo release (thesis): https://zenodo.org/records/8382764
- Orfanidis 1997 shelving filter paper: https://www.semanticscholar.org/paper/Digital-Parametric-Equalizer-Design-with-Prescribed-Orfanidis/b98f2ac93a6ef9b60e109960c6f2f00edde9bf64
- Audio EQ Cookbook (RBJ): https://webaudio.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html
- Koren triode SPICE model article: https://www.normankoren.com/Audio/Tubemodspice_article.html
- Koren model HAL paper: https://hal.science/hal-00811215/document
- Pultec simultaneous boost+cut analysis: https://abbeyroadinstitute.nl/blog/demystifying-the-pultec/
- Matched Z-transform vs bilinear: https://relayman.org/fisher/mzt.html
- Zavalishin "Art of VA Filter Design": https://www.native-instruments.com/fileadmin/ni_media/downloads/pdf/VAFilterDesign_1.1.1.pdf
- JUCE dsp::Oversampling: https://docs.juce.com/master/classdsp_1_1Oversampling.html
