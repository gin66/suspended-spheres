# TMC2209 Session 2 — `f_stall`, StealthChop, M2 pancake, 300 mA

> **Status: `f_stall`(M2, StealthChop) ≈ 21 kstep/s.** 16–21k in sync,
> 22k lost (operator bits: `1 1 1 1 1 1 0 0`). `GCONF=0x1C0` read back and
> `stealth=1` confirmed before the run. 21k at 1/16 = 394 RPM.

## Setup

M2, Usongshine US-17HS4023S pancake, unloaded, 300 mA, 1/16, interpolation
on, StealthChop (`GCONF=0x1C0`), `SGTHRS=0`, `TCOOLTHRS=0xFFFFF`,
`TPWMTHRS=0`, acceleration 2000 step/s². Supply and temperature not measured.

> Note: directory `../2026-11-10-M2-SC-300-16-01/` was produced with a script
> bug that wrote `GCONF=0x1C4`, so it is a **SpreadCycle** run (16–23k all in
> sync) and is not StealthChop evidence.

## Results

| f_step | Result |
|-------:|--------|
| 16000 | in sync |
| 17000 | in sync |
| 18000 | in sync |
| 19000 | in sync |
| 20000 | in sync |
| **21000** | **in sync — `f_stall`** |
| 22000 | lost |
| 23000 | lost |

At 40 mm, 21k = 0.825 m/s; `0.7 × 21k = 14.7 kstep/s = 0.577 m/s`. Homing at
40–50 mm/s is 1.0–1.25k step/s at 40 mm, 2.0–2.5k at 20 mm, 4.0–5.0k at
10 mm — all well below `f_stall`.

## Gate table

| Gate | Question this session | Result | Envelope consequence | Not tested |
|------|-----------------------|--------|----------------------|------------|
| G2 | Does SpreadCycle or a low hybrid move `f_stall`? | M2: SC 21k → SP ≥94k. M0: SC 18k → SP ≈74k. Hybrid not measured | SpreadCycle for motion; StealthChop for SG homing | hybrid, noise number |
| G3 | Nominal tier inside `0.7 × f_stall` and `0.5 × a_stall`? | M2 SC: 0.7× = 0.577 m/s at 40 mm. SP margin is the relevant one | Homing speed sits well inside the SC limit | acceleration, thermal |

## Files

- `serial.log` (true StealthChop run)
- `../2026-11-10-M2-SC-300-16-01/` — mislabeled, actually SpreadCycle
- `../2026-11-10-M2-SP-300-16-01/note.md` — M2 SpreadCycle
- `../2026-11-10-M0-SC-300-16-07/note.md`, `../2026-11-10-M0-SP-300-16-01/note.md`
