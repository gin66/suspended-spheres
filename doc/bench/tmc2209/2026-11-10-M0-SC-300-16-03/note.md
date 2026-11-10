# TMC2209 speed staircase 30k–50k — INVALID (rotor already stalled)

> **Status: invalid.** Run from rest to 30 kstep/s, then 32k–50k. The operator
> observed the rotor stall. The first point ramps from 0 through the 18–20k
> region where the previous run lost it, so every point here is consistent with
> a stalled rotor that the step source keeps commanding. SG is not a motion
> reference.

## Setup

Same as `../2026-11-10-M0-SC-300-16-02/note.md`: M0, unloaded, 300 mA, 1/16,
StealthChop, `SGTHRS=0`, `TCOOLTHRS=0xFFFFF`, acceleration 2000 step/s².

## Commanded points

| f_step | SG mean | DIAG |
|-------:|--------:|-----:|
| 30000 | 370 (mean over the whole 0→30k ramp) | 1 (sg=404) |
| 32000 | 437 | 0 |
| 34000 | 439 | 0 |
| 36000 | 442 | 0 |
| 38000 | 447 | 0 |
| 40000 | 454 | 0 |
| 42000 | 462 | 0 |
| 44000 | 461 | 0 |
| 46000 | 469 | 0 |
| 48000 | 470 | 0 |
| 50000 | 472 | 0 |

`CS_ACTUAL=8/8`, `stealth=1`, no fault bits. `TSTEP` matched the command
(25 at 30k down to 15 at 50k). SG rises monotonically and never collapses.

## Conclusion

The step source reaches 50 kstep/s and the driver reports no fault, but the
rotor was stalled, so this is **not** evidence of `f_stall` or of a 50 kstep/s
capability. Stop-source timing was not measured; the `stop` from 50k added
another `SGTHRS=0` DIAG flood.

## Files

- `serial.log` (unedited)
- `note.md` (this file)
