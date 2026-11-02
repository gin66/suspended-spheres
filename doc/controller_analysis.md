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
| 4-layer PCB | 1 | $5.00 |
| Assembly (SMT/passives) | 1 | $8.00 |
| **Total per controller** | | **~$105** |
| **Total for 8 controllers (120 axes)** | | **~$840** |

> With TMC2209 the driver cost depends on the package: bare ICs drop it to ~$25–35 per board (soldered), whereas plug-in TMC2209 StepSticks cost ~$4 each (~$64 for 16). See §11.3 and §11.6. Torque at 50 g does not require TMC5160.

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

> **These are candidates for the installed piece, not bench-only experiments.** They are evaluated for WiFi, included drivers, DIAG availability, node count for 120 axes, cost, and production fit. Prices are approximate, in EUR including German VAT, excluding shipping; AliExpress/Mellow “from” prices usually refer to a bare board or accessory variant and often exclude tax. **Verify the exact SKU/photo before using a price**: the same listing may mix board-only, driver-bundle, WiFi-module and interface-board variants. In particular, the FLY D5 board price excludes its five TMC2209 modules, while the FLY E3 Pro Ultra/Max includes five onboard TMC2209 but only XYZ sensorless homing.
>
> The architecture is grouping-agnostic: **4–16 motors per node**. A COTS board fixes the grouping (4–8); the custom board uses 16. The timebase, buffering and local-planner design in §9/§12 apply to both.

### 11.1 ESP32 CNC boards — native WiFi

| Board | Axes / driver sockets | Drivers included? | WiFi | StallGuard / DIAG | Approx. price |
|-------|----------------------|-------------------|------|-------------------|---------------|
| **MKS DLC32 V2.1** | 3 (X/Y/Z) | No — bare, or kit with 3 × TMC2209 | ✅ ESP32 on board | ⚠️ UART must be soldered to the I²C pins, plus a DIAG wire; three drivers need a 470 Ω modification | €20 bare, €30–35 with 3 drivers |
| **MKS DLC32 MAX** | 4 | No — kit with TMC2209 available | ✅ ESP32 on board | ⚠️ Same DLC32 wiring caveat; not documented for this board | ~$31 kit |
| **FYSETC E4** | **3 usable** (4 drivers) | ✅ **4 × TMC2209 soldered** | ✅ ESP32 on board (ESP3D) | ✅ DIAG on X/Y/Z; E axis has no DIAG. **With no mechanical switches, only 3 axes are usable** | **€30.39** |
| **FYSETC Cheetah v2.0** | **3 usable** (4 drivers) | ✅ **4 × TMC2209 onboard** | ❌ no WiFi; ❌ no CAN (USB/UART only) | ⚠️ Sensorless homing on X/Y/Z; E0 has no endstop/DIAG | €35.39 |
| **Mellow FLY-RPFMEX** | 4 | ✅ **4 × TMC2209 onboard** | ❌ no WiFi/CAN; **USB-C (USBSERIAL)** host link | ✅ **4/4 DIAG** on independent RP2040 GPIOs | ~€26.59 |
| **Mellow FLY-Micro4 V1.0** | 4 | ✅ **4 × TMC2209 onboard** | ✅ **CAN** (bridging); **CAN: 2-pin PH2.0**; no WiFi | ✅ **4/4 DIAG** on independent RP2040 GPIOs; **no NTC inputs** — TMC2209 `otpw`/`otsd` only | ~€27 (free shipping; ~€24.33 net with €80 coupon at 30 boards) |
| **FYSETC Spider V3.0 H7** | **6 usable** (8 sockets) | No — sockets | ❌ no WiFi; ✅ **CAN** + USB-C | ⚠️ **6/8 DIAG** (X/Y/Z/E0/E1/E2 via shared endstop pins); E3/E4 none; **CAN connector verify; remove 120 Ω on 18/20** | $34.90 |
| **MKS Monster8 V1/V2** | **6 usable** (8 sockets) | No — sockets | ❌ no WiFi; ✅ **CAN** | ⚠️ **6/8 DIAG** (DIAG0–5 on six endstops); F407 @ 168 MHz | ~€45–55 (verify) |
| **Mellow Fly-Super8 Pro H723** | 8 | No — sockets | ⚠️ WiFi for RRF; ✅ **CAN** | ✅ **8/8 DIAG** per driver (Fly-2209 switch) | ~$80–100 (verify) |
| **Mellow Fly-CDYv3** | 6 | No — sockets | ⚠️ **ESP32 WiFi (RRF only)**; USB for Klipper | ✅ per-driver DIAG (verify 6/6) | ~$35 (verify) |
| **BTT SKR 3 / SKR 3 EZ** | 5 | No — sockets (EZ drivers) | ⚠️ **ESP32/ESP-12S WiFi module**; ✅ **FDCAN hardware** (classic in stock firmware) | ✅ **5/5 DIAG** (shared with endstop pins); H743 @ 480 MHz | ~€50–60 (verify) |
| **MKS TinyBee** | 5 axes / 6 motors | ✅ on-board TMC2209 | ✅ ESP32 on board | ❌ **No UART to the drivers**; DIP-switch microstepping only | €30–44 |
| **PiBot V4.96 Pro** (runs FluidNC / GrblHAL) | 6 | No — StepStick sockets; TMC2130-SPI variant | ✅ ESP32 on board | ⚠️ Possible via TMC2130 SPI; verify DIAG routing | ~€80 |

### 11.2 3D-printer boards — native WiFi or WiFi add-on

| Board | Axes | Drivers included? | WiFi | StallGuard | Approx. price |
|-------|------|-------------------|------|------------|---------------|
| **BTT Octopus V1.1 / Pro F446** | 8 | No | ⚠️ **Add-on ESP8266 module** (not included) | ✅ TMC2209/2226/5160; **8/8 DIAG via jumpers**; **classic CAN** (no FD); **CAN: V1.1 2-pin; Pro RJ11**; **3 NTC inputs (V1.1) / 5 NTC inputs + PT100/PT1000 (Pro); sensors external** | €43–45 + ~€4/driver + €5–15 ESP8266 module |
| **BTT Octopus Pro H723 (V5.0)** (CAN) | 8 | No | ✅ **FDCAN hardware** + MCP2542 (RJ11 — not recommended for field cabling; V1.1 2-pin preferred); **stock firmware = classic CAN 1 Mbit/s**, no WiFi | ✅ TMC2209/2226/5160; **DIAG0–DIAG7 via DIAG jumpers** (module DIAG pins required); **5 NTC inputs + PT100/PT1000; sensors external** | **€94.69 incl. 8 × TMC2209** |
| **Mellow FLY D5 V1.0** | 5 | No — **5 driver sockets** (TMC2209 modules separate) | ❌ no WiFi; ✅ CAN | ✅ TMC2209 UART/StallGuard4; **DIAG via on-board jumpers** (module DIAG pin required) | ~€18–20 board (excl. VAT) + 5 drivers |
| **Mellow FLY E3 Pro Ultra / Ultra Max** | 5 | ✅ **5 × TMC2209 onboard** | ⚠️ **ESP32 WiFi module option** | ⚠️ **Only XYZ support sensorless homing (3/5 DIAG)**; CAN; STM32H723VGT6 | €87 / €99 |
| **Duet 3 Mini 5+ WiFi** | 5 | ✅ **5 × TMC2209** | ✅ native | ✅ supported in RRF; check DIAG per axis | €116–160; FYSETC “Big Dipper” clone ~€94 |
| **Duet 2 WiFi** (legacy) | 5 | Sockets | ✅ native | ✅ | ~€100+ used/legacy |
| **BTT Manta M8P V2 + CB1/CM4** | 8 | No — sockets | ✅ via CB1/CM4 compute module (Klipper host + WiFi) | ✅ **8/8 DIAG via jumpers** (verify) | ~€80–100 + CB1/CM4 |
| **Mellow FLY-C8 (H5 + F407)** | **6 usable** (8 sockets) | No — sockets | ✅ **WiFi/Ethernet/CAN**; Allwinner H5 host + F407 MCU | ⚠️ **6/8 DIAG** (DRIVER0–5); DRIVER6/7 none | ~€100–150 (verify) |

### 11.3 System-level production comparison — 120 axes

Costs below are **boards only, no TMC drivers**. Add ~€1,200–1,400 for 120 motors, PSU and cabling, against the €4,000 BOM cap (`NFR-COST-001`). Driver costs are listed separately.

