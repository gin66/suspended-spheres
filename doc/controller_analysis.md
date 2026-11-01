# Controller Analysis — Hanging Balls

> Detailed evaluation of microcontroller platforms for the 100–120-ball kinetic sculpture.
>
> **Operating point updated.** The piece now uses 100–120 hollow chrome-finish
> spheres (Ø 60–80 mm, ≤ 50 g) over ~2 m of travel at very slow speeds
> (≤ 0.3 m/s, `ASM-015`). Figures below that still refer to the earlier
> 4.5 m/s free-fall concept are marked as such; the platform/bus conclusions
> (local motion generation, modular grouping) remain valid.

---

## 1. Requirements Summary

Before evaluating controllers, we define the hard requirements:

| Requirement | Value |
|-------------|-------|
| Motors per controller | design choice (12-motor grouping as baseline) |
| Communication | modular field bus (CAN 2.0B as baseline) |
| Max speed | 0.3 m/s (`ASM-015`) |
| Max acceleration | ~0.5 m/s² (`ASM-015`) |
| Synchronization precision | < 1 ms across all 100–120 balls |
| Position reference | exactly one endstop (`FR-MOT-009`) |
| Real-time motion control | Yes — deterministic timing, local trajectories |

---

## 2. Candidate Platforms

### 2.1 ESP32-S3 (classic CAN; CAN-FD needs an external controller)

| Specification | Value |
|--------------|-------|
| **CPU** | Dual-core Xtensa LX7, 240 MHz |
| **Flash** | 8 MB+ (QSPI module) |
| **SRAM** | 512 KB (internal) + up to 16 MB PSRAM |
| **CAN** | TWAI controller: **classic CAN 2.0B only, no CAN-FD**; no on-chip transceiver |
| **GPIO** | ~45 usable pins |
| **PWM** | 8 LEDC channels |
| **I2S** | 2 I2S peripherals |
| **SPI** | 2 SPI peripherals |
| **I2C** | 2 I2C peripherals |
| **ADC** | 2× 12-bit SAR ADC |
| **Connectivity** | WiFi 4, Bluetooth 5 (LE) |
| **Logic level** | 3.3 V |
| **Package** | QFN-56 (usually supplied as a module) |
| **Cost** | ~$3–5 per module |

#### Stepper Control Architecture

**Direct PWM approach (6–8 channels):**
- Use LEDC PWM to generate step pulses for 6–8 steppers directly
- Each channel handles one stepper's STEP signal
- DIR and ENABLE lines are GPIO

**I2S shift-register approach:**
- I2S outputs a serial frame that a shift register on the driver board demuxes into per-driver step/dir/enable bits
- The timing study in §8 sizes one such frame; it does not imply a four-channel-per-I2S limit

#### CAN Bus

- The ESP32-S3 TWAI controller speaks **classic CAN 2.0B**, up to 1 Mbit/s. It is not a CAN-FD controller; CAN-FD frames are bus errors.
- There is no on-chip transceiver: an external CAN transceiver is required.
- CAN is **half-duplex**.
- For CAN-FD on an S3, add an **MCP2518FD** (SPI) plus a transceiver. That pair, not the S3 itself, provides FD.

#### Pros
- ✅ High CPU headroom for local motion planning and pattern generation
- ✅ Plenty of GPIO, PSRAM for trajectory buffers, WiFi/BT for configuration and monitoring
- ✅ Large community and ecosystem
- ✅ Classic CAN is sufficient with local trajectories (§9.5.8)

#### Cons
- ⚠️ No CAN-FD without an external MCP2518FD
- ⚠️ No on-chip CAN transceiver
- ⚠️ Higher power consumption (~200–500 mA active)
- ⚠️ Heat generation at sustained high CPU load

#### Verdict: **Strong choice** for a classic-CAN column controller. Add an MCP2518FD only if CAN-FD is actually wanted.

---

### 2.1a ESP32-C5 (on-chip CAN-FD)

| Specification | Value |
|--------------|-------|
| **CPU** | Single HP RISC-V core, up to 240 MHz |
| **SRAM** | 384 KB HP SRAM |
| **CAN** | **Two TWAI controllers, classic CAN and CAN-FD (ISO 11898-1)** |
| **GPIO** | 29 |
| **Connectivity** | WiFi 6 (2.4 and 5 GHz) |
| **Logic level** | 3.3 V |
| **Transceiver** | Still external |

The C5 is the on-chip CAN-FD option. It removes the MCP2518FD, not the transceiver. Its single core and smaller GPIO count are the trade-off against the S3.

#### Verdict: **Preferred when CAN-FD must be on-chip.** For classic CAN, the ESP32-S3 is the more flexible part.

---

### 2.2 Raspberry Pi Pico 2 (RP2350)

| Specification | Value |
|--------------|-------|
| **CPU** | Dual ARM Cortex-M33, 150 MHz |
| **Flash** | 2 MB (QSPI, on-board) |
| **SRAM** | **520 KB** |
| **CAN** | ❌ **No native CAN controller** — needs an external controller **and** a transceiver |
| **GPIO** | 30 (QFN-60) or 48 (QFN-80) |
| **PIO** | **3 PIO blocks × 4 state machines = 12**; each block has 32 instructions, shared |
| **USB** | USB 1.1 full-speed Device/Host |
| **SPI** | 2× SPI controllers |
| **I2C** | 2× I2C controllers |
| **ADC** | 12-bit, 4 channels (QFN-60) or 8 (QFN-80) |
| **Package** | QFN-60 (30 GPIO) or QFN-80 (48 GPIO) |
| **Cost** | ~$3–4 per chip |

#### Stepper Control via PIO

- **One PIO state machine can produce the step stream for one motor.** DIR and ENABLE are ordinary GPIO.
- With 12 state machines, 12 step streams fit directly; no three-machines-per-stepper scheme is needed.
- Each PIO block shares 32 instructions across its four state machines, so plan the program footprint.

```
  RP2350 PIO State Machines
    │
    ├─ SM0: Step stream ──► Stepper 1
    ├─ SM1: Step stream ──► Stepper 2
    ├─ ...
    └─ SM11: Step stream ──► Stepper 12
       (DIR / ENABLE on GPIO)
```

#### CAN Bus — The Problem

The RP2350 has **no built-in CAN controller**. Options:

