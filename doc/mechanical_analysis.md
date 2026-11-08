# Mechanical Analysis — Suspended Spheres

> Physics check for the slow-motion design: force/torque budgets, motor speed,
> the sine frequency ceiling, thread behaviour, drum winding, and scale accuracy.
>
> **This revision supersedes the earlier 4.5 m/s free-fall analysis.** The
> designer wants very slow, smooth, hypnotic motion; free-fall speeds and a
> 1 cm braked catch are not required.

---

## 1. Parameters

| Parameter | Symbol | Value |
|-----------|--------|-------|
| Ball mass | `m` | ≤ 0.050 kg |
| Ball diameter | `D_ball` | 60–80 mm (hollow chrome-finish plastic) |
| Ball count | `N` | 100–120 (120 baseline) |
| Vertical travel | `h` | ~2.0 m |
| Installation footprint | — | ~3 × 3 m (3 × 3.5 m possible) |
| Ceiling height | — | 3.0 m |
| Grid pitch | — | ~0.25–0.30 m (proposal) |
| Nominal fast speed | `v_max` | ≈ 0.44–0.59 m/s (2 m in 4–5 s, ASM-015) |
| Optional effect speed | `v_opt` | ≈ 0.8–1.3 m/s (2 m in 2–3 s, ASM-015) |
| Nominal acceleration | `a_max` | ≈ 1.0–1.5 m/s² |
| Optional acceleration | `a_opt` | ≈ 2–4 m/s² |
| Gravity | `g` | 9.81 m/s² |
| Drive | — | Stepper shaft is the spool. No gear. Ratio 1. |
| Microstepping | — | TBD. The step-rate tables are worked cases at 1/8 and 1/16. |

> Motor size, drum diameter, thread, and microstepping are still open.
> This document gives the load envelope they must meet. The drive ratio
> is 1: the spool is the stepper shaft.

---

## 2. Motion Regime

Motion is primarily slow and smooth, with a faster tier for occasional
effects. For a symmetric trapezoid (`accel = decel = a`), the stroke time is
`T = D/v + v/a`; the nominal 2 m stroke therefore takes:

| Setting | Peak `v` | Accel `a` | Stroke time |
|---------|----------|-----------|-------------|
| Very slow choreography | ≤ 0.3 m/s | ≤ 0.5 m/s² | ≥ 6.7 s |
| **Nominal fast** | 0.44–0.59 m/s | 1.0–1.5 m/s² | **4–5 s** |
| Optional 3 s effect | ≈ 0.8 m/s | ≈ 2 m/s² | 2.9 s |
| Optional 2 s effect (stretch) | ≈ 1.2 m/s | ≈ 4 m/s² | 2.0 s |

There is **no free-fall mode** and therefore no catch/brake sizing case. The
worst-case motor load is a lift at the optional 4 m/s² tier:

```
F_up = m (g + a) = 0.050 × (9.81 + 4.0) = 0.691 N
```

Compare with the static hold:

```
F_hold = m g = 0.050 × 9.81 = 0.491 N
```

Downward acceleration stays below `g` even at 4 m/s², so the thread never goes
slack; the minimum tension is `m (g − a) ≈ 0.29 N`.

### 2.1 Hypnotic sine: amplitude and frequency

Treat the hypnotic motion as one vertical sine. Positive is upward. Travel is
~2 m, so a wave centred in the stroke has peak amplitude `A ≤ 1 m`
(peak-to-peak `2A`).

```
y(t)    = A sin(2π f t)
a_peak  = A (2π f)²
```

`FR-MOT-012` requires the thread to remain in tension: across the operating
envelope the commanded downward acceleration stays below `g`. On this sine the
downward peak is at the upper crest, where the tension is `m (g − a_peak)`.
Crest tension reaches zero at `a_peak = g`, which fixes the highest frequency
for a given amplitude. A played wave stays below that frequency.

```
f_max = (1 / 2π) √(g / A)
A_max = g / (2π f)²
T_min = 2π √(A / g)
v_peak = √(A g)
```

`g = 9.81 m/s²`. Frequency scales as `1/√A`: halving the amplitude raises
`f_max` by `√2`. Each entry below is rounded from those expressions.
Superposed waves share the acceleration budget, so the ceiling applies to
the sum.

| Amplitude `A` | Peak-to-peak | `f_max` | `T_min` | Crest speed |
|---------------|--------------|---------|---------|-------------|
| 1.00 m | 2.00 m | 0.498 Hz | 2.01 s | 3.13 m/s |
| 0.50 m | 1.00 m | 0.705 Hz | 1.42 s | 2.21 m/s |
| 0.25 m | 0.50 m | 0.997 Hz | 1.00 s | 1.57 m/s |
| 0.10 m | 0.20 m | 1.576 Hz | 0.63 s | 0.99 m/s |
| 0.05 m | 0.10 m | 2.229 Hz | 0.45 s | 0.70 m/s |
| 0.02 m | 0.04 m | 3.525 Hz | 0.28 s | 0.44 m/s |
| 0.01 m | 0.02 m | 4.985 Hz | 0.20 s | 0.31 m/s |

