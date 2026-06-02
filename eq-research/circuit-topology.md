# Pultec EQP-1A and MEQ-5 Circuit Topology Research
### For JUCE Plugin DSP Implementation — May 2026

---

## 1. EQP-1A Overview and Signal Path

The Pultec EQP-1A (introduced 1951 as EQP-1, revised to EQP-1A) is a **passive-then-active** program equalizer. The signal path is:

```
Input Transformer (600Ω) → Passive LC Filter Network → Tube Makeup Amplifier → Output Transformer (600Ω)
```

### Why Passive + Active Matters

The passive LC filter network can only attenuate — it never adds energy. By design, it imposes roughly **15–16 dB of insertion loss** at all frequencies even with all controls at zero. The tube makeup amplifier (12AX7 first stage, 12AU7 output driver) is calibrated to restore exactly that 15–16 dB, yielding unity gain at flat. This architecture has several sonic consequences:

1. **Extreme linearity and transparency in the passive stage.** There is no active element in the EQ signal path. No op-amp phase shift or slew limiting. All nonlinearity comes from the tube stage that follows.

2. **The passive stage loads the signal.** All four filter sections (LF boost, LF cut, HF boost, HF cut) are connected in parallel across the same nodes with no buffering between them. This means they interact electrically at all times, not just during the boost+cut trick.

3. **The tube stage imparts even-harmonic character.** The push-pull 12AX7/12AU7 configuration adds predominantly 2nd-harmonic saturation at higher levels, with soft compression as the tubes approach their operating limits. Negative feedback is taken from a secondary winding on the output transformer and fed back to the cathodes of the input valves, reducing but not eliminating this character.

4. **Transformer colouration.** Three transformers (input, interstage, output) are in the signal path. Each contributes phase shift and harmonic content, especially at frequency extremes. The output transformer is a Peerless S-217D or equivalent in originals; clones commonly use Carnhill or Jensen units. The input transformer is a Triad HS56. These are beyond the scope of a DSP white-box model of the filter stage alone but must be represented if targeting the full analog character.

   > **Update (2026-05-29):** The "must be represented" qualifier turned out to bind harder than expected. Without the **inter-stage transformer** in particular, the WDF model exhibits an LF coupling artifact — engaging the HF Boost section injects a constant ~+5.5 dB LF shelf regardless of boost amount or BW. This is because the HF Boost pot redistributes impedance across the full network, and the output formula reads voltages in the LF section. In the real circuit, the inter-stage transformer's high primary impedance isolates the EQ output from the makeup-amp loading, blocking this coupling path. See `eq-research/transformer-planning.md` for the proposed fix.

5. **Input/output impedance interaction.** The passive network is designed to work into 600Ω source and load. At any other impedance, the frequency response changes. This is why the bandwidth of the HF boost varies with source impedance and why clone builders report frequency response differences based on their gain stage choices.

**For a DSP emulation:** The SMC 2024 paper (Barrera et al.) confirms that an accurate white-box model of the passive EQ stage alone (without transformers or tube gain stage) achieves very high accuracy against the measured hardware unit. The gain stage and transformers are modeled separately if harmonic distortion is desired.

---

## 2. LF Boost Section

### Architecture

The LF boost is a **first-order low-frequency shelving boost**, implemented as a passive voltage divider with a frequency-selective bypass. The essential topology is:

The circuit forms a resistive voltage divider (the source impedance and a shunt resistor R_bLo form the pad). A series LC circuit is placed in parallel with that shunt resistor — or, equivalently, in the signal path as a shunt element to ground. The boost potentiometer (a 10K log-taper pot) controls how much of the LC branch is engaged.

More precisely (from schematic analysis): at low frequencies below resonance, the LC series combination presents a high impedance, so the divider ratio is dominated by the resistors. As frequency approaches the resonant frequency f_0 = 1/(2π√LC), the LC impedance falls toward the real DC resistance of the inductor coil (very low, typically 1–5Ω for the LF inductors), which creates a low-impedance shunt that "short-circuits" the pad at those frequencies, allowing more signal through. Above the resonant frequency, the LC impedance rises again (now inductive), and the circuit returns toward the unmodified divider ratio.

This gives a **shelving boost**: gain rises from the mid-band baseline up to a shelf at very low frequencies, with the shelf corner frequency set by the LC resonance.

### Tuned Frequencies

The LF frequency selector switch connects different LC combinations:

| Position | Frequency |
|----------|-----------|
| 1        | 20 Hz     |
| 2        | 30 Hz     |
| 3        | 60 Hz     |
| 4        | 100 Hz    |

Note: Some early EQP-1 variants had 30/60/100 Hz only. The EQP-1A adds 20 Hz.

### Filter Shape and Order

- **Filter shape:** First-order low-frequency **shelf** (not a peak). The boost region is a flat shelf at low frequencies, not a resonant hump (in isolation — the hump appears only when boost+cut are simultaneous; see Section 4).
- **Filter order:** 2nd-order (one LC pair = one resonance = second-order system) but the shelving topology means the asymptotic slope is **-6 dB/octave** (first-order shelf slope), not -12 dB/octave. This is because the pot and circuit resistances dominate at frequencies away from resonance, reducing the effective order.
- **Slope above the shelf corner:** Approximately **6 dB/octave** transition slope from the boosted shelf back toward unity, covering about 1–2 octaves above the tuned frequency.

