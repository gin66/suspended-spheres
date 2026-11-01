# Mechanical System Analysis — Hanging Balls

> Physics-based verification of the 4.5 m/s speed claim, force/torque budgets,
> thread dynamics, and drum/motor trade-offs.

---

## 1. System Parameters

| Parameter | Symbol | Value |
|-----------|--------|-------|
| Ball mass | m | 50 g = 0.050 kg |
| Ball diameter | D_ball | ~46 mm (50 g steel sphere) |
| Vertical travel | h | 1000 mm = 1.0 m |
| Gravity | g | 9.81 m/s² |
| Claimed max speed | v_max | 4.5 m/s |
| Grid | — | 10 columns × 12 rows = 120 balls |
| Thread diameter | d_thread | 0.5 mm (Dyneema or Kevlar) |
| Thread length (approx.) | L_thread | 1.2–1.5 m (from drum to ball) |

---

## 2. The 4.5 m/s Figure — A Ceiling, Not a Target

### 2.1 Free-Fall Ceiling

The 4.5 m/s figure is the **upper bound** on system speed, taken from free fall over the 1 m travel:

```
v_free_fall = √(2gh) = √(2 × 9.81 × 1.0) = √19.62 = 4.43 m/s
```

This is the fastest a ball can move downward under gravity alone over 1 m; it is specified as 4.5 m/s. It bounds the system but is **not** a speed every move must reach.

### 2.2 Upward Motion

Upward motion is motor-driven. Reaching the ceiling over the full stroke would need:

```
a = v² / (2h) = 4.5² / (2 × 1.0) = 10.125 m/s²
a_total = a + g = 10.125 + 9.81 = 19.935 m/s²
F = m × a_total = 0.050 × 19.935 = 0.997 N ≈ 1.0 N
t = v / a = 4.5 / 10.125 = 0.444 s
```

A trapezoidal profile reaching 4.5 m/s with 0.4 m ramps needs a peak acceleration of 25.31 m/s² and a thread force of 1.756 N.

These are worst-case numbers, not a requirement to reach 4.5 m/s upward. Upward acceleration is a separate requirement (`FR-MOT-004`, ≥ 30 m/s²), and a design may choose a lower peak speed.

**Stopping constraint:** from 4.4 m/s at one g takes about `v² / 2g ≈ 1 m`. An upward move that actually reaches the ceiling cannot then be stopped inside the 1 m travel. While the thread is taut, gravity is the only downward force, so the thread cannot slow an upward ball faster than g.

### 2.3 Free-Fall Drop with a Braked Catch

A free-fall drop is an artistic motion, not a fault. The thread is allowed to go slack. The catch is a brake over the last few centimetres, down to the last few millimetres.

A catch from full free-fall speed in only a few millimetres is a hard stop. Thread strength can take it. A stepper at a few thousand RPM usually cannot, because torque has already collapsed. The braked distance has to be checked against the torque-speed curve, not against holding torque. Homing is a separate, slow descent onto the same stop.

### 2.4 Speed Summary

| Direction | Limiting factor | Speed |
|-----------|----------------|-------|
| **Downward** | Free fall over 1 m | **4.43 m/s** (specified as 4.5 m/s) |
| **Upward** | Motor torque / RPM, bounded by the same ceiling | **≤ 4.5 m/s** |

> The 4.5 m/s figure is the free-fall ceiling rounded up. It is an upper bound, not a target, and it is not specific to upward motion.

---

## 3. Force and Torque Budget

### 3.1 Thread Force Analysis

| Phase | Acceleration | Thread Force | Torque (20 mm drum) |
|-------|-------------|-------------|---------------------|
| **Static (hold)** | 0 m/s² | 0.491 N | 0.0049 Nm |
| **Max upward (4.5 m/s, full stroke)** | 10.125 m/s² | 0.997 N | 0.00997 Nm |
| **Max upward (trapezoidal, aggressive)** | 25.31 m/s² | 1.756 N | 0.01756 Nm |
| **Deceleration faster than g** | < −9.81 m/s² | 0 N (thread slack) | 0 Nm (slack) |
| **Free fall (down)** | 9.81 m/s² | 0 N (slack) | 0 Nm (slack) |
| **Braking descent** | −5 m/s² (braking) | 0.741 N | 0.00741 Nm |

