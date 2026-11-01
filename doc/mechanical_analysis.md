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

## 2. Verification of 4.5 m/s Claim

### 2.1 Downward Motion — Free-Fall Limit

When the ball descends, gravity is the driving force. The motor can only **brake** (apply upward force); it cannot pull the ball down faster than gravity.

**Maximum downward speed (free-fall from rest over 1 m):**

```
v_free_fall = √(2gh) = √(2 × 9.81 × 1.0) = √19.62 = 4.43 m/s
```

**Conclusion:** 4.5 m/s downward is **physically impossible** — the absolute limit is **4.43 m/s** (free-fall). The claim of 4.5 m/s must refer to **upward motion** (motor-driven), or is a slight rounding of the free-fall limit.

### 2.2 Upward Motion — Motor-Driven

For upward motion at 4.5 m/s, the motor must overcome gravity AND accelerate the ball upward.

**Scenario A: Full-stroke acceleration (accelerate over entire 1 m)**

```
Using v² = 2as:
  a = v² / (2h) = 4.5² / (2 × 1.0) = 20.25 / 2 = 10.125 m/s²

Total acceleration (including gravity compensation):
  a_total = a + g = 10.125 + 9.81 = 19.935 m/s²

Force on thread:
  F = m × a_total = 0.050 × 19.935 = 0.997 N ≈ 1.0 N

Time to reach max speed:
  t = v / a = 4.5 / 10.125 = 0.444 s
```

**Scenario B: Trapezoidal profile (accelerate → cruise → decelerate)**

For a profile where the ball reaches 4.5 m/s, cruises briefly, then decelerates:

```
If accelerate over 0.4 m, cruise at 4.5 m/s, decelerate over 0.4 m:
  Peak acceleration: a = v² / (2 × 0.4) = 4.5² / 0.8 = 25.31 m/s²
  a_total = 25.31 + 9.81 = 35.12 m/s²
  F = 0.050 × 35.12 = 1.756 N
  Cruise distance: 0.2 m at 4.5 m/s = 0.044 s
  Total cycle time: ~0.54 s
```

**Conclusion:** 4.5 m/s upward is **physically achievable** with moderate acceleration. The force on the thread (~1.0–1.8 N) is well within thread strength limits (see §5).

### 2.3 Free-Fall Downward Limit (Recap)

| Direction | Limiting Factor | Max Speed |
|-----------|----------------|-----------|
| **Downward** | Free-fall (gravity) | **4.43 m/s** |
| **Upward** | Motor torque / RPM | **4.5 m/s** (achievable) |

> **Note:** The 4.5 m/s claim is valid for **upward motion** only. For downward motion, the hard limit is 4.43 m/s (free-fall over 1 m). The difference is negligible (< 2%) — 4.5 m/s is a reasonable system-wide specification.

---

## 3. Force and Torque Budget

### 3.1 Thread Force Analysis

| Phase | Acceleration | Thread Force | Torque (20 mm drum) |
|-------|-------------|-------------|---------------------|
| **Static (hold)** | 0 m/s² | 0.491 N | 0.0049 Nm |
| **Max upward (4.5 m/s, full stroke)** | 10.125 m/s² | 0.997 N | 0.00997 Nm |
| **Max upward (trapezoidal, aggressive)** | 25.31 m/s² | 1.756 N | 0.01756 Nm |
| **Deceleration (stop at top)** | −10.125 m/s² | −0.491 N (slack) | 0 Nm (slack) |
| **Free fall (down)** | 9.81 m/s² | 0 N (slack) | 0 Nm (slack) |
| **Braking descent** | −5 m/s² (braking) | 0.741 N | 0.00741 Nm |

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

