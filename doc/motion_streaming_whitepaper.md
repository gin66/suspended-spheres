# Motion Streaming White Paper — Fly-Micro4 (RP2040) + PC Simulation

> **Status: motion data plane, for review.** The node is selected: Mellow
> Fly-Micro4 V1.0 (`controller_analysis.md`). This paper defines the motion
> payload, the RP2040 implementation strategy, and the PC simulation /
> pattern-authoring tool. It is a working document, not a requirement set.
> CAN-ID allocation, node identity and commissioning are in
> `Fly_Micro4_addressing.md` and are not repeated here. Requirements remain in
> `system_requirements.md`; physics remains in `mechanical_analysis.md`.

---

## 1. Executive summary

The selected node is an **RP2040 with 4 × TMC2209 and a CAN 2.0B (classic CAN,
1 Mbit/s) interface implemented in PIO**. That replaces the earlier WiFi
baseline in `controller_analysis.md`. It is a better real-time platform, and
it has a hard bandwidth limit.

**Key findings**

1. **The message rate is `N·Δt` per motor, with `Δt` configurable.** A message
   carries `N` future positions spaced by `Δt` (e.g. 10, 11 or 15 ms), with
   either `N=3` plus the end speed or `N=4` positions. The message period is
   `N·Δt` and the per-motor rate is `1/(N·Δt)`. At 120 motors the bus sees
   `120/(N·Δt)` frames/s:
   `N=3` → 4,000 / 3,636 / 2,667 frames/s at Δt = 10 / 11 / 15 ms
   (**~52 / 47 / 35 %** load); `N=4` → 3,000 / 2,727 / 2,000 frames/s
   (**~39 / 35 / 26 %**). The familiar 30/40 ms cases are `Δt=10 ms`. All fit;
   a 10 ms *message* rate would not (156 %).
2. **The frame carries the cubic, not samples of it.** Four int16 fields —
   position, speed, acceleration, jerk at the start of the segment — are
   exactly 8 B. The segment lasts `N·Δt`. The node evaluates that polynomial.
   It does not fit a Hermite or Catmull-Rom curve, and it does not need the
   previous message to learn the tangent. Position is **16-bit full-range**
   (code 0 = 0 mm, code 65535 = 2000 mm). Speed is **0.04 mm/s** (int16,
   ±1310 mm/s).
3. **Absolute positions, not deltas, are the robust choice.** A lost message
   leaves a bounded gap; the next message's absolute values re-anchor without
   accumulated error, and the node can decelerate/hold and resume cleanly.
   Delta coding is more compact, but a single loss corrupts every later value
   unless a re-anchor protocol is added.
4. **The node should play back a trajectory, not plan one.** The designer tool
   owns the artistic and kinematic profile (including S-curves/jerk limits).
   The node evaluates the streamed polynomial and feeds the step generator.
   Do not use `moveTo()` or the FastAccelStepper ramp at each segment;
   restarting a ramp there plans a new rest-to-rest move instead of playing
   the curve the master already computed.
5. **FastAccelStepper is a good fit at the queue level.** Use its Pico PIO
   driver plus `FasTimed` (multi-axis timed chunks). Do **not** use `FasNAxis`
   for the grid: the balls are independent 1-D axes, not a coordinated
   N-dimensional path. The high-level ramp API is also unsuitable. A custom PIO
   player is unnecessary for 4 axes.
6. **There is a hard RP2040 resource limit.** can2040 uses one PIO block;
   FastAccelStepper claims the other (its full program memory plus one SM per
   axis). That leaves **4 state machines = 4 steppers per board** — exactly the
   Fly-Micro4's 4 drivers. The Fly-Micro4 is fixed at 4 axes; this is the hard
   ceiling for the chosen board.
7. **The RP2040 has no FPU.** Put the interpolation and step conversion in
   integer/fixed-point code. Use the *same* portable C++ core in the firmware,
   in host tests, and compiled to WebAssembly for the simulator. This is the
   cheapest way to guarantee that the designer's preview and the hardware
   replay the same trajectory.
8. **The simulation should be OS-agnostic, and it should be a live view of a file.** A browser app (TypeScript +
   Three.js) with the shared core compiled to WASM runs on macOS, Windows and
   Linux without an installer. It watches an animation-data file and rebakes
   the running preview when that file changes. A small local bridge (Python/Node + `python-can`)
   handles USB-CAN playback. The designer, or an agentic coder, edits the file
   or a Python script that writes it. The viewer does not host a model.

**Recommended baseline**

| Item | Baseline |
|------|----------|
| Node | **Selected:** Mellow Fly-Micro4 V1.0, RP2040, 4 × TMC2209, can2040 |
| CAN | Classic CAN 2.0B, 1 Mbit/s. Two 120 Ω ends: Waveshare CAN jumper and the last Fly-Micro4 with J3 inserted; all other Fly boards leave J3 open (no 120 Ω). R39/R58 stay fitted — no desoldering |
| Sample interval | `Δt` configurable: 10, 11, 15 ms (default 10 ms) |
| Wire format | 1 frame per motor: position, speed, acceleration, jerk at the segment start; message period `N·Δt` |
| Lookahead | One segment of length `N·Δt`. The message carries the whole polynomial |
| Node playback | Integer Horner of that polynomial. No curve fit on the node |
| Step generation | FastAccelStepper Pico PIO + `FasTimed`, 2 ms chunks |
| Microstepping | **TBD.** Figures that quote 25.46 steps/mm or 39.3 µm are the 1/16 worked case at 40 mm |
| Sync | Absolute timeline in a periodic sync frame; 20–100 ms is ample |
| PC tool | Shared C++ core → WASM; live view of an animation file; Three.js 3-D view |
| Authoring | Human or agent edits the animation file, or a Python script that writes it |

The rest of this paper justifies these choices and defines the interfaces.

---

## 2. Scope and constraints

### 2.1 System recap

- 100–120 balls, 10 × 10 or 10 × 12, ~2.0 m usable travel.
- Nominal fast tier 0.44–0.59 m/s; optional short effects up to ~1.2 m/s;
  most choreography is much slower.
- Motion must look smooth and stay synchronized within 1 ms
  (`FR-SYNC-001`). Position quantization at the drum should be ≤ 50 µm
  (`FR-MOT-002`).
- 40 mm drum on the stepper shaft (no gear), 200-step motors.
  Microstepping is **TBD**. The 1/16 worked case is **25.46 steps/mm**,
  **39.3 µm per microstep**, and **30.6 kstep/s** at 1.2 m/s
  (`mechanical_analysis.md` §4).
- 4 axes per Fly-Micro4 → **30 nodes** for 120 balls.

### 2.2 Fly-Micro4 hardware reality

| Item | Consequence |
|------|-------------|
| RP2040, dual Cortex-M0+ @ 133 MHz | **No FPU**; integer/fixed-point hot path |
| No CAN peripheral | can2040 (PIO + CPU) implements **CAN 2.0B**, up to 1 Mbit/s |
| MCP2542 transceiver | **Physical layer only**: differential driver/receiver, no protocol controller, filters or buffers. can2040 is the controller. **Not CAN-FD** with can2040 |
| 2 × PIO blocks, 4 SMs each | can2040 and FastAccelStepper each consume a whole block's program memory; can2040 uses all 4 SMs, FastAccelStepper one SM per axis → **4 axes** |
| 4 × TMC2209, step/dir | No on-driver trajectory ramp (unlike TMC5160); MCU generates steps |
| 4/4 DIAG routed (Drive0–3: GPIO13/14/15/12) | Sensorless homing and stall/overtemperature events; **GPIO12 is shared with the official Z endstop** |
| 2-pin CAN, no GND, jumper-selectable termination | CANH/CANL, no GND. Terminate the two ends only, as in the baseline table: J3 inserted on the last board, open elsewhere. Common 24 V star ground |
| USB-C BOOTSEL/UF2 | Easiest field recovery of the surveyed boards |

> **CAN-FD is not a Fly-Micro4 option.** The MCP2542 is only the transceiver;
> the RP2040 has no CAN peripheral and the available stack is can2040, which is
> classic CAN 2.0B only. An “FD” transceiver variant would not add a protocol
> controller. The board is fixed; do not size the protocol on CAN-FD.

### 2.3 What “hypnotic” implies

Slow motion makes **position resolution** more important than raw update rate.
At 10 mm/s a 10 ms sample advances only 0.1 mm; one 16-bit code step over 2 m
(0.0305 mm) is 31 % of that. The model is **one vertical axis per ball**. Lateral swing
is not a design mode and is not modelled: the mechanical design fixes the
take-off point and guides the thread so winding does not introduce lateral
forces (`mechanical_analysis.md` §9, `RISK.md` §2.4), and `ASM-005` assumes a
stable environment. The visibility argument therefore rests on the 50 µm /
step-size analysis and the TMC2209 MicroPlyer, not on a pendulum low-pass. The
motor can still turn a coarse position grid into audible 100 Hz ripple, so the
design uses the 16-bit full-range encoding and measures the slowest intended
speed before committing.

The motion is also **not** free-fall and never slack: downward acceleration
stays below `g`, and the S-curve/jerk limits are part of the pattern, not the
node. That separation is what allows the node to be a simple, deterministic
player.

---

## 3. Analysis of the proposed message

This section is the sample-message study. The bus-rate numbers still apply:
one 8-byte frame per motor per `N·Δt`. The payload those frames carry is the
coefficient layout in §4.2, not the position samples analysed here.

### 3.1 The message as stated

Parameters:

- `Δt`: configurable sample interval, e.g. 10, 11 or 15 ms.
- `N`: number of future positions per message. `N=3` plus the signed speed at
  the end, or `N=4` positions.
- Message period = `N·Δt`; per-motor rate = `1/(N·Δt)`.

Per motor, one message every `N·Δt`:

- **N=3 mode:** positions at `T+Δt`, `T+2Δt`, `T+3Δt` plus the signed speed at
  `T+3Δt`.
- **N=4 mode:** positions at `T+Δt`, `T+2Δt`, `T+3Δt`, `T+4Δt`.

At `Δt=10 ms` these are the familiar 30 ms and 40 ms messages. At `Δt=11 ms`
they are 33/44 ms; at `Δt=15 ms`, 45/60 ms. The node receives `N` samples at
once and interpolates at its own control rate; the bus sees one message per
motor per `N·Δt`.

### 3.2 Units and ranges

| Field | Proposed | Issue | Recommendation |
|-------|----------|-------|----------------|
| Position | 0.1 mm (100 µm) | Coarser than a 1/16 step at 40 mm (39.3 µm; microstep count still TBD) and equal to the 50 µm visual threshold; leaves no margin for winding/scale error | **16-bit full-range 0–2000 mm** (code 65535 = 2000 mm; LSB 0.0305 mm) |
| Position range | 0.1 mm × 16 bit = 6.55 m | Fine, but coarse | 16-bit full-range = 2.000 m |
| Speed | 0.02 mm/s × int16 = ±655 mm/s | Does **not** cover the optional 1.2 m/s tier | **0.04 mm/s** LSB (±1310 mm/s) or 0.05 (±1638 mm/s) |
| Speed resolution | 0.02 mm/s | Fine, but only if the range is fixed | 0.04 mm/s still resolves slow motion well |
| Message size | 3 positions + speed | Exactly **8 B** | Fits one classic CAN frame |
| Message size | 4 positions | Exactly **8 B** | Fits one classic CAN frame |

**Why 16-bit full-range and 0.04 mm/s?** The position is encoded as a fraction
of full scale, not as a fixed millimetre LSB:

```
z = code / 65535 × 2000 mm      code 0 = 0 mm, code 65535 = 2000 mm
LSB = 2000 / 65535 = 0.0305 mm  (0.0015 % of full scale)
```

This is the finest 16-bit encoding that covers the 2.0 m travel. The speed is
`0.04 mm/s` because `0.04 × 32 767 = 1310 mm/s` covers the optional 1.2 m/s
tier; a 0.03 mm/s LSB would span only ±983 mm/s. A 0.02 mm position LSB would
span only 1.31 m.

