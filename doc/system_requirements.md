# System Requirements — Hanging Balls

> Formal system-level requirements specification for the 120-ball kinetic sculpture.

---

## 1. System Scope

A real-time kinetic art installation comprising 120 individually-controlled steel balls suspended in a grid. Each ball is driven independently by a motor via thread wound on a motor-driven spool. The system produces synchronized wave patterns, interference effects, and choreographed visual displays.

The system comprises two parts: (a) a PC-side pattern development and 3D simulation environment, and (b) the physical motor installation. These may be delivered as separate tools; patterns are authored on the PC and uploaded to the installation.

This document states **behaviour and constraints**. Specific design choices — controller part numbers, microstep ratio, sync interval, oscillator grade, endstop sensor, enclosure material, bus variant — are made in the analysis documents and are not requirements here.

**Top-level safety requirement:** the installation shall be inherently safe — no ball motion, drop, or component failure shall be able to injure a person. Safety shall be achieved by the physical design (containment, masses, speeds, guards), not by relying on a safety-rated stop system.

---

## 2. Stakeholders

| Stakeholder | Interest |
|-------------|----------|
| **Artist/Designer** | Visual quality, pattern diversity, aesthetic appeal |
| **Systems Engineer** | Reliability, maintainability, safety |
| **Mechanical Engineer** | Structural integrity, vibration, wear |
| **Electrical Engineer** | Power distribution, signal integrity, EMI |
| **Software Consultant** | Review of LLM-generated firmware; real-time control, pattern generation, communication protocol |
| **Facility Manager** | Safety, maintenance, power consumption |
| **Viewers/Visitors** | Visual experience, interactivity |

---

## 3. Functional Requirements

### 3.1 Motion Control

| ID | Requirement | Priority |
|----|-------------|----------|
| FR-MOT-001 | The system shall independently control the vertical position of all 120 balls simultaneously. | MUST |
| FR-MOT-002 | Position quantization at the drum shall be ≤ 50 µm linear (ASM-014), so individual steps are not perceptible at any commanded speed. | SHOULD |
| FR-MOT-003 | The system shall be able to follow the free-fall speed envelope v(s) = √(2 g s) on descent when a pattern calls for it. The envelope is concave, so ~4.4 m/s is reached only near the lower stop (s ≈ 1 m); a sustained 4.5 m/s segment does not exist. | MUST |
| FR-MOT-004 | The drive shall cover the worst-case vertical load envelope: (a) hold at rest with a = g, so the holding force is F ≥ mg; and (b) arrest a full free-fall descent (4.43 m/s) within the last 1 cm, requiring a_catch = v²/(2·0.01) ≈ 981 m/s² and F ≈ 49 N (≈ 153 N including reflected inertia, ≈ 1.9 Nm at a 25 mm drum). The catch is the sizing case; the requirement is on system capability, not on which element provides it. | MUST |
| FR-MOT-005 | Each ball shall operate within a vertical travel range of 1000 mm (1 m). | MUST |
| FR-MOT-006 | The system shall support S-curve (jerk-limited) motion profiles. | SHOULD |
| FR-MOT-008 | The system shall hold ball position against gravity without slipping. At standstill the equivalent acceleration is g, so the required holding force is F ≥ mg; this corresponds to a static holding torque of ≥ 0.01 Nm at the drum with margin. | MUST |
| FR-MOT-009 | The system shall provide exactly one position-reference endstop. Its location (upper or lower) and detection method are design choices; it shall be detected on every homing attempt and shall not false-trigger during commanded motion. | MUST |
| FR-MOT-010 | The system shall perform an automatic homing sequence on power-up: move to the reference endstop, record the home position, return to a safe offset. Homing shall complete within 60 s; if the stop is not detected within travel + margin, the system shall stop and report a fault. The offset shall keep all balls clear of the stop and of the lower extreme of travel. | MUST |
| FR-MOT-011 | The red stop button shall remove power from the drives. After power removal the balls shall either be held by the drives' de-energised detent torque or descend to the lower limit of travel; no powered action is required. This is an operational stop, not a safety-rated function. | MUST |
| FR-MOT-012 | During ordinary motion the thread shall remain in tension and shall not go slack, including through control transients. The downward-acceleration limit intended to achieve this is 0.9 g (ASM-010), to be confirmed by a visual test. | MUST |
| FR-MOT-013 | The system shall support an intentional free-fall descent with a braked catch within the last 1 cm (catch distance per ASM-008). | SHOULD |
| FR-MOT-015 | A digital/PC stop command shall halt all motion at the next control opportunity, decelerating within the configured limits, and shall retain each ball's position so that no re-homing is required. | MUST |