| Board | Usable axes/board | Boards | WiFi nodes | Per-axis DIAG | Boards cost | Drivers (per board) | Drivers (120 axes) | Production verdict |
|-------|--------------|--------|------------|---------------|-------------|-------------------|------------------|--------------------|
| **FYSETC E4** | **3** (4 drivers) | **40** | **40** | 3 of 4 | **€1,215.60** | 4 × TMC2209 included | — | Cheapest COTS system cost, but 3 usable axes → 40 boards/nodes; classic ESP32 (no FTM) |
| **FYSETC Cheetah v2.0** | **3** (4 drivers) | **40** | — | **3 of 4** | **€1,415.60** | 4 × TMC2209 onboard | — | No WiFi/CAN; USB/UART single-printer board; not a network node |
| **Mellow FLY-RPFMEX** | **4** | **30** | — | **4 of 4** | **€798** (30 × €26.59) | 4 × TMC2209 onboard | — | Best 4-axis/4-DIAG candidate; **USB-C host link** → 30 USB nodes; no CAN/WiFi; expansion-board form factor but usable standalone via USB-C + 24 V |
| **Mellow FLY-Micro4 V1.0** | **4** | **30** | — (30 CAN) | **4 of 4** | **~€730 net** (30 × €27 = €810, −€80 coupon; free shipping) | 4 × TMC2209 onboard | — | **Stock: rejected** (fixed split termination, 2-pin CAN without GND; J3 removes only one 60 Ω half, not a proper disable). **With termination rework: viable** — remove both R39/R58 on all but the two end nodes. Lowest-cost COTS at €27/board; 30 nodes. |
| **FYSETC Spider V3.0 H7** | **6** (8 sockets) | **20** | — (20 CAN) | **6 of 8** | **~€1,220** (20 × ~€61 incl. drivers) | 6 × TMC2209 | ~€522 | **Stock: rejected** (permanent 120 Ω). **With rework: viable** — remove the 120 Ω on 18 of 20 boards. |
| **MKS Monster8 V1/V2** | **6** (8 sockets) | **20** | — (20 CAN) | **6 of 8** | **~€1,500–1,600** (20 × ~€75–80 incl. drivers) | 6 × TMC2209 | ~€522 | 6/8 DIAG + CAN, F407; **CAN termination must be selectable/DNP — verify** |
| **Mellow Fly-Super8 Pro H723** | **8** | **15** | 15 (RRF WiFi) | **8 of 8** | **~€1,875** (15 × ~€125 incl. drivers) | 8 × TMC2209 | ~€522 | 8/8 DIAG + CAN + H723; likely above the Octopus bundle on cost |
| **Mellow Fly-CDYv3** | **6** | **20** | 20 (ESP32, RRF) | **6 of 6** (verify) | **~€1,220** (20 × ~€61 incl. drivers) | 6 × TMC2209 | ~€522 | 6-axis WiFi option; WiFi only with RRF, Klipper via USB; F407 |
| **BTT SKR 3 / SKR 3 EZ** | **5** | **24** | 24 (WiFi module) | **5 of 5** | **~€1,800–1,900** (24 × ~€75–80 incl. drivers) | 5 × TMC2209 | ~€522 | 5/5 DIAG + FDCAN hardware + WiFi module; 24 nodes, higher cost/axis than Octopus |
| **BTT Manta M8P V2 + CB1/CM4** | **8** | **15** | 15 (CB1 WiFi) | **8 of 8** | **~€2,300–2,600** (15 × ~€150–175 incl. drivers + CB1) | 8 × TMC2209 | ~€522 | 8/8 DIAG + WiFi per node, but a Linux host per board; expensive/complex |
| **Mellow FLY-C8 (H5 + F407)** | **6** (8 sockets) | **20** | 20 (H5 WiFi) | **6 of 8** | **~€2,600+** (20 × ~€130 incl. drivers) | 6 × TMC2209 | ~€522 | Host+MCU all-in-one; 20 Linux hosts; 6/8 DIAG; expensive/overkill |
| **Duet 3 Mini 5+ WiFi / Big Dipper** | 5 | 24 | 24 | 5 (verify) | **€2,256 clone / €2,784 genuine** | 5 × TMC2209 included | — | Most production-ready COTS: native WiFi, included drivers, RRF stall detection |
| **Octopus Pro H723, no WiFi** | 8 | 15 | — (15 CAN) | **8 via DIAG jumpers (module pins required)** | **€1,420** | included (8 × TMC2209) | — | Lowest Octopus hardware cost, but RJ11 is a poor field connector; use V1.1 2-pin |
| **Octopus Pro H723 + ESP8266 module** | 8 | 15 | 15 | **8 via DIAG jumpers (module pins required)** | **€1,570** | included (8 × TMC2209) | — | 15 nodes, 8 DIAG via jumpers; WiFi add-on; RJ11 cabling caveat |
| **Octopus V1.1 / Pro F446** | 8 | 15 | — (15 CAN) | **8/8 via DIAG jumpers** (module pins) | **~€1,200** (15 × ~€80 incl. drivers) | 8 × TMC2209 | ~€522 | **Classic CAN, no FD.** Cheaper than the H723 bundle; same always-fitted 120 Ω termination — DNP/remove on non-end nodes. |
| **Mellow FLY D5 V1.0** | 5 | 24 | — (24 CAN) | **5 via DIAG jumpers (module pins required)** | **€960–1,000** (24 × ~€40–42 incl. drivers) | 5 × TMC2209 (separate) | ~€522 | Cheapest CAN node; **STM32F072 (48 MHz, 128 KB flash, 16 KB RAM)** is the step-rate/firmware risk; no WiFi |
| **Mellow FLY E3 Pro Ultra / Max** | **3** (5 drivers) | **40** | 40 (ESP32 module) | **3 of 5** | **€3,480–3,960** | 5 × TMC2209 onboard | — | H723 + onboard drivers, but only 3 usable axes → 40 boards; not competitive with the E4 |
| **Custom carrier, ESP32-S3** | 16 | 8 | 8 | 16/16 (wired) | **~€260–300** | 16 × TMC2209 | ~€480 | **Best fit/cost:** 16/16 DIAG, 8 nodes, no QFN; needs custom PCB + firmware |
| **Custom carrier, Pico 2 (RP2354B)** | 12 | 10 | 10 | 12/12 (wired) | **~€300–350** | 12 × TMC2209 | ~€480 | 10 nodes; 200 kHz PIO on RISC-V; WiFi handled separately |
| **MKS DLC32 MAX** | **0** (no DIAG) | — | — | 0 free | — | — | — | **Unusable under the no-switch rule** without rework |
| **MKS TinyBee** | **0** (no UART) | — | — | 0 | — | — | — | **Unusable under the no-switch rule** |
| **PiBot V4.96 Pro** (runs FluidNC / GrblHAL) | 6 (verify) | 20 | 20 | **TMC2130 + StallGuard2 (SPI)** | **~€1,600** | 6 × TMC2130 | ~€960 | StallGuard2 provides sensorless homing; verify DIAG routing per axis |

### 11.4 Findings for production