The position encoding is the most consequential choice. The 16-bit full-range
code gives a 0.0305 mm LSB at 2 m, finer than a 1/16 step at 40 mm
(39.3 µm; the microstep count is still TBD); the node's
interpolation supplies sub-step values and the TMC2209 MicroPlyer smooths them.
A 0.01 mm grid would need 20 bits per position and no longer fits three
positions plus a speed in 8 B; the 16-bit full-range encoding is the limit.

### 3.3 CAN bandwidth

A classic-CAN standard frame with 8 data bytes is about **111 bits**, or
~**130 bits including worst-case stuffing**. At 1 Mbit/s that is ~130 µs per
frame, a ceiling of roughly **7,700 frames/s** and about **61 kB/s of payload**
after framing overhead. Practical designs should stay below ~60–70 % load.

| Mode | `Δt` (ms) | Period (ms) | Per-motor rate (Hz) | Frames/s (120 motors) | Bus load @ 1 Mbit/s | With sync/status |
|------|-----------|-------------|---------------------|-----------------------|---------------------|------------------|
| **N=3 + speed** | **10** | 30 | 33.3 | **4,000** | **52 %** | **53 %** |
| N=3 + speed | 11 | 33 | 30.3 | 3,636 | 47 % | 48 % |
| N=3 + speed | 15 | 45 | 22.2 | 2,667 | 35 % | 36 % |
| **N=4 positions** | **10** | 40 | 25.0 | **3,000** | **39 %** | **40 %** |
| N=4 positions | 11 | 44 | 22.7 | 2,727 | 35 % | 36 % |
| N=4 positions | 15 | 60 | 16.7 | 2,000 | 26 % | 27 % |
| 10 ms *message* (counterfactual) | 10 | 10 | 100 | 12,000 | 156 % | — |

`Δt` is the free variable. At `N=3`, `Δt=10 ms` is 52 % before overhead,
`Δt=11 ms` is 47 %, `Δt=15 ms` is 35 %. `N=4` at `Δt=10 ms` is 39 %. The
“with sync/status” column adds a 50 Hz sync and 30 × 1 Hz status (80
frames/s). Stay below ~60 %: `N=3`/`Δt=10` is workable but the tightest; a
larger `Δt` or `N=4` gives margin. Increasing `Δt` reduces bus and CPU load but
coarsens the trajectory sampling; the node’s cubic interpolation and the slow
motion tolerate 11–15 ms, but this is a measured choice.

**Per-node alternative** (not the baseline): A = 4 × 16-bit full-range at
10 ms, one frame per node (3,000 frames/s, 39 %). It gives node-level
atomicity but does not change the fundamental load.

**CAN-FD** would make a 10 ms message trivial, but the Fly-Micro4 is classic
CAN only. The design uses `N·Δt` with the fixed board.

### 3.4 Lookahead and interpolation

One message delivers `N` samples over `N·Δt`. In the `N=3` + speed mode, the
node also keeps the **previous** message, so it has the start state `P(T), V(T)`
and the end state `P(T+3Δt), V(T+3Δt)` for a cubic Hermite over the whole
`3Δt`. In the `N=4` mode, the previous message supplies `P(T)` and the current
message supplies `P(T+Δt) … P(T+4Δt)`; the five points give a Catmull-Rom
segment, and the boundary tangent at `T+4Δt` uses the next message's first
position when it arrives.

Buffering two messages gives a **`N·Δt` future segment** plus the previous
segment's start state; the total buffered span is `2·N·Δt`. That
absorbs CAN jitter and a late message. A *lost* message leaves a 40/50 ms gap
before the next message's first sample; the node decelerates at the end of the
known segment and holds. It does not jump to the next sample.

The node should **not** repeatedly reset its interpolator to the current step
position. That turns the stream into a tracking loop and injects quantization
noise and following-error corrections into the visible motion. The stepper is
open-loop; the step counter is the commanded position. Evaluate the absolute
trajectory and use the step counter only to detect a real fault (lost steps,
stall), not to re-phase every 10 ms.

### 3.5 Verdict

| Question | Answer |
|----------|--------|
| Is the `N·Δt` message rate feasible? | ✅ `N=3`: 52/47/35 % at Δt = 10/11/15 ms; `N=4`: 39/35/26 % |
| Is `Δt` configurable? | ✅ Yes; the bus rate and load scale as `120/(N·Δt)` |
| Is a 10 ms *message* rate feasible? | ❌ No: ~156 % |
| Can 3 positions + speed fit in one frame? | ✅ Exactly 8 B |
| Can 4 positions fit in one frame? | ✅ Exactly 8 B |
| Is 0.1 mm position resolution acceptable? | ❌ Use 16-bit full-range 0–2000 mm (code 65535 = 2000 mm) |
| Is 0.02 mm/s speed range enough? | ❌ ±655 mm/s; use 0.04 mm/s (±1310 mm/s) |
| Is the end-speed field useful? | ✅ Preferred in `N=3`: it makes the message boundary C1 without waiting for the next message |

---

## 4. Recommended motion data plane

### 4.1 Design principles

1. **Absolute, time-stamped samples.** Every position is an absolute value on
   the global timeline. A lost frame is a bounded gap, not a corrupted running
   sum.
2. **The master owns the trajectory.** The PC/embedded master generates
   positions (with S-curves, blends, artistic logic). The node owns faithful
   interpolation and step generation.
3. **The node is open-loop.** No encoder. The step counter is the commanded
   position. Stall and lost-step detection are separate fault paths.
4. **Buffered, not immediate.** The node executes `N·Δt` behind the master so
   CAN jitter and a lost message do not reach the motors.
5. **One authoritative core.** The same integer interpolation/step code runs on
   the RP2040, in host tests, and in the simulation (WASM).

### 4.1a Relationship to the architecture requirements

`FR-ARCH-001` requires each ball's trajectory to be executed locally at a rate
sufficient to meet `FR-SYNC-001` and `FR-MOT-002`, independent of the master's
command rate. It does **not** prescribe the payload format. `FR-ARCH-002`
requires the command load to be independent of the local control/step rate;
`NFR-REL-006` covers master/network loss.

The sample layouts studied in §3 meet those outcomes, and so does the
coefficient frame in §4.2. The coefficient frame is the baseline. The node
evaluates it at its own rate (1 kHz, or 2 ms chunks). The network rate is
`1/(N·Δt)` per motor and is independent of the control rate. On message loss,
the node decelerates, holds, and keeps its step position; the next absolute
`p0` re-anchors (`NFR-REL-006`).

The payload format is a design choice; the requirements constrain the
outcomes, not the encoding.

### 4.2 Baseline wire format — one cubic segment per motor

One classic CAN data frame per motor per segment. The segment length is
`N·Δt` from the sync frame. `Δt` and `N` change only at a pattern boundary,
not mid-segment. CAN ID type 0 (`Fly_Micro4_addressing.md`). Little-endian:

```
[ p0 : u16 ][ v0 : s16 ][ a0 : s16 ][ j : s16 ]
```

| Field | Meaning at τ = 0 | Unit |
|-------|------------------|------|
| `p0` | absolute position | full-range code, 0 = 0 mm, 65535 = 2000 mm |
| `v0` | speed | 0.04 mm/s, ±1310 mm/s |
| `a0` | acceleration | 1 mm/s², ±32 767 mm/s² |
| `j` | jerk, constant over the segment | 1 m/s³, ±32 767 m/s³ |

`τ` is microseconds since the segment start `T`. The polynomial is

```
p(τ) = p0 + v0·τ + a0·τ²/2 + j·τ³/6
```

Positive position, speed, and acceleration are upward. The node converts
`p0` with `code × 2 000 000 / 65 535` and the other three fields by their
LSBs, then evaluates with the integer Horner form in §4.7.

A lost frame is still a bounded gap: the next message carries an absolute
`p0`, not a delta from the previous sample. The master chooses `p0`, `v0`,
and `a0` so the join matches the end of the previous segment. Matching `a0`
to `a0 + j·Tseg` makes the join C2. The node checks the join; it does not
invent the coefficients.

§3 records the earlier idea of shipping three or four position samples and
fitting a cubic on the node. That fit is retired; `lib/motion_core` packs
and evaluates the four-field frame in §4.7.

**Resolution check:** one full-range code step is 0.0305 mm = 30.5 µm at 2 m,
finer than a 1/16 step at 40 mm (39.3 µm) and well under the 50 µm
requirement (quantization ±15.3 µm). The microstep count is still TBD: a
1/32 step at 40 mm is 19.6 µm, smaller than this LSB. The polynomial
evaluation supplies the positions between the 2 ms chunks; the TMC2209
MicroPlyer can smooth the steps once the input microstep count is chosen.

**Range check:** the coordinate is exactly the usable **0–2000 mm** travel.
The home position and safe offset must lie inside it. Homing may search
outside this coordinate using the local step counter; the 16-bit wire
coordinate is for normal playback only. There is no spare range in the field,
so normal motion must not exceed 2000 mm; a longer mechanical travel would
need a re-based coordinate or a coarser LSB.

**Changing `Δt` at runtime:** the node uses the new interval for the next
segment. The master must not change `Δt` while a segment is in flight. Because
`Δt` is a timeline property, the sync/window header or the persistent
configuration must identify the active value.

### 4.3 Alternative packing — per-node atomicity

If fewer frames/IRQs and node-level atomicity are preferred, pack the four axes
into one frame:

| Packing | Cadence | Frames/node | Frames/s (30 nodes) | Bus load | Resolution |
|---------|---------|-------------|---------------------|----------|------------|
| A: 4 × 16-bit full-range | 10 ms | 1 | 3,000 | 39 % | 0.0305 mm at 2 m |

- **A** is the simplest per-node format; velocity is derived from the three
  buffered frames.
- This is an alternative, not the baseline. The per-motor baseline has the
  same bus load at `N·Δt` and keeps each message self-contained.

### 4.4 Message framing

The motion payload is §4.2. Which CAN ID a frame uses, which four motors a
node owns, and how a board is commissioned are in `Fly_Micro4_addressing.md`.

- A motion message is one 8-byte classic CAN frame per motor per `N·Δt`.
- The payload is only the samples in §4.2.
- Sync, status and command payloads are in §4.5. Their type codes are in the
  addressing document.

### 4.5 Sync, status and commands

- **Sync / window header** (broadcast, one per motion window or 20–100 ms).
  One 8-byte frame, field 0, type 2, little-endian:

  | Offset | Type | Field |
  |--------|------|--------|
  | 0 | `uint32` | `t_us_low`, low 32 bits of the master time in µs |
  | 4 | `uint16` | `dt_us` (10 000, 11 000, or 15 000) |
  | 6 | `uint8` | `n` (3 or 4) |
  | 7 | `uint8` | `seq`, increments once per sync, wraps at 256 |

  The core timeline is still `int64` µs. The node recovers the high half by
  counting wraps of `t_us_low` (one wrap is about 71.6 min). A header at the
  start of each `N·Δt` window gives the nominal `T` for the following motion
  frames. Identifiers stay 11-bit. Nodes timestamp the start-of-frame with a
  free-running 1 MHz timer and fit offset/rate.
- **Status** (node → master, 1 Hz, plus immediately on a state or fault
  change). One 8-byte frame, type 3, little-endian:

  | Offset | Type | Field |
  |--------|------|--------|
  | 0 | `uint8` | `state`: 0 idle, 1 homing, 2 primed, 3 playing, 4 holding, 5 fault |
  | 1 | `uint8` | `fault_flags`: bit0 sample timeout, bit1 sync lost, bit2 out of range, bit3 acceleration exceeded, bit4 underrun, bit5 following error, bit6 reset |
  | 2–5 | `uint8` × 4 | following-error bucket per axis, in steps, saturated at 255 |
  | 6 | `uint8` | `buffer_depth`, queued motion messages, saturated at 255 |
  | 7 | `uint8` | `last_seq`, low 8 bits of the last accepted sync `seq` |

  On request or on failure, a separate command response carries per-ball
  position and velocity (`FR-MON-001`). The status frame never does.
