# TMC2209 Session — M0 NEMA-17 pancake, 40 mm spool: load direction and low-current homing

> **Status: bench run 2026-11-15, free hang, operator-confirmed.** Axis M0,
> NEMA-17 pancake, 40 mm spool, 1/16. Direction convention measured on this
> rig: from the bottom stop, **negative steps raise (wind)** and **positive
> steps lower into the bottom stop**. 300, 150 and 100 mA lifted continuously;
> 30 mA lifted slowly but lost sync, with DIAG STALL throughout the lift. The
> proposed homing method (minimum current, run into the endstop) therefore
> needs the lowest current that still lifts, not the absolute minimum.
> Top endstop estimated at 15–18 turns from bottom (48,000–57,600 µsteps,
> 1.89–2.26 m); it was not reached in this session.

## Identity header

| Field | Value |
|-------|-------|
| run_id | `2026-11-15-M0-SC-300-16-01` (session swept 300, 150, 100, 30 mA) |
| date, operator | 2026-11-15, bench owner |
| firmware | repo HEAD `8c13022`; the flashed binary was not re-verified this session |
| board | Fly-Micro4 as built; no solder, no added wire |
| axis, UART address | M0, address 0 (`scan`, `status 0`) |
| motor MPN | NEMA-17 pancake (US-17HS4023S class); nameplate not re-read |
| full steps/rev | 200 (assumed) |
| microsteps, interpolation | 16 (MRES = 4), interpolation on |
| spool | 40 mm diameter; π × 40 mm = **125.66 mm/turn**; 3200 µstep/turn; **25,465 µstep/m** |
| load, hang | suspended weight, **free** hang; mass not weighed this session (design ball 50 g) |
| supply, temperature, ambient | not measured |
| UART | 250 kBaud (4 µs/bit), idle 4, `SENDDELAY` 2 (bring-up default) |
| GCONF / CHOPCONF / PWMCONF | `0x1C0` (StealthChop) / `0x14020054` / `0xC10D0024` |
| SGTHRS, TCOOLTHRS, TPWMTHRS | 40 (threshold 80), `0xFFFFF`, 0 |
| COOLCONF | never written; SEMIN stays at the reset value 0. The firmware does not program CoolStep. |
| position convention | 0 = bottom / thread end. −steps lifts, +steps drives into the bottom stop (repeated accidental bottom-stop drives proved this). |

Current codes actually applied (all VSENSE = 1, CS_ACTUAL = IRUN at standstill):

| request | IRUN/IHOLD | note |
|--------:|-----------:|------|
| 300 mA | 8 / 8 | boot default |
| 150 mA | 3 / 3 | — |
| 100 mA | 2 / 2 | — |
| 30 mA | 0 / 0 | 1/32 of full scale ≈ 30 mA RMS |

`cur` returned `FAILED` on 1–2 attempts per change and succeeded on retry.
`stats` showed `rd_fail = 574 / 8137` reads (≈ 7 %) during the session. A point
is kept only after the successful retry, with `status` confirming `IRUN` and
`CS_ACTUAL`.

## Predicted load

Free-hang 50 g on a 40 mm spool (radius 20 mm): static thread force 0.491 N,
torque **9.81 mNm**. Up and down cruise differ only by bearing/guide friction;
the ramps add ±m·a·r (`doc/mechanical_analysis.md` §3.5). `SG_RESULT` is a
magnitude, not a torque in mNm, and there is no load cell: the numbers below
are the SG indicator, not measured torque.

## Commands

```text
quiet on
cur 0 <mA> <mA>          # retried until it did not print FAILED
mode 0 stealth           # StallGuard/DIAG only valid here
sg 0 40                  # threshold = 2*SGTHRS = 80
enable 0 1
diag clear
monitor 0 <seconds> 200
move 0 -32000 8000       # up 10 turns
move 0 +32000 8000       # down 10 turns to bottom, zero-velocity end
# 1.5 s dwell at bottom (let the pendulum settle)
move 0 -32000 8000       # up 10 turns
move 0 +32000 8000       # down 10 turns to bottom
```