### S-Domain Transfer Function H_boost(s)

For the core passive LC boost network (simplified to its essential form with the inductor L, series capacitor C, shunt resistance R_shunt representing the divider leg, and series resistance R_series representing the source/load termination), the voltage transfer function has the form:

```
                    Z_LC(s) + R_shunt
H_boost(s)  =  α · ─────────────────────────────
                    Z_LC(s) + R_shunt + R_series
```

where `α` = boost pot wiper position (0 to 1), and:

```
Z_LC(s)  =  sL + 1/(sC)  =  (s²LC + 1) / (sC)
```

Substituting Z_LC:

```
                  (s²LC + 1)/sC + R_shunt
H_boost(s) = α · ─────────────────────────────────────────────
                  (s²LC + 1)/sC + R_shunt + R_series
```

Multiply numerator and denominator by sC:

```
                  s²LC + sCR_shunt + 1
H_boost(s) = α · ──────────────────────────────────────────────
                  s²LC + sC(R_shunt + R_series) + 1
```

Defining ω_0 = 1/√(LC) and normalizing by (LC):

```
                  s² + s(R_shunt/L) + ω_0²
H_boost(s) = α · ─────────────────────────────────────────────
                  s² + s((R_shunt + R_series)/L) + ω_0²
```

This is a **biquad (2nd-order) transfer function** with the same poles and zeros but different damping in numerator vs. denominator. The zero damping (R_shunt/L) is lower than the pole damping ((R_shunt+R_series)/L), producing a boost. At DC (s→0) and at infinite frequency (s→∞), H→α, while near ω_0 the numerator impedance dominates to lift the response. The actual circuit includes the pot as a variable R_shunt, which modulates the depth of the shelf.

**Key parameters derived from this:**
- Resonant frequency: `f_0 = 1/(2π√LC)` — set by the switched L and C values
- Q of the boost peak: `Q = (1/ω_0) · (R_shunt + R_series) / L` at the pole side; lower at the zero side, producing the asymmetric shelf character
- Maximum boost: determined by the ratio R_series / R_shunt

The inductor DC resistance (typically 1–5Ω for LF sections) limits the maximum boost achievable and also affects Q. A lower-DCR inductor gives higher Q and more boost — this is why original Pultec inductors are spec'd for very low DCR.

### Approximate Component Values (LF Section)

LF section inductors are large-value toroidal inductors. Precise values vary by schematic revision, but approximate ranges derived from f_0 = 1/(2π√LC) for the LF frequencies:

A standard approach uses a fixed capacitor C_bLo paired with switched inductor taps. For a typical capacitor of around 2.2–4.7 µF (electrolytic or film, sometimes switched), the required inductances are:

| Frequency | Approx. L (H) at C=2.2µF | Approx. L (H) at C=4.7µF |
|-----------|--------------------------|--------------------------|
| 20 Hz     | ~29 H                    | ~13.5 H                  |
| 30 Hz     | ~13 H                    | ~6 H                     |
| 60 Hz     | ~3.2 H                   | ~1.5 H                   |
| 100 Hz    | ~1.15 H                  | ~0.54 H                  |

These are very large inductors (multi-Henry), which is why the original Pultec used custom-wound toroids and why clones are expensive. The actual Pultec LF inductors are typically in the range of 0.5H to 30H depending on the configuration.

---

## 3. LF Cut Section

### Architecture

The LF cut is a **completely separate circuit** from the LF boost. It is **not** an LC-based circuit. It uses a simple **RC shelving network**.

The LF cut section connects a capacitor C_cLo in shunt to ground through the cut potentiometer R_cLo. At high frequencies, C_cLo is a low impedance path, creating more attenuation; at low frequencies below the RC corner frequency, C_cLo is a high impedance, so the signal passes more freely. This creates a **low-frequency shelving cut** (high-pass shelf).

The critical point: the cut section uses the **same frequency selector switch** as the boost section (they share the switched node), but the cut is implemented with an RC network tuned to a **different corner frequency** at each switch position, while the boost is LC-tuned. The RC time constant R_cLo × C_cLo sets the cutoff, but because RC and LC have fundamentally different impedance vs. frequency relationships, the boost and cut shapes are not symmetric.

### H_cut(s) Expression

For the RC shunt network, with the cut pot resistance R_cLo (wiper position β from 0 to 1) and capacitor C_cLo:

```
Z_cut(s) = 1 / (s·C_cLo)  in series with part of R_cLo
```

The basic transfer function for the shunted signal:

```
              1
H_cut(s) = ──────────────────────────────────
             1 + s·β·R_cLo·C_cLo
```

This is a **first-order high-pass shelving function** at DC. As β increases (more cut), the time constant increases, and the cut corner moves lower in frequency. This is fundamentally a 1st-order system:

```
                        s·τ
H_cut(s) = ──────────────────────  (normalized high-pass character)
                      1 + s·τ

where τ = β·R_cLo·C_cLo
```

