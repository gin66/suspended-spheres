# Motion streaming — host TDD plan

> Test-first work that needs no Fly-Micro4, motor, transceiver, CAN adapter,
> or logic analyser. The contracts are `motion_streaming_whitepaper.md` and
> `Fly_Micro4_addressing.md`. Bench work stays in the whitepaper §5.9 and
> roadmap W0, W2–W4.
>
> Write one failing test, then the smallest code that makes it pass. Do not
> implement a phase ahead of its tests. Numbers below are the assertions;
> the derivations stay in the whitepaper.
>
> **Read first:** `motion_streaming_whitepaper.md` and `Fly_Micro4_addressing.md`.
> This file is the test list. Those two are the protocol. The decisions in §0.1
> are closed; do not reopen them in the host implementation.

---

## 0.1 Closed decisions

**Language and build.** C++17. The host harness is CMake (3.16 or newer) and
produces one executable, `motion_core_tests`. Firmware later uses PlatformIO
on the same sources. No Bazel, no fetched test framework.

**Assertions.** One `ASSERT(cond)` macro in the test directory. A failure
prints the file, the line, and the expression, then returns a non-zero status
from `main`. Catch2 and GoogleTest are not dependencies.

**WASM.** Emscripten, as in the whitepaper §6.8 (`emcc -std=c++17`). Phase 12
compiles the same sources and compares bytes with the native binary. Node
runs the WASM side. This phase is last.

**Clock.** Tests never sleep and never read the operating-system clock. One
type, in the test support code:

```cpp
struct PlaybackClock {
  void set_crystal_ppm(int32_t ppm);   // 0, or ±35 for the drift test
  void advance_us(int64_t dt_us);      // dt_us >= 0; moves master time by dt_us
  int64_t master_us() const;           // sync-frame timeline, int64 microseconds
  int64_t local_us() const;            // master time scaled by the crystal
  void on_sync(int64_t master_us);     // may change the correction rate
  int64_t eval_us() const;             // time passed to mc_eval
  int32_t correction_ppm() const;
  bool sync_lost(int64_t timeout_us) const;
};
```

`advance_us`, `local_us`, `on_sync`, `eval_us`, and `correction_ppm` follow
the integer rules in the whitepaper §4.9. `mc_eval` is called with
`eval_us()`. `sync_lost(timeout_us)` is true before the first sync, and true
again when local time since the last sync exceeds `timeout_us`. Phase 1 does
not implement this type. It is first required by the node tests in §8.

The file watcher does not own a clock. Its calls take `now_us` from the
test: `on_change(now_us, bytes)` and `poll(now_us)`. The debounce interval
is 50 ms.

**Layouts.** Assert the tables that already exist. Do not restate them here.

- Sync frame, 10 Hz status frame, and the version-1 `.anim.json` schema,
  including `patterns/golden_sine.anim.json`: whitepaper §4.5 and §6.9.
- `ASK`, `DISC`, and `ASSIGN`: `Fly_Micro4_addressing.md` §3. The board ID
  is volatile. The host map `(flash_id, axis) → position` is not on the wire.
- The other clean cadences and the violation case are built in the test from
  that golden, as §6.9 describes.
- `tests/fixtures/write_sine.py` is the generator in A8. The checker never
  runs it.

## 0.2 First implementation slice

As of 2026-11-03 the first session (harness + units) has been started in the
repository: the shared core lives in `lib/motion_core/`, the host harness in
`tests/`, and the firmware-only scaffold in `rp2040/` carries a PlatformIO
`[env:rpipico]`. Still open: the `patterns/golden_sine.anim.json` file and the
Python fixture. Leave `boards/`, `LICENSE`, and the EasyEDA project unchanged.

This plan is ready to implement. The first session covers §1 and §2 only,
in that order:

1. Read `motion_streaming_whitepaper.md` and `Fly_Micro4_addressing.md`.
2. Add the shared core `lib/motion_core/` (with `include/` and `src/`), the
   C++17 CMake target `motion_core_tests` in `tests/`, and the `ASSERT` macro
   from §0.1. `rp2040/` stays firmware-only and gets the PlatformIO target later.
3. Write H1–H4, then U1–U8. One failing test, then the code that passes it.

