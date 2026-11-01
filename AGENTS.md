# AGENTS.md — Hanging Balls Development Guidelines

> AI agent and developer reference for the Hanging Balls kinetic sculpture project.

---

## 1. Project Identity

| Field | Value |
|-------|-------|
| **Name** | Hanging Balls |
| **Type** | Real-time kinetic art installation |
| **Scale** | 120 balls, 10 × 12 grid, 1 m vertical travel |
| **Ball mass** | 50 g polished steel (Ø ≈ 46 mm) |
| **Max speed** | ~4.5 m/s (upward) |
| **Communication** | CAN / CAN-FD @ 1 Mbit/s |
| **Control granularity** | Per-ball independent motion |

---

## 2. Hardware Architecture

### 2.1 Mechanical Stack (per station)

```
  Frame top
    │
    ├─ Spool / Drum (Ø 10–30 mm)
    │   └─ driven by stepper motor
    │
    ├─ Thread (Dyneema/Kevlar, ~0.5 mm)
    │   └─ wound on spool
    │
    ├─ Steel ball (50 g, polished, Ø ~46 mm)
    │
    └─ Lower mechanical stop
        └─ detected by motor stall current
```

### 2.2 Electrical Architecture

```
  24 V power bus
      │
      ├─── per-motor current regulation
      │
  CAN bus (1 Mbit/s, CAN-FD)
      │
  ┌───┼─────────────────────────────────┐
  ▼   ▼         ▼                       ▼
Motor 1  Motor 2  …  Motor 12  (per controller)
  │        │                   │
  ▼        ▼                   ▼
TMC driver  TMC driver  …  TMC driver
  │        │                   │
  ▼        ▼                   ▼
NEMA-17   NEMA-17   …  NEMA-17
  │        │                   │
  ▼        ▼                   ▼
Spool    Spool     …  Spool
  │        │                   │
  ▼        ▼                   ▼
Ball     Ball    …  Ball
```

### 2.3 Controller Topology

```
                  ┌─────────────┐
                  │  Master     │
                  │  (PC/ESP32) │
                  └──────┬──────┘
                         │ CAN / CAN-FD
              ┌──────────┼──────────┐
              ▼          ▼          ▼
        ┌──────────┐ ┌──────────┐ ┌──────────┐
        │ Ctrl 1   │ │ Ctrl 2   │ │ Ctrl N   │
        │ (12 balls)│ │ (12 balls)│ │ (12 balls)│
        └──────────┘ └──────────┘ └──────────┘
```

**Recommended grouping: 12 motors per controller (one column = 12 balls)**
- Total controllers: 10 (for 120 balls)
- Each controller manages one column of the 10 × 12 grid

---

## 3. Controller Comparison

### 3.1 ESP32-S3 (Strongest Candidate)

| Feature | Value |
|---------|-------|
| CPU | Xtensa LX7, up to 240 MHz |
| CAN | Native CAN 2.0 + **CAN-FD** |
| Stepper channels | 6–8 high-speed directly |
| I2S mux expansion | Up to **32 channels** via I2S PWM |
| GPIO | 40+ usable |
| Memory | 512 KB SRAM, 8 MB+ PSRAM |
| Connectivity | WiFi, Bluetooth (for remote control) |

**Pros:**
- Native CAN-FD support (critical for 120-motor real-time control)
- I2S PWM allows multiplexing many stepper drivers
- High CPU headroom for motion planning
- WiFi for remote monitoring/control

**Cons:**
- 6–8 channels natively (requires I2S mux for 12)
- Higher power consumption than Pico

**Verdict:** **Preferred controller** for this project.

### 3.2 Raspberry Pi Pico 2 (RP2350)

| Feature | Value |
|---------|-------|
| CPU | Dual ARM Cortex-M33, up to 150 MHz |
| CAN | **No native CAN** — requires external transceiver (SJA1000, MCP2517) |
| Stepper channels | 8–12 via PIO state machines |
| PIO | 2 state machines per core (4 total) — excellent for precise timing |
| Memory | 264 KB SRAM, no PSRAM |
| GPIO | 30+ usable |

**Pros:**
- PIO allows precise stepper pulse generation
- Low power consumption
- Good for 12 motors directly
- Can use I2C ring bus as CAN alternative

**Cons:**
- **No native CAN** — adds BOM cost and complexity
- Lower CPU than ESP32
- No WiFi/Bluetooth