The attenuation below the cut corner approaches -β·20dB (controlled by pot position and circuit loading).

**Key difference from the boost section:**
- Boost: 2nd-order LC response — Q is nonzero, response peaks near resonance
- Cut: 1st-order RC response — no resonance, gentle 6dB/oct slope

This asymmetry in order and Q is the fundamental reason why boost and cut at the same frequency do not cancel — they have different shapes, and their interaction creates the characteristic Pultec curve.

### Corner Frequency Relationship

The cut section's corner frequency, at the same switch position as the boost, is approximately **1/2 to 2/3 of an octave higher** than the labeled frequency. This has been confirmed by the Sound on Sound review of the 2019 hardware reissue: "the corner frequencies of the attenuation curves actually start roughly half an octave higher" than labeled. So with the selector at 60 Hz, the cut starts rolling off around 80–90 Hz, while the boost is centered at 60 Hz.

---

## 4. The Simultaneous Boost+Cut ("Pultec Trick")

### The Circuit Mechanism

Both the boost and cut networks are connected in parallel across the same signal nodes, with no buffering between them. When both are active simultaneously, the total impedance seen at that node is the parallel combination of both networks. Because they have different topologies (LC vs. RC) and different frequency-dependent impedances, their combined effect is more complex than simple addition.

The resulting combined transfer function is approximately:

```
H_combined(s) ≈ H_boost(s) × H_cut(s)
```

But this multiplicative view is a simplification. The actual loading interaction means the networks influence each other's effective impedance. The SMC 2024 paper (Barrera et al.) notes that because there is no buffering between the stages, the cut becomes "slightly resonant" when the boost is also active — the boost's LC network interacts with the cut's RC network to create a loaded resonance that neither produces alone.

### What the Combined Response Looks Like

With both boost and cut engaged at the same frequency (e.g., 60 Hz), the frequency response shows:

1. **Sub-bass boost:** A broad, smooth shelf boost below the tuned frequency (e.g., below 60 Hz), reaching maximum boost at the lowest frequencies. The boost curve is broad and gentle — often 1.5 to 2 octaves wide.

2. **A resonant peak (hump) near the tuned frequency:** Centered approximately at or slightly below the selector frequency. This is a modest secondary peak, not a dramatic notch resonance.

3. **A dip above the tuned frequency:** Because the cut section starts attenuating slightly above the selector frequency (e.g., at 80–90 Hz when set to 60 Hz), there is a region of attenuation in the low-mid range (roughly 80–300 Hz depending on settings). This dip is the "mud reduction" effect.

4. **Unity gain in the mid and upper frequencies:** The boost and cut together settle back to the insertion-loss baseline above a few hundred Hz.

The SMC 2024 paper Figure 2 shows the measured LTSpice simulation of this response at 60 Hz with full boost and cut: a broad low-end shelf boost reaching +16 dB below 30 Hz, transitioning through the resonant peak region near 60 Hz, then dropping to approximately -4 to -8 dB attenuation in the 100–300 Hz region before returning to baseline.

### Parametric Characterization

- **Peak height:** Proportional to the boost control position. Approximately 1/3 to 1/2 of the maximum boost dB setting.
- **Peak Q:** Low, approximately Q = 0.7 to 1.5, very broad. The passive network cannot produce high-Q behavior without very low-loss inductors.
- **Dip depth:** Proportional to the cut control position. Maximum dip is approximately -4 to -8 dB below unity in the low-mid region (before the gain stage compensates).
- **Frequency of peak vs. dip:** The peak appears at approximately the labeled frequency; the dip center is roughly 1–1.5 octaves above the labeled frequency.
- **Interaction:** Because both networks share the same nodes, increasing one control changes the effective operating point of the other. This is the non-linear interaction that makes the Pultec trick difficult to replicate with two independent parametric EQ bands in a DAW — the peak and dip are correlated and cannot be set independently.

### DSP Emulation Approach

The most accurate approach is a white-box model (WDF or biquad cascade) that models both networks simultaneously as a loaded circuit. A simplified but musically useful approximation:

1. A low-shelf biquad for the boost, with ω_0 at the labeled frequency and Q ≈ 0.7
2. A high-pass first-order shelf for the cut, with corner frequency 20–40% above the labeled frequency
3. Both evaluated as a series combination at the same nodes, with a small coupling resistor to represent the shared node interaction

The dip in the mid-range emerges naturally from this topology without needing to add a separate notch filter.

---

## 5. HF Boost Section

### Architecture

The HF boost uses a **parallel LC resonant tank circuit** (inductor in parallel with capacitor) placed as a shunt-to-ground element. At the tank resonant frequency, the parallel LC presents maximum impedance (theoretically infinite for ideal components), blocking the shunt path and allowing maximum signal to pass — creating a **peak boost** at the resonant frequency.

The multi-tap inductor has seven taps (in the EQP-1A) selecting center frequencies of 3, 4, 5, 8, 10, 12, and 16 kHz. A single inductor winding with multiple taps reduces cost and ensures consistent magnetic core material across all frequency positions.