That session stops there. It does not add `PlaybackClock`, the identifier
tests (§3), the frame codecs beyond the endian check in U7, the golden
file, the Python fixture, or the WASM build.

**Progress note (2026-11-03, third slice).** §1–§3, §4 (P1–P3, P10),
§5 (B1–B5, B7), §6 (C1–C10) and §7 (S1–S10) are implemented and passing.
The identifier rules follow the updated addressing document: type 0 is
motion, type 1 is reserved and rejected. The retired `N=3` / `N=4` sample
packer has been replaced by the four-field cubic frame (P1–P3, P10), and
`mc_eval` / `mc_step_chunk` implement the §4.7 Horner playback and the
per-axis fractional-step accumulator.
Still open: node playback (§8), the remaining payloads P4–P9, B6/B8,
commissioning (§9), the pattern bake and file state (§10–§11), and the
golden file / WASM parity (§12).

Later byte layouts are the Markdown tables in the whitepaper §4.5 and in
`Fly_Micro4_addressing.md` §3. There is no separate schema file. A test
copies those offsets.

`set_crystal_ppm` and `on_sync` are independent. From a zeroed clock,
`set_crystal_ppm(35)` then `advance_us(100000)` returns `master_us() == 100000`
and `local_us() == 100003`, with residual `500000`. `on_sync` does not read
the crystal value. It sets `correction_ppm` from the measured intervals in
the whitepaper §4.9. Implement that type with the §8 tests, not in this slice.

---

## 0. Rules for this list

- The shared core is C++ with the integer types in the whitepaper §5.4.
  Host tests and the WASM build call that core. The renderer may use
  floating point; the core may not.
- Timed tests use `PlaybackClock` from §0.1 and an in-memory frame log.
  No PIO, no can2040, no GPIO, no serial port, no operating-system clock.
- The host executable and the `ASSERT` macro are §0.1.
- Tests live in the host harness (`tests/`), not inside the shared core, as in
  the whitepaper §5.10. They run on the development machine.
- Payload bytes and the animation schema are the whitepaper and the
  addressing note. A test that needs a different layout is a documentation
  change first.
- Items marked **property** generate many inputs from one rule. I10 and B7
  are exhaustive loops over their small domains. C10 and N14 use one
  xorshift32 seed, `0x12345678`, and print that seed with the failing input.
  No clock is used as a seed, and no property-test library is added.

**Out of this list** (needs hardware or a scope): PIO block ownership,
can2040 on the wire, termination and grounding, USB-CAN adapter throughput,
StallGuard thresholds, DIAG pins, real flash unique-ID, step-pulse timing
against a logic analyser, homing repeatability, acoustic noise, PSU and
thermals.

---

## 1. Harness

- [x] **H1** The host target builds the core and the tests with no Pico SDK,
  no PIO headers, and no CAN socket. The test binary exits 0 when every
  assertion holds.
- [x] **H2** A compile-time check fails if `TICKS_PER_S` is not 16 000 000,
  `MIN_CMD_TICKS` is not 3 200, or the per-chunk step limit is not 128.
  These are the `FasTimed` limits from §5.3, copied as constants the host
  can see.
- [x] **H3** A source check fails if the core directory contains a `float`
  or `double` type. Conversion to floating point is allowed only in the
  renderer and in test printers.
- [x] **H4** The public C ABI in §6.3 (`mc_trajectory_push`, `mc_eval`,
  `mc_step_chunk`, `mc_pack_frame`, `mc_unpack_frame`) links from a C
  caller. Names and integer widths match the header.

---

## 2. Units and quantization

Whitepaper §3.2, §4.2, Appendix A.

- [x] **U1** Code 0 converts to 0 µm. Code 65 535 converts to 2 000 000 µm.
- [x] **U2** Conversion uses `code × 2 000 000 / 65 535` in integer
  arithmetic. A test that instead multiplies by 0.0305 mm diverges from
  the integer result across the travel; the core matches the integer form.
- [x] **U3** `code = round(z / 2 000 mm × 65 535)` round-trips every code
  in `0 … 65 535` back to itself.
- [x] **U4** One code step is 30.5 µm at 2 m, which is below the 50 µm
  visual limit (`FR-MOT-002`) and below the 39.3 µm microstep.