- **Health / temperature** (node → master, 1 Hz). One 8-byte frame, type 14,
  field = the answering board ID (0–29; field 127 is invalid). Little-endian:

  | Offset | Type | Field |
  |--------|------|--------|
  | 0 | `int8` | `mcu_temp_c`, RP2040 die temperature in °C; ignored when `valid_mask` bit 0 is clear |
  | 1 | `uint8` | `otpw_mask` bits 0–3 (1 = driver pre-warning), `ot_mask` bits 4–7 (1 = driver shutdown) |
  | 2 | `uint8` | `diag_mask` bits 0–3 (1 = DIAG asserted) per driver; bits 4–7 reserved |
  | 3 | `int8` | `ntc_t0_c`, external T0 NTC in °C; valid only when `valid_mask` bit 1 is set |
  | 4 | `int8` | `ntc_bed_c`, external bed NTC in °C; valid only when `valid_mask` bit 2 is set |
  | 5 | `uint8` | `valid_mask`: bit0 MCU temp, bit1 T0 NTC, bit2 bed NTC, bit3 `DRV_STATUS` read fresh; bits 4–7 reserved |
  | 6 | `uint8` | `seq`, increments once per health frame, wraps at 256 |
  | 7 | `uint8` | reserved, 0 |

  The frame is periodic telemetry, not an alarm path. A TMC over-temperature
  pre-warning asserts DIAG immediately; the DIAG handler and an immediate
  status update carry the fault. The health frame is for logging, trend and the
  panel’s thermal display. It is sent only after the board has a board ID.
  Nodes phase-offset the 1 Hz period by `33 ms × board_id` so 30 health frames
  do not arrive as a burst. The detailed `DRV_STATUS` is read on demand or
  logged when an `otpw`/`ot` bit changes; this frame carries the
  pre-warning/shutdown masks, not the threshold bits. The node firmware
  fills `mcu_temp_c`, `diag_mask`, `otpw_mask`, `ot_mask` and `valid_mask`
  bit 3 from the TMC2209 UART driver (§5.6.1). `DRV_STATUS` is read at boot
  and on a DIAG change, not periodically during motion.
- **Version query / response** (types 12/13). The master sends an 8-byte
  `VERSION_REQ` of zeros to field 127 (every board) or to one board. Each
  board answers `VERSION_RSP` in the same 8-byte layout, little-endian:

  | Offset | Type | Field |
  |--------|------|--------|
  | 0 | `uint8` | `major` |
  | 1 | `uint8` | `minor` |
  | 2 | `uint8` | `patch` |
  | 3 | `uint8` | `flags`, bit 0 = dirty working tree |
  | 4–7 | `uint32` | `build` id |

  A board delays its response by `1 ms × (board_id mod 10)` so a query to all
  boards does not turn into a burst of identical identifiers. The panel keeps
  the last version per board; a missing answer is shown as `--`.
- **Command** (master → node, type 4, field = board ID or 127). One 8-byte
  frame, little-endian:

  | Offset | Type | Field |
  |--------|------|--------|
  | 0 | `uint8` | `action`: 1 up, 2 down, 3 stop, `0x7F` re-identify |
  | 1 | `uint8` | `axis_mask`, bits 0–3 |
  | 2–3 | `uint16` | `distance_mm` |
  | 4–5 | `uint16` | `speed_mm_s` |
  | 6 | `uint8` | `seq` |
  | 7 | `uint8` | reserved, 0 |

  Field 127 addresses every board; a single board ID addresses one. `0x7F`
  is re-identify: the board stops its axes, drops its RAM board ID and
  answers the next `ASK`. A stop on field 127 with mask `0x0F` is the
  operational stop.
- **Home** (master → node, type 5, field = board ID). One 8-byte frame:
  `[0]` axis mask, `[1..7]` reserved zero. Homing and calibration are on
  demand and are not part of the motion stream.
- **E-stop**: the hardwired power removal remains the safety function
  (`FR-MOT-011`). A CAN stop is operational only.

**Do not** put a 120-axis command in one broadcast frame: classic CAN cannot
carry it, and per-motor or per-node frames already give natural grouping.

### 4.6 Bus budget

Total bus budget for 120 motors. Motion frames are `120/(N·Δt)`; add the 50 Hz
sync, 30 × 1 Hz status and 30 × 1 Hz health (110 frames/s).

| Mode | `Δt` (ms) | Motion frames/s | Total frames/s | Bus load @ 1 Mbit/s |
|------|-----------|-----------------|----------------|---------------------|
| N=3 + speed | 10 | 4,000 | ~4,110 | **53 %** |
| N=3 + speed | 11 | 3,636 | ~3,746 | **49 %** |
| N=3 + speed | 15 | 2,667 | ~2,777 | **36 %** |
| N=4 positions | 10 | 3,000 | ~3,110 | **40 %** |
| N=4 positions | 11 | 2,727 | ~2,837 | **37 %** |
| N=4 positions | 15 | 2,000 | ~2,110 | **27 %** |

At ~130 bits/frame. Keep the sustained target below 60 %: `N=3`/`Δt=10` is the
tightest; `Δt=11` or `N=4` gives margin. If the design adds per-frame
repetition or more status, re-run the budget; do not assume the remaining
percentage is free.

**Window scheduling matters.** At 1 Mbit/s, 120 motion frames occupy about
**15.6 ms** of bus time regardless of `Δt`. The master must not start sending
the window at the nominal time `T`: it must start early enough that every
motor’s first sample (`T+Δt`) is on the bus in time. A practical rule is to
start the window **15–20 ms before `T`** and finish before `T+Δt`. The node
buffers the window; this is a “send ahead” protocol, not a “send at T”
protocol. A larger `Δt` leaves more idle time between windows for sync, status
and retransmissions.

A PC USB-CAN adapter must sustain the back-to-back window; if its TX queue adds
gaps, the window slips into the first sample. An embedded master with a
hardware CAN controller (or an RP2040 running can2040) is the safer production
choice; the PC is best for authoring and bench work.

### 4.7 Node playback

The node does not fit a curve. One message is one polynomial. With `τ` in
microseconds and the coefficients converted to µm, µm/s, µm/s², and µm/s³:

```
x = j * τ / 6 / 1_000_000 + a / 2
x = x * τ / 1_000_000 + v
x = x * τ / 1_000_000 + p
```

All intermediates are `int64_t`. Division truncates toward zero. This is the
only evaluator. Do not implement a Hermite basis, a Catmull-Rom tangent, or
a monotone limiter on the node: the master already chose the shape, including
any limit that keeps the sphere from reversing inside the segment.

`j * τ` at the int16 rails and `τ = 30 ms` stays inside int64. Divide before
the next multiply, in the order above.

- **Range.** Reject the segment if `p(0)` or `p(Tseg)` is outside
  0–2 000 000 µm, or if `|v0|`, `|a0|`, or `|j|` exceeds its int16 range.
  Do not clamp.
- **Thread.** Acceleration along the segment is `a0 + j·τ`, linear in time.
  Positive is upward. Reject the segment if either endpoint is below
  −9.81 m/s².
- **Join.** If the axis is already playing, the new `p0` and `v0` must match
  `p(Tseg)` and `v(Tseg)` of the previous segment within one LSB. Otherwise
  hold and fault. After a hold, the next `p0` must match the held position
  within one position code.
- **One message is enough.** The four coefficients define the segment. There
  is no priming message and no stored tangent from the previous frame.
- **Step conversion.** Map the evaluated micrometres to steps with the
  Q16.16 `steps_per_mm` factor. Keep the fractional remainder. Do not round
  each chunk independently.
- **Chunking.** Evaluate at 2 ms boundaries and feed the per-axis step delta
  to `FasTimed`. `|steps| ≤ 128` and `ticks ∈ [MIN_CMD_TICKS, 65535]`.

**Why this is not FastAccelStepper `log2`.** `src/log2/Log2Representation.cpp`
stores a positive magnitude as `x = 2^(xi/512)`. Multiply is an add, and a
square root is a divide by two. Zero and negative numbers are outside the
representation. `RampCalculator` uses that to answer a different question:
how many ticks a rest-to-rest ramp needs for a positive step count. Playback
speed and acceleration are signed and are often near zero, so the logarithmic
form does not apply. Leave `log2` inside FastAccelStepper for homing and for
a fault stop. The playback hot path is the Horner form above.

**S-curves (`FR-MOT-006`).** Jerk is constant inside the segment, so
acceleration is continuous there. At the boundary, acceleration is continuous
only when the master sets the next `a0` to `a0 + j·Tseg`. The node plays the
coefficients it is given.

### 4.8 Timing, buffering and failure

| Event | Node response |
|-------|---------------|
| Normal | Keep the current and previous message; evaluate on the global timeline |
| One message lost | The next message starts `N·Δt` later; there is no overlap. Decelerate at the end of the known segment and hold; the next absolute message re-anchors. A lost message affects one motor. can2040 auto-retransmits a failed frame, so a loss usually means the bus is disturbed |
| Next message absent before the buffer empties | Decelerate along the last velocity within the accel limit, then hold; flag fault |
| Sync lost | Free-run on the local clock; after a timeout, stop or hold |
| Sample out of range | Reject, hold, flag; do not clamp silently into another ball's range |
| Node reset | Drivers disable (EN pull-up), balls descend or hold by detent; master re-homes that node |
| CAN bus-off / wiring fault | can2040 does not implement automatic bus-off; detect error counters/timeouts and hold/stop. can2040 does auto-retransmit failed frames |
| E-stop | Hardwired power removal; no software dependence |

The deceleration path is essential: at 1.2 m/s, one missing message is a
36–72 mm gap for `N·Δt` of 30–60 ms. A jump to the next sample would be
visible; a controlled stop is not.

A node failure affects only its four balls; the other nodes continue
(`NFR-REL-002`/`NFR-REL-003`). The master should enter degraded mode, log the
event, and re-home the failed node when it returns.

### 4.9 Synchronization and drift

This is much easier than the WiFi case in `controller_analysis.md` §10:

- The bus is wired and shared. All nodes see the same sync frame. Motion
  frames are not a timebase; the identifier is 11-bit and carries no tick.
- A 35 ppm oscillator drifts only **0.7 µs per 20 ms** and **3.5 µs per
  100 ms**. A sync every 20–100 ms keeps the residual far below the 1 ms
  requirement; no rest-pause trick or long-term correction is needed.
- The residual is receive-timestamp jitter, not drift. Timestamp the CAN
  callback (or the SOF in the PIO if available) and measure. Expect tens of µs;
  even 100 µs is 10 % of the budget.
- Use a first-order phase-locked correction on the local sample clock. Do not
  step the position to correct time; adjust the rate.
- Three integer times, all in microseconds. Division truncates toward zero.
  A residual accumulator keeps the fractional microsecond, so five advances
  of 20 ms match one advance of 100 ms.
  - **Master time** is the `int64` timeline reconstructed from the sync frame.
    An advance of `dt` µs adds `dt` to it.
  - **Local time** is the node's crystal. The same advance adds
    `dt + extra`, where `extra` comes from a residual:
    `acc += dt × crystal_ppm`, `extra = acc / 1 000 000`,
    `acc -= extra × 1 000 000`. At +35 ppm, one advance of 100 000 µs adds
    3 µs and leaves `acc = 500 000`. One advance of 1 000 000 µs adds 35 µs
    and leaves `acc = 0`.
  - **Eval time** is what the interpolator receives. The first sync sets it
    to that frame's master time and sets the correction to 0 ppm. Each later
    sync measures
    `correction_ppm = (d_local − d_master) × 1 000 000 / d_master`
    (`d_master > 0`; otherwise the correction is unchanged), then rebases the
    anchors so eval time is the same value before and after the call. Between
    syncs,
    `eval = anchor_master + elapsed_local × (1 000 000 − correction_ppm) / 1 000 000`.
    After a 1 s interval at +35 ppm the correction becomes 35, and the next
    1 s of master time adds 999 999 µs to eval time. The earlier offset stays.
    The sync does not add a position offset.
- `N·Δt` is **lookahead**, not transport latency. A frame is ~130 µs on the
  bus at 1 Mbit/s, well inside `FR-SYNC-004` (≤ 5 ms worst-case inter-controller
  latency).

> **Conclusion:** with wired CAN and a 20–100 ms sync, `FR-SYNC-001` (≤ 1 ms)
> is a measurement task, not a research task.