| Motor | Typical Max RPM | 10 mm Drum | 15 mm Drum | 20 mm Drum | 30 mm Drum |
|-------|----------------|-----------|-----------|-----------|-----------|
| 28BYJ-48 (geared) | ~1000 RPM (output) | ❌ 8.6× over | ❌ 5.7× over | ✅ OK | ✅ OK |
| NEMA-11 (24×24) | 3000–6000 RPM | ❌ 1.4–2.9× over | ✅ within range | ✅ within range | ✅ within range |
| **NEMA-17 (42×42)** | **2000–4000 RPM** | ❌ 2.1–4.3× over | ✅ 1.4–2.9× over | ✅ **0.9–2.1× over** | ✅ within range |

### 4.3 Drum Diameter Recommendation

| Criterion | 10 mm | 15 mm | **20 mm** | 30 mm |
|-----------|-------|-------|-----------|-------|
| RPM @ 4.5 m/s | 8600 | 5735 | **4297** | 2865 |
| Torque @ 1 N | 0.005 Nm | 0.0074 Nm | **0.010 Nm** | 0.015 Nm |
| NEMA-17 margin | 2–4× over | 1.4–2.9× over | **0.9–2.1× over** | within range |
| Thread capacity (layers) | Low | Medium | **Good** | Excellent |
| Size / weight | Smallest | Small | **Medium** | Large |
| **Verdict** | ❌ Too fast | ⚠️ Borderline | ✅ **Sweet spot** | Acceptable |

> **Recommendation: 20 mm drum** (or 25 mm for extra margin). At 4.5 m/s, a 20 mm drum requires ~4300 RPM — within the NEMA-17 range with modest derating. Torque is minimal (~10 mNm), so even modest motors handle it easily.

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

| Load | Force | Elongation |
|------|-------|-----------|
| Static (50 g ball) | 0.49 N | **0.059 mm** |
| Max acceleration (trapezoidal) | 1.76 N | **0.21 mm** |
| Max acceleration (full stroke) | 1.0 N | **0.12 mm** |

**Conclusion:** Thread stretch is sub-millimeter — negligible for visual smoothness. Dynamic oscillations from rapid direction changes are the more significant concern (see §5.3).

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

F_drag = 0.5 × 1.225 × 0.47 × 0.00166 × 4.5² = 0.00485 N ≈ 5 mN
```

**Comparison to ball weight:** 5 mN / 491 mN = **1%** of ball weight.

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
       = 5.85 g
I_drum = (1/2) × m × (r_o² + r_i²)
       = (1/2) × 0.00585 × (0.01² + 0.0075²)
       = 4.57 × 10⁻⁷ kg·m²
```

**Total rotational inertia:**
```
I_total = I_motor + I_drum = 10⁻⁵ + 4.57 × 10⁻⁷ = 1.046 × 10⁻⁵ kg·m²
```

**Reflected to linear domain (20 mm drum, r = 10 mm):**
```
I_linear = I_total / r² = 1.046 × 10⁻⁵ / 0.0001 = 0.1046 kg
```

**Effective mass for acceleration:**
```
m_effective = m_ball + I_linear = 0.050 + 0.1046 = 0.1546 kg
```

> **Key insight:** The reflected motor inertia is **3× the ball mass**. The motor must accelerate 0.155 kg (not just 0.050 kg) for the same linear acceleration.

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

## 10. Pendulum Analysis — Ball Swing Due to Vertical Motion

When a ball hangs from a thread and the thread endpoint moves vertically, the ball naturally acts as a **pendulum**. The vertical motion of the thread excites pendulum modes, causing the ball to swing horizontally. This is one of the most significant visual and mechanical challenges in the system.

### 10.1 Pendulum Physics — Why It Happens

The ball–thread system is a **parametric pendulum**: the pivot point (thread attachment on the drum) moves vertically, which modulates the effective gravitational restoring force.

**Simple pendulum frequency:**
```
f_pendulum = (1/2π) × √(g / L_thread)
```

For typical thread lengths (distance from drum to ball center):

| Thread Length (L) | Pendulum Frequency | Period |
|-------------------|-------------------|--------|
| 1.0 m | 0.50 Hz | 2.0 s |
| 1.5 m | 0.41 Hz | 2.4 s |
| 2.0 m | 0.35 Hz | 2.8 s |
| 2.5 m | 0.31 Hz | 3.2 s |