- [x] **U5** Speed unit is 0.04 mm/s, signed 16-bit. +32 767 encodes
  +1 310.68 mm/s. −32 768 is the negative rail. 1.2 m/s encodes as 30 000.
  A value past ±1 310 mm/s is rejected, not wrapped.
- [x] **U6** A 0.02 mm/s LSB does not cover 1.2 m/s (the core's speed unit
  is 0.04 mm/s). This locks the §3.2 recommendation.
- [x] **U7** Positions and speeds in a frame are little-endian.
- [x] **U8** Position domain in the core is `0 … 2 500 000` µm. A sample
  outside `0 … 2 000 000` µm is out of the wire range (§4.2).

---

## 3. 11-bit identifiers

`Fly_Micro4_addressing.md` §2 and §4.

- [x] **I1** `id = (field << 4) | type`, and `id` is in `0 … 0x7FF`. A
  value above 0x7FF is rejected. There is no 29-bit encoder.
- [ ] **I2** Type 0 carries a motor field in `0 … 119`. Motor 0 is legal.
  Type 1 is reserved and rejected. Motor fields `120 … 127` are rejected.
  The checked implementation still rejects field 0; update it to this rule.
- [x] **I3** Type 2 (sync) requires field 0.
- [ ] **I4** Types 3–7 carry a board ID in `0 … 29`, or 127 for every board.
  Fields `30 … 126` are rejected. Field 0 is board 0.
- [ ] **I5** Types 8 (`DISC`) and 11 (`ASK`) require field 0. Type 10
  (`ASSIGN`) carries the new board ID in the field. Type 9 is rejected.
- [x] **I6** Types 12–15 are rejected.
- [x] **I7** Motor 5 / type 0 and board 5 / type 3 produce different
  identifiers. The decoder reports a motor for type 0 and a board for
  types 3–7. Type 9 is rejected (I5).
- [ ] **I8** Board *b* owns motors `4b … 4b+3`. Board 0 owns 0–3. Board 29
  owns 116–119. Board IDs outside 0–29 own nothing.
- [ ] **I9** Board *b* accepts motion for those four motors, frames
  addressed to board *b*, field 127 on types 3–7, and field 0 on types
  2, 8 and 11. Any other motion frame is discarded before the payload is
  parsed. A table drives all 30 boards.
- [ ] **I10** **Property.** For every motor in `0 … 119` (type 0), exactly
  one board accepts the frame.

---

## 4. Payloads

Whitepaper §4.2, §4.3, §4.5. Addressing §3.

- [x] **P1** A type-0 motion payload is exactly 8 bytes: one unsigned
  16-bit position and three signed 16-bit coefficients (`p0, v0, a0, j`),
  little-endian. Pack and unpack are inverses, including negative
  coefficients.
- [x] **P2** Type 1 is reserved; pack and unpack reject it. The retired
  `N=3` / `N=4` sample layouts are not motion frames.
- [x] **P3** A payload shorter or longer than 8 bytes is rejected.
- [ ] **P4** The sync frame matches the whitepaper §4.5 byte table. A sync
  ID is field 0, type 2. After `t_us_low` wraps, the reconstructed `int64`
  time gains `2^32` µs.
- [ ] **P5** The 10 Hz status frame matches the whitepaper §4.5 byte table.
  It carries no position and no velocity.
- [ ] **P6** Command, home, and parameter frames are types 4, 5, and 6.
  A motion decoder ignores them. Homing bytes are not part of the motion
  sample buffer.
- [ ] **P7** `DISC` and `ASSIGN` payloads are exactly the 8-byte flash ID,
  little-endian. `ASK` is 8 zero bytes.
- [ ] **P8** `ASSIGN` is one frame, type 10, field = board ID 0–29, payload
  = the flash ID. The board stores that field only when the payload matches
  its own flash ID. A second frame is not required.
- [ ] **P9** The per-node alternate (four 16-bit positions, one frame per
  node) packs and unpacks. A flag on the unpacked frame says it is not the
  per-motor baseline.
- [x] **P10** `mc_pack_frame` / `mc_unpack_frame` follow P1 and P2 for the
  motion variants in §6.3.

---

## 5. Bus budget and send-ahead