1. **COTS can be the product**, but the node count rises: 10–40 nodes instead of 8 with a 16-axis custom board. A dedicated router AP handles that; an ESP32 SoftAP (≈10 clients) does not.
2. **DIAG is mandatory per axis; no switches are allowed.** An axis without stall detection is not usable under the project constraint. That changes the comparison: the E4 provides **3 usable axes per board**, so 120 axes require **40 boards/nodes**. The Duet is 5/5 (verify). The Octopus Pro has on-board **DIAG jumpers** (manual §4.3) that connect each driver's diagnostic output to an endstop input. A stock TMC2209 StepStick does **not** bring DIAG out on its 2×8 header (it is a separate pad), so the jumper only works if the module has the DIAG pin fitted. Pre-assembled modules with the extra 2-pin `DIAG`/`INDEX` header (e.g. Makebase/MKS TMC2209, ~€34.79 for 8, ~€4.35 each) satisfy this without soldering. With those modules and the jumpers fitted, the Octopus is **8/8 DIAG**; with bare StepSticks it is UART-only. Verify the board revision, jumper numbering and endstop pin map. TinyBee and DLC32 are unusable without rework.
3. **Timebase:** classic ESP32 boards (E4, DLC32, TinyBee) and the Octopus ESP8266 module have **no FTM**. They use the numbered broadcast receive-time scheme (§9.2A); FTM is only an upgrade. Check the Duet WiFi module separately.
4. **Firmware is still custom.** RRF/Marlin/FluidNC do not scale to a 120-axis global timeline. Buying a COTS board saves hardware integration, not the control software. This is the main remaining cost.
5. **Serviceability is good:** each COTS board is a field-replaceable module, matching `NFR-MAINT-006`.
6. **Cost:** FYSETC E4 is €1,215.60 in boards (40 × €30.39) but needs 40 nodes, ~€2,420–2,620 system total. Duet clone is ~€3,460–3,660 system total; the genuine board is ~€3,980–4,180 and can exceed the cap. Octopus is ~€2,770–2,970 system total with 8 DIAG via on-board jumpers (module pins required) and 15 WiFi nodes; the no-module CAN variant is ~€1,420 (15 bundles at €94.69, including 120 TMC2209) + motors, PSU and cabling (~€1,200–1,400) = ~€2,620–2,820. The Octopus variant + ESP8266 module is ~€1,570 + motors, PSU and cabling = ~€2,770–2,970. The **Octopus V1.1 2-pin** is cheaper (~€2,385–2,825 system total) and is the preferred cabling variant; the Pro H723's RJ11 is the caveat. The **ESP32-S3 carrier** is ~€260–300 (pure carrier, no TMC) + ~€480 (120 TMC2209) + motors, PSU and cabling (~€1,200–1,400) = ~€1,940–2,180 with 8 nodes and 16/16 DIAG; the **Pico 2 carrier** is ~€300–350 (pure carrier, no TMC) + ~€480 (120 TMC2209) + motors, PSU and cabling = ~€1,980–2,230 with 10 nodes and 12/12 DIAG; a fully custom SMT board is ~€2,300–2,500.
7. **The module-based carrier is the missing middle:** a custom PCB carrying an ESP32-S3 module, 16 TMC2209 StepSticks (plugged into sockets) and a 74HC595 chain gives exact axis count, per-axis DIAG and 8 nodes without QFN soldering. It still requires a PCB design and the same custom firmware.
8. **Stall detection is now the only reference**, so it must be validated on the real drum at 50 g. It needs a hard stop and motion; false negatives are a homing failure. Add a plausibility check (step count vs travel) and treat a missed home as a fault (`FR-MOT-010`).
9. **Mellow FLY D5 is the cheapest 5-axis CAN board, but the MCU is the constraint.** It has 5 driver **sockets**; TMC2209 modules are separate, and the module needs the extra DIAG pin for the board's DIAG jumpers. The STM32F072 (48 MHz Cortex-M0, 128 KB flash, 16 KB RAM, no PIO/FPU) must generate 5 axes of step pulses and run the custom S-curve/timebase firmware. At the 1.2 m/s tier that is ~305 k step events/s; validate the achievable step rate and RAM headroom on the bench before adopting it. At the 0.6 m/s nominal tier the load is roughly half. CAN only, no WiFi.
10. **FLY E3 Pro Ultra / Ultra Max is not a 5-axis candidate under the no-switch rule.** It has 5 × TMC2209 onboard and an H723, but only XYZ support sensorless homing (3/5 DIAG), so 120 axes need 40 boards at €87–99 — worse than the E4 at €30.39 with the same 3 usable axes.
11. **Cheetah v2.0 is rejected for network topology, not for the drivers.** It has 4 × TMC2209 onboard and sensorless homing at €35.39, but only X/Y/Z have endstop/DIAG inputs (E0 does not) and the board has **no WiFi and no CAN** — only USB/UART. It is a single-printer board; 120 axes would need 40 boards and a USB infrastructure, and it costs more than the E4 while lacking the E4's onboard ESP32.
12. **FLY-RPFMEX is the best 4-axis/4-DIAG candidate, but it is a USB-C node.** It has 4 × TMC2209 onboard, an RP2040 (dual-core 133 MHz, PIO), and 4/4 DIAG on independent GPIOs at ~€26.59 — 30 boards ≈ €798 for 120 axes. Klipper uses `USBSERIAL` over the Type-C port; there is no CAN or WiFi. It can be used standalone via USB-C + 12–24 V (the missing heaters/temperature are irrelevant here), but 30 USB MCUs require a powered hub tree and a host with enough USB bandwidth/ports, and the timebase/sync design must be USB-based rather than CAN-broadcast. Hardware-wise it is the strongest cheap 4-axis node; topology-wise it is a different architecture from the CAN/WiFi baseline.
13. **Spider V3.0 H7 is disqualified by its CAN termination.** At $34.90 it is the cheapest 6-axis CAN board (H723, 8 sockets, 6/8 DIAG), but the FYSETC wiki states the 120 Ω CAN termination is permanent and cannot be disabled. A 20-node bus of these boards would be massively over-terminated. It is usable only as a bus end node, or after removing the termination resistor on every non-end board — not acceptable for a production node.
14. **Fly-Super8 Pro H723 is the 8/8-DIAG alternative, but likely not cheaper.** It has 8 sockets, H723, CAN and per-driver DIAG (8/8), but the price is not published on the doc page (estimate $80–100). At ~$125/board with drivers, 15 boards ≈ $1,875, above the Octopus Pro V5.0 bundle. The Fly-CDYv3 at ~$35 gives 6 axes and per-driver DIAG with ESP32 WiFi (RRF only), but Klipper uses USB; it is a 20-node WiFi/USB path.
15. **BTT SKR 3 / SKR 3 EZ is a 5/5-DIAG CAN/WiFi board, but 24 nodes is costly.** It has an H743 @ 480 MHz, five driver sockets, DIAG on all five axes (shared with the endstop pins), FDCAN and an ESP32/ESP-12S WiFi module header. At ~€75–80/board with drivers, 120 axes need 24 boards ≈ €1,800–1,900 hardware — above the Spider V3.0 H7 (~€1,220 for 20 boards) and the Octopus Pro V5.0 bundle (~€1,420 for 15 boards incl. drivers). It is a good 5-axis/5-DIAG node, but not the cheapest per axis; verify its CAN termination is selectable/DNP before shortlisting.
16. **FLY-Micro4 V1.0 is disqualified by its CAN physical layer.** The schematic shows an MCP2542 transceiver and a 2-pin PH2.0 CAN connector (CANH/CANL only, no common GND), plus a fixed split termination R39/R58 60.4 Ω + C63 100 nF. J3 removes only one 60.4 Ω half, leaving an asymmetric single-ended 60 Ω + C63 termination rather than opening the 120 Ω differential pair; it is not a proper termination disable. Thirty boards on one bus would over-terminate it (~4 Ω). The 4/4 DIAG and RP2040 are good, but the CAN termination makes it unsuitable as a 30-node production node.
17. **MKS Monster8 V1/V2 is a 6-axis CAN board only if its termination is selectable.** It has 8 sockets, built-in CAN and an F407, but only DIAG0–5 are routed (to the six endstops), so it is 6-axis. At ~€75–80/board with drivers, 20 boards ≈ €1,500–1,600. Before considering it, verify the CAN termination is selectable or DNP-able; the Spider V3.0 H7 is cheaper but is itself rejected on permanent termination.
18. **RAMPS 1.4 / MKS Gen L are rejected.** They are 8-bit ATmega2560 boards at 16 MHz with five driver sockets, no WiFi/CAN, and no DIAG routing to the MCU. The step-rate requirement (≈30 kHz/axis at 0.6 m/s, ≈61 kHz at 1.2 m/s; 150–305 k events/s for five axes) is far beyond an AVR running S-curves, and sensorless homing would need hand-wired DIAG. They are hobby/bench boards, not 120-axis nodes.
19. **Manta M8P V2 is an 8/8-DIAG WiFi board, but each node carries a Linux host.** It has 8 driver sockets, 8/8 DIAG via jumpers, and a CB1/CM4 slot for Klipper + WiFi. At ~€150–175/node with drivers and CB1, 15 nodes ≈ €2,300–2,600 hardware (~€3,500–4,000 system total), with 15 Linux hosts to manage. The Octopus Pro V5.0 bundle gives the same 8 axes for ~€1,420 incl. drivers; the Manta is only justified if a host per node is a requirement, and its CAN termination must be selectable/DNP.
20. **FLY-C8 is a host-controller all-in-one, not a cheap driver board.** It pairs an Allwinner H5 (1 GB DDR3, WiFi/Ethernet/CAN, HDMI) with an STM32F407 MCU and 8 driver sockets, but only DRIVER0–5 have DIAG (6/8). It is a complete Klipper host + 6-axis node at ~€100–150; 120 axes would need 20 boards, each with a Linux host, ~€2,600+ hardware. It is only justified if a host per node is required; the Octopus/Spider paths are cheaper and simpler.
21. **Octopus versions: FD is only the H723, and FD is not needed here.** The Octopus V1.1 / Pro F446 uses classic CAN and is ~€43–45 + drivers, cheaper than the H723/FD version (€94.69 bundle). For 120 axes at 1 Mbit/s, classic CAN is sufficient. Also correct the earlier implication: the Octopus does **not** have jumper-selectable termination; §11.6.1/§13.2 record an always-fitted 120 Ω, so it needs the same termination DNP/removal step as the other cheap CAN boards. Only the custom carrier has switchable termination.