| Option | Description | Cost/Complexity |
|--------|-------------|-----------------|
| **MCP2518FD** | SPI CAN-FD **controller** (currently preferred) | ~$2, plus SPI |
| **MCP2517FD** | Older SPI CAN-FD controller | ~$2 |
| **MCP2515** | SPI classic CAN 2.0B controller (no FD) | ~$1 |
| **SJA1000** | Parallel-interface CAN **controller**, not a transceiver | ~$3, more pins |
| **I2C ring bus** | Alternative to CAN entirely | Novel, unproven at scale |

Each choice still needs an external CAN **transceiver**.

#### I2C Ring Bus Alternative

If CAN is unacceptable, an I2C ring bus could work:

```
  Master (Pico 2)
      │
      ├─ SDA/SCL ──► Controller 1
      │                └─ SDA/SCL ──► Controller 2
      │                                 └─ SDA/SCL ──► Controller 3
      │                                          ...
      └─ (daisy-chained I2C)
```

- Each controller has an I2C slave address
- Master polls controllers for status
- Controllers report motor positions/speeds
- **Limitation:** I2C max 400 kHz (standard) or 3.4 MHz (fast-mode plus)
- **Latency concern:** 120 motors on a slow I2C ring may be too slow for deterministic multi-axis control

#### Pros
- ✅ PIO allows deterministic, hardware-level step pulse generation
- ✅ Low power consumption (~50–100 mA active)
- ✅ 12 state machines cover 12 motors directly
- ✅ Dual cores allow separation of communication and motion control

#### Cons
- ❌ **No native CAN controller** — needs a controller plus a transceiver
- ❌ No CAN-FD without an external controller
- ❌ Lower CPU (150 MHz vs 240 MHz)
- ❌ No WiFi/Bluetooth
- ❌ I2C ring bus alternative is unproven at 120-motor scale

#### Verdict: **Viable alternative** if an external CAN controller and transceiver are acceptable. PIO is the best step-pulse tool; the CAN gap is a controller plus a transceiver, not a missing crystal.

---

### 2.3 ATmega328P + MCP2515 (Not Recommended)

| Specification | Value |
|--------------|-------|
| **CPU** | 8-bit AVR, 20 MHz |
| **Flash** | 32 KB |
| **SRAM** | 2 KB |
| **EEPROM** | 1 KB |
| **CAN** | ❌ No native — external MCP2515 (SPI) |
| **GPIO** | 23 usable pins |
| **Timers** | 3 (8/16-bit) |
| **ADC** | 6× 10-bit |
| **UART** | 1 |
| **SPI** | 1 |
| **I2C (TWI)** | 1 |
| **Cost** | ~$1 (chip) + ~$2 (MCP2515 module) |

#### Stepper Control

- Bit-banged step pulses via GPIO
- Max practical stepper channels: 2–4 per chip
- No hardware step pulse generation
- Interrupt-driven control is slow at 20 MHz

#### CAN Bus

- MCP2515: SPI-connected, max 10 MHz SPI, CAN 2.0B only (no FD)
- Software bit-timing, no hardware FIFO
- Limited throughput for 12 motors

#### Pros
- ✅ Very cheap
- ✅ Simple, well-understood
- ✅ Good for single-ball proof-of-concept

#### Cons
- ❌ **Too slow** for deterministic motion (less critical at 0.3 m/s, but still not recommended)
- ❌ No CAN-FD
- ❌ Only 2 KB SRAM — insufficient for trajectory buffers
- ❌ 8-bit CPU — poor floating-point performance
- ❌ Limited I/O for 12 motors
- ❌ No real-time guarantees for synchronized multi-ball moves

#### Verdict: ❌ **Not recommended for production.** Acceptable for a single-ball proof-of-concept only.

---

## 3. Comparative Summary

| Feature | ESP32-S3 | ESP32-C5 | Pico 2 (RP2350) | ATmega328P + MCP2515 |
|---------|----------|----------|-----------------|---------------------|
| **CPU** | 240 MHz dual-core | 240 MHz single-core | 150 MHz dual-core | 20 MHz single-core |
| **CAN** | Classic CAN 2.0B on-chip; FD only via MCP2518FD | ✅ **On-chip CAN-FD** | ❌ External controller + transceiver | ❌ External controller (no FD) |
| **Stepper ch.** | 6–8 direct PWM; shift-register fan-out | GPIO/PWM | **12 PIO state machines** | 2–4 bit-banged |
| **Step precision** | Good (PWM) | Good (PWM) | **Excellent (PIO)** | Poor (bit-bang) |
| **SRAM** | 512 KB + PSRAM | 384 KB | **520 KB** | 2 KB |
| **Real-time** | Good | Good | **Best (PIO)** | Poor |
| **Power** | ~300 mA | ~200 mA | ~75 mA | ~30 mA |
| **Connectivity** | WiFi + BT | WiFi 6 | USB only | None |
| **Cost per controller** | ~$5–8 (+ FD controller if used) | ~$5–8 | ~$5–8 + controller + transceiver | ~$3–4 |
| **Suitability for 12 balls** | ✅ Excellent | ✅ Excellent | ✅ Good | ❌ Poor |
| **Suitability for 120 balls** | ✅ Excellent | ✅ Excellent | ✅ Good (w/ CAN) | ❌ Not viable |

---

## 4. Recommendation Matrix

### If CAN-FD is required:
| Priority | Choice |
|----------|--------|
| **On-chip CAN-FD** | **ESP32-C5** (still needs a transceiver) |
| **S3 ecosystem with CAN-FD** | ESP32-S3 + **MCP2518FD** + transceiver |
| Best step precision | Pico 2 + MCP2518FD + transceiver |

### If classic CAN is sufficient:
| Priority | Choice |
|----------|--------|
| **Best overall** | **ESP32-S3** (TWAI classic CAN, I2S/shift-register fan-out, WiFi) |
| Lowest power | Pico 2 |
| Cheapest PoC | ATmega328P + MCP2515 (single ball only) |

### Final Recommendation: **ESP32-S3 for classic CAN; ESP32-C5 if CAN-FD must be on-chip**

**Rationale:**
1. With local trajectories on the column controllers (§9.5.8), classic CAN at 1 Mbit/s is sufficient. CAN-FD is optional headroom.
2. The ESP32-S3's TWAI is classic CAN only. Do not put an S3 TWAI node on a bus that carries FD frames.
3. If CAN-FD is wanted, use the ESP32-C5 (on-chip FD) or an ESP32-S3 plus MCP2518FD. Either way, add a transceiver.
4. WiFi/Bluetooth remains the operator path for configuration, monitoring, and pattern upload.
5. PSRAM allows large trajectory buffers for complex patterns.

**Per-controller BOM (ESP32-S3, classic CAN):**