---

## 5. RP2040 implementation

### 5.1 Resource map and the PIO conflict

| Resource | Use | Constraint |
|----------|-----|------------|
| PIO block 0 | can2040 (CAN 2.0B) | **All 4 SMs** (sync, RX, match, TX) and **all 32 instruction slots**; **11-bit standard IDs only**, ≤ 1 Mbit/s |
| PIO block 1 | FastAccelStepper | **Whole block's program memory**; 4 SMs = 4 axes; one SM per axis; 200 kHz max per axis |
| Core 0 | CAN, motion, app | can2040 uses CPU time in IRQ/background |
| Core 1 | optional | Could run the motion task or diagnostics; keep the CAN timing on one core |
| TMC2209 UART | configuration/homing | One bus, 4 addresses; not a motion channel |
| DIAG × 4 | stall/overtemp events | Independent GPIOs; use interrupts |

**can2040 is not a one-SM program.** It loads a 32-instruction program and
enables four state machines in the same block: SM0 sync, SM1 RX, SM2 match and
SM3 TX. That block is fully consumed; it cannot host a second program or lend
an SM to the stepper driver. A hypothetical one-SM CAN would not automatically
free room for steppers either: FastAccelStepper consumes the **whole program
memory** of the PIO block it uses (and one SM per stepper), so the two programs
cannot share a block even if the SM count allowed it. There is no way to free a
PIO block on the Fly-Micro4: can2040 is fixed to one block and the board has
two. Four steppers is the ceiling.

**Initialization order matters.** Initialize can2040 first so it claims a PIO
block. Then initialize FastAccelStepper; it will claim the other block and
allocate 4 state machines. If FastAccelStepper claims both blocks first (e.g.
by creating 8 steppers), can2040 has no PIO program space and fails.

> **Hard limit: 4 steppers per Fly-Micro4.** can2040 consumes one PIO block;
> one block (4 SMs) remains. The Fly-Micro4 is the chosen board and is not
> modified. No external CAN controller, board rework, or 8-axis variant is part
> of this design.

**RTOS requirement.** The FastAccelStepper Pico driver is built on FreeRTOS
(`src/pd_pico/pd_config.h` includes `FreeRTOS.h` and creates a `StepperTask`).
Use Arduino-Pico, which supplies FreeRTOS, or add FreeRTOS to a Pico SDK build.
A bare-metal Pico SDK build is not a drop-in.

**CAN is fixed.** The Fly-Micro4 uses can2040; there is no SPI controller or
board change. Keep the bus below ~60 %, keep can2040 IRQ latency short, and use
heartbeat/timeouts plus the hardwired E-stop. Four axes is the ceiling; CAN is
not a safety function.

### 5.2 FastAccelStepper integration levels

| Level | API | Fit for this project |
|-------|-----|----------------------|
| High-level ramp | `move()`, `moveTo()`, `setSpeedInHz()`, `setAcceleration()` | ❌ Each 10 ms update restarts a rest-to-rest ramp; velocity discontinuities, following error, wrong abstraction |
| Queue level | `addQueueEntry()` (steps, ticks) | ✅ Works, but manual multi-axis bookkeeping |
| Timed multi-axis | `FasTimed<4>` | ✅ **Recommended**; shared tick timeline, exact chunk sums |
| N-axis planner | `FasNAxis<4>` | ❌ Coordinated path/DDA; slows at vertices and assumes a path, not independent balls |
| On-driver ramp | TMC5160 SPI | ❌ Fly-Micro4 has TMC2209, no on-driver ramp |
| Custom PIO player | FIFO of (period, steps) | ⚠️ Only if > 4 axes or exact sample playback; reimplements what FAS already does |

**Why not `FasNAxis`:** it is a multi-axis *path* planner. It couples axes via
a DDA master and inserts path stops at hard vertices/reversals. In this
installation each ball is an independent vertical axis; a direction reversal on
one ball must not slow the other three. `FasTimed` preserves the shared clock
without imposing a path geometry.

**Why not the high-level ramp:** the trajectory is already a position-vs-time
curve. A ramp generator that plans rest-to-rest moves to a target is solving a
different problem. Feeding it a new target every 10 ms produces a sequence of
overlapping ramps, not a smooth playback.

**Why not custom PIO:** the Pico driver already gives deterministic PIO step
timing at 200 kHz, direction handling and a queue. A custom player would save
little and add a second timing model to validate.

### 5.3 `FasTimed` limits and chunking math

On the Pico configuration (`src/pd_pico/pd_config.h`):

| Limit | Value | Consequence |
|-------|-------|-------------|
| Tick rate | 16 MHz | `ticks` are 62.5 ns units |
| `MIN_CMD_TICKS` | 3,200 = **200 µs** | Minimum chunk |
| `ticks` type | `uint16_t` | Maximum **65 535 = 4.096 ms** per call |
| Steps per call | `[-128, +128]` per axis | Maximum displacement per chunk |
| Horizon | 8 chunks (default) | Stream: add until `Rejected`, pump, add again |

At 40 mm and the 1/16 worked case, 1.2 m/s is the following. The microstep
count is TBD, so the step counts scale with that choice:

| Chunk | Steps/axis | Fits `FasTimed`? |
|-------|------------|------------------|
| 2 ms | 61 | ✅ |
| 4 ms | 122 | ✅ |
| 4.096 ms | 125 | ✅ (at the edge) |
| 5 ms | 153 | ❌ |
| 10 ms | 306 | ❌ |

**Use 2 ms chunks.** At `Δt=10 ms` there are five per sample; at `Δt=11` or
`15 ms` the sample boundaries do not align with 2 ms, so evaluate the segment
at each 2 ms boundary. At 0.6 m/s that is 31 steps per chunk; at 1.2 m/s, 61.
The `Δt` samples are interpolated, not fed directly.

Step counts scale with the microstep setting. At 1/32 the same 1.2 m/s move
is 122 steps per 2 ms (inside 128) and 244 per 4 ms (over 128).

`FasTimed` emits one or two `addQueueEntry` commands per axis whose tick sums
are equal; `pump()` feeds the queue. Check `TimedStatus::Underrun` and
`Error` every cycle. A latched underrun means the interpolation/feed task
missed its deadline; treat it as a fault, not a warning.

> `FasTimed` is marked experimental in FastAccelStepper. Pin the library to a
> known commit, vendor it in the repository, and add a compile-time assertion
> on `TICKS_PER_S`, `MIN_CMD_TICKS` and the step limit. Have a fallback plan:
> a small custom chunker over `addQueueEntry()` with the same interface.

### 5.4 Fixed-point arithmetic

The RP2040 has no FPU. Define the core units once:

| Quantity | Type | Unit | Range |
|----------|------|------|-------|
| Position | `int32_t` | µm | 0 … 2 500 000 |
| Velocity | `int32_t` | µm/s | ±1 300 000 |
| Acceleration | `int32_t` | µm/s² | ±4 000 000 |
| Time | `int64_t` | µs | patterns up to years |
| Step position | `int32_t` | steps | ~64 k for 2.5 m |
| Step rate factor | `int32_t` Q16.16 | steps/mm | 25.46 |

- Use `int64_t` intermediates for cubic products and time×velocity. The
  Cortex-M0+ emulates 64-bit multiply/divide in software, but 4 axes at 1 kHz
  is only ~4,000 evaluations/s — negligible against 133 MHz.
- Keep a fractional-step accumulator per axis. The position→step conversion
  must not accumulate rounding error.
- The steps/mm factor is the Q16.16 integer for `3200 / (π × 40 mm)`, the
  1/16 worked case while microstepping is TBD. It makes 1.2 m/s over 2 ms
  equal 61 steps. Position codes use
  `code × 2 000 000 / 65 535`. Both are integer divisions, truncating toward
  zero except where a test names `round`.
- The host/WASM build uses the same integer types. The shared core has no
  `float` or `double`. Convert to floating point only in the renderer and in
  test printers.

### 5.5 Task and interrupt architecture

```
CAN RX IRQ (can2040)
  └─ copy frame + timestamp into a lock-free ring (no parsing in IRQ)

Motion task (500 Hz – 1 kHz, core 0 or 1)
  ├─ drain CAN ring, validate seq/tick, update per-axis sample buffers
  ├─ evaluate cubic at the current global time for 4 axes
  ├─ convert to step deltas at 2 ms boundaries
  ├─ FasTimed::addDelta()/pump()
  └─ check buffer depth, sync error, following error, faults

StepperTask (FastAccelStepper internal, ~4 ms)
  └─ manage queues / PIO FIFOs

Housekeeping (10–50 Hz)
  ├─ status frame (1 Hz + on change), health frame (1 Hz), DRV_STATUS
  ├─ sync fit/PLL update
  └─ watchdog kick
```

- Keep the CAN RX callback short. can2040 calls it from its own context; a
  long callback can lose frames. The bus delivers `120/(N·Δt)` frames/s
  (4,000 at `N=3`/`Δt=10`), so every node sees that rate even though it owns
  only four motor IDs. Filter on the ID immediately and copy the 8 bytes to
  the ring; do not parse or log in the callback.
- The motion task must not block on UART, flash or USB. Configuration and
  homing run in a separate state.
- Use a hardware timer/alarm for the 1 kHz task, not a busy loop. On
  Arduino-Pico, a `repeating_timer` or FreeRTOS task with a precise period is
  fine; measure jitter.
- The RP2040 hardware watchdog resets a hung node. A reset disables the
  TMC2209 outputs (EN pull-up) and drops that node's balls; this is an
  acceptable fault response (the piece stays safe, four balls descend), but it
  is visible. Log the reset cause.

### 5.6 TMC2209 configuration

- **Microstepping:** TBD. Until the count is chosen, the host constant
  `MC_STEPS_PER_MM_Q16` and the chunk examples use the 1/16 worked case.
  MicroPlyer (`intpol`) can then interpolate that input toward 1/256.
- **Chopper:** StealthChop for the slow/quiet programme. Verify it does not
  lose steps at the 1.2 m/s / 4 m/s² effects; if it does, switch to SpreadCycle
  for the fast tier or reduce the acceleration. The duty is tiny (0.25–0.3 A
  RMS), so thermal headroom is ample.
- **Current:** set run/hold current over UART at boot. Do not run at the
  module's 1–2 A rating; the mechanical analysis needs ~0.02 Nm and the 2×
  margin is already covered at 0.25–0.3 A.
- **UART:** one bus, addresses 0–3 via MS1/MS2. Configure at boot and during
  homing only. Never put motion on UART.
- **DIAG:** push-pull, asserts on stall, overtemperature pre-warning, short/
  open load. Route all four to interrupt-capable GPIOs. On a DIAG event, read
  `DRV_STATUS`/`SG_RESULT` and decide: hold, retry, or fault the board.
- **Temperature:** the TMC2209 has no numeric temperature register.
  `DRV_STATUS` exposes `otpw` (pre-warning) and `ot` (shutdown) plus threshold
  flags, all boolean. The RP2040 internal sensor on ADC4 gives an approximate
  die temperature (`analogReadTemp()`); it is uncalibrated and suitable for
  trend/overtemperature, not ambient measurement. The board has two external
  NTC inputs (T0 GPIO28/ADC2, BED GPIO29/ADC3) with 4.7 kΩ pull-ups; no NTC is
  fitted. Use the internal sensor for local health and the TMC flags for driver
  cutoff; add an external NTC or I²C sensor only if a numeric board/driver
  temperature is required. Report the MCU temperature, TMC masks and any valid
  NTC values in the 1 Hz type-14 health frame (§4.5).
- **Direction:** configure a DIR setup/hold delay. FastAccelStepper inserts a
  pause on reversal (hundreds of µs). At 1.2 m/s that is 0.6–1.2 mm of delayed
  motion; keep reversals rare and account for the pause in the trajectory
  verification.

#### 5.6.1 Bring-up implementation and bench tuning