The bandwidth control (a continuously variable potentiometer, ~10K linear or log) places a resistor in series with the parallel LC tank. This resistor (R_bHiQ) controls the Q of the resonant peak:

```
Q = R_total / (ω_0 · L)
```

where R_total is the parallel combination of the tank's loading resistors plus R_bHiQ. As R_bHiQ decreases (narrow bandwidth setting), Q increases (sharper peak); as R_bHiQ increases (wide bandwidth setting), Q decreases (broader peak).

### HF Inductor and Capacitor Values

From VintageWindings EQ-1A measurements (corrected values for the 7-tap design):

| Frequency | Inductor (mH) | Capacitor     |
|-----------|---------------|---------------|
| 16 kHz    | 29.9 mH       | 3.3 nF        |
| 12 kHz    | 31.4 mH       | 5.6 nF        |
| 10 kHz    | 31.4 mH       | 8.2 nF        |
| 8 kHz     | 48.2 mH       | 8.2 nF        |
| 5 kHz     | 67.5 mH       | 15 nF         |
| 4 kHz     | 87.9 mH       | 18 nF         |
| 3 kHz     | 156 mH        | 18 nF         |

The original 5-tap Pultec used inductor values of approximately 32.0, 51.1, 84.3, 100.2, and 150.5 mH. Note that multiple frequency positions share the same capacitor value (e.g., 8.2 nF for both 10 kHz and 8 kHz) — the different L values tune the resonance.

Verification of resonant frequency: f_0 = 1/(2π√LC). For 5 kHz: 1/(2π × √(67.5e-3 × 15e-9)) = 1/(2π × √(1.0125e-9)) = 1/(2π × 3.18e-5) ≈ 4997 Hz ≈ 5 kHz. ✓

### Transfer Function H_hf_boost(s)

For the parallel LC tank with series Q-control resistor R_Q and loaded by parallel resistance R_load:

```
Z_tank(s) = (sL · (1/sC)) / (sL + 1/sC) = sL / (s²LC + 1)
```

The total shunt impedance with R_Q in series:

```
Z_shunt(s) = R_Q + sL / (s²LC + 1) = (s²LC·R_Q + sL + R_Q) / (s²LC + 1)
```

The voltage transfer function into load R_load:

```
                       Z_shunt(s) ∥ R_load
H_hf_boost(s) = ─────────────────────────────────────────
                 R_series + Z_shunt(s) ∥ R_load
```

At resonance (s = jω_0), the LC tank impedance maximizes, allowing maximum signal through. The boost amount depends on the ratio of R_load to R_series at resonance. Maximum boost ≈ 18 dB occurs at narrow bandwidth (high Q, minimum R_Q) into a well-matched load.

### Bandwidth (Q) Range

- **Wide bandwidth (broad):** Q ≈ 0.3 to 0.5. Very gentle, covers 2+ octaves around center frequency. Maximum gain is reduced compared to narrow setting (the Sound on Sound review notes "maximum gain 9dB greater when the bandwidth is set to narrow").
- **Narrow bandwidth (sharp):** Q ≈ 1.5 to 3.0. More selective peak, up to 18 dB at center frequency.
- **Control:** 10K potentiometer R_bHiQ. As pot increases resistance → more damping → broader peak, lower peak gain. As pot decreases → less damping → narrower peak, higher peak gain.

### Filter Shape

At moderate gain settings and narrow bandwidth: **true peaking band** (bell shape). At wide bandwidth settings with significant boost: the peak is so broad it begins to resemble a high-frequency shelving boost rather than a discrete peak. The shape never reaches a true high-frequency shelf even at maximum bandwidth — it always has a resonant peak structure with declining gain above the tuned frequency.

At high gain and narrow bandwidth (e.g., 18 dB at 5 kHz, narrow), the boost extends 1–2 octaves above and below the center frequency, creating the "air" effect on vocals and cymbals.

---

## 6. HF Cut Section

### Architecture

The HF cut is a **first-order RC shelving cut** (low-pass shelf). It uses a capacitor C_cHi in shunt to ground through the cut control resistor R_cHi, creating a high-frequency rolloff.

The HF cut has a separate frequency selector switch with three positions: 5, 10, and 20 kHz. Different capacitor values select the corner frequency:

```
f_c = 1 / (2π · R_cHi · C_cHi)
```

### Transfer Function H_hf_cut(s)

```
                        1
H_hf_cut(s) = ─────────────────────────────
               1 + s · β · R_cHi · C_cHi
```

where β is the pot wiper position (0 = no cut, 1 = maximum cut).

This is a **first-order low-pass shelving function**. The rolloff slope is **-6 dB/octave** above the corner frequency, approaching a shelf floor set by the circuit loading at very high frequencies (does not go to -∞).

### Characteristics

- **Shape:** Simple 1st-order RC low-pass shelf. No resonance. Very gentle rolloff.
- **Maximum cut:** Up to 20 dB (16 dB in the latest reissue spec) above the corner frequency.
- **Frequencies:** 5, 10, and 20 kHz. The 5 kHz setting creates a broad "air roll" effect affecting the upper midrange; 10 kHz and 20 kHz are used for tape-speed presence adjustments or de-essing effects.
- **Interaction with HF boost:** Since both sections share the same signal path nodes, the cut section's RC loading affects the Q of the HF boost's parallel LC tank. In practice, the cut and boost sections are usually not used simultaneously at the same frequency (unlike the LF "trick"), though they can be.