**Verdict:** **Viable alternative** if CAN transceiver cost is acceptable. I2C ring bus is a fallback option.

### 3.3 ATmega328P + MCP2515 (Not Recommended)

| Feature | Value |
|---------|-------|
| CPU | 8-bit AVR, 20 MHz |
| CAN | MCP2515 SPI CAN controller (no CAN-FD) |
| Stepper channels | 2–4 max |
| Memory | 2 KB SRAM, 32 KB Flash |

**Pros:**
- Cheap, widely available
- Simple to prototype

**Cons:**
- **Too slow** for 4.5 m/s motion profiles
- No CAN-FD support
- Limited I/O for 12 motors
- Insufficient processing for S-curve planning

**Verdict:** ❌ **Do not use** for production. May be useful for a single-ball proof-of-concept.

---

## 4. Stepper Motor Comparison

### 4.1 Torque vs. Drum Diameter

Required torque per motor (ball mass = 50 g, g = 9.81 m/s²):

```
Torque_static = m × g × r = 0.05 × 9.81 × (drum_Ø / 2)
```

| Drum Ø | Static Torque | Max Accel Torque (50 m/s²) | 28BYJ-48 | NEMA-11 | NEMA-17 |
|--------|--------------|---------------------------|----------|---------|---------|
| 10 mm  | 0.025 Nm    | 0.125 Nm                 | ⚠️ tight | ✅ OK   | ✅ OK   |
| 15 mm  | 0.037 Nm    | 0.188 Nm                 | ⚠️ tight | ✅ OK   | ✅ OK   |
| 20 mm  | 0.049 Nm    | 0.250 Nm                 | ❌ no    | ✅ OK   | ✅ OK   |
| 30 mm  | 0.074 Nm    | 0.375 Nm                 | ❌ no    | ✅ OK   | ✅ OK   |

### 4.2 Motor Specifications

| Parameter | 28BYJ-48 | NEMA-11 (24×24) | NEMA-17 (42×42) |
|-----------|----------|-----------------|-----------------|
| Frame size | 28 mm | 24×24 mm | 42×42 mm |
| Holding torque | 0.09 Nm | 0.2–0.5 Nm | 0.4–1.0 Nm |
| Rated current | 0.6 A | 0.8–1.2 A | 1.5–2.5 A |
| Steps/rev | 2048 (gear 1:64) | 200 (1.8°) | 200 (1.8°) |
| Max RPM | ~1000 (geared) | 3000–6000 | 2000–4000 |
| Size / weight | Small / 25 g | Medium / 80 g | Large / 350 g |
| Cost | ~$2 | ~$8 | ~$12 |
| **Verdict** | Demo only | **Viable** | **Recommended** |

### 4.3 Max RPM by Drum Size (at 4.5 m/s)

```
RPM = (v / (π × D)) × 60
```

| Drum Ø | 28BYJ-48 (OK?) | NEMA-11 (OK?) | NEMA-17 (OK?) |
|--------|----------------|---------------|---------------|
| 10 mm  | ❌ 8600 RPM | ✅ 8600 RPM | ✅ 8600 RPM |
| 15 mm  | ❌ 5730 RPM | ✅ 5730 RPM | ✅ 5730 RPM |
| 20 mm  | ❌ 4300 RPM | ✅ 4300 RPM | ✅ 4300 RPM |
| 30 mm  | ❌ 2860 RPM | ✅ 2860 RPM | ✅ 2860 RPM |

> **Note:** 28BYJ-48 has an internal 1:64 gear reduction, so the spool sees ~1/64th the motor RPM, but this makes precise high-speed control very difficult. **Not recommended for production.**

### 4.4 Recommendation

**NEMA-17 (42×42) with 20–30 mm drum** is the sweet spot:
- Sufficient torque for 4.5 m/s acceleration
- Reasonable RPM at practical drum sizes
- Widely available, well-supported drivers
- Good balance of size, cost, and performance

---

## 5. Stepper Driver / Controller IC

| Driver | CAN | CAN-FD | StallGuard2 | Channels per IC | Notes |
|--------|-----|--------|-------------|-----------------|-------|
| **TMC5160** | SPI (no native) | No | ✅ | 1 (per IC) | Mature, well-documented |
| **TMC2209** | UART (no CAN) | No | ✅ | 1 | Cheap, quiet (StealthChop) |
| **TMC6300** | **Native CAN-FD** | ✅ | ✅ | 1 | **Best for this project** |
| **BTS7971** | No | No | No | 1 (BLDC) | For BLDC with FOC |

