# System Requirements — Hanging Balls

> Formal system-level requirements specification for the 120-ball kinetic sculpture.

---

## 1. System Scope

A real-time kinetic art installation comprising 120 individually-controlled steel balls suspended in a 10 × 12 grid. Each ball is driven by an independent stepper motor via thread wound on a motor-driven spool. The system produces synchronized wave patterns, interference effects, and choreographed visual displays.

This document states **behaviour and constraints**. Specific design choices — controller part numbers, microstep ratio, sync interval, oscillator grade, endstop sensor, enclosure material, bus variant — are made in the analysis documents and are not requirements here.

---

## 2. Stakeholders

| Stakeholder | Interest |
|-------------|----------|
| **Artist/Designer** | Visual quality, pattern diversity, aesthetic appeal |
| **Systems Engineer** | Reliability, maintainability, safety |
| **Mechanical Engineer** | Structural integrity, vibration, wear |
| **Electrical Engineer** | Power distribution, signal integrity, EMI |
| **Software Engineer** | Real-time control, pattern generation, CAN protocol |
| **Facility Manager** | Safety, maintenance, power consumption |
| **Viewers/Visitors** | Visual experience, interactivity |

---

## 3. Functional Requirements

### 3.1 Motion Control

| ID | Requirement | Priority |
|----|-------------|----------|
| FR-MOT-001 | The system shall independently control the vertical position of all 120 balls simultaneously. | MUST |
| FR-MOT-002 | Nominal per-ball position resolution shall be ≤ 20 µm linear at the drum under 1/16 microstepping on a 20 mm drum. Actual accuracy is limited by missed steps; visible smoothness is governed by CON-010 and NFR-PERF-004. | SHOULD |
| FR-MOT-003 | The system shall support vertical speeds up to the free-fall ceiling of 4.5 m/s over the 1 m travel. 4.5 m/s is an upper bound, not a speed every move must reach. | MUST |
| FR-MOT-004 | Each ball shall achieve an upward acceleration of ≥ 30 m/s². | MUST |
| FR-MOT-005 | Each ball shall operate within a vertical travel range of 1000 mm (1 m). | MUST |
| FR-MOT-006 | The system shall support S-curve (jerk-limited) motion profiles. | SHOULD |
| FR-MOT-007 | The system shall support trapezoidal motion profiles. | SHOULD |
| FR-MOT-008 | The system shall hold ball position against gravity without slipping (static holding torque ≥ 0.01 Nm at the drum, 20 mm Ø). | MUST |
| FR-MOT-009 | The system shall detect the mechanical lower endstop, by motor stall current or a switch at the stop. | MUST |
| FR-MOT-010 | The system shall perform an automatic homing sequence on power-up: move to the lower stop, record the home position, return to a safe offset. | MUST |
| FR-MOT-011 | On emergency stop, all balls shall enter a controlled stop or held state within 10 ms of the E-stop signal. The stop shall not rely on de-energising the motors. | MUST |
| FR-MOT-012 | The system shall keep the thread taut during ordinary motion. Payout faster than g is permitted only as the intentional free-fall drop. | MUST |
| FR-MOT-013 | The system shall support an intentional free-fall descent with a braked catch over the last few centimetres. | SHOULD |
| FR-MOT-014 | Downward acceleration beyond g shall only occur during the intentional free-fall drop. | MUST |

### 3.2 Pattern Generation

| ID | Requirement | Priority |
|----|-------------|----------|
| FR-PAT-001 | The system shall generate traveling sine waves across the grid. | MUST |
| FR-PAT-002 | The system shall generate standing wave patterns. | MUST |
| FR-PAT-003 | The system shall generate spiral patterns (rotating through the grid). | SHOULD |
| FR-PAT-004 | The system shall generate random cascade patterns. | SHOULD |
| FR-PAT-005 | The system shall support music-reactive mode (FFT-driven from audio input). | SHOULD |
| FR-PAT-006 | The system shall support user-defined choreography (keyframe sequences). | SHOULD |
| FR-PAT-007 | The system shall support at least 8 distinct pattern types. | MUST |
| FR-PAT-008 | Pattern transitions shall be smooth (no discontinuous position jumps). | MUST |
| FR-PAT-009 | The system shall allow runtime pattern switching without stopping motion. | SHOULD |

