# Controller Analysis — Hanging Balls

> Detailed evaluation of microcontroller platforms for the 120-ball kinetic sculpture.

---

## 1. Requirements Summary

Before evaluating controllers, we define the hard requirements:

| Requirement | Value |
|-------------|-------|
| Motors per controller | 12 (one column) |
| Communication | CAN 2.0B or CAN-FD @ 1 Mbit/s |
| Max per-ball speed | 4.5 m/s |
| Max per-ball acceleration | ~50 m/s² (est.) |
| Synchronization precision | < 1 ms across all 120 balls |
| Endstop detection | StallGuard / current monitoring |
| Real-time motion control | Yes — deterministic timing |

---

## 2. Candidate Platforms

### 2.1 ESP32-S3 (Recommended)

| Specification | Value |
|--------------|-------|
| **CPU** | Dual-core Xtensa LX7, 240 MHz |
| **Flash** | 8 MB+ (QSPI) |
| **SRAM** | 512 KB (internal) + up to 16 MB PSRAM |
| **CAN** | Native CAN 2.0B + **CAN-FD** (built-in) |
| **GPIO** | 40+ usable pins |
| **PWM** | 8 LEDC channels (up to 80 MHz) |
| **I2S** | 2 I2S peripherals (up to 80 MHz) |
| **SPI** | 2 SPI peripherals |
| **I2C** | 2 I2C peripherals |
| **ADC** | 2× 12-bit SAR ADC (18 channels) |
| **Connectivity** | WiFi 4, Bluetooth 5 (LE) |
| **Package** | QFN-104 (20×20 mm) or QFN-64 |
| **Cost** | ~$3–5 per module |

#### Stepper Control Architecture

**Direct PWM approach (6–8 channels):**
- Use LEDC PWM to generate step pulses for 6–8 steppers directly
- Each channel handles one stepper's STEP signal
- DIR lines controlled via GPIO

**I2S multiplexing approach (up to 32 channels):**
- I2S peripheral outputs a serial stream of stepper commands
- Demux on the driver side distributes to individual stepper drivers
- Each I2S peripheral can handle 4 stepper channels
- 2 I2S peripherals → 8 channels per I2S → 16 total
- With two ESP32-S3 chips per controller → 32 channels

```
  ESP32-S3 I2S Stream
    │
    ├─ Step Pulse 1 ──► TMC Driver 1
    ├─ Step Pulse 2 ──► TMC Driver 2
    ├─ Step Pulse 3 ──► TMC Driver 3
    ├─ Step Pulse 4 ──► TMC Driver 4
    └─ (demux on driver PCB)
```

#### CAN Bus

- Native CAN-FD controller in silicon
- Supports classic CAN (up to 1 Mbit/s) and CAN-FD (up to 8 Mbit/s data phase)
- Full-duplex communication
- Hardware error handling and retransmission

#### Pros
- ✅ Native CAN-FD (critical for 120-motor real-time coordination)
- ✅ High CPU headroom for motion planning and pattern generation
- ✅ I2S PWM multiplexing enables 12+ stepper channels
- ✅ WiFi + Bluetooth for remote monitoring/control
- ✅ Large community, extensive libraries (ESP-IDF, Arduino)
- ✅ PSRAM support for large trajectory buffers

#### Cons
- ⚠️ No hardware step pulse generator (uses PWM, slightly less precise than PIO)
- ⚠️ Higher power consumption (~200–500 mA active)
- ⚠️ 5 V logic (requires level shifting for some components)
- ⚠️ Heat generation at sustained high CPU load

#### Verdict: **STRONGLY RECOMMENDED** — best overall fit.

---

### 2.2 Raspberry Pi Pico 2 (RP2350)

| Specification | Value |
|--------------|-------|
| **CPU** | Dual ARM Cortex-M33, 150 MHz |
| **Flash** | 2 MB (QSPI, on-board) |
| **SRAM** | 264 KB (tightly coupled) + 8 KB SRAM0/1 + 4 KB SRAM2/3 |
| **CAN** | ❌ **No native CAN** — requires external transceiver |
| **GPIO** | 30 usable pins |
| **PIO** | 2 state machines per core (4 total), each 4 instructions deep |
| **USB** | USB 1.1 Device/Host/SSEG |
| **SPI** | 2× SPI controllers (4 channels total) |
| **I2C** | 2× I2C controllers (4 channels total) |
| **ADC** | 1× 16-bit SAR ADC (4 channels) |
| **Temperature** | 1× temperature sensor |
| **Package** | QFN-56 (7×7 mm) or QFN-48 |
| **Cost** | ~$3–4 per chip |

