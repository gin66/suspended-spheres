# RISK.md — Suspended Spheres Risk Register & Technical Review

> Detailed technical review and risk assessment for the Suspended Spheres kinetic sculpture project.

---

## 1. Dynamic Load Analysis

### 1.1 Dynamic Load on Structural Frame

The design is now slow and the supporting structure is substantial steel beams, owned by the designer (`ASM-016`):

- **Static weight:** ≤ 120 × 50 g = 6 kg ≈ 59 N
- **Nominal dynamic load:** ≤ ≈ 0.57 N per ball → ≈ 68 N total (≈ 15 % above static)
- **Optional 1.2 m/s tier:** ≤ ≈ 0.69 N per ball → ≈ 83 N total (≈ 40 % above static)
- **No free-fall catch load** — free-fall is not part of the motion repertoire
- **Asymmetry:** uneven movement still produces modest torsional loading

> **Scope note:** structural design and verification are the designer's responsibility. The control system's job is to stay within the slow-motion load envelope.

### 1.2 Primary Technical Risk: Cumulative Noise Floor

The cumulative acoustic and mechanical noise from 100–120 motors is still a significant risk. Most choreography is slow, but the nominal 0.6 m/s tier and the optional 1.2 m/s effects raise motor RPM and therefore noise. A 40 mm drum keeps the optional tier at ~573 RPM instead of ~1,146 RPM on a 20 mm drum, which is the main acoustic lever:

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
| **WiFi, grouped nodes (8–30) + broadcast beacons** | OTA possible | Sufficient (720 B/window) | Common broadcast + receive timestamp; FTM if needed | ✅ **Baseline** |

> **Recommendation:** WiFi is sufficient for this application when controllers are grouped (8–30 nodes), commands are buffered 20–100 ms ahead, and a numbered broadcast is timestamped on reception. The hardwired E-stop is independent of the network.

### 2.6 Proof-of-Concept Strategy

**Step 1: Stress-test a single stepper** using FastAccelStepper (ESP32 or Pico):
- Test at the optional 1.2 m/s tier (≈ 573 RPM on a 40 mm drum) and at the nominal 0.6 m/s tier; this determines whether NEMA-11 is sufficient (at what voltage) or whether NEMA-14/17 is required.
- **Critical unknown:** Maximum rotation speed and torque at ~900 RPM are often unspecified in datasheets — must be empirically evaluated with the chosen drum.
- This stress test answers the fundamental actuation question before any scaling.

### 2.7 Quartz Drift Between Controllers

Each controller runs on its own quartz oscillator. A consumer crystal is specified at ± 35 ppm; two crystals at opposite ends of that tolerance drift at up to 70 ppm relative to each other:

| Elapsed time | Worst-case relative drift (70 ppm) | Error @ 0.6 m/s | Error @ 1.2 m/s |
|--------------|-----------------------------------|-----------------|-----------------|
| 1 minute | 4.2 ms | 2.5 mm | 5.0 mm |
| 5 minutes | 21 ms | 12.6 mm | 25.2 mm |
| 1 hour | 252 ms | 151 mm | 302 mm |

At the optional 1.2 m/s tier, 21 ms of drift gives ≈ 25 mm after 5 minutes and ≈ 302 mm after an hour — about one grid pitch. The drift is certain, not intermittent; without correction it is a synchronisation failure rather than a nuisance. (The earlier 2.5 s/hour figure was 10× too high: 70 ppm × 3600 s = 0.252 s.)

**Mitigation — not a different oscillator:**

- A global timebase (SYNC messages from the master) lets each controller measure its drift.
- Correct the drift by inserting timing pauses during rest periods, or by a slow clock trim when a pattern has no rest.
- A better oscillator (TCXO, ± 5 ppm) is an optional way to lengthen the interval between corrections, not a substitute for the sync method.
- Resync and verify alignment before a pattern starts; hold the ≤ 1 ms requirement in `FR-SYNC-001`.

---

## 3. Risk Register

| # | Risk | Severity | Likelihood | Mitigation |
|---|------|----------|------------|------------|
| 1 | **Structural failure** | Critical | Low | Designer-owned substantial steel structure; control loads are static/slow (≤ 83 N total) |
| 2 | **Cumulative motor noise floor** | High | Medium | Acoustic/structural decoupling; the 40 mm drum keeps 1.2 m/s at ~573 RPM; fallback to BLDC/PMSM + SimpleFOC |
| 3 | **Motor selection for the fast tier** | Low | Low | Torque is trivial across the envelope; verify torque and noise at ~900 RPM for the 1.2 m/s tier; 40 mm drum lowers RPM |
| 4 | **PC real-time limitation** | High | Certain | Offload all step generation to edge controllers; PC handles pattern scheduling only |
| 5 | **No open-source firmware scales to 120 steppers** | Medium | Certain | Custom firmware development required |
| 6 | **Power supply heat dissipation** | Medium | Medium | Slow motion reduces duty; air cooling and thermal modelling of driver arrays |
| 7 | **Current distribution across many motors** | Medium | Medium | Low-resistance bus design; voltage-drop analysis; local bulk capacitance |
| 8 | **Module interconnect reliability** | Medium | Medium | Minimize connectors; use board-level interconnects; gold-plated contacts |
| 9 | **ESP32 long-term stability (WiFi + FTM + stepper control)** | Medium | Medium | Watchdog management; task isolation; stress-test continuous operation with WiFi traffic and I2S DMA |
| 10 | **X/Y pendulum swing from guide imperfections** | Low-Medium | Medium | Precision overhead guide design; transient swing modeling |
| 11 | **Loss of inter-controller synchronization from quartz drift** | High | High | Global timebase with periodic SYNC; measure and correct drift (rest pause or clock trim); optional TCXO; verify ≤ 1 ms before each pattern |
| 12 | **Grounding / EMI: ground loops and common-mode noise between motor power returns, controller ground, and bus reference** | High | Medium | Separate power and signal returns; single-point bonding; shielded or isolated bus; validate EMC; see `IR-ELEC-004` |
| 13 | **Fire from an electrical fault or overheated driver array** | High | Medium | Overcurrent/short-circuit protection, thermal design, fire-retardant enclosure/cable/decorative materials; see `NFR-SAFE-010` |
| 14 | **Position-scale error from multi-layer winding** | High | Medium | Level-wind single layer, or calibrate radius vs turns; second layer adds ≈4 % scale error (≈80 mm over the 2 m travel) vs the 50 µm/pitch limit (`NFR-PHYS-007`) |
| 15 | **Hollow ornament detachment or cracking** | Medium | Medium | Containment (`NFR-SAFE-001`); inspect ornaments; secure the thread attachment |
| 16 | **Step-rate ceiling at the optional 1.2 m/s tier** | Medium | Medium | ESP32 I2S mux tops out at 40 kHz; 40 mm drum at 1/16 gives 30.6 kHz (1.31× margin). Benchmark before committing; a 30 mm drum misses 1.2 m/s by ~2 %, and 1/8 at 30 mm fails the 50 µm resolution |


---

*This document is a living reference. Update it as design decisions evolve.*
