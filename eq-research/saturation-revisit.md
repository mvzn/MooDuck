# Saturation Revisit — Pultec EQP-1A

**Date opened:** 2026-05-29
**Owner:** TBD
**Tracker:** `PULTEC_DEV.md` Phase 8
**Trigger:** User reports the current Pultec saturation sounds "very digitally distorted" — fizzy / harsh / not tube-like

---

## 1. Current implementation audit

File: `Source/KorenTriode.h` (199 lines, dated 2026-05-28 per `git log`).

### 1.1 What the code does

- **Static model:** Norman Koren 1996 plate-current curve fit with 12AX7 parameters (MU=100, EX=1.4, KG1=1060, KP=600).
- **Operating point:** V_GRID_BIAS = -2 V, V_PLATE_BIAS = 200 V, R_LOAD = 100 kΩ. Class-A bias.
- **Signal mapping:** `vGridExcursion = audio_sample · GRID_SCALE · drive` where GRID_SCALE = 1.0 and drive = 1.0 → audio ±1.0 maps directly to grid ±1.0 V excursion (signal swings −3 to −1 V at grid, well within the class-A envelope).
- **Output:** `vOut = −ΔIp · R_LOAD / linearGain` (inverting common-cathode stage). Normalized so small-signal gain is unity.
- **Oversampling:** 4× polyphase IIR half-band (JUCE `Oversampling::filterHalfBandPolyphaseIIR`).
- **Output clamp:** `std::clamp(vOut, -1.0, 1.0)` — **hard rail clip at ±1**.

### 1.2 What's wrong (hypothesis ranking, by audible impact)

| # | Suspected cause | Why it sounds digital | Confidence |
|---|---|---|---|
| 1 | **Hard rail clip at ±1** (line 197) | Real tubes never produce a square edge — even at extreme drive they go into smooth grid-current limiting. Hard clip produces all odd harmonics out to infinity → broadband fizz that aliases even at 4× OS. | High |
| 2 | **No grid-current asymmetric limiting** | When grid voltage swings positive (above 0 V relative to cathode), real tubes start drawing grid current → input impedance drops, signal is soft-compressed on positive half cycles only. This is a primary source of even-order distortion in real tube circuits. Missing here → distortion is symmetric and lacks the warm character. | High |
| 3 | **Single-stage model** | Real EQP-1A signal path is **12AX7 (voltage gain) → 12AU7 (output driver) + output transformer feedback**. Each stage contributes its own curve and saturation regime, plus the NFB loop linearises low-signal behaviour while letting the output stage saturate first. Folding all this into a single Koren-12AX7 misses the layered character. | Med-high |
| 4 | **No transformer saturation contribution** | Both the inter-stage and output transformers contribute even-order distortion under signal (see `transformer-planning.md` Tier 3). Some of the "tube warmth" credited to tubes is actually transformer character. We have zero transformer saturation. | Medium |
| 5 | **4× oversampling may be insufficient** for the hard clip's sharp transition. With anti-derivative anti-aliasing (ADAA, Parker-Esqueda-Bilbao DAFx 2016) we could keep 4× and still suppress aliases. Without ADAA we may need 8× or even 16× to push the audible aliases below noise floor. | Medium |
| 6 | **Bias / drive defaults too hot** | drive=1.0 maps audio peaks to ±1 V grid excursion — well into the curve's nonlinear region. Real Pultec operation rarely pushes the 12AX7 first stage that hard (most of the saturation comes from the output stage). Lowering drive to ~0.3 might make the default sound closer to "transparent with optional drive". | Low-medium |
| 7 | **DC drift accumulation** | The asymmetric curve produces non-zero DC offset under signal. Currently no high-pass on the output path, so DC builds up in downstream IIR filters. Unlikely the primary culprit but worth checking. | Low |

### 1.3 What the user actually hears

