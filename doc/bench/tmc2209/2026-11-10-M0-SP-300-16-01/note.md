# TMC2209 Session 2 — `f_stall`, SpreadCycle, M0, 300 mA

> **Status: `f_stall`(SpreadCycle) ≈ 74 kstep/s.** 74.0k held 2 s in the
> staircase and again for a 12 s dwell; 76.0k failed. Exact value not refined
> (the 74/76 gap is ~3 % and is not decision-relevant). Operator-confirmed in
> sync at every point up to 74k. Supply not the limiter: **210 mA total
> including the Fly board at 74k, 20 V stable**.

## Setup

M0, full NEMA-17, unloaded, 300 mA, 1/16, interpolation on, SpreadCycle
(`GCONF=0x1C4`, read back each run), `TPWMTHRS=0`, acceleration 2000 step/s².
StealthChop comparison is in `../2026-11-10-M0-SC-300-16-07/note.md`.
Bench supply current measured by the operator; temperature not measured.

## Method

Operator in the loop: each point is a 2 s dwell after the speed change; the
operator reports one bit per point (in sync / stalled). No tach. SG is not
available in SpreadCycle, so DIAG is fault-only and was not used for the
stall call.

## Results

| f_step [step/s] | RPM at 1/16 | Result |
|----------------:|------------:|--------|
| 18000–28000 (2k steps) | 338–525 | all in sync |
| 30000–40000 (2k steps) | 563–750 | all in sync |
| 42000–56000 (2k steps) | 788–1050 | all in sync |
| 58000–72000 (2k steps) | 1088–1350 | all in sync |
| **74000** | **1388** | **in sync, also 12 s dwell** |
| 76000 | 1425 | **stalled** |

At 74.0k the status read `tstep=10`, `cs=8/8`, no fault bits, and
`pwm=255/0` with the firmware's `CURRENT_LIMITED` flag set (PWM_SCALE at 250+;
note PWM_SCALE is not the StealthChop regulator in SpreadCycle). The 12 s
dwell at 74k held, so this is not a transient.

Selected supply reading at 74k: 210 mA (sum of all four drivers + board) at
20 V. There is no supply-current story behind the 76k loss.

## Consequence

SpreadCycle raises the limit by ~4.1× over StealthChop (18k → 74k). Motion
should run in SpreadCycle; StealthChop is only for StallGuard homing.

Speed against the margin rule `f_op ≤ 0.7 × f_stall` = **51.8 kstep/s**:

| drum | steps/m | nominal tier | optional 1.2 m/s | inside 0.7×? |
|-----:|--------:|-------------:|-----------------:|--------------|
| 40 mm | 25,465 | 11.2–15.0k | 30.6k | yes, all |
| 20 mm | 50,930 | 22.4–30.0k | 61.1k | nominal yes; optional no |
| 10 mm | 101,859 | 44.8–60.1k | 122.2k | nominal top no; optional no |

Streaming note: the 2 ms playback chunk (64 kstep/s) still covers 20 mm at
1.2 m/s (61.1k) but not 10 mm at 1.2 m/s (122k), independent of the motor.

## Gate table

| Gate | Question this session | Result | Envelope consequence | Not tested |
|------|-----------------------|--------|----------------------|------------|
| G1 | True stall or DIAG with the shaft turning? | closed by the SC session and operator observation | — | — |
| G2 | Does SpreadCycle or a low hybrid move `f_stall`? | **Yes: SC 18.0k → SP ≈74k.** Hybrid not measured. Noise note: SP is louder (not measured) | Choose SpreadCycle for motion; StealthChop only for SG homing. Record the noise with the choice | hybrid, noise level |
| G3 | Nominal tier inside `0.7 × f_stall` and `0.5 × a_stall`? | Speed: 0.7×74k = 51.8k. 40 mm all tiers inside; 20 mm nominal inside, optional outside; 10 mm nominal top outside. `a_stall` not measured | Speed envelope passes for 40 mm; 20/10 mm optional tiers are outside the margin. Acceleration still unmeasured (bench 0.079 m/s²) | acceleration, thermal |
| G4 | Homing window: current, speed, `SGTHRS`, repeatability | not this session | — | — |
| G5 | `SG_RESULT` versus the predicted tension | not this session | — | — |
| G6 | Measured UART mask, and the period that fits | not this session | — | — |

## Files

- `serial.log` (staircases and the 74k single point)
- `../2026-11-10-M0-SP-300-16-02/serial.log` — 74k repeat with 12 s dwell
- `../2026-11-10-M0-SC-300-16-07/note.md` — StealthChop `f_stall`