| Item | Qty | Cost (est.) |
|------|-----|-------------|
| ESP32-S3 module | 1 | $4.00 |
| CAN transceiver | 1 | $1.00 |
| TMC5160 driver | 12 | $60.00 |
| Decoupling caps, resistors | 1 set | $2.00 |
| Connector board | 1 | $3.00 |
| **Total per controller** | | **~$70** |
| **Total for 10 controllers** | | **~$700** |

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
| Max speed 0.3 m/s | Easy | Easy |

**Verdict:** At 0.3 m/s both are easily sufficient. Steppers are simpler and cheaper; BLDC + FOC remains an option for lower noise or if the speed target rises substantially.

---

## 6. Driver IC Comparison

The drivers sit on the column board and are not CAN bus nodes. Only the ESP32 (or an external controller such as the MCP2518FD) is a bus node.

| Driver | Interface | CAN | StallGuard2 | Ramp generator | Notes |
|--------|-----------|-----|-------------|----------------|-------|
| **TMC5160** | SPI + step/dir | ❌ | ✅ | ✅ | **Recommended** — mature, on-driver ramp, high current |
| **TMC2209** | UART or step/dir | ❌ | ✅ | Basic | Cheap, quiet (StealthChop); step/dir path must be verified at the catch step rate |
| **TMC5130** | SPI + step/dir | ❌ | ✅ | ✅ | Lower current; **has a ramp generator** |
| **TMC6200** | SPI | ❌ | ❌ | — | Gate driver, no StallGuard |
| **TMC6300** | 6× PWM | ❌ | ❌ | — | **3-phase BLDC gate driver**, 2–11 V, ~2 A peak. Not a stepper driver and not a bus node |
| **BTS7971B** | — | ❌ | ❌ | — | BLDC, not a stepper |

**Recommendation:** **TMC5160** for production. It provides StallGuard2, an on-driver ramp, and generates the step edges locally. TMC2209 is the cheaper quiet alternative where its step/dir rate is adequate. No driver in this table provides CAN or eliminates the CAN transceiver.

> The earlier recommendation of a "TMC6300 with native CAN-FD" was wrong: the TMC6300 is a low-voltage 3-phase BLDC gate driver with no CAN, no StallGuard2, and no NEMA-17 capability.

---

## 7. Per-Controller Block Diagram (ESP32-S3, classic CAN)

```
  ┌─────────────────────────────────────────────────────┐
  │                  ESP32-S3 Controller                 │
  │                                                     │
  │  ┌──────────┐  ┌──────────┐  ┌──────────────────┐  │
  │  │  TWAI    │  │  I2S     │  │  SPI + GPIO      │  │
  │  │ (classic │  │ (step/   │  │  (TMC5160 config │  │
  │  │  CAN)    │  │  dir mux)│  │   DIR/ENABLE)    │  │
  │  └────┬─────┘  └────┬─────┘  └────────┬─────────┘  │
  │       │             │                  │            │
  │       ▼             ▼                  ▼            │
  │  ┌──────────┐  ┌────────────────────────────────┐  │
  │  │ CAN      │  │  12 × TMC5160                  │  │
  │  │transceiver│ │  (SPI daisy chain + step/dir)  │  │
  │  └────┬─────┘  └───────────────┬────────────────┘  │
  │       │                        │                    │
  │       ▼                        ▼                    │
  │   CAN bus                12 × NEMA-17               │
  │                                                     │
  │  24 V Power ──► DC-DC ──► Motor + Logic Supplies    │
  └─────────────────────────────────────────────────────┘
```

For CAN-FD, replace the transceiver/ESP32-S3 pair with an ESP32-C5 + transceiver, or add an MCP2518FD between the S3 and a CAN-FD transceiver.

---

## 8. Stepping Resolution Analysis: 1/16 µ-Step vs. I2S Mux (~4 µs Resolution)

This section answers a critical design question: **is 1/16 microstepping sufficient**, or do we need finer resolution, given that the I2S multiplexing approach has ~4 µs timing granularity?

### 8.1 The Core Trade-off

| Approach | Timing Resolution | Max µ-Stepping | Linear Resolution (20 mm drum) | Max Step Rate @ 0.3 m/s |
|----------|------------------|----------------|-------------------------------|------------------------|
| **I2S mux** | ~4 µs | 1/16 (practical) | 19.6 µm | 15.3 kHz |
| **PIO (Pico 2)** | ~6.7 ns | 1/32+ | 9.8 µm | 30.6 kHz |
| **Direct GPIO (ESP32)** | ~50–100 ns | 1/32+ | 9.8 µm | 30.6 kHz |

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

**Key finding:** At 1/16 microstepping with a 20 mm drum, linear resolution is **19.6 µm** — well within the 50 µm limit of `ASM-014`. At 1/8 microstepping the same drum gives 39.3 µm, which still meets the requirement; 1/16 is chosen for margin.

### 8.3 Is 19.6 µm Good Enough? (Visual Perception Analysis)

The ball is ~23 mm in diameter. A 19.6 µm step displacement is:

```
19.6 µm / 23,000 µm = 0.085% of ball diameter
```

`ASM-014` sets the perceptual limit at 50 µm (below the ~85 µm acuity limit at 0.5 m).

```
19.6 µm < 50 µm
```

**Conclusion:** 1/16 microstepping resolution is **visually imperceptible** — the step transitions appear perfectly smooth. 1/8 (39.3 µm) also meets `ASM-014`; finer microstepping (1/32, 1/256) provides no visual benefit at this scale.

### 8.4 Timing Analysis at Maximum Speed (0.3 m/s)

**Step rate required at max speed:**
```
Steps/sec = velocity / linear_resolution_per_step
```

| Microstepping | 10 mm Drum | 20 mm Drum | 30 mm Drum |
|--------------|-----------|-----------|-----------|
| Full Step | 1,910 Hz | 955 Hz | 637 Hz |
| 1/4 µ-Step | 7,639 Hz | 3,820 Hz | 2,546 Hz |
| 1/8 µ-Step | 15,279 Hz | 7,639 Hz | 5,093 Hz |
| **1/16 µ-Step** | **30,558 Hz** | **15,279 Hz** | **10,186 Hz** |
| 1/32 µ-Step | 61,115 Hz | 30,558 Hz | 20,372 Hz |

**At 1/16 µ-Step with 20 mm drum @ 0.3 m/s:**
- Step frequency: **15,279 steps/sec**
- Time per step: **65.4 µs**

### 8.5 I2S Mux Timing Budget

The I2S peripheral on ESP32-S3 runs at up to **80 MHz**.

