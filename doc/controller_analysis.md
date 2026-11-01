# Controller Analysis — Hanging Balls

> Detailed evaluation of microcontroller platforms for the 100–120-ball kinetic sculpture.
>
> **Operating point updated.** The piece uses 100–120 hollow chrome-finish
> spheres (Ø 60–80 mm, ≤ 50 g) over ~2 m of travel. The nominal fast tier is
> 0.44–0.59 m/s (2 m in 4–5 s); optional short effects reach 0.8–1.3 m/s
> (2 m in 2–3 s); most choreography is much slower. See `ASM-015`. Figures that
> still refer to the earlier 4.5 m/s free-fall concept are marked as such; the
> platform/bus conclusions (local motion generation, modular grouping) remain
> valid.

---

## 1. Requirements Summary

Before evaluating controllers, we define the hard requirements:

| Requirement | Value |
|-------------|-------|
| Motors per controller | design choice: 16 (custom) or 4–8 (COTS); §11–§12 |
| Communication | WiFi (grouped nodes, buffered commands, broadcast/FTM timebase) |
| Nominal fast speed | 0.44–0.59 m/s (`ASM-015`) |
| Optional effect speed | 0.8–1.3 m/s (`ASM-015`) |
| Acceleration | ~1.0–1.5 m/s² nominal; ~2–4 m/s² optional |
| Synchronization precision | < 1 ms across all 100–120 balls |
| Position reference | exactly one endstop (`FR-MOT-009`) |
| Real-time motion control | Yes — deterministic timing, local trajectories |

---

## 2. Candidate Platforms

### 2.1 ESP32-S3 (WiFi + FTM)

| Specification | Value |
|--------------|-------|
| **CPU** | Dual-core Xtensa LX7, 240 MHz |
| **Flash** | 8 MB+ (QSPI module) |
| **SRAM** | 512 KB (internal) + up to 16 MB PSRAM |
| **GPIO** | ~45 usable pins |
| **PWM** | 8 LEDC channels (not used for stepper timing) |
| **I2S** | 2 I2S peripherals |
| **SPI** | 2 SPI peripherals |
| **I2C** | 2 I2C peripherals |
| **ADC** | 2× 12-bit SAR ADC |
| **Connectivity** | WiFi 4, Bluetooth 5 (LE) |
| **Logic level** | 3.3 V |
| **Package** | QFN-56 (usually supplied as a module) |
| **Cost** | ~$3–5 per module |

#### Stepper Control Architecture — FastAccelStepper

Use **FastAccelStepper exclusively** on the ESP32-S3. Do not bit-bang steps or use raw LEDC PWM for step generation.

| FastAccelStepper driver | Steppers | Max step rate | Use here |
|-------------------------|----------|---------------|----------|
| **I2S mux** + 74HC595 | **16** step + dir (32 step-only) | 40 kHz | **Baseline: 16 per board** |
| **RMT / MCPWM-PCNT** | up to 8 (4 RMT + 4 MCPWM/PCNT) | 200 kHz | Small builds, spares |
| **I2S direct** | 1–3 | 200 kHz | Not needed at 16 axes |

- The library owns step timing, ramps, command queues and start synchronization; the application sets speed, acceleration and target only.
- I2S mux: one DMA buffer drives all 16 steppers, so they are inherently synchronized. Direction reversal costs 500 µs (DIR on a mux slot) or ~1 ms (DIR on GPIO).
- §8 sizes the I2S mux frame; §12 covers the 16-stepper grouping.

#### Pros
- ✅ High CPU headroom for local motion planning and pattern generation
- ✅ Plenty of GPIO, PSRAM for trajectory buffers, WiFi/BT for configuration and monitoring
- ✅ Large community and ecosystem
- ✅ WiFi + broadcast/FTM timebase is sufficient with grouped, buffered controllers (§9)

#### Cons
- ⚠️ Higher power consumption (~200–500 mA active)
- ⚠️ Heat generation at sustained high CPU load

#### Verdict: **Baseline choice** for a WiFi column controller: WiFi + FTM, I2S mux, PSRAM.

---

### 2.1a ESP32-C5 (WiFi 6)

| Specification | Value |
|--------------|-------|
| **CPU** | Single HP RISC-V core, up to 240 MHz |
| **SRAM** | 384 KB HP SRAM |
| **GPIO** | 29 |
| **Connectivity** | WiFi 6 (2.4 and 5 GHz), FTM |
| **Logic level** | 3.3 V |

The C5 adds WiFi 6 and FTM at the cost of a single core and fewer GPIO. It is a valid alternative if 5 GHz operation or WiFi 6 is specifically wanted; the S3 remains the baseline for 16 steppers and PSRAM.

#### Verdict: **Not the baseline.** Use it only if WiFi 6 / 5 GHz is a site requirement.

---

### 2.2 Raspberry Pi Pico 2 (RP2350)

| Specification | Value |
|--------------|-------|
| **CPU** | Dual ARM Cortex-M33, 150 MHz |
| **Flash** | 2 MB (QSPI, on-board) |
| **SRAM** | **520 KB** |
| **GPIO** | 30 (QFN-60) or 48 (QFN-80) |
| **PIO** | **3 PIO blocks × 4 state machines = 12**; FastAccelStepper allocates **8 steppers** on ARM (12 on RISC-V) |
| **USB** | USB 1.1 full-speed Device/Host |
| **SPI** | 2× SPI controllers |
| **I2C** | 2× I2C controllers |
| **ADC** | 12-bit, 4 channels (QFN-60) or 8 (QFN-80) |
| **Package** | QFN-60 (30 GPIO) or QFN-80 (48 GPIO) |
| **Cost** | ~$3–4 per chip |

#### Stepper Control via PIO (FastAccelStepper)

- FastAccelStepper's Pico driver uses PIO: **one state machine per stepper**, up to **200 kHz per stepper**.
- The RP2350 has 3 PIO blocks × 4 state machines = 12 in hardware. With the Arduino/ARM core, FastAccelStepper can allocate **8 steppers** (two PIOs); the RISC-V core can allocate all 12.
- FastAccelStepper claims a whole PIO block and uses its full 32-instruction program space, so unused state machines in a claimed block are not available to the application.
- DIR and ENABLE are ordinary GPIO; set a direction delay.

```
  RP2350 PIO (ARM: 8 steppers, RISC-V: 12)
    │
    ├─ SM0: Step stream ──► Stepper 1
    ├─ SM1: Step stream ──► Stepper 2
    ├─ ...
    └─ SM7: Step stream ──► Stepper 8
       (SM8–11 with RISC-V)
```

#### WiFi — The Problem

The RP2350 has **no WiFi**. Add an ESP32 (or ESP32-C3) as a bridge over UART/SPI, or use a Pico 2 W variant. The bridge also supplies the broadcast/FTM timebase; the PIO remains the step generator.

#### Pros
- ✅ PIO allows deterministic, hardware-level step pulse generation
- ✅ Low power consumption (~50–100 mA active)
- ✅ 8 steppers on ARM, 12 on RISC-V, at up to 200 kHz
- ✅ Dual cores allow separation of communication and motion control

#### Cons
- ❌ Lower CPU (150 MHz vs 240 MHz)
- ❌ No WiFi/Bluetooth; needs a bridge or a Pico 2 W variant

#### Verdict: **Viable alternative** if a WiFi bridge is added. PIO is the best step-pulse tool; the missing WiFi is the integration cost, not the PIO.

---

## 3. Comparative Summary

| Feature | ESP32-S3 | ESP32-C5 | Pico 2 (RP2350) |
|---------|----------|----------|-----------------|
| **CPU** | 240 MHz dual-core | 240 MHz single-core | 150 MHz dual-core |
| **WiFi / FTM** | ✅ WiFi + FTM | ✅ WiFi 6 + FTM | ❌ Needs a bridge |
| **Stepper ch.** | **16 I2S mux** (32 step-only) via FastAccelStepper; 8 RMT/MCPWM | GPIO/PWM | **8 PIO steppers (12 RISC-V)** |
| **Step precision** | Good (PWM) | Good (PWM) | **Excellent (PIO)** |
| **SRAM** | 512 KB + PSRAM | 384 KB | **520 KB** |
| **Real-time** | Good | Good | **Best (PIO)** |
| **Power** | ~300 mA | ~200 mA | ~75 mA |
| **Connectivity** | WiFi + BT + FTM | WiFi 6 + FTM | USB only (needs a WiFi bridge) |
| **Cost per controller** | ~$5–8 | ~$5–8 | ~$5–8 + WiFi bridge |
| **Suitability for 12 balls** | ✅ Excellent | ✅ Excellent | ✅ Good |
| **Suitability for 120 balls** | ✅ Excellent | ✅ Good (fewer GPIO) | ⚠️ Needs WiFi bridge |

---

## 4. Recommendation Matrix

| Priority | Choice |
|----------|--------|
| **Best overall (custom)** | **ESP32-S3** (WiFi + FTM, I2S mux, 16 steppers/board) |
| **Production COTS** | Duet 3 Mini 5+ WiFi / Big Dipper, Octopus + WiFi, FYSETC E4 (§11.5) |
| **Custom module carrier** | ESP32-S3 + 16 TMC2209 StepSticks, 8 nodes (§11.6) |
| Cheapest PoC | FYSETC E4 or ESP32 + TMC2209 modules (§11) |
| Best step precision | Pico 2 (needs a WiFi bridge; not the baseline) |

### Final Recommendation: **ESP32-S3, WiFi-only**

**Rationale:**
1. Grouped controllers (8 nodes × 16 steppers custom, or 15–30 COTS boards), buffered commands (20–100 ms) and a broadcast/FTM timebase make WiFi sufficient (§9, §11).
2. No data cabling; each board needs 24 V and its motor harness only.
3. The ESP32-S3 supports 802.11mc FTM. Classic ESP32 does not; use S3, S2 or C6 if FTM sync is required.
4. WiFi/Bluetooth remains the operator path for configuration, monitoring, pattern upload and OTA.
5. PSRAM allows trajectory and pattern buffers.
6. COTS boards are a valid production path with 15–30 nodes; see §11.3–§11.5 for the trade-offs.

