# Pultec EQP-1A Character & Accuracy Guide

Compiled from the AnalogVibes "3 Bands of Glory" circuit deep-dive.
Purpose: equip future experimentator and tester agents with ground-truth knowledge
about how the hardware actually behaves, so implementations can be verified against
the real circuit's character — not just textbook filter theory.

---

## 1. Signal Path (Full Chain)

```
Input → [Input Transformer]
       → [Passive Filter Circuit  −20 dB]
       → [Interstage Transformer  1:2 step-up, unbal→bal]
       → [12AX7 triode  +20 dB voltage gain]
       → [12AU7 triode  output stage]
       → [Peerless S217-D Output Transformer  w/ hidden tertiary winding]
       → Output
```

**Key facts for testing:**

- The passive filter is a **−20 dB PAD** (10 kΩ HF-Boost pot + 1 kΩ HF-Atten pot in
  series). Every signal path — boosted, cut, or bypassed — goes through this divider.
  "Boost" means certain frequencies bypass more of the PAD and therefore appear louder.
- The interstage transformer is a **1:2 step-up**. It also converts unbalanced to
  balanced for the push-pull amp. The step-up ratio adds level before the 12AX7.
- The output transformer has a **hidden tertiary winding** that feeds negative feedback
  back to the 12AX7 grid. This feedback loop keeps distortion low and the frequency
  response flat in the amp section. Without it the 12AX7 would colour the signal far
  more heavily.
- **Bypass ≠ true bypass.** When the EQ is switched "OFF", the signal still travels
  through the full amp circuit (through both transformers and both tubes). Engineers
  report the signal sounds better even in bypass mode — attributed to transformer
  saturation and tube harmonic colouring. Our `KorenTriode` bypass toggle should
  model whether this always-on path is included or cut.

---

## 2. Three Filter Sections

### 2.1 LF Boost & Cut (left section)

| Control | Range | Type |
|---------|-------|------|
| LF Freq select | 20 / 30 / 60 / 100 Hz | stepped switch, shared boost+cut |
| LF Boost pot | 0–18 dB | shelf boost |
| LF Atten (cut) pot | 0–18 dB | shelf cut |

**Filter type: shelving** (not bell). The document explicitly states "We're talking
shelving filter in this band."

**Boost mechanism:** The 10 kΩ HF-Boost pot wiper routes increasing signal to a
capacitor+inductor array selected by the LF freq switch (pole 1). This LC network
presents **high impedance** to low frequencies at the selected corner, so those
frequencies are attenuated less by the PAD → they appear boosted relative to the
rest of the spectrum.

**Cut mechanism:** The 100 kΩ LF-Cut pot (pole 2 of the same 2-pole rotary switch)
routes signal through capacitors that pass frequencies above the corner to ground.
Low-frequency content cannot pass the capacitor → it is blocked = low-end cut.

**The critical asymmetry (Pultec Trick source):**
- Pole 1 (boost) and Pole 2 (cut) use **different capacitor arrays** even though they
  are on the same physical switch.
- The cut section's capacitors set a corner frequency **30–50% above the labelled
  frequency** (confirmed in circuit-topology.md from SMC 2024 analysis).
- This means at e.g. 100 Hz: boost corner ≈ 100 Hz, cut corner ≈ 130–150 Hz.
- When both are turned up simultaneously: frequencies below ~100 Hz are boosted,
  frequencies around 130–150 Hz are cut, creating a **resonant hump below + dip
  above** the labelled frequency. This is the famous "Pultec Trick."

**Domination rule:** When boost and cut are applied simultaneously, **boost dominates**
at and below the boost corner. The original Pultec manual warns against using both
simultaneously — engineers do it anyway because the shape is musically useful.

**Testing implications:**
- At any single LF frequency position with boost only: measure a smooth shelving
  rise starting at (and below) the labelled frequency. No ringing, no bell peak.
- With simultaneous boost + cut: expect a bump below the labelled freq and a dip
  just above it. The bump amplitude > the dip depth at equal pot positions.
- Verify the cut corner is higher than the boost corner — if they match exactly,
  the Pultec Trick will not appear. This is the single most important correctness
  criterion for the LF integrated WDF section.

**Interaction with HF cut:** When LF Boost = 0, the HF-cut capacitors are directly
to ground. When LF Boost > 0, the LF boost's cap array introduces higher impedance
in the HF-cut cap path, **shifting the effective LF cut corner**. The two sections
are not independent — this is why an integrated WDF tree (sharing junction nodes)
is mandatory; independent biquads cannot reproduce this coupling.

---

### 2.2 HF Boost (center section, 3 knobs)

| Control | Range | Type |
|---------|-------|------|
| HF Boost freq select | 3 / 4 / 5 / 8 / 10 / 12 / 16 kHz | 7 positions |
| HF Boost pot (10 kΩ) | 0–18 dB | bell boost |
| Bandwidth pot (2.5 kΩ) | Narrow → Wide | Q control |