> The thread only pulls. When downward acceleration exceeds g, `m(g + a)` goes negative, which the thread cannot supply, so it goes slack. The earlier −0.491 N row was simply −mg, not `m(g + a)`.

### 3.2 Torque vs. Drum Diameter

```
Torque = Force × radius = F × (drum_Ø / 2)
```

| Drum Ø | Static Torque | Max Accel Torque (1.0 N) | Max Accel Torque (1.76 N) |
|--------|--------------|-------------------------|-------------------------|
| 10 mm | 0.0025 Nm | 0.0050 Nm | 0.0088 Nm |
| 15 mm | 0.0037 Nm | 0.0074 Nm | 0.0131 Nm |
| **20 mm** | **0.0049 Nm** | **0.00997 Nm** | **0.01756 Nm** |
| 30 mm | 0.0074 Nm | 0.0149 Nm | 0.0263 Nm |

**Comparison with motor holding torque:**

| Motor | Holding Torque | Margin @ 20 mm Drum |
|-------|---------------|---------------------|
| 28BYJ-48 (0.09 Nm) | 0.09 Nm | 9× (static), 9× (accel) — tight at speed |
| NEMA-11 (0.3 Nm) | 0.3 Nm | 60× (static), 30× (accel) |
| **NEMA-17 (0.6 Nm)** | **0.6 Nm** | **122× (static), 60× (accel)** |

> **Conclusion:** Even the smallest practical motor (NEMA-11) has ample torque margin. The NEMA-17 provides very comfortable margins at all drum sizes.

---

## 4. Speed and RPM Analysis

### 4.1 Motor RPM Required at 4.5 m/s

```
Linear speed v = ω × r = (2π × RPM / 60) × (drum_Ø / 2)
RPM = v × 60 / (π × drum_Ø)
```

| Drum Ø | Circumference | RPM @ 4.5 m/s | RPM @ 4.43 m/s (free-fall) |
|--------|--------------|---------------|---------------------------|
| 10 mm | 31.42 mm | **8,602 RPM** | 8,531 RPM |
| 15 mm | 47.12 mm | **5,735 RPM** | 5,684 RPM |
| 20 mm | 62.83 mm | **4,297 RPM** | 4,261 RPM |
| 30 mm | 94.25 mm | **2,865 RPM** | 2,841 RPM |

### 4.2 Motor RPM Ratings

| Motor | Typical Max Output RPM | 10 mm Drum | 15 mm Drum | 20 mm Drum | 25 mm Drum | 30 mm Drum |
|-------|-----------------------|-----------|-----------|-----------|-----------|-----------|
| 28BYJ-48 (geared) | ~15 RPM at the output shaft | ❌ | ❌ | ❌ | ❌ | ❌ |
| NEMA-11 (24×24) | 3000–6000 RPM | ❌ 1.4–2.9× over | ✅ within range | ✅ within range | ✅ within range | ✅ within range |
| **NEMA-17 (42×42)** | **2000–4000 RPM** | ❌ | ❌ 1.4–2.9× over | ⚠️ 4297 RPM, just over | ✅ 3438 RPM | ✅ 2865 RPM |

> The 28BYJ-48's 1:64 gearbox puts the output shaft at roughly 15 RPM; it cannot track this travel at any drum size. Its earlier "~1000 RPM" and its acceptable verdicts at 20–30 mm were wrong.

### 4.3 Drum Diameter Recommendation

| Criterion | 10 mm | 15 mm | 20 mm | **25 mm** | 30 mm |
|-----------|-------|-------|-------|-----------|-------|
| RPM @ 4.5 m/s | 8602 | 5735 | 4297 | **3438** | 2865 |
| Torque @ 1.76 N | 0.0088 Nm | 0.0132 Nm | 0.0176 Nm | **0.0220 Nm** | 0.0263 Nm |
| NEMA-17 (2000–4000 RPM) | ❌ | ❌ | ⚠️ just over | ✅ **within range** | ✅ within range |
| Thread winding | Tight | Workable | Workable | **Good (single layer)** | Good |
| Size / weight | Smallest | Small | Medium | **Medium** | Large |
| **Verdict** | ❌ Too fast | ⚠️ Borderline | ⚠️ Marginal | ✅ **Sweet spot** | ✅ Acceptable |