**Per-controller BOM (ESP32-S3, WiFi-only):**

| Item | Qty | Cost (est.) |
|------|-----|-------------|
| ESP32-S3 module | 1 | $4.00 |
| TMC5160 driver | 16 | $80.00 |
| 74HC595 demux chain | 4 | $1.00 |
| Decoupling caps, resistors | 1 set | $2.00 |
| Connector board / terminals | 1 | $4.00 |
| Antenna / connector | 1 | $0.50 |
| **Total per controller** | | **~$92** |
| **Total for 8 controllers (120 axes)** | | **~$735** |

> With TMC2209 instead of TMC5160 the driver cost drops to ~$25–35 per board; see §12.5. Torque at 50 g does not require TMC5160.

---

## 5. Alternative: BLDC with FOC

For completeness, brushless DC motors with Field-Oriented Control (FOC) were considered:

| Aspect | Stepper | BLDC + FOC |
|--------|---------|------------|
| Positioning | Open-loop (precise steps) | Requires encoder (closed-loop) |
| Speed | Limited by step rate | **Higher max speed** |
| Efficiency | Lower (holding current) | **Higher** |
| Noise | StealthChop (quiet) | Quiet |
| Complexity | Simpler | Encoder + FOC algorithm |
| Cost | Lower | Higher (encoder + driver) |
| Max speed 1.2 m/s (optional) | Easy | Easy |

**Verdict:** At the 0.6–1.2 m/s envelope both are easily sufficient. Steppers are simpler and cheaper; BLDC + FOC remains an option for lower noise or if the speed target rises substantially.

---

## 6. Driver IC Comparison

The drivers sit on the column board and are not network nodes. Only the ESP32 is a WiFi node.

| Driver | Interface | StallGuard2 | Ramp generator | Notes |
|--------|-----------|-------------|----------------|-------|
| **TMC5160** | SPI + step/dir | ✅ | ✅ | **Recommended** — mature, on-driver ramp, high current |
| **TMC2209** | UART or step/dir | ✅ | Basic | Cheap, quiet (StealthChop); step/dir path must be verified at the faster tier |
| **TMC5130** | SPI + step/dir | ✅ | ✅ | Lower current; **has a ramp generator** |
| **TMC6200** | SPI | ❌ | — | Gate driver, no StallGuard |
| **TMC6300** | 6× PWM | ❌ | — | **3-phase BLDC gate driver**, 2–11 V, ~2 A peak. Not a stepper driver |
| **BTS7971B** | — | ❌ | — | BLDC, not a stepper |

**Recommendation:** **TMC5160** for production. It provides StallGuard2, an on-driver ramp, and generates the step edges locally. TMC2209 is the cheaper quiet alternative where its step/dir rate is adequate.

---

## 7. Per-Controller Block Diagram (ESP32-S3, WiFi)

```
  ┌──────────────────────────────────────────────────────┐
  │                 ESP32-S3 Controller                  │
  │                                                      │
  │  ┌──────────┐  ┌──────────┐  ┌───────────────────┐  │
  │  │ WiFi STA │  │ I2S mux  │  │ UART / SPI        │  │
  │  │ + FTM    │  │ step/dir │  │ driver config     │  │
  │  └────┬─────┘  └────┬─────┘  └────────┬──────────┘  │
  │       │             │                  │             │
  │       ▼             ▼                  ▼             │
  │   AP / master   74HC595 chain     16 × TMC2209/5160  │
  │  (UDP/ESP-NOW)  (32 outputs)      step/dir + config  │
  │       │             │                  │             │
  │       │             └────────┬─────────┘             │
  │       │                      ▼                       │
  │       │               16 × motors                    │
  │       │                                              │
  │  24 V ──► DC-DC ──► logic                            │
  └──────────────────────────────────────────────────────┘
```

Only 24 V and the motor harness connect to the board. WiFi carries commands, timebase and telemetry; the I2S mux plus a 74HC595 chain fans out 16 step/dir pairs.

---

## 8. Stepping Resolution Analysis: 1/16 µ-Step vs. I2S Mux (4 µs Frame, 40 kHz Ceiling)

This section answers a critical design question: **is 1/16 microstepping sufficient**, or do we need finer resolution, given that the I2S multiplexing approach has 4 µs frame granularity?

> The I2S mux ceiling is **40 kHz per stepper**, not the ~416 kHz implied by an 80 MHz shift-register calculation. The figures below use the FastAccelStepper implementation (ESP-IDF ≥ 5.3): 250 kHz sample rate, 32-bit frame = 4 µs, 25 µs minimum step period. The 40 mm drum at 1/16 is the baseline: 15.3 kHz at the 0.6 m/s nominal tier and 30.6 kHz at the 1.2 m/s optional tier.

### 8.1 The Core Trade-off

| Approach | Timing Resolution | Chosen µ-Stepping | Linear Resolution (40 mm drum) | Required Step Rate @ 1.2 m/s |
|----------|------------------|-------------------|-------------------------------|-----------------------------|
| **I2S mux** | 4 µs frame (25 µs min) | 1/16 | 39.3 µm | 30.6 kHz |
| **PIO (Pico 2)** | 62.5 ns tick; 200 kHz max | 1/32 | 19.6 µm | 61.1 kHz |
| **RMT/MCPWM (ESP32, ≤8)** | 16 MHz tick; 200 kHz max | 1/32 | 19.6 µm | 61.1 kHz |

### 8.2 Linear Resolution by Drum Diameter and Microstepping

**Formula:**
```
Steps per revolution = 200 × microstepping_ratio
Linear resolution = (π × drum_Ø) / steps_per_rev
```

| Drum Ø | Full Step | 1/4 µ-Step | 1/8 µ-Step | **1/16 µ-Step** | 1/32 µ-Step |
|--------|-----------|------------|------------|----------------|-------------|
| 10 mm | 0.157 mm | 39.3 µm | 19.6 µm | **9.8 µm** | 4.9 µm |
| 15 mm | 0.236 mm | 58.9 µm | 29.5 µm | **14.7 µm** | 7.4 µm |
| 20 mm | 0.314 mm | 78.5 µm | 39.3 µm | **19.6 µm** | 9.8 µm |
| 30 mm | 0.471 mm | 117.8 µm | 58.9 µm | **29.5 µm** | 14.7 µm |
| 40 mm | 0.628 mm | 157.1 µm | 78.5 µm | **39.3 µm** | 19.6 µm |

**Key finding:** At the 40 mm baseline, 1/16 microstepping gives **39.3 µm** — within the 50 µm limit of `ASM-014` with 10.7 µm to spare. At 20 mm, 1/16 gives 19.6 µm and 1/8 gives 39.3 µm; both meet the limit. At 30 mm, 1/8 (58.9 µm) fails, and at 40 mm, 1/8 (78.5 µm) fails clearly.

### 8.3 Is 39.3 µm Good Enough? (Visual Perception Analysis)

The ball is ~23 mm in diameter. A 39.3 µm step displacement (40 mm drum, 1/16) is:

```
39.3 µm / 23,000 µm = 0.17% of ball diameter
```

`ASM-014` sets the perceptual limit at 50 µm (below the ~85 µm acuity limit at 0.5 m).

```
39.3 µm < 50 µm
```

**Conclusion:** 1/16 microstepping is **visually imperceptible** — the step transitions appear perfectly smooth. At the 40 mm baseline, 1/16 gives 39.3 µm, still under the 50 µm limit; 1/8 at 40 mm gives 78.5 µm and fails. At 20 mm, 1/8 gives 39.3 µm and remains acceptable. Finer microstepping (1/32, 1/256) provides no visual benefit at this scale.

### 8.4 Timing Analysis at the Speed Envelope

```
Steps/sec = velocity × steps_per_rev / (π × drum Ø)
```

| Drum / µ-step | @ 0.6 m/s (nominal) | @ 0.8 m/s (3 s) | @ 1.2 m/s (2 s) |
|---------------|---------------------|-----------------|-----------------|
| **40 mm / 1/16** | **15,279** | **20,372** | **30,558** |
| 40 mm / 1/8 | 7,639 | 10,186 | 15,279 |
| 30 mm / 1/16 | 20,372 | 27,163 | 40,743 |
| 30 mm / 1/8 | 10,186 | 13,581 | 20,372 |
| 20 mm / 1/16 | 30,558 | 40,744 | 61,115 |
| 20 mm / 1/8 | 15,279 | 20,372 | 30,558 |

At the baseline **40 mm drum, 1/16, 1.2 m/s**:
- Step frequency: **30,558 steps/sec**
- Time per step: **32.7 µs**
- 0.6 m/s: 15,279 steps/s (65.4 µs); 0.8 m/s: 20,372 steps/s (49.1 µs)

The 1/8 rows halve the rate and double the linear step (39.3 µm at 40 mm,
58.9 µm at 30 mm). At 30 mm, 1/8 fails the 50 µm limit; at 20 mm, 1/8 is the
only way to reach 1.2 m/s inside 40 kHz.

### 8.5 I2S Mux Timing Budget (FastAccelStepper implementation)

The ESP32 I2S peripheral is an audio transmitter clocked at 250 kHz in 16-bit stereo, i.e. one 32-bit frame every 4 µs. Each bit of the frame is one demultiplexed output (step or direction). It is not an 80 MHz raw shift register.

