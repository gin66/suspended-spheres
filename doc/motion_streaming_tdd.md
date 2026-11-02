# Motion streaming — host TDD plan

> Test-first work that needs no Fly-Micro4, motor, transceiver, CAN adapter,
> or logic analyser. The contracts are `motion_streaming_whitepaper.md` and
> `Fly_Micro4_addressing.md`. Bench work stays in the whitepaper §5.9 and
> roadmap W0, W2–W4.
>
> Write one failing test, then the smallest code that makes it pass. Do not
> implement a phase ahead of its tests. Numbers below are the assertions;
> the derivations stay in the whitepaper.

---

## 0. Rules for this list

- The shared core is C++ with the integer types in the whitepaper §5.4.
  Host tests and the WASM build call that core. The renderer may use
  floating point; the core may not.
- A simulated clock and an in-memory frame log stand in for the bus. No
  PIO, no can2040, no GPIO, no serial port.
- A single host executable and one assertion macro are enough. Add a test
  framework only when this list outgrows that harness.
- Tests live next to the core (`rp2040/lib/motion_core/tests/`), as in the
  whitepaper §5.10. They run on the development machine.
- Where the papers describe a payload but do not yet name every byte, the
  first test of that payload is a layout table checked into the same change.
  Do not invent a second layout in the test and a different one in the code.
- Items marked **freeze** are that layout table. Items marked **property**
  generate many inputs from one rule.

**Out of this list** (needs hardware or a scope): PIO block ownership,
can2040 on the wire, termination and grounding, USB-CAN adapter throughput,
StallGuard thresholds, DIAG pins, real flash unique-ID, step-pulse timing
against a logic analyser, homing repeatability, acoustic noise, PSU and
thermals.

---

## 1. Harness

- [ ] **H1** The host target builds the core and the tests with no Pico SDK,
  no PIO headers, and no CAN socket. The test binary exits 0 when every
  assertion holds.
- [ ] **H2** A compile-time check fails if `TICKS_PER_S` is not 16 000 000,
  `MIN_CMD_TICKS` is not 3 200, or the per-chunk step limit is not 128.
  These are the `FasTimed` limits from §5.3, copied as constants the host
  can see.
- [ ] **H3** A source check fails if the core directory contains a `float`
  or `double` type. Conversion to floating point is allowed only in the
  renderer and in test printers.
- [ ] **H4** The public C ABI in §6.3 (`mc_trajectory_push`, `mc_eval`,
  `mc_step_chunk`, `mc_pack_frame`, `mc_unpack_frame`) links from a C
  caller. Names and integer widths match the header.

---

## 2. Units and quantization

Whitepaper §3.2, §4.2, Appendix A.

- [ ] **U1** Code 0 converts to 0 µm. Code 65 535 converts to 2 000 000 µm.
- [ ] **U2** Conversion uses `code × 2 000 000 / 65 535` in integer
  arithmetic. A test that instead multiplies by 0.0305 mm diverges from
  the integer result across the travel; the core matches the integer form.
- [ ] **U3** `code = round(z / 2 000 mm × 65 535)` round-trips every code
  in `0 … 65 535` back to itself.
- [ ] **U4** One code step is 30.5 µm at 2 m, which is below the 50 µm
  visual limit (`FR-MOT-002`) and below the 39.3 µm microstep.
- [ ] **U5** Speed unit is 0.04 mm/s, signed 16-bit. +32 767 encodes
  +1 310.68 mm/s. −32 768 is the negative rail. 1.2 m/s encodes as 30 000.
  A value past ±1 310 mm/s is rejected, not wrapped.
- [ ] **U6** A 0.02 mm/s LSB does not cover 1.2 m/s (the core's speed unit
  is 0.04 mm/s). This locks the §3.2 recommendation.
- [ ] **U7** Positions and speeds in a frame are little-endian.
- [ ] **U8** Position domain in the core is `0 … 2 500 000` µm. A sample
  outside `0 … 2 000 000` µm is out of the wire range (§4.2).

---

## 3. 11-bit identifiers

`Fly_Micro4_addressing.md` §2 and §4.

- [ ] **I1** `id = (field << 4) | type`, and `id` is in `0 … 0x7FF`. A
  value above 0x7FF is rejected. There is no 29-bit encoder.
- [ ] **I2** Type 0 and type 1 carry a motor ID in `1 … 120`. Field 0 with
  either motion type is rejected. Motor IDs `121 … 127` are rejected.
- [ ] **I3** Type 2 (sync) requires field 0.
- [ ] **I4** Types 3–7 carry a node ID in `1 … 30`, or 0 for a broadcast.
  Node ID 31 is rejected.