> **Recommendation: 25 mm drum.** At 4.5 m/s it requires ~3440 RPM, inside the NEMA-17 range with margin. A 20 mm drum needs ~4300 RPM, which is just over the top of that range. Torque is minimal (~22 mNm at 25 mm), so even modest motors handle it easily.

---

## 5. Thread Analysis

### 5.1 Thread Material Options

| Material | Tensile Strength | Ø 0.5 mm Breaking Force | Safety Factor (10:1) | Max Operating Force |
|----------|-----------------|------------------------|---------------------|---------------------|
| **Dyneema (UHMWPE)** | 3000–3600 MPa | **588–707 N** | 59–71 N | ✅ (max ~1.8 N) |
| Kevlar (Aramid) | 2800–3600 MPa | **549–707 N** | 55–71 N | ✅ (max ~1.8 N) |
| Nylon | 500–800 MPa | 98–157 N | 10–16 N | ✅ (max ~1.8 N) |
| Steel wire (0.5 mm) | 1500–2500 MPa | 295–491 N | 29–49 N | ✅ (max ~1.8 N) |

**Conclusion:** All common thread materials have enormous safety margins for a 50 g ball. Dyneema is preferred for low stretch and high strength.

### 5.2 Thread Stretch (Elasticity)

```
Elongation ΔL = (F × L) / (E × A)

Where:
  F = force on thread
  L = thread length
  E = Young's modulus
  A = cross-sectional area = π × (d/2)²
```

**Dyneema (E ≈ 120 GPa), 0.5 mm Ø, 1.5 m length:**

| Load | Force | Elongation (k = 15.7 kN/m) |
|------|-------|---------------------------|
| Static (50 g ball) | 0.49 N | **0.031 mm** |
| Max acceleration (trapezoidal) | 1.76 N | **0.11 mm** |
| Max acceleration (full stroke) | 1.0 N | **0.064 mm** |

**Conclusion:** Thread stretch is sub-millimeter — negligible for visual smoothness. These are lower bounds: a braided rope stretches more than the solid-fibre modulus predicts. `ASM-002`'s < 100 µm is the governing limit and is tighter than this idealised calculation. Dynamic oscillations from rapid direction changes are the more significant concern (see §5.3).

### 5.3 Thread Dynamic Response (Spring-Mass System)

The thread-ball system behaves as a spring-mass oscillator:

```
Natural frequency: f_n = (1/2π) × √(k/m)
Spring constant: k = (E × A) / L
```

For Dyneema, 0.5 mm Ø, 1.5 m length:
```
A = π × (0.00025)² = 1.963 × 10⁻⁷ m²
k = (120 × 10⁹ × 1.963 × 10⁻⁷) / 1.5 = 15,704 N/m
f_n = (1/2π) × √(15704 / 0.050) = (1/2π) × 560.4 = **89.2 Hz**
ω_n = 560.4 rad/s
```

**Damping:** Dyneema has low internal damping (ζ ≈ 0.01–0.03).

**Settling time (to 1% of initial displacement):**
```
t_settle ≈ 4 / (ζ × ω_n)
For ζ = 0.02: t_settle ≈ 4 / (0.02 × 560) = 0.357 s
```

**Implication:** After a rapid direction change, the thread may oscillate for ~0.3–0.4 seconds before settling. This is the **primary practical limit** on how fast the system can change direction, not the motor speed.

**Mitigation:**
- Use slightly higher damping thread (coated Dyneema)
- Limit acceleration during direction changes (jerk-limited profiles)
- Accept minor oscillation as part of the visual effect

### 5.4 Air Resistance

Drag force on a 46 mm sphere at 4.5 m/s:

```
F_drag = 0.5 × ρ_air × C_d × A × v²
  ρ_air = 1.225 kg/m³ (sea level, 20°C)
  C_d = 0.47 (sphere)
  A = π × (0.023)² = 0.00166 m²
  v = 4.5 m/s

F_drag = 0.5 × 1.225 × 0.47 × 0.00166 × 4.5² = 0.0097 N ≈ 10 mN
```