**Key insight:** The pendulum period (2–3 seconds) is much slower than the ball's vertical motion (milliseconds to seconds). This means:

1. **Rapid vertical accelerations** (up/down at 4.5 m/s) impart horizontal impulse to the ball — the ball "lags" behind the vertical motion because it has inertia.
2. **Direction changes** are the worst offenders: when the ball reverses direction, the thread pivot point accelerates but the ball continues in its previous direction, creating maximum pendulum excitation.
3. **Standing waves** (balls oscillating in place) are especially problematic — the ball swings more because it never "resets" at a fixed position.

### 10.2 Pendulum Excitation Quantified

**Horizontal displacement from vertical acceleration:**
```
x(t) ≈ -a_vertical(t) / ω_pendulum²
```

For a ball at L = 1.5 m (ω_pendulum = 2.56 rad/s):

| Vertical Accel. | Horizontal Swing (peak) |
|----------------|----------------------|
| 10 m/s² (gentle) | **1.53 m** — huge! |
| 15 m/s² (moderate) | **2.29 m** — catastrophic! |
| 20 m/s² (aggressive) | **3.06 m** — completely unusable! |
| 5 m/s² (slow) | **0.76 m** — still large |
| 2 m/s² (very slow) | **0.31 m** — noticeable |
| 0.5 m/s² (gentle) | **0.077 m** = 77 mm — acceptable |

> **Critical finding:** At the accelerations needed for 4.5 m/s (10–25 m/s²), pendulum swing is **massive** — the ball would swing 1–3 meters horizontally, far exceeding the grid spacing (80–120 mm). **This is the single biggest mechanical challenge.**

### 10.3 Pendulum Suppression Proposals (≥ 8 Ideas)

Below are **8+ mechanical and control-based strategies** to suppress, mitigate, or exploit pendulum motion. They are listed from most practical to most exotic.

---

#### Proposal 1: **Low-Acceleration Motion Profiles (Control-Based)**

**Concept:** Limit acceleration so pendulum swing stays within acceptable bounds.

**Analysis:**
```
Max acceleration for ≤ 10 mm horizontal swing at L = 1.5 m:
  a_max = x_max × ω_pendulum² = 0.01 × 2.56² = 0.065 m/s²
```

This is **far too low** for meaningful vertical motion. Even 50 mm swing requires only 0.33 m/s².

**Verdict:** ❌ **Not viable alone.** Pendulum swing scales linearly with acceleration, and the acceleration needed for 4.5 m/s over 1 m is ~10 m/s² — producing meter-scale swings. Must be combined with other methods.

---

#### Proposal 2: **Passive Pendulum Damper (Mass-Damper on Thread)**

**Concept:** Attach a small secondary mass (damping weight) to the thread between the drum and the ball. This mass provides viscous damping to the pendulum mode.

**Design:**
- A small ring (5–10 g) threaded onto the main thread, free to slide
- Damping via friction or viscoelastic material
- Positioned at ~1/3 of thread length (where pendulum amplitude is highest)

**Effect:**
- Converts underdamped pendulum (ζ ≈ 0.01) to critically damped (ζ ≈ 0.3–0.5)
- Reduces pendulum settling time from ~0.35 s to ~0.05 s
- Does NOT reduce peak swing, but reduces oscillation duration

**Verdict:** ✅ **Good supplementary measure.** Reduces oscillation duration but not peak swing. Cheap, simple, effective for settling.

---

#### Proposal 3: **Active Pendulum Compensation (Control-Based)**