- [ ] **I5** Types 8, 10 and 11 (`DISC`, `SET_ID` A/B) require field 0.
  Type 9 (`HELLO`) carries the short node ID.
- [ ] **I6** Types 12–15 are rejected.
- [ ] **I7** Motor 5 / type 0 and node 5 / type 3 produce different
  identifiers. The decoder reports a motor for types 0–1 and a node for
  types 3–7 and 9.
- [ ] **I8** Node *n* owns motors `4(n−1)+1 … 4(n−1)+4`. Node 1 owns 1–4.
  Node 30 owns 117–120. Node 0 and node 31 own nothing.
- [ ] **I9** Node *n* accepts motion for its four motors, frames addressed
  to node *n*, and field 0. A motion frame for any other motor is discarded
  before the payload is parsed. A table drives all 30 nodes.
- [ ] **I10** **Property.** For every motor in `1 … 120` and both motion
  types, exactly one node accepts the frame.

---

## 4. Payloads

Whitepaper §4.2, §4.3, §4.5. Addressing §3.

- [ ] **P1** An `N=3` motion payload is exactly 8 bytes: three unsigned
  16-bit positions and one signed 16-bit speed, little-endian. Pack and
  unpack are inverses, including a negative speed.
- [ ] **P2** An `N=4` motion payload is exactly 8 bytes: four unsigned
  16-bit positions. Pack and unpack are inverses.
- [ ] **P3** A payload shorter or longer than 8 bytes is rejected.
- [ ] **P4** **Freeze**, then test, the sync frame: 32-bit master time in
  µs, plus the active `Δt` and `N`, in one or two 8-byte frames as the
  layout table says. A sync ID is field 0, type 2.
- [ ] **P5** **Freeze**, then test, the status frame: 8 bytes, type 3,
  carrying state, fault flags, an 8-bit following-error bucket per axis,
  buffer depth, and the last sequence. It does not carry a continuous
  120-axis position stream.
- [ ] **P6** Command, home, and parameter frames are types 4, 5, and 6.
  A motion decoder ignores them. Homing bytes are not part of the motion
  sample buffer.
- [ ] **P7** `DISC` payload is exactly the 8-byte flash ID. `HELLO`
  payload is the same 8 bytes, with the short node ID in the CAN ID.
- [ ] **P8** One `SET_ID` frame cannot hold an 8-byte flash ID plus a node
  ID plus flags. The codec requires two frames, types 10 then 11, field 0.
  **Freeze** the split, then test both directions.
- [ ] **P9** The per-node alternate (four 16-bit positions, one frame per
  node) packs and unpacks. A flag on the unpacked frame says it is not the
  per-motor baseline.
- [ ] **P10** `mc_pack_frame` / `mc_unpack_frame` follow P1 and P2 for the
  variants in §6.3.

---

## 5. Bus budget and send-ahead

Whitepaper §3.3, §4.6, Appendix A. The test calls one function. The
expected column is the whitepaper table, so a changed bit-stuffing
assumption fails here.

- [ ] **B1** Frame cost is 130 bits at 1 Mbit/s (130 µs). Ceiling is
  7 700 frames/s.
- [ ] **B2** Motion frames/s = `120 / (N·Δt)` for `Δt` in seconds.

  | `N` | `Δt` | Frames/s | Load |
  |-----|------|----------|------|
  | 3 | 10 ms | 4 000 | 52 % |
  | 3 | 11 ms | 3 636 | 47 % |
  | 3 | 15 ms | 2 667 | 35 % |
  | 4 | 10 ms | 3 000 | 39 % |
  | 4 | 11 ms | 2 727 | 35 % |
  | 4 | 15 ms | 2 000 | 26 % |

- [ ] **B3** Adding a 50 Hz sync and 30 × 10 Hz status (350 frames/s)
  raises those loads to 57, 52, 39, 44, 40, and 31 %.
- [ ] **B4** A 10 ms *message* period (12 000 frames/s) is 156 % and is
  reported as over budget. The limit used by the pattern checker is 60 %.
- [ ] **B5** 120 motion frames occupy 15.6 ms of bus time.
- [ ] **B6** The scheduler starts a window 15–20 ms before the nominal `T`
  and finishes before `T+Δt`. A plan that starts at `T` fails the test.
- [ ] **B7** **Property.** Load scales as `1/(N·Δt)` for `Δt` in
  {10, 11, 15} ms and `N` in {3, 4}. Frame count does not change when the
  step rate or the microstep setting changes (`FR-ARCH-002`).