| Parameter | Value |
|-----------|-------|
| Sample rate | 250 kHz |
| Frame | 32 bits = 16-bit L + 16-bit R = **4 µs** |
| Bit clock | 8 MHz BCLK |
| Step pulse width | 4 µs (one frame) |
| Minimum step period | 25 µs (6 frames) → **40 kHz per stepper** |
| Steppers per mux | **32** step-only; **16** step + dir; **10** step + dir + enable |
| Demux | 74HC595 chain, or 74HC154 / 74HC138 cascade |
| Synchronism | All mux steppers share one DMA buffer → inherently synchronized |
| Direction latency | 500 µs when DIR is a mux slot; ~1 ms when DIR is a GPIO |

**At 1/16 µ-Step with the 40 mm drum:**
- 0.6 m/s: 15,279 Hz (65.4 µs) → **2.6× margin**
- 0.8 m/s: 20,372 Hz (49.1 µs) → **2.0× margin**
- 1.2 m/s: 30,558 Hz (32.7 µs) → **1.31× margin**

Because step periods are quantized to 4 µs frames, the driver cannot place an
edge at exactly 65.4 µs; it emits 64/68 µs alternating. The mux fill carries the
sub-frame remainder into the next step, so there is **no cumulative timing
error**: the average period is the requested one, and the instantaneous jitter
is bounded.

#### 8.5.1 Speed resolution and jitter — deeper analysis

The queue period is expressed in 16 MHz ticks (62.5 ns). The mux fill converts
the low phase to whole 4 µs frames and carries the remainder (`off_ticks`) into
the next step. All 16 step bits latch in the same frame; there is no per-stepper
serialization penalty. That gives two different quantities:

- **Average speed resolution:** one tick, i.e. `62.5 ns / period`. At 30.6 kHz this is 0.19 % of the period; at 15.3 kHz it is 0.10 %. This is the common 16 MHz timer limit, shared with RMT/MCPWM — not an I2S mux penalty.
- **Instantaneous jitter:** one frame at most. The step intervals alternate rather than drift.

| Speed (40 mm, 1/16) | Requested period | Emitted periods | Peak deviation | Position deviation (`v × Δt`) |
|---------------------|------------------|-----------------|----------------|-------------------------------|
| 0.3 m/s | 130.9 µs | 128 / 132 µs | ≈ 2 µs | ≈ 0.6 µm |
| 0.6 m/s | 65.4 µs | 64 / 68 µs | ≈ 2.6 µs | ≈ 1.6 µm |
| 1.2 m/s | 32.7 µs | 32 / 36 µs | ≈ 3.3 µs | ≈ 4.0 µm |
| Worst case | — | one frame | 4 µs | 4.8 µm at 1.2 m/s |

Interpretation:

- The jitter does **not accumulate**. The sub-frame remainder is carried forward, so position error oscillates within the bound and returns; there is no slow drift or lost step.
- The worst-case displacement error, 4.8 µm, is about **12 % of a 39.3 µm microstep** and **under 10 % of the 50 µm perceptual limit**. At 0.6 m/s it is 2.4 µm.
- The events are at 15–30 kHz, far above the mechanical bandwidth of the ball/rotor; inertia filters the 4 µs modulation. This is a timing ripple, not a visible position step.
- At low speed the same 4 µs bound is a smaller fraction of a long period (0.4 % at 1 kHz, 0.04 % at 100 Hz) **and** a smaller displacement (`v × 4 µs`), so slow choreography is the easiest case, not the hardest.
- The real I2S mux limits are the **40 kHz ceiling** (1.31× at 1.2 m/s) and the **500 µs / 1 ms direction-reversal pause**, not speed resolution.

Putting it together: the I2S mux does **not** quantize the average speed to 4 µs; it quantizes the placement of an edge. The average is tick-exact, the instantaneous error is bounded by one frame, and the mechanics filter it. Speed resolution as such is set by the 16 MHz tick and the microstep size, not by the mux frame.

If a future pattern needs more than 40 kHz per stepper, the answer is not a finer frame: split the group to ≤ 8 axes and use RMT/MCPWM (200 kHz), or use Pico 2 (200 kHz). Sixteen axes cannot exceed 40 kHz on one I2S mux.

Two implementation costs must be budgeted:

- **Direction reversal** inserts a driver-level pause: 500 µs with DIR on an I2S mux slot, ~1 ms with DIR on a GPIO. At 1.2 m/s that is 0.6–1.2 mm of delayed motion per reversal (0.3–0.6 mm at 0.6 m/s) unless the trajectory accounts for it.
- **Hardware validation**: the mux code is implemented, but the end-to-end 74HC595 validation is still listed as open in the FastAccelStepper driver document. Budget a bring-up.

### 8.6 What About 1/32 Microstepping via I2S Mux?

At 40 mm, 1/32 halves the linear step to 19.6 µm and doubles the step rate:

| Speed | Step rate | Margin vs 40 kHz |
|-------|-----------|------------------|
| 0.6 m/s | 30,558 Hz | 1.31× |
| 0.8 m/s | 40,744 Hz | 0.98× — just over |
| 1.2 m/s | 61,115 Hz | 0.65× — not feasible |

So 1/32 at 40 mm is usable only for the slow and nominal tiers, not for the
1.2 m/s optional effects. The 2 s tier requires 1/16 at 40 mm (or 1/8 at
20–25 mm). For margin, **1/16 with a 40 mm drum is the safe default**; 1/32 is
a slow-tier refinement after the 74HC595 bring-up shows no additional losses.

### 8.7 PIO (Pico 2) Comparison

Use FastAccelStepper's Pico driver, not a hand-counted PIO instruction budget. Its published limits are:

| Parameter | Value |
|-----------|-------|
| Max step rate | **200 kHz per stepper** |
| Timing tick | 16 MHz (62.5 ns) |
| Steppers | **8** on Arduino/ARM; **12** on RISC-V |
| PIO usage | One state machine per stepper; FastAccelStepper claims the whole PIO block |

**At 1/16 µ-Step, 40 mm drum, 1.2 m/s:**
- Required: 30,558 Hz (32.7 µs per step)
- FastAccelStepper Pico 2 capability: 200,000 Hz
- **Margin: ~6.5×** — ample, but not the ~164× a theoretical 5 MHz PIO calculation suggested

At 1/32 the margin is ~3.3×. The Pico 2 still has no WiFi; a bridge is required, which is the real reason it is not the baseline.

### 8.8 Summary: Resolution Recommendation

| Factor | 1/16 µ-Step | 1/32 µ-Step |
|--------|------------|------------|
| **Visual smoothness** | ✅ Imperceptible steps | ✅ (no visible improvement) |
| **Holding torque** | Good | ~2× better (lower ripple) |
| **Resonance damping** | Acceptable | Better (finer control) |
| **I2S mux @ 1.2 m/s, 40 mm** | ✅ 1.31× margin | ❌ 0.65× — not feasible |
| **I2S mux @ 0.6 m/s, 40 mm** | ✅ 2.6× margin | ✅ 1.31× margin |
| **I2S mux direction reversal** | ⚠️ 0.6–1.2 mm at 1.2 m/s | same |
| **PIO @ 1.2 m/s, 40 mm** | ✅ 6.5× margin | ✅ 3.3× margin |
| **ESP32 RMT/MCPWM @ 1.2 m/s, ≤8** | ✅ 6.5× margin | ✅ 3.3× margin |
| **CPU load** | Lower | ~2× higher |
| **Network load** | No difference | No difference (steps are local, not on the network) |

### 8.9 Final Recommendation

**1/16 microstepping is the optimal choice for the I2S multiplexing approach:**

1. **Visually indistinguishable** from finer stepping — ball motion appears perfectly smooth
2. **Fits within I2S mux timing** with **2.6× margin at 0.6 m/s** and **1.31× at 1.2 m/s** using the 40 mm drum
3. **Sufficient holding torque** for 50 g ball + acceleration forces
4. **No network penalty** — step generation is local; microstepping does not change network traffic
5. **Acceptable resonance** — stepper motors at 1/16 have manageable resonance peaks

1/32 at 40 mm is feasible for the slow and nominal tiers (1.31× at 0.6 m/s) but not for the 1.2 m/s optional effects; 1/16 at 40 mm is the baseline that covers the full envelope and already meets the 50 µm limit with margin. Smaller drums do not help: 1/32 at 20 mm exceeds 40 kHz even at 0.6 m/s.

For the **Pico 2 with FastAccelStepper**, 1/32 is feasible with ~3.3× step-rate margin and provides slightly smoother low-speed motion, but the visual benefit is marginal and WiFi still needs a bridge. **1/16 remains the recommended default** for both platforms; once the ramp is local, microstepping does not load the network.

---

## 9. Communication — WiFi-Only Baseline

> **Conclusion: WiFi is sufficient.** The conditions are grouped controllers (8–30 nodes; 16 motors per custom board, 4–8 per COTS board), commands buffered 20–100 ms ahead, and a common timebase. The simplest timebase is a **numbered broadcast timestamped on reception**: all nodes see the same transmission, so propagation is common-mode, and a linear fit over many beacons gives offset and drift without a two-way exchange. If the measured receive-timestamp jitter is too high, **802.11mc FTM** provides hardware timestamps (the FTS project reports < 100 ns jitter and ~1.7 ns regression standard deviation on ESP32-S3).

The earlier analysis assumed **120 per-ball WiFi nodes and immediate commands**. Both assumptions are removed: the architecture groups steppers (8–30 nodes), buffers commands, and runs trajectories locally.

### 9.1 Why WiFi is sufficient

| Concern | Why it does not bind |
|---------|----------------------|
| **Latency / jitter** | Commands are scheduled at a beacon epoch 20–100 ms ahead; network latency is absorbed by the buffer. |
| **Packet loss** | Commands are idempotent, sequenced and repeated; the next window replaces a lost one before `T`. |
| **Bandwidth** | 120 steppers × 6 B = 720 B per window. At 10–20 Hz plus repeats that is < 50 kB/s. |
| **Node density** | 8–30 nodes, not 120. A dedicated router AP handles them. |
| **Determinism** | Only the timebase must be real-time, not packet delivery. |
| **E-stop** | Hardwired power removal (`FR-MOT-011`). A WiFi stop is operational only. |
| **Failure** | Nodes finish the buffer, then hold/stop on heartbeat loss. No safety function depends on the network. |