### 3.3 Synchronization

| ID | Requirement | Priority |
|----|-------------|----------|
| FR-SYNC-001 | All 120 balls shall be synchronized to a global timebase with ≤ 1 ms precision. | MUST |
| FR-SYNC-002 | The system shall support buffered motion commands executed on the synced timeline. | MUST |
| FR-SYNC-003 | The system shall provide a global timebase mechanism sufficient to maintain FR-SYNC-001. | MUST |
| FR-SYNC-004 | Inter-controller communication latency shall be ≤ 5 ms (worst case, 10 controllers). | MUST |
| FR-SYNC-005 | Each controller shall track its local oscillator drift relative to the master timebase. | MUST |
| FR-SYNC-006 | The system shall correct oscillator drift by any suitable method — timing pause, clock trim, or a better oscillator — so that alignment stays within FR-SYNC-001. | MUST |
| FR-SYNC-007 | Clock correction shall not cause a visible position discontinuity. | MUST |

### 3.4 Communication

| ID | Requirement | Priority |
|----|-------------|----------|
| FR-COMM-001 | The system shall use a CAN 2.0B bus at ≥ 1 Mbit/s for controller-to-master communication. | MUST |
| FR-COMM-002 | The system may support CAN-FD (bitrate switching) for higher data throughput. | SHOULD |
| FR-COMM-003 | The CAN bus shall be a linear bus, terminated at both ends, with short stubs only. | MUST |
| FR-COMM-004 | The system shall define message types for: move commands, status reports, homing, emergency stop, pattern commands, global sync. | MUST |
| FR-COMM-005 | The system shall support at least 10 controller nodes on a single CAN bus. | MUST |
| FR-COMM-006 | The system shall provide a master controller interface (USB/Ethernet/WiFi) for pattern upload and monitoring. | SHOULD |

### 3.5 Local Trajectories

| ID | Requirement | Priority |
|----|-------------|----------|
| FR-ARCH-001 | Each column controller shall execute its own trajectory at ≥ 1 kHz. The master shall send motion parameters, not position samples. | MUST |

### 3.6 Monitoring & Diagnostics

| ID | Requirement | Priority |
|----|-------------|----------|
| FR-MON-001 | The system shall report per-ball position, velocity, and error status to the master. | MUST |
| FR-MON-002 | The system shall report motor temperature per controller. | SHOULD |
| FR-MON-003 | The system shall log error events (overcurrent, stall, communication loss) with timestamps. | MUST |
| FR-MON-004 | The system shall provide a web-based or GUI monitoring interface. | SHOULD |
| FR-MON-005 | The system shall indicate controller health via status LEDs (green = OK, red = fault). | MUST |

### 3.7 User Interface

| ID | Requirement | Priority |
|----|-------------|----------|
| FR-UI-001 | The system shall provide a pattern selection interface (web UI or physical controls). | SHOULD |
| FR-UI-002 | The system shall allow runtime adjustment of pattern parameters (speed, amplitude, frequency). | SHOULD |
| FR-UI-003 | The system shall provide a prominent hardware emergency stop button. | MUST |
| FR-UI-004 | The system shall support scheduled operation (start/stop times, pattern rotation). | OPTIONAL |

---

## 4. Non-Functional Requirements

### 4.1 Performance

| ID | Requirement | Priority |
|----|-------------|----------|
| NFR-PERF-001 | Speed ceiling: 4.5 m/s per ball (free-fall limit over 1 m). This is an upper bound. | MUST |
| NFR-PERF-002 | Maximum per-ball upward acceleration: ≥ 30 m/s². | MUST |
| NFR-PERF-003 | Maximum per-ball jerk: ≤ 500 m/s³ (for visual smoothness). | SHOULD |
| NFR-PERF-004 | System-wide pattern update rate: ≥ 60 Hz (all 120 balls). | MUST |
| NFR-PERF-005 | CAN bus message latency (controller → master): ≤ 5 ms. | MUST |
| NFR-PERF-006 | Pattern generation to motion execution delay: ≤ 10 ms. | SHOULD |

### 4.2 Reliability