---

## 7. MEQ-5 Topology

### Overview

The MEQ-5 (Midrange Equalizer, Program 5) is a companion unit designed to complement the EQP-1A. It contains three separate passive peaking filter sections:

1. **Low-Mid Boost:** 200, 300, 500, 700, 1000 Hz (0–10 dB peak boost)
2. **Mid Dip (Cut):** 200, 300, 500, 700, 1000, 1500, 2000, 3000, 4000, 5000, 7000 Hz (0–10 dB cut)
3. **High-Mid Boost:** 1500, 2000, 3000, 4000, 5000 Hz (0–8 dB peak boost)

Note: Some sources list the dip section as covering 200–7000 Hz with 11 positions, while others show 200–5000 Hz. The broad overlap of the dip range allows it to be used in conjunction with either boost section.

Tubes: 12AX7 (gain stage), 6AQ5 (output), 6X4 (rectifier) — a different configuration from the EQP-1A.

### Filter Topology for MEQ-5 Sections

Each of the three sections uses a **series resonant LC peaking circuit** placed as a shunt element (analogous to the EQP-1A HF boost but tuned to the midrange). The boost and cut sections each use their own switched multi-tap inductor. The MEQ-5 uses **3 multi-tap inductors** total.

For the boost sections: the LC series tank presents minimum impedance at resonance, creating a shunt that diverts signal away from load — which when viewed in the passive voltage divider context provides a **mid-frequency dip**. The signal taken across the tank itself is the boosted output. This is the classic series-resonant topology for peaking: at resonance, signal is extracted from the LC shunt, amplified by the makeup stage, and added back.

More precisely: the series LC forms a band-pass filter in the voltage divider. The boost potentiometer varies how much of the LC branch is in the signal path. At resonance (ω_0 = 1/√LC), the LC impedance drops to just the coil DCR, providing maximum coupling.

### Peaking Filter Transfer Function (MEQ-5 sections)

For a series RLC peaking section with series resistance R_damp (combining DCR, potentiometer, and source/load):

```
                     s·(L/R_damp)
H_peak(s) = ─────────────────────────────────────────
              s² + s·(R_damp/L) + ω_0²
```

This is a **2nd-order bandpass (peaking) filter**. The Q factor:

```
Q = ω_0 · L / R_damp = 1 / (R_damp · ω_0 · C) = (1/R_damp) · √(L/C)
```

The Q is determined by the coil DCR and the circuit resistances. The MEQ-5 all-inductor DCRs (measured on the HF section) run 14–34Ω, giving moderate Q values.

### Section Interaction

The MEQ-5 sections are described as "three independent sets of controls," but like the EQP-1A, they are passive and connected to the same signal nodes without buffering. They do interact electrically, but the interaction is less dramatic than the EQP-1A boost+cut trick because:
- The boost sections target different frequency ranges (low-mid vs. high-mid)
- The dip section can be set to a frequency different from either boost section
- There is no fundamental topology mismatch (all three sections are LC-based peaking)

In practice, the MEQ-5 sections are used as three quasi-independent peaking filters. The fixed Q values (not adjustable by any bandwidth knob) are approximately:

- **Low-Mid Boost (200–1000 Hz):** Q ≈ 1.0 to 2.0 (moderate, broad peak). Fixed per frequency position.
- **High-Mid Boost (1.5–5 kHz):** Q ≈ 1.5 to 3.0 (slightly narrower in the higher register due to inductor characteristics).
- **Dip section:** Q ≈ 0.7 to 1.5. The dip is inherently broader than the boost for the same inductor, because the loading resistances are asymmetric.

Unlike the EQP-1A, **no bandwidth control exists on any MEQ-5 section**. The Q is fixed by the component values and the pot resistance at maximum. This is the key UI distinction: the MEQ-5 is purely frequency + depth, with no shape control.

### Filter Order

Each MEQ-5 section is a **2nd-order (biquad) bandpass** — same as the EQP-1A HF boost. The overall MEQ-5 has 6th-order filter response (three cascaded 2nd-order sections), plus any cross-loading effects between sections at the shared nodes.

---

## 8. Tube Makeup Amplifier

### Configuration (EQP-1A)

The EQP-1A uses a **fully balanced push-pull tube amplifier** with two dual-triode valves:
- **Input stage:** ECC83 (12AX7) — high gain, high impedance
- **Output stage:** ECC82 (12AU7) — lower gain, lower output impedance
- **Rectifier:** 6X4 (full-wave vacuum rectifier)
- **Power supply:** 117V AC input → ~325V DC HT; regulated to 290V for output anodes, 140V for input stages

Negative feedback: derived from an extra secondary winding on the output transformer, fed back to the cathodes of the input valves. This reduces output impedance and harmonic distortion, but the circuit is not fully linearized — some tube character remains.