### 9.2 Timebase: three options, simplest first

**A. Broadcast plus receive-time (simplest).** The master broadcasts a small numbered beacon (or the command window itself) at 1–10 Hz. Every node timestamps its arrival in the earliest receive callback and records `(beacon_index, local_time)`.

- Because all nodes receive the **same transmission**, propagation is common-mode and cancels in node-to-node differences. That is the key point: for relative sync you do not need the absolute one-way delay, only the differences between nodes.
- If the beacon carries the master's transmit timestamp, use it as the regressor; the queue/airtime delay is also common to all receivers and cancels in the differences.
- **No feedback and no inter-node exchange are required.** Each node maps its local clock to the same beacon timeline; the common delay is identical for all nodes, so it does not affect relative timing.
- Use a **beacon-relative timeline**: commands say “execute at beacon *k* + Δ”, not “at absolute master time T”. The unknown one-way delay then cancels; no two-way exchange is required.
- A linear fit of `local_time` against `beacon_index` over many beacons gives both offset and **rate** (drift) per node. Drift is very recognizable this way.
- **A single repeat is not enough.** Two samples give a poor offset/rate estimate; use 1–10 Hz and fit 10–100 beacons. Averaging reduces jitter, but it does not remove a constant unknown delay — only the common-mode broadcast comparison does that.
- **The real limit is the receive timestamp.** A software timestamp in the application task carries milliseconds of scheduling jitter. Timestamp in the WiFi receive callback, pin WiFi and the handler, disable power save (`WIFI_PS_NONE`), and measure. Expect tens of µs to ~1 ms; with calibration of the fixed per-node latency, likely well under 1 ms.
- Fixed latency differences between nodes remain as a constant phase bias. Calibrate once (scope/LED) or accept the residual.

**B. FTM (hardware timestamps).** If A's measured residual is too high, use 802.11mc FTM: one master responder and the grouped nodes as initiators. The **FTS project** (ESP32-S3, native ESP-IDF) reports **< 100 ns jitter** and a regression standard deviation of **~1.7 ns** between master and slaves. Treat that as a bench result and validate in situ; even a 1000× degradation is 100 µs.

**C. NTP-style two-way exchange.** If neither A nor B is available, a two-way timestamp exchange over UDP at 1–10 Hz gives 0.2–1 ms. Use it only as a fallback.

A wired PPS/sync line is not required. If in-situ measurement ever shows otherwise, it is a one-wire addition, not a data bus.

### 9.3 Transport and buffering

- The master sends command windows referencing the **beacon timeline** (`epoch k`, `Δ`) or an absolute time in that timeline. The lookahead `L` is **20–30 ms** for interactive/audio-reactive use, **100 ms** for robustness.
- Transport: UDP broadcast (the same packet is also the sync beacon), UDP unicast to the grouped nodes with ACK/retry, or **ESP-NOW** (16 × 6 B = 96 B; 32 × 6 B = 192 B, inside the 250 B payload). Broadcast is attractive because the sync event and the command share one transmission.
- Each node buffers commands, maps the beacon timeline to its local clock, runs its local S-curve, and starts at the commanded epoch.
- Sequence numbers and idempotent commands; a lost packet is replaced by the next window. Missing beacons are harmless for the fit.
- Buffer depth must exceed `2 × update interval + worst observed latency`.

### 9.4 Failure modes and limits

| Event | Response |
|-------|----------|
| Master / AP loss | Nodes finish the buffer, then hold position and flag a fault. The hardwired E-stop remains. |
| Packet loss | The next window replaces it; the buffer covers several losses. |
| WiFi congestion / EMI | Dedicated channel, 8–30 nodes, external antennas, retries; the buffer absorbs jitter. |
| Clock offset / drift | FTM clock model; §10 correction; verify before each pattern. |
| Node reset | Re-home (`FR-MOT-010`). This is not a link-layer issue. |
| Safety | No safety function depends on WiFi. |

### 9.5 Local motion planners

Each controller must generate its own trajectory from parameters, not consume per-ball position samples:

- The master sends a target position plus a profile (speed, acceleration, S-curve).
- The controller interpolates locally at ≥ 1 kHz and drives the step generator.
- The network is idle between parameter updates; its load is independent of control rate.
- This is what keeps a WiFi command window at ~720 B instead of ~120 position samples per cycle.

### 9.6 Final Verdict

| Question | Answer |
|----------|--------|
| **Is WiFi sufficient for real-time control?** | ✅ **Yes** — grouped nodes, buffered commands, and a broadcast timebase remove the latency objection |
| **Is any additional wired link needed?** | ❌ **No.** WiFi is the control channel |
| **What is the minimum viable communication?** | 8–30 grouped WiFi nodes, UDP/ESP-NOW broadcast, 20–100 ms lookahead, numbered beacons timestamped on reception |
| **Is WiFi acceptable for the E-stop?** | ❌ No — keep the hardwired power removal of `FR-MOT-011` |
| **Must controllers have motion planners?** | ✅ **Yes.** The master sends parameters; each controller interpolates locally at ≥ 1 kHz |
| **What if broadcast receive-time is not accurate enough?** | Use FTM (hardware timestamps), then NTP-style two-way (0.2–1 ms), then one sync wire; relax `FR-SYNC-001` only with a visual justification |

**Recommendation: WiFi-only baseline.** Use grouped controllers — 8 × 16-axis custom boards, or 15–30 COTS boards — with UDP/ESP-NOW command windows buffered 20–100 ms ahead, numbered broadcast beacons for the timebase (FTM if receive-time jitter is too high), local S-curve generation, and a hardwired E-stop.

**Critical design rule:** The master sends motion **parameters** (target position + profile), not position samples. Each controller generates the trajectory locally. This is what makes the network load trivial.

> Step-rate note: at 1/16 microstep and a 40 mm drum, 1.2 m/s is about 30.6 kHz per motor — inside the 40 kHz I2S mux ceiling of §8.5 by 1.31×. That is the binding real-time constraint, not the network.

---

## 10. Clock Drift Between Controllers — Synchronization via Rest Pauses

Each of the 10 controllers runs on its own **independent quartz oscillator**. Over time, these oscillators drift relative to each other, causing the balls to fall out of sync. This section quantifies the drift and presents a practical correction strategy.

### 10.1 Quartz Oscillator Drift Specification

Typical quartz crystal oscillators (used in ESP32-S3, Pico 2, etc.):

| Parameter | Value |
|-----------|-------|
| Nominal frequency | 40 MHz (internal) or 24/48 MHz (external) |
| Frequency tolerance (initial) | ± 20 ppm (standard) to ± 2 ppm (precision) |
| Temperature drift | ± 15 ppm over −40°C to +85°C |
| Aging drift | ± 1–5 ppm/year |
| **Worst-case total drift** | **± 35 ppm** (typical consumer-grade crystals) |
| **Best-case total drift** | **± 5 ppm** (precision TCXO) |

### 10.2 Drift Accumulation Over Time

**Maximum drift between two controllers:**
```
Drift_rate = 2 × max_frequency_error (one fast, one slow)
           = 2 × 35 ppm = 70 ppm = 70 × 10⁻⁶ s/s
```

| Time | Max Drift (consumer quartz, 35 ppm) | Max Drift (TCXO, 5 ppm) |
|------|-----------------------------------|------------------------|
| 1 second | 70 µs | 10 µs |
| 10 seconds | 700 µs | 100 µs |
| 1 minute | 4.2 ms | 600 µs |
| 5 minutes | 21 ms | 3 ms |
| 15 minutes | 63 ms | 9 ms |
| 1 hour | 252 ms | 18 ms |
| 8 hours | 2.0 s | 144 ms |
| 24 hours | 6.05 s | 432 ms |

> **Key finding:** With consumer-grade quartz (35 ppm), two controllers can drift **21 ms apart after just 5 minutes** and **252 ms apart after 1 hour**. For synchronized wave patterns requiring sub-millisecond alignment, this is **unacceptable without correction**.
>
> The 70 ppm figure is the worst case: one fast and one slow crystal at 35 ppm each. Two crystals drifting the same way need less. Design for the worst case.

### 10.3 Impact on Synchronized Patterns

**Sub-millisecond synchronization requirement:**
```
Position error from timing drift: Δx = v × Δt
```

| Pattern Speed | Drift @ 5 min (21 ms) | Drift @ 1 hour (252 ms) |
|--------------|----------------------|-------------------------|
| Static (v = 0) | 0 mm | 0 mm |
| Very slow (v = 0.05 m/s) | 1.05 mm | 12.6 mm |
| Slow wave (v = 0.15 m/s) | 3.2 mm | 37.8 mm |
| Nominal fast (v = 0.6 m/s) | 12.6 mm | 151 mm |
| Optional effect (v = 1.2 m/s) | 25.2 mm | 302 mm |

> **Critical finding:** At the nominal fast tier, controllers drift **12.6 mm after 5 minutes**; at the optional 1.2 m/s tier, **25 mm after 5 minutes** and **302 mm after 1 hour** — about one grid pitch. Clock correction is still required, and the faster tier tightens the budget.

### 10.4 Correction Strategy: Pause-Based Clock Alignment

**Concept:** During periods when balls are at rest (common in artistic patterns), controllers use the **idle time** to correct clock drift by inserting small timing adjustments (pauses) into their motion loops.

**How it works:**