#### Stepper Control via PIO

The PIO (Programmable I/O) is the Pico 2's killer feature for stepper control:

- Each state machine can generate precise step pulses independently
- 4 state machines → 4 steppers directly from PIO
- For 12 steppers: 3 PIO state machines per stepper (step, dir, enable)
- Or use 12 GPIO pins with direct register manipulation

```
  RP2350 PIO State Machines
    │
    ├─ SM0: Step pulse gen ──► Stepper 1
    ├─ SM1: Step pulse gen ──► Stepper 2
    ├─ SM2: Step pulse gen ──► Stepper 3
    ├─ SM3: Step pulse gen ──► Stepper 4
    └─ GPIO bit-bang: Steppers 5–12
```

#### CAN Bus — The Problem

The RP2350 has **no built-in CAN controller**. Options:

| Option | Description | Cost/Complexity |
|--------|-------------|-----------------|
| **MCP2517FD** | SPI-connected CAN-FD controller | ~$2, adds SPI bus |
| **MCP2515** | SPI-connected CAN 2.0B (no FD) | ~$1, no FD support |
| **SJA1000** | Parallel interface CAN controller | ~$3, more pins |
| **I2C ring bus** | Alternative to CAN entirely | Novel, unproven at scale |

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
- **Latency concern:** 120 motors on a slow I2C ring may be too slow for 4.5 m/s control

#### Pros
- ✅ PIO allows deterministic, hardware-level step pulse generation
- ✅ Low power consumption (~50–100 mA active)
- ✅ Small form factor (7×7 mm)
- ✅ Good for precise timing-critical applications
- ✅ Dual cores allow separation of CAN and motion control

#### Cons
- ❌ **No native CAN** — requires external transceiver (adds cost, PCB area, latency)
- ❌ No CAN-FD support (without external chip)
- ❌ Limited SRAM (264 KB) — tight for trajectory buffers
- ❌ Lower CPU (150 MHz vs 240 MHz)
- ❌ I2C ring bus alternative is unproven at 120-motor scale

#### Verdict: **Viable alternative** if CAN transceiver cost is acceptable. PIO is superior for step pulse precision, but the CAN gap is significant.

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
- ❌ **Too slow** for 4.5 m/s motion profiles
- ❌ No CAN-FD
- ❌ Only 2 KB SRAM — insufficient for trajectory buffers
- ❌ 8-bit CPU — poor floating-point performance
- ❌ Limited I/O for 12 motors
- ❌ No real-time guarantees for synchronized multi-ball moves

#### Verdict: ❌ **Not recommended for production.** Acceptable for a single-ball proof-of-concept only.

---

## 3. Comparative Summary

| Feature | ESP32-S3 | Pico 2 (RP2350) | ATmega328P + MCP2515 |
|---------|----------|-----------------|---------------------|
| **CPU** | 240 MHz dual-core | 150 MHz dual-core | 20 MHz single-core |
| **CAN** | ✅ Native CAN-FD | ❌ External only | ❌ External only (no FD) |
| **Stepper ch.** | 6–8 direct, 32 w/ I2S mux | 4 PIO + 8 GPIO | 2–4 bit-banged |
| **Step precision** | Good (PWM) | **Excellent (PIO)** | Poor (bit-bang) |
| **SRAM** | 512 KB + PSRAM | 264 KB | 2 KB |
| **Real-time** | Good | **Best (PIO)** | Poor |
| **Power** | ~300 mA | ~75 mA | ~30 mA |
| **Connectivity** | WiFi + BT | USB only | None |
| **Cost per controller** | ~$5–8 | ~$5–8 + $2 CAN | ~$3–4 |
| **Suitability for 12 balls** | ✅ Excellent | ✅ Good | ❌ Poor |
| **Suitability for 120 balls** | ✅ Excellent | ✅ Good (w/ CAN) | ❌ Not viable |

