# RISK.md — Hanging Balls Risk Register & Technical Review

> Detailed technical review and risk assessment for the Hanging Balls kinetic sculpture project.

---

## 1. Dynamic Load Analysis

### 1.1 Dynamic Load on Structural Frame

When all 120 balls (50 g each) move simultaneously, the system generates significant dynamic loads on the structural frame:

- **Static weight:** 120 × 50 g = 6 kg ≈ 59 N
- **Worst-case dynamic load:** all 120 balls catching a free-fall drop together ≈ 120 × 49 N ≈ 5.9 kN — about 100× the static load
- **Torsional stress:** Uneven movement across columns induces significant torsional loading on the frame
- **Implication:** The structural design is critically important to handle these combined forces safely and reliably

> **Design imperative:** Structural integrity must be validated under worst-case asymmetric load scenarios, not just static weight.

### 1.2 Primary Technical Risk: Cumulative Noise Floor

The cumulative acoustic and mechanical noise from 120 steppers operating simultaneously is the primary technical risk:

- Noise may radiate through the spheres and the mechanical enclosure
- Strategic structural and acoustic decoupling may mitigate (but not eliminate) the issue
- Predicting full-scale system behavior from a small demonstrator is inherently difficult
- **Fallback strategy:** If the noise floor proves unacceptable, reconsider the actuation strategy — explore **BLDC or PMSM motors**, potentially controlled via the **SimpleFOC library**, which may offer quieter operation

---

## 2. Technical Review

> The following notes reflect a detailed technical review of the system architecture, informing risk assessment and mitigation.

### 2.1 Controller Architecture: Dedicated Boards vs. Off-the-Shelf

| Approach | Trade-offs |
|----------|------------|
| **3D printer controller boards** | Reduces integration effort to "just making connections"; however, must decide whether to use existing driver firmware or write custom. Custom firmware requires schematics review — some boards use unexpected I/O extenders to drive steppers, complicating direct control. |
| **Dedicated stepper boards plus a few Pico 2 boards** | Significantly more work; introduces reliability concerns (loose cables, bad connections). |
| **One custom PCB with 4/8/12 stepper drivers per board** | Best balance of integration and reliability. Reduces point-of-failure connections. |

### 2.2 PC Real-Time Limitation

- **A PC is not suitable for hard real-time step pulse generation** at kSteps/s rates across 120 motors.
- **Implication:** Motion planning must reside on the microcontrollers, or **precomputed (flashed) motion profiles** must be used.
- **Conclusion:** The PC's role is limited to high-level pattern scheduling and monitoring. All real-time step generation happens on the edge controllers.

### 2.3 Open-Source Firmware Ecosystem

- **No known open-source firmware/software currently scales to 120 steppers simultaneously.**
- This project will require custom firmware development for the controller layer.

### 2.4 Swinging / Pendulum Considerations

- **X/Y swinging** should be eliminated by the overhead guide from spool to ball.
- **Z-swinging (pendulum)** should not be expected if the vertical guide is well-designed.
- Motion speeds are not extremely fast, reducing pendulum excitation risk, but transient swings during acceleration/deceleration transitions must be modeled.
- **String guide recommendation:** Plan guides between the spool and the outlet so that the winding/unwinding string position changes do not create X/Y forces, which may contribute to pendulum-like oscillations.
- **Sync tolerance:** If all steppers start within milliseconds, this should be sufficiently precise. This is not an application where slight deviations from the linear path have an impact like FDM printing.

### 2.5 Communication Options — Comparative Assessment

| Option | Upgrade Path | Bandwidth | Time Sync | Verdict |
|--------|-------------|-----------|-----------|---------|
| **USB (25 connections to PC)** | Easy SW upgrade | Sufficient | May be insufficient for ms-level sync | ⚠️ Prototype only |
| **CAN bus** | SW upgrade tricky | Constrains control design | Well-established, deterministic | ✅ **Recommended** |
| **WiFi (ESP32)** | OTA possible | Sufficient | ms-level sync may be unreliable | ⚠️ Diagnostic/upgrade channel |

> **Recommendation:** CAN bus is the best balance of deterministic timing and bandwidth for 120-motor synchronization. WiFi (ESP32) is valuable for diagnostics and firmware upgrades but should not be the primary control channel.

### 2.6 Proof-of-Concept Strategy