**Comparison to ball weight:** 10 mN / 491 mN = **2%** of ball weight.

**Conclusion:** Air resistance is negligible at 4.5 m/s.

---

## 6. Inertia Analysis

### 6.1 Reflected Inertia

The motor and drum have rotational inertia that is "reflected" to the linear domain:

```
I_linear = I_rotational / r²
```

**NEMA-17 rotor inertia:** ~100 g·cm² = 10⁻⁵ kg·m²

**Drum (aluminum, 20 mm Ø × 20 mm long, 5 mm wall):**
```
m_drum = ρ × π × (r_o² - r_i²) × L
       = 2700 × π × (0.01² - 0.0075²) × 0.02
       = 7.4 g
I_drum = (1/2) × m × (r_o² + r_i²)
       = (1/2) × 0.0074 × (0.01² + 0.0075²)
       = 5.8 × 10⁻⁷ kg·m²
```

**Total rotational inertia:**
```
I_total = I_motor + I_drum = 10⁻⁵ + 5.8 × 10⁻⁷ = 1.058 × 10⁻⁵ kg·m²
```

**Reflected to linear domain (20 mm drum, r = 10 mm):**
```
I_linear = I_total / r² = 1.058 × 10⁻⁵ / 0.0001 = 0.106 kg
```

**Effective mass for acceleration:**
```
m_effective = m_ball + I_linear = 0.050 + 0.106 = 0.156 kg
```

> **Key insight:** The reflected inertia is about **2.1× the ball mass**. The motor must accelerate 0.156 kg (not just 0.050 kg) for the same linear acceleration.

**Winding constraint:** keep the wind single-layer. One metre of thread on a 25 mm drum is about 13 turns — under 7 mm of drum width. A second layer changes the effective radius and therefore the millimetres per step, which corrupts the position scale.

### 6.2 Revised Force with Reflected Inertia

| Phase | Force (ball only) | Force (with inertia) | Torque (20 mm drum) |
|-------|-------------------|---------------------|---------------------|
| Static (hold) | 0.491 N | 0.491 N | 0.0049 Nm |
| Max upward (full stroke) | 0.997 N | **1.543 N** | 0.0154 Nm |
| Max upward (trapezoidal) | 1.756 N | **2.718 N** | 0.0272 Nm |
| Braking descent | 0.741 N | 1.147 N | 0.0115 Nm |

Even with reflected inertia, torque requirements remain well within NEMA-17 capability (~0.6 Nm holding, ~0.3 Nm continuous at speed).

---

## 7. Motor Selection — Detailed Comparison

### 7.1 Force/Torque per Motor

| Motor | Holding Torque | Continuous Torque @ 2000 RPM | Max Force @ 20 mm Drum | Max Force @ 15 mm Drum |
|-------|---------------|----------------------------|----------------------|----------------------|
| 28BYJ-48 (0.09 Nm) | 0.09 Nm | ~0.04 Nm | 4.5 N | 6.0 N |
| NEMA-11 (0.3 Nm) | 0.3 Nm | ~0.15 Nm | 15 N | 20 N |
| **NEMA-17 (0.6 Nm)** | **0.6 Nm** | **~0.3 Nm** | **30 N** | **40 N** |

Required force (with inertia): max **2.72 N** (aggressive trapezoidal).

All three motors exceed this, but:
- **28BYJ-48:** Marginal at high speed (RPM limit), geared motor has backlash
- **NEMA-11:** Comfortable margin, but smaller drum RPM may be tight
- **NEMA-17:** Excellent margin at all drum sizes

### 7.2 Recommended: NEMA-17 (42×42 mm)

| Specification | Value |
|--------------|-------|
| Frame size | 42 × 42 mm |
| Shaft | 5 mm or 8 mm |
| Length | 40–55 mm (varies) |
| Holding torque | 0.4–1.0 Nm (typical 0.6 Nm) |
| Rated current | 1.5–2.5 A per phase |
| Phase resistance | 1.5–3.0 Ω |
| Phase inductance | 1.5–3.0 mH |
| Steps per revolution | 200 (1.8°) |
| Max recommended RPM | 2000–4000 RPM |
| Weight | ~350 g |
| Cost | $8–15 |

