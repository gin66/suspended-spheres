# TMC2209 speed staircase 10k–30k — INVALID (rotor stalled)

> **Status: invalid for `f_stall`.** The operator observed the rotor stall.
> The staircase below only proves the *step source* reached 30 kstep/s; it does
> not prove the rotor followed. `SG_RESULT` read high (330→423) with no fault
> bits, which the run initially took as "turning"; that inference was wrong.
> No tach or shaft-mark camera was in use.

## Setup

M0, full NEMA-17, unloaded, 300 mA, 1/16, interpolation on, StealthChop,
`SGTHRS=0`, `TCOOLTHRS=0xFFFFF`, `TPWMTHRS=0`, bench acceleration 2000
step/s². Same identity as `../2026-11-10-identity-01/note.md`. No solder, no
added wire (pending confirmation). Supply and temperature not measured.

## Commanded points (2 s dwell each, continuous climb)

| f_step | SG mean | DIAG | Note |
|-------:|--------:|-----:|------|
| 10000 | 330 | 1 (sg=0) | start |
| 12000 | 407 | 0 | |
| 14000 | 416 | 0 | |
| 16000 | 428 | 0 | |
| 18000 | 430 | 0 | |
| **19000** | **319** | **1 (sg=342)** | **only anomaly; likely loss/resonance** |
| 20000 | 339 | 0 | |
| 21000 | 359 | 0 | |
| 22000 | 369 | 0 | |
| 23000 | 380 | 0 | |
| 24000 | 392 | 0 | |
| 25000 | 399 | 0 | |
| 26000 | 407 | 0 | |
| 27000 | 412 | 0 | |
| 28000 | 420 | 0 | |
| 29000 | 420 | 0 | |
| 30000 | 423 | 0 | |

`CS_ACTUAL=8/8`, `stealth=1`, `otpw/ot/s2g/ola/olb=0` at every point. `stop 0`
then produced a DIAG flood (same `SGTHRS=0` artefact as session 1).

## What the data does say

- SG means **rise** monotonically with step rate (330 → 423) and never
  collapse. If the rotor is stalled, `SG_RESULT` is not a motion detector at
  all. This alone disqualifies SG as the sole stall reference.
- The 18→20 kstep/s region is the only place with a DIAG and an SG dip
  (19 kstep/s). That is consistent with the original "~20 kstep/s" observation,
  but the exact `f_stall` is unknown.
- `TSTEP` matched the command at every point (e.g. 42, 25); it proves the step
  train, not the shaft.

## Next

Repeat the 10–20 kstep/s region with an independent motion reference (shaft
mark + phone slow-motion, or tach) and 2 s dwells, stopping at the first
confirmed loss. Discard this file and `../2026-11-10-M0-SC-300-16-03/` as
`f_stall` evidence.

## Files

- `serial.log` (unedited)
- `note.md` (this file)