Replacing `g` with a design acceleration `a` multiplies every frequency by
`√(a/g)`: ×0.639 at the optional 4 m/s², ×0.391 at the nominal 1.5 m/s².

Crest speed on the `g` ceiling is `√(A g)`. At `A = 1 m` that is 3.13 m/s,
above the §2 envelope (optional peak ≈ 0.8–1.3 m/s). For a full-stroke wave
the speed envelope sets the frequency, and crest acceleration stays below `g`.
The `g` ceiling is the tighter bound once `√(A g)` lies inside the envelope:
below `A ≈ 0.17 m` at 1.3 m/s, and below `A ≈ 0.035 m` at 0.59 m/s.

---

## 3. Force and Torque Budget

| Phase | Thread force | Torque @ 20 mm | @ 25 mm | @ 30 mm | @ 40 mm |
|-------|--------------|----------------|---------|---------|---------|
| Static hold | 0.491 N | 0.0049 Nm | 0.0061 Nm | 0.0074 Nm | 0.0098 Nm |
| Lift at 1.5 m/s² (nominal) | 0.566 N | 0.0057 Nm | 0.0071 Nm | 0.0085 Nm | 0.0113 Nm |
| Lift at 4 m/s² (optional) | 0.691 N | 0.0069 Nm | 0.0086 Nm | 0.0104 Nm | 0.0138 Nm |
| + reflected inertia (`m_r` ≈ 0.1 kg, 4 m/s²) | 1.091 N equivalent | 0.011 Nm | 0.014 Nm | 0.016 Nm | 0.022 Nm |

Even the smallest practical motor (NEMA-11, ≈ 0.06–0.2 Nm holding) has a large
margin, and a NEMA-14 or NEMA-17 pancake has 5–10× margin. Motor selection is
therefore driven by speed, noise, and integration, not torque.

### 3.1 De-energised detent (power-off hold)

Bench measurements with a 40 mm spool, motor unpowered:

| Motor | Spool Ø | Holds | Does not hold | Implied detent torque |
|-------|---------|-------|---------------|-----------------------|
| NEMA-17 pancake | 40 mm | 22 g (0.22 N) | 30 g (0.29 N) | 4.3–5.9 mNm |
| NEMA-17 full | 40 mm | 50 g (0.49 N); limit not tested | — | ≥ 9.8 mNm |

The pancake detent is only ≈ 4–6 mNm. At the measured 22 g threshold the torque
is ≈ 4.3 mNm, so holding the 50 g ball (0.49 N) on detent alone would need a
spool of ≈ 18 mm diameter — well below 20 mm. The full NEMA-17 holds 50 g at
40 mm, but its limit was not tested, so the 2× margin of `FR-MOT-008` is not
demonstrated on detent. `FR-MOT-011` allows a de-energised descent, so the
40 mm spool with a pancake is acceptable only where that descent is acceptable
(or a brake is fitted).

---

## 4. Motor Speed and Step Rate

The stepper shaft is the spool. There is no gear, so shaft speed is drum
speed and one motor revolution pays out one turn of thread.

```
RPM = v × 60 / (π × drum_Ø)
```

| Drum Ø | RPM @ 0.6 m/s | @ 0.8 m/s (3 s) | @ 1.2 m/s (2 s) |
|--------|---------------|-----------------|-----------------|
| 20 mm | 573 | 764 | 1,146 |
| 25 mm | 458 | 611 | 917 |
| 30 mm | 382 | 509 | 764 |
| 40 mm | 286 | 382 | 573 |

A larger drum lowers RPM and narrows the winding, at the cost of slightly more
torque. At 1.2 m/s even the 20 mm drum is only 1,146 RPM; the 40 mm drum is
573 RPM, which is much quieter.

Microstepping is TBD. The two tables are worked cases, not a selection.

**Worked case: 1/16** (steps/m = 3200 / (π × drum Ø)).
The last two columns are `v = f / steps_per_m` at the PIO ceiling
(200 kstep/s) and at the 2 ms playback chunk (64 kstep/s).