Whitepaper §3.3, §4.6, Appendix A. The test calls one function. The
expected column is the whitepaper table, so a changed bit-stuffing
assumption fails here.

- [x] **B1** Frame cost is 130 bits at 1 Mbit/s (130 µs). Ceiling is
  7 700 frames/s.
- [x] **B2** Motion frames/s = `120 / (N·Δt)` for `Δt` in seconds.

  | `N` | `Δt` | Frames/s | Load |
  |-----|------|----------|------|
  | 3 | 10 ms | 4 000 | 52 % |
  | 3 | 11 ms | 3 636 | 47 % |
  | 3 | 15 ms | 2 667 | 35 % |
  | 4 | 10 ms | 3 000 | 39 % |
  | 4 | 11 ms | 2 727 | 35 % |
  | 4 | 15 ms | 2 000 | 26 % |

- [x] **B3** Adding a 50 Hz sync and 30 × 10 Hz status (350 frames/s)
  raises those loads to 57, 52, 39, 44, 40, and 31 %.
- [x] **B4** A 10 ms *message* period (12 000 frames/s) is 156 % and is
  reported as over budget. The limit used by the pattern checker is 60 %.
- [x] **B5** 120 motion frames occupy 15.6 ms of bus time.
- [ ] **B6** The scheduler starts a window 15–20 ms before the nominal `T`
  and finishes before `T+Δt`. A plan that starts at `T` fails the test.
- [x] **B7** **Property.** Load scales as `1/(N·Δt)` for `Δt` in
  {10, 11, 15} ms and `N` in {3, 4}. Frame count does not change when the
  step rate or the microstep setting changes (`FR-ARCH-002`).
- [ ] **B8** `Δt` and `N` applied while a segment is in flight are
  rejected. The same change at a pattern boundary is accepted (§4.2).

---

## 6. Playback polynomial

Whitepaper §4.2 and §4.7. The node evaluates coefficients. It does not fit
samples. The four-field frame replaced the retired sample packer in
`lib/motion_core` in this slice.

- [x] **C1** `mc_eval` of `p0, v0=0, a0=0, j=0` returns `p0` at every `τ`
  inside the segment, in micrometres.
- [x] **C2** Constant speed: `a0 = 0`, `j = 0`. At `τ` the result is
  `p0 + v0·τ`, using the Horner order in §4.7. One speed LSB over 30 ms
  moves 1.2 µm.
- [x] **C3** Constant acceleration and constant jerk match
  `p + vτ + aτ²/2 + jτ³/6` at `τ = 0`, at 2 ms, and at `Tseg`. The test
  computes that polynomial with the same divide-before-multiply order, in
  `int64_t`.
- [x] **C4** A second call at the same `τ` returns the same micrometres.
  A different step-counter value does not change the result.
- [x] **C5** One message is a complete segment. No previous message is
  consulted. Type 1 is rejected.
- [x] **C6** While playing, the next `p0` and `v0` must match the previous
  segment's end position and end speed within one LSB. A larger position
  gap holds and faults.
- [x] **C7** After a hold, a segment whose `p0` matches the held position
  within one position code is accepted.
- [x] **C8** A segment whose acceleration at either endpoint is below
  −9.81 m/s² is rejected. The stored position is unchanged.
- [x] **C9** `p(Tseg)` outside 0–2 000 000 µm is rejected.
- [x] **C10** **Property.** 100 random int16 coefficient sets, seed
  `0x12345678`. Each accepted segment matches the independent Horner
  implementation at 20 times. No `float` in either implementation.

---

## 7. Steps and chunks

Whitepaper §4.7, §5.3, §5.4, Appendix A.

- [x] **S1** The Q16.16 steps/mm constant represents
  `3200 / (π × 40 mm) = 25.4648 steps/mm`. Full-scale 2 000 mm converts
  to a step count within 1 step of `round(2 000 × 25.4648)`.
- [x] **S2** A fractional-step accumulator is kept per axis. Summing chunk
  deltas across 2 000 mm equals the net step count from S1. Rounding each
  chunk on its own is the failing control: that sum drifts by more than
  one step.
- [x] **S3** At 1.2 m/s a 2 ms chunk is 61 steps. At 0.6 m/s it is 31
  steps. Both are inside `[-128, +128]`.