> **Recommendation:** TMC6300 with native CAN-FD, or TMC5160 with external CAN transceiver.

---

## 6. CAN Bus Architecture

### 6.1 Protocol Design

```
Frame format (CAN 2.0B / CAN-FD):
┌─────────────────────────────────────────────┐
│ ID (11/29 bit) │ DLC │ Data (8/64 bytes)   │
├─────────────────────────────────────────────┤
│ Station ID (7 bits) │ Cmd Type (4 bits)     │
│ Position (32 bit float) │ Velocity (16 bit)  │
│ Flags (status, error, endstop)              │
└─────────────────────────────────────────────┘
```

### 6.2 Message Types

| Cmd ID | Name | Direction | Payload |
|--------|------|-----------|---------|
| 0x01 | `CMD_MOVE` | Master → Controller | Target X, Y, velocity profile, timing |
| 0x02 | `CMD_STATUS` | Controller → Master | Current pos, speed, errors, temp |
| 0x03 | `CMD_HOME` | Master → Controller | Trigger endstop homing sequence |
| 0x04 | `SYNC` | Master → All | Global timestamp (sub-ms sync) |
| 0x05 | `CMD_PATTERN` | Master → Controller | Pattern ID + parameters |
| 0x06 | `CMD_EMERGENCY_STOP` | Master → All | Kill all motion immediately |

### 6.3 Bus Topology

```
  Master (CAN transceiver)
      │
      ├─── Terminal resistor (120 Ω)
      │
  daisy-chain cable (twisted pair)
      │
  ┌───┼─────────────────────────────────┐
  ▼   ▼         ▼                       ▼
Ctrl 1    Ctrl 2  …  Ctrl 10
  │           │                   │
  ▼           ▼                   ▼
 120 Ω      120 Ω             120 Ω
 (terminating resistors at bus ends)
```

### 6.4 CAN-FD Advantages

- **Higher data rate** for burst messages (up to 8 Mbit/s in data phase)
- **Larger payloads** (up to 64 bytes vs 8 bytes)
- **Lower bus load** for complex motion commands
- **Better for synchronized multi-ball moves**

---

## 7. Motion Control

### 7.1 Motion Profile

```
Velocity
    ┌───────┐
    │       │
    │       │
    │       │
────┘       └────> Time
   accel   cruise  decel
   (jerk)            (jerk)

Position
    ╱───────╲
   ╱         ╲
  ╱           ╲
 ╱             ╲
╱               ╲
```

- **Profile type:** S-curve (jerk-limited) recommended for smooth visual effect
- **Max acceleration:** ~50 m/s² (adjustable per pattern)
- **Max jerk:** Limited by thread elasticity and visual smoothness

### 7.2 Synchronization

- Global timebase via CAN `SYNC` messages
- Sub-millisecond precision required for wave patterns
- Controllers buffer motion commands and execute on synced timeline

### 7.3 Endstop / Homing

```
  Homing sequence:
  1. Motor moves downward at slow speed
  2. StallGuard2 detects current spike at mechanical stop
  3. Position is recorded as "home" (0 mm)
  4. Move to safe offset (e.g., +5 mm)
  5. Mark station as "homed"
  6. Report status to master
```

- **Stall detection:** Monitor motor current via StallGuard2 (TMC5160/6300)
- **Homing speed:** Slow (50–100 mm/s) for accuracy
- **Frequency:** On power-up and periodically during operation

---

## 8. Software Modules

### 8.1 Firmware Structure

```
firmware/
├── master/
│   ├── main.cpp
│   ├── can_bridge/
│   │   ├── can_driver.cpp      # CAN/CAN-FD initialization
│   │   └── protocol.cpp        # Message encoding/decoding
│   ├── motion_planner/
│   │   ├── scurve.cpp          # S-curve trajectory generation
│   │   └── synchronizer.cpp    # Global timebase sync
│   └── pattern_engine/
│       ├── wave.cpp            # Traveling / standing waves
│       ├── spiral.cpp          # Spiral patterns
│       ├── cascade.cpp         # Random cascade
│       └── music_reactive.cpp  # FFT audio response
└── controller/
    ├── main.cpp
    ├── stepper/
    │   ├── esp32_driver.cpp    # ESP32-specific stepper control
    │   ├── pico_driver.cpp     # Pico 2-specific (PIO)
    │   └── tmc_driver.cpp     # TMC5160/6300 communication
    ├── motion/
    │   ├── axis_controller.cpp # Per-ball position/speed control
    │   └── endstop.cpp         # StallGuard2 homing logic
    └── can/
        ├── can_driver.cpp      # CAN/CAN-FD initialization
        └── protocol.cpp        # Message handling
```

