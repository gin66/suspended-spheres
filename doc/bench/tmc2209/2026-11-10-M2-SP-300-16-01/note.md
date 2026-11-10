# TMC2209 Session 2 — `f_stall`, SpreadCycle, M2 pancake, 300 mA

> **Status: `f_stall`(M2, SpreadCycle) ≥ 94 kstep/s, not found.** The axis
> stayed in sync to 94k (1763 RPM at 1/16). The climb was stopped because the
> mechanics sound "horrible" at that speed (operator), not because of a stall.
> No fault bits. Operator-confirmed in sync at every point.

## Setup

M2, Usongshine US-17HS4023S pancake (200 steps/rev, 1.0 A rating, 3.5 Ω,
3.2 mH), unloaded, 300 mA (0.3 of rating), 1/16, interpolation on,
SpreadCycle (`GCONF=0x1C4`, read back each run), acceleration 2000 step/s².
Supply not measured this run; on M0 at 74k it was 210 mA total at 20 V.

## Results (2 s dwell per point, operator bits)

| f_step range | RPM at 1/16 | Result |
|-------------:|------------:|--------|
| 16k–30k (2k) | 300–563 | all in sync |
| 32k–46k (2k) | 600–863 | all in sync |
| 48k–62k (2k) | 900–1163 | all in sync |
| 64k–78k (2k) | 1200–1463 | all in sync |
| 80k–94k (2k) | 1500–1763 | all in sync, **mechanically loud** |

No fault bits were seen; `GCONF=0x1C4` each run.

## Interpretation

- M2 (0.13 Nm, 1.0 A) reaches a **higher** step rate than M0 (0.42 Nm, 1.5 A),
  which failed at 76 kstep/s (`../2026-11-10-M0-SP-300-16-01/note.md`). So the
  high-speed loss is not rotor torque. It is per-motor (resonance) or
  electrical/driver, and M0 is the weaker axis.
- The limiting factor at 94k is **mechanical noise**, not loss of sync. That is
  well above every step rate the mechanism needs (10 mm spool: 60.1k for
  0.59 m/s).
- `0.7 × 94k = 65.8 kstep/s`. At 20 mm that is 1.29 m/s, so the optional
  tier (1.2 m/s = 61.1k) is inside the margin and inside the 64k playback
  chunk. At 10 mm, the nominal tier (60.1k) is inside, but optional 1.2 m/s
  (122k) is outside both.

## Gate table

| Gate | Question this session | Result | Envelope consequence | Not tested |
|------|-----------------------|--------|----------------------|------------|
| G2 | Does SpreadCycle or a low hybrid move `f_stall`? | M2: SC 21k → SP ≥94k. M0: SC 18k → SP ≈74k. Hybrid not measured | Choose SpreadCycle; M0 is the speed-limiting axis at 300 mA | hybrid, noise number |
| G3 | Nominal tier inside `0.7 × f_stall` and `0.5 × a_stall`? | M2 speed margin fine (see above); M0 at 74k is tighter. `a_stall` not measured | 20 mm optional feasible on M2; 10 mm optional not streamable | acceleration, thermal |

## Noise note

One method, qualitative only: operator reports the mechanics "sound horrible"
by 94k (1763 RPM). No meter or dB. This is a mechanical/bearing/coupling
concern at high RPM, not a driver fault. Stop the climb when it is heard.

## Files

- `serial.log` (all M2 SP staircases)
- `../2026-11-10-M0-SP-300-16-01/note.md` — M0 SP limit
- `../2026-11-10-M0-SC-300-16-07/note.md` — M0 SC limit
