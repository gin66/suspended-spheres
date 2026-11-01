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
| Max commanded speed | `v_max` | 0.3 m/s (proposal, ASM-015) |
| Max acceleration | `a_max` | ~0.5 m/s² (proposal, ASM-015) |
| Gravity | `g` | 9.81 m/s² |

> The motor, drum, and thread are design choices; this document gives the
> load envelope they must meet.

---

## 2. Motion Regime

Motion is slow by design. At 0.3 m/s a full 2 m stroke takes ≈ 6.7 s; typical
patterns will be slower still. Acceleration is kept low so that motion is
smooth and the thread stays in tension.

There is **no free-fall mode** and therefore no catch/brake sizing case. The
worst-case motor load is a slow lift at `a_max`:

```
F_up = m (g + a) = 0.050 × (9.81 + 0.5) = 0.516 N
```

Compare with the static hold:

```
F_hold = m g = 0.050 × 9.81 = 0.491 N
```

Downward acceleration is far below `g`, so thread tension stays close to `mg`
(≈ 0.49 N) at all times.

---

## 3. Force and Torque Budget

| Phase | Force | Torque @ 20 mm drum | Torque @ 30 mm drum |
|-------|-------|---------------------|---------------------|
| Static hold | 0.491 N | 0.0049 Nm | 0.0074 Nm |
| Lift at 0.5 m/s² (ball only) | 0.516 N | 0.0052 Nm | 0.0077 Nm |
| Including reflected inertia (`m_r` ≈ 0.1 kg) | 0.568 N | 0.0057 Nm | 0.0085 Nm |

Even the smallest practical motor (NEMA-11, ≈ 0.3 Nm holding) has a large
margin. Motor selection is therefore driven by cost, noise, and integration,
not torque.

---

## 4. Motor Speed and Step Rate

```
RPM = v × 60 / (π × drum_Ø)
```

| Drum Ø | RPM @ 0.3 m/s |
|--------|--------------|
| 20 mm | 286 |
| 25 mm | 229 |
| 30 mm | 191 |

These are very low speeds for any stepper or BLDC.

Step rate at 1/16 microstepping:

| Drum Ø | Linear step | Steps/s @ 0.3 m/s |
|--------|------------|-------------------|
| 20 mm | 19.6 µm | 15,300 |
| 25 mm | 24.5 µm | 12,200 |
| 30 mm | 29.5 µm | 10,200 |

At 1/8 microstepping the rates halve. All are trivial for a local
microcontroller; the earlier 4.5 m/s step-rate problem does not arise.

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
| Lift at 0.5 m/s² | 0.516 N | ≈ 55 µm |

This is within the 100 µm limit in `IR-MECH-002` and the 50 µm position
tolerance in `ASM-014` is dominated by other terms (winding scale, control).

**Strength.** `IR-MECH-002` requires ≥ 10× the maximum operating force
(≥ 5 N). A 0.5 mm Dyneema thread breaks around 100 N, giving a large margin.

---

## 6. Inertia

The reflected drum/motor inertia `m_r = I / r²` is roughly 1.5–2× the ball
mass (≈ 0.1 kg on a 20 mm drum). The extra force is `m_r a`, only ≈ 0.05 N at
`a_max = 0.5 m/s²`. It is negligible here but is included in the torque table
above.

---

## 7. Air Resistance

For an 80 mm sphere at 0.3 m/s:
```
F_drag = 0.5 × 1.225 × 0.47 × π(0.04)² × 0.3² ≈ 0.13 mN
```
That is ≈ 0.03 % of ball weight — negligible.

---

## 8. Drum and Winding

Two-and-a-half metres of 0.5 mm thread (2 m travel plus margin):

| Drum Ø | Turns | Single-layer width |
|--------|-------|-------------------|
| 20 mm | ≈ 40 | ≈ 20 mm |
| 25 mm | ≈ 32 | ≈ 16 mm |
| 30 mm | ≈ 27 | ≈ 13 mm |

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
- Slow-motion dynamic: ≤ ≈ 0.52 N per ball → ≈ 62 N total (≈ 5 % above static)

There is no free-fall catch load.

---

## 11. Summary

| Aspect | Result | Note |
|--------|--------|------|
| Max speed | 0.3 m/s | Slow by design (ASM-015) |
| Full-stroke time | ≈ 6.7 s at max speed | Slower in typical patterns |
| Worst motor force | ≈ 0.52 N (≈ 0.57 N with inertia) | Trivial torque |
| Motor speed | 190–290 RPM | Any small motor |
| Step rate @ 1/16 | ≈ 10–15 kHz at max speed | Easy for local control |
| Thread stretch | ≈ 52 µm static | Within IR-MECH-002 |
| Thread strength | ≥ 5 N required; ≈ 100 N available | Large margin |
| Air drag | ≈ 0.13 mN | Negligible |
| Scale | ≤ 50 µm per 300 mm pitch | Needs level-wind or compensation |
| Structure | Designer-owned | Static/slow loads only |

The slow regime removes the hard problems of the earlier free-fall concept:
no catch, no high-RPM/torque wall, and no high step-rate requirement. The
remaining critical item is uniform winding (scale accuracy).