**Specified output impedance:** ~50Ω (with transformer and feedback).  
**Maximum output:** +21 dBm into 600Ω.  
**THD spec:** ≤ 0.15% at +10 dBm into 600Ω.

### Harmonic Distortion Character

The push-pull arrangement largely cancels odd harmonics (3rd, 5th) through symmetry, leaving **predominantly 2nd-harmonic distortion** at low to moderate levels. At higher signal levels approaching clipping, the onset of asymmetric saturation (from unit-to-unit variations in tube matching) can introduce some 3rd harmonic. Typical character:

- **2nd harmonic:** Primary component, -40 to -50 dB below fundamental at normal operating levels
- **3rd harmonic:** Secondary, -55 to -65 dB below fundamental
- Even harmonics dominate: perceived as warmth rather than harshness

### Soft Compression Behavior

As signal level increases beyond about +10 dBm, the tube operating point shifts and gain decreases slightly (soft saturation). This is not the hard limiting of a solid-state circuit; rather, it is a gentle, frequency-dependent gain reduction that is most audible on peaks. This is musically perceived as "glue" on transients.

### DSP Emulation of the Tube Stage

For a faithful digital emulation:

1. **Static waveshaper** approximating 2nd-harmonic saturation (e.g., soft polynomial waveshaper: y = x - k·x³ for very small k, or tanh-family). This is the simplest approach.
2. **Dynamic (level-dependent) gain modulation** to model the soft compression. A forward-path gain element that decreases slightly with RMS level.
3. **Full triode model** using the Koren or Ayumi equations with WDF integration — more accurate but computationally intensive (see SMC 2024 future work suggestions, referencing [30][31] from Karjalainen & Pakarinen).
4. **Neural network tube model** as in Darabundit et al. DAFx 2022 [32] — black-box model trained on I/O measurements of the actual 12AX7/12AU7 gain stage.

The SMC 2024 paper (Barrera et al.) explicitly excludes the gain stage from their WDF model and identifies it as the primary remaining source of character not captured by the filter model alone.

### Output Impedance Interaction

The 50Ω output impedance is low enough that normal load variations (600Ω to 300Ω) have minimal effect on frequency response. However, the input impedance of the passive filter section (before the amp) is nominally 600Ω, and any deviation from this changes the insertion loss and the Q of the filter sections. This is relevant when cascading the MEQ-5 with the EQP-1A in a real studio — the output impedance of the first unit loads the input of the second, shifting frequency responses slightly.

---

## 9. Filter Primitives Needed (Summary Table)

| Section             | Filter Type                    | Order | Typical Q        | Shape at Max Gain                       | Notes                                              |
|---------------------|--------------------------------|-------|------------------|-----------------------------------------|----------------------------------------------------|
| LF Boost            | Low-frequency shelving boost   | 2nd   | 0.5–1.5          | Flat low shelf, ~6dB/oct transition     | LC series shunt; Q set by DCR and circuit R         |
| LF Cut              | High-pass shelving cut         | 1st   | N/A (no resonance)| 6dB/oct rolloff above corner            | RC shunt; corner ~30–50% above labeled freq        |
| LF Boost+Cut (Trick)| Resonant shelf + dip           | 3rd   | Peak Q ≈ 1.0     | Shelf boost + mid dip 1–2 oct above     | Emerge from circuit interaction; not additive      |
| HF Boost            | Peaking (resonant bandpass)    | 2nd   | 0.3–3.0 variable | Bell curve, peaking at labeled freq     | Parallel LC; Q controlled by bandwidth pot         |
| HF Cut              | Low-pass shelving cut          | 1st   | N/A              | 6dB/oct rolloff above corner            | RC shunt; same topology as LF cut but HF tuned     |
| MEQ-5 Low-Mid Boost | Peaking bandpass               | 2nd   | ~1.0–2.0 fixed   | Bell curve                              | Series LC; no user Q control                       |
| MEQ-5 Dip/Cut       | Notch-like bandpass cut        | 2nd   | ~0.7–1.5 fixed   | Broad dip                              | Series LC; fixed Q                                  |
| MEQ-5 High-Mid Boost| Peaking bandpass               | 2nd   | ~1.5–3.0 fixed   | Bell curve, narrower than low-mid       | Series LC; fixed Q; no bandwidth control           |
| Tube Stage          | Memoryless waveshaper + level compression | — | —          | 2nd-harmonic dominant saturation        | Soft polynomial or tanh; add gentle gain compression |

---

## 10. Key References Found

### Academic Papers