**Filter type: bell (peaking)**, not shelf. See fig. 2 in the document — clear
resonant peak.

**Mechanism:** The HF freq select switch connects the wiper of the 10 kΩ boost pot
to one of several capacitors in series with the filter coil (inductor). This LC
parallel tank presents **low impedance** at the resonant frequency, bypassing the
−20 dB PAD for those frequencies → they appear boosted.

**Bandwidth control:** The 2.5 kΩ bandwidth pot adds resistance in series with the
LC filter. More resistance = broader peak (lower Q), less resistance = narrower
peak (higher Q). This is opposite to how most people intuitively think of "bandwidth."
When testing: maximum bandwidth pot = broad (low Q); minimum = narrow (high Q).

**Testing implications:**
- At each of the 7 frequency positions, verify the peak is centred on the labelled
  frequency (±5% acceptable for component tolerance).
- Sweep the bandwidth pot and confirm Q increases as resistance decreases.
- At 5 kHz with HF bandwidth at minimum (narrow): expect a sharp, high-Q resonant
  peak. Reference component values from circuit-topology.md: L=67.5 mH, C=15 nF.

---

### 2.3 HF Cut (right section)

| Control | Range | Type |
|---------|-------|------|
| HF Atten freq select | 5 / 10 / 20 kHz | 3 positions |
| HF Atten pot (1 kΩ) | 0–18 dB | shelf cut |

**Filter type: shelving** — NOT bell. The document explicitly distinguishes:
"the HF boost section is a bell filter, both the low end as well as the HF cut
section are shelving."

**Mechanism:** The wiper of the 1 kΩ HF-Atten pot is routed through the HF-Atten
freq select switch to one of 3 capacitors connected to ground. Frequencies above
the selected corner frequency pass through the capacitor to ground — they are
removed from the signal.

**Coupling to LF section:** The HF-cut capacitors are not exclusively connected to
ground. The LF Boost circuit, when active, "has its foot in the door" and modifies
the impedance at these nodes. When LF Boost > 0, the HF-cut caps see a different
ground impedance, subtly shifting the effective HF-cut corner. This is a second-
order interaction that biquad models miss entirely.

**Testing implications:**
- With LF Boost = 0: HF cut at 5/10/20 kHz should be clean shelving filters.
  Measure −3 dB point and confirm it's at the labelled frequency.
- With LF Boost > 0: the HF cut corner may shift slightly. This is correct hardware
  behaviour and should not be "fixed."

---

## 3. Passive EQ Working Principle (Critical Mental Model)

**This determines whether test results make sense:**

The filter is a −20 dB PAD. It attenuates everything. The controls determine
**which frequencies get attenuated less** than others. Those frequencies therefore
appear louder — "boosted" — relative to the attenuated baseline.

The tube amplifier restores the overall −20 dB loss with +20 dB gain.

Consequence for implementation testing:
- Unity gain (all controls at zero) means the passive section attenuates −20 dB
  and the amp restores +20 dB. Net = 0 dB at output. If your test measures
  something other than 0 dB with all pots at zero, the gain staging is wrong.
- A "boost" of +10 dB at 100 Hz means the passive network attenuates 100 Hz
  by only −10 dB instead of −20 dB. The amp still adds +20 dB. Net = +10 dB.
- There is no "active amplification" inside the filter itself. Apparent boosts
  are always a result of less attenuation for certain frequencies.

---

## 4. Tube Amplifier Character

**Push-pull topology:** 12AX7 (voltage gain stage) + 12AU7 (output/power stage).

**Negative feedback:** The Peerless S217-D output transformer's tertiary winding
feeds back to the 12AX7 grid. This dramatically reduces harmonic distortion and
flattens the frequency response of the amp. The amp section is intentionally clean.

**Transformer saturation at low levels:** Even with filters bypassed, the signal
passes through two transformers. At typical programme levels these are linear, but
at hot input levels they introduce even-order harmonics (warmth). This is the source
of the "sounds better even in bypass" observation.

**12AX7 Koren model constants** (already implemented in `Source/KorenTriode.h`):
```
MU=100, EX=1.4, KG1=1060, KP=600
Bias: V_GRID=−2V, V_PLATE=200V, R_LOAD=100kΩ
```
The model is normalised at the bias point so unity gain = 0 dB through the triode
when operated in its linear region. Oversampling is 4× (2^2 polyphase IIR) to
prevent aliasing from the nonlinear Koren equation.

---

## 5. Expected Frequency Curves for Testing

All measurements assume: fs = 44100 Hz (or 48000 Hz), 4096-sample warmup +
4096-sample RMS measurement, sinusoidal test tones.

### 5.1 Unity gain (all controls zero)
- Flat response from 20 Hz to 20 kHz
- 0 dB ± 0.1 dB across all test frequencies
- Any deviation indicates gain staging error