### 3.2 Pattern Generation

| ID | Requirement | Priority |
|----|-------------|----------|
| FR-PAT-001 | The system shall reproduce arbitrary user-defined position-vs-time waveforms for all balls, within the speed envelope (CON-003) and the drive's acceleration capability, synchronised across the grid (FR-SYNC-001). Waveform types (sine, standing, spiral, cascade, keyframe choreography, etc.) are not separately specified. | MUST |
| FR-PAT-005 | The system shall support real-time control of motion from a PC (waveforms/parameters streamed in real time). Audio-reactive behaviour, if required, is implemented on the PC side. | SHOULD |
| FR-PAT-008 | Pattern transitions shall be smooth (no discontinuous position jumps), blended over ~1 s (ASM-012). | MUST |
| FR-PAT-009 | The system shall allow runtime pattern switching without stopping motion. | SHOULD |

### 3.3 Synchronization

| ID | Requirement | Priority |
|----|-------------|----------|
| FR-SYNC-001 | All 120 balls shall be synchronized to the global timeline such that, at any instant, every ball's executed position corresponds to the same global time within 1 ms. | MUST |
| FR-SYNC-002 | The system shall support buffered motion commands executed on the synced timeline. | MUST |
| FR-SYNC-004 | Inter-controller communication latency shall be ≤ 5 ms (worst case) at the system's chosen node count. | MUST |
| FR-SYNC-005 | The system shall compensate clock drift between controllers so that FR-SYNC-001 holds, without causing a visible position discontinuity. | MUST |

### 3.4 Communication

| ID | Requirement | Priority |
|----|-------------|----------|
| FR-COMM-004 | The system shall define message types for: move commands, status reports, homing, stop, pattern commands, global sync, error/diagnostic reports, and alive/heartbeat. | MUST |
| FR-COMM-005 | The control architecture shall be modular: the number of controller nodes, the balls per node, and the bus segmentation are design choices determined by the implementation. | MUST |
| FR-COMM-006 | The system shall provide a host interface for pattern upload and monitoring. The transport is a design choice. | SHOULD |
| FR-COMM-007 | Each controller shall have a unique, persistent identity that maps it to its grid position and to the balls it controls. The mapping shall survive power cycles and form part of the persistent configuration (IR-SOFT-003). | MUST |

### 3.5 Local Trajectories

| ID | Requirement | Priority |
|----|-------------|----------|
| FR-ARCH-001 | Each controller shall execute its trajectory locally at a rate sufficient to satisfy FR-SYNC-001 and FR-MOT-002. The master shall distribute motion parameters rather than per-ball position samples; controller grouping is modular. | MUST |

### 3.6 Monitoring & Diagnostics

| ID | Requirement | Priority |
|----|-------------|----------|
| FR-MON-001 | The system shall report per-ball position, velocity, and error status on request or on failure (e.g. carried in the alive/heartbeat message), not continuously. | MUST |
| FR-MON-003 | The system shall log error events (overcurrent, stall, communication loss) with timestamps on the global timeline. | MUST |
| FR-MON-004 | The system shall provide a monitoring interface for the operator. | SHOULD |
| FR-MON-005 | The system shall indicate controller health locally with a visible OK/fault indication. | MUST |

### 3.7 User Interface

| ID | Requirement | Priority |
|----|-------------|----------|
| FR-UI-001 | The system shall provide a PC-based pattern development environment: authoring, preview through the simulation, and upload to the installation. | MUST |
| FR-UI-002 | The system shall allow runtime adjustment of pattern parameters (speed, amplitude, frequency). | SHOULD |
| FR-UI-003 | The system shall provide a prominent hardware power-off stop button. | MUST |
| FR-UI-004 | The system shall support scheduled operation (start/stop times, pattern rotation). | OPTIONAL |

### 3.8 Simulation

| ID | Requirement | Priority |
|----|-------------|----------|
| FR-SIM-001 | The system shall provide a PC-based 3D simulation of the installation, in addition to the physical piece, that reproduces the ball motion from the same pattern/motion data for preview and development without the hardware. Simulated and real motion shall match within the visual limits. | SHOULD |

---

## 4. Non-Functional Requirements

### 4.1 Reliability

| ID | Requirement | Priority |
|----|-------------|----------|
| NFR-REL-001 | Mean time between failures (MTBF): ≥ 1000 hours. | SHOULD |
| NFR-REL-002 | The remaining balls shall continue operating if a controller fails (degraded mode). | SHOULD |
| NFR-REL-003 | Single controller failure shall not affect operation of other controllers. | MUST |
| NFR-REL-004 | Thread failure (breakage) on one ball shall not cause cascading failures. Detection of the breakage is not required. | MUST |
| NFR-REL-005 | Power loss recovery: the system shall re-home all balls on power restoration (per FR-MOT-010). | MUST |