---

## 8. Drum Design

### 8.1 Diameter Trade-offs

| Drum Ø | RPM @ 4.5 m/s | Torque @ 2.72 N | Thread Capacity | NEMA-17 Fit |
|--------|--------------|-----------------|----------------|-------------|
| 10 mm | 8600 | 0.0136 Nm | Low (few layers) | ❌ RPM too high |
| 15 mm | 5735 | 0.0204 Nm | Medium | ⚠️ Borderline |
| **20 mm** | **4297** | **0.0272 Nm** | **Good** | ✅ **Optimal** |
| 25 mm | 3438 | 0.0340 Nm | Very good | ✅ Excellent |
| 30 mm | 2865 | 0.0408 Nm | Excellent | ✅ Excellent |

### 8.2 Thread Capacity

For a drum of length 20 mm and thread Ø 0.5 mm:

| Drum Ø | Layers (single layer) | Layers (5) | Total Thread Length |
|--------|----------------------|-----------|---------------------|
| 10 mm | 40 turns (20 mm / 0.5 mm) | 200 turns | 200 × π × 0.0125 = 7.85 m |
| 15 mm | 40 turns | 200 turns | 200 × π × 0.0175 = 11.0 m |
| **20 mm** | **40 turns** | **200 turns** | **200 × π × 0.0225 = 14.1 m** |
| 30 mm | 40 turns | 200 turns | 200 × π × 0.0325 = 20.4 m |

> **Conclusion:** Even a single layer (40 turns) provides 0.6–1.3 m of thread — sufficient for 1 m travel plus suspension. Multiple layers provide generous margin.

> **Keep the wind single-layer.** A second layer changes the effective drum radius and therefore the millimetres per step, which corrupts the position scale. One metre of thread on a 25 mm drum is about 13 turns — under 7 mm of drum width — so a single layer is practical.

### 8.3 Drum Material Options

| Material | Density | Machinability | Cost | Recommendation |
|----------|---------|--------------|------|---------------|
| Aluminum 6061 | 2700 kg/m³ | Excellent | $ | ✅ **Recommended** |
| 3D printed (PETG/ABS) | 1100–1400 kg/m³ | Good (FDM) | ¢ | ✅ Acceptable for prototype |
| Stainless steel | 8000 kg/m³ | Fair | $$ | ⚠️ Heavy, high inertia |
| Delrin (POM) | 1410 kg/m³ | Excellent | $ | ✅ Good alternative |

**Recommended:** Aluminum 6061, CNC-machined or 3D printed for prototypes.

---

## 9. Frame and Structural Analysis

### 9.1 Frame Requirements

| Parameter | Value |
|-----------|-------|
| Grid spacing (horizontal) | 80–120 mm (adjustable) |
| Grid spacing (vertical) | 80–120 mm (adjustable) |
| Total width (10 columns) | ~800–1200 mm |
| Total height (12 rows + travel) | ~1200–1800 mm |
| Total depth (drum + motor) | ~150–250 mm |

### 9.2 Frame Load

| Component | Weight |
|-----------|--------|
| 120 steel balls | 6.0 kg |
| 120 stepper motors (NEMA-17) | 42 kg |
| 120 drums + thread | ~3 kg |
| 10 controllers + wiring | ~5 kg |
| Frame (aluminum extrusion) | ~20–40 kg |
| **Total suspended** | **~56 kg** |
| **Total (including frame)** | **~76–96 kg** |

### 9.3 Vibration Considerations

- Ball impact at lower stop: energy = mgh = 0.05 × 9.81 × 0.05 = 0.025 J (from 50 mm above stop)
- Frame resonance: aluminum 2020 extrusion, first mode ~50–100 Hz
- Mitigation: rubber dampers at mounting points, rigid frame construction

---

## 10. Lateral Motion — What Vertical Drive Can and Cannot Do

A hanging ball has a pendulum mode:

```
f_p = (1 / 2π) × √(g / L)
```

For a 1.0–1.5 m thread, `f_p ≈ 0.4–0.5 Hz` (period 2.0–2.4 s).

