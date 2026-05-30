# Transformer Modeling Plan — Pultec EQP-1A

**Date opened:** 2026-05-29
**Owner:** TBD
**Tracker:** `PULTEC_DEV.md` Phase 7
**Trigger:** HF Boost LF-coupling artifact (open issue in `PULTEC_DEV.md`)

---

## 1. Why this matters

The real Pultec EQP-1A signal path is:

```
Input → Input XFMR (1:1 600 Ω) → Passive LC EQ → Inter-stage XFMR (1:2 step-up) → 12AX7 → 12AU7 → Output XFMR (Peerless S-217D, w/ tertiary NFB winding) → Output
```

Our current WDF implementation collapses this into:

```
Input → Passive LC EQ → MAKEUP_GAIN scalar → Koren triode (4× OS) → Output
```

Three transformers omitted, two tubes collapsed into one, output transformer's tertiary feedback loop ignored. The simplification was deliberate (per `PULTEC_DEV.md` Phase 4 spec) — but two consequences are now confirmed measurable:

1. **HF Boost LF coupling (~5.5 dB shelf)** — caused by the missing **inter-stage transformer**. The HF Boost pot adjusts impedances inside the passive network; in the real device the inter-stage transformer's high primary inductance presents a near-infinite load to those changes at all but the lowest frequencies, decoupling them from the output. Without it, the pot's full impedance variation propagates to the output formula via `voltage(J)`.
2. **Saturation feels "digital"** — caused partly by the missing transformer saturation contribution (separately tracked in `saturation-revisit.md`). Output and inter-stage transformers both add even-order harmonics under drive.

This planner addresses (1). Saturation is its own document.

---

## 2. What "modeling a transformer" means in WDF

A real transformer is a coupled inductor with three regimes:

| Regime | Behaviour | DSP cost to model |
|---|---|---|
| **Linear, in-band** (~30 Hz – 30 kHz, low signal) | Acts as a ~unity gain element with phase shift; primary inductance presents impedance to source. | Trivial — a 2-port linear network. |
| **Linear, out-of-band** | LF: primary inductance becomes finite-Z → LF roll-off. HF: leakage inductance + parasitic capacitance → HF roll-off and possible resonance peak. | Moderate — add LF/HF poles or zeros, or model LC parasitics explicitly. |
| **Saturating** | Core flux exceeds linearity → smooth compression, even-harmonic distortion, hysteresis. | High — needs nonlinear B-H curve (Jiles-Atherton or similar). |

For the LF coupling problem specifically, only the **linear in-band** regime matters. The inter-stage transformer's job in our context is **impedance isolation**: present a high impedance back into the EQ output node so the pot-divider effect doesn't propagate.

---

## 3. Three modeling fidelity tiers

### Tier 1 — Output-stage high-pass (workaround, no WDF change) ⭐ recommended first

**Concept:** Don't model the transformer. Instead, add a single one-pole high-pass filter on the EQ output (post-WDF, pre-saturator) tuned to mimic the inter-stage transformer's LF roll-off (~20 Hz, -3 dB).

**What it fixes:** The constant LF shelf is shifted out of the audio band. At 60 Hz the lift would drop from +5.46 dB to <1 dB (the HP rolls the artifact down with the bass).

**What it doesn't fix:**
- The artifact is still mathematically present in the WDF output; the HP just hides it below the audio band's bottom.
- LF Boost section (which legitimately should lift 20-100 Hz) would also be high-passed — would need a workaround like inserting the HP only on the HF Boost contribution, which is impossible without restructuring the output formula.
- No transformer harmonic colour.

**Effort:** 1 hour. One biquad coefficient set, one stage, one tuning parameter.

**Verdict:** Quick A/B test to confirm hypothesis. If the LF artifact tracks the HP cutoff as expected, that validates the diagnosis. Then move to Tier 2 or 3 for the real fix.

### Tier 2 — Ideal transformer R-type adaptor (canonical fix)

**Concept:** Insert an ideal transformer 2-port between the existing EQ output (currently `voltage(J)`, `voltage(R3)`) and a new "amp grid" node. Use `chowdsp::wdft::RtypeAdaptor<4>` (or 3-port if the formula collapses neatly) with a custom impedance function that implements the transformer's port impedance matrix.

**WDF transformer port equations** (ideal, turns ratio n, primary inductance Lp, secondary terminated by R_L):

For an ideal 2-port transformer with primary on port 1 (port impedance R1) and secondary on port 2 (R2):

```
v2 = n · v1
i2 = -i1 / n
```

The port impedance constraint for the WDF adaptor:
```
R1 = R2 / n²        (impedance reflected from secondary)
R2 = n² · R1        (impedance reflected from primary)
```

Add finite primary inductance Lp as a shunt element across the primary; this turns the transformer into a 3-port (primary, secondary, Lp). The Lp is what gives us the LF roll-off and the desired impedance isolation.