```
  Master broadcast:     Beacon k        Beacon k+1      Beacon k+2
  Common airtime:       T_k             T_k+1           T_k+2
                          │               │               │
  Node A local receive: t_A(k)          t_A(k+1)        t_A(k+2)
  Node B local receive: t_B(k)          t_B(k+1)        t_B(k+2)

  Each node fits local_time = a × beacon_index + b.
  A slope difference means a faster/slower local clock.
  Correction is applied locally during rest; no reply is sent.
```

**Algorithm per controller (during rest):**

```
while (ball is at rest):
  1. Receive numbered broadcast beacon; record (beacon_index, local_time)
  2. Update the local fit: local_time = a * beacon_index + b
     (a gives the rate/drift, b the offset in the beacon timeline)
  3. Compare the commanded epoch with the local mapping
  4. If |error| > threshold (e.g., 100 µs):
       a. Calculate correction = error
       b. Spread correction over remaining rest time:
          - If fast: insert small pauses in motion loop
          - If slow: remove small pauses from motion loop
       c. Apply correction gradually (avoid position jumps)
  5. Sleep until the next beacon (e.g., 100 ms–1 s interval)
```

The nodes do not need a per-controller offset from the master; each node fits its own clock to the common beacon timeline. This is what makes one-way broadcast sufficient.

### 10.5 Pause Magnitude and Frequency

**Typical pause per correction cycle:**

| Drift Rate | SYNC Interval | Correction per SYNC | Pause per Cycle |
|-----------|--------------|-------------------|----------------|
| 35 ppm (consumer) | 100 ms | 3.5 µs | 3.5 µs |
| 35 ppm (consumer) | 1 s | 35 µs | 35 µs |
| 35 ppm (consumer) | 5 s | 175 µs | 175 µs |
| 35 ppm (consumer) | 15 s | 525 µs | 525 µs |
| 35 ppm (consumer) | 1 min | 2.1 ms | 2.1 ms |
| 5 ppm (TCXO) | 100 ms | 0.5 µs | 0.5 µs |
| 5 ppm (TCXO) | 1 s | 5 µs | 5 µs |
| 5 ppm (TCXO) | 1 min | 300 µs | 300 µs |

> **Key finding:** Drift accumulates at 35 µs/s — 2.1 ms per minute. A single SYNC cycle corrects only the drift since the last SYNC (3.5 µs at 100 ms). The earlier "2–5 ms pause" mixed one to two minutes of accumulation into a single cycle and did not match the 3.5 µs per 100 ms figure.

### 10.6 Rest Period Frequency in Artistic Patterns

**How often do balls rest?**

| Pattern Type | Rest Frequency | Typical Rest Duration | Correction Feasibility |
|-------------|---------------|----------------------|----------------------|
| **Static hold** | Continuous | ∞ | ✅ Trivial — always resting |
| **Slow breathing** (period 10 s) | Every 10 s | 2–5 s per cycle | ✅ Easy |
| **Sine wave** (period 5 s) | Every 5 s | 0.5–1 s per cycle (at peaks) | ✅ Easy |
| **Standing wave** | Continuous | N/A (oscillating in place) | ✅ Easy |
| **Medium wave** (period 2 s) | Every 2 s | 0.2–0.4 s per cycle | ⚠️ Tight but OK |
| **Fast wave** (period 0.5 s) | Every 0.5 s | 0.05–0.1 s per cycle | ⚠️ Difficult |
| **Rapid cascade** (period 0.2 s) | Every 0.2 s | 0.02–0.04 s per cycle | ❌ Too fast |

> **Key insight:** Most artistic patterns (breathing, sine waves, standing waves, spirals) have **natural rest points** at the peaks of motion where velocity = 0. These rest periods are **sufficient for clock correction** in the vast majority of patterns.

### 10.7 Pause Insertion Mechanics

**How to insert pauses without affecting ball position:**

**Method A: Extend the rest period**
```
If ball reaches target at t=500ms and next command arrives at t=2500ms:
  Normal: hold at target for 2000ms
  With correction: hold at target for 2000ms + correction_time
  Ball position: unchanged (still at target)
  Effect: delays next motion start by correction_time
```

**Method B: Slow down the ramp slightly**
```
If ball is moving (not at rest):
  Reduce velocity by small factor: v_corrected = v × (1 − ε)
  where ε = correction / remaining_motion_time
  Effect: slightly slower motion, ball arrives slightly later
  Ball position at nominal time: slightly behind (by correction amount)
```

**Method C: Micro-pauses in motion loop**
```
Insert 1–10 µs pauses between motion loop iterations:
  Normal loop: 1000 iterations/sec (1 kHz)
  With pauses: 999 iterations/sec (1 kHz − small amount)
  Effect: accumulates correction over time without visible position error
```

> **Recommendation:** Use **Method A** (extend rest) when balls are at rest. Use **Method C** (micro-pauses) when balls are in motion but changing direction slowly. Never use Method B (slow down ramp) as it affects visible motion.

### 10.8 Broadcast Beacon for Clock Correction

```
WiFi broadcast beacon (master -> all controllers):
┌─────────────────────────────────────────────┐
│ magic + version                             │
│ beacon_index (32-bit)                       │
│ master transmit timestamp (optional)        │
│ event / command payload (optional)          │
│ CRC                                         │
└─────────────────────────────────────────────┘
```

Each node records `(beacon_index, local_receive_time)` in the earliest receive callback and fits a line. The beacon doubles as the sync event and the command carrier; the master does not need per-node offsets.

**Update frequency:** 1–10 Hz is enough for drift; use 10 Hz during rest and 1 Hz during motion. Fit over 10–100 beacons; a single repeat (two samples) is not sufficient.

**Correction threshold:** Apply correction when |error| > 100 µs (consumer quartz) or > 10 µs (TCXO).

**Timestamp quality:** software receive timestamps carry interrupt/scheduling jitter; timestamp in the WiFi receive callback, disable power save, pin WiFi and the handler, and measure. If the residual exceeds 1 ms, move to FTM (§9.2B).

### 10.9 Worst-Case Analysis

**Worst case: 35 ppm drift, 100 ms SYNC interval, 0.5 s pattern period (fast wave)**

```
Drift per SYNC: 35 ppm × 0.1 s = 3.5 µs
Correction per SYNC: 3.5 µs pause
Rest per cycle: 0.05–0.1 s (at wave peaks)
Available correction time: 50–100 ms per cycle
Correction needed: 3.5 µs per 100 ms = 0.0035%
```

**Feasibility:** 3.5 µs pause during 50–100 ms rest = **easily achievable** (0.007–0.002% of rest time).

**Worst case: 35 ppm drift, no rest (continuous rapid cascade at 0.2 s period)**
```
No rest periods → no pause-based correction
Drift accumulates: 35 µs/s
After 30 s: 1.05 ms drift → 1.26 mm position error at 1.2 m/s
After 5 min: 10.5 ms drift → 12.6 mm position error at 1.2 m/s
(At the 70 ppm worst case: 21 ms → about 25 mm at 1.2 m/s.)
```

**Solution for continuous-motion patterns:** Use a **TCXO (temperature-compensated crystal oscillator)** with ± 5 ppm drift. Corrections needed only every 15–30 minutes, and even then, only ~300 µs per correction.

### 10.10 Recommendation

| Parameter | Consumer Quartz (35 ppm) | TCXO (5 ppm) |
|-----------|-------------------------|---------------|
| **Correction interval** | 100 ms (during rest) | 1–5 s (during rest) |
| **Correction magnitude** | 3.5 µs per 100 ms | 0.5 µs per 100 ms |
| **Rest needed** | Frequent (every 2–5 s) | Rare (every 15–30 min) |
| **Pattern compatibility** | Most patterns (rest required) | All patterns |
| **Cost per controller** | ~$0 (built-in) | ~$2–5 |
| **Recommendation** | ✅ For most patterns | ✅ For continuous-motion patterns |

**Final recommendation:**
1. **The requirement is alignment, not a method.** `FR-SYNC-001` asks for ≤ 1 ms between controllers; the choices below are one way to meet it.
2. **Use built-in quartz** (consumer grade, 35 ppm) — sufficient for most artistic patterns that include rest periods.
3. **Send SYNC messages periodically** during rest and correct the measured drift; 100 ms is a reasonable starting interval, not a mandate.
4. **Extending a hold does not move a ball.** A pattern with no hold needs a **slow clock trim** (rate adjustment) rather than a pause. A TCXO (± 5 ppm) is one option for continuous-motion patterns, not a requirement.
5. **Target synchronization accuracy:** < 1 ms between any two controllers at all times.
6. **Rest periods are sufficient:** Most artistic patterns naturally include rest at motion peaks — use these for clock correction when they exist.

> Do not freeze the 100 Hz SYNC rate, the 5 ms pause, or a TCXO into the requirement set. Those are implementation choices; the requirement is the ≤ 1 ms alignment.

---

## 11. Off-the-Shelf Controllers — Production Assessment (November 2026)

> **These are candidates for the installed piece, not bench-only experiments.** They are evaluated for WiFi, included drivers, DIAG availability, node count for 120 axes, cost, and production fit. Prices are approximate, in EUR including German VAT, excluding shipping; AliExpress “from” prices usually refer to a bare board or accessory variant.
>
> The architecture is grouping-agnostic: **4–16 motors per node**. A COTS board fixes the grouping (4–8); the custom board uses 16. The timebase, buffering and local-planner design in §9/§12 apply to both.

### 11.1 ESP32 CNC boards — native WiFi