| ID | Requirement | Priority |
|----|-------------|----------|
| NFR-REL-001 | Mean time between failures (MTBF): ≥ 1000 hours. | SHOULD |
| NFR-REL-002 | The system shall continue partial operation if 1–2 controllers fail (degraded mode). | SHOULD |
| NFR-REL-003 | Single controller failure shall not affect operation of other controllers. | MUST |
| NFR-REL-004 | Thread failure (breakage) on one ball shall not cause cascading failures. | MUST |
| NFR-REL-005 | Power loss recovery: the system shall re-home all balls on power restoration. | MUST |

### 4.3 Safety

| ID | Requirement | Priority |
|----|-------------|----------|
| NFR-SAFE-001 | A ball that falls shall remain inside the piece. Secondary cables, an enclosure, or another means may be used; the requirement is containment, not a particular device. | MUST |
| NFR-SAFE-002 | The enclosure or guard shall contain a detached ball and its hardware. | MUST |
| NFR-SAFE-003 | Emergency stop shall end in a hold or a braked descent within 100 ms. It shall not rely on cutting motor current alone, because motor current is what holds a ball. | MUST |
| NFR-SAFE-004 | Per-motor overcurrent protection (electronic or fuse) shall be provided. | MUST |
| NFR-SAFE-005 | The system shall include warning signage and safety interlocks on the enclosure. | MUST |
| NFR-SAFE-006 | Total suspended weight (120 balls + threads + hardware) shall not exceed the structural capacity of the frame. | MUST |
| NFR-SAFE-007 | The system shall be grounded/earthed for electrical safety. | MUST |
| NFR-SAFE-008 | The 24 V bus shall absorb energy regenerated during lowering and braking (clamp or brake resistor). | MUST |

### 4.4 Environmental

| ID | Requirement | Priority |
|----|-------------|----------|
| NFR-ENV-001 | Operating temperature: 10 °C – 40 °C. | SHOULD |
| NFR-ENV-002 | Operating humidity: 20 % – 80 % RH (non-condensing). | SHOULD |
| NFR-ENV-003 | The system shall be suitable for indoor operation. | MUST |
| NFR-ENV-004 | Noise level: ≤ 45 dB(A) at 1 m. | SHOULD |

### 4.5 Maintainability

| ID | Requirement | Priority |
|----|-------------|----------|
| NFR-MAINT-001 | Individual ball/station replacement shall be possible without removing other balls. | SHOULD |
| NFR-MAINT-002 | Controller modules shall be hot-swappable (replaced without system shutdown). | SHOULD |
| NFR-MAINT-003 | Diagnostic mode shall allow individual ball testing. | SHOULD |
| NFR-MAINT-004 | The master and all column controllers shall support field firmware update over CAN or WiFi without removing boards. | MUST |
| NFR-MAINT-005 | The spare-parts list shall be documented and commercially available. | MUST |

### 4.6 Physical

| ID | Requirement | Priority |
|----|-------------|----------|
| NFR-PHYS-001 | Grid dimensions: 10 columns × 12 rows, ball spacing ≥ 80 mm (adjustable). | MUST |
| NFR-PHYS-002 | Total system height: ≥ 1200 mm (1 m travel + suspension + frame). | MUST |
| NFR-PHYS-003 | Ball diameter: ~46 mm (50 g polished steel sphere). | MUST |
| NFR-PHYS-004 | Total ball weight: 120 × 50 g = 6 kg. | MUST |
| NFR-PHYS-005 | Frame material: aluminum extrusion (2020 profile or equivalent) or steel. | MUST |
| NFR-PHYS-006 | Total system weight (frame + motors + electronics): ≤ 150 kg. | SHOULD |
| NFR-PHYS-007 | The thread shall be wound in a single layer on a constant-radius drum, so that the position scale does not change with height. | MUST |

---

## 5. Interface Requirements

### 5.1 Mechanical Interfaces

| ID | Requirement | Priority |
|----|-------------|----------|
| IR-MECH-001 | Drum Ø: 20–30 mm (designer selectable). | MUST |
| IR-MECH-002 | Thread type: Dyneema or Kevlar, Ø 0.5 mm. | MUST |
| IR-MECH-003 | Motor mounting: standard NEMA-17 (42 × 42 mm) footprint. | MUST |
| IR-MECH-004 | Frame construction: 2020 aluminum extrusion or equivalent. | MUST |

### 5.2 Electrical Interfaces