- **Barrera, A., Lizarraga-Seijas, X., and Font, F. (2024).** "Modeling the Pultec EQP-1A with Wave Digital Filters." *Proceedings of the Sound and Music Computing Conference (SMC 2024)*, pp. 445–452. Barcelona: Universitat Pompeu Fabra. — [https://smcnetwork.org/smc2024/papers/SMC2024_paper_id132.pdf](https://smcnetwork.org/smc2024/papers/SMC2024_paper_id132.pdf)  
  **Primary source for this document.** Contains LTSpice circuit schematic, WDF implementation tree, frequency response validation plots, and performance benchmarks. Figure 3 shows the simplified circuit with component labels (C1, L1, R_bHi1/2, R_bLo, R_cLo, R_cHi1/2, C_bLo, C_cLo, C_cHi, R1–R4). Figure 6 shows the WDF tree.

- **Zenodo preprint companion:** [https://zenodo.org/records/8382764](https://zenodo.org/records/8382764)

- **GitHub implementation (C++/JUCE using chowdsp_wdf):** [https://github.com/ABSounds/EQP-WDF-1A](https://github.com/ABSounds/EQP-WDF-1A)

- **Anthon, G., Lizarraga-Seijas, X., and Font, F. (2023).** "Pywdf: an open source library for prototyping and simulating wave filter circuits in python." *DAFx-23*, pp. 335–341. — Referenced in SMC 2024 paper as the Python prototyping tool.

- **Chowdhury, J. (2022).** "chowdsp_wdf: An Advanced C++ Library for Wave Digital Circuit Modelling." arXiv:2210.12554. — [https://arxiv.org/abs/2210.12554](https://arxiv.org/abs/2210.12554). The C++ WDF library used in the EQP-WDF-1A plugin.

- **Werner, K.J. (2016).** "Virtual Analog Modeling of Audio Circuitry Using Wave Digital Filters." Ph.D. dissertation, Stanford University. — Foundational WDF reference for audio circuit modeling.

- **Fettweis, A. (1986).** "Wave digital filters: Theory and practice." *Proceedings of the IEEE*, vol. 74, no. 2, pp. 270–327. — Original WDF theory.

- **Karjalainen, M. and Pakarinen, J. (2006).** "Wave digital simulation of a vacuum-tube amplifier." *ICASSP 2006*, vol. 5. — WDF tube modeling approach for the gain stage.

- **Pakarinen, J. and Karjalainen, M. (2009).** "Enhanced wave digital triode model for real-time tube amplifier emulation." *IEEE TASLP*, vol. 18, no. 4, pp. 738–746.

- **Darabundit, D., Roosenburg, D., and Smith III, J.O. (2022).** "Neural net tube models for wave digital filters." *DAFx-22*, pp. 153–160. — Neural network approach to tube modeling within WDF.

### Hardware/Schematic Resources

- **Gyraf Audio G-Pultec schematic and DIY documentation:** [https://www.gyraf.dk/gy_pd/pultec/pultec.htm](https://www.gyraf.dk/gy_pd/pultec/pultec.htm) — Open-source Pultec clone schematic; referenced in SMC 2024 paper as one of the three circuit references used. Contains filter schematic PDFs.

- **Dave Library (EQP-1R schematic):** http://www.davegroupjapan.com/ekoukoku9.html — Referenced in SMC 2024 as the EQP-1R variant schematic.

- **Pulse Techniques (Pultec) EQP-1A User Manual:** https://www.thehistoryofrecording.com/Manuals/Pultec/Pultec_EQP-1A_Manual.pdf

- **VintageWindings EQ-1A Inductor (corrected component values):** [https://vintagewindings.com/products/VintageWindings%20EQ-1A.html](https://vintagewindings.com/products/VintageWindings%20EQ-1A.html) — Provides corrected inductor values (mH) and capacitor values (nF) for all 7 HF frequency positions.

- **MobyTransformers EQP1A Inductor:** [https://www.mobytransformers.com/eqp1a-inductor](https://www.mobytransformers.com/eqp1a-inductor) — Wound to original vintage spec.

### Review and Technical Analysis

- **Robjohns, H. (2019).** "Pulse Techniques EQP-1A." *Sound on Sound*, February 2019. [https://www.soundonsound.com/reviews/pulse-techniques-eqp-1a](https://www.soundonsound.com/reviews/pulse-techniques-eqp-1a) — Contains measurement-based confirmation of cut corner frequency offset, HF section bandwidth gain difference, tube configuration.

- **GroupDIY Forum — Pultec EQP-1A stock inductor values and HF response curves:** [https://groupdiy.com/threads/pultec-eqp-1a-stock-inductor-values-and-hf-response-curves.63207/](https://groupdiy.com/threads/pultec-eqp-1a-stock-inductor-values-and-hf-response-curves.63207/)

- **GroupDIY Forum — Pultec EQP-1A3 solid-state clone LF boost question:** [https://groupdiy.com/threads/pultec-eqp-1a3-solid-state-clone-lf-boost-question.70993/](https://groupdiy.com/threads/pultec-eqp-1a3-solid-state-clone-lf-boost-question.70993/) — Contains discussion of the LF boost circuit mechanism (capacitor bypass of voltage divider).

- **Barrera et al. SMC 2024 Figure 2** (published in paper above): Shows measured frequency response curves for the simultaneous boost+cut at 20, 30, 60, and 100 Hz — the key reference shape for the Pultec trick.

- **Rane Note 122 — Operator Adjustable Equalizers:** [https://www.ranecommercial.com/legacy/note122.html](https://www.ranecommercial.com/legacy/note122.html) — Historical context for passive EQ topology classification (bridged-T, series resonant).

### DIY Build Documentation

- **illdigger.wordpress.com — "Trio de Pultec from scratch Pt.1 — The EQP-1LL":** [https://illdigger.wordpress.com/2017/11/10/trio-de-pultec-from-scratch-pt-1-the-eqp-1ll/](https://illdigger.wordpress.com/2017/11/10/trio-de-pultec-from-scratch-pt-1-the-eqp-1ll/) — Confirms bridge-T topology description, component selection methodology.

- **illdigger.wordpress.com — "Trio de Pultec from scratch Pt.2 — The MEQ-5":** [https://illdigger.wordpress.com/2018/02/27/trio-de-pultec-from-scratch-pt-2-the-meq-5/](https://illdigger.wordpress.com/2018/02/27/trio-de-pultec-from-scratch-pt-2-the-meq-5/) — MEQ-5 uses 3 multi-tap inductors; film caps only in filter section; API 2520 solid-state makeup.

- **DIY Recording Equipment — "Designing a 500-Series Pultec-style EQ":** [https://www.diyrecordingequipment.com/blogs/news/114110532-designing-a-500-series-pultec-style-eq](https://www.diyrecordingequipment.com/blogs/news/114110532-designing-a-500-series-pultec-style-eq) — Confirms basic filter topology description; independent LF+/LF- frequency controls vs. shared in original.

- **Korneff Audio — "Audio 101: From Passive Components to a Passive EQ to a Plug-in":** [https://korneffaudio.com/audio-101-from-passive-components-to-a-passive-eq-to-a-plug-in/](https://korneffaudio.com/audio-101-from-passive-components-to-a-passive-eq-to-a-plug-in/) — Explains the passive boost-via-cut mechanism conceptually and contrasts component-level vs. block-level DSP modeling.

---

## Appendix A: Practical Implementation Notes for C++/JUCE

### Biquad-per-section approach (simplified, non-interacting)

For a pragmatic first implementation that captures the character without full WDF:

**LF Boost (per frequency):**
```
Type: Low-shelf biquad (Audio EQ Cookbook rbj_low_shelf)
ω_0: 2π × f_lf  (f_lf = 20, 30, 60, or 100 Hz)
S (shelf slope): 1.0
Gain: boost_dB (0 to +13.5 dB)
Q: 0.707 (Butterworth shelving — approximation)
```

**LF Cut (per frequency):**
```
Type: High-shelf biquad (inverted, or high-pass shelving cut)
ω_0: 2π × f_lf × 1.4  (corner ~40% above labeled frequency)
Gain: -cut_dB (0 to -17.5 dB)
Q: 0.5 (very gentle, first-order approximation)
```

**LF Boost+Cut interaction correction:**
Add a peaking biquad to represent the loading resonance that appears only when both are active:
```
Type: Peaking biquad
ω_0: 2π × f_lf × 0.8  (slightly below labeled frequency)
Gain: +2 to +4 dB (proportional to min(boost_dB, cut_dB))
Q: 1.0 (fixed)
```
This is approximate. A WDF implementation eliminates the need for this correction term.

**HF Boost (per frequency):**
```
Type: Peaking biquad
ω_0: 2π × f_hf  (f_hf = 3, 4, 5, 8, 10, 12, or 16 kHz)
Gain: boost_dB (0 to +18 dB)
Q: 0.3 + bandwidth_pot × 2.7  (Q from 0.3 at wide to 3.0 at narrow)
```

**HF Cut (per frequency):**
```
Type: High-shelf biquad (cut)
ω_0: 2π × f_hf_cut  (f_hf_cut = 5, 10, or 20 kHz)
Gain: -cut_dB (0 to -16 dB)
Q: 0.5 (first-order shelf approximation)
```

**MEQ-5 sections:**
```
Each section: Peaking biquad
Low-mid boost: Q ≈ 1.5, ω_0 at selected frequency (200–1000 Hz)
Dip:           Q ≈ 1.0, ω_0 at selected frequency (200–7000 Hz), negative gain
High-mid boost: Q ≈ 2.0, ω_0 at selected frequency (1500–5000 Hz)
```

### WDF approach (accurate, recommended for production)

See the chowdsp_wdf library (https://github.com/Chowdhury-DSP/chowdsp_wdf) and the ABSounds EQP-WDF-1A implementation (https://github.com/ABSounds/EQP-WDF-1A) for a complete, validated C++/JUCE implementation of the passive EQ stage. Key points from the SMC 2024 paper:

- Requires oversampling (4x recommended, 2x minimum) to reduce bilinear-transform warping errors at high frequencies
- RMSE against hardware at 192 kHz: LF Boost 9.37e-3, LF Cut 1.39e-4, HF Boost 2.20e-1, HF Cut 3.96e-3
- The HF Boost has the highest error at 48 kHz (RMSE 1.40) because the resonant peak is near the Nyquist limit for the 3–16 kHz range; higher oversampling is critical here
- Real-time performance at 48 kHz: 217× real-time ratio on a mid-range desktop (i7-8550U) — suitable for production use

---

*Research compiled May 2026. Primary circuit analysis based on SMC 2024 paper (Barrera et al.) and supporting sources as cited.*