- [x] **S4** A requested 5 ms chunk at 1.2 m/s (153 steps) is rejected.
  A 10 ms chunk (306 steps) is rejected. A 2 ms chunk is accepted.
- [x] **S5** A 2 ms chunk at 16 MHz is 32 000 ticks. Ticks lie in
  `[3 200, 65 535]`. A chunk shorter than 200 µs is rejected.
- [x] **S6** For `Δt` of 11 ms and 15 ms, chunk boundaries stay on the
  2 ms grid. The evaluator is queried at those boundaries, not at `Δt`.
- [x] **S7** Four axes in one timed chunk share one tick count. A reversal
  on axis 0 does not change the step delta of axes 1–3.
- [x] **S8** `mc_step_chunk` returns per-axis steps and the tick count for
  a 2 ms boundary. Steps match S3 for a constant-speed segment.
- [x] **S9** An underrun flag, once set by the simulated feeder, stays set
  until an explicit clear and is reported as a fault.
- [x] **S10** The chunk feeder's host double is a queue of `(steps, ticks)`
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

- [ ] **M1** The host table is `(flash_id, axis 0–3) → position 0–119`,
  plus `flash_id → board_id`. Lookup of 30 entries is exact. The board
  under test has no position value.
- [ ] **M2** A repeated advertisement of a known flash ID is assigned the
  same board ID. A second board claiming that board ID is not given it.
- [ ] **M3** A board with no board ID answers `ASK` with one `DISC`. A
  board that already has a board ID stays silent.
- [ ] **M4** The advertise delay is `1 ms × (flash_id byte 0 mod 10)`,
  measured from the `ASK`. The same flash ID always waits the same
  amount, and the wait is under 10 ms. The `DISC` identifier is field 0,
  type 8.
- [ ] **M5** `ASSIGN` is the single frame in P8. After a matching payload
  the board's stored board ID equals the CAN field, and the next `ASK`
  draws no `DISC` from that board.
- [ ] **M6** Clearing the board's RAM ID makes it unidentified. The next
  `ASK` produces a `DISC`, and the master assigns the board ID already
  stored for that flash ID. The host position map is unchanged.
- [ ] **M7** The master sends `ASK` every 5 s, including while a pattern
  is playing. Identified boards still do not answer. An `ASK` that
  receives no `DISC` ends the startup loop.

---

## 10. Pattern bake, limits, report, export

Whitepaper §6.4, §6.6, §6.9. Headless. No browser.

- [ ] **A1** `.anim.json` version 1 matches the whitepaper §6.9.
  A file missing `version`, `grid`, `dt_ms`, `n`, `duration_ms`, `tier`, or
  `layers` fails parse. The parser accepts grids `[10, 10]` and `[10, 12]`.
  A non-integer numeric field fails parse.
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
- [ ] **A8** `tests/fixtures/write_sine.py` writes a version-1 `.anim.json`.
  The test process may run that script. The checker accepts the JSON it
  wrote. The checker returns a parse error when its input path ends in
  `.py`.
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

- [ ] **W1** The same core sources compile with Emscripten (`-std=c++17`).
  A host driver calls the native binary and the WASM binary (via Node)
  with `patterns/golden_sine.anim.json` from the whitepaper §6.9.
- [ ] **W2** Sample bytes, chunk bytes, packed CAN frames, and the report
  JSON match bit for bit between the two builds.
- [ ] **W3** The golden file is checked in. A change to interpolation,
  units, or chunking that is intentional updates the golden in the same
  change; an unintentional change fails W2.

---

## 13. Suggested order

The list is already in dependency order. The first session is §0.2:
harness (§1) then units (§2), including the new directories and CMake.
After that:

1. Identifiers (§3).
2. Motion payloads P1, P2, P3, P10 (§4) and the bus budget B1–B5, B7 (§5).
3. Playback polynomial C1–C10 (§6) and steps S1–S10 (§7). **Done.**
4. Node playback N1–N8, N13 (§8) and the remaining payloads P4, P5, P8.
   Those byte tables are in the whitepaper §4.5 and the addressing note §3.
5. Commissioning (§9) and the pattern bake (§10).
6. Live file state (§11), then native/WASM parity (§12).

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
