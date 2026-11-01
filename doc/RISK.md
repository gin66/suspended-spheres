# RISK.md — Hanging Balls Risk Register & Technical Review

> Detailed technical review and risk assessment for the Hanging Balls kinetic sculpture project.

---

## 1. Dynamic Load Analysis

### 1.1 Dynamic Load on Structural Frame

The design is now slow and the supporting structure is substantial steel beams, owned by the designer (`ASM-016`):

- **Static weight:** ≤ 120 × 50 g = 6 kg ≈ 59 N
- **Slow-motion dynamic load:** ≤ ≈ 0.52 N per ball → ≈ 62 N total (≈ 5 % above static)
- **No free-fall catch load** — free-fall is not part of the motion repertoire
- **Asymmetry:** uneven movement still produces modest torsional loading

> **Scope note:** structural design and verification are the designer's responsibility. The control system's job is to stay within the slow-motion load envelope.

### 1.2 Primary Technical Risk: Cumulative Noise Floor

The cumulative acoustic and mechanical noise from 100–120 motors is still a significant risk, though slow motion greatly reduces it compared with the earlier high-speed concept:

- Noise may radiate through the hollow ornaments and the mechanical enclosure
- Strategic structural and acoustic decoupling may mitigate (but not eliminate) the issue
- Predicting full-scale system behavior from a small demonstrator is inherently difficult
- **Fallback strategy:** If the noise floor proves unacceptable, explore **BLDC or PMSM motors** (potentially with the **SimpleFOC library**), which may run more quietly

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

Each controller runs on its own quartz oscillator. A consumer crystal is specified at ± 35 ppm; two crystals at opposite ends of that tolerance drift at up to 70 ppm relative to each other. At the fastest planned speed (0.3 m/s) this becomes a visible position error:

| Elapsed time | Worst-case relative drift (70 ppm) | Position error at 0.3 m/s |
|--------------|-----------------------------------|---------------------------|
| 1 minute | 4.2 ms | ~1.3 mm |
| 5 minutes | 21 ms | ~6.3 mm |

At the fastest planned speed (0.3 m/s) 21 ms of drift gives ≈ 6.3 mm after 5 minutes and ≈ 0.75 m after an hour — the latter exceeds the ~300 mm grid pitch, so wave patterns visibly break up. The drift is certain, not intermittent; without correction it is a synchronisation failure rather than a nuisance.

**Mitigation — not a different oscillator:**

- A global timebase (SYNC messages from the master) lets each controller measure its drift.
- Correct the drift by inserting timing pauses during rest periods, or by a slow clock trim when a pattern has no rest.
- A better oscillator (TCXO, ± 5 ppm) is an optional way to lengthen the interval between corrections, not a substitute for the sync method.
- Resync and verify alignment before a pattern starts; hold the ≤ 1 ms requirement in `FR-SYNC-001`.

---

## 3. Risk Register

| # | Risk | Severity | Likelihood | Mitigation |
|---|------|----------|------------|------------|
| 1 | **Structural failure** | Critical | Low | Designer-owned substantial steel structure; control loads are static/slow (≤ 62 N total) |
| 2 | **Cumulative motor noise floor** | High | Medium | Acoustic/structural decoupling; slow motion helps; fallback to BLDC/PMSM + SimpleFOC |
| 3 | **Motor selection for slow, quiet operation** | Low | Low | RPM/torque are trivial at 0.3 m/s; choose on noise, cost, and integration |
| 4 | **PC real-time limitation** | High | Certain | Offload all step generation to edge controllers; PC handles pattern scheduling only |
| 5 | **No open-source firmware scales to 120 steppers** | Medium | Certain | Custom firmware development required |
| 6 | **Power supply heat dissipation** | Medium | Medium | Slow motion reduces duty; air cooling and thermal modelling of driver arrays |
| 7 | **Current distribution across many motors** | Medium | Medium | Low-resistance bus design; voltage-drop analysis; local bulk capacitance |
| 8 | **Module interconnect reliability** | Medium | Medium | Minimize connectors; use board-level interconnects; gold-plated contacts |
| 9 | **ESP32 long-term stability (WiFi + CAN + stepper control)** | Medium | Medium | Watchdog management; task isolation; stress-test continuous operation |
| 10 | **X/Y pendulum swing from guide imperfections** | Low-Medium | Medium | Precision overhead guide design; transient swing modeling |
| 11 | **Loss of inter-controller synchronization from quartz drift** | High | High | Global timebase with periodic SYNC; measure and correct drift (rest pause or clock trim); optional TCXO; verify ≤ 1 ms before each pattern |
| 12 | **Grounding / EMI: ground loops and common-mode noise between motor power returns, controller ground, and bus reference** | High | Medium | Separate power and signal returns; single-point bonding; shielded or isolated bus; validate EMC; see `IR-ELEC-004` |
| 13 | **Fire from an electrical fault or overheated driver array** | High | Medium | Overcurrent/short-circuit protection, thermal design, fire-retardant enclosure/cable/decorative materials; see `NFR-SAFE-010` |
| 14 | **Position-scale error from multi-layer winding** | High | Medium | Level-wind single layer, or calibrate radius vs turns; second layer adds ≈4 % scale error (≈80 mm over the 2 m travel) vs the 50 µm/pitch limit (`NFR-PHYS-007`) |
| 15 | **Hollow ornament detachment or cracking** | Medium | Medium | Containment (`NFR-SAFE-001`); inspect ornaments; secure the thread attachment |

---

*This document is a living reference. Update it as design decisions evolve.*