---

## 4. Recommendation Matrix

### If CAN-FD is required (recommended):
| Priority | Choice |
|----------|--------|
| **Best overall** | **ESP32-S3** (native CAN-FD, I2S mux, WiFi) |
| Best step precision | Pico 2 + MCP2517FD (PIO + CAN-FD) |

### If cost is the primary concern:
| Priority | Choice |
|----------|--------|
| **Cheapest viable** | **ESP32-S3** (still cheaper than Pico 2 + CAN transceiver) |
| Cheapest PoC | ATmega328P + MCP2515 (single ball only) |

### If power consumption matters:
| Priority | Choice |
|----------|--------|
| **Lowest power** | Pico 2 (~75 mA) |
| Acceptable power | ESP32-S3 (~300 mA) |

### Final Recommendation: **ESP32-S3**

**Rationale:**
1. Native CAN-FD is critical — 120 motors on a single bus require high throughput
2. I2S PWM multiplexing solves the 12-channel stepper problem elegantly
3. WiFi/Bluetooth enables remote monitoring and pattern upload
4. PSRAM allows large trajectory buffers for complex patterns
5. Total cost per controller (~$5–8) is competitive even with the Pico 2 + CAN transceiver option
6. Large community and ecosystem reduce development risk

**Per-controller BOM (ESP32-S3):**

| Item | Qty | Cost (est.) |
|------|-----|-------------|
| ESP32-S3 module | 1 | $4.00 |
| TMC5160 driver | 12 | $60.00 |
| Decoupling caps, resistors | 1 set | $2.00 |
| Connector board | 1 | $3.00 |
| **Total per controller** | | **~$69** |
| **Total for 10 controllers** | | **~$690** |

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
| Max speed @ 4.5 m/s | Viable with proper drum | Better margin |

**Verdict:** BLDC + FOC could handle higher speeds but adds encoder complexity and cost. For 4.5 m/s, steppers are sufficient. Consider BLDC only if speed target increases significantly (> 6 m/s).

---

## 6. Driver IC Comparison

| Driver | CAN | CAN-FD | StallGuard2 | Steppers | Notes |
|--------|-----|--------|-------------|----------|-------|
| **TMC6300** | ✅ Native | ✅ | ✅ | 1 | **Best choice** — native CAN-FD, StallGuard2, high current (up to 5.7 A RMS) |
| **TMC5160** | SPI | ❌ | ✅ | 1 | Mature, well-documented, no native CAN |
| **TMC5130** | SPI | ❌ | ✅ | 1 | Lower current, no ramp generator |
| **TMC2209** | UART | ❌ | ✅ | 1 | Cheapest, no CAN, quiet operation |
| **TMC6200** | SPI | ❌ | ❌ | 1 | No StallGuard, no CAN |
| **BTS7971B** | ❌ | ❌ | ❌ | 1 (BLDC) | For BLDC FOC, not stepper |

**Recommendation:** **TMC6300** for production. Native CAN-FD eliminates external transceivers, reducing BOM and failure points. StallGuard2 provides endstop detection.

---

---

## 8. Stepping Resolution Analysis: 1/16 µ-Step vs. I2S Mux (~4 µs Resolution)

This section answers a critical design question: **is 1/16 microstepping sufficient**, or do we need finer resolution, given that the I2S multiplexing approach has ~4 µs timing granularity?

### 8.1 The Core Trade-off

| Approach | Timing Resolution | Max µ-Stepping | Linear Resolution (20 mm drum) | Max Step Rate @ 4.5 m/s |
|----------|------------------|----------------|-------------------------------|------------------------|
| **I2S mux** | ~4 µs | 1/16 (practical) | 19.6 µm | 229 kHz |
| **PIO (Pico 2)** | ~6.7 ns | 1/32+ | 9.8 µm | 229 kHz |
| **Direct GPIO (ESP32)** | ~50–100 ns | 1/32+ | 9.8 µm | 229 kHz |

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

**Key finding:** At 1/16 microstepping with a 20 mm drum, linear resolution is **19.6 µm** — well within visual acceptability.

### 8.3 Is 19.6 µm Good Enough? (Visual Perception Analysis)