| Board | Axes / driver sockets | Drivers included? | WiFi | StallGuard / DIAG | Approx. price |
|-------|----------------------|-------------------|------|-------------------|---------------|
| **MKS DLC32 V2.1** | 3 (X/Y/Z) | No — bare, or kit with 3 × TMC2209 | ✅ ESP32 on board | ⚠️ UART must be soldered to the I²C pins, plus a DIAG wire; three drivers need a 470 Ω modification | €20 bare, €30–35 with 3 drivers |
| **MKS DLC32 MAX** | 4 | No — kit with TMC2209 available | ✅ ESP32 on board | ⚠️ Same DLC32 wiring caveat; not documented for this board | ~$31 kit |
| **FYSETC E4** | **3 usable** (4 drivers) | ✅ **4 × TMC2209 soldered** | ✅ ESP32 on board (ESP3D) | ✅ DIAG on X/Y/Z; E axis has no DIAG. **With no mechanical switches, only 3 axes are usable** | **€36** |
| **MKS TinyBee** | 5 axes / 6 motors | ✅ on-board TMC2209 | ✅ ESP32 on board | ❌ **No UART to the drivers**; DIP-switch microstepping only | €30–44 |
| **PiBot V4.96 Pro** | 6 | No — StepStick sockets; TMC2130-SPI variant | ✅ ESP32 on board | ⚠️ Possible via TMC2130 SPI; verify DIAG routing | ~$80 |
| **FluidNC “6-Pack”** | 6 external | No | ✅ ESP32 on board | ⚠️ Only if external-driver DIAG is wired in | $48–55 |

### 11.2 3D-printer boards — native WiFi or WiFi add-on

| Board | Axes | Drivers included? | WiFi | StallGuard | Approx. price |
|-------|------|-------------------|------|------------|---------------|
| **BTT Octopus V1.1 / Pro** | 8 | No | ⚠️ **Add-on ESP8266 module** (the board provides an ESP8266 interface; not included) | ✅ TMC2209/2226/5160; **DIAG0–DIAG7 exposed on J22/J23** (Pro V1.1 schematic) | €43–45 + ~€4/driver + €5–15 ESP8266 module |
| **Duet 3 Mini 5+ WiFi** | 5 | ✅ **5 × TMC2209** | ✅ native | ✅ supported in RRF; check DIAG per axis | €116–160; FYSETC “Big Dipper” clone ~€94 |
| **Duet 2 WiFi** (legacy) | 5 | Sockets | ✅ native | ✅ | ~€100+ used/legacy |
| **BTT Manta M8P + CB1** | 8 | No | ✅ via CB1 compute module (Klipper host) | ✅ | ~€80–100 |

### 11.3 System-level production comparison — 120 axes

Costs below are boards only; add ~€1,200–1,400 for 120 motors, PSU and cabling, against the €4,000 BOM cap (`NFR-COST-001`).

| Board | Usable axes/board | Boards | WiFi nodes | Per-axis DIAG | Boards cost | Production verdict |
|-------|--------------|--------|------------|---------------|-------------|--------------------|
| **FYSETC E4** | **3** (4 drivers) | **40** | **40** | 3 of 4 | **€1,440** | Cheapest per board, but 3 usable axes → 40 boards/nodes; classic ESP32 (no FTM) |
| **Duet 3 Mini 5+ WiFi / Big Dipper** | 5 | 24 | 24 | 5 (verify) | **€2,256 clone / €2,784 genuine** | Most production-ready COTS: native WiFi, included drivers, RRF stall detection |
| **Octopus + drivers, no WiFi module** | 8 | 15 | — (15 CAN) | **8/8 (J22/J23)** | **€1,125–1,155 (boards + drivers)** | Lowest Octopus hardware cost; requires wired CAN or an external WiFi bridge |
| **Octopus + ESP8266 module** | 8 | 15 | 15 | **8/8 (J22/J23)** | **€1,335 (boards + drivers + WiFi module)** | Best WiFi axis count/cost: 15 nodes, 8/8 DIAG; WiFi is an add-on ESP8266 |
| **Custom carrier, ESP32-S3 + 16 × TMC2209** | 16 | 8 | 8 | 16/16 (wired) | ~€450–560 + NRE | **Best fit/cost:** 16/16 DIAG, 8 nodes, no QFN; needs custom PCB + firmware |
| **Custom carrier, Pico 2 + 12 × TMC2209** | 12 | 10 | 10 | 12/12 (wired) | ~€500–650 + NRE | 10 nodes; 200 kHz PIO on RISC-V; WiFi via Pico 2 W or ESP32 bridge |
| **MKS DLC32 MAX** | **0** (no DIAG) | — | — | 0 free | — | **Unusable under the no-switch rule** without rework |
| **MKS TinyBee** | **0** (no UART) | — | — | 0 | — | **Unusable under the no-switch rule** |
| **PiBot V4.96 Pro** | 6 (verify) | 20 | 20 | TMC2130 SPI possible | ~$1,600 + drivers | Possible only if all 6 DIAG are routed/exposed |
| **FluidNC 6-Pack** | 6 (verify) | 20 | 20 | wired externally | ~$1,000 + drivers | Possible if each external driver’s DIAG is wired; integration-heavy |

### 11.4 Findings for production

1. **COTS can be the product**, but the node count rises: 10–40 nodes instead of 8 with a 16-axis custom board. A dedicated router AP handles that; an ESP32 SoftAP (≈10 clients) does not.
2. **DIAG is mandatory per axis; no switches are allowed.** An axis without stall detection is not usable under the project constraint. That changes the comparison: the E4 provides **3 usable axes per board**, so 120 axes require **40 boards/nodes**. The Duet is 5/5 (verify). The Octopus Pro V1.1 exposes all eight DIAG pins (J22/J23), so it qualifies with 8/8; confirm the exact board revision. TinyBee and DLC32 are unusable without rework.
3. **Timebase:** classic ESP32 boards (E4, DLC32, TinyBee) and the Octopus ESP8266 module have **no FTM**. They use the numbered broadcast receive-time scheme (§9.2A); FTM is only an upgrade. Check the Duet WiFi module separately.
4. **Firmware is still custom.** RRF/Marlin/FluidNC do not scale to a 120-axis global timeline. Buying a COTS board saves hardware integration, not the control software. This is the main remaining cost.
5. **Serviceability is good:** each COTS board is a field-replaceable module, matching `NFR-MAINT-006`.
6. **Cost:** FYSETC E4 is €1,440 in boards but needs 40 nodes, ~€2,600–2,800 all-in. Duet clone is ~€3,450–3,700 all-in; the genuine board is ~€4,000–4,200 and can exceed the cap. Octopus is ~€2,500–2,800 all-in with 8/8 DIAG and 15 WiFi nodes; the no-module CAN variant is ~€1,125–1,155 in boards + drivers before the CAN master and harness. The **ESP32-S3 module carrier** is ~€1,700–1,900 all-in with 8 nodes and 16/16 DIAG; the **Pico 2 carrier** is ~€1,800–2,000 with 10 nodes and 12/12 DIAG; a fully custom SMT board is ~€2,000–2,200.
7. **The module-based carrier is the missing middle:** a custom PCB with an ESP32-S3 module, 16 TMC2209 StepSticks and a 74HC595 chain gives exact axis count, per-axis DIAG and 8 nodes without QFN soldering. It still requires a PCB design and the same custom firmware.
8. **Stall detection is now the only reference**, so it must be validated on the real drum at 50 g. It needs a hard stop and motion; false negatives are a homing failure. Add a plausibility check (step count vs travel) and treat a missed home as a fault (`FR-MOT-010`).

### 11.5 Production shortlist

- **Best COTS integration:** **Duet 3 Mini 5+ WiFi** or the FYSETC Big Dipper clone — 5/5 DIAG (verify), native WiFi, 24 nodes.
- **Best axis count / cost, WiFi:** **Octopus Pro + ESP8266 WiFi module** — 8/8 DIAG (J22/J23), 15 nodes, ~€1,335 for boards + drivers + module. The module is not included; the board provides an ESP8266 interface.
- **Best Octopus hardware cost, wired:** **Octopus Pro without WiFi module** — 8/8 DIAG, 15 CAN nodes, ~€1,125–1,155 for boards + drivers; add CAN master and harness (§13).
- **FYSETC E4:** €36, but only **3 usable axes** → 40 boards/nodes. Cheapest per board, not the lowest system cost. No FTM.
- **Best fit / lowest system cost:** **custom ESP32-S3 carrier with 16 TMC2209 modules** — 8 nodes, 16/16 DIAG, ~€1,700–1,900 all-in; requires a PCB design.
- **Pico 2 carrier:** 12/12 DIAG, 10 nodes, 200 kHz PIO; WiFi bridge or Pico 2 W, and the timebase must be validated.
- **Unusable as-is under the no-switch rule:** MKS TinyBee (no UART), MKS DLC32 (no free DIAG).
- **Custom 16-axis board:** still the lowest node count and cost, but no longer the only production path.

If the project intends to buy rather than build, the **Duet** or **Octopus** paths are the production choices; the custom board is the optimization.

### 11.6 Custom module-based carrier board

The missing middle: a custom PCB that carries **modules**, not QFN chips. Only passives, connectors, the 74HC595 chain and power are soldered; the MCU and drivers plug in.

**ESP32-S3 carrier, 16 axes**

- ESP32-S3-WROOM-1 module (WiFi + FTM).
- 16 × TMC2209 StepStick modules (UART + DIAG).
- 4 × 74HC595 (32 mux outputs: 16 step + 16 direction).
- 24 V → 5 V/3.3 V DC-DC, bulk capacitors, per-driver 100 µF, input fuse/TVS.
- 16 motor connectors, power input, DIAG/endstop header.
- No QFN soldering. JLCPCB/PCBWay can place the passives and connectors; the module headers are hand-soldered.

| Item | Qty | Cost (est.) |
|------|-----|-------------|
| 4-layer PCB, 100 × 100 mm | 1 | $5 |
| ESP32-S3-WROOM-1 module | 1 | $4 |
| TMC2209 StepStick | 16 | $24 |
| 74HC595 | 4 | $1 |
| Headers/sockets | 17 | $2 |
| 24 V → 5 V/3.3 V DC-DC | 1 | $2 |
| 4:1 UART mux | 1 | $0.50 |
| Capacitors, connectors, fuse, TVS | 1 set | $10 |
| Assembly (passives/connectors) | 1 | $5–10 |
| **Per board** | | **~$53–58** |
| **8 boards** | | **~$424–464** |
| **+ NRE (stencil, setup, review)** | | **~$50–100** |