At 30 mA the same four moves were run at **2000 µstep/s** because the motor
slipped at higher speed.

Motion profile: bench acceleration is 2000 µstep/s². At 8000 µstep/s a
32,000-step move is **triangular** (4 s accelerate + 4 s decelerate, 8000
µstep/s peak, no cruise). At 2000 µstep/s there is a 14 s cruise. Absolute
speed never exceeded the commanded 8000 µstep/s ≈ 0.31 m/s; the 1 m/s target
is not reachable with this bench acceleration over 10 turns.

## Results

Experiment phases only (excluding the up-10 setup on the 150/100 runs).
`STALL` is the firmware flag in the `mon` line; it also asserts on the
low-speed ramps where SG naturally falls below 80, so it is not by itself a
slip indicator. `DIAG STALL` is the count of ISR stall lines in the log.

| current | direction | SG mean (min–max) | monitor STALL | DIAG STALL lines |
|--------:|-----------|------------------:|--------------:|-----------------:|
| 300 mA | down | 182 (0–318) / 188 (14–286) | 20 / 21 % | 31 |
| 300 mA | up | **146** (2–248) | 22 % | |
| 150 mA | down | 187 (0–294) / 188 (0–292) | 13 / 8 % | 53 |
| 150 mA | up | **146** (0–254) | 23 % | |
| 100 mA | down | 164 (0–270) / 176 (0–276) | 15 / 11 % | 60 |
| 100 mA | up | **155** (0–258) | 18 % | |
| 30 mA @ 2000 | down | 200 (0–276) / 206 (2–268) | 8 / 4 % | **775** |
| 30 mA @ 2000 | up | **85 (0–152) / 65 (0–156)** | **60 / 73 %** | |

Key observations:

- **Up is the loaded direction.** At 100–300 mA the lift SG mean is 5–42 counts
  below the lower phases, in the expected direction (higher SG = less load).
  The operator saw continuous lift and called no slip.
- **100 mA is the lowest clean point tested.** SG still separates up from down
  (≈ +10–20 counts down), and no slip was called.
- **30 mA is below the reliable lift point.** The lift SG mean falls to 65–85
  while the down phases stay at ≈ 200, and 60–73 % of lift samples flag STALL.
  The 775 DIAG STALL lines are the same event. The operator saw the rotor slip
  at speed; at 2000 µstep/s it still slips, and the 8000 µstep/s and 4000
  µstep/s 1-turn tests showed 74–76 % STALL.
- **No driver fault.** `DRV_STATUS` stayed at the stst+stealth pattern
  (`0xC0080000`, `0xC0030000`, `0xC0020000`, `0xC0000000` by current);
  `otpw = ot = s2g = ola = olb = 0` throughout.

## Events and cause

| Event | Cause | Action |
|-------|-------|--------|
| Motor driven into the bottom stop (repeated) | Wrong sign assumed: `+` from position 0 lowers into the stop, it does not lift | Operator stopped/power-cycled; direction corrected to −up. No fault bits. |
| DIAG STALL throughout the 30 mA lift | Loss of sync under load at low current, not an endstop | Recorded; 30 mA rejected for lifting/homing |
| DIAG STALL on 100–300 mA lifts/downs | Normal SG dip at low speed on the acceleration ramps | Not a loss of sync; SG magnitude is the load record |
| `cur` prints `FAILED` on first attempts | ≈7 % UART read failures; `setCurrents` reads CHOPCONF first | Retried; kept only the verified point |

## Homing implication (minimum current, run into the top endstop)

The proposed method is sound only with the slip caveat:

1. **Current:** use the lowest current that lifts without slipping, not the
   absolute minimum. On this rig that is **100 mA** (30 mA slips). If a lower
   value is wanted, bisect 30–100 mA on the lift, not on the hold.
2. **Speed:** keep it low (≤ 2000 µstep/s ≈ 0.08 m/s) so the rotor does not
   lose sync before the stop and `TSTEP` stays inside the StallGuard window.