| ID | Requirement | Priority |
|----|-------------|----------|
| IR-ELEC-001 | Motor supply voltage: 24 V DC nominal. | MUST |
| IR-ELEC-002 | Average system power consumption shall fit the available supply (design budget ≤ 1200 W). | SHOULD |
| IR-ELEC-003 | Logic supply: 3.3 V or 5 V (controller-dependent). | MUST |
| IR-ELEC-004 | Power distribution: centralized 24 V bus with per-motor current regulation. | MUST |

### 5.3 Communication Interfaces

| ID | Requirement | Priority |
|----|-------------|----------|
| IR-COMM-001 | Bus: CAN 2.0B at ≥ 1 Mbit/s; CAN-FD optional. | MUST |
| IR-COMM-002 | Bus topology: linear, with 120 Ω termination at both ends and short stubs only. | MUST |
| IR-COMM-003 | Master interface: USB, Ethernet, or WiFi (at least one). | MUST |
| IR-COMM-004 | Message frame: CAN 2.0B (8 bytes) or CAN-FD (up to 64 bytes). | MUST |

### 5.4 Software Interfaces

| ID | Requirement | Priority |
|----|-------------|----------|
| IR-SOFT-001 | Pattern API: RESTful or WebSocket interface for pattern upload. | SHOULD |
| IR-SOFT-002 | Status API: real-time position/velocity reporting via CAN or network. | SHOULD |
| IR-SOFT-003 | Configuration: persistent storage of grid layout and calibration data. | MUST |
| IR-SOFT-004 | Logging: event log accessible via the network interface. | SHOULD |

---

## 6. Constraints

| ID | Constraint | Priority |
|----|-----------|----------|
| CON-001 | 120 balls, 10 × 12 grid, 1 m vertical travel — fixed. | MUST |
| CON-002 | Ball mass: 50 g each — fixed. | MUST |
| CON-003 | Speed shall not exceed 4.5 m/s — the free-fall ceiling over 1 m. | MUST |
| CON-004 | Motor type: stepper motor (NEMA-11 or NEMA-17) or BLDC with FOC. | MUST |
| CON-005 | Communication: CAN 2.0B, with CAN-FD optional. | MUST |
| CON-006 | Lower stop: mechanical. Detection by stall current or a switch; the stop is the reference. | MUST |
| CON-007 | Drum diameter: 10–30 mm, constant radius, single-layer wind. | MUST |
| CON-008 | Controller grouping and count are design choices; the bus shall support the chosen node count (at least 10). | MUST |
| CON-009 | Indoor installation — no weatherproofing required. | MUST |
| CON-010 | Visual quality: motion must appear smooth to the human eye (no visible stepping). | MUST |

---

## 7. Assumptions

| ID | Assumption |
|----|-----------|
| ASM-001 | Balls are suspended from a rigid overhead frame. |
| ASM-002 | Thread elasticity is negligible for motion accuracy (< 100 µm stretch at maximum load). |
| ASM-003 | Air resistance is negligible at 4.5 m/s for a 46 mm sphere (drag coefficient ~0.47, drag force ~0.01 N, about 2 % of ball weight). |
| ASM-004 | Thermal effects on thread strength are within material limits at operating temperatures. |
| ASM-005 | The installation environment is stable (no significant vibration or air currents). |
| ASM-006 | Viewers are at 0.5–3 m distance from the sculpture. |
| ASM-007 | Power is available: 24 V DC, ~1200 W continuous. |

---

## 8. Glossary

| Term | Definition |
|------|-----------|
| **CAN** | Controller Area Network, serial bus protocol |
| **CAN-FD** | CAN with Flexible Data-rate, supports higher bitrate and larger payloads |
| **FOC** | Field-Oriented Control, BLDC motor control algorithm |
| **NEMA** | National Electrical Manufacturers Association (motor frame standard) |
| **PIO** | Programmable I/O (RP2350 feature for precise timing) |
| **S-curve** | Jerk-limited motion profile with smooth acceleration transitions |
| **StallGuard** | TMC motor driver feature for current-based load sensing |
| **StealthChop** | TMC motor driver mode for quiet, smooth low-speed operation |
| **µ-Step** | Microstepping — subdividing full motor steps for smoother motion |

---

*Document version: 2.0*