**Where to insert in the WDF tree:** The inter-stage transformer sits BETWEEN the passive EQ output and the 12AX7 grid. In our WDF tree the EQ output is sampled (not a node we drive into), so the cleanest approach is:

```
... S3 (existing) → primary of inter-stage XFMR
                  → secondary feeds a HIGH-Z dummy load (representing 12AX7 grid, ~1 MΩ)
                  → output sampled at secondary voltage
```

This requires either:
1. Restructuring the WDF tree so S3 is no longer the root (currently `Vin → S3`).
2. Adding the transformer as an R-type with the secondary load baked into its impedance matrix.

**Effort:** 4-8 hours. Most time spent verifying the R-type impedance function matches the analytic transformer behavior.

**Verification:** New unit test: drive 1 V step into transformer primary, measure secondary voltage rise time and DC ratio. Verify against `Lp/R_L = τ` time constant.

**Verdict:** Right level of fidelity for the EQ-only side of the problem.

### Tier 3 — Full physical model (input + inter-stage + output XFMRs, with hysteresis)

**Concept:** Add all three transformers with proper RLC parasitic models AND nonlinear core saturation (Jiles–Atherton or Preisach hysteresis). The output transformer also implements the tertiary feedback winding to the 12AX7 cathodes.

**What it fixes:** Authentic Pultec sound including:
- LF coupling artifact (input + inter-stage isolation)
- Transformer harmonic colour at moderate-to-high signal
- Output transformer's NFB-mediated tube/transformer interaction
- "Bypass-isn't-bypass" character (signal always travels the transformer + tube chain)

**Effort:** 30-60 hours engineering + tuning. References: Massarini-Kazimierczuk 2000s transformer modeling, Jiles 1992 B-H model, Cohen-Helie 2011 for the audio-circuit specific tuning.

**Verdict:** Out of scope for the immediate LF-coupling fix. Worth a separate milestone (Phase 9 or v2) if pursuing full vintage character.

---

## 4. Recommended sequence

1. **Tier 1 sanity check** — Insert one-pole HP at 20 Hz on the EQ output, observe whether LF artifact tracks the cutoff. ~1 hour. Goes/no-goes the hypothesis.
2. **Tier 2 implementation** — If (1) confirms hypothesis, build the R-type inter-stage transformer. ~1-2 days.
3. **Tier 3 deferred** — Add input + output transformer modeling as a separate milestone once Tier 2 lands and stabilizes. ~Week.

---

## 5. Open questions / unknowns

- **Inter-stage transformer turns ratio:** Documented as 1:2 step-up in `pultec-character-guide.md`. Need to verify against an actual schematic — some clones use 1:1.5 or 1:1.78.
- **Primary inductance value:** Not in any of our research docs. Typical audio inter-stage XFMR primary: 10–50 H. We'll need to either measure or sweep this in tuning.
- **Output transformer NFB topology:** The Peerless S-217D has a hidden tertiary feeding the 12AX7 grid (per `pultec-character-guide.md`). Modeling this requires both transformers AND the triode in a single closed loop, which a feed-forward WDF tree can't represent without iteration. May need to fix-point iterate or use IIR approximation.
- **Whether ABSounds models transformers:** Verified 2026-05-29 — they don't. They get away with it because they target a flat baseline (their default settings) where the LF coupling is also present but unmeasured. Our diagnostic exposed it because we tested moderate boost settings explicitly.

---

## 6. Test plan (post-fix)

Add to `tests/runtests.cpp` after Phase 7 lands:

```cpp
// T9: HF Boost LF coupling must be < 0.5 dB at 60 Hz across all settings
void test_pultec_hf_boost_isolation()
{
    for (float bw : { 0.1f, 0.5f, 0.9f })
        for (float boost : { 3.0f, 6.0f, 12.0f, 16.0f })
            for (int attenSel : { 0, 1, 2 })
            {
                // Measure baseline and boost; LF (60 Hz) diff must be < 0.5 dB.
                // The HF peak should be the dominant feature, not the LF lift.
            }
}
```

The threshold (0.5 dB) is chosen as the just-noticeable difference for shelf-shape changes at low frequencies; tighter than that hits measurement noise.

---

## 7. Risk register

| Risk | Mitigation |
|---|---|
| R-type impedance function gets the port semantics wrong → silence or NaN | Unit test the transformer in isolation before wiring into the main tree |
| Inserting the transformer changes baseline insertion loss → MAKEUP_GAIN re-tune | Already done once (15.4882 → 16.598); re-tune again after transformer lands |
| WDF tree restructure breaks existing tests (T1–T8) | Land Tier 1 first as a feature flag toggle; A/B tests against the old path |
| Transformer's LF time constant interacts with envelope follower → click on parameter changes | Smooth the inductance parameter or pre-roll the transformer state |