```
I2S clock = 80 MHz
Bit time = 12.5 ns
```

For stepper step pulses sent via I2S multiplexing:

```
I2S mux frame structure (per stepper channel):
  ┌────────────────────────────────────────┐
  │ 1 bit: step pulse high  │ 1 bit: low    │
  │ 1 bit: dir bit          │ 1 bit: enable │
  │ padding / guard bits    │              │
  └────────────────────────────────────────┘
  ~8–16 bits per stepper per frame
  12 steppers → ~96–192 bits per frame
```

**Time per I2S frame (12 steppers, 16 bits each = 192 bits):**
```
Frame time = 192 bits / 80 MHz = 2.4 µs
```

This means the I2S mux can update all 12 steppers every **~2.4 µs**, which translates to a **maximum step pulse frequency of ~416 kHz per motor** (if every frame contains a step pulse).

**At 1/16 µ-Step, 20 mm drum, 0.3 m/s:**
- Required: 15,279 Hz (65.4 µs per step)
- I2S mux capability: ~416,000 Hz (2.4 µs per frame)
- **Margin: 27×** — ✅ very comfortable

### 8.6 What About 1/32 Microstepping via I2S Mux?

```
Required step rate (1/32, 20 mm drum, 0.3 m/s): 30,558 Hz
Time per step: 32.7 µs
I2S mux frame time: 2.4 µs
```

**Margin: 13.6×** — ✅ **I2S mux can sustain 1/32 µ-Step at 0.3 m/s**.

The I2S mux would need to run faster or use a wider frame, but this would consume more CPU bandwidth and introduce jitter.

### 8.7 PIO (Pico 2) Comparison

The Pico 2's PIO runs at the CPU clock (150 MHz):
```
PIO instruction cycle = 1 / 150 MHz = 6.67 ns
```

A single step pulse can be generated in **2–3 PIO instructions**:
```
Minimum step pulse width = 2 × 6.67 ns = 13.3 ns
Maximum step frequency = 1 / 13.3 ns = 75 MHz (theoretical)
Practical max (with overhead): ~5–10 MHz
```

**At 1/32 µ-Step, 20 mm drum, 0.3 m/s:**
- Required: 30,558 Hz (32.7 µs per step)
- PIO capability: ~5,000,000 Hz (conservative)
- **Margin: ~164×** — ✅ far more than sufficient

### 8.8 Summary: Resolution Recommendation

| Factor | 1/16 µ-Step | 1/32 µ-Step |
|--------|------------|------------|
| **Visual smoothness** | ✅ Imperceptible steps | ✅ (no visible improvement) |
| **Holding torque** | Good | ~2× better (lower ripple) |
| **Resonance damping** | Acceptable | Better (finer control) |
| **I2S mux @ 0.3 m/s** | ✅ 27× margin | ✅ 13.6× margin |
| **PIO @ 0.3 m/s** | ✅ 327× margin | ✅ 164× margin |
| **CPU load** | Lower | ~2× higher |
| **Bus load** | No difference | No difference (steps are local, not on CAN) |

### 8.9 Final Recommendation

**1/16 microstepping is the optimal choice for the I2S multiplexing approach:**

1. **Visually indistinguishable** from finer stepping — ball motion appears perfectly smooth
2. **Fits within I2S mux timing** with 27× margin at max speed (0.3 m/s)
3. **Sufficient holding torque** for 50 g ball + acceleration forces
4. **No CAN-bus penalty** — step generation is local; microstepping does not change CAN traffic
5. **Acceptable resonance** — stepper motors at 1/16 have manageable resonance peaks

At the slow operating speed, 1/32 is also feasible on the I2S mux. It remains optional; 1/16 already meets the 50 µm limit with margin.

For the **Pico 2 with PIO**, 1/32 microstepping is feasible and provides slightly smoother low-speed motion, but the visual benefit is marginal. **1/16 remains the recommended default** for both platforms; once the ramp is local, microstepping does not load the CAN bus.

---

## 9. CAN vs WiFi — Is CAN Needed When Using ESP32 with WiFi?

This is a critical architectural question. The ESP32-S3 has WiFi and classic CAN (CAN-FD needs an external controller). Since WiFi is available, **can we skip CAN entirely** and use WiFi (or Ethernet) for controller-to-master communication?

### 9.1 The Short Answer

**Yes, CAN is still strongly recommended** — even (and especially) when the ESP32 has WiFi. Here's why:

| Requirement | CAN | WiFi | Ethernet |
|-------------|-----|------|----------|
| **Deterministic latency** | ✅ < 1 ms (bus arbitration) | ❌ 10–100+ ms (CSMA/CA, retries) | ⚠️ ~1–5 ms (switched) |
| **Real-time priority** | ✅ Hardware arbitration | ❌ No priority queuing | ⚠️ Limited (QoS) |
| **Collision handling** | ✅ Bitwise arbitration (deterministic) | ❌ Exponential backoff (non-deterministic) | ⚠️ Store-and-forward |
| **Message loss tolerance** | ✅ Retransmission in < 1 ms | ❌ TCP retransmission: 10–500 ms | ⚠️ Variable |
| **Broadcast efficiency** | ✅ 120 messages, ~1 ms total | ❌ 120 messages, seconds of airtime | ⚠️ Switch flooding |
| **Power consumption** | ✅ ~5 mA (idle), ~30 mA (active) | ❌ ~80–200 mA (WiFi active) | ⚠️ ~100 mA (Ethernet PHY) |
| **EMI / RF interference** | ✅ Shielded twisted pair | ❌ 2.4 GHz crowded spectrum | ✅ Shielded cable |
| **Single point of failure** | ✅ Bus survives partial failure | ❌ AP failure = total loss | ⚠️ Switch failure = total loss |
| **Installation complexity** | ✅ Simple twisted pair | ❌ WiFi coverage, channels, interference | ❌ Switch + Cat5 cabling |
| **Cost** | ✅ ~$0.50 (transceiver) | ✅ Built-in (free) | ⚠️ Switch + cables |

### 9.2 Detailed Analysis: Why CAN Wins for This Application

#### 9.2.1 Real-Time Determinism

**CAN bus (1 Mbit/s):**
```
Worst-case message delivery:
  - Arbitration: < 1 ms (bitwise, hardware)
  - Propagation: < 10 µs (twisted pair, ~200 ns/m)
  - Total worst-case: < 1.1 ms
  - 120 messages on bus: ~120 ms total (sequential)
  - With CAN-FD (8 Mbit/s data phase): ~15 ms total
```