The ball is ~46 mm in diameter. A 19.6 µm step displacement is:

```
19.6 µm / 46,000 µm = 0.043% of ball diameter
```

**Human visual acuity** at 1 meter viewing distance is ~0.17 mm (1 arcminute). At 0.5 m distance, ~0.085 mm.

```
19.6 µm << 85 µm (visual threshold at 0.5 m)
```

**Conclusion:** 1/16 microstepping resolution is **visually imperceptible** — the step transitions appear perfectly smooth to the human eye. Finer microstepping (1/32, 1/256) provides no visual benefit at this scale.

### 8.4 Timing Analysis at Maximum Speed (4.5 m/s)

**Step rate required at max speed:**
```
Steps/sec = velocity / linear_resolution_per_step
```

| Microstepping | 10 mm Drum | 20 mm Drum | 30 mm Drum |
|--------------|-----------|-----------|-----------|
| Full Step | 14,324 Hz | 9,549 Hz | 6,366 Hz |
| 1/4 µ-Step | 57,296 Hz | 38,197 Hz | 25,465 Hz |
| 1/8 µ-Step | 114,592 Hz | 76,394 Hz | 50,929 Hz |
| **1/16 µ-Step** | **229,183 Hz** | **152,788 Hz** | **101,855 Hz** |
| 1/32 µ-Step | 458,366 Hz | 305,577 Hz | 203,715 Hz |

**At 1/16 µ-Step with 20 mm drum @ 4.5 m/s:**
- Step frequency: **152,788 steps/sec**
- Time per step: **6.54 µs**
- Time per 1/16 step at 4.5 m/s: **6.54 µs**

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

**At 1/16 µ-Step, 20 mm drum, 4.5 m/s:**
- Required: 152,788 Hz (6.54 µs per step)
- I2S mux capability: ~416,000 Hz (2.4 µs per frame)
- **Margin: 2.7×** — ✅ comfortable margin

### 8.6 What About 1/32 Microstepping via I2S Mux?

```
Required step rate (1/32, 20 mm drum, 4.5 m/s): 305,577 Hz
Time per step: 3.27 µs
I2S mux frame time: 2.4 µs
```

**Margin: 0.74×** — ❌ **I2S mux cannot sustain 1/32 µ-Step at 4.5 m/s**.

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

**At 1/32 µ-Step, 20 mm drum, 4.5 m/s:**
- Required: 305,577 Hz (3.27 µs per step)
- PIO capability: ~5,000,000 Hz (conservative)
- **Margin: 16×** — ✅ more than sufficient

### 8.8 Summary: Resolution Recommendation

| Factor | 1/16 µ-Step | 1/32 µ-Step |
|--------|------------|------------|
| **Visual smoothness** | ✅ Imperceptible steps | ✅ (no visible improvement) |
| **Holding torque** | Good | ~2× better (lower ripple) |
| **Resonance damping** | Acceptable | Better (finer control) |
| **I2S mux @ 4.5 m/s** | ✅ 2.7× margin | ❌ 0.74× margin (fails) |
| **PIO @ 4.5 m/s** | ✅ 33× margin | ✅ 16× margin |
| **CPU load** | Lower | ~2× higher |
| **Data throughput (CAN)** | Lower | ~2× higher |

### 8.9 Final Recommendation

**1/16 microstepping is the optimal choice for the I2S multiplexing approach:**

1. **Visually indistinguishable** from finer stepping — ball motion appears perfectly smooth
2. **Fits within I2S mux timing** with 2.7× margin at max speed (4.5 m/s)
3. **Sufficient holding torque** for 50 g ball + acceleration forces
4. **Lower CAN bus load** — 1/16 steps generate half the data of 1/32
5. **Acceptable resonance** — stepper motors at 1/16 have manageable resonance peaks

**Use 1/32 microstepping only with direct GPIO/PIO control** (not I2S mux), where the margin is large enough to justify the extra data throughput.

For the **Pico 2 with PIO**, 1/32 microstepping is feasible and provides slightly smoother low-speed motion, but the visual benefit is marginal. **1/16 remains the recommended default** for both platforms to minimize CAN bus load.

---

## 7. Per-Controller Block Diagram (ESP32-S3)