### 11.5 Production shortlist

> **CAN-FD caveat:** on the COTS boards (Octopus H723, SKR 3, Manta) the FD capability is **hardware-only**; stock Katapult/Klipper runs classic CAN 1 Mbit/s. Do not select or price a board on CAN-FD unless you are also writing the custom bootloader/firmware that enables FD. For this project, classic CAN is sufficient.

> **CAN selection criterion:** a CAN node must have **selectable or DNP-able termination** (fit 120 Ω only at the two physical ends). No COTS board in this survey is confirmed stock-qualified: the Octopus, FLY-Micro4, Spider V3.0 H7 and Monster8 all require the termination resistors to be DNP/removed on every non-end node, or must be verified against their schematic. The **custom carrier** is the only design here with switchable split termination.

> **Flashing/maintenance:** RP2040/RP2354B nodes use **BOOTSEL/UF2** over USB-C — no bootloader install, easy recovery. STM32 boards (Octopus, Spider, Monster8, Super8 Pro) need an **initial bootloader via SD card, USB DFU/BOOT0, or ST-Link**, then Katapult/Klipper over CAN. CAN updates are fine once Katapult is installed; the initial multi-board setup and brick recovery are the extra work. This favors the Micro4 (RP2040) or the custom carrier (RP2354B) when field flashing matters.

| Board | MCU | Initial flash | Recovery | Field update |
|---|---|---|---|---|
| **Octopus V1.1 / Pro F446** | STM32F446 | SD card, USB DFU/BOOT0, or ST-Link; install Katapult | DFU/ST-Link | Katapult over CAN |
| **Octopus Pro H723** | STM32H723 | SD/DFU/BOOT0/ST-Link; install Katapult | DFU/ST-Link | Katapult over CAN |
| **FLY-Micro4** | RP2040 | **USB-C BOOTSEL/UF2** | **BOOTSEL/UF2** | USB UF2 or Katapult over CAN |
| **Spider V3.0 H7** | STM32H723 | SD/DFU/ST-Link; install Katapult | DFU/ST-Link | Katapult over CAN |
| **MKS Monster8** | STM32F407 | SD/DFU/ST-Link; install Katapult | DFU/ST-Link | Katapult over CAN |
| **Fly-Super8 Pro** | STM32H723 | SD/DFU/ST-Link; install Katapult | DFU/ST-Link | Katapult over CAN |
| **Custom carrier** | RP2354B | **USB-C BOOTSEL/UF2** | **BOOTSEL/UF2** | USB UF2 or Katapult over CAN |

The RP2040/RP2354B ROM UF2 is the simplest initial flash and recovery. The STM32 boards are field-updatable over CAN once Katapult is installed, but the first install and brick recovery need SD/DFU/ST-Link.


> **Cost basis (important).** **Termination rework is assumed in-house/free**; if outsourced, add €10–150 depending on the board (13 resistors on the Octopus V1.1, 56 on the Micro4, 18 on the Spider). **Board set** = boards + drivers. **Hardware subtotal** = board set + board-level power/CAN harness. **System total** = hardware subtotal + motors, PSU and cabling. The COTS hardware figures in this list are **boards + drivers only**. Add ~€1,200–1,400 for motors, PSU and cabling. The custom Pico 2 carrier figure is a **system total**. Like-for-like **system totals**: RPFMEX ~€2,000–2,200; Pico 2 carrier ~€1,980–2,230; E4 ~€2,420–2,620; Micro4 ~€2,030–2,380; Octopus V1.1 ~€2,385–2,825; Octopus Pro H723 bundle ~€2,620–2,820. The Micro4 and Spider V3.0 H7 are viable only with termination rework; no COTS board here is stock-qualified. The Octopus Pro H723 bundle is among the most expensive system totals, not the Pico carrier; the Octopus V1.1 is cheaper; the Pico carrier's hidden cost is custom PCB/firmware engineering and first-article debug, not BOM. See `BOM_summary.md` for the custom carrier's parts-only and manufacturing ranges.

- **Best COTS integration:** **Duet 3 Mini 5+ WiFi** or the FYSETC Big Dipper clone — 5/5 DIAG (verify), native WiFi, 24 nodes.
- **Best 8-axis classic-CAN value, with rework:** **Octopus V1.1 (non-Pro)** — 8/8 DIAG via jumpers, 3 NTC inputs, **2-pin CAN**, 15 nodes, ~€1,200 for boards + drivers. Remove the 120 Ω on 13 of 15 boards. **STM32 flashing:** SD/DFU/ST-Link, then Katapult over CAN; not UF2. The **Pro F446/H723 uses RJ11**, which is not an appropriate field connector; use the Pro only if you need its 5 NTC + PT100 inputs and replace the RJ11 with a pigtail.
- **Octopus Pro H723 — cabling caveat:** 8/8 DIAG, 5 NTC + PT100, 15 nodes, €94.69/board incl. drivers, but the **6P6C RJ11** is a poor field connector. Excluded from the cabling shortlist; use the V1.1 2-pin or a JST-GH pigtail.
- **FYSETC E4:** €30.39, but only **3 usable axes** → 40 boards/nodes. Cheapest COTS system cost, but the highest node count and no FTM.
- **FYSETC Cheetah v2.0:** €35.39 with 4 onboard TMC2209, but 3/4 DIAG and **no WiFi/CAN**; not a network node.
- **Mellow FLY-RPFMEX:** ~€26.59 with 4 onboard TMC2209 and **4/4 DIAG** on RP2040; 30 boards ≈ €798. **USB-C/USBSERIAL** host link, no CAN/WiFi → 30 USB nodes; strongest cheap 4-axis hardware, but a different topology from the CAN/WiFi baseline.
- **Mellow FLY-Micro4 V1.0 — reliability/cost middle ground:** 4/4 DIAG, 4 × TMC2209 onboard (no sockets), RP2040, CAN; 30 nodes, **~€730 net for 30 boards** (€27 each, free shipping, −€80 coupon). **Viable with termination rework:** remove R39/R58 on 28 of 30 boards and keep them on the two end nodes. The 2-pin CAN has no GND, but a 1 V ground shift is inside the CAN common-mode range; use a common 24 V busbar/star ground. A TMC failure costs a 4-axis board (~€27), so board replacement is the module swap. At €27/board, 30 boards = €810 − €80 coupon = **€730 net**, free shipping; hardware subtotal ~€830–980 and system total ~€2,030–2,380 with motors/PSU/cabling. This is now the **lowest-cost COTS option**. **RP2040 UF2 via USB-C** — easiest flashing/recovery.
- **FYSETC Spider V3.0 H7:** **viable with rework** — 6/8 DIAG, 20 nodes, ~€1,220; remove the permanent 120 Ω on 18 of 20 boards. STM32H7 flashing via SD/DFU/ST-Link, not UF2.
- **MKS Monster8 V1/V2:** ~€45–55, 8 sockets, **6/8 DIAG** (DIAG0–5), CAN, F407 → 20 boards, ~€1,500–1,600 with drivers; dominated by the Spider V3.0 H7. STM32F407 flashing via SD/DFU, not UF2.
- **Mellow Fly-Super8 Pro H723:** 8 sockets, **8/8 DIAG**, CAN, H723 — 15 boards, but likely ~€1,875 with drivers, above the Octopus bundle.
- **Mellow Fly-CDYv3:** ~$35, 6 axes, per-driver DIAG, ESP32 WiFi (RRF only) → 20 boards, ~€1,220 with drivers; Klipper via USB.
- **BTT SKR 3 / SKR 3 EZ:** ~€50–60, 5 axes, **5/5 DIAG**, FDCAN + WiFi module, H743 → 24 boards, ~€1,800–1,900 with drivers; good 5-axis node, but verify selectable CAN termination and note the higher cost.
- **BTT Manta M8P V2 + CB1/CM4:** 8 axes, **8/8 DIAG**, WiFi via a Linux host per board → 15 nodes, ~€2,300–2,600 hardware; heavier and more expensive than the Octopus bundle.
- **Mellow FLY-C8:** host+MCU all-in-one (H5 + F407), 8 sockets but **6/8 DIAG**, WiFi/Ethernet/CAN → 20 boards, each a Linux host, ~€2,600+; overkill unless host-per-node is required.
- **Mellow FLY D5 V1.0:** ~€40–42/board with 5 TMC2209 modules → 24 CAN nodes, ~€960–1,000 hardware; 5/5 DIAG via jumpers if the modules have the DIAG pin. Cheapest CAN node, but only if the STM32F072 can carry the custom 5-axis step/timebase firmware.
- **Mellow FLY E3 Pro Ultra / Max:** €87/€99 with 5 onboard TMC2209, but only 3/5 axes support sensorless homing → 40 boards; not competitive.
- **Best integration, not lowest cost:** **custom Pico 2 carrier (bare RP2354B) + 12 TMC2209** — 10 nodes, 12/12 DIAG, ~€300–350 pure carrier + ~€480 drivers (120 axes). The €548 figure is parts only for 10 boards; with PCB/assembly/test/yield/NRE the hardware subtotal is ~€1,110–1,980, plus design/first-article effort (`BOM_summary.md`). **RP2354B UF2 via USB-C** — the easiest flashing/recovery of the shortlist. Choose it only if 10 nodes and the integrated thermal/interlock/expansion justify the design NRE.
- **Pico 2 carrier:** 12/12 DIAG, 10 nodes, 200 kHz PIO; pure carrier ~€300–350 (no TMC, no WiFi); WiFi handled separately.
- **Unusable as-is under the no-switch rule:** MKS TinyBee (no UART), MKS DLC32 (no free DIAG).
- **Custom 16-axis board:** lowest node count, but the module carrier is now cheaper at ~€1,940–2,180.

