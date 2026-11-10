# TMC2209 Session 3 — StallGuard window, M0, 300 mA

> **Status: SG_RESULT is stable and monotonic from 750 step/s upward.** The
> low-speed unstable region is below ~750 step/s. No high-speed collapse was
> seen up to 17 kstep/s. No DIAG in the whole free-run sweep. Homing speeds
> sit well inside the stable region.

## Setup

M0, full NEMA-17, unloaded, 300 mA, 1/16, interpolation on, StealthChop
(`GCONF=0x1C0`, read back; `stealth=1`), `SGTHRS=0`, `TCOOLTHRS=0xFFFFF`,
`TPWMTHRS=0`, acceleration 2000 step/s². Monitor 20 ms. No tach.

## Free-run SG sweep

`sg_sd` is the sample standard deviation of the 20 ms monitor stream. At low
speed the stream repeats each full-step update, so the count `n` overstates
independent samples; the numbers are still usable as a level comparison.
`diag` counts DIAG lines in that point's window (all zero, including spin-up
at 500).

| f_step | SG mean | SG sd | min | max | zeros | n | diag |
|-------:|--------:|------:|----:|----:|------:|--:|-----:|
| 500 | 67.8 | 24.6 | 0 | 100 | 8 | 121 | 0 |
| 750 | 109.0 | 15.1 | 56 | 132 | 0 | 130 | 0 |
| 1000 | 139.0 | 14.8 | 96 | 162 | 0 | 121 | 0 |
| 1250 | 164.0 | 15.8 | 124 | 192 | 0 | 121 | 0 |
| 1500 | 186.2 | 16.2 | 146 | 214 | 0 | 130 | 0 |
| 1750 | 206.5 | 20.4 | 160 | 236 | 0 | 121 | 0 |
| 2000 | 228.5 | 17.7 | 160 | 254 | 0 | 131 | 0 |
| 2500 | 261.7 | 16.4 | 212 | 292 | 0 | 120 | 0 |
| 3000 | 291.8 | 15.9 | 244 | 320 | 0 | 131 | 0 |
| 3500 | 327.2 | 13.0 | 288 | 354 | 0 | 121 | 0 |
| 4000 | 353.5 | 14.7 | 312 | 382 | 0 | 130 | 0 |
| 5000 | 348.9 | 16.7 | 324 | 390 | 0 | 131 | 0 |
| 6000 | 355.2 | 12.9 | 326 | 378 | 0 | 131 | 0 |
| 7000 | 366.3 | 13.3 | 342 | 452 | 0 | 141 | 0 |
| 8000 | 380.2 | 12.7 | 356 | 406 | 0 | 131 | 0 |
| 10000 | 393.8 | 12.6 | 366 | 422 | 0 | 160 | 0 |
| 12000 | 405.3 | 9.5 | 380 | 426 | 0 | 161 | 0 |
| 14000 | 414.6 | 17.3 | 372 | 452 | 0 | 161 | 0 |
| 16000 | 426.2 | 28.8 | 372 | 474 | 0 | 161 | 0 |
| 17000 | 427.0 | 28.1 | 382 | 474 | 0 | 131 | 0 |

## Homing band

Whitepaper §5.7 uses 40–50 mm/s. On this motor at 300 mA:

| drum | 40–50 mm/s | SG mean there |
|-----:|-----------:|--------------:|
| 40 mm | 1.0–1.25k | 139–164 |
| 20 mm | 2.0–2.5k | 228–262 |
| 10 mm | 4.0–5.0k | 353–349 |

All are inside the stable region, well above the 750 step/s floor.

## Gate table

| Gate | Question this session | Result | Envelope consequence | Not tested |
|------|-----------------------|--------|----------------------|------------|
| G4 | Homing window: current, speed, `SGTHRS`, repeatability | Window **found** (stable above 750); threshold and repeatability not yet chosen | `SGTHRS`/`TCOOLTHRS` and travel must be set in Session 4; do not use SG below 750 step/s | hard-stop homing, repeatability |
| G5 | `SG_RESULT` versus the predicted tension | not this session | — | — |
| G6 | Measured UART mask, and the period that fits | not this session | — | — |

## Caveats

- This is free run. A stall leaves `SG_RESULT` high (session 1), so the stable
  free-run signal is not by itself proof of stall detection.
- 500 and 800 mA were not run; 300 mA is the design point.
- The SG value rises with speed, so a fixed `SGTHRS` gives a different margin
  at 10 mm than at 40 mm. That must be set in Session 4.

## Files

- `serial.log` (free-run sweep)
- `../2026-11-10-M0-SC-300-16-07/note.md` — SC `f_stall`
- `../2026-11-10-M0-SP-300-16-01/note.md` — SP `f_stall`