**Step 1: Stress-test a single stepper** using FastAccelStepper (ESP32 or Pico):
- High-speed and high-acceleration testing determines whether NEMA-11 is sufficient (at what voltage) or whether NEMA-17 is required.
- **Critical unknown:** Maximum rotation speed of NEMA-11 is often unspecified in datasheets — must be empirically evaluated.
- This stress test answers the fundamental actuation question before any scaling.

### 2.7 Quartz Drift Between Controllers

Each column controller runs on its own quartz oscillator. A consumer crystal is specified at ± 35 ppm; two crystals at opposite ends of that tolerance drift at up to 70 ppm relative to each other. At 4.5 m/s this becomes a visible position error:

| Elapsed time | Worst-case relative drift (70 ppm) | Position error at 4.5 m/s |
|--------------|-----------------------------------|---------------------------|
| 1 minute | 4.2 ms | ~19 mm |
| 5 minutes | 21 ms | ~94 mm |

At the 21 ms figure the error is comparable to the 100 mm proposed grid pitch, so wave and interference patterns visibly break up. The drift is certain, not intermittent; without correction it is a synchronisation failure rather than a nuisance.

**Mitigation — not a different oscillator:**

- A global timebase (SYNC messages from the master) lets each controller measure its drift.
- Correct the drift by inserting timing pauses during rest periods, or by a slow clock trim when a pattern has no rest.
- A better oscillator (TCXO, ± 5 ppm) is an optional way to lengthen the interval between corrections, not a substitute for the sync method.
- Resync and verify alignment before a pattern starts; hold the ≤ 1 ms requirement in `FR-SYNC-001`.

---

## 3. Risk Register

| # | Risk | Severity | Likelihood | Mitigation |
|---|------|----------|------------|------------|
| 1 | **Structural failure under asymmetric dynamic load** | Critical | Medium | Validate frame under worst-case asymmetric loading; factor of safety ≥ 2 (see `NFR-SAFE-006`) |
| 2 | **Cumulative stepper noise floor** | High | High | Acoustic/structural decoupling; fallback to BLDC/PMSM + SimpleFOC |
| 3 | **NEMA-11 max RPM unspecified** | Medium | High | Empirical stress test in PoC phase |
| 4 | **PC real-time limitation** | High | Certain | Offload all step generation to edge controllers; PC handles pattern scheduling only |
| 5 | **No open-source firmware scales to 120 steppers** | Medium | Certain | Custom firmware development required |
| 6 | **Power supply heat dissipation** | High | High | Active air cooling design; thermal modeling of driver arrays |
| 7 | **High peak current distribution (120 motors)** | High | High | Low-resistance bus design; voltage drop analysis; local bulk capacitance |
| 8 | **Module interconnect reliability** | Medium | Medium | Minimize connectors; use board-level interconnects; gold-plated contacts |
| 9 | **ESP32 long-term stability (WiFi + CAN + stepper control)** | Medium | Medium | Watchdog management; task isolation; stress-test continuous operation |
| 10 | **X/Y pendulum swing from guide imperfections** | Low-Medium | Medium | Precision overhead guide design; transient swing modeling |
| 11 | **Loss of inter-controller synchronization from quartz drift** | High | High | Global timebase with periodic SYNC; measure and correct drift (rest pause or clock trim); optional TCXO; verify ≤ 1 ms before each pattern |
| 12 | **Grounding / EMI: ground loops and common-mode noise between motor power returns, controller ground, and bus reference** | High | Medium | Separate power and signal returns; single-point bonding; shielded or isolated bus; validate EMC; see `IR-ELEC-004` |
| 13 | **Fire from an electrical fault or overheated driver array** | High | Medium | Overcurrent/short-circuit protection, thermal design, fire-retardant enclosure/cable/decorative materials; see `NFR-SAFE-010` |
| 14 | **Position-scale error from multi-layer winding** | High | Medium | Level-wind single layer, or calibrate radius vs turns; second layer adds ≈4 % scale error (≈40 mm over travel) vs the 50 µm/pitch limit (`NFR-PHYS-007`) |
| 15 | **Free-fall catch overloads thread/motor** | High | Medium | ≈49 N (≈153 N with inertia) per catch; provide a brake/energy absorber, verify thread ≥150 N (`IR-MECH-002`) and frame at ≈5.9 kN (`NFR-SAFE-006`) |

---

*This document is a living reference. Update it as design decisions evolve.*