With motors, PSU and cabling (~€1,200–1,400), the system is ~€1,700–1,900 — the lowest-cost production path in this table.

**Pico 2 carrier, 12 axes**

- Pico 2 module compiled for **RISC-V**; FastAccelStepper gives **12 steppers** at 200 kHz.
- 12 × TMC2209 StepSticks, 74HC595 for direction, 74HC165 for DIAG. The 165 chain reduces the 12 driver-side DIAG signals to **3 MCU pins** (clock/load/data); the CAN bus aggregates status across boards.
- A Pico 2 board exposes ~26 GPIO: 12 step pins plus shift-register direction/DIAG and UART fits. A bare RP2350 QFN-80 (48 GPIO) allows direct wiring instead.
- **10 boards / 10 nodes** for 120 axes, **12/12 DIAG**.
- WiFi via a Pico 2 W module or an ESP32 bridge. The ESP32 bridge is preferred for the validated broadcast/FTM timebase; a Pico 2 W WiFi timebase must be measured.
- BOM ~$46 per board; ~$460 for 10 boards plus NRE.
- **MCU mounting choice:**
  - **Pico 2 module:** 26 exposed GPIO; needs the 74HC595/74HC165 expansion; socketable and replaceable.
  - **Bare RP2354B:** 48 GPIO and integrated 2 MB flash; 12 step + 12 DIR + 12 DIAG + UART fits directly, so no shift registers. Requires QFN-80 assembly and the usual crystal/USB/decoupling support.
  - **Bare RP2350B:** same 48 GPIO, but needs external QSPI flash.

**GPIO budget, bare RP2354B (48 GPIO), 12 axes:**

| Function | Direct | With one 74HC165 for DIAG |
|----------|-------:|--------------------------:|
| 12 × STEP (PIO) | 12 | 12 |
| 12 × DIR | 12 | 12 |
| 12 × DIAG | 12 | 3 |
| CAN-FD controller (MCP2518FD: SCK/MOSI/MISO/CS + INT) | 5 | 5 |
| TMC UART (3 buses, bit-banged) | 3 | 3 |
| Temperature sensor (I²C) | 2 | 2 |
| Common enable | 1 | 1 |
| Status/fault LED | 1 | 1 |
| **Total** | **48 — exactly full** | **39 — 9 free** |

The CAN-FD **5 pins are four SPI signals plus INT** — there is no enable pin. The transceiver STB is tied low, and the controller's crystal is local to the module. Polling instead of using INT drops it to 4 pins; add CLKIN only if the MCU supplies the controller clock instead of a local crystal.

Boot/reset uses the dedicated RUN pin, and USB and the crystal are dedicated. The final GPIO drives a **status/fault LED**, so direct wiring is **exactly 48/48 with no spare**. That fits, but leaves zero margin: any future signal, per-driver enable, or second LED forces the 74HC165. With the 165 the total is 39, leaving 9 GPIO free.

A single-wire WS2812 fits the same LED pin, so it does not change the count. It does need 5 V and a level shifter (3.3 V data is marginal against the 0.7 VDD threshold), and precise 800 kHz timing. Since all three PIO blocks are committed to the step streams, drive it from the second core with interrupts disabled for the ~30 µs frame, or use a plain LED.

**UARTs:** a TMC2209 has 4 UART addresses per bus, so 12 drivers need **3 buses**. Each single-wire half-duplex bus needs one GPIO, i.e. **3 GPIO** total. The RP2354B has 2 hardware UARTs; remapping does not create a third peripheral, and all three PIO blocks are already used by the 12 step streams.

That does not force a mux: **bit-bang the third bus (or all three) on the second core**. UART is only used for configuration and homing, and 115200 baud is trivial for a 150 MHz core. Bit-banging also gives exact control of the half-duplex turnaround: drive during TX, then switch the pin to input to release the line for the TMC reply. A 3:1 mux on one UART is the alternative, also 3 GPIO (data + 2 select).

**Write-only option:** if every driver receives identical configuration, tie the three buses together and **broadcast from one TX pin**; no mux and no RX. This works because address 0–3 is repeated once per bus, so a write reaches all three drivers with that address. It removes readback, so you lose `SG_RESULT` tuning, `DRV_STATUS` fault/temperature flags, and configuration verification. Homing still works from the per-driver DIAG pins. Use this only after bench tuning, and keep a service header for readback; otherwise retain one bidirectional pin per bus (3 GPIO) or a mux.

**Temperature:** TMC2209 reports over-temperature warning/shutdown flags in `DRV_STATUS`, but not necessarily an absolute temperature. If a numeric reading is required, add one I²C sensor per board near the driver array (2 GPIO); that is the 2-pin line above.

**Pico 2 module limitation:** once CAN-FD, UART and a temperature sensor are added, the Pico 2 module's ~26 GPIO is too tight even with both shift registers: 12 STEP + 3 DIR + 3 DIAG + 5 CAN + 3 UART + 2 temp + 1 enable = 29. For a CAN node, use the **bare RP2354B** (48 GPIO); the Pico 2 module is better suited to the WiFi-only variant or to 8 axes.


**Trade-offs**