The firmware implements §5.6 in `rp2040/src/tmc2209.cpp`. All four `PDN_UART`
pins share GPIO9, and both PIO blocks are fully allocated (can2040 and four
FastAccelStepper state machines), so the core's PIO-based `SoftwareSerial`
cannot be used. GPIO9 is a UART1 **RX** pin, not a TX pin, and the firmware
does not use a UART peripheral or a TMC library at all: SIO drives the whole
request as one continuous bit stream, then releases the pad and samples the
reply on the 1 µs tick. Masking interrupts across both halves keeps the
request and the reply back-to-back; no PIO state machine or background UART
ISR is involved. The pure bit-level code is in `lib/tmc_uart_bits/`, which is
also built and tested on the host.

The UART address is per drive, not the drive index: the official Klipper
config uses 0, 1, 3, 2 for Drive0–Drive3, because Z is Drive3 and E0 is Drive2
(`boards/Fly-Micro4/Fly_Micro4_pinmap.md` §1). The firmware takes the address
from each drive's pin table.

Boot order follows `controller_analysis.md` §6.1: drive all `ENN` high, read
`GCONF` and `OTP_READ`, reject a driver with OTP0.6 = 1, force `GCONF.1 = 0`
and verify it, then write `GCONF` with `I_scale_analog = 0`,
`en_spreadcycle = 0`, `pdn_disable = 1`, `mstep_reg_select = 1` and
`multistep_filt = 1`. The chopper is TOFF = 4, MRES = 4 (1/16) with
interpolation, `PWMCONF = 0xC10D0024`, `TPWMTHRS = 0`,
`TCOOLTHRS = 0xFFFFF`, `SGTHRS = 40`. Only the `GCONF.1 = 0` external-sense
write is read back (§6.1 safety gate). The remaining configuration registers
are written without a read-back; `IHOLD_IRUN`, `TPOWERDOWN`, `TPWMTHRS`,
`TCOOLTHRS` and `SGTHRS` are write-only in the TMC2209 register map, so a
read-back would be meaningless. A driver left `not configured` is kept
disabled, and only drivers that complete bring-up are enabled by the node.

The default is 300 mA RMS run and hold at `RSENSE = 0.110 Ω`. The firmware
selects VSENSE and the IRUN/IHOLD codes with the integer form
`CS + 1 = 588313 · mA / (10⁸ · V_fs)`, where `V_fs` is 0.325 V or 0.180 V;
if `CS < 16` it switches to the high-sensitivity range and recomputes. Hold
equals run so the ball weight is still held with margin; do not reduce it
without measuring the holding margin. At `RSENSE = 0.110 Ω` the
high-sensitivity current step is about 30 mA, so the codes nearest 300 mA are
275 mA (`IRUN = 8`) and 306 mA (`IRUN = 9`). The bench console caps the
run-current request at 800 mA, matching the official reference; this is above
the 250–300 mA design point and is only an experiment limit, not an operating
recommendation.

**Bench console.** USB serial accepts the following commands
(`rp2040/src/bench_console.cpp`). It is a bring-up tool, not an operator
interface, and it can command motion.

| Command | Meaning |
|---------|---------|
| `status [axis]` | Read and print `DRV_STATUS`, `SG_RESULT`, `TSTEP`, currents and DIAG |
| `cur <axis> <run_ma> [hold_ma]` | Set run/hold current and print status |
| `sg <axis> <0..255>` | Set `SGTHRS` |
| `tcool <axis> <value>` | Set the 20-bit `TCOOLTHRS` |
| `micro <axis> <2..256> [0\|1]` | Set microsteps and interpolation |
| `chop <axis> <hex>` / `pwm <axis> <hex>` | Raw CHOPCONF / PWMCONF; VSENSE is preserved |
| `raw <axis> <reg> [value]` | Register access in hex; OTP, `VACTUAL`, `IHOLD_IRUN` and unsafe GCONF bits are refused |
| `move <axis> <steps> <hz>` | Finite move |
| `run <axis> <hz> [0\|1]` | Continuous move, 1 = forward |
| `stop [axis]`, `enable <axis> <0\|1>`, `diag` | Stop, enable outputs, print DIAG pins |
| `reinit` | Stop, re-run the TMC bring-up, enable the drivers that pass |
| `quiet [on\|off]` | Silence the periodic CAN statistics and command logs; CAN traffic is unchanged |
| `selftest` | Bit-bang a pattern through SIO and sample the pad at every bit centre |
| `scan` | Read `GCONF` at UART addresses 0–3 to find which strap addresses answer |
| `bbprobe [addr]` | Pure bit-banged GCONF read plus the raw oversampled reply |
| `line [us]` | Release GPIO9 (pulls off) and report bus level, high count and edges |
| `bit [us]` | Show/set the bit time: 4 µs = 250 kBaud (default), 5 µs = 200 kBaud |
| `senddelay <axis> <0..15>` | Set `SLAVECONF.SENDDELAY` (reply gap in bit times) |

`raw` is for register diagnosis, not current control: use `cur` so the 800 mA
cap and the VSENSE selection apply. The console can still command motion, so
keep the bench clear of the mechanism.

`reinit` is the bring-up path after the motor supply is applied: the Fly-Micro4
has no reset button, and unplugging USB loses the serial log. `quiet on` keeps
the console readable. `selftest` proves the SIO output path by driving a fresh
pattern on GPIO9 and sampling every bit centre; if it passes and `GCONF read
failed` remains, the fault is external (TMC supply, PDN bus, address, or
wiring). None of these commands changes the CAN protocol.

Bring-up writes `SLAVECONF.SENDDELAY = 2` (two extra bit times before each
reply) and does not read it back: writes are write-only, and only reads use the
reply capture and CRC check. That gives the reply a clean, known gap after our
last stop bit; `senddelay` overrides it on the bench. The line is driven high
before and after that write.

The board pulls `PDN_UART` down through 20 kΩ when the line is released, so the
UART init sets and verifies `GCONF.pdn_disable = 1` (together with
`mstep_reg_select` and `multistep_filt`) to take the pin's power-down function
out of the picture. This is a firmware-only change, with no board change: the
pin is an SIO output driving 1 whenever the firmware is not sampling a reply.
`bbprobe` keeps the SIO output enabled across the whole datagram, so the stop
bit of one byte is immediately followed by the start bit of the next, and
releases the pad only after the final stop bit. The older per-byte bit-bang
released the pad after every byte; the pull-down then pulled the line low
between bytes. Bring-up logs every register it writes, the current scaling
and the `pdn_disable`/SENDDELAY checks, so the serial log shows what was
initialized on start and on `reinit`.

`bbprobe` bit-bangs the reply at the selected bit time (default 250 kBaud,
4 µs per bit) and samples it once per microsecond over 128 bit times (512
samples at 250 kBaud, 640 at 200 kBaud), covering the reply delay, the maximum
`SENDDELAY` and the 8-byte reply. The capture starts at the transmit release,
so the decoder can treat the line-release 1→0 edge and the `0x05` sync edges
as references. It prints the raw samples as `bbprobe: os <start>: …` lines,
then `lib/tmc_uart_bits` scans the 1→0 edges for the reply start and fits the
bit period: a candidate is accepted only when the frame starts with `0x05` and
its CRC8 matches. This is why the earlier 3-bit period estimate failed — the
TMC re-times the reply from our datagram and answers at roughly 0.95–0.98 of
our bit time (4.75–4.90 µs at 5.00 µs, 3.8–3.9 µs at 4.00 µs), so a fixed
grid walked off the CRC by the last byte. On a good reply `bbprobe` prints the
fitted start and period, which shows how much margin the decoder had. The host
tests replay recorded captures through the same production function.

**Baud and interrupt budget.** The TMC2209 auto-detects the rate and is
specified for up to roughly 500 kbit/s. The default is 250 kBaud (4 µs per
bit); `bit <us>` switches it at runtime for A/B tests on the same address
(5 µs = 200 kBaud, 8 µs = 125 kBaud). Interrupts are masked for one request
plus the 8-bit idle hold and one capture: `(4·10 + 8) · T + 128 · T`, i.e.
880 µs at 200 kBaud and 704 µs at 250 kBaud (4× oversampling on the same 1 µs
grid). The decoder searches around the nominal period, so it follows the
runtime bit time automatically. Before the first edge the transmitter waits for
the next 1 µs timer tick, so the start bit is not shortened by the fractional
tick offset; without that alignment the first bit can be up to 1 µs short
(25 % at 4 µs) and the TMC auto-baud can lock onto the wrong rate and reject
the request. Verify a rate on the bench before relying on it. `selftest`
reports the driven idle level; use `line` to release the pad and inspect the
bus, and remember that the board's pull-down makes a released line read low by
design.

**StallGuard calibration.** At the intended homing current, run the axis free
with `run <axis> <hz> 1` and sample `SG_RESULT` with `status <axis>`. Then
block the axis and sample again. `SG_RESULT` falls with load and high values
mean light load; `SGTHRS` sets where DIAG asserts (higher is less sensitive,
0 disables StallGuard). Sweep `SGTHRS` at each current step, because the
signal amplitude scales with current. The DIAG path is also the fault path:
it asserts on overtemperature pre-warning, short-to-GND and open load, so a
DIAG read must be interpreted with `DRV_STATUS`, not assumed to be a stall.

The node reads `DRV_STATUS` at boot and on a DIAG change only; it does not
poll UART in the motion loop. The 1 Hz health frame carries the resulting
`otpw`/`ot` masks. On-demand `status` reads block the CPU for tens of
milliseconds per driver, which is acceptable at the bench but must not be
used while motion is playing.

### 5.7 Homing and position reference

**Chosen reference:** the **ball-top (upper) mechanical stop**, detected by the
TMC2209 **StallGuard4/DIAG**. No separate endstop sensor. This is a design
choice under `FR-MOT-009`/`CON-006`; verify repeatability on the bench.

**DIAG routing:** all four DIAG are routed (Drive0–3: GPIO13/14/15/12). The
official config uses GPIO12 (Drive3 DIAG) as the Z endstop, so Drive3 StallGuard
and a separate endstop share that pin. For 4-axis StallGuard, all four are
available; do not use GPIO12 as both. See `boards/Fly-Micro4/Fly_Micro4_pinmap.md`.

**Derived homing requirements**

- The upward homing move shall use the **lowest current that reliably moves the
  ball**, to limit the force applied to the top stop and the ball/thread.
- If StallGuard asserts **immediately** — before a minimum travel/time — treat
  it as “current too weak / false stall”, not as the reference. Increase the
  homing current in small steps (e.g. 25 mA RMS) and retry, up to the normal
  run current.
- If the assert occurs after the minimum travel/time, accept it as the top
  reference. Stop, record home, restore normal current, and back off to the
  safe offset.
- If the axis still asserts immediately at the normal run current, it is
  either already at the top (accept only after a confirmed stop) or jammed
  (fault). Do not keep escalating.
- A thread break or missed home is inferred if the step count exceeds the
  travel limit before the reference (`NFR-REL-004`).
- The top stop and ball attachment shall be designed for the maximum homing
  force at the run-current cap.

**Procedure**

1. Set the bench-calibrated homing current and the matching `SGTHRS`; use
   StealthChop and a slow but nonzero homing speed (StallGuard4 needs motion to
   produce a load estimate).
2. Optional: if the lower stop has margin, command a small downward slack move
   (e.g. 10–20 mm) so a ball already at the top can travel before the stop;
   ignore StallGuard on this move and stop on the travel limit.
3. Move upward at a slow, constant speed.
4. On a valid StallGuard assert (after the minimum travel/time), stop and
   record home. On an immediate assert, step the current up and retry; cap at
   the run current. Re-arm the matching `SGTHRS` for the new current.
5. Restore the normal run/hold current, back off to the safe offset, and mark
   the axis ready.
6. Store the calibrated `SGTHRS` and homing current per axis; read `SG_RESULT`
   over UART during commissioning to set the threshold and the minimum
   travel/time.

**Timing:** home one axis at a time or four in parallel per board; run boards
in parallel over CAN. The official sensorless-homing reference uses
**40 mm/s**; at that speed a 2.0 m worst-case search is 50 s, inside the 60 s
budget (`FR-MOT-010`) but with limited retry margin. A 2.5 m search would take
62.5 s and exceed the budget. Use 40–50 mm/s and verify StallGuard
repeatability; the earlier 0.1 m/s figure is faster but may be too fast for
reliable StallGuard.