### 5.2 LF boost only (100 Hz, full boost)
- Below 50 Hz: approaching +18 dB
- At 100 Hz: approximately +14–16 dB (shelving corner)
- At 1 kHz: approaching 0 dB (shelf flattens)
- Smooth, gradual slope — no ringing or bell shape

### 5.3 LF cut only (100 Hz, full cut)
- Below 100 Hz: approaching −18 dB
- Corner at approximately 130–150 Hz (NOT 100 Hz — cut corner asymmetry)
- Above 300 Hz: approaching 0 dB
- If the cut corner is at exactly 100 Hz: implementation is wrong

### 5.4 LF boost + cut simultaneously (100 Hz, both at max)
- Below 80 Hz: strong boost (+12 dB or more) dominated by boost section
- Around 100–130 Hz: resonant hump peak
- Around 150–200 Hz: noticeable dip (1–5 dB) below 0 dB baseline
- Above 300 Hz: returns to 0 dB
- If the curve is flat or shows only a simple shelf: WDF junction coupling is broken

### 5.5 HF boost (5 kHz, narrow bandwidth)
- Bell peak centred at 5 kHz
- Peak height up to +18 dB at maximum boost
- At minimum bandwidth (narrowest): Q visible and sharp
- At maximum bandwidth (broadest): gentler, wider hump
- Below 1 kHz and above 10 kHz: near 0 dB

### 5.6 HF boost (5 kHz, wide bandwidth)
- Same peak frequency
- Lower peak height for same boost pot setting (energy spread wider)
- Wider bell — −3 dB points spread across larger frequency range

### 5.7 HF cut (5 kHz)
- Above 5 kHz: approaching −18 dB
- Shelving shape — gradual rolloff, not a brick wall
- At 10 kHz: well into the cut region (−12 dB or more at full atten)
- Below 2 kHz: near 0 dB

### 5.8 Mastering preset (Pultec Trick on bass)
Expected: LF freq=100Hz, LF Boost=7, LF Atten=5
- Low-shelf boost below ~80 Hz
- Slight dip around 150–200 Hz
- Net result: kick drum weight added, upper-bass muddiness removed

---

## 6. Common Implementation Errors to Catch

| Symptom | Likely cause |
|---------|-------------|
| LF boost+cut shows no dip | Boost and cut using same corner freq — shared cap array not implemented |
| Pultec Trick hump+dip both same magnitude | Boost should dominate — coupling mismatch |
| HF cut is a bell, not a shelf | Section misidentified as HF boost in code |
| HF boost is a shelf, not a bell | Section misidentified as HF cut in code |
| Unity gain ≠ 0 dB | PAD/makeup gain imbalance in signal path |
| LF cut corner at exactly labelled freq | Cut pole capacitors same as boost pole — pole 2 not correctly modelled |
| No response change when bandwidth pot moves | Bandwidth resistance not wired in series with LC tank |
| HF cut corner shifts when LF boost is non-zero | This is CORRECT; do not report as a bug |
| Bypass has 0 dB + zero colouration | If tube saturation bypass is OFF this is fine; if ON and still no saturation, check KorenTriode wiring |

---

## 7. Perceptual/Character Notes for Listening Tests

- **"Smooth"** is the primary character word. No harshness. Shelving filters are
  gentle and musical, not surgical.
- **Low end** boosted with the Pultec sounds "full and round" not "boomy" — because
  the simultaneous cut removes the muddiness that pure shelving boost would add.
- **High end** boost (bell) sounds "airy" and "sweet" not harsh — the LC resonance
  is inherently lower-Q than typical digital peaking EQ at equivalent bandwidth
  settings.
- **High end cut** rolls off smoothly — no phase ringing artefacts expected (it's
  a first-order or gentle second-order RC circuit, not a steep digital brick wall).
- **Saturation character:** very subtle at typical studio levels. Even harmonics
  (2nd, 4th) dominate due to 12AX7 class-A operation. If saturation sounds "crunchy"
  or adds odd harmonics prominently, the tube model bias point is wrong.

---

## 8. Reference Documents

- `eq-research/circuit-topology.md` — full transfer function derivations, component values
- `eq-research/filter-topology.md` — WDF implementation topology and chowdsp_wdf usage
- `PULTEC_DEV.md` — 6-phase implementation plan, all 7 design decisions, 8 test cases
- `Source/KorenTriode.h` — 12AX7 Koren model implementation (complete, clean build)
- `Source/PultecCore.h` — WDF passive filter tree (in progress as of 2026-05-28)
- `tests/runtests.cpp` — headless frequency response test suite

SMC 2024 paper: "Modeling the Pultec EQP-1A with Wave Digital Filters"
(Barrera & Eijas) — reference implementation at github.com/ABSounds/EQP-WDF-1A