If the project intends to buy rather than build, the **Duet** or **Octopus** paths are the production choices; the custom board is the optimization.

### 11.6 Custom module-based carrier board

The missing middle: a custom PCB that carries **modules**, not QFN chips. Only passives, connectors, the 74HC595 chain and power are soldered; the MCU and drivers plug in.

**ESP32-S3 carrier, 16 axes**

- ESP32-S3-WROOM-1 module (WiFi + FTM).
- 16 × TMC2209 StepSticks plug into sockets on the carrier — **not included in carrier BOM**.
- 4 × 74HC595 (32 mux outputs: 16 step + 16 direction).
- 24 V → 5 V/3.3 V DC-DC, bulk capacitors, per-driver 100 µF, input fuse/TVS.
- 16 motor connectors, power input, DIAG/endstop header.
- No QFN soldering. JLCPCB/PCBWay can place the passives and connectors; the module headers are hand-soldered.

| Item | Qty | Cost (est.) |
|------|-----|-------------|
| 4-layer PCB, 100 × 100 mm | 1 | €5 |
| ESP32-S3-WROOM-1 module | 1 | €4 |
| 74HC595 | 4 | €1 |
| Headers/sockets | 17 | €2 |
| 24 V → 5 V/3.3 V DC-DC | 1 | €2 |
| 4:1 UART mux | 1 | €0.50 |
| Capacitors, connectors, fuse, TVS | 1 set | €10 |
| 16 × TMC2209 StepStick sockets | 16 | €1.60 |
| Assembly (passives/connectors) | 1 | €5–10 |
| **Pure carrier (no TMC)** | | **~€32–37** |
| **8 pure carriers** | | **~€256–296** |
| **+ NRE (stencil, setup, review; first-article/design separate)** | | **~€50–100** |

With motors, PSU and cabling (~€1,200–1,400), the system is ~€1,940–2,180 (excluding NRE) — the lowest-cost production path in this table.

**Pico 2 carrier, 12 axes — bare RP2354B**