The official config also drops the current to 0.7× run current during homing
and restores it afterwards. That supports the low-current idea; our procedure
starts lower and escalates only on an immediate trigger.

`FR-MOT-009` requires exactly one position-reference endstop per axis; a hard
stop plus StallGuard is the detection method. If a physical sensor is ever
required, the Fly-Micro4 needs one endstop input per axis; the board routes
DIAG, not separate endstops, so that would be a wiring or carrier change.

The node needs a persistent per-axis calibration (steps/mm, drum radius,
winding scale, home offset, `SGTHRS`, homing current). Reserve a flash
sector or use a small key-value store; document the wear/erase strategy.

### 5.8 Fault handling and safety

| Fault | Response |
|-------|----------|
| CAN sample timeout | Decelerate to rest, hold, report |
| Buffer underrun | Same; do not extrapolate indefinitely |
| Sync timeout | Hold last position; stop after a bounded time |
| DIAG stall/overtemp | Hold that axis/board, report; do not silently retry into a jam |
| Lost steps suspected | Re-home that axis; if repeated, fault the board |
| Bus-off/wiring | can2040 has no automatic bus-off; detect via error/timeout and hold/stop |
| Node reset | Drivers disable; master re-homes; log |
| Firmware update | UF2 over USB-C gives recovery but not rollback. `NFR-MAINT-004` also requires integrity verification and rollback on failure: add A/B images or a known-good image with a hash check if the requirement is enforced |
| E-stop | Hardwired power removal; all nodes lose drive power |

The CAN protocol is **not** a safety function. The red button removes drive
power (`FR-MOT-011`); software only provides orderly stops.

#### 5.8.1 Local status LED (D5, GPIO8)

`FR-MON-005` requires a local OK/fault indication. The Fly-Micro4 has one free
single-colour LED, **D5 on GPIO8**, active high. D1–D4 are heater and fan
indicators tied to the `HEAT0`, `BED`, `FAN0` and `FAN1` drive nets; they are
not free status outputs. The RGB header on GPIO11 is a connector only: no RGB
LED is fitted and no WS2812/PIO driver is used. This LED is the local
maintenance indicator, separate from the operator-facing global indicator
(`FR-MON-006`).

The pattern is table-driven and non-blocking. It must not delay CAN, stepping
or the main loop. Timings are `on/off` in milliseconds.

| State | Pattern | Period | Meaning |
|-------|---------|-------:|---------|
| Boot | 500 on, then off | one-shot | self-test / firmware started |
| Unidentified | 100/200, 100/200, 1400 off | 2 s | no board ID yet |
| Idle, primed | 50 on, 1950 off | 2 s | OK, alive, waiting |
| Homing | 250 on, 250 off | 0.5 s | busy homing/startup |
| Playing | 50 on, 9950 off | 10 s | OK, running |
| Holding | 100/200 × 3, 1100 off | 2 s | controlled stop, awaiting command |
| Fault | 100 on, 100 off | 0.2 s | fault, latched |

The mnemonic is **1 pulse = OK, 2 flashes = no board ID, 3 flashes =
stopped, slow blink = homing, fast blink = fault, solid 0.5 s = boot, slow
heartbeat = running**. Healthy states stay quiet; only exceptional states draw
attention.

State mapping:

- Reset: `Boot` → `Unidentified`. The board ID is RAM
  (`Fly_Micro4_addressing.md` §2), so every cold start begins unidentified.
- `ASSIGN` received: `Unidentified` → `Idle`.
- `HOME`: `Idle` → `Homing` → `Idle` or `Fault`.
- First motion window: `Primed` → `Playing`.
- Stop, lost master or buffer underrun: `Playing` → `Holding`.
- Any fault, bus-off or driver fault: → `Fault`, latched until reset or an
  explicit fault clear.
- `Fault` overrides every other state.

Implementation requirements:

- `status_led::init()` configures D5; `status_led::set(state)` selects a
  pattern and restarts it; `status_led::set_mc_state(state)` maps the type-3
  status state; `status_led::clear_fault()` releases the fault latch;
  `status_led::tick(now_ms)` advances the pattern from the main loop. No
  `delay()`, no PIO and no hardware timer.
- `set(Fault)` latches. Other states are ignored until `clear_fault()` or a
  reset.
- A 5–10 ms tick resolution is sufficient; the shortest pulse is 50 ms.
- Do not drive the LED from the CAN ISR or the FastAccelStepper callback. Set
  the desired state from the protocol/main-loop context.
- The existing “flash on every CAN frame” behaviour in `main.cpp` is not part
  of this scheme and is removed.
- Timing tolerance is ±20%. The pattern table is pure and can be host-tested
  if the engine is moved into `lib/`.

### 5.9 Bring-up tests (before the full build)

1. **PIO coexistence:** can2040 + FastAccelStepper on one board, 4 axes,
   1 Mbit/s traffic, 30 kstep/s. Check for PIO claim failures, CAN errors and
   step jitter on a logic analyser.
2. **`FasTimed` timing:** 2 ms chunks, 4 axes, sweep 0.1–1.2 m/s; verify tick
   sums, direction pauses, underrun/error status.
3. **CAN load:** 30-node traffic generator (or 30 boards) at 1 Mbit/s; measure
   error frames and worst-case latency at 40 % and 60 % load.
4. **Sync:** two boards, scheduled LED/step event, scope skew; target ≪ 1 ms.
5. **Termination:** the bus ends are the Waveshare CAN jumper at 120 Ω and the
   last Fly-Micro4 with J3 inserted; every other Fly board leaves J3 open (no
   120 Ω). R39/R58 stay fitted — no desoldering. Measure the differential
   waveform and the common-mode shift.
6. **Homing repeatability:** 20 cycles per axis, StallGuard threshold sweep.
7. **Thermal/current:** 120 motors holding, 30 A at 24 V; measure board and
   enclosure temperatures.

### 5.10 PlatformIO project layout

```
rp2040/                       # firmware-only
├── platformio.ini
├── include/
│   ├── board_pins.h          # per board revision; verify from schematic
│   └── app_config.h          # node id, limits, timing
├── src/
│   ├── main.cpp
│   ├── can_node.cpp/.h       # can2040 adapter, RX ring, discovery, TX
│   ├── status_led.cpp/.h     # D5/GPIO8 status patterns (§5.8.1)
│   ├── motion_node.cpp/.h    # sample buffers, interpolator, chunker
│   ├── homing.cpp/.h
│   ├── tmc2209.cpp/.h        # UART, register setup, current/StallGuard
│   ├── bench_console.cpp/.h  # USB-serial current/StallGuard tuning
│   └── config_store.cpp/.h
└── test/
    └── test_motion_core.cpp  # on-target self-test for W0 bench
```

The portable core itself lives **outside** `rp2040/`, in `lib/motion_core/`
(`include/` + `src/`), so the firmware folder stays firmware-only. It is the only
source exchanged between the PC harness and the firmware; `rp2040/platformio.ini`
finds it through `lib_extra_dirs = ../lib`. The host harness (CMake, one
executable `motion_core_tests`) lives in `tests/` at the repository root.

`platformio.ini` outline:

```ini
[env:fly_micro4]
platform = raspberrypi
board = pico                  ; confirm flash size/variant for the Fly-Micro4
framework = arduino
board_build.core = earlephilhower
build_flags =
    -DPICO_RP2040
    -DCFG_TUSB_MCU=OPT_MCU_RP2040
lib_deps =
    ; the repository's ../FastAccelStepper already contains FasTimed;
    ; vendor that commit (or a pinned fork), do not track master
    https://github.com/.../FastAccelStepper.git#<commit-with-FasTimed>
    https://github.com/KevinOConnor/can2040.git#<commit>
    ; the TMC2209 wire is bit-banged in rp2040/src/tmc2209.cpp;
    ; no TMC library is linked
```

**Pin map.** Board pin assignment, DIAG routing and the official Klipper
reference are in `boards/Fly-Micro4/Fly_Micro4_pinmap.md` and
`boards/Fly-Micro4/Fly_Micro4_klipper_reference.cfg`. Record the verified pins in
`board_pins.h`.

---

## 6. PC simulation and designer tool

### 6.1 Goals and use cases

1. **Author** artistic patterns: timelines, keyframes, procedural fields,
   layers, blends, transitions. The authoring surface is an animation-data
   file, not a chat box.
2. **Preview live.** The running 3-D view reloads that file when it changes,
   without restarting the camera or the playhead.
3. **Validate** every reload against the installation: travel, speed,
   acceleration, jerk, step rate, CAN load, buffer depth, quantization.
4. **Export** only a take whose report is clean, as a pattern file for the
   installation, or stream it to a bench over USB-CAN.
5. **Verify** that the preview and the hardware replay the same motion, using
   the same core and golden traces.


Non-goals: lateral/pendulum dynamics, physical ball dynamics for the final
piece, audio analysis, or a general 3-D animation package. The simulation
models vertical position only, consistent with the mechanical assumption that
lateral swing is suppressed.

### 6.2 Architecture options

| Option | Pros | Cons | Verdict |
|--------|------|------|---------|
| **Shared C++ core + WASM + Three.js web app** | One motion implementation; OS-agnostic in a browser; excellent 3-D and UI; easy distribution; deterministic | WASM build step; browser USB access needs a local bridge | ✅ **Recommended** |
| Python + Qt/ModernGL + pybind11 core | Fast prototyping; scientific stack; same core | Packaging macOS/Windows is painful; UI/3-D less polished | ✅ Fallback if the designer prefers Python |
| Native C++/Qt + OpenGL | Same core; native performance | Build/distribution and UI effort; slower iteration | ⚠️ Only if a native look is required |
| Godot/Unity | Great 3-D/UI | Heavy, licensing, duplicates the motion core | ❌ Not for the reference tool |
| Pure Python, independent math | Easiest first demo | Duplicates the core; drift between preview and firmware | ❌ Only as a test oracle |

**Recommendation:** make the portable core the single source of truth and build
the designer tool as a web app. If a native executable is wanted, wrap the same
web UI in Tauri; do not rewrite the motion core.

### 6.3 Shared core

```
motion_core/
  include/motion/
    units.h          # µm, µs, Q16.16 helpers
    sample.h         # Sample { t_us, pos_um }
    trajectory.h     # per-axis ring of absolute samples
    playback.h       # Horner evaluation of p0, v0, a0, j
    step_planner.h   # µm -> step deltas, fractional accumulator
    protocol.h       # CAN frame pack/unpack
    limits.h         # travel, velocity, accel, step rate
  src/...
  tests/             # native unit/property tests
```

Expose a small C ABI for WASM:

```c
int  mc_trajectory_push(int axis, int type, int64_t t_start_us,
                        int64_t tseg_us, const mc_segment_t* seg);
int  mc_eval(int axis, int64_t t_us);            /* µm, -1 while idle */
int  mc_step_chunk(int16_t out_steps[4], uint16_t* ticks,
                   int64_t t_us, int32_t chunk_us);
int  mc_pack_frame(int type, const mc_segment_t* seg, uint8_t out[8]);
int  mc_unpack_frame(const uint8_t in[8], int len, int expect_type,
                     mc_segment_t* seg);
```

The firmware links the same sources. The simulation calls `mc_eval` for
rendering and `mc_pack_frame` to show/emit the exact wire bytes. Host tests run
the same functions. This is the contract that makes “simulated and real motion
match” (`FR-SIM-001`) testable rather than aspirational.

Host tests are C++17, built with CMake as the executable `motion_core_tests`,
and they use one `ASSERT` macro. The WASM build is Emscripten (`emcc -std=c++17`).
The clock double, the debounce interval, and the test order are in
`motion_streaming_tdd.md` §0.1.

### 6.4 Pattern model

Keep the authoring model separate from the wire format:

- **Layers** with a generator and parameters. Version 1 implements:
  - `keyframes`: time, position offset; linear interpolation, no easing.
  - `sine` / `travelling_wave`: amplitude, period, wavelength, direction
    (travelling_wave only), phase, axis (`col` or `row`).
  - `interference`: sum of two waves with separate periods, wavelengths,
    phases and axes.
  - `spiral`, `ripple`, `cascade`, `random_walk`; `random_walk` is seeded and
    `spiral`/`ripple` use the group centre.
  - `expression`: deferred; not part of version 1.