### 4.2 Safety

| ID | Requirement | Priority |
|----|-------------|----------|
| NFR-SAFE-009 | The installation shall be inherently safe: no ball motion, drop, or component failure shall be able to injure a person. This is the top-level safety requirement and takes precedence over the operational stop functions. | MUST |
| NFR-SAFE-001 | The system shall contain any detached ball and its hardware within the piece, by secondary cables, an enclosure, or another means. The requirement is containment, not a particular device. | MUST |
| NFR-SAFE-004 | Per-motor overcurrent protection shall be provided. | MUST |
| NFR-SAFE-005 | The system shall include warning signage, and access interlocks that stop motion when an enclosure is opened. | MUST |
| NFR-SAFE-006 | The frame shall withstand the worst-case load — all 120 balls catching simultaneously (≈5.9 kN) plus static weight — with a safety factor of ≥ 2. | MUST |
| NFR-SAFE-007 | The system shall be grounded/earthed for electrical safety. | MUST |
| NFR-SAFE-010 | The system shall be designed against fire: the supply shall have overcurrent and short-circuit protection, and enclosure, cable, and decorative materials shall be fire-retardant where practical. Unattended operation shall only be permitted with these protections in place. | MUST |

### 4.3 Environmental

| ID | Requirement | Priority |
|----|-------------|----------|
| NFR-ENV-001 | Operating temperature: 10 °C – 40 °C. | SHOULD |
| NFR-ENV-002 | Operating humidity: 20 % – 80 % RH (non-condensing). | SHOULD |
| NFR-ENV-004 | Noise level: ≤ 45 dB(A) at 1 m. | SHOULD |

### 4.4 Maintainability

| ID | Requirement | Priority |
|----|-------------|----------|
| NFR-MAINT-001 | Individual ball/station replacement shall be possible without removing other balls. | SHOULD |
| NFR-MAINT-003 | Diagnostic mode shall allow individual ball testing. | SHOULD |
| NFR-MAINT-004 | Software update shall be possible in the installed position, without removing boards. A USB (or equivalent bootloader) path shall be available for bring-up. Updates shall have integrity verification and rollback on failure. | MUST |
| NFR-MAINT-005 | The spare-parts list shall be documented and commercially available. | MUST |
| NFR-MAINT-006 | A failed controller/station module shall be exchangeable within 10 minutes. | MUST |

### 4.5 Physical

| ID | Requirement | Priority |
|----|-------------|----------|
| NFR-PHYS-001 | Grid: 120 balls (count fixed). Arrangement and pitch are proposals pending designer confirmation — see NFR-PHYS-008 and ASM-009. | MUST |
| NFR-PHYS-002 | Total system height: ≥ 1200 mm (1 m travel + suspension + frame). | MUST |
| NFR-PHYS-003 | Ball diameter: ~23 mm (50 g solid polished steel sphere; Ø from V = m/ρ_steel). | MUST |
| NFR-PHYS-004 | Total weight of all balls: 120 × 50 g = 6 kg. | MUST |
| NFR-PHYS-007 | Position-scale variation over travel shall not exceed 50 µm per grid pitch (ASM-014), so height does not visibly distort the pattern. Single-layer winding or calibrated compensation are both acceptable means. | MUST |
| NFR-PHYS-008 | Ball pitch: 100 mm nominal in both axes (field ≈ 0.9 × 1.1 m centre-to-centre). This is an engineering proposal pending designer approval (ASM-009). | SHOULD |

### 4.6 Cost

| ID | Requirement | Priority |
|----|-------------|----------|
| NFR-COST-001 | Total parts/BOM cost for the installation and its PC tooling shall be ≤ 4,000 €, excluding labour and installation. | MUST |

---

## 5. Interface Requirements

### 5.1 Mechanical Interfaces

| ID | Requirement | Priority |
|----|-------------|----------|
| IR-MECH-002 | Suspension thread: breaking strength ≥ 3× the peak catch load (≥ 150 N), and elongation ≤ 100 µm at the maximum static load (≈ 0.5 N) over the suspension length. Material and diameter are design choices. | MUST |

### 5.2 Electrical Interfaces

| ID | Requirement | Priority |
|----|-------------|----------|
| IR-ELEC-001 | Motor supply voltage: 24 V DC nominal (preliminary, depends on motor selection; see ASM-007). | SHOULD |
| IR-ELEC-002 | Average system power consumption shall fit the available supply (design budget ≤ 1200 W; preliminary, depends on motor selection). | SHOULD |
| IR-ELEC-004 | The grounding and bonding scheme shall prevent ground loops and common-mode noise between motor power returns, controller logic ground, and the field-bus reference. Motor power and signal grounds shall be separated as the drive requires. | MUST |