### 10.1 Vertical Motion Does Not Throw the Ball Sideways

The common formula `x ≈ −a / ω²` describes the lean of a pendulum whose **support accelerates horizontally**. A support that moves only vertically does not apply a horizontal force, so it does not throw a hanging ball sideways. Applying that formula to vertical drive produces the earlier meter-scale swing figures, which are wrong.

### 10.2 What Can Excite Lateral Motion

1. **Parametric excitation.** Vertical acceleration modulates thread tension. This can pump energy into a swing that already exists, most strongly when the drive oscillates near **twice** the pendulum frequency. With `f_p ≈ 0.4–0.5 Hz`, resonance is around 0.8–1.0 Hz. Avoid sustained vertical oscillation near `2 f_p`, or damp the mode.
2. **Slack catch.** In a free-fall drop the thread may go slack. If it is caught while the ball is off vertical, the ball swings. Keep the drop vertical and the catch centred.
3. **Pre-existing swing or off-vertical lead-out.** Keep the drum-to-ball path vertical and the take-off point fixed.

### 10.3 Spacing Is the Practical Limit

At the minimum 80 mm pitch, the 46 mm ball leaves about **34 mm between neighbours**. Any residual swing, from any cause, has to stay well below that gap.

### 10.4 Mitigation

- Keep the take-off point fixed and the thread path vertical.
- Use damped or coated thread to shorten any oscillation that starts.
- Avoid sustained vertical oscillation near `2 f_p`.
- A guide tube or rigid rod mechanically constrains lateral motion if the design needs it, but the vertical-drive physics above does not require one.

> The earlier ``x ≈ −a/ω²`` table, the "1.5 m swing at 10 m/s²" result, and the guide-tube / rod / per-ball-sensor proposals that followed from it were based on horizontal-support motion and have been removed.

## 11. Summary of the 4.5 m/s Ceiling

| Aspect | Result | Notes |
|--------|--------|-------|
| **Downward ceiling** | **4.43 m/s** | Free-fall limit over 1 m; specified as 4.5 m/s |
| **Upward ceiling** | **≤ 4.5 m/s** | Motor-driven, bounded by the same figure |
| **Upward acceleration** | Requirement ≥ 30 m/s² | Speed is a ceiling; acceleration is the separate requirement |
| **Thread strength** | ✅ **588 N breaking** | Max operating force ~2.7 N (218× safety factor) |
| **Thread stretch** | **0.11 mm max** | Idealised lower bound; `ASM-002` governs at < 100 µm |
| **Thread axial oscillation** | **89 Hz natural frequency** | Low damping → ~0.35 s settling after direction change |
| **Pendulum mode** | **0.4–0.5 Hz** | Excited parametrically near `2 f_p` or by an off-vertical slack catch |
| **Air resistance** | **10 mN @ 4.5 m/s** | 2% of ball weight, negligible |
| **Motor torque** | **30 N @ 20 mm drum** | 11× margin over required 2.72 N (with inertia) |
| **Motor RPM** | **3438 RPM @ 25 mm drum** | Within the NEMA-17 range (2000–4000 RPM) |

### Final Verdict

**4.5 m/s is the free-fall ceiling over the 1 m travel, rounded up from 4.43 m/s.** It bounds the system; it is not a speed every move must reach, and the design does not need to size the motor as if it must hit the ceiling upward.

**Practical limiting factors (in order of significance):**
1. **Thread oscillation** (low damping, ~0.35 s settling) — limits rapid direction changes
2. **Motor RPM** — 4000 RPM limit; use a 25 mm drum to keep the 4.5 m/s ceiling inside the NEMA-17 range
3. **Stopping distance** — from the ceiling, stopping at one g takes about 1 m, so a move that reaches the ceiling cannot also stop inside the travel while the thread is taut
4. **Catch from free fall** — must be checked against the torque-speed curve, not holding torque
5. **Thread elasticity** — sub-millimeter, negligible

**Recommendation:** Use a **25 mm drum** (~3440 RPM at the ceiling) and keep the wind single-layer. Treat 4.5 m/s as the ceiling; choose acceleration from `FR-MOT-004` and the catch design, not from the ceiling.