**WiFi (802.11n/g, 2.4 GHz):**
```
Worst-case message delivery:
  - CSMA/CA contention: 0–100+ ms (random backoff)
  - Retransmission (packet loss): 10–500 ms
  - Interference (microwave, Bluetooth, neighbors): variable
  - Total worst-case: 50–500+ ms (highly variable)
  - 120 messages: seconds of airtime (shared medium)
  - Channel congestion in urban environments: severe
```

> **Key point:** CAN provides **deterministic** latency (< 1.1 ms worst case). WiFi provides **stochastic** latency (10–500+ ms, highly variable). For synchronized motion control of 120 balls, deterministic timing is **essential**.

#### 9.2.2 Synchronization Precision

The system requires < 1 ms synchronization across all 120 balls.

| Metric | CAN (1 Mbit/s) | CAN-FD (1 M/8 Mbit/s) | WiFi 802.11n | Ethernet (100 Mbps) |
|--------|---------------|----------------------|---------------|-------------------|
| Worst-case latency | < 1.1 ms | < 0.5 ms | 10–500+ ms | 1–5 ms |
| Jitter | < 100 µs | < 50 µs | 5–100 ms | 0.5–2 ms |
| Sync accuracy | < 50 µs | < 20 µs | 1–10 ms | 0.1–1 ms |
| Deterministic? | ✅ Yes | ✅ Yes | ❌ No | ⚠️ Mostly |

> **WiFi cannot achieve < 1 ms synchronization reliably.** The CSMA/CA protocol, channel contention, and retransmission make WiFi fundamentally non-deterministic.

#### 9.2.3 Reliability in Real-World Conditions

**CAN bus advantages:**
- Shielded twisted pair — immune to RF interference
- No channel congestion (unlike 2.4 GHz WiFi)
- Built-in error detection and retransmission
- Works in electrically noisy environments (motors, power supplies)
- Point-to-point wiring — no infrastructure (AP, switch) required

**WiFi disadvantages in this application:**
- 2.4 GHz is **crowded** (WiFi, Bluetooth, microwaves, Zigbee)
- Line-of-sight not required but **multipath fading** causes signal drops
- **Interference from motors** (EMI) can disrupt WiFi packets
- **Density problem:** 10 controllers plus 10 operator devices on one 2.4 GHz channel; if every ball carried WiFi it would be 120 nodes and severe congestion
- **AP/single-point failure:** If the access point fails, the entire system is dead
- **Range limitations:** 1200–1800 mm tall sculpture may have dead zones
- **Regulatory:** 2.4 GHz power limits vary by region

#### 9.2.4 Power Consumption

| Mode | CAN (transceiver) | WiFi (ESP32-S3) | Ethernet (LAN8720) |
|------|----------------------|-----------------|-------------------|
| Idle | ~5 mA | ~80 mA (beaconing) | ~15 mA |
| Active tx/rx | ~30 mA | ~200 mA (tx burst) | ~30 mA |
| Per 10 controllers | **0.3 W** | **2 W** | **0.3 W** |

> WiFi consumes **6–8× more power** than CAN for the same communication load. The machine has **10 controllers**, not 120; the power figures here are per 10 controllers.

#### 9.2.5 System Architecture Comparison

**With CAN (recommended):**
```
  Master (PC/ESP32)
      │
      └─ CAN bus (twisted pair, 120 Ω termination)
          │
          ├─ Controller 1 (ESP32-S3 + transceiver)
          ├─ Controller 2 (ESP32-S3 + transceiver)
          └─ ... Controller 10
```
- Simple twisted pair wiring
- No infrastructure required
- Deterministic, real-time
- Low power
- Robust in noisy environments

**Without CAN, WiFi only:**
```
  WiFi Access Point (or Master with WiFi)
      │
      └─ 2.4 GHz wireless (shared medium)
          │
          ├─ Controller 1 (ESP32-S3, WiFi)
          ├─ Controller 2 (ESP32-S3, WiFi)
          └─ ... Controller 10
```
- Requires AP or master with WiFi
- Channel congestion at 120 devices
- Non-deterministic latency
- High power consumption
- EMI from motors degrades signal
- Single point of failure (AP)

### 9.3 When WiFi CAN Be Used (Limited Cases)

WiFi is **acceptable** for:
- **Configuration/monitoring** (non-real-time): uploading patterns, reading status
- **Infrequent commands** (pattern changes, parameter adjustments)
- **Diagnosis and logging** (error reports, health checks)

WiFi is **NOT acceptable** for:
- **Real-time motion commands** (position updates at 100+ Hz)
- **Global synchronization** (< 1 ms precision)
- **Emergency stop** (must be deterministic, < 10 ms)
- **Per-ball position feedback** (120 balls × 100 Hz = 12 kHz real-time)

### 9.4 Recommended Hybrid Architecture

**Use CAN for real-time control, WiFi for configuration/monitoring:**

```
  ┌─────────────────────────────────────────────────┐
  │                    Master                        │
  │  ┌─────────────┐  ┌──────────────────────────┐  │
  │  │  CAN-FD     │  │  WiFi (AP / Station)     │  │
  │  │  (real-time)│  │  (config / monitoring)   │  │
  │  └──────┬──────┘  └──────────────────────────┘  │
  └─────────┼───────────────────────────────────────┐
            │ CAN bus (1 Mbit/s)                    │
            │                                       │
  ┌─────────┼───────────────────────────────────────┐
  │         ▼                                       │
  │  ┌───────────────────────────────────────────┐  │
  │  │  Controller (ESP32-S3)                    │  │
  │  │  ┌───────────┐  ┌───────────────────────┐ │  │
  │  │  │ CAN       │  │  WiFi (AP/Station)    │ │  │
  │  │  │ (real-    │  │  (config / monitoring)│ │  │
  │  │  │  time)    │  │                       │ │  │
  │  │  └────┬──────┘  └───────────────────────┘ │  │
  │  │       ▼                                   │  │
  │  │  12 × TMC5160 → 12 × NEMA-17 motors       │  │
  │  └───────────────────────────────────────────┘  │
  └─────────────────────────────────────────────────┘
```

| Channel | Purpose | Protocol | Latency Requirement |
|---------|---------|----------|-------------------|
| **CAN bus** | Real-time motion commands, status, sync, E-stop | CAN 2.0B @ 1 Mbit/s (FD optional) | < 1 ms |
| **WiFi** | Pattern upload, parameter adjustment, diagnostics, logging | TCP/UDP | < 100 ms |

### 9.5 CAN Bus Traffic Analysis — Corrected