- **Groups**: all, rows, columns, rings, checkerboard, masks; the same layer
  can be limited to one group. Group fields are in §6.9.
- **Blend/transition**: crossfade over ~1 s (`FR-PAT-008`); never a position
  jump.
The tool bakes layers to per-axis absolute samples at the chosen cadence
(the configured `1/Δt` rate). The bake is deterministic: same inputs → same
bytes. The limits below are the installation's, read from the same `limits.h`
the firmware uses. A pattern that only looks right in an unconstrained
preview is not a pattern this tool accepts for export.

| Limit | Value the tool enforces | Source |
|-------|-------------------------|--------|
| Travel | 0–2000 mm. No coordinate outside the 16-bit wire range | `FR-MOT-005`, §4.2 |
| Nominal fast tier | peak ≈ 0.44–0.59 m/s, acceleration ≈ 1.0–1.5 m/s² | `ASM-015` |
| Optional short effects | peak ≈ 0.8–1.3 m/s, acceleration ≈ 2–4 m/s² | `ASM-015` |
| Ordinary choreography | ≤ 0.3 m/s; the fast tiers are the ceiling, not the default | `ASM-015` |
| Thread tension | commanded downward acceleration stays below `g` | `FR-MOT-012` |
| Continuity | no position jump; a blend takes ~1 s | `FR-PAT-008`, `FR-MOT-006` |
| Wire sample | 16-bit full-range position, 0.04 mm/s speed; message period `N·Δt` | §4.2 |
| Bus | sustained load inside §4.6, below 60 % at 1 Mbit/s, 11-bit frames | §4.6 |
| Steps | 25.46 steps/mm; a 2 ms chunk stays within 128 steps | §5, Appendix A |
| Grid | 100–120 balls, 10×10 or 10×12, four axes per Fly-Micro4 | §2.1 |
| Time | one timeline; nodes stay within 1 ms | `FR-SYNC-001` |
| Degrees of freedom | vertical position only | §2.3 |

The preview may show a violating take, with the offending ball and time
marked. Export and live CAN playback stay disabled until the report is clean.
The tool does not silently clamp a ball into range: clamping would hide the
designer's request and can move a sphere the pattern did not name.

### 6.5 3-D view and UX

- 10 × 10 / 10 × 12 grid at the real pitch, 3 m ceiling, 3 m floor, chrome
  spheres (environment map), soft shadows.
- Instanced rendering: 120 spheres is trivial for WebGL2/WebGPU at 60 fps.
- Timeline with play/pause, scrub, speed, loop, markers; per-ball selection and
  inspector; velocity/acceleration colour maps; trails optional.
- Camera presets (front, side, top, audience eye) and free orbit.
- Overlay diagnostics: min/max position, peak speed, peak step rate, CAN load,
  buffer depth, interpolation error.
- Fidelity toggles:
  - **Ideal:** continuous curve.
  - **Wire:** quantize to the selected format before display.
  - **Steps:** quantize to microsteps and show the step grid.
  This makes the 16-bit quantization visible before the hardware exists.

### 6.6 Export and live streaming

**Installation export** (`.hbp`, one HBP2 file). The sim watches
`.anim.json`, not this file (§6.9). A clean report is what produces it:

```
HBP2 header        # version, grid, cadence, window, codec, calibration, sha256
block table        # block offsets, sizes and CRC32
frames             # precomputed 8-byte cubic frames, window-major
```

The body is already the wire stream: `p0:u16, v0:s16, a0:s16, j:s16`, one frame
per motor per `N·Δt`. There is no raw 32-bit trajectory in the playback file.
`duration_ms` is the authored take; `playback_ms` is a whole number of windows
and may be up to one window longer. A short final window is padded to the full
`N·Δt` and finishes at rest, so the master can stop without a jump.
The block table makes the file streamable on the panel and allows a bad block
to stop the show rather than corrupt it. The `codec` field is `none` today;
a block codec can be added without changing the frame layout.

Include the exact sample format, cadence, axis order and calibration so the
file is self-describing. The embedded master reads HBP2 blocks from SD into a
PSRAM chunk cache ahead of the window; it never relies on continuous SD
streaming during playback and never fits or bakes motion. The cache sizing is
in `memory_map.md`.

**Live streaming:** a local bridge process (Python or Node) owns the USB-CAN
adapter; the web app talks to it over WebSocket. Do not try to drive a USB-CAN
adapter directly from browser JavaScript.

- Cross-platform adapter: CANable 2.0 in **slcan** mode (serial) or a PEAK
  PCAN-USB with its driver; `python-can` supports both.
- Linux production: SocketCAN (`can0`) with a Raspberry Pi or PC master.
- The bridge also implements the master scheduler: `1/Δt` sample ticks, sync
  frames, buffering, status logging, and a safe stop.
- For the installation, prefer an **embedded master** (RP2040/ESP32/Raspberry
  Pi) that plays the `.hbp` file without a PC. The PC is for authoring and
  bench work; a desktop OS is not a real-time controller.

### 6.7 Verification

| Level | What | Pass criterion |
|-------|------|----------------|
| Unit | Interpolator, monotone limits, step accumulator, frame pack/unpack | Host tests, property tests |
| Golden trace | Pattern → samples → chunks; compare host vs firmware dump | Bit-identical chunk stream |
| Single node HIL | 4 motors, `N·Δt` messages with configurable `Δt`, scope on STEP/DIR | Step times within one 62.5 ns tick of the core prediction; no underrun |
| Sync | 2+ nodes, scheduled event, scope | Skew ≪ 1 ms (target < 100 µs) |
| Bus | 30 nodes / traffic generator at 40–60 % | No error frames; bounded latency; buffer never empty |
| Visual | Slow-motion camera vs simulation | No visible step, jerk or seam |
| Fault injection | Kill a frame, a node, the master, the bus | Controlled stop/hold; no jump; safe |

The golden trace is the key artifact: it turns “the sim looks right” into a
reproducible comparison. The host-only sequence for that trace is
`motion_streaming_tdd.md`. Scope, homing repeatability, and bus electrical
checks stay on a bench.

### 6.8 Project layout

```
simulation/
├── package.json
├── vite.config.ts
├── index.html
├── src/
│   ├── main.ts
│   ├── viewer/               # Three.js scene, instancing, materials
│   ├── timeline/             # authoring UI
│   ├── patterns/             # watch .anim.json, bake, write .report.json
│   ├── diagnostics/          # CAN load, step rate, limits from limits.h
│   ├── wasm/                 # core bindings
│   └── bridge/               # WebSocket client for CAN playback
├── wasm/
│   └── build.sh              # emcc -I../../lib/motion_core
└── patterns/                 # .anim.json sources, generators, reports, .hbp exports
```

The core stays in `lib/motion_core` (repository root) so PlatformIO builds it
directly via `lib_extra_dirs = ../lib` and the simulation includes it by path.
If the project later prefers a root `core/`, move it once and update both build
files; do not fork the code.

### 6.9 Live animation file

The simulation is a live view of an animation-data file. Saving the file
updates the running preview.

| File | Role |
|------|------|
| `patterns/<name>.anim.json` | Authoring source. Schema version 1, below. This is the file the sim watches |
| `patterns/<name>.report.json` | Written on every bake: violations with ball, time, limit, measured value, allowed value. Empty `violations` means the take may be exported |
| `patterns/<name>.hbp` | One HBP2 playback file with precomputed wire frames. Written only from a clean report |
| `patterns/<name>.py` | Optional generator. A person or an agent runs it; it writes the `.anim.json`. The checker and the sim read only the JSON |

Version 1 is a JSON object. Every number is an integer. A missing required
field, or a numeric field that is not an integer, fails the parse.

| Field | Required | Values |
|-------|----------|--------|
| `version` | yes | `1` |
| `grid` | yes | `[cols, rows]`: `[10, 10]` or `[10, 12]` |
| `dt_ms` | yes | `10`, `11`, or `15` |
| `n` | yes | `3` or `4` |
| `duration_ms` | yes | length of the take, in milliseconds |
| `tier` | yes | `ordinary`, `nominal`, or `optional` |
| `base_um` | no | rest position of every ball, default `1000000` (1.000 m) |
| `layers` | yes | array of layer objects |

All layer offsets are added to `base_um`, so layers sum. Every field is an
integer. An unknown `op` or a non-integer number fails the parse.

#### Layer fields

These fields are accepted on any layer. A field that does not apply to the
selected `op` is ignored.

| Field | Required | Used by | Default | Meaning |
|-------|----------|---------|---------|---------|
| `op` | yes | all | — | `keyframes`, `sine`, `travelling_wave`, `interference`, `spiral`, `ripple`, `cascade`, `random_walk` |
| `amplitude_um` | no | sine, travelling_wave, interference (wave 1), spiral, ripple, cascade, random_walk | `0` | signed peak offset in µm |
| `period_ms` | no | sine, travelling_wave, interference (per wave), spiral, ripple, cascade, random_walk | `0` | wave period or random-walk waypoint interval; `≤ 0` disables time variation (random_walk returns 0) |
| `wavelength_cols` | no | sine, travelling_wave, interference (per wave), spiral, ripple | `0` | spatial wavelength in columns; `≤ 0` makes all balls move together |
| `phase_deg` | no | sine, travelling_wave, interference, spiral, ripple | `0` | phase offset in degrees, reduced modulo 360 |
| `phase_deg` | no | cascade | `0` | **delay in ms per diagonal step**; `≤ 0` means 1 ms |
| `axis` | no | sine, travelling_wave, interference (wave 1) | `"col"` | `"col"` uses the column index, `"row"` uses the row index |
| `direction` | no | travelling_wave | `1` | negative reverses the spatial direction; ignored by sine |
| `seed` | no | random_walk | `0` | 32-bit PRNG seed |
| `amplitude2_um` | no | interference | `0` | peak offset of the second wave |
| `period2_ms` | no | interference | `0` | period of the second wave |
| `wavelength2_cols` | no | interference | `0` | spatial wavelength of the second wave |
| `phase2_deg` | no | interference | `0` | phase of the second wave |
| `axis2` | no | interference | `"col"` | axis of the second wave |
| `keys` | no | keyframes | `[]` | array of `{ "t_ms": int, "pos_um": int }`, sorted by `t_ms`, linearly interpolated and clamped at the ends |
| `group` | no | all | all balls | restrict the layer, see below |

#### Group fields

`group` limits a layer to a subset of balls and also supplies the centre for
`spiral`, `ripple` and `ring`. If `group` is omitted, every ball is included.

| Field | Required | Default | Meaning |
|-------|----------|---------|---------|
| `kind` | no | `"all"` | `all`, `row`, `column`, `ring`, `checkerboard`, `mask` |
| `index` | row/column only | `0` | row or column number |
| `center` | spiral/ripple/ring | `[0, 0]` | `[col, row]` wave or ring centre |
| `radius` | ring only | `0` | Chebyshev distance from `center` |
| `parity` | checkerboard only | `0` | `0` = even `(col + row)`, `1` = odd |
| `balls` | mask only | `[]` | explicit ball indices, `row × cols + col` |

`spiral` and `ripple` use `group.center` even when `kind` is `all`. The
default centre is the top-left corner, so set `center` explicitly.

#### Op formulas

`A` is `amplitude_um`, `T` is `period_ms`, `λ` is `wavelength_cols`, `φ` is
`phase_deg`, and `i` is the column or row selected by `axis`.

| `op` | Offset at time `t` |
|------|--------------------|
| `sine` | `A · sin(2π(t/T + i/λ) + φ)` |
| `travelling_wave` | `A · sin(2π(t/T + direction·i/λ) + φ)` |
| `interference` | wave 1 with `(A, T, λ, φ, axis)` plus wave 2 with `(A2, T2, λ2, φ2, axis2)` |
| `spiral` | `A · sin(2π(t/T + r/λ) + φ)`, where `r = floor(√((col−cx)² + (row−cy)²))` |
| `ripple` | same radial formula as `spiral` in version 1 |
| `cascade` | `A · sin(2π·(t − (col+row)·delay)/T)`, `delay = phase_deg > 0 ? phase_deg : 1` ms |
| `random_walk` | seeded per-ball waypoints every `T`, smoothstep between them, scaled by `A` |
| `keyframes` | linear interpolation of `keys`; clamped before the first and after the last key |