```
  ┌─────────────────────────────────────────────────────┐
  │                  ESP32-S3 Controller                 │
  │                                                     │
  │  ┌──────────┐  ┌──────────┐  ┌──────────────────┐  │
  │  │  CAN-FD  │  │  I2S PWM │  │  GPIO / SPI      │  │
  │  │  Module  │  │  (8 ch)  │  │  (DIR/ENABLE)    │  │
  │  └────┬─────┘  └────┬─────┘  └────────┬─────────┘  │
  │       │             │                  │            │
  │       ▼             ▼                  ▼            │
  │  ┌──────────┐  ┌──────────┐  ┌──────────────────┐  │
  │  │ TMC6300  │  │ TMC6300  │  │ TMC6300 (x12)    │  │
  │  │ (ch 1)   │  │ (ch 8)   │  │ (ch 9-12 via SPI)│  │
  │  └────┬─────┘  └────┬─────┘  └────────┬─────────┘  │
  │       │             │                  │            │
  │       ▼             ▼                  ▼            │
  │  ┌──────────┐  ┌──────────┐  ┌──────────────────┐  │
  │  │Motor 1  │  │Motor 8  │  │Motors 9-12       │  │
  │  └──────────┘  └──────────┘  └──────────────────┘  │
  │                                                     │
  │  24 V Power ──► DC-DC ──► Motor + Logic Supplies    │
  └─────────────────────────────────────────────────────┘
```

---

## 9. CAN vs WiFi — Is CAN Needed When Using ESP32 with WiFi?

This is a critical architectural question. The ESP32-S3 has **both** WiFi and CAN-FD. Since WiFi is available, **can we skip CAN entirely** and use WiFi (or Ethernet) for controller-to-master communication?

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
- **Density problem:** 120 ESP32-S3 devices on one 2.4 GHz channel = severe congestion
- **AP/single-point failure:** If the access point fails, the entire system is dead
- **Range limitations:** 1200–1800 mm tall sculpture may have dead zones
- **Regulatory:** 2.4 GHz power limits vary by region

#### 9.2.4 Power Consumption

| Mode | CAN (MCP2517/TMC6300) | WiFi (ESP32-S3) | Ethernet (LAN8720) |
|------|----------------------|-----------------|-------------------|
| Idle | ~5 mA | ~80 mA (beaconing) | ~15 mA |
| Active tx/rx | ~30 mA | ~200 mA (tx burst) | ~30 mA |
| Per 120 controllers | **3.6 W** | **24 W** | **3.6 W** |

> WiFi consumes **6–8× more power** than CAN for the same communication load. For a 120-controller system, this is 20+ watts of extra heat and power draw.

#### 9.2.5 System Architecture Comparison

**With CAN (recommended):**
```
  Master (PC/ESP32)
      │
      └─ CAN bus (twisted pair, 120 Ω termination)
          │
          ├─ Controller 1 (ESP32-S3 + CAN-FD)
          ├─ Controller 2 (ESP32-S3 + CAN-FD)
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
  │  │  │ CAN-FD    │  │  WiFi (AP/Station)    │ │  │
  │  │  │ (real-    │  │  (config / monitoring)│ │  │
  │  │  │  time)    │  │                       │ │  │
  │  │  └────┬──────┘  └───────────────────────┘ │  │
  │  │       ▼                                   │  │
  │  │  12 × TMC6300 → 12 × NEMA-17 motors       │  │
  │  └───────────────────────────────────────────┘  │
  └─────────────────────────────────────────────────┘
```

| Channel | Purpose | Protocol | Latency Requirement |
|---------|---------|----------|-------------------|
| **CAN bus** | Real-time motion commands, status, sync, E-stop | CAN-FD @ 1 Mbit/s | < 1 ms |
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

**Per-controller batch messages (one message per controller, containing all 12 motor positions) are REQUIRED for real-time control at 1 kHz.**

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
| Control rate | **500 Hz** (sufficient for 4.5 m/s motion) |
| Bus protocol | **CAN-FD**, 1 Mbit/s base / 8 Mbit/s data phase |
| Messages per cycle | 20 (2 frames × 10 controllers) |
| Bus utilization @ 500 Hz | **37.5%** (comfortable margin) |
| Messages per cycle (status, 10 Hz) | 10 (1 frame × 10 controllers) |
| Status bus utilization | **< 1%** (negligible) |
| Total bus utilization (500 Hz + 10 Hz) | **~38%** |