This section was previously wrong. Here is the **corrected analysis**.

#### 9.5.1 The Real Numbers

**System:** 120 motors, 1 kHz control rate (kinematic position updates)

**Two architectures:**

| Architecture | Messages/Cycle | Bytes/Cycle | Messages/Sec (1 kHz) |
|-------------|---------------|-------------|---------------------|
| **Per-motor** (120 messages) | 120 | 120 × 8 = 960 B | 120,000 msg/s |
| **Per-controller batch** (10 messages) | 10 | 10 × 96 = 960 B | 10,000 msg/s |

Total data payload is the same (960 bytes/cycle), but **CAN frame overhead** differs by 12× in message count.

#### 9.5.2 Classic CAN (8-byte payload) at 1 Mbit/s

Each classic CAN frame: ~108 bits (11-bit ID + arbitration + control + data + CRC + ACK + inter-frame bits)

| Architecture | Bits/Cycle | Bits/Sec (1 kHz) | Bus Utilization @ 1 Mbit/s |
|-------------|-----------|-----------------|-------------------------|
| **Per-motor** (120 frames) | 120 × 108 = 12,960 | 12,960,000 | **1296% — IMPOSSIBLE** |
| **Per-controller batch** (10 frames) | 10 × 108 = 1,080 | 1,080,000 | **108% — STILL TOO HIGH** |

> **Classic CAN at 1 Mbit/s CANNOT handle 1 kHz per-motor updates for 120 motors.**
> Even batch messages (10 controllers) exceed 1 Mbit/s at 1 kHz.

**What rate works on classic CAN (1 Mbit/s)?**
```
Max sustainable rate (at 80% utilization = 800 kbit/s):
  1080 bits/cycle × f_Hz × 0.8 = 800,000
  f_Hz = 800,000 / 1080 = 741 Hz (batch messages)
  
Per-motor messages:
  12,960 bits/cycle × f_Hz × 0.8 = 800,000
  f_Hz = 800,000 / 12,960 = 61.7 Hz (per-motor)
```

| Mode | Max Rate @ 1 Mbit/s (Classic CAN) |
|------|----------------------------------|
| Per-motor (120 messages) | **~60 Hz** |
| Per-controller batch (10 messages) | **~740 Hz** |

#### 9.5.3 CAN-FD (Flexible Data Rate) at 1 Mbit/s (base) / 8 Mbit/s (data phase)

CAN-FD frames with 64-byte payload: ~300 bits (including arbitration at 1 Mbit/s base rate)

| Architecture | Bits/Cycle | Bits/Sec (1 kHz) | Bus Utilization @ 1 Mbit/s | Bus Utilization @ 8 Mbit/s (data phase) |
|-------------|-----------|-----------------|-------------------------|--------------------------------------|
| **Per-motor** (120 frames) | 120 × 300 = 36,000 | 36,000,000 | 3600% — IMPOSSIBLE | 450% — STILL TOO HIGH |
| **Per-controller batch** (10 frames) | 10 × 300 = 3,000 | 3,000,000 | 300% — TOO HIGH | 37.5% — OK! |
| **Per-controller batch** (10 frames, 100 Hz) | 3,000 | 300,000 | 30% — OK | 3.75% — trivially OK |

> **Key finding:** CAN-FD with 8 Mbit/s data phase CAN handle 1 kHz per-motor updates **only with batch messages** (10 controllers, not 120). Per-motor CAN-FD messages at 1 kHz still exceed 8 Mbit/s.

#### 9.5.4 Corrected Recommendation: Batch Messages Required

**If full positions are streamed, per-controller batch messages (one message per controller, containing all 12 motor positions) are REQUIRED for real-time control at 1 kHz.** Local trajectory generation (§9.5.8) removes this requirement.

| Configuration | Classic CAN @ 1 Mbit/s | CAN-FD @ 1/8 Mbit/s |
|--------------|----------------------|---------------------|
| Per-motor, 1 kHz | ❌ 1296% (impossible) | ❌ 450% (impossible) |
| Per-motor, 100 Hz | ❌ 129.6% (too high) | ❌ 45% (too high) |
| Per-motor, 60 Hz | ❌ 77.8% (barely OK) | ❌ 27% (barely OK) |
| **Batch (10 msgs), 1 kHz** | ❌ 108% (too high) | ✅ 37.5% (OK!) |
| **Batch (10 msgs), 100 Hz** | ✅ 10.8% (plenty) | ✅ 3.75% (trivial) |
| **Batch (10 msgs), 500 Hz** | ❌ 54% (OK) | ✅ 18.75% (plenty) |

#### 9.5.5 CAN Bus Protocol Design — Batch Messages

Each controller sends **one batch message per cycle** containing all 12 motor positions:

```
CAN-FD Frame (64-byte payload, data phase @ 8 Mbit/s):
┌─────────────────────────────────────────────────────┐
│ 11-bit ID: Controller # (0-9)                        │
│ DLC: 64 bytes (data phase)                           │
│ Data:                                              │
│   Motor 0: position (32-bit int), velocity (16-bit)  │
│   Motor 1: position (32-bit int), velocity (16-bit)  │
│   ...                                              │
│   Motor 11: position (32-bit int), velocity (16-bit) │
│   Flags: status, error, endstop (32-bit)             │
└─────────────────────────────────────────────────────┘
Payload: 12 × (4 + 2) + 4 = 76 bytes → fits in one 64-byte frame? No.
  → Split into 2 CAN-FD frames per controller (64 + 12 bytes)
```

**Revised frame structure (2 frames per controller):**

| Frame | Payload | Content |
|-------|---------|--------|
| Frame 1 (64 bytes) | 64 | 10 motors × (4 B pos + 2 B vel) + 4 B flags = 76 B → split: 64 B + 12 B |
| Frame 2 (12 bytes) | 12 | Remaining 2 motors × 6 B = 12 B |

**Total bus load (batch, 1 kHz, 2 frames/controller):**
- 20 CAN-FD frames/cycle × 300 bits = 6,000 bits/cycle
- At 1 kHz: 6,000,000 bits/s = 6 Mbit/s
- At 8 Mbit/s data phase: **75% utilization** — acceptable but tight
- At 500 Hz: **37.5% utilization** — comfortable
- At 200 Hz: **15% utilization** — plenty

#### 9.5.6 Mixed Controller Architecture (4/8/12 per controller)

If controllers with different motor counts (4, 8, 12) are mixed on the same bus:

| Configuration | Controllers | Frames/Cycle (2 per ctrl) | Bus Load @ 1 kHz (8 Mbit/s) | Bus Load @ 500 Hz (8 Mbit/s) |
|--------------|------------|-------------------------|---------------------------|---------------------------|
| All 12-motor | 10 | 20 | 75% | 37.5% |
| All 8-motor | 15 | 30 | 112.5% | 56.25% |
| All 4-motor | 30 | 60 | 225% | 112.5% |
| Mixed (avg 8) | 15 | 30 | 112.5% | 56.25% |
| Mixed (avg 12) | 10 | 20 | 75% | 37.5% |

> **Key insight:** Mixed controllers with 4 motors each (30 controllers) make 1 kHz **impossible** even on CAN-FD at 8 Mbit/s (225% utilization). The system must either:
> 1. Use controllers with 12 motors each (10 controllers → 75% at 1 kHz)
> 2. Reduce control rate to 500 Hz (37.5% at 12-motor controllers)
> 3. Use CAN-FD with higher data phase bitrate (e.g., 12 Mbit/s)

#### 9.5.7 Practical Recommendation

| Parameter | Value |
|-----------|-------|
| Controller grouping | **12 motors per controller** (10 controllers total) |
| Control rate | **500 Hz** (ample for ≤ 0.3 m/s motion) |
| Bus protocol | **CAN-FD** if streaming positions (1 Mbit/s base / 8 Mbit/s data phase); **classic CAN 2.0B** if trajectories are local (§9.5.8) |
| Messages per cycle | 20 (2 frames × 10 controllers) |
| Bus utilization @ 500 Hz | **37.5%** (comfortable margin) |
| Messages per cycle (status, 10 Hz) | 10 (1 frame × 10 controllers) |
| Status bus utilization | **< 1%** (negligible) |
| Total bus utilization (500 Hz + 10 Hz) | **~38%** |

> **Conclusion:** If the master streams full positions, CAN-FD with an 8 Mbit/s data phase is needed, and 12-motor controllers (not 4-motor) keep the load manageable. If the controllers run local trajectories (§9.5.8), bus use falls below ~12 % and classic CAN is sufficient; CAN-FD becomes optional headroom. The architecture choice, not the bus, is what makes the traffic close.

#### 9.5.8 Bandwidth Reduction via On-Controller Ramp Generation

The previous analysis assumed the master sends **full position/velocity for every motor every cycle**. This is wasteful. If each controller has its own **motion planner** (ramp / S-curve generator), the master only sends **motion parameters** infrequently, and the controller interpolates locally at high rate.

**Key insight:** The master sends a new motion command only when the ball's trajectory changes (e.g., new target position). Between commands, the controller generates the ramp/S-curve locally at 1 kHz — **zero CAN traffic** during motion execution.

**Scenario comparison:**

| Scenario | Master sends | Controller does | CAN traffic |
|----------|-------------|----------------|-------------|
| **Naive** (previous analysis) | Full position/velocity 120×/cycle | Just set position | 37.5% @ 1 kHz |
| **With ramp gen** | Motion params when target changes | Interpolate at 1 kHz | **~2%** (see below) |

**How it works:**

```
Master (pattern engine):
  1. Computes trajectory for a ball (or group)
  2. Sends ONE motion command:
     { motor_id, target_position, velocity_profile_params }
  3. Controller stores command in its motion queue
  4. Controller's internal planner generates position/velocity
     at 1 kHz — NO CAN traffic needed
  5. New command sent only when trajectory changes
```

**Typical pattern: traveling sine wave across 120 balls**

```
Phase 1 — Command distribution (brief burst):
  Master sends 120 motion commands sequentially:
    CMD_MOVE { motor_0, pos=100mm, vel_profile=S-curve }
    CMD_MOVE { motor_1, pos=105mm, vel_profile=S-curve }
    ...
    CMD_MOVE { motor_119, pos=95mm, vel_profile=S-curve }
  Duration: 120 commands × (300 bits/frame / 8 Mbit/s) = 4.5 ms
  (2 frames per controller × 10 controllers = 20 frames)
  
Phase 2 — Execution (no CAN traffic):
  Each controller's internal planner interpolates at 1 kHz
  CAN bus is IDLE — 0% utilization
  Duration: depends on pattern (e.g., 2 seconds for one wave cycle)
  
Phase 3 — Next command (brief burst):
  When pattern changes, master re-sends updated targets
  Same 4.5 ms burst
```

**Bus utilization by pattern type:**

| Pattern | Command Burst Duration | Cycle Duration | Duty Cycle | Bus Utilization @ 8 Mbit/s |
|---------|----------------------|---------------|-----------|-------------------------|
| **Static hold** (balls at rest) | 0 (no commands) | ∞ | 0% | **0%** |
| **Slow breathing** (period 10 s) | 4.5 ms | 10,000 ms | 0.045% | **0.045%** |
| **Sine wave** (period 5 s) | 4.5 ms | 5,000 ms | 0.09% | **0.09%** |
| **Medium wave** (period 2 s) | 4.5 ms | 2,000 ms | 0.225% | **0.225%** |
| **Fast wave** (period 0.5 s) | 4.5 ms | 500 ms | 0.9% | **0.9%** |
| **Rapid cascade** (period 0.2 s) | 4.5 ms | 200 ms | 2.25% | **2.25%** |
| **Maximum aggression** (period 0.1 s) | 4.5 ms | 100 ms | 4.5% | **4.5%** |

> **Key finding:** With on-controller ramp generation, CAN bus utilization is **negligible** (< 5% even for the most aggressive patterns). The CAN bus is essentially idle between command bursts.

**Status feedback traffic (controller → master):**

Controllers report status at 10 Hz (not 1 kHz — position feedback is not needed in real-time):

```
10 controllers × 10 Hz × 2 frames/controller × 300 bits/frame = 600,000 bits/s
Bus utilization: 600,000 / 8,000,000 = 7.5% (data phase)
```

**Total bus utilization (commands + status):**

| Pattern | Command Burst | Status Feedback | Total |
|---------|-------------|----------------|-------|
| Static hold | 0% | 7.5% | **7.5%** |
| Slow breathing | 0.045% | 7.5% | **7.55%** |
| Fast wave | 0.9% | 7.5% | **8.4%** |
| Maximum aggression | 4.5% | 7.5% | **12%** |

> **Conclusion:** With on-controller ramp generation, CAN bus utilization is **always under 12%** even for the most aggressive patterns. The CAN bus is **massively over-provisioned**. Even classic CAN at 250 kbit/s would suffice for most patterns.