`spiral` and `ripple` are aliases today; a later version may add an angular
term to `spiral`. The radial distance is Euclidean and truncated to an integer
before the spatial phase is computed. `cascade` reuses `phase_deg` as a delay,
not as a phase; `wavelength_cols` is not used by cascade.

The checked-in parity file is `patterns/golden_sine.anim.json`:

```json
{
  "version": 1,
  "grid": [10, 12],
  "dt_ms": 10,
  "n": 3,
  "duration_ms": 1000,
  "tier": "ordinary",
  "layers": [
    {
      "op": "sine",
      "amplitude_um": 50000,
      "period_ms": 5000,
      "wavelength_cols": 10,
      "axis": "col"
    }
  ]
}
```

A layer from each op family (each object goes in the top-level `layers`
array):

```json
[
  { "op": "travelling_wave", "amplitude_um": 80000, "period_ms": 6000,
    "wavelength_cols": 10, "axis": "col", "direction": -1, "phase_deg": 0 },
  { "op": "interference", "amplitude_um": 70000, "period_ms": 7000,
    "wavelength_cols": 10, "phase_deg": 0, "axis": "col",
    "amplitude2_um": 50000, "period2_ms": 11000, "wavelength2_cols": 6,
    "phase2_deg": 120, "axis2": "col" },
  { "op": "spiral", "amplitude_um": 60000, "period_ms": 8000,
    "wavelength_cols": 4, "phase_deg": 0,
    "group": { "center": [4, 5] } },
  { "op": "ripple", "amplitude_um": 50000, "period_ms": 5000,
    "wavelength_cols": 3, "phase_deg": 0,
    "group": { "center": [4, 5] } },
  { "op": "cascade", "amplitude_um": 70000, "period_ms": 4000,
    "phase_deg": 120 },
  { "op": "random_walk", "amplitude_um": 40000, "period_ms": 3000,
    "seed": 1234 },
  { "op": "keyframes", "keys": [
      { "t_ms": 0, "pos_um": 0 },
      { "t_ms": 1000, "pos_um": 50000 },
      { "t_ms": 4000, "pos_um": -50000 }
    ] }
]
```

Peak speed for the sine parity file is `0.05 m × 2π / 5 s ≈ 0.063 m/s`,
inside the ordinary tier. Other cadences are this file with `dt_ms` in
{10, 11, 15} and `n` in {3, 4}. A violation case raises `amplitude_um` until
the selected tier is exceeded. The host test may run
`tests/fixtures/write_sine.py` to produce a version-1 file. Passing that `.py`
path to the checker is a parse error.

- Watch the `.anim.json` (mtime, then a content hash). Debounce bursts by
  50 ms, rebake on a worker, and swap the trajectory in place.
- Keep the camera, the selection and the playhead. The new samples apply from the current time forward; the view does not jump back to t = 0 on every save.
- A file that does not parse keeps the last good take on screen and shows the parse error. The previous report stays, with the parse error appended, so a watcher can see that the reload failed.
- The **Wire** and **Steps** fidelity toggles in §6.5 apply to the reloaded take, so quantization and the step grid stay visible while the file is being edited.
- `Δt` and `N` are fields of the file. Changing them rebakes the whole take; they are not animated parameters inside a playing pattern (§4.2).

### 6.10 Who edits the file

A designer can edit the JSON by hand. The usual programmatic path is a Python
script that writes the JSON: waves, interference, a seeded cascade, a
spreadsheet of keyframes. The same script is what an agent edits.

An agentic coder (OpenCode CLI, or any agent that can read and write files in
this repository) is the LLM interface. It is not built into the viewer.

- The agent reads `.anim.json` and `.report.json`, changes the JSON or the
  Python generator, and runs the generator if there is one.
- The running simulation picks up the new JSON and rewrites the report.
- The agent stops when `violations` is empty, or when the designer accepts the
  picture. It does not declare success from the script's own printout.
- The agent does not emit CAN frames, step pulses, node IDs or firmware. The
  shared core is the only packer. Playback to hardware still goes through the
  bridge in §6.6, and only after a clean report.
- No model API key lives in the browser. The agent brings whatever model it is
  configured with. The installation does not depend on a provider.

This splits the work the way the hardware already does. The file is the
pattern. The core checks it against the bus and the mechanics. The viewer
shows the result while it is running. The agent is another editor of the file.

---

## 7. Interfaces and contracts

| Contract | Definition |
|----------|------------|
| Time | `int64` µs. Master, local, and eval time are §4.9. Sync carries `t_us_low`, `dt_us`, `n`, and `seq` (§4.5) |
| Position | int32 µm in the core; wire format is 16-bit full-range (0–2000 mm; 65535 = 2000 mm; LSB 0.0305 mm) |
| Axis order | Drive0–Drive3 are local axes 0–3. The motor field is assigned in `Fly_Micro4_addressing.md` §2 (field 63 is skipped). Grid position 0–119 is a host map from `(uuid, axis)` and is unknown to the board |
| Sample | Absolute position at a timeline tick; no deltas in the baseline |
| Playback | One cubic per message: `p0, v0, a0, j`. Integer Horner, §4.7 |
| Step chunk | 2 ms, `|steps| ≤ 128`, `ticks ∈ [3200, 65535]` at 16 MHz |
| Fault | Hold/decelerate, report, never jump; E-stop is hardware |
| Identity/addressing | `Fly_Micro4_addressing.md` |
| Pattern file | Version-1 `.anim.json` is the watched source (§6.9). `.hbp` is the baked export. `.report.json` is the limit report |

Any change to the wire format, interpolation or step limits must update the
shared core, the host tests and this table together.

---

## 8. Risks and mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Classic CAN bandwidth | 10 ms message rate impossible; `N·Δt` fits | Choose `N` (3/4) and `Δt` (10/11/15) by load test; per-node A if needed |
| 30.5 µm quantization at very slow speed | Audible/visible ripple | Speed field + limited cubic; measure at the slowest intended speed |
| PIO block conflict | CAN or steppers fail to initialize | Init can2040 first; hard 4-axis limit; assert at boot |
| `FasTimed` experimental | API/behavior change | Pin/vendor; wrap behind a small interface; fallback custom chunker |
| Direction reversals | DIR pause, thread slack, noise | Monotone cubic, deadband/hysteresis, rare reversals, account for pause |
| CAN termination | Over-termination, reflections, errors | Two 120 Ω ends only (baseline table): Waveshare jumper and last Fly-Micro4 with J3; all other boards open (no 120 Ω). Scope test |
| 2-pin CAN without GND | Common-mode shift | Common 24 V/ground star; keep motor return separate; measure |
| Node reset | Four balls drop/descend | Accept per FR-MOT-011; watchdog cause log; re-home |
| can2040 no bus-off | Silent bus fault | Error counters/timeouts, heartbeat, hold/stop |
| PC not real-time | Starved buffer during show | Embedded master for the installation; PC only for authoring/HIL |
| Simulation drift | Preview ≠ hardware | Shared integer core, golden traces, HIL |
| Homing repeatability | Position offset per ball | StallGuard calibration; physical endstop fallback |
| Pin map/revision mismatch | Damage or wrong motion | Verify schematic/silkscreen; `board_pins.h`; meter check |

---

## 9. Roadmap

| Phase | Deliverable | Exit criterion |
|-------|-------------|----------------|
| W0 | Bench one Fly-Micro4 + 4 motors + USB-CAN | can2040 + FAS coexist; 4 axes step correctly; termination understood |
| W1 | Protocol + shared core + host tests | `motion_streaming_tdd.md` host gate: golden traces pass; worst-case chunk/tick asserts |
| W2 | One node HIL | `N·Δt` messages, 4 axes, sync < 1 ms, homing, fault injection |
| W3 | 8 nodes / 32 axes | 40–60 % bus, PSU/thermal/termination validated |
| W4 | 30 nodes / 120 axes | Full pattern, visual/noise test, 60 s homing |
| W5 | Designer tool | Live reload of `.anim.json`; export blocked while `.report.json` lists violations; sim/hardware match |
| W6 | Installation | Embedded master, logging, scheduled operation, maintenance |

**Immediate next steps**

1. Confirm the Fly-Micro4 revision: CAN pins, step/dir/en pins, flash size,
   termination resistors, DIAG routing.
2. Order one USB-CAN adapter (slcan preferred for cross-platform) and a logic
   analyser.
3. Keep the cubic coefficient frame in §4.2. Choose the segment length
   `N·Δt` (Δt of 10, 11, or 15 ms) by a bench quantization and jitter test
   on one ball at the slowest intended speed.
4. Stand up the shared core and the host tests in `motion_streaming_tdd.md`
   before writing firmware. That list is the work that needs no board.
5. Prototype the web viewer with a synthetic 120-ball sine field; add the WASM
   core once the API is stable.

---

## 10. Appendix A — formulas and numbers

```
Steps/mm (40 mm drum on the stepper shaft, ratio 1, 200 steps/rev).
Microstepping is TBD; this block is the 1/16 worked case:
  3200 / (π × 40 mm) = 25.4648 steps/mm
  step = 39.27 µm

Step rate:
  f = v × 25.4648 steps/mm
  0.6 m/s → 15.28 kstep/s
  1.2 m/s → 30.56 kstep/s

FasTimed chunk steps:
  steps = f × chunk_s
  1.2 m/s, 2 ms → 61.1   (OK)
  1.2 m/s, 4 ms → 122.2  (OK)
  1.2 m/s, 5 ms → 152.8  (over 128)

CAN frame (standard, 8 B):
  ~111 bits + stuffing ≈ 130 bits → 130 µs @ 1 Mbit/s
  ceiling ≈ 7.7 kframes/s, ~61 kB/s payload

Bus load:
  frames/s = 120 / (N·Δt)
  load = frames/s × 130 bits / 1e6
  N=3, Δt=10 ms → 4,000 → 52 %
  N=3, Δt=11 ms → 3,636 → 47 %
  N=3, Δt=15 ms → 2,667 → 35 %
  N=4, Δt=10 ms → 3,000 → 39 %
  N=4, Δt=11 ms → 2,727 → 35 %
  N=4, Δt=15 ms → 2,000 → 26 %
  10 ms message rate → 12,000 → 156 %  (not feasible)
  30 nodes × 1 frame / 10 ms = 3,000 → 39 %  (per-node alternative)

16-bit full-range position:
  z = code / 65535 × 2000 mm     code 0 = 0 mm, code 65535 = 2000 mm
  LSB = 2000 mm / 65535 = 0.0305 mm = 0.001526 % FS
  code = round(z / 2000 mm × 65535)

Drift (35 ppm):
  20 ms → 0.7 µs
  100 ms → 3.5 µs
```

## 11. Appendix B — references

- `../FastAccelStepper/`: `FasTimed.h`, `FasNAxis.h`, `src/pd_pico/`,
  `examples/NaxesTimed/`, `extras/doc/FasTimed.md`.
- can2040: Kevin O'Connor, RP2040/RP2350 software CAN 2.0B, up to 1 Mbit/s,
  one PIO block per interface.
- `controller_analysis.md`: platform survey, CAN termination/grounding, PIO
  limits, custom carrier.
- `mechanical_analysis.md`: speed envelope, drum/step rate, resolution.
- `system_requirements.md`: `FR-MOT-*`, `FR-SYNC-001`, `FR-ARCH-001`,
  `FR-SIM-001`.
- Mellow Fly-Micro4 schematic: `boards/Fly-Micro4/Fly-Micro4-schematic.pdf`;
  confirm the exact revision before assigning pins.
- Official Mellow Fly-Micro4 pin map and Klipper reference:
  `boards/Fly-Micro4/Fly_Micro4_pinmap.md`,
  `boards/Fly-Micro4/Fly_Micro4_klipper_reference.cfg`.
- Node identity and CAN-ID allocation: `Fly_Micro4_addressing.md`.
- Host TDD plan (no hardware): `motion_streaming_tdd.md`.