- [ ] **B8** `Δt` and `N` applied while a segment is in flight are
  rejected. The same change at a pattern boundary is accepted (§4.2).

---

## 6. Interpolation

Whitepaper §3.4, §4.7.

- [ ] **C1** With one message and no previous message, `mc_eval` returns
  "not primed". Playback does not start.
- [ ] **C2** After a home state `P(T)`, `V = 0`, the first future message
  primes the axis. A second message is the other legal way to prime.
- [ ] **C3** `N=3`: the cubic on `[T, T+3Δt]` uses `V(T)` from the previous
  message's `v3` and `V(T+3Δt)` from the current `v3`. Endpoints match
  those positions in µm. Interior `p2` and `p3` are checkpoints: a cubic
  that misses a checkpoint by more than one code step is a fault, and the
  checkpoints do not move the endpoints.
- [ ] **C4** At a message boundary the outgoing speed equals the next
  message's incoming speed when both `v3` values agree. Sampled velocity
  on either side of `T+3Δt` agrees within one speed LSB.
- [ ] **C5** `N=4`: Catmull-Rom uses `P(T)` from the previous message and
  `P(T+Δt) … P(T+4Δt)` from the current one. Before the next message
  arrives, the tangent at `T+4Δt` is the `p3–p4` chord. When the next
  message arrives, the tangent becomes its `p1`.
- [ ] **C6** Monotone limit: for a non-decreasing pair of endpoints the
  cubic does not go below the lower endpoint or above the upper one.
  The tangent bound is `|m| ≤ 3·|ΔP|/h`.
- [ ] **C7** A direction reversal that would overshoot is limited so the
  position does not reverse twice inside the segment.
- [ ] **C8** A segment whose implied acceleration exceeds the configured
  limit (1.5 m/s² nominal, 4 m/s² optional) is rejected. The axis holds.
  The core does not clamp the sample into range.
- [ ] **C9** Evaluation is a pure function of the timeline. Calling
  `mc_eval` twice at the same `t_us` returns the same µm. Injecting a
  different step-counter value does not change the result (`§3.4`,
  open loop).
- [ ] **C10** **Property.** Random monotone keyframes inside the travel
  stay inside their endpoints at 100 query times per segment. All
  arithmetic uses `int64_t` intermediates; the test compares against an
  independent integer implementation, not against `float`.

---

## 7. Steps and chunks

Whitepaper §4.7, §5.3, §5.4, Appendix A.

- [ ] **S1** The Q16.16 steps/mm constant represents
  `3200 / (π × 40 mm) = 25.4648 steps/mm`. Full-scale 2 000 mm converts
  to a step count within 1 step of `round(2 000 × 25.4648)`.
- [ ] **S2** A fractional-step accumulator is kept per axis. Summing chunk
  deltas across 2 000 mm equals the net step count from S1. Rounding each
  chunk on its own is the failing control: that sum drifts by more than
  one step.
- [ ] **S3** At 1.2 m/s a 2 ms chunk is 61 steps. At 0.6 m/s it is 31
  steps. Both are inside `[-128, +128]`.
- [ ] **S4** A requested 5 ms chunk at 1.2 m/s (153 steps) is rejected.
  A 10 ms chunk (306 steps) is rejected. A 2 ms chunk is accepted.
- [ ] **S5** A 2 ms chunk at 16 MHz is 32 000 ticks. Ticks lie in
  `[3 200, 65 535]`. A chunk shorter than 200 µs is rejected.
- [ ] **S6** For `Δt` of 11 ms and 15 ms, chunk boundaries stay on the
  2 ms grid. The evaluator is queried at those boundaries, not at `Δt`.
- [ ] **S7** Four axes in one timed chunk share one tick count. A reversal
  on axis 0 does not change the step delta of axes 1–3.
- [ ] **S8** `mc_step_chunk` returns per-axis steps and the tick count for
  a 2 ms boundary. Steps match S3 for a constant-speed segment.
- [ ] **S9** An underrun flag, once set by the simulated feeder, stays set
  until an explicit clear and is reported as a fault.
- [ ] **S10** The chunk feeder's host double is a queue of `(steps, ticks)`
  with the §5.3 horizon of 8. `add` returns rejected when the horizon is
  full; after `pump` frees a slot, `add` succeeds. This double is not
  `FasTimed` and does not touch PIO. A later test may compare it with
  `FasTimed` on the host if that library links without a Pico; that
  comparison is optional and is not the gate.

---

## 8. Node playback

Whitepaper §4.8, §4.9, §5.5. One simulated node, four axes, a frame log,
and a clock the test advances.

