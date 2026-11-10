# TMC2209 Session 2 — `f_stall`, StealthChop, M0, 300 mA (preliminary)

> **Status: `f_stall` = 18.0 kstep/s**, highest rate clean in 3/3 repetitions.
> First repeatable loss at 18.25–18.5 kstep/s. No tach; the stall reference is
> the operator's observation (shaft stops and the motor makes the stall noise)
> plus the matching DIAG burst. `SG_RESULT` does **not** show the stalled state;
> see the correction in `../2026-11-10-M0-SC-SP-300-16-01/note.md`.
> SpreadCycle and M2 were not measured (no motion reference once SG is gone).

## Setup

M0, full NEMA-17, unloaded, 300 mA, 1/16, interpolation on, StealthChop,
`SGTHRS=0`, `TCOOLTHRS=0xFFFFF`, `TPWMTHRS=0`, bench acceleration 2000 step/s².
Bench motor supply and temperature not measured. Runs in this session:
`../2026-11-10-M0-SC-300-16-04` (coarse 17.0–19.5k), `-05` (18.3–18.45k),
`-06` (18.25/18.275k), `-07` (18.0k, 3 repetitions).

## How a loss appears

At the loss, the driver emits a **burst of DIAG lines** (RG 0–500) and the shaft
stops. With `SGTHRS=0` the comparator threshold is 0, so the burst is
`SG_RESULT` touching 0. After the burst, `SG_RESULT` returns to ~400 even
though the rotor is stalled, so SG cannot be used to check the state. A
`stop 0` after a stall produces a much larger DIAG flood at 11–15 kstep/s
(the re-capture); that flood is **not** the stall. A single DIAG at 0–1 kstep/s
at spin-up is also an artefact (`SG_RESULT` not valid yet) and is excluded.

## Results

| f_step | Observation | Verdict |
|-------:|-------------|---------|
| 17000 | clean, SG ≈ 429 | good |
| 17250 | clean, SG ≈ 427 | good |
| 17500 | clean, SG ≈ 432 | good |
| 17750 | clean, SG ≈ 427 | good |
| **18000** | **clean 3/3 (SG 431/434/434, TSTEP 42)** | **`f_stall`** |
| 18250 | one clean run, one burst at arrival | not reliable |
| 18300 | burst at arrival (10 DIAG) | bad |
| 18500 | burst at arrival (3 DIAG) | bad |

At 40 mm / 1/16 (`25,465 steps/m`):

| Quantity | Value |
|----------|-------|
| `f_stall` | 18,000 step/s = **0.707 m/s** |
| `0.7 × f_stall` | 12,600 step/s = **0.495 m/s** |
| nominal tier (`ASM-015`) | 0.44–0.59 m/s = 11.2–15.0 kstep/s |
| optional tier | 1.2 m/s = 30.6 kstep/s |

The nominal tier's top (0.59 m/s = 15.0 kstep/s) is **above** `0.7 × f_stall`
(0.495 m/s). Under the plan's margin rule the commanded speed must be lowered
to ≤ 0.495 m/s, or a different mode/current must be found. The optional tier
fails by a wide margin. This is unloaded, StealthChop, 300 mA; no thermal
record, and 18.25k passed once, so treat it as preliminary.

## Gate table

| Gate | Question this session | Result | Envelope consequence | Not tested |
|------|-----------------------|--------|----------------------|------------|
| G1 | True stall or DIAG with the shaft turning? | **True stall**, operator-confirmed, no fault bits; DIAG burst at the loss. `SG_RESULT` does not indicate the stalled state | Do not use SG as a motion reference; stop at the first DIAG burst at speed | tach |
| G2 | Does SpreadCycle or a low hybrid move `f_stall`? | **Yes: SC 18.0k → SP ≈74k** (`../2026-11-10-M0-SP-300-16-01/note.md`). Hybrid not measured | Choose SpreadCycle for motion | hybrid, noise |
| G3 | Nominal tier inside `0.7 × f_stall` and `0.5 × a_stall`? | **Partial close**: `f_stall` = 18.0 kstep/s, `0.7 ×` = 0.495 m/s; nominal top 0.59 m/s is outside. `a_stall` not measured | Lower the commanded envelope to ≤ 0.495 m/s, or change mode/current | acceleration; bench command is 0.079 m/s² |
| G4 | Homing window: current, speed, `SGTHRS`, repeatability | not this session | — | — |
| G5 | `SG_RESULT` versus the predicted tension | not this session | — | — |
| G6 | Measured UART mask, and the period that fits | not this session | — | — |

## Files

- `serial.log`, `samples.csv` (this directory, 18.0k runs)
- `../2026-11-10-M0-SC-300-16-04/`, `-05/`, `-06/` — the coarse and refine runs
- `../2026-11-10-M0-SC-300-16-02/`, `-03/` — invalid continuous staircases
- Supply, temperature, tach: not measured