| Drum Ø | Linear step | @ 0.6 m/s | @ 0.8 m/s | @ 1.2 m/s | Max `v` at 200 kstep/s | Max `v` at 64 kstep/s |
|--------|-------------|-----------|-----------|-----------|------------------------|------------------------|
| 20 mm | 19.6 µm | 30,558 | 40,744 | 61,115 | 3.93 m/s | 1.26 m/s |
| 25 mm | 24.5 µm | 24,446 | 32,595 | 48,892 | 4.91 m/s | 1.57 m/s |
| 30 mm | 29.5 µm | 20,372 | 27,163 | 40,743 | 5.89 m/s | 1.88 m/s |
| 40 mm | 39.3 µm | 15,279 | 20,372 | 30,558 | 7.85 m/s | 2.51 m/s |

**Worked case: 1/8** (halve the rates, double the linear step):

| Drum Ø | Linear step | @ 0.6 m/s | @ 1.2 m/s | Meets 50 µm? |
|--------|-------------|-----------|-----------|--------------|
| 20 mm | 39.3 µm | 15,279 | 30,558 | ✅ |
| 25 mm | 49.1 µm | 12,223 | 24,446 | ✅ (no margin) |
| 30 mm | 58.9 µm | 10,186 | 20,372 | ❌ |
| 40 mm | 78.5 µm | 7,639 | 15,279 | ❌ |

The selected node is the Fly-Micro4. Its step generator is the RP2040 PIO
through FastAccelStepper: the queue rejects a period shorter than 80 ticks
at 16 MHz, so the maximum step rate is **200 kstep/s** per axis
(`16×10⁶ / 80`). The TMC2209 STEP input is faster than that. At 1/16 each
STEP advances 16 of the 256 microsteps, and the datasheet caps the finest
microstep rate at ½ fCLK (fullstep rate fCLK/512). On the internal 12 MHz
clock, at 1/16, that is fCLK/32 ≈ **375 kstep/s** at the STEP pin, with a
100 ns minimum high and low time. The limit scales with the microstep
setting. The 40 kHz figure in `controller_analysis.md`
§8.5 is the ESP32 I2S mux from the superseded platform survey.

The motion stream uses 2 ms `FasTimed` chunks of at most 128 steps
(`motion_streaming_whitepaper.md` §5.3). While that chunk length is kept,
continuous playback is **64 kstep/s** (`128 / 0.002 s`). That is the second
column. A shorter legal chunk can reach the 200 kstep/s pulse ceiling. The
2 ms length is the playback choice.

Consequences:

- **40 mm drum at 1/16** (worked case): 30.6 kstep/s at 1.2 m/s, 39.3 µm,
  573 RPM. Margin is 6.5× against the PIO ceiling and 2.1× against the 2 ms
  chunk (61 steps).
- **30 mm drum at 1/16** also covers 1.2 m/s (40.7 kstep/s, 81 steps in 2 ms)
  and gives 29.5 µm.
- **20 mm drum at 1/16** covers the step rate (61.1 kstep/s, 122 steps in
  2 ms) but runs at 1,146 RPM, with a ~20 mm winding.
- **25 mm drum at 1/8** sits at 49.1 µm, with no margin against the 50 µm
  limit once winding scale error is included.
- **30–40 mm drum at 1/8** fails the 50 µm resolution limit and is not usable.
  At 1/16, 1.2 m/s already fits the 2 ms chunk on every drum in the table.

In the 40 mm / 1/16 worked case the linear step is 39.3 µm, which leaves
~10.7 µm under `ASM-014`. The winding-scale requirement (`NFR-PHYS-007`)
still makes level-wind or radius calibration mandatory. A 30 mm drum at
1/16 gives 29.5 µm and still covers the 1.2 m/s tier. The microstep count
that will actually be used is still open.

---

## 5. Thread

Assume 0.5 mm Dyneema as a baseline (material is a design choice).

**Elongation.** For a 2.5 m suspension, `E ≈ 120 GPa`, `A = 1.96 × 10⁻⁷ m²`:

```
k = E A / L = 120 × 10⁹ × 1.96 × 10⁻⁷ / 2.5 ≈ 9.4 kN/m
```

| Load | Force | Elongation |
|------|-------|------------|
| Static hold | 0.491 N | ≈ 52 µm |
| Lift at 4 m/s² (optional tier) | 0.691 N | ≈ 73 µm |

This is within the 100 µm limit in `IR-MECH-002`. Thread stretch is a static
offset common to all balls (and partly taken up at homing), not a position
quantization term; the 50 µm `ASM-014` budget is shared by step quantization
and winding scale. In the 40 mm / 1/16 worked case the step is 39.3 µm.

**Strength.** `IR-MECH-002` requires ≥ 10× the maximum operating force
(≥ 5 N). A 0.5 mm Dyneema thread breaks around 100 N, giving a large margin.

---

## 6. Inertia