Likely chain of effects: At moderate drive (saturation enabled but not pushed hard), the Koren curve produces some 2nd harmonic — close to what one would expect for a single triode. As drive increases, the curve's sharp top approaches the ±1 clamp; eventually the clamp activates and adds high-order odd harmonics (3rd, 5th, 7th...). Those harmonics are above 20 kHz at the upsampled rate but fold back into the audio band on the downsample → fizz. The user's description ("very digitally distorted") matches the fizz signature of poorly-band-limited hard-clip aliasing.

---

## 2. Reference models worth considering

Listed in roughly increasing fidelity:

### 2.1 Soft-clip families (cheap)

- **`std::tanh(x)`**: classic. Symmetric, smooth, no aliasing problems. Doesn't capture tube asymmetry. Good as a baseline replacement for the hard clip.
- **`x / (1 + |x|)` (Reinhard / sigmoid family)**: similar to tanh but cheaper and slightly different harmonic spectrum.
- **Bias-shifted tanh**: `tanh(x − bias) + tanh(bias)` — adds asymmetry and even harmonics. Cheap. Defensible match to triode-with-grid-bias character.

### 2.2 Static triode equations (medium)

- **Koren 1996** (what we have now). Parameter set is well-documented; the implementation just needs the hard-clip removed and replaced with a tanh-shaped soft clip OR with proper grid-current modeling on the positive half cycle.
- **Yeh-Smith 2008/2009** ("Simplified, Physically-Informed Models of Distortion and Overdrive Guitar Effects Pedals", DAFx 2007/2008; PhD thesis 2009). Static curve plus first-order grid-current limiter, suitable for both 12AX7 and 12AU7. Cited 100+ times — well-tested.
- **Pakarinen-Yeh 2009** ("A Review of Digital Techniques for Modeling Vacuum-Tube Guitar Amplifiers", CMJ 2009). Comprehensive comparison of static and dynamic models; good reference.

### 2.3 Physical models (high fidelity)

- **Cohen-Helie 2011** ("Real-time simulation of a guitar power amplifier", DAFx 2011). Includes plate current, screen current, and grid current with realistic asymmetric soft clipping. Tested on EL34 / EL84 / 12AX7. Real-time-feasible.
- **Eichas-Zollner 2012/2014** ("Black-box modeling of distortion circuits with block-oriented models"). Less physical, more system-identification, but produces excellent character match when trained against measurements.
- **Macak-Schimmel 2010/2011** (DAFx). Modified nodal analysis of full tube stages including coupling caps and load impedances.

### 2.4 Aliasing mitigation

- **ADAA (Anti-Derivative Anti-Aliasing)** — Parker, Esqueda, Bilbao DAFx 2016. Replaces `f(x)` with `(F(x_n) - F(x_{n-1})) / (x_n - x_{n-1})` where F is the antiderivative of f. Suppresses aliases by 20–40 dB at a fixed oversampling rate. Particularly effective for hard / soft clip and `tanh`-like curves. **Strongly recommended** regardless of which saturation curve we end up with.
- **Higher oversampling factor** — bump from 4× to 8× or 16×. More CPU, less elegant than ADAA but a known quantity.

---

## 3. Three-tier improvement plan

### Tier 1 — Replace hard clip with soft clip ⭐ recommended first

Single-line change: replace `std::clamp(vOut, -1.0, 1.0)` with `std::tanh(vOut)`. ~5-minute fix.

**Expected outcome:** Fizz disappears immediately. Distortion character becomes warmer (smooth approach to saturation). The asymmetry from Koren is preserved.

**Risk:** Tanh is symmetric; if the Koren output is already asymmetric the combined response will reflect that, but the asymmetric character may be subtler than what the user expects.

**Test:** A/B against current state. User decides whether the smoothness improvement alone is enough or whether to push to Tier 2.

### Tier 2 — Add grid-current asymmetric soft-clip + ADAA

- **Grid-current modeling:** When `vGridExcursion > V_GRID_CURRENT_THRESH` (typically 0 V relative to cathode), apply a soft compression to the input signal modeling the input impedance drop. Formula:
  ```cpp
  if (vGrid > 0) vGrid = V_GRID_CURRENT_THRESH * std::tanh(vGrid / V_GRID_CURRENT_THRESH);
  ```