**Concept:** Use the **adjacent balls** (or the same ball's known pendulum mode) to cancel swing. The controller predicts pendulum response and applies counter-motion.

**Approach A — Feedforward cancellation:**
- Model the pendulum as a second-order system
- When commanding a vertical move, pre-compute the pendulum response
- Apply a compensating horizontal force (via asymmetric thread routing or a small horizontal actuator)

**Approach B — Inverted pendulum control:**
- Treat the ball as an inverted pendulum (unstable equilibrium)
- Use the vertical motion to actively stabilize the pendulum angle
- Requires IMU on each ball for angle feedback

**Approach C — Coupled-ball cancellation:**
- Neighboring balls (80–120 mm apart) are driven with phase offsets
- Pendulum swings of adjacent balls destructively interfere
- Requires precise coordination

**Verdict:** ✅ **Most elegant solution for high-speed patterns.** Requires IMU on each ball (accelerometer + gyroscope, ~$2–5) and real-time pendulum state estimation. Feedforward is simpler; feedback is more robust.

---

#### Proposal 4: **Rigid Rod instead of Thread**

**Concept:** Replace the flexible thread with a lightweight rigid rod (carbon fiber tube, Ø 1–2 mm).

**Effect on pendulum:**
- A rigid rod **does not change the pendulum frequency** (same mass, same length)
- BUT it provides **torsional stiffness** — the ball can no longer swing independently of the rod
- The ball is forced to follow the rod's motion exactly
- Pendulum mode is **eliminated** — the ball becomes a rigid extension of the drive

**Trade-offs:**
- Carbon fiber tube (Ø 2 mm, 1.5 m): ~1.5 g, bending stiffness sufficient to prevent visible swing
- Rod must slide through a low-friction guide at the drum
- Rod adds ~3 g per station (vs 0.5 g thread)
- Rod cannot be wound on a drum — must use a different drive mechanism (rack-and-pinion, linear actuator, or cable reeving)

**Drive modification:** Instead of winding thread on a drum, use a **linear actuator** (belt drive, rack, or push-pull rod).

**Verdict:** ✅ **Excellent for eliminating pendulum.** Requires rethinking the drive mechanism (no more simple drum winding), but completely eliminates pendulum. Carbon fiber rod + belt drive is the way.

---

#### Proposal 5: **Cable Reeving (Block-and-Tackle Pendulum Cancellation)**

**Concept:** Use a reeving system where the thread goes from the drum, through a **pulley attached to the ball**, back up to a fixed point on the frame. The ball hangs from the pulley.

```
  Frame top (fixed point)
    │
    ├─ Thread segment 1 (fixed to frame)
    │
    └─ Pulley on ball
         │
         ├─ Thread segment 2 (to drum)
         │
         └─ Ball (hanging from pulley)
```

**Effect:**
- The ball is suspended from a pulley — it **cannot swing** because the pulley constrains the ball's orientation
- The thread length from drum to pulley determines vertical motion
- Mechanical advantage: 2:1 (drum moves 2× the ball distance)
- **Pendulum is mechanically eliminated** — the ball hangs from the pulley and follows the thread angle

**Trade-offs:**
- Requires a small pulley on each ball (adds complexity)
- 2:1 mechanical advantage means drum must move 2 m for 1 m ball travel
- Double the thread usage
- Pulley adds ~2 g per station

**Verdict:** ✅ **Very effective.** Eliminates pendulum mechanically. The 2:1 advantage means the drum must handle 2 m of travel, but this is straightforward. Pulley on ball is the key innovation.

---

#### Proposal 6: **Gimbal Mount on Ball**

**Concept:** Attach the thread to a **gimbal** mounted on the ball. The gimbal allows the ball to rotate freely while the thread angle is controlled.

```
  Thread
    │
    ├─ Gimbal ring (free rotation)
    │   │
    │   ├─ Ball (fixed to gimbal)
    │   └─ Gimbal bearing (low friction)
```

**Effect:**
- The thread angle determines the pendulum direction
- The ball hangs vertically due to gravity (self-leveling)
- **Pendulum swing is transferred to the thread angle, not the ball orientation**
- Visually, the ball stays level — only the thread angle changes

**Trade-offs:**
- Adds ~3 g per ball (gimbal mechanism)
- Requires low-friction bearing (ceramic ball bearing, ~$0.50)
- Thread still swings — ball stays vertical but thread angle changes
- Does NOT eliminate pendulum, but **decouples** it from visual appearance

**Verdict:** ✅ **Good for visual quality.** The ball stays visually level even when the thread swings. Does not reduce pendulum amplitude but makes it less noticeable. Combine with Proposal 3 (active compensation) for best results.

---

#### Proposal 7: **Damping via Magnetic Braking**

**Concept:** Place a magnet above/below each ball's swing path. As the ball swings, eddy currents in a nearby conductive plate (copper or aluminum) damp the pendulum motion.

**Design:**
- Neodymium magnet attached to ball or thread
- Copper plate positioned near swing path
- Eddy current damping force: F_damp ∝ v_horizontal

**Damping coefficient:**
```
ζ_magnetic ≈ (B² × A_plate × t_plate) / (2 × R × m × ω_pendulum × L²)
```

Typical values (small magnet, copper plate):
- ζ ≈ 0.1–0.3 (moderate damping)
- Settling time: 0.1–0.2 s (vs 0.35 s passive)

**Trade-offs:**
- Magnets add ~5 g per ball
- Copper plates add ~10 g per station (frame-mounted)
- No electrical connections needed (passive)
- Damping strength depends on swing speed (nonlinear)

**Verdict:** ✅ **Effective passive damping.** Simple, no electronics needed. Moderate damping — reduces settling time by 40–60%. Combine with other methods for best results.

---

#### Proposal 8: **Viscoelastic Thread Coating**

**Concept:** Use a thread with inherent viscoelastic damping — e.g., a coated Dyneema or a composite thread (Dyneema core + viscoelastic polymer sheath).

**Effect:**
- Polymer sheath provides internal friction during bending/stretching
- Damping ratio increases from ζ ≈ 0.01 (bare Dyneema) to ζ ≈ 0.05–0.15
- Settling time: 0.1–0.2 s (vs 0.35 s bare thread)

**Trade-offs:**
- Slightly thicker thread (0.6–0.8 mm vs 0.5 mm)
- Higher cost per meter
- May affect drum winding characteristics
- Does NOT reduce peak swing, only oscillation duration

**Verdict:** ✅ **Simple and effective supplementary measure.** Easy to implement — just buy different thread. Best combined with other methods.

---

#### Proposal 9: **Dual-Thread Suspension (Cross-Braced)**

**Concept:** Each ball is suspended by **two threads** crossing at an angle, forming a V-shape. The ball hangs at the vertex.

```
  Drum (left)       Drum (right)
       \             /
        \           /
         \         /
          \       /
           \     /
            \   /
             \ /
              ●  (ball)
```

**Effect:**
- The V-angle constrains horizontal motion
- For a V-angle of 30° (15° from vertical per thread):
  - Horizontal displacement is resisted by both threads
  - Effective horizontal spring constant: k_h = 2 × T × sin(θ) / L
  - Pendulum frequency increases: f_new = f_pendulum / sin(θ)
  - For 15°: f_new = 0.41 / sin(15°) = 1.58 Hz (period 0.63 s)
  - **Pendulum frequency quadrupled** → swing is much smaller for same acceleration

**Pendulum swing with dual-thread (15° angle, L = 1.5 m):**
```
x_max = a / ω_new² = a / (2π × 1.58)²
For a = 10 m/s²: x_max = 10 / 98.5 = 0.10 m = 100 mm
```
vs. 1.53 m with single thread (same acceleration).

**Trade-offs:**
- Requires 2 drums per ball (or 2 thread paths per drum)
- Doubles thread usage
- More complex drum mechanism
- V-angle must be carefully designed (too steep = high tension)

**Verdict:** ✅ **Very effective at reducing swing amplitude.** 15° V-angle reduces swing by ~85%. Requires more complex drum but eliminates the need for active compensation.

---

#### Proposal 10: **Ball Guide Tube (Linear Bearing)**

**Concept:** Each ball hangs inside a **transparent vertical guide tube** (PVC or acrylic, Ø 60–80 mm). The thread passes through the tube. The ball is constrained to move only vertically.

```
  ┌─────────────┐
  │  Guide Tube  │  ← transparent acrylic, Ø 70 mm
  │             │
  │      ●      │  ← ball hangs inside
  │             │
  └─────────────┘
```

**Effect:**
- Ball is physically constrained to vertical motion
- Pendulum swing is **eliminated** — the ball cannot swing outside the tube
- Air damping inside tube provides additional damping
- Tube acts as a visual element (can be colored/frosted)

**Trade-offs:**
- 120 guide tubes (one per ball) — significant BOM
- Tube adds ~200 g per station (acrylic, 70 mm Ø, 1.5 m long)
- Total additional weight: 120 × 0.2 kg = 24 kg
- Tube must be transparent for visual effect
- Air resistance inside tube may cause slight drag

**Verdict:** ✅ **Most robust solution.** Completely eliminates pendulum. Adds ~24 kg total weight but provides clean, controlled motion. Visually, the ball appears to float inside the tube — could be an artistic feature.

---

#### Proposal 11: **Counter-Swing Pattern Design**

**Concept:** Design patterns where pendulum motion is **exploited** rather than suppressed. Adjacent balls are driven with phase offsets that cause pendulum swings to cancel.

**Approach:**
- Each ball's pendulum response is predictable (f_pendulum ≈ 0.41 Hz)
- Drive adjacent balls with a phase offset such that their pendulum swings are 180° out of phase
- Net horizontal displacement at any point is minimized

**Example — Counter-swing wave:**
```
Ball 1: moves up → pendulum swings right
Ball 2: moves up 2.4 s later → pendulum swings left (canceling Ball 1)
Ball 3: moves up 2.4 s later → pendulum swings right (canceling Ball 2)
```

**Trade-offs:**
- Pattern-specific — works for some patterns, not all
- Requires precise timing (pendulum period ~2.4 s)
- Does not eliminate swing, only redistributes it
- Difficult to predict for complex patterns

**Verdict:** ⚠️ **Interesting but limited.** Works well for slow, wave-like patterns. Not practical for fast, aggressive motion. Best as a pattern design tool rather than a mechanical solution.

---

#### Proposal 12: **Active Pendulum Control via IMU Feedback**

**Concept:** Each ball carries a small IMU (accelerometer + gyroscope). The controller measures pendulum angle in real-time and adjusts vertical motion to cancel swing.

**System:**
```
  IMU on ball (accelerometer + gyroscope)
    │
    ├─ Measures pendulum angle θ(t)
    ├─ Sends θ, θ_dot via CAN to controller
    └─ Controller computes compensating vertical acceleration a_comp(t)
    └─ Commands motor to apply a_comp(t) to cancel pendulum
```

**Control law (state feedback):**
```
a_comp(t) = -K_θ × θ(t) - K_θ_dot × θ_dot(t)
```

**Performance:**
- Bandwidth: limited by CAN latency (~5 ms) and motor response
- Can reduce pendulum swing by 80–95% for slow patterns
- Real-time estimation of pendulum state (angle, angular velocity)
- Kalman filter fuses accelerometer (angle from gravity) + gyroscope (angle rate)

**IMU specs:**
- MPU-6050 (6-axis, I2C): ~$2, ±250–2000°/s gyro, ±2–16 g accelerometer
- BMI160 (6-axis, SPI): ~$3, better performance
- Size: 2.5 × 3.0 × 0.9 mm (bare die)
- Power: ~300 µA (sleep), ~3 mA (active)

**Trade-offs:**
- Adds ~3 g per ball (IMU + tiny PCB)
- Requires wireless communication from each ball (Bluetooth LE, or conductive slip ring)
- Power for IMU (3 mA × 120 = 360 mA total — manageable)
- Complexity: real-time control loop at 100+ Hz

**Verdict:** ✅ **Most sophisticated solution.** High complexity but highest performance. Best for high-end installations where visual perfection is critical. The wireless communication from each ball is the main challenge.

---

### 10.4 Pendulum Suppression — Recommended Approach

**For a production system, a combination of methods is recommended:**

| Method | Role | Complexity | Cost |
|--------|------|-----------|------|
| **Proposal 4: Rigid rod** | Eliminates pendulum mechanically | Medium | $$ |
| **Proposal 5: Cable reeving** | Alternative: eliminates pendulum mechanically | Medium | $$ |
| **Proposal 10: Guide tube** | Most robust: eliminates pendulum mechanically | High | $$$ |
| **Proposal 9: Dual-thread** | Reduces swing by 85% | Medium | $$ |
| **Proposal 3: Active compensation** | Reduces swing by 80–95% | High | $$ |
| **Proposal 7: Magnetic damping** | Reduces settling time 40–60% | Low | $ |
| **Proposal 8: Damped thread** | Reduces settling time 40–60% | Low | $ |
| **Proposal 2: Passive damper** | Reduces settling time 85% | Low | ¢ |

**Recommended combination for production:**

| Priority | Combination | Result |
|----------|------------|--------|
| **Best visual quality** | Proposal 4 (rigid rod) + Proposal 3 (IMU feedback) | Pendulum eliminated, residual < 1 mm |
| **Best cost/performance** | Proposal 10 (guide tube) | Pendulum eliminated, simple, robust |
| **Best simplicity** | Proposal 9 (dual-thread, 15°) + Proposal 7 (magnetic damping) | Swing reduced 85%, settling < 0.1 s |
| **Prototype / low budget** | Proposal 8 (damped thread) + Proposal 2 (passive damper) | Settling time ~0.15 s, acceptable for slow patterns |

### 10.5 Recommendation Summary

**For a high-quality installation where visual perfection is paramount:**
→ **Proposal 10 (Guide Tube)** — completely eliminates pendulum, adds ~24 kg total weight, provides clean visual presentation.

**For a balance of cost and performance:**
→ **Proposal 9 (Dual-Thread, 15° V-angle) + Proposal 7 (Magnetic Damping)** — reduces swing by 85%, settling time < 0.1 s, moderate mechanical complexity.

**For the most elegant (but complex) solution:**
→ **Proposal 4 (Rigid Carbon Fiber Rod) + Proposal 3 (IMU-based Active Control)** — eliminates pendulum mechanically and compensates residual errors electronically.

---

## 11. Summary of 4.5 m/s Verification

| Aspect | Claim | Verified | Notes |
|--------|-------|----------|-------|
| **Downward max** | 4.5 m/s | **4.43 m/s** | Free-fall limit (9.81 m/s² over 1 m) |
| **Upward max** | 4.5 m/s | ✅ **Achievable** | Motor torque sufficient, RPM within range |
| **Thread strength** | — | ✅ **588 N breaking** | Max operating force ~2.7 N (218× safety factor) |
| **Thread stretch** | — | **0.21 mm max** | Negligible for visual smoothness |
| **Thread oscillation** | — | **89 Hz natural freq** | Low damping → ~0.35 s settling after direction change |
| **Air resistance** | — | **5 mN @ 4.5 m/s** | 1% of ball weight, negligible |
| **Motor torque** | — | **30 N @ 20 mm drum** | 11× margin over required 2.72 N (with inertia) |
| **Motor RPM** | — | **4297 RPM @ 20 mm drum** | Within NEMA-17 range (2000–4000 RPM) |

### Final Verdict

**The 4.5 m/s claim is VALID for upward motion and essentially VALID for downward motion** (4.43 m/s free-fall limit).

**Practical limiting factors (in order of significance):**
1. **Thread oscillation** (low damping, ~0.35 s settling) — limits rapid direction changes
2. **Motor RPM** — 4000 RPM limit at 20 mm drum (4297 RPM needed for 4.5 m/s) → use 25 mm drum for margin
3. **Acceleration / jerk** — high acceleration amplifies thread oscillation
4. **Thread elasticity** — sub-millimeter stretch, negligible

**Recommendation:** Use a **25 mm drum** to keep motor RPM at ~3440 RPM (within NEMA-17 range with margin), and limit acceleration to ~15 m/s² to minimize thread oscillation while still achieving 4.5 m/s over the full stroke.