### 8.2 Simulation

```
simulation/
├── physics/
│   ├── ball.py          # Ball mass, gravity, air resistance
│   ├── thread.py        # Thread elasticity, damping
│   └── motor.py         # Motor torque, acceleration limits
├── renderer/
│   ├── webgl.py         # 3D visualization (Three.js / WebGL)
│   └── console.py       # ASCII art visualization (debug)
└── patterns/
    ├── wave.py          # Wave pattern definitions
    └── choreography.py  # Keyframe-based sequences
```

---

## 9. Development Phases

### Phase 1: Single-Ball Proof of Concept
- [ ] Select motor (NEMA-17 recommended)
- [ ] Select driver (TMC5160 or TMC6300)
- [ ] Build single station (motor + drum + ball + stop)
- [ ] Implement basic up/down motion
- [ ] Implement endstop homing
- [ ] **Goal:** Ball moves smoothly, homes correctly

### Phase 2: Single-Column (12 balls)
- [ ] Select controller (ESP32-S3 recommended)
- [ ] Design PCB / wiring for 12 motors
- [ ] Implement per-motor CAN protocol
- [ ] Synchronize 12 motors on one controller
- [ ] Generate simple wave pattern across column
- [ ] **Goal:** 12 balls move in coordinated patterns

### Phase 3: Multi-Column Integration
- [ ] Design CAN bus topology for 10 controllers
- [ ] Implement master controller (ESP32 or PC)
- [ ] Cross-column synchronization
- [ ] Full 120-ball pattern engine
- [ ] **Goal:** All 120 balls synchronized

### Phase 4: Full Installation
- [ ] Mechanical frame construction
- [ ] Install all 120 stations
- [ ] Cable management (CAN bus + power)
- [ ] Safety enclosure
- [ ] Artistic pattern library
- [ ] **Goal:** Complete kinetic sculpture operational

---

## 10. Key Decisions & Open Questions

### Decisions Made
| Decision | Choice | Rationale |
|----------|--------|-----------|
| Stepper motor | NEMA-17 (42×42) | Best torque/RPM balance |
| Controller | ESP32-S3 | Native CAN-FD, I2S mux |
| Drum diameter | 20–30 mm | Practical RPM, torque margin |
| Grouping | 12 motors / controller | One controller per column |
| Bus | CAN-FD @ 1 Mbit/s | Real-time, high throughput |
| Driver | TMC6300 (preferred) or TMC5160 | CAN integration, StallGuard2 |

### Open Questions
| Question | Impact | Status |
|----------|--------|--------|
| Thread material? | Affects elasticity, max speed | Research needed |
| Drum material? | 3D print vs. machined | Prototype phase |
| Power distribution? | 24 V bus vs. per-motor | Design phase |
| Enclosure design? | Safety, aesthetics | Design phase |
| Pattern editor UI? | User interaction | Future work |
| Audio input? | Music-reactive mode | Feature request |

---

## 11. Quick Reference

### Critical Parameters

```python
BALL_MASS       = 0.050    # kg
GRAVITY         = 9.81     # m/s²
MAX_SPEED       = 4.5      # m/s
TRAVEL_DISTANCE = 1.0      # m
GRID_COLS       = 10
GRID_ROWS       = 12
TOTAL_BALLS     = 120
CAN_BAUD        = 1_000_000  # 1 Mbit/s
MOTORS_PER_CTRL = 12
TOTAL_CONTROLLERS = 10
```

### Recommended BOM (abbreviated)

```
120 × NEMA-17 stepper motor (42×42, 0.6 Nm)
120 × TMC6300 driver (or TMC5160 + CAN transceiver)
 10 × ESP32-S3 controller board
120 × Spool/drum (Ø 25 mm, 3D printed or machined)
150 m Dyneema thread (0.5 mm)
120 × 50 g polished steel sphere (Ø 46 mm)
 1 × 24 V power supply (50 A total)
 1 × Aluminum frame (2020 extrusion)
```

---

*This document is a living reference. Update it as design decisions evolve.*