- **ADAA wrapping:** Replace `processSampleKoren(x)` calls with an ADAA wrapper that holds the previous input and computes `(F(x_n) - F(x_{n-1})) / (x_n - x_{n-1})`. F is the antiderivative of the Koren+tanh pipeline; compute numerically once and store in a lookup table.
- **DC blocker on output:** One-pole HP at 5 Hz to prevent bias drift accumulation.

**Effort:** ~1 day. Most time on the ADAA antiderivative table and on tuning the grid-current threshold to taste.

**Risk:** ADAA introduces a half-sample group delay; check this doesn't conflict with the 4× oversampling latency reporting.

### Tier 3 — Two-stage 12AX7 + 12AU7 model with NFB loop

Out of scope for the immediate "sounds digital" fix. This is where the model becomes a Pultec-character emulation rather than a generic tube saturator. Couples tightly with the transformer modeling planner (`transformer-planning.md`) because the NFB loop closes through the output transformer's tertiary winding.

**Effort:** ~3-5 days; couples with Phase 7 Tier 3.

---

## 4. Recommended sequence

1. **Tier 1 (tanh swap)** — 5 minute fix, ship immediately. Validates the hard-clip-is-the-problem hypothesis.
2. **Spectral A/B** — capture FFT of current output vs Tier 1 output at three drive levels (0.3, 1.0, 2.0) and three test signals (1 kHz sine, 10 Hz sweep, drum hit). User-listenable WAV exports plus an FFT plot. Validates against ear.
3. **Tier 2 (grid current + ADAA)** — if Tier 1 isn't enough character improvement, layer in asymmetric grid-current limiting plus ADAA.
4. **Tier 3 (two-stage + NFB)** — only after Phase 7 (transformers) lands, because the NFB loop needs the output transformer.

---

## 5. Test plan

Add to `tests/runtests.cpp`:

```cpp
// T10: saturation does not exceed 0.1% THD at low signal levels (linearity)
// T11: at -6 dBFS sine, THD is 1-5% (tube-like character)
// T12: at -0.5 dBFS sine, no harmonic above 22 kHz exceeds -70 dB (alias floor)
//      — this is the critical anti-aliasing assertion that current code fails
// T13: drive parameter sweep produces monotonically increasing THD
// T14: no DC offset accumulation after 10 s of 0 dBFS sine through saturation
```

T12 is the make-or-break test. Current code fails it; Tier 1 + ADAA should pass it.

---

## 6. Open questions / unknowns

- **Drive scaling:** What value of `drive` corresponds to "0 dB knob position" in the future UI? The current code accepts a raw multiplier with no calibration. Need a curve that maps a user-facing 0-1 (or 0-10) knob to perceptually-equal-step saturation increase.
- **Bypass vs unity:** Currently `setEnabled(false)` is a hard bypass. Real Pultec engineers report bypass-isn't-bypass character — signal still flows through tubes + transformers, picking up colour. Decide whether `enabled=false` means "true passthrough" or "tubes+transformers active but no clipping" (would require an additional toggle).
- **Stereo crosstalk:** Real tubes share a heater across L/R channels and can pick up subtle crosstalk. Probably below audibility threshold; document as deferred.

---

## 7. Risk register

| Risk | Mitigation |
|---|---|
| Tanh replacement changes the gain calibration → output level shifts | Re-tune `linearGain` after the swap; T10 catches level drift |
| ADAA's antiderivative table eats memory on small devices | Use a fixed-size table (e.g., 1024 points across the input range); negligible |
| Asymmetric soft-clip introduces DC offset → IIR drift | Mandatory DC blocker on output |
| Higher oversampling (Tier 3 8×) costs CPU on real-time hosts | Profile; if needed, gate behind a quality-vs-CPU user setting |
| User's "sounds digital" complaint actually isn't the clip — it's the missing transformer character | Run Tier 1 first; if user still says digital, the answer is "we also need Phase 7 Tier 3" |