- Exact axis count and per-axis DIAG: **8 nodes** at 16 axes (ESP32-S3), **10 nodes** at 12 axes (Pico 2).
- Low NRE, no QFN, and a failed driver is a module swap (good for `NFR-MAINT-006`).
- Still a custom PCB and custom firmware; module sockets add height and connector risk (`RISK.md` #8).
- TMC2209 UART is a **configuration/homing** channel, not a motion channel: one UART plus a 4:1 mux serves 16 drivers in groups of four. Add a ~$0.50 mux; no four-bus requirement.
- TMC2209 at 0.25 A is ample for the 50 g balls; TMC5160 modules cost 5–10× more and are unnecessary electrically.

Sources: FYSETC E4 wiki (`docs.fysetc.com`), MKS DLC32/TinyBee GitHub repositories, Diode Laser Wiki (DLC32 UART and sensorless homing), Duet3D stall-detection documentation, FluidNC Wiki.

---

## 12. Grouped WiFi Architecture — Baseline

> **Status: baseline, to be validated on hardware.** `FR-SYNC-001` still requires ≤ 1 ms. FastAccelStepper refers to its ESP32 I2S driver (ESP-IDF ≥ 5.3).

### 12.1 Concept

- **Grouping is a design choice:** 16 steppers per custom ESP32-S3 board (8 nodes), 12 per Pico 2 board (10 nodes), or 4–8 per COTS board (15–40 nodes). Not 120 WiFi nodes.
- **No data cabling.** Each board has WiFi; the only physical connections are 24 V and the motor harness.
- The master broadcasts numbered beacons/command windows **20–100 ms ahead**. Commands reference the beacon timeline (`epoch k`, `Δ`); each board buffers them and starts its local S-curve at that epoch.
- Local trajectories do the high-rate work. The network carries parameters, not position samples.

### 12.2 Why grouping and buffering make WiFi sufficient

The earlier analysis assumed **120 per-ball WiFi nodes and immediate commands**. Both assumptions are removed:

| Property | Naive WiFi | Grouped, buffered baseline |
|----------|------------|----------------------------|
| WiFi nodes | 120 | 8 |
| Command timing | Immediate | Scheduled at a beacon epoch, 20–100 ms ahead |
| Effect of latency/jitter | Directly violates sync | Absorbed by the buffer |
| Effect of packet loss | Lost command | Replaced by the next future window before the epoch |
| Remaining sync limit | Network latency | **Clock offset and drift only** |

This does not make packet delivery deterministic. It moves the problem to the timebase, where a common broadcast plus local clock fitting makes it measurable and bounded.

### 12.3 What still has to hold

| Item | Requirement / budget | Notes |
|------|----------------------|-------|
| **Clock offset** | ≤ 1 ms for `FR-SYNC-001` | Broadcast receive-time differences cancel common-mode propagation; the residual is per-node receive latency. Timestamp in the WiFi RX callback, calibrate fixed latency, and fit 10–100 beacons. If the residual is too high, use FTM (§9.2B). **Measure it.** |
| **Clock drift** | 35 ppm = 1 ms per ~28 s | Use §10 correction (rest pause or slow trim) or re-anchor periodically; a 10-minute pattern drifts ~21 ms uncorrected. |
| **Direction reversal** | 500 µs (DIR in I2S mux) / ~1 ms (DIR on GPIO) | Driver inserts the pause. At 1.2 m/s that is 0.6–1.2 mm of delayed motion; include it in the trajectory and in the sync budget. |
| **Packet loss** | Buffer > 2 × update interval + worst latency | Repeat each future window; use sequence numbers and idempotent commands. A 100 ms buffer with a 10–20 ms refresh tolerates several losses. |
| **E-stop** | Hardwired | A WiFi stop is operational only. Keep the red-button power removal of `FR-MOT-011`. |
| **Interactive latency** | 20–30 ms vs 100 ms | 100 ms is fine for precomposed patterns, poor for live/audio-reactive control (`FR-PAT-005`). Make buffer depth tunable. |
| **Radio** | 8–30 nodes, one AP / ESP-NOW peer | Antenna placement inside a steel structure and 2.4 GHz congestion still need a site check. |

At 1.2 m/s, a 1 ms offset is 1.2 mm of position error; at 0.6 m/s it is 0.6 mm. That is what the 1 ms requirement protects.

**Transport options:** UDP broadcast of one 120-stepper window is ~720 B at 6 B/stepper and fits an IP packet; it is unacknowledged and sent at a basic rate, but it doubles as the sync beacon, which is the point. With 8–30 nodes, unicast with ACK/retry is a reliable alternative for the command payload while a separate broadcast beacon carries sync. **ESP-NOW** is a lower-overhead broadcast option (16 × 6 B = 96 B, 32 × 6 B = 192 B, inside the 250 B payload). The choice does not affect the timebase argument.

### 12.4 Stepper and driver budget

| Property | Value |
|----------|-------|
| I2S mux frame | 4 µs (250 kHz, 32-bit) |
| Max step rate | **40 kHz per stepper** (25 µs minimum period) |
| Steppers per mux | **32** step-only; **16** step + dir; **10** step + dir + enable |
| Synchronism | Inherent — all slots share one DMA buffer |
| Demux | 74HC595 chain, or 74HC154 / 74HC138 cascade |
| Driver validation | 74HC595 end-to-end still listed as open in the library docs; bring-up required |

**For a custom board, 16 steppers is the sweet spot.** Step + direction both on the mux uses exactly 32 slots, leaves enable common, gives 8 boards for 120, and keeps 500 µs direction latency. The practical build is the module carrier of §11.6: ESP32-S3 module + 16 TMC2209 StepSticks + 74HC595, no QFN. COTS boards use their fixed 4–8 axes and 15–30 nodes. **32 steppers/board** requires 32 step slots plus direction via GPIO or a second shift-register chain, and moves direction latency to ~1 ms.

**TMC2209 UART is a configuration and homing channel, not a motion channel.**

- Step/dir runs autonomously; UART is needed only to set current, microstep and StallGuard threshold, and to read status during homing.
- A TMC2209 has 4 UART addresses per bus. Use **one UART plus a 4:1 mux** (or a 16:1 mux) to select groups of four; configure and home them sequentially, then park the mux and run.
- There is no need for four hardware UARTs or SPI. The extra part is a ~$0.50 analogue mux.
- Timing: configuring 16 drivers takes tens of milliseconds; homing in groups of four stays well inside the 60 s of `FR-MOT-010`. Motion does not use UART.
- Route all 16 DIAG lines to the MCU (or a 74HC165), even though only the active group is homed at a time.

### 12.5 Current and thermal

At the current this duty actually needs, not at driver maximum:

| Configuration | Current @ 0.25 A | Power @ 24 V | Current @ 0.3 A | Power @ 24 V |
|---------------|------------------|--------------|-----------------|--------------|
| 16 steppers/board | 4.0 A | 96 W | 4.8 A | 115 W |
| 32 steppers/board | 8.0 A | 192 W | 9.6 A | 230 W |
| 120 motors holding | 30 A | 720 W | 36 A | 864 W |

- 0.25–0.3 A RMS still gives roughly 2–4× the torque needed for a 50 g ball, depending on drum (about 0.02 Nm required at 40 mm with 2× margin; ~0.04 Nm available at 0.25 A from a NEMA-14/17 pancake). Do not run the drivers at their 1–2 A rating.
- On 2 oz copper with wide pours, 8–10 A is not a copper problem (~2 mΩ over a 20 mm × 150 mm pour). **Connectors, harness and ground return are the limit**: 32 motors means 128 motor wires on one board; 16 halves that.
- Driver dissipation at 0.25 A is ~0.1–0.3 W each, i.e. ~3–10 W per 32-driver board. Enclosure temperature (40 °C) and local airflow decide the heatsink.
- The **continuous holding current** dominates the 1200 W supply budget, not the motion peaks. 30 A at 24 V is the number to check against PSU sizing and room heat.
- The faster tier adds acceleration power of roughly `F × v ≈ 0.7 N × 1.2 m/s ≈ 0.8 W` per ball, i.e. ~100 W for 120 balls, and only during accelerations. It does not change the PSU sizing rule, which is set by holding current.

### 12.6 Bench tests to confirm the baseline

1. **I2S mux + 74HC595** at 16 steppers, 15 kHz, on a logic analyser; confirm the 40 kHz ceiling, pulse width and frame quantization.
2. **WiFi + I2S coexistence**: 16 steppers at 15 kHz with WiFi traffic on the other core; check DMA underrun and step jitter.
3. **Offset/drift**: two or three nodes with the beacon scheme; measure offset error and drift over 10 minutes. Is 1 ms real?
4. **Direction reversal**: measure the 500 µs (mux DIR) and ~1 ms (GPIO DIR) pauses against the trajectory model.
5. **Holding torque at 0.25 A** on the real drum with a 50 g ball; confirm the 3–4× margin and the power-off behaviour of `FR-MOT-011`.

### 12.7 Decision rule

- If tests 1–3 pass, the WiFi-only baseline is confirmed. The hardwired E-stop remains in all cases.
- If broadcast receive-time cannot hold 1 ms, move to FTM. If FTM also fails, add a single sync wire or relax `FR-SYNC-001` with a visual justification (5 ms = 3 mm at 0.6 m/s and 6 mm at 1.2 m/s; compare the 50 µm criterion of `ASM-014` and the ~300 mm grid pitch).
- This section changes no requirement. Adopting it requires review of `FR-SYNC-001`, `FR-COMM-004` and `IR-SOFT-003`.

---

## 13. Wired CAN Node — Octopus Pro H723

> Alternative to the WiFi baseline. The H723 variant has an FDCAN controller and an MCP2542 transceiver, 8 drivers, 8 DIAG inputs and a 6P6C RJ11 CAN connector.

### 13.1 CAN or CAN-FD in practice

- The **hardware** is CAN-FD capable: STM32H723 FDCAN + MCP2542 (an FD transceiver).
- The **COTS software stack is not**. Katapult's STM32H723 configuration uses **classic CAN at 1 Mbit/s**; Klipper also uses classic CAN. CAN-SW-Upgrade over CAN therefore works, but not with CAN-FD frames.
- True CAN-FD would require a custom bootloader/firmware path; it is not available off the shelf for this board.
- For this project, classic CAN at 1 Mbit/s is sufficient for both control and firmware upload. CAN-FD is optional future headroom, not a requirement.

### 13.2 Physical node

- 24 V power, 8 motor connectors, one 6P6C RJ11 CAN connector.
- CAN is a **bus**, not a true daisy-chain: run a trunk with short stubs. Terminate with 120 Ω at the two physical ends only.
- The schematic shows one CAN connector and a 120 Ω resistor. Check whether that resistor is fitted; if it is fitted on every board, remove it on all intermediate nodes or the bus will be over-terminated.
- For in/out daisy-chaining, add a second RJ11 in parallel or use a passive splitter at each node.

### 13.3 Firmware update

- Katapult `flashtool.py` can request the bootloader and upload over CAN at 1 Mbit/s.
- Budget ~10–30 s per board; 15 boards sequentially is roughly 3–8 minutes.
- No WiFi module is needed, which removes the jamming/AP risk and saves ~€10–15 per board.

**Cost (15 nodes):** ~€1,155 for boards + drivers (no WiFi), plus motors, PSU and CAN harness. That is below the WiFi-based Octopus path.

### 13.4 Master / host options

The Octopus nodes run classic CAN at 1 Mbit/s today, so an FD-capable master gives no speed benefit yet; it is future headroom. Choose the master for compute, storage and maintenance, not for FD.

| Master | CAN-FD | Compute / storage | UI / pattern engine | Firmware update | Verdict |
|--------|--------|-------------------|---------------------|-----------------|---------|
| **PC + USB-CAN-FD adapter** | ✅ (adapter) | Best | Native | Katapult `flashtool.py` on the same host | Best for development and exhibition with a PC present |
| **Raspberry Pi + CAN-FD HAT/adapter** | ✅ | Good | Good | Native | Best standalone Linux host; Klipper-compatible |
| **ESP32-S3 + MCP2518FD** | ✅ via SPI | Good (dual core, PSRAM) | Limited; PC over WiFi for UI | Needs a CAN bootloader bridge or PC | Best embedded master if no PC |
| **ESP32-C5** | ✅ on-chip | Modest (single core, 384 KB) | Limited | Same caveat | Cheapest on-chip FD master; least headroom |

**Recommendation:** use a **PC or Raspberry Pi with a USB-CAN-FD adapter** as the master. It runs the pattern engine, logging, simulation and Katapult firmware updates in one place, and the adapter handles bus timing. If the installation must run without a PC, use an **ESP32-S3 + MCP2518FD** as the embedded master and keep the PC as an optional WiFi/Ethernet client.

Configure the master for **classic CAN 1 Mbit/s** while the Octopus nodes are classic; switch to FD only when the node firmware/bootloader actually supports it.

### 11.7 RP2350 CNC boards on the market

RP2350 CNC boards do exist, but almost all are **breakout boards for external drivers**, not integrated multi-driver boards:

| Board | MCU | Axes | Drivers | Connectivity | Approx. price |
|-------|-----|------|---------|--------------|---------------|
| **RP23CNC / RP23U5XBB** | RP2350B / RP2354B | 5 | external, screw terminals | USB + Ethernet, UART headers | ~$50 board |
| **Bolang RP2350 GRBLHAL** | RP2350 | 3/4/5 | external | USB/UART | ~€17–40 |
| **RP2354B GRBLHAL board** | RP2354B | 3/4/5 | external | USB/UART | ~€17 |

Implications for this project:

- **5 axes per board** means 24 boards / 24 nodes for 120 axes.
- **No onboard drivers**: add 120 external drivers and their wiring. DIAG must be wired to a limit input; stall detection is not plug-and-play.
- **No WiFi or CAN**: USB/Ethernet only. They are host-tethered boards, not nodes for a distributed bus.
- They are useful as a **cheap 5-axis CNC controller for a single machine**, not as a 120-axis node.

The **custom** Pico 2 module carrier (§11.6) remains the better RP2350 option here: 12 onboard TMC2209s, 12/12 DIAG, 10 nodes.