The reflected drum/motor inertia `m_r = I / r²` is roughly 1.5–2× the ball
mass (≈ 0.1 kg on a 20 mm drum). The extra force is `m_r a`, i.e. ≈ 0.15 N at
1.5 m/s² and ≈ 0.40 N at 4 m/s². It adds motor torque but not thread tension;
it is included in the torque table above.

---

## 7. Air Resistance

For an 80 mm sphere:
```
F_drag = 0.5 × 1.225 × 0.47 × π(0.04)² × v²
```
At 0.3 m/s this is ≈ 0.13 mN; at the 1.2 m/s optional ceiling it is ≈ 2.1 mN.
Even at 1.2 m/s that is ≈ 0.4 % of ball weight — negligible.

---

## 8. Drum and Winding

Two-and-a-half metres of 0.5 mm thread (2 m travel plus margin):

| Drum Ø | Turns | Single-layer width |
|--------|-------|-------------------|
| 20 mm | ≈ 40 | ≈ 20 mm |
| 25 mm | ≈ 32 | ≈ 16 mm |
| 30 mm | ≈ 27 | ≈ 13 mm |
| 40 mm | ≈ 20 | ≈ 10 mm |

A single layer fits on a practical drum width, preferably with a level-wind
(traverse) so the thread packs uniformly.

**Thread guidance (lesson learned).** The thread must be constrained, not
merely wound. On an open drum it can ride up the winding and jump off the
spool, especially when the take-off point moves or the thread goes slack.
Fit drum flanges and guide the thread, with a fairlead close to the drum, so
it always leaves from a controlled point and cannot escape the winding.

**Scale accuracy.** `NFR-PHYS-007` allows ≤ 50 µm of scale variation per grid
pitch (≈ 300 mm), i.e. ≈ 0.017 %. A second layer adds one thread diameter to
the radius — ≈ 4 % on a 25 mm drum, or ≈ 80 mm over the 2 m travel — far
outside the limit. A single layer with a level-wind, or calibration of radius
against turns, is required.

---

## 9. Swing / Pendulum

With a ~2 m suspension the pendulum frequency is:
```
f_p = (1 / 2π) √(g / L) ≈ 0.35 Hz
```

Slow vertical motion and a fixed, vertical thread path keep excitation low.
Avoid sustained vertical oscillation near `2 f_p`, and keep the take-off point
fixed so winding does not introduce lateral forces.

---

## 10. Structural Loads

The supporting structure is substantial steel beams and is the designer's
responsibility (`ASM-016`). For information:

- Static: 120 × 0.05 kg ≈ 6 kg → ≈ 59 N total
- Nominal dynamic (1.5 m/s²): ≈ 0.57 N per ball → ≈ 68 N total (≈ 15 % above static)
- Optional dynamic (4 m/s²): ≈ 0.69 N per ball → ≈ 83 N total (≈ 40 % above static)

There is no free-fall catch load.

---

## 11. Summary

| Aspect | Result | Note |
|--------|--------|------|
| Nominal fast speed | 0.44–0.59 m/s | 2 m in 4–5 s (ASM-015) |
| Optional effect speed | 0.8–1.3 m/s | 2 m in 2–3 s, not essential |
| Full-stroke time | ≈ 3.3–5 s nominal; 2–3 s optional | Most patterns much slower |
| Sine at `a = g` | `f = (1/2π) √(g/A)` | 0.498 Hz at A = 1 m; crest tension is zero |
| Worst motor force | ≈ 0.69 N (≈ 1.09 N equivalent with inertia) | Trivial torque |
| Motor speed | 286–573 RPM @ 40 mm; 573–1,146 RPM @ 20 mm | Any small motor |
| Step rate | 15.3–30.6 kstep/s @ 40 mm at 1/16 | Microstepping TBD. PIO ceiling 200 kstep/s; 2 ms chunk 64 kstep/s |
| Power-off detent @ 40 mm | Pancake 22 g (< 30 g); full NEMA-17 ≥ 50 g | `FR-MOT-011` permits descent |
| Thread stretch | ≈ 73 µm at 4 m/s² | Within IR-MECH-002 (100 µm) |
| Thread strength | ≥ 5 N required; ≈ 100 N available | Large margin |
| Air drag | ≈ 2.1 mN at 1.2 m/s | Negligible |
| Scale | ≤ 50 µm per 300 mm pitch | Needs level-wind or compensation |
| Structure | Designer-owned | Static + slow dynamic loads |

The faster tier does not create a torque or motor-speed wall on the selected
node. Microstepping is TBD. In the 1/16 worked case a 40 mm drum at 1.2 m/s
is 573 RPM and 30.6 kstep/s, against a 200 kstep/s PIO ceiling (6.5×) and a
64 kstep/s playback-chunk ceiling (2.1×). Uniform winding (scale accuracy)
remains the other critical item.