- [ ] **N1** In the normal case the node keeps the current and the previous
  message and evaluates on the sync timeline, not on the arrival time.
- [ ] **N2** A missing message: at the end of the known segment the axis
  decelerates within the configured acceleration and then holds. It does
  not jump. At 1.2 m/s the jump that must not happen is 36 mm over a
  30 ms gap and 72 mm over a 60 ms gap.
- [ ] **N3** The next absolute message after N2 becomes the new anchor.
  Position error does not accumulate across the gap.
- [ ] **N4** If the following message is still absent when the buffer
  empties, the axis decelerates along its last velocity within the
  acceleration limit, holds, and raises a fault.
- [ ] **N5** A sample outside `0 … 2 000 mm` is rejected. The axis holds
  and raises a fault. The stored position is unchanged.
- [ ] **N6** While sync frames arrive every 20–100 ms, a 35 ppm clock
  error (0.7 µs per 20 ms, 3.5 µs per 100 ms) stays under 1 ms
  (`FR-SYNC-001`). The corrector changes the rate. It does not add a
  position step.
- [ ] **N7** After the sync timeout the node holds. Free-running without
  a timeout fails the test.
- [ ] **N8** A CAN fault or a sample timeout decelerates to rest, holds,
  and reports. The simulated hardwired E-stop is a separate input; a
  type-7 CAN frame does not clear it and does not replace it.
- [ ] **N9** The receive ring stores bytes and a timestamp. Parsing runs
  in the motion task. A test producer fills the ring; the motion step
  observes no parse call inside the producer.
- [ ] **N10** A frame for another node's motor never updates this node's
  sample buffer (I9, applied to the live node).
- [ ] **N11** A following-error injected on the step counter raises a
  fault and leaves the evaluated curve unchanged (C9).
- [ ] **N12** One node at fault does not change the sample buffers of a
  second simulated node (`NFR-REL-003`).
- [ ] **N13** When the simulated master stops sending, each node
  decelerates within its limit and keeps its step position. The test
  does not require a re-home (`NFR-REL-006`). A simulated reset does
  require a new prime before motion.
- [ ] **N14** **Property.** Across 1 000 random dropouts of a single
  motor's frame, position stays continuous and inside the travel, and
  the following good frame restores the absolute sample.

---

## 9. Commissioning logic

Addressing §1–§3. No flash chip: the test supplies an 8-byte ID.

- [ ] **M1** The master table is `flash_id → short_id → grid_position`.
  Lookup is exact for 30 entries.
- [ ] **M2** A second node that advertises an already assigned flash ID
  is rejected. A second node that claims an already assigned short ID
  is rejected.
- [ ] **M3** An unassigned node emits `DISC` only. An assigned node emits
  `HELLO` with its short ID and its flash ID.
- [ ] **M4** Backoff is a pure function of the flash ID (10-bit hash ×
  5 ms). The same ID always yields the same delay. The identifier of
  that frame stays 11-bit.
- [ ] **M5** `SET_ID` is two frames (P8). After both frames the node's
  stored short ID matches the assignment, and the next frame it emits
  is `HELLO`.
- [ ] **M6** If the master table is empty and a node still has a stored
  short ID, the node sends `HELLO` and the master can reassign. A test
  covers both "table confirms" and "table reassigns".
- [ ] **M7** Discovery frames are absent from a playback trace. A test
  that injects `DISC` during an active pattern fails the scheduler check.

---

## 10. Pattern bake, limits, report, export

Whitepaper §6.4, §6.6, §6.9. Headless. No browser.

- [ ] **A1** **Freeze** a minimal `.anim.json` schema: grid, `Δt`, `N`,
  duration, and a layer list. A file missing any of those fields fails
  parse. The parser accepts `10×10` and `10×12`.
- [ ] **A2** The same `.anim.json` bakes to the same sample bytes on two
  runs. A one-bit change in a layer parameter changes the bytes.
- [ ] **A3** Layers that bake and pass a slow take: keyframes, sine,
  travelling wave, interference of two waves, spiral, ripple, cascade,
  and a seeded random walk. The seeded walk repeats for a repeated seed.
- [ ] **A4** A group (row, column, ring, checkerboard, mask) applies a
  parameter to those balls only. Balls outside the group stay at the
  base layer.
