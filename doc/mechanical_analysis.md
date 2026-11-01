# Mechanical Analysis — Hanging Balls

> Physics check for the slow-motion design: force/torque budgets, motor speed,
> thread behaviour, drum winding, and scale accuracy.
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

> The motor, drum, and thread are design choices; this document gives the
> load envelope they must meet.

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

---

## 4. Motor Speed and Step Rate

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

**Step rate at 1/16 microstepping** (steps/m = 3200 / (π × drum Ø)):

| Drum Ø | Linear step | @ 0.6 m/s | @ 0.8 m/s | @ 1.2 m/s | Max `v` at 40 kHz |
|--------|-------------|-----------|-----------|-----------|-------------------|
| 20 mm | 19.6 µm | 30,558 | 40,744 | 61,115 | 0.79 m/s |
| 25 mm | 24.5 µm | 24,446 | 32,595 | 48,892 | 0.98 m/s |
| 30 mm | 29.5 µm | 20,372 | 27,163 | 40,743 | 1.18 m/s |
| 40 mm | 39.3 µm | 15,279 | 20,372 | 30,558 | 1.57 m/s |

**Step rate at 1/8 microstepping** (halve the rates, double the linear step):

| Drum Ø | Linear step | @ 0.6 m/s | @ 1.2 m/s | Meets 50 µm? |
|--------|-------------|-----------|-----------|--------------|
| 20 mm | 39.3 µm | 15,279 | 30,558 | ✅ |
| 25 mm | 49.1 µm | 12,223 | 24,446 | ✅ (no margin) |
| 30 mm | 58.9 µm | 10,186 | 20,372 | ❌ |
| 40 mm | 78.5 µm | 7,639 | 15,279 | ❌ |

The local I2S mux ceiling is 40 kHz per stepper (§8.5 of `controller_analysis.md`).
Consequences:

- **40 mm drum at 1/16** covers the whole envelope: 30.6 kHz at 1.2 m/s, 39.3 µm
  resolution, 573 RPM. This is the recommended baseline.
- **30 mm drum at 1/16** covers 3 s comfortably (27.2 kHz) and 2 s at 40.7 kHz —
  1.8 % over the ceiling, i.e. effectively 2.03 s. Fine if the 2 s end is a
  stretch, not a requirement.
- **20 mm drum at 1/8** also covers 2 s (30.6 kHz, 39.3 µm) but runs at 1,146 RPM
  and a 20 mm-wide winding.
- **25 mm drum at 1/8** is RPM- and step-rate-comfortable but sits at 49.1 µm,
  with no margin against the 50 µm limit once winding scale error is included.
- **30–40 mm drum at 1/8** fails the 50 µm resolution limit and is not usable.

The 39.3 µm of the 40 mm / 1/16 baseline leaves only ~10.7 µm of margin under
`ASM-014`; the separate winding-scale requirement (`NFR-PHYS-007`) still makes
level-wind or radius calibration mandatory. If the margin is uncomfortable,
the 30 mm / 1/16 option gives 29.5 µm and still covers 3 s effects; it gives up
the 2 s tier (1.2 m/s exceeds 40 kHz by ~2 %).

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
quantization term; the 50 µm `ASM-014` budget is set by step quantization
(39.3 µm at 40 mm/1/16) and winding scale.

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
| Worst motor force | ≈ 0.69 N (≈ 1.09 N equivalent with inertia) | Trivial torque |
| Motor speed | 286–573 RPM @ 40 mm; 573–1,146 RPM @ 20 mm | Any small motor |
| Step rate @ 1/16 | 15.3–30.6 kHz @ 40 mm; 30.6–61.1 kHz @ 20 mm | 40 kHz I2S ceiling: 40 mm fits, 20 mm does not |
| Thread stretch | ≈ 73 µm at 4 m/s² | Within IR-MECH-002 (100 µm) |
| Thread strength | ≥ 5 N required; ≈ 100 N available | Large margin |
| Air drag | ≈ 2.1 mN at 1.2 m/s | Negligible |
| Scale | ≤ 50 µm per 300 mm pitch | Needs level-wind or compensation |
| Structure | Designer-owned | Static + slow dynamic loads |

The faster tier does not create a torque or motor-speed wall: at 1.2 m/s the
load is still grams, and a 40 mm drum turns at only 573 RPM. It does make the
**step rate** a real constraint: the cheap ESP32 I2S mux tops out at 40 kHz per
stepper, so the 40 mm drum at 1/16 is the baseline that covers the whole
envelope with margin. Uniform winding (scale accuracy) remains the other
critical item.