**Critical design requirement:** Each controller MUST have its own motion planner (ramp / S-curve generator). The master sends motion commands, and the controller interpolates locally at high rate (≥ 1 kHz). This reduces CAN traffic by **two orders of magnitude** compared to the naive per-motor position update approach.

### 9.6 Final Verdict

| Question | Answer |
|----------|--------|
| **Can WiFi replace CAN for real-time control?** | ❌ No — non-deterministic latency makes it unsuitable |
| **Can WiFi replace CAN for monitoring/config?** | ✅ Yes — WiFi is excellent for this |
| **Should CAN be kept when using ESP32 with WiFi?** | ✅ **Yes — CAN for real-time, WiFi for config** |
| **What is the minimum viable communication?** | Classic CAN 2.0B at 1 Mbit/s with local trajectories |
| **Is CAN-FD necessary?** | ❌ No. With local trajectories, bus use stays under about 12 %; classic CAN at 250 kbit/s would do for most patterns. CAN-FD is optional headroom. |
| **Must controllers have motion planners?** | **YES — this is critical.** Without on-controller ramp generation, CAN traffic is 37.5% (batch) or 450% (per-motor) at 1 kHz — impossible. With ramp generation, utilization drops to < 12%. |

**Recommendation: Use classic CAN 2.0B as the primary real-time bus at 1 Mbit/s (CAN-FD is optional headroom). Use WiFi exclusively for configuration, monitoring, and pattern upload. Each controller MUST have its own motion planner (ramp / S-curve generator) to minimize CAN traffic. This hybrid approach gives the best of both worlds: deterministic real-time control + convenient wireless configuration.**

**Critical design rule:** The master sends motion **parameters** (target position + profile type), not position samples. The controller's internal motion planner generates the trajectory at ≥ 1 kHz locally. This reduces CAN traffic by 2 orders of magnitude and makes the system feasible.

> The earlier claim that CAN-FD is "necessary" contradicted §9.5.8. The architecture that makes the traffic tables close is local trajectory generation; with it, classic CAN is sufficient and CAN-FD is headroom. Also note the step rate: at 1/16 microstep and a 25 mm drum the ceiling is about 180 000 steps/s per motor, and on the order of two million per column if the column drops together. A TMC5160 generates those edges itself. Step/dir from the MCU (TMC2209, or the I2S stream in §8) has to be shown to sustain that rate before it is the catch path.

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
| 1 hour | 2.5 s | 18 ms |
| 8 hours | 20 s | 144 ms |
| 24 hours | 60 s | 432 ms |

> **Key finding:** With consumer-grade quartz (35 ppm), two controllers can drift **21 ms apart after just 5 minutes** and **2.5 seconds apart after 1 hour**. For synchronized wave patterns requiring sub-millisecond alignment, this is **unacceptable without correction**.
>
> The 70 ppm figure is the worst case: one fast and one slow crystal at 35 ppm each. Two crystals drifting the same way need less. Design for the worst case.

### 10.3 Impact on Synchronized Patterns

**Sub-millisecond synchronization requirement:**
```
Position error from timing drift: Δx = v × Δt
```

| Pattern Speed | Drift @ 5 min (21 ms) | Drift @ 1 hour (2.5 s) |
|--------------|----------------------|------------------------|
| Static (v = 0) | 0 mm | 0 mm |
| Very slow (v = 0.05 m/s) | 1.05 mm | 125 mm |
| Slow wave (v = 0.15 m/s) | 3.2 mm | 375 mm |
| Fastest (v = 0.3 m/s) | 6.3 mm | 750 mm |

> **Critical finding:** Even in the slow regime, controllers drift **6.3 mm after 5 minutes** at the fastest speed and **0.75 m after 1 hour** — larger than the ~300 mm grid pitch and clearly visible in a wave pattern. Clock correction is still required.

### 10.4 Correction Strategy: Pause-Based Clock Alignment

**Concept:** During periods when balls are at rest (common in artistic patterns), controllers use the **idle time** to correct clock drift by inserting small timing adjustments (pauses) into their motion loops.

**How it works:**

```
  Controller A (fast clock)          Controller B (slow clock)
  ───────────────────────          ─────────────────────────
  Time: 0 ms                         Time: 0 ms
  Motion: move to 100mm              Motion: move to 100mm
  (finishes at t=500ms)              (finishes at t=510ms)
  
  Ball at rest: 100mm                Ball at rest: 100mm
  
  ┌─ 2-second rest period ─┐        ┌─ 2-second rest period ─┐
  │                         │        │                         │
  │  Master sends SYNC      │        │  Receives SYNC          │
  │  Controller A:          │        │  Controller B:          │
  │  "You are 10ms ahead"   │        │  "You are 10ms behind"  │
  │  Insert 10ms pause      │        │  Remove 10ms pause      │
  │  (extend rest by 10ms)  │        │  (shorten rest by 10ms) │
  │                         │        │                         │
  └─────────────────────────┘        └─────────────────────────┘
  
  Next motion starts synchronized     Next motion starts synchronized
```

**Algorithm per controller (during rest):**

```
while (ball is at rest):
  1. Receive SYNC message from master (contains global timestamp)
  2. Compare local timer with global time
  3. Compute drift = local_time − global_time
  4. If |drift| > threshold (e.g., 100 µs):
       a. Calculate correction = drift
       b. Spread correction over remaining rest time:
          - If fast: insert small pauses in motion loop
          - If slow: remove small pauses from motion loop
       c. Apply correction gradually (avoid position jumps)
  5. Sleep until next SYNC (e.g., 100 ms interval)
```

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

### 10.8 SYNC Message Protocol for Clock Correction

```
CAN-FD SYNC message (from master to all controllers):
┌─────────────────────────────────────────────┐
│ ID: 0x7FF (global broadcast)                │
│ DLC: 32 bytes (CAN-FD)                      │
│ Data:                                       │
│   Global timestamp (64-bit, µs precision)   │
│   Master's estimated drift per controller   │
│   (10 × 16-bit signed µs offsets)           │
│   Correction flag (per controller)          │
└─────────────────────────────────────────────┘
```

**Update frequency:** SYNC messages every **100 ms** (10 Hz) during rest, every **1 s** during motion.

**Correction threshold:** Apply correction when |drift| > 100 µs (consumer quartz) or > 10 µs (TCXO).

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
After 30 s: 1.05 ms drift → 0.32 mm position error at 0.3 m/s
After 5 min: 10.5 ms drift → 3.2 mm position error at 0.3 m/s
(At the 70 ppm worst case: 21 ms → about 6.3 mm.)
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