> **Conclusion:** CAN-FD is necessary for this application. Classic CAN at 1 Mbit/s cannot handle 1 kHz per-motor updates even with batch messages. CAN-FD with 8 Mbit/s data phase handles 500 Hz with 37.5% utilization — a comfortable margin. The system MUST use 12-motor controllers (not 4-motor) to keep bus load manageable.

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
| **What is the minimum viable communication?** | CAN-FD at 1 Mbit/s base / 1 Mbit/s data phase is sufficient |
| **Is CAN-FD necessary?** | Yes — for burst handling and status feedback. Classic CAN at 250 kbit/s is barely sufficient for status-only. |
| **Must controllers have motion planners?** | **YES — this is critical.** Without on-controller ramp generation, CAN traffic is 37.5% (batch) or 450% (per-motor) at 1 kHz — impossible. With ramp generation, utilization drops to < 12%. |

**Recommendation: Keep CAN-FD as the primary real-time bus. Use WiFi exclusively for configuration, monitoring, and pattern upload. Each controller MUST have its own motion planner (ramp / S-curve generator) to minimize CAN traffic. This hybrid approach gives the best of both worlds: deterministic real-time control + convenient wireless configuration.**

**Critical design rule:** The master sends motion **parameters** (target position + profile type), not position samples. The controller's internal motion planner generates the trajectory at ≥ 1 kHz locally. This reduces CAN traffic by 2 orders of magnitude and makes the system feasible.

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

> **Key finding:** With consumer-grade quartz (35 ppm), two controllers can drift **25 ms apart after just 5 minutes** and **2 seconds apart after 1 hour**. For synchronized wave patterns requiring sub-millisecond alignment, this is **unacceptable without correction**.

### 10.3 Impact on Synchronized Patterns

**Sub-millisecond synchronization requirement:**
```
Position error from timing drift: Δx = v × Δt
```

| Pattern Speed | Drift @ 5 min (25 ms) | Drift @ 1 hour (2.5 s) |
|--------------|----------------------|------------------------|
| Static (v = 0) | 0 mm | 0 mm |
| Slow breathing (v = 0.5 m/s) | 12.5 mm | 1.25 m |
| Medium wave (v = 2 m/s) | 50 mm | 5 m (off grid!) |
| Fast wave (v = 4.5 m/s) | 112 mm | 11.2 m (way off grid!) |

> **Critical finding:** At 4.5 m/s, controllers drift **112 mm out of sync after only 5 minutes** — exceeding grid spacing (80–120 mm). After 1 hour, balls are **11 meters apart** — completely unsynchronized.

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

> **Key finding:** With consumer quartz (35 ppm), corrections of **2–5 ms** during rest periods are sufficient to maintain synchronization for several minutes. With TCXO (5 ppm), corrections are sub-millisecond and rarely needed.

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
│ DLC: 8 bytes                                │
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
Correction needed: 3.5 µs per 100 ms = 3.5%
```

**Feasibility:** 3.5 µs pause during 50–100 ms rest = **easily achievable** (0.007–0.002% of rest time).

**Worst case: 35 ppm drift, no rest (continuous rapid cascade at 0.2 s period)**
```
No rest periods → no pause-based correction
Drift accumulates: 35 µs/s
After 30 s: 1.05 ms drift → 4.7 mm position error at 4.5 m/s
After 5 min: 10.5 s drift → 47 m (off grid!)
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
1. **Use built-in quartz** (consumer grade, 35 ppm) for all controllers — sufficient for most artistic patterns that include rest periods.
2. **Send SYNC messages every 100 ms** during rest periods to correct drift.
3. **Insert pauses of 1–5 ms** during rest to correct accumulated drift.
4. **Use TCXO (± 5 ppm)** for controllers in patterns that require continuous motion without rest (rapid cascades).
5. **Target synchronization accuracy:** < 1 ms between any two controllers at all times.
6. **Rest periods are sufficient:** Most artistic patterns naturally include rest at motion peaks — use these for clock correction.