- [ ] **A5** The limit checker writes `.report.json` with one entry per
  violation: ball, time, limit name, measured value, allowed value.
  An empty `violations` array is the export gate.

  | Case | Expected |
  |------|----------|
  | Position outside 0–2000 mm | violation, no clamp |
  | Peak speed above the selected tier | violation |
  | Ordinary take above 0.3 m/s when the file selects the ordinary tier | violation |
  | Nominal tier peak inside 0.44–0.59 m/s and acceleration inside 1.0–1.5 m/s² | clean |
  | Optional tier peak inside 0.8–1.3 m/s and acceleration inside 2–4 m/s² | clean |
  | Downward acceleration ≥ 9.81 m/s² | violation (`FR-MOT-012`) |
  | Two positions at the same time that differ | violation (`FR-PAT-008`) |
  | A blend of about 1 s between two continuous takes | clean |
  | `Δt` / `N` that push the bus to or above 60 % | violation (B4) |
  | A 2 ms chunk that would exceed 128 steps | violation (S4) |

- [ ] **A6** `.hbp` is written only when `violations` is empty. It is a
  zip with `manifest.json` (grid, units, cadence, duration, calibration),
  `trajectory.bin` (int32 µm, little-endian), and `sha256.txt` over those
  members. A dirty report leaves no new zip, or leaves the previous zip
  untouched.
- [ ] **A7** Unpacking `.hbp` and running `mc_eval` reproduces
  `trajectory.bin`. Packing that trajectory with P1 or P2 reproduces the
  wire bytes the manifest names.
- [ ] **A8** A Python generator writes `.anim.json`. The checker reads
  only the JSON. A test points the checker at the `.py` path and expects
  a parse failure, which locks "the sim does not execute the script".
- [ ] **A9** A 120-ball sine inside the ordinary tier bakes clean, stays
  inside the travel, and stays under 60 % bus load at the file's `Δt`
  and `N`.
- [ ] **A10** Wire-fidelity samples differ from the ideal curve by at most
  half a code step (15.3 µm). Step-fidelity samples lie on the microstep
  grid of 39.3 µm. Both are functions of the same core.

---

## 11. Live file state

Whitepaper §6.9. A temp directory and a fake clock. No window, no WebGL.

- [ ] **L1** A content change of `.anim.json`, after the debounce interval,
  replaces the trajectory. Two writes inside the debounce window cause
  one bake.
- [ ] **L2** The playhead, the selected ball, and the camera pose are
  unchanged by a reload. Samples from the current time forward come from
  the new bake. Time is not reset to 0.
- [ ] **L3** A file that does not parse keeps the last good trajectory.
  The report gains a parse error and keeps the previous violations.
- [ ] **L4** Changing `Δt` or `N` in the file rebakes the whole take from
  the start of the file. The in-flight-segment rule (B8) still applies
  to a playing node; the file change is a new take, not a mid-segment edit.
- [ ] **L5** Export and the simulated CAN playback entry point stay
  disabled while `violations` is non-empty, and become enabled on the
  next clean report.
- [ ] **L6** A watcher that is shown a `.hbp` write does not treat it as
  an authoring change. Only `.anim.json` is the watched source.

---

## 12. Native and WASM parity

Whitepaper §6.3, §6.7. Still no board.

- [ ] **W1** The same core sources compile to WASM. A host driver calls
  the native binary and the WASM binary with one golden `.anim.json`.
- [ ] **W2** Sample bytes, chunk bytes, packed CAN frames, and the report
  JSON match bit for bit between the two builds.
- [ ] **W3** The golden file is checked in. A change to interpolation,
  units, or chunking that is intentional updates the golden in the same
  change; an unintentional change fails W2.

---

## 13. Suggested order

The list is already in dependency order. A practical cut:

1. Phase 1 and phase 2, so later tests print real units and real IDs.
2. Phase 3 (P1, P2, P3, P10) and phase 5 (B1–B5, B7).
3. Phase 6 (C1–C8) and phase 7 (S1–S8).
4. Phase 8 (N1–N8, N13) and phase 4 payloads that still need a freeze
   (P4, P5, P8).
5. Phase 9 and phase 10, which are the designer loop.
6. Phase 11, then phase 12.

Phase 12 is the host gate named in roadmap W1: golden traces pass, and
the worst-case chunk and tick asserts (S3–S5) pass. W0 and the scope
tests stay on a bench.

---

## 14. Definition of done for the host

- Every box in §1–§12 is checked, or an item is explicitly deferred with
  a one-line reason in this file.
- `N=3` at `Δt` of 10, 11, and 15 ms, and `N=4` at the same three values,
  each have at least one clean golden and one violation golden.
- The core directory still has no `float`, no PIO include, and no CAN
  device open.
- The whitepaper tables cited in B2, B3, S3, and U1–U5 still match the
  tests. If a number changes, the paper and the test change together.