- **Bare RP2354B** (QFN-80, 48 GPIO) on a custom 4-layer carrier board.
- FastAccelStepper on RISC-V gives **12 steppers** at 200 kHz.
- 12 × **bare TMC2209** (QFN-28, `TMC2209-LA`) soldered on the carrier, one per axis. The StepStick socket is rejected here: its 2×8 header carries EN/MS1/MS2/PDN_UART/CLK/STEP/DIR/GND and VM/GND/A1/A2/B1/B2/VDD/GND, but **`DIAG` is only on a `CONT1` solder pad**, so a socketed build cannot deliver the 12 `DIAG` lines the architecture relies on.
- **`DIAG` is event-driven.** It is a push-pull output that asserts on stall, over-temperature pre-warning, short-to-GND and open-load. All 12 go to interrupt-capable GPIOs, so a fault raises an ISR and the handler reads `DRV_STATUS`/`SG_RESULT` over UART — no continuous polling of 12 drivers.
- **`EN` is the motor-power control, not a high-side FET.** All 12 `ENN` pins share `TMC_EN` (active-low) with a **single 10 kΩ pull-up to `+3V3` at the MCU** (`R9`), so a floating or crashed MCU leaves every output stage **disabled**. Per-driver pull-ups were dropped deliberately: a bad IC joint leaves that pin floating anyway, so 12 resistors would only cover a cracked trace, at the cost of 11 parts and ~4 mA of GPIO sink current. A series MOSFET on the shared 24 V rail is rejected: it would switch ~1.2 mF (12 × 100 µF) and needs soft-start, while `EN` already disables the output stage in microseconds and keeps the logic powered.
- **Driver instance (×12, one page each):** `TMC2209-LA` QFN-28; `0.47 Ω` sense resistors (max 0.47 A RMS at `IRUN=31`, ≈0.25 A at `IRUN≈17`); `100 nF + 100 µF` on `VS`; `100 nF` `VCP→VS`; `22 nF` `CPO–CPI`; `2.2 µF` on `5VOUT`; `100 nF` on `VCC_IO`; `1 kΩ` `PDN_UART` series; `MS1`/`MS2` tied **directly** to `GND`/`+3V3` (no straps); 4-pin motor connector. `CLK` and `SPREAD` are tied to GND (internal clock, StealthChop), `VREF`/`INDEX` open.
- **Motor and endstop connectors:** 12 × 4-pin JST XH (`J4`–`J15`) for motor phases are required. The 12 × 2-pin JST XH (`J18`–`J29`) endstop connectors are **DNP by default** — sensorless homing via `DIAG` is the default; direct wiring to the U23 spare inputs can replace them. U24, C117 and R90–R101 are also DNP.
- **Temperature sensing:** the TMC2209 has **no numeric temperature register** — `DRV_STATUS` only exposes `otpw` (pre-warning) and `otsd` (shutdown). Actual temperatures come from four `TMP1075DGKR` on I²C1: `U19` 0x48 ambient, `U20`/`U21`/`U22` 0x49/0x4A/0x4B for the three driver groups (A0–A2 strapped to GND/+3V3). Shared 2 × 4.7 kΩ pull-ups, 100 nF per sensor. On the PCB each sensor sits mid-group with a copper patch to the plane; `DIAG` remains the fast per-driver cutoff.
- **UART addressing:** three single-wire buses carry four drivers each (drivers 1–4 bus 0, 5–8 bus 1, 9–12 bus 2); `MS1`/`MS2` are hard-tied to `GND`/`+3V3` to set address 0–3 (no 0 Ω straps — the addresses are fixed in the bus plan). The 12 pages differ only in designators, the per-driver local nets, `STEPnn`/`DIRnn`/`DIAGnn`, and the two hard-tied address pins.
- 12 step + 12 direction + CAN (MCP2518FD + **TJA1051T/3**; **MCP2562FD** is the pin-compatible CAN-FD alternative) + 3× TMC UART (bit-banged) + I²C1 (4 temperature sensors + 1 populated GPIO expander; U24 DNP) + expander interrupt + common enable + status LED = **37 GPIO** for the motion core, plus **11 GPIO** for the expansion/service block (§11.6.0a) = **48 GPIO, 0 spare**. The 12 `DIAG` lines are on **U23 (TCA9555, 0x20)** with 4 spare inputs; **U24 (0x21) is DNP** for the optional endstops, so the STEP/DIR block fits entirely inside the PIO `GPIO0–31` window.
- 2 MB integrated QSPI flash; 24 V → 5 V/3.3 V DC-DC; USB-C for programming/debug.
- **USB-C (device, programming/debug):** 16-pin receptacle, `CC1`/`CC2` pulled down with 5.1 kΩ (sink), `D±` pass through a `USBLC6-2SC6` ESD array, `SBU` pins open.
- **USB / 24 V diode-OR:** `VBUS → D3 (SS14) → +5V_SYS` and `buck +5V → D4 (SS14) → +5V_SYS`; the AMS1117 runs from `+5V_SYS`. Whichever source is present powers the board, the other diode blocks, so **USB alone can flash the board** and neither the 24 V buck nor the USB host is ever back-fed. With both connected `+5V_SYS` sits at the higher source minus ~0.5 V; the AMS1117 still regulates at the MCU's ~0.2 A load. There is **no VBUS-sense GPIO** (none is needed by the USB device stack; all GPIO are now assigned, so VBUS sense would have to share an ADC channel).
- **24 V input protection:** fuse → **33 V TVS** → reverse-polarity P-MOS → buck input, then the TPS54360B (600 kHz, 162 kΩ `RT`, EN divider 453 kΩ/88.7 kΩ → UVLO ≈ 7.3 V start / 5.8 V stop typical). The TVS sits **before** the P-MOS on purpose: the SMBJ33A clamps at 53.3 V while the reverse-polarity FET is only rated **Vds 40 V**, so clamping first keeps the FET's Vds to ≈ clamp − 24 V ≈ 29 V instead of exposing it to the unclamped surge.
- **24 V current budget (per board):** the motor rail dominates — 12 × TMC2209 at 0.25 A RMS holding = **≈3 A continuous** (72 W). The buck itself only adds ~0.1 A (it carries ~0.25 A at 5 V). Therefore **F1 must be 4–5 A**, not 2 A, and the P-MOS, connector and 24 V copper must be rated for ≥3 A continuous.
- **Bulk on the 24 V rail is the real requirement, not the buck's 10 µF.** The 2 × 10 µF ceramics are the buck's HF input and are correctly sized for ~0.1 A input ripple. The motor rail needs **220–470 µF / 35–50 V bulk** near the motor connectors plus **100 µF per driver** (as in §11.6's ESP32-carrier note). A small ferrite/few-µH filter between the motor `+24 V` and the buck input keeps chopper noise out of the converter.
- **Input capacitor voltage rating: 50 V is accepted.** The rail is a clean 24 V supply, so 50 V gives ~2 × continuous margin; the SMBJ33A clamp (~53 V) is a transient only. The 50 V MLCCs fail **short**, so **F1 clears the fault** (it sits upstream of the caps). If margin is ever tightened, use two `200 nF / 100 V` in series (→ 100 nF / 200 V) rather than a single higher-voltage part, with ~1 MΩ balancing resistors.
- **10 boards / 10 nodes** for 120 axes, **12/12 DIAG**.
- WiFi handled separately (Octopus Pro H723 CAN node, or a standalone ESP32-S3 AP). The carrier itself has no WiFi.

| Item | Qty | Cost (est.) |
|------|-----|-------------|
| 4-layer PCB, 120 × 160 mm | 1 | €8 |
| RP2354B (QFN-80) | 1 | €4 |
| CAN transceiver (TJA1051T/3, classic; MCP2562FD FD alternative) | 1 | €0.58 |
| 24 V → 5 V/3.3 V DC-DC | 1 | €2 |
| Fuse holder + 5 A Nano2 fuse (Littelfuse 0154005.DR, 125 V, 50 A interrupt) | 1 | €1.50 |
| 24 V bulk polymer, 220 µF / 35 V (SPE1VM221E12OL) | 1 | €0.50 |
| Capacitors, connectors, TVS | 1 set | €10 |
| 12 × motor connectors (mating with motor cable) | 12 | €1.50 |
| 12 × TMC2209-LA (QFN-28, C2150710) | 12 | €34 |
| Per-driver passives (VCP/CPO caps, sense resistors, 100 µF) | 12 set | €6 |
| USB-C connector (programming/debug) | 1 | €0.30 |
| Status LED + resistor | 1 | €0.10 |
| Assembly (QFN/passives/connectors) | 1 | €10–15 |
| **Driver board (RP2354B + 12 TMC2209; parts + PCB + SMT)** | | **~€69–74** |
| **10 driver boards (same basis)** | | **~€690–740** |
| **+ NRE (stencil, setup, review; first-article/design separate)** | | **~€50–100** |

With 12 × TMC2209-LA (~€2.83 each = €34/board) plus motors, PSU and cabling (~€1,200–1,400), the 10-board system is ~€1,890–2,140 on the parts+PCB+SMT basis. Including THT soldering, test, yield and NRE, the hardware subtotal is ~€1,074–1,939 and the system total ~€2,274–3,339, plus design/first-article effort (`BOM_summary.md`); it is **not cheaper** than the Octopus V1.1 or Micro4 once design NRE is counted. The bare IC is chosen for **`DIAG` and reliability**, not cost: 12 socketed 2 A contacts are the classic failure point in an installation that runs unattended, and the socket cannot carry `DIAG` at all.

**GPIO budget, bare RP2354B (48 GPIO), 12 axes:**

| Function | Qty | GPIO |
|----------|-----|------|
| 12 × STEP + 12 × DIR (PIO, interleaved) | 24 | 24 |
| CAN-FD controller (SCK/MOSI/MISO/CS + INT) | 5 | 5 |
| TMC UART (3 buses) | 3 | 3 |
| I²C1 (4 temp sensors + 2 GPIO expanders) | 2 | 2 |
| Expander interrupt (`EXINT`) | 1 | 1 |
| Common enable (via interlock gate) | 1 | 1 |
| Status/fault LED | 1 | 1 |
| Expansion: interlock sense | 1 | 1 |
| Expansion: WiFi UART | 2 | 2 |
| Expansion: RS-485 (TX/RX/DE) | 3 | 3 |
| Expansion: 24 V rail sense (ADC0) | 1 | 1 |
| Expansion: SPI display CS/DC | 2 | 2 |
| Expansion: fan PWM/tach | 2 | 2 |
| **Total** | **48** | **48 — 0 spare** |

**Pin assignment (final):**

| GPIO | Net | GPIO | Net |
|------|-----|------|-----|
| 0–3 | SPI0 MISO/CS/SCK/MOSI | 28 | `CAN_INT` |
| 4, 5 | STEP01, DIR01 | 29 | `EXINT` (TCA9555) |
| 6, 7 | STEP02, DIR02 | 30 | `TMC_EN_CTRL` → interlock gate |
| 8, 9 | STEP03, DIR03 | 31 | `LED_STATUS` |
| 10, 11 | STEP04, DIR04 | 32–34 | `TMC_UART0/1/2` |
| 12, 13 | STEP05, DIR05 | 35 | `INTERLOCK` |
| 14, 15 | STEP06, DIR06 | 36, 37 | `WIFI_TX`, `WIFI_RX` |
| 16, 17 | STEP07, DIR07 | 38, 39 | `RS485_TX`, `RS485_RX` |
| 18, 19 | STEP08, DIR08 | 40 / ADC0 | `VIN_24V_SENSE` |
| 20, 21 | STEP09, DIR09 | 41 / ADC1 | `DISP_CS` |
| 22, 23 | STEP10, DIR10 | 42, 43 | I²C1 SDA/SCL |
| 24, 25 | STEP11, DIR11 | 44 / ADC4 | `DISP_DC` |
| 26, 27 | STEP12, DIR12 | 45 / ADC5 | `RS485_DE` |
| | | 46 / ADC6 | `FAN_PWM` |
| | | 47 / ADC7 | `FAN_TACH` |

All 24 STEP/DIR pins sit inside the PIO `GPIO0–31` window. The 12 `DIAG` lines went to a **TCA9555 I²C expander** (0x20), which asserts `EXINT` on any change — DIAG stays interrupt-driven while freeing 12 pins. The optional endstop inputs use a second **TCA9555** (U24, 0x21) on 12 × 2-pin headers with 10 kΩ pull-ups; **U24, C117, R90–R101 and J18–J29 are DNP** in the default build (sensorless homing via DIAG).

The CAN-FD **5 pins are four SPI signals plus INT** — there is no enable pin. The transceiver STB is tied low, and the controller's crystal is local to the module. Polling instead of using INT drops it to 4 pins; add CLKIN only if the MCU supplies the controller clock instead of a local crystal.

Boot/reset uses the dedicated RUN pin; USB and the crystal are dedicated. With DIAG on U23 (and optional DNP endstops on U24) the motion core uses **37 of 48 GPIO**; the remaining 11 are assigned to the expansion/service block below, so **no GPIO is left unallocated**.

#### 11.6.0a Expansion and service block (11 pins)

The `Expansion` schematic page carries the following optional and service circuits. All configuration is by **pluggable 2.54 mm shunts or DNP headers**; there are no 0 Ω links and no solder bridges.

| Circuit | Nets / parts | Default population | Notes |
|---------|--------------|--------------------|-------|
| Enclosure interlock | `INTERLOCK` loop → 74LVC1G32 OR gate, output `TMC_EN` | fitted | NC loop through all panels; 10 kΩ pull-up, 100 nF filter. Gate also takes `TMC_EN_CTRL` from GPIO30. Opening the loop forces `TMC_EN` high (all drivers off) even if the MCU is hung; a dead gate leaves the bus pull-up to disable the drivers. Fulfils `NFR-SAFE-005` without relying on firmware. |
| 24 V rail sense | 100 kΩ / 10 kΩ divider → `VIN_24V_SENSE` (ADC0) | fitted | 2.18 V at 24 V, 100 nF filter; detects brown-out before motion. |
| WiFi bridge | 1×5 `5V / 3V3 / GND / TX / RX` → `WIFI_TX/RX` | DNP header | ESP32-C3 SuperMini (or equivalent) as a UART↔WiFi bridge. The host RP2354B forwards to CAN if it is the network master. No per-node ESP32-CAM. |
| RS-485 | SP3485 + SM712 + 3P terminal, `RS485_TX/RX/DE`, jumperable 120 Ω | fitted (optional function) | 3.3 V half-duplex; DE//RE on one GPIO; fail-safe bias fitted — fit bias at **one** RS485 node only (DNP on the others); A/B follow the SP3485 convention (silkscreen warning). |
| SPI display | 1×7 `3V3 / GND / SCK / MOSI / CS / DC / RST` | DNP header | Shares `SPI0_SCK/MOSI` with the MCP2518FD; `CS`/`DC` use GPIO41/44. RST and backlight to 3V3. |
| Fan | 1×4 `GND / +5V_SYS / TACH / PWM` | DNP header | 4-pin 5 V fan. `JP2`/`JP3` (1×3 shunts) route GPIO46/47 to the fan or to the future-use header. |
| Future use | 1×5 `3V3 / +5V_SYS / GND / EXP_IO1 / EXP_IO2` | fitted | `EXP_IO1/2` are the alternate JP2/JP3 positions. |
| Saleae / logic analyser | `J37` GND+`INTERLOCK,WIFI_TX,WIFI_RX,RS485_TX`; `J38` GND+`RS485_RX,CAN_INT,EXINT,TMC_UART0`; `J39` GND+ADC pins | DNP | Two GND+4 groups for an 8-channel analyser, plus a separate analog header. |
| Driver probes | 12 × 1×4 `GND / STEPnn / DIRnn / DIAGnn` | DNP | One header next to each TMC2209 on its driver page. |
| Test points | `TP1`…`TP8` = `+24V_PROT`, `+5V_SYS`, `+3V3`, `+1V1`, `GND`, `CANH`, `CANL`, `TMC_EN` | fitted | 1×1 2.54 mm pads. |

**Expansion BOM (JLCPCB/LCSC, verified in stock at design time).**

| Ref | Part | LCSC | Populated |
|-----|------|------|-----------|
| U26 | 74LVC1G32GW OR gate, SOT-353 | C19829655 | yes |
| R102 | 10 kΩ 0402 pull-up | C25744 | yes |
| R103 | 100 kΩ 0402 divider top | C25741 | yes |
| R104 | 10 kΩ 0402 divider bottom | C25744 | yes |
| C119, C120 | 100 nF 0402 | C1525 | yes |
| J32 | 1×2 2.54 mm interlock loop | C492401 | yes |
| J33 | 1×5 2.54 mm WiFi module | C492404 | header yes, module DNP |
| J34 | 1×7 2.54 mm SPI display | C492406 | header yes, module DNP |
| J35 | 1×4 2.54 mm fan | C2691448 | header yes, fan DNP |
| JP2, JP3 | 1×3 2.54 mm shunt selectors | C2937625 | yes |
| J36 | 1×5 2.54 mm future use | C492404 | yes |
| J37, J38 | 1×5 2.54 mm Saleae | C492404 | **DNP** |
| J39 | 1×7 2.54 mm analog probe | C492406 | **DNP** |
| J41–J52 | 1×4 2.54 mm driver probes | C2691448 | **DNP** |
| TP1–TP8 | 1×1 2.54 mm test points | C492400 | yes |
| U25 | SP3485EN RS-485, SOIC-8 | C8963 | yes |
| D7 | PSM712 TVS, SOT-23 | C32677 | yes |
| R18, R19 | 680 Ω 0402 fail-safe bias | C25130 | yes (fit at one node only; DNP on the others) |
| R20 | 120 Ω 1206 termination | C17909 | via J31 shunt |
| R24 | 10 kΩ 0402 DE pull-down | C25744 | yes |
| C118 | 100 nF 0402 | C1525 | yes |
| J30 | 3P 5.0 mm terminal | C474882 | yes |
| J31 | 1×2 2.54 mm termination shunt | C492401 | yes |

> **Motor rail.** The driver `VM`/`VS` pins and their bulk capacitors are the **same net as `+24V_PROT`** (after the fuse and reverse-polarity P-MOS). There is no separate motor-supply input: the 24 V input feeds both the buck and the TMC2209 output stages, as the 24 V current budget assumes.

A single-wire WS2812 fits the same LED pin, so it does not change the count. It does need 5 V and a level shifter (3.3 V data is marginal against the 0.7 VDD threshold), and precise 800 kHz timing. Since all three PIO blocks are committed to the step streams, drive it from the second core with interrupts disabled for the ~30 µs frame, or use a plain LED.

**UARTs:** a TMC2209 has 4 UART addresses per bus, so 12 drivers need **3 buses**. Each single-wire half-duplex bus needs one GPIO, i.e. **3 GPIO** total. The RP2354B has 2 hardware UARTs; remapping does not create a third peripheral, and all three PIO blocks are already used by the 12 step streams.

That does not force a mux: **bit-bang the third bus (or all three) on the second core**. UART is only used for configuration and homing, and 115200 baud is trivial for a 150 MHz core. Bit-banging also gives exact control of the half-duplex turnaround: drive during TX, then switch the pin to input to release the line for the TMC reply. A 3:1 mux on one UART is the alternative, also 3 GPIO (data + 2 select).

**Write-only option:** if every driver receives identical configuration, tie the three buses together and **broadcast from one TX pin**; no mux and no RX. This works because address 0–3 is repeated once per bus, so a write reaches all three drivers with that address. It removes readback, so you lose `SG_RESULT` tuning, `DRV_STATUS` fault/temperature flags, and configuration verification. Homing still works from the per-driver DIAG pins. Use this only after bench tuning, and keep a service header for readback; otherwise retain one bidirectional pin per bus (3 GPIO) or a mux.

**Temperature:** TMC2209 reports over-temperature warning/shutdown flags in `DRV_STATUS`, but not necessarily an absolute temperature. If a numeric reading is required, add one I²C sensor per board near the driver array (2 GPIO); that is the 2-pin line above.

**Pico 2 module limitation:** once CAN-FD, UART and a temperature sensor are added, the Pico 2 module's ~26 GPIO is too tight even with both shift registers: 12 STEP + 3 DIR + 3 DIAG + 5 CAN + 3 UART + 2 temp + 1 enable = 29. For a CAN node, use the **bare RP2354B** (48 GPIO); the Pico 2 module is better suited to the WiFi-only variant or to 8 axes.

#### 11.6.1 CAN front end — two connectors, symmetric switchable termination

The Octopus Pro has one CAN connector and an always-fitted 120 Ω (§13.2), so an
installed set over-terminates as soon as more than two boards keep the resistor.
The Pico carrier fixes this at the board level: **two JST-GH connectors (CAN IN /
CAN OUT) and a jumper-selectable split termination**, so a node can be an end node
from either side and every intermediate node leaves its termination open.

**Grounding.** The CAN GND is **optional**: CAN is differential and a 1 V ground shift is inside the transceiver common-mode range, so a 2-pin CAN is electrically sufficient when all nodes share a common 24 V return. The 3-pin JST-GH's GND is useful as a signal/ESD reference if needed; if used, keep it as a separate signal ground, join it to the power ground at one star point (preferably at the PSU), and do not use it as a motor return. The Octopus RJ11's GND is tied to the power ground and its connector is poor; the Micro4 2-pin has no GND and references the power ground. Both are electrically acceptable for ~1 V, but the connector/cabling differs.

**Transceiver.** `TJA1051T/3` (SOIC-8, classic CAN 1 Mbit/s, VCC = 5 V with VIO = 3.3 V) is the default: same pinout as the `MCP2562FD-H/SN` and ~€0.58 vs €2.90. The TJA1050 is the 5 V-only part with a 5 V RXD output and no VIO, so it would overdrive the 3.3 V RP2354B; do not use it. Both rails already exist on the carrier (24 V → 5 V, 5 V → 3.3 V). **CAN-FD alternative:** `MCP2562FD-H/SN` is pin-compatible (same SOIC-8; pin 8 is STBY instead of S, tie to GND), so the same footprint can be fitted when CAN-FD is needed. The `MCP2518FD` controller is FD-capable; the default TJA1051T/3 limits the bus to classic CAN.

| Pin | Name | Connection |
|-----|------|------------|
| 1 | TXD | MCP2518FD `TXCAN` (controller drives the bus) |
| 2 | VSS | GND |
| 3 | VCC (VDD) | `+5V_SYS`, 100 nF |
| 4 | RXD | MCP2518FD `RXCAN` |
| 5 | VIO | +3V3, 100 nF |
| 6 | CANL | bus CANL, 60.4 Ω split leg |
| 7 | CANH | bus CANH, 60.4 Ω split leg |
| 8 | S (STBY on MCP2562FD) | GND (normal mode; high = silent/standby) |

**Physical layer.**

```
  J1 CAN-IN ──o───────────────o── CANH ── R1 60.4R ──[JP1] ──┐
              │               │                              ├── C1 4.7nF ── GND
  J2 CAN-OUT ─o───────────────o── CANL ── R2 60.4R ──[JP2] ──┘
                              │
                              ├── U TJA1051T/3 (MCP2562FD pin-compatible FD alternative)
                              └── D PESD1CAN
```

- **J1 / J2 are wired in parallel** (pin 1 = CANH, pin 2 = CANL, pin 3 = GND) and
  the transceiver tap plus the termination sit **midway between them**, so both
  connectors see the same stub. This is the "symmetric" requirement: neither
  connector is privileged, and the termination is not a stub hanging off one end.
- **Split termination**: 2 × 60.4 Ω (1 %) in series across CANH/CANL with their
  common node returned to GND through 4.7 nF. This is the 120 Ω bus termination,
  but symmetric with respect to CANH and CANL, and it gives the common-mode noise
  a defined path. 2 × 60.4 Ω = 120.8 Ω, inside the usual 120 Ω ±5 % window.
- **Both legs are switched.** A single 1-pole jumper cannot disable a split
  termination without leaving 120 Ω partly connected; opening both legs is what
  makes the node a clean pass-through. JP1/JP2 are the two poles of one 2 × 2
  header (`PZ2.54-2x2`), with two shunts: **fit both shunts only on the two
  physical end nodes.**
- `PESD1CAN` protects the bus against field transients on the JST-GH wiring.
- BOM: 2 × `SM03B-GHS-TB`, 1 × `TJA1051T/3` (pin-compatible `MCP2562FD` for CAN-FD), 1 × `MCP2518FD` (SOIC-14, local
  40 MHz crystal), 2 × 60.4 Ω, 1 × 4.7 nF, 1 × 2 × 2 header, 1 × `PESD1CAN`,
  3 × 100 nF.

**Concrete GPIO map (RP2354B, 48 GPIO, 48 used).** This is the pin-level
assignment used by the schematic; it supersedes the earlier budget-only table.

| Signal | GPIO | Alt function used |
|--------|------|-------------------|
| CAN SPI MISO / CS / SCK / MOSI | GPIO0–3 | SPI0 |
| STEP01…STEP12, DIR01…DIR12 | GPIO4–27 | PIO step streams |
| CAN nINT | GPIO28 | SIO/IRQ |
| TCA9555 expander interrupt | GPIO29 | SIO/IRQ |
| TMC common EN (`TMC_EN_CTRL`) | GPIO30 | SIO, to interlock OR gate |
| Status/fault LED | GPIO31 | SIO |
| TMC UART bus 0/1/2 | GPIO32–34 | bit-banged |
| Enclosure interlock sense | GPIO35 | SIO input |
| WiFi bridge TX / RX | GPIO36, GPIO37 | UART |
| RS-485 TX / RX | GPIO38, GPIO39 | UART |
| 24 V rail sense | GPIO40 / ADC0 | ADC |
| SPI display CS | GPIO41 / ADC1 | GPIO |
| I²C1 SDA / SCL | GPIO42, GPIO43 | I²C1 |
| SPI display DC | GPIO44 / ADC4 | GPIO |
| RS-485 DE//RE | GPIO45 / ADC5 | GPIO |
| Fan PWM / tach | GPIO46, GPIO47 / ADC6, ADC7 | GPIO / input |

The 40 MHz crystal for the MCP2518FD stays local (as the doc's 5-pin CAN budget
assumes); sharing an RP2350 clock output would need a sixth pin and is not used.

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

## 13. Wired CAN Node — Octopus Pro H723 (V5.0)

> Alternative to the WiFi baseline. The H723 variant has an FDCAN controller and an MCP2542 transceiver, 8 drivers, 8 DIAG outputs routed to endstop inputs by on-board DIAG jumpers (module DIAG pins required), and a 6P6C RJ11 CAN connector.
>
> **Cabling verdict:** the 6P6C RJ11 is **not an appropriate field connector**. The **Octopus V1.1 2-pin CAN** is the preferred Octopus variant; the Pro H723 is documented here for reference.

### 13.1 CAN or CAN-FD in practice

- The **hardware** is CAN-FD capable: STM32H723 FDCAN + MCP2542 (an FD transceiver).
- The **COTS software stack is not**. Katapult's STM32H723 configuration uses **classic CAN at 1 Mbit/s**; Klipper also uses classic CAN. CAN-SW-Upgrade over CAN therefore works, but not with CAN-FD frames.
- True CAN-FD would require a custom bootloader/firmware path; it is not available off the shelf for this board.
- For this project, classic CAN at 1 Mbit/s is sufficient for both control and firmware upload. CAN-FD is optional future headroom, not a requirement.

### 13.2 Physical node

- 24 V power, 8 motor connectors, one 6P6C RJ11 CAN connector.
- CAN is a **bus**, not a true daisy-chain: run a trunk with short stubs. Terminate with 120 Ω at the two physical ends only.
- The schematic shows one CAN connector and a 120 Ω resistor. Check whether that resistor is fitted; if it is fitted on every board, remove it on all intermediate nodes or the bus will be over-terminated.
- For in/out daisy-chaining, add a second RJ11 in parallel or use a passive splitter at each node. The custom Pico carrier (§11.6.1) already has this built in: two connectors, and a split termination switched by a 2-pole jumper so only the two end nodes are terminated.
- **Grounding:** the RJ11 GND is tied to the board power ground. CAN is differential and a 1 V ground shift is inside the common-mode range, so **leave the RJ11 GND unconnected** unless you need a common-mode/ESD reference. If you do use it, keep it as a separate signal ground, join it at one star point, and never use it as a motor return. The Pro's remaining cabling issue is the RJ11 connector itself; the V1.1 2-pin references the power ground.
- **Driver modules:** use pre-assembled TMC2209 StepSticks with the extra 2-pin `DIAG`/`INDEX` header fitted (e.g. Makebase/MKS, ~€34.79/8 ≈ €4.35 each). This avoids hand-soldering 18 pins per module; DIAG then reaches the MCU through the Octopus on-board DIAG jumpers, not through external wiring.

### 13.3 Firmware update

- Katapult `flashtool.py` can request the bootloader and upload over CAN at 1 Mbit/s.
- Budget ~10–30 s per board; 15 boards sequentially is roughly 3–8 minutes.
- No WiFi module is needed, which removes the jamming/AP risk and saves ~€10–15 per board.

**Cost (15 nodes):** ~€1,420 for 15 bundles at €94.69 (Octopus Pro V5.0 incl. 8 × TMC2209 each), plus motors, PSU and CAN harness. That is below the WiFi-based Octopus path.

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