### 5.3 Software Interfaces

| ID | Requirement | Priority |
|----|-------------|----------|
| IR-SOFT-003 | Configuration: persistent storage of grid layout and calibration data. | MUST |
| IR-SOFT-004 | Logging: event log accessible to the operator. | SHOULD |

---

## 6. Constraints

| ID | Constraint | Priority |
|----|-----------|----------|
| CON-001 | 120 balls and 1 m usable vertical travel are fixed; grid arrangement and pitch are proposals (ASM-009). | MUST |
| CON-002 | Ball mass: 50 g each — fixed. | MUST |
| CON-003 | Speed envelope: v(s) = √(2 a s), where s is the height above the lower extreme of travel. On free-fall descent a = g, giving v = 4.43 m/s at s = 1 m (stated as 4.5 m/s); for motor-driven motion a is the commanded acceleration. Commanded speed shall stay inside this envelope, and upward speed shall always allow braking within the remaining travel. | MUST |
| CON-006 | Exactly one position-reference endstop; its location (upper or lower) and implementation are design choices. The stop is the position reference. | MUST |
| CON-008 | The control architecture shall be modular; controller grouping, node count, and bus segmentation are design choices. | MUST |
| CON-009 | Indoor installation — no weatherproofing required. | MUST |
| CON-010 | Visual quality: motion must appear smooth to the human eye (no visible stepping). | MUST |

---

## 7. Assumptions

| ID | Assumption |
|----|-----------|
| ASM-001 | Balls are suspended from a rigid overhead frame. |
| ASM-003 | Air resistance is negligible at 4.5 m/s for a 23 mm sphere (drag force ≈2.4 mN, about 0.5 % of ball weight). |
| ASM-004 | Thermal effects on thread strength are within material limits at operating temperatures. |
| ASM-005 | The installation environment is stable (no significant vibration or air currents). |
| ASM-006 | Viewers are at 0.5–3 m distance from the sculpture. |
| ASM-007 | Power is available: 24 V DC, ~1200 W continuous. |
| ASM-008 | Braked catch distance for an intentional free-fall drop is 1 cm — an engineering proposal pending designer approval. |
| ASM-009 | Grid form and pitch are not yet fixed by the designer; a rectangular 10 × 12 layout at 100 mm pitch is an engineering proposal pending designer approval. |
| ASM-010 | Limiting downward acceleration to 0.9 g (T ≥ 0.1 mg) keeps the thread visibly taut during ordinary motion; to be verified by a visual test. The limit is waived only for the intentional free-fall drop. |
| ASM-011 | An S-curve jerk limit of ~500 m/s³ is assumed to appear smooth; to be verified by a visual test. |
| ASM-012 | A smooth transition from the old to the new pattern over ~1 s is assumed to look good and be acceptable for live changes; to be verified visually. |
| ASM-013 | A replacement module is available in stock, so module exchange time (≤ 10 min) is the binding recovery metric. |
| ASM-014 | Ball vertical displacements up to 50 µm are visually acceptable (imperceptible at the viewing distances of ASM-006); to be verified by a visual test. |

---

## 8. Verification & Test

| ID | Test | Verifies |
|----|------|----------|
| T-DROP-001 | Make the drop: execute an intentional free-fall descent and monitor the thread (visual and/or tension sensor). Confirm the thread stays taut during ordinary motion and goes slack only during the intended drop, and that the catch arrests the ball within 1 cm. | FR-MOT-003, FR-MOT-012, FR-MOT-013, ASM-008, ASM-010 |
| T-SMOOTH-001 | Run the fastest acceleration/deceleration transitions and observe visually that no jerk or stepping is perceived. | FR-MOT-002, FR-MOT-006, ASM-011 |
| T-FRAME-001 | Apply a static proof load of 2× the worst-case dynamic load (≈12 kN) to the frame and verify no failure or permanent deformation. | NFR-SAFE-006 |
| T-VISUAL-001 | Move a ball in 50 µm increments and observe from the closest viewing distance (0.5 m) whether the displacement is perceptible. | ASM-014, FR-MOT-002 |

---

## 9. Glossary

| Term | Definition |
|------|-----------|
| **Jerk** | Rate of change of acceleration; limited for visually smooth motion |
| **Quantization** | Smallest commanded position increment at the drum |
| **S-curve** | Jerk-limited motion profile with smooth acceleration transitions |

---

*Document version: 3.0*