3. **Arm:** StealthChop and a threshold calibrated for the homing current and
   speed (`SGTHRS = 10` for 100 mA / 1000 µstep/s in `homing.cpp`). Set
   `TCOOLTHRS` to the `TSTEP` of the lowest speed to be armed, not `0xFFFFF`
   (which arms every speed; see the 2026-11-10 finding). The firmware keeps
   `0xFFFFF`, searches at one fixed speed, and ignores the spin-up ramp with a
   settle time plus the moving (`stst`) condition.
4. **Detect:** sustained `SG_RESULT ≤ 2·SGTHRS` with `stst = 0`, plus the
   DIAG edge, plus a **travel cap** just past the estimated endstop
   (48,000–57,600 µsteps) and a timeout. Never rely on SG alone: it is not a
   motion sensor, and at 30 mA it is already saturated in ordinary lift.
5. **Direction:** start at the bottom stop (position 0) and home **negative**
   (up). A positive move from 0 drives into the bottom stop.

## Homing validation (later 2026-11-15)

The implemented search (`rp2040/src/homing.cpp`, pure core
`lib/homing_core`) uses **300 mA**, StealthChop, **2000 µstep/s**, accel
2000, `SGTHRS = 15` (threshold 30) and `TCOOLTHRS = 400`. Four cycles accepted at **45,920 / 46,110 / 46,098 / 46,182 steps**
(14.35–14.43 turns), spread 262 steps (0.08 turn), with
`forceStopAndNewPosition(0)` and a one-turn (3,200-step) backoff. The accepted
diag to the pulse generator stopping was **3–4 ms / 6–8 steps**. The bench
procedure drives the axis to the bottom before disabling the driver. The park
distance is the accepted top travel minus the one-turn backoff, e.g.
46,182 − 3,200 = 42,982 steps; it inherits the top detection offset, not the
6–8-step stop latency. Two details from the run:

- At 100 mA the top signal is too weak; the 100 mA / 1000 and 100 mA / 4000
  searches reached the 18–19-turn cap with the hardware DIAG low.
- At 300 mA / 2000 the lift SG is 50–116 and the top press 2–24. The top is a
  **slipping** contact: the motor stops, but SG fluctuates and the hardware
  DIAG line did not assert reliably. A 20 ms single-`SG_RESULT` read combined
  with the DIAG pin catches it; a 5-turn minimum travel rejects lift glitches.

Repeatability over 20 homes is still unmeasured. `getCurrentPosition()` must
not be called in the search loop: on the Pico driver it drains the PIO RX FIFO
and stalls the loop; the search uses `homing_core::travelAt`.

## Gate table

| Gate | Question this session | Result | Envelope consequence | Not tested |
|------|-----------------------|--------|----------------------|------------|
| G1 | True stall or DIAG with the shaft turning? | not this session | — | tach |
| G2 | Does SpreadCycle or a low hybrid move `f_stall`? | not this session | — | — |
| G3 | Nominal tier inside `0.7 × f_stall` and `0.5 × a_stall`? | not this session; 8000 µstep/s peak ≈ 0.31 m/s, accel 2000 µstep/s² | — | acceleration |
| G4 | Homing window: current, speed, `SGTHRS`, repeatability | **partial**: 100 mA lifts, 30 mA slips at all tested speeds; endstop not reached | homing current ≥ 100 mA, low speed | top-stop repeatability, `TCOOLTHRS` |
| G5 | `SG_RESULT` versus the predicted tension | **partial**: stable up/down separation at 100–300 mA, free hang; not calibrated to mNm | no hardware action | friction line, repeatability |
| G6 | Measured UART mask, and the period that fits | not this session | — | — |

## Files

- `samples.csv` — `monitor` rows for the four current runs, same schema as
  `doc/tmc2209_characterization.md` §12.4; `tach_rpm` and `drv_status` are
  blank because the monitor does not print them. `event = diag` marks a row
  that carries the firmware STALL flag; the separate DIAG ISR lines are
  counted in the note.
- `serial.log` — the six raw sessions (300/150/100/30 at 2000, plus the 30 mA
  8000 lift and the 4000 1-turn repeat), unedited, on the bench host. It is
  gitignored per this directory's README.
- No tach, supply meter or temperature measurement this session.
