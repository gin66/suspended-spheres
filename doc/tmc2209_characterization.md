# TMC2209 + Stepper Characterization — Bench Test Plan

> Status: proposed, not yet implemented. This is a bench-only procedure.
> The commands described here can command motion and must never run with the
> mechanism in service. The existing `bench_console` is already such a tool.
> Hardware is the Fly-Micro4 as built; clip-on instruments only. The bench
> output is the saved session record in §12, not a serial scroll.

---

## 1. Purpose

Characterise the TMC2209 on the Fly-Micro4 as built, so that one unclassified
bench observation becomes an operating point and a homing reference on the
board that is already selected.

The observation (2026-11-10): on an unloaded shaft, both a full-size NEMA-17
(M0) and a NEMA-17 pancake (M2) run at 18 kstep/s and show a stall/DIAG event
near 20 kstep/s. The same step rate on two very different rotors points at the
driver configuration, the step source, or the supply — not at ball torque.
There is no ball on the shaft.

The plan splits into three decisions this hardware can actually make:

1. **Classify the 20 kstep/s event** with a tach and `DRV_STATUS`, then measure
   the true speed and acceleration limits in StealthChop and SpreadCycle at
   the 300 mA design current. The output is the motion envelope the firmware
   is allowed to command, with a stated margin.
2. **Make StallGuard4 reliable for the one position reference.** Homing is
   already assigned to the upper stop through the existing DIAG pins
   (`FR-MOT-009`, `FR-MOT-010`, `doc/motion_streaming_whitepaper.md` §5.7). The
   output is a homing current, a homing speed inside the valid StallGuard
   window, an `SGTHRS`, a `TCOOLTHRS`, and a repeatability figure.
3. **Bound what `SG_RESULT` can see during ordinary motion**, including
   whether a 50 g hanging load changes it and whether lift and lower look
   different. This is an experiment. No requirement asks the driver to tell
   lift from lower; the controller already commands the direction.

The former UART-sampling question and the operating-margin question are parts
of decisions 1 and 2, not separate product features.

Hardware is frozen: the Fly-Micro4 as built, unmodified. No soldering, board
rework, added driver, added sensor, or reuse of a production pin. Clip-on
instruments — scope probes, an optical tach, a camera — are allowed. A bench
session is only useful when it leaves a saved record that can close a named
gate; a serial scroll that is not saved does not.

The test plan is written for invocation from the USB-serial bench console. The
serial command set in §10 is a proposal; only the commands in §11 exist today.

---

## 2. Bench configuration

Record the exact configuration for every run. The current bench (2026-11-10):

| Axis | Motor | Load | Observed |
|------|-------|------|----------|
| M0 (Drive0) | NEMA-17, full size | none | 18 kstep/s OK, ~20 kstep/s stall/DIAG |
| M1 (Drive1) | NEMA-17, full size | none | same class as M0 |
| M2 (Drive2) | NEMA-17 pancake | none | 18 kstep/s OK, ~20 kstep/s stall/DIAG |
| M3 (Drive3) | no motor | — | — |

The ~20 kstep/s rows are unclassified: "stall/DIAG" is still two events until
a tach and `DRV_STATUS` separate them. The firmware does not stop on DIAG, so
a DIAG pulse alone does not prove the shaft stopped.

Both a full NEMA-17 and a pancake reaching the same step-rate limit is a strong
hint that the limit is **not motor torque alone**. The first priority is to
separate a true shaft stall from a DIAG event while the shaft keeps turning.

Firmware defaults written at bring-up (`doc/motion_streaming_whitepaper.md`
§5.6.1):

| Item | Value |
|------|-------|
| Current | 300 mA RMS run and hold |
| Microsteps | 1/16 (`MRES = 4`), interpolation on |
| Chopper | StealthChop forced (`GCONF.en_spreadCycle = 0`, `TPWMTHRS = 0`) |
| StallGuard | `SGTHRS = 40`, `TCOOLTHRS = 0xFFFFF` (enabled at all speeds) |
| CHOPCONF | TOFF = 4, HSTRT = 5, HEND = 0, TBL = 0, INTPOL = 1 |
| PWMCONF | `0xC10D0024` |
| DIAG | Active high, rising-edge latched in the GPIO ISR |
| Bench acceleration | 2000 step/s² (`kBenchAccel` in `bench_console.cpp`) |

Fill in the motor data before testing; the torque and StallGuard calculations
depend on it. An empty cell stops the session; see the identity header in
§12.2:

| Parameter | M0 / M1 full NEMA-17 | M2 pancake |
|-----------|----------------------|------------|
| MPN | none. Candidate sheet only, not read from this motor | Usongshine US-17HS4023S |
| Full steps/rev | 200 assumed (1.8°). Not read from a label | 200 |
| Rated current per phase | 1.5 A, candidate | 1.0 A |
| Phase resistance | 1.6 Ω ± 10%, candidate | 3.5 Ω |
| Phase inductance | 3.8 mH ± 20%, candidate | 3.2 mH ± 20% |
| Holding torque | 0.42 Nm (42 N·cm), candidate | 0.13 Nm (13 N·cm) |
| Detent torque | 0.018 Nm, candidate | not on the sheet |
| Rotor inertia | 5.7×10⁻⁶ kg·m² (57 g·cm²), candidate | not on the sheet |
| Rated terminal voltage | 2.4 V (1.5 A × 1.6 Ω). The sheet's 24 V is the driver supply | 3.5 V (1.0 A × 3.5 Ω) |

M2's row is the Usongshine product listing for US-17HS4023S (1 A, 3.5 Ω,
3.2 mH, 13 N·cm, 1.8°, body 42 × 42 × 23 mm). It is not a measurement of this
unit. Other sellers print 0.7 A, 4 Ω and 9 or 14 N·cm against the same model
number; those figures are not used. The listing's "4.1 V" title line does not
match 1.0 A × 3.5 Ω. Detent and rotor inertia are absent. The detent
measurement in `doc/mechanical_analysis.md` §3.1 is not tied to this MPN.
`doc/motor_selection.md` quotes a 0.16 Nm pancake as a catalog class; that is
not this motor.

M0 and M1 have no label. The row above is a candidate datasheet for a 42 N·cm
motor, not a nameplate. 42 N·cm and 420 mN·m are the same holding torque.
1.5 A × 1.6 Ω is 2.4 V, so the sheet's 24 V is the supply it expects the
chopper to run from, not the voltage across a winding. The bench supply at
20 V is that kind of supply. A winding measurement near 1.6 Ω would confirm
the sheet. If the sheet is the right motor, 300 mA is 0.2 of rated current and
the holding torque scales to about 0.084 Nm. M0 and M1 are the larger motors:
0.42 Nm and 1.5 A against the pancake's 0.13 Nm and 1.0 A, and a lower phase
resistance, so the same current heats them less.

The 1 A figure of the bench supply is a limit of that supply. It is not a
motor rating and not an installation budget. The supply now in use is set to
20 V and 1 A. Four axes at 300 mA RMS do not draw 1.2 A from that rail in any
simple way; measure the rail current. 300 mA remains the required design
current on every motor. 500 mA and 800 mA are allowed on M0, M1, and M2. Both
are under the pancake's 1.0 A rating and under the full-size candidate's
1.5 A. They are optional, one axis at a time, and a run stops if `DRV_STATUS`
shows `otpw` or `ot`. The whitepaper's 800 mA is a firmware bench cap, not an
operating recommendation. At 300 mA, M2 is at 0.3 of its rated current, so its
holding torque scales to about 0.04 Nm, above the 0.02 Nm the mechanism asks
for.

Record the drum diameter and thread when the load is added. The worked case in
`doc/mechanical_analysis.md` §4 is a 40 mm drum at 1/16: 3200 steps/rev,
25,465 steps/m, 0.785 m/s at 20 kstep/s. At 18 kstep/s the speed is 0.707 m/s.
The nominal fast tier is 0.44–0.59 m/s (`ASM-015`); the optional tier reaches
1.2 m/s = 30.6 kstep/s.

The 20 kstep/s figure is a failure speed, not a capability. `f_stall` is the
highest rate that stayed synchronous; the 18 kstep/s good point is already
above the nominal tier, and the 20 kstep/s event is below the optional tier.
Margin is computed from `f_stall` (§5), never from the failure speed. The PIO
ceiling (200 kstep/s) and the 2 ms playback chunk (64 kstep/s) are both above
the event, so the observation is not the known step-generator ceiling.

Useful conversions for 200 full steps/rev:

```
RPM        = f_step * 60 / (200 * microsteps)
TSTEP      = 12e6 * microsteps / (256 * f_step)   [clock cycles]
v_linear   = f_step * pi * drum_diameter / (200 * microsteps)
```

At 1/16, `TSTEP = 750000 / f_step`. Examples:

| f_step [step/s] | RPM | TSTEP | 40 mm drum [m/s] |
|-----------------|-----|-------|------------------|
| 1,000 | 18.8 | 750 | 0.039 |
| 7,600 | 142.5 | 99 | 0.298 |
| 15,300 | 287 | 49 | 0.601 |
| 18,000 | 337.5 | 42 | 0.707 |
| 20,000 | 375 | 37.5 | 0.785 |
| 30,600 | 574 | 24.5 | 1.202 |

The TMC2209 measures `TSTEP` between 1/256 microsteps, independent of the
actual `MRES`; the formula above expresses that period in terms of the STEP
input rate at `N` microsteps. Verify one value with `status <axis>` before
trusting the table for threshold settings.

---

## 3. What the datasheet and field experience say

### 3.1 StealthChop and SpreadCycle are different regulators

StealthChop is a voltage-mode PWM chopper. The driver guesses how long to
apply voltage to reach the target current and refines the guess from
measurements. It is very quiet at low and medium speed, but the guess becomes
worse when the motor is accelerating, when the load changes, and when the
back-EMF rises with speed. SpreadCycle is a current-mode chopper with fast
cycle-by-cycle regulation. It is louder but gives more torque, better
positional accuracy and more stable high-speed operation.

Sources:

- The TMC2209 datasheet calls SpreadCycle "an option to StealthChop2" for
  "highest velocity applications" and recommends combining them so that
  StealthChop is used only at low velocity (§1.4, §6.5).
- Klipper defaults TMC drivers to SpreadCycle. Its documentation states that
  SpreadCycle provides greater torque and positional accuracy, while
  StealthChop can be quieter; it also warns that changing mode at a non-zero
  velocity often gives "poor and confusing results".
- The Klipper "SpreadCycle and StealthChop" advanced guide explains that
  StealthChop is a guessed voltage pulse, not a current chopper, and that
  SpreadCycle is generally preferred for high speed and positional accuracy.
- Field reports (including the user's DLC32 experience) commonly trace
  high-speed stalling to StealthChop and resolve it by switching to
  SpreadCycle or lowering the hybrid threshold.

### 3.2 StallGuard4 only works in StealthChop, and only in a window

The datasheet is explicit:

- `SG_RESULT` is "intended for StealthChop mode, only" and is updated once per
  full step (§5.3, §11.3).
- `DIAG` is pulsed by StallGuard when `SG_RESULT <= 2 * SGTHRS`. It is only
  enabled in StealthChop and when
  `TCOOLTHRS >= TSTEP > TPWMTHRS` (§5.3).
- StallGuard4 works best at medium velocity. Very low speed has too little
  back-EMF; very high speed, where the back-EMF approaches the supply and the
  full sinusoidal current is not driven into the coils, collapses the
  response (§11.5).
- The datasheet notes that a stall always means the loss of four full steps,
  so the SG path cannot react faster than about four full-step periods. At
  20 kstep/s and 1/16 that is 3.2 ms; at 15.3 kstep/s it is 4.2 ms; at
  7.6 kstep/s it is 8.4 ms. Faster `SG_RESULT` sampling cannot beat this
  physical limit.

Therefore a DIAG event at 20 kstep/s is not automatically a real stall. With
`SGTHRS = 40` and `TCOOLTHRS = 0xFFFFF`, the firmware enables the StallGuard
comparison at every speed. A high-speed collapse of `SG_RESULT`, a resonance,
or an electrical fault can all produce a DIAG pulse. The first test is to
determine which one happened. The firmware comment that `TCOOLTHRS = 0xFFFFF`
"keeps a low-speed false stall from showing" is misleading: the comparison
`TSTEP <= 0xFFFFF` is true at every speed the chip can report. Copy the register
value into the run note, not the comment.

### 3.3 Changing chopper mode

- `GCONF.en_spreadCycle = 1` forces SpreadCycle.
- `GCONF.en_spreadCycle = 0` enables StealthChop below the `TPWMTHRS`
  crossover and SpreadCycle above it; `TPWMTHRS = 0` keeps StealthChop at all
  step rates.
- Set `TPWMTHRS` to the `TSTEP` value measured at the desired crossover. The
  datasheet recommends a **low** transfer velocity, because switching at high
  velocity causes a phase shift between voltage and current, a jerk, and
  possibly a temporary overcurrent (§6.5). A crossover at a few tens of RPM is
  safer than a crossover near the high-speed limit.
- Do not change chopper mode while the motor is moving. Stop, switch, wait for
  standstill, then start again. StealthChop also needs a standstill period
  (at least 128 chopper periods) for its initial current regulation.

Every configuration write follows §3.7. Hybrid setup belongs to a future
`mode` command. Until it exists, log the value sent and confirm the mode from
`DRV_STATUS` bit 30 (`stealth`) with the shaft both above and below the
crossover. `sg` and `tcool` are the commands for the other two registers;
`cur` remains the only current command.

### 3.4 Supply and current

The 1 A figure is the bench supply used that day, not a motor rating. It may be
ample for one motor at 300 mA RMS, but it is not proof that the supply is not
involved, and four axes at 300 mA are not 1.2 A of rail current in any simple
way. StealthChop can draw more than the configured current when its guess is
wrong, and acceleration transients add demand. Read `DRV_STATUS` at the event:
`uv_cp` (undervoltage), `reset`, `otpw`/`ot`, `short`/`open load` separate a
supply or fault event from a torque limit. Measure the supply voltage and
current with a scope or a logging meter; do not infer them from the current
setting.

### 3.5 StallGuard, the hanging load, and lift versus lower

`SG_RESULT` is a 10-bit load indicator: a higher value means less load. It is
a **magnitude**, not a signed torque. Bits 9 and 0 always read 0, it updates
once per full step, and it is intended for StealthChop only.

The thread carries tension only. Positive acceleration is upward, so the thread
force on a 50 g ball is `T = m (g + a)`, and the torque at a 40 mm drum
(radius 20 mm) is `T × 0.020`. `FR-MOT-012` keeps downward acceleration below
`g`, so `T` stays positive. Gravity never pushes on the thread, and the motor
torque that balances it stays in the winding direction for hold, lift, and
controlled lower.

| Segment | `a` [m/s²] | Tension [N] | Torque at the drum [mNm] |
|---------|-----------:|------------:|-------------------------:|
| Hold, or either cruise | 0 | 0.491 | 9.81 |
| Nominal lift | +1.5 | 0.566 | 11.3 |
| Nominal lower | −1.5 | 0.416 | 8.31 |
| Optional lift | +4.0 | 0.691 | 13.8 |
| Optional lower | −4.0 | 0.291 | 5.81 |

The commonly quoted `2 × 9.81 = 19.6 mNm` is the difference between `+mg·r`
and `−mg·r`. That pair is not a pair of operating points of this machine.
What the motor actually sees:

- **Constant velocity, either direction.** Tension is `mg` both ways. Bearing
  and guide friction oppose the shaft velocity, so the two cruises differ by
  about `2 × τ_friction`. On a free hang that residual is the whole up/down
  signal; it is not 19.6 mNm.
- **Acceleration.** The legal up-to-down gap is `2 · m · a · r`: 3.0 mNm at the
  nominal 1.5 m/s² and 8.0 mNm at the optional 4 m/s². The gap exists during
  the ramps and closes as soon as the speed is constant.
- **Rotor and drum inertia.** `doc/mechanical_analysis.md` §3 puts an
  inertia-inclusive lift at the optional tier at 0.022 Nm on a 40 mm drum
  (0.0138 Nm thread torque plus reflected inertia). That term is proportional
  to angular acceleration and reverses with it; it widens the ramp gap and is
  absent in cruise. Air drag is negligible, about 2.1 mN at 1.2 m/s.
- **Hard stop (homing).** The load moves from about 10 mNm toward the motor's
  pull-out torque. That step is the StallGuard event the homing procedure
  depends on, and it is a different measurement from cruise up versus down.
- **Thread break.** Tension falls from about 10 mNm toward zero.
  `NFR-REL-004` does not require continuous detection; a break is inferred at
  the next home, and a frozen board has no second way to see it.
- **Pendulum.** `doc/mechanical_analysis.md` §9 gives a ~2 m pendulum a period
  of 2.84 s (0.35 Hz). For a small swing the tension change is second order,
  about `θ²/2` of the static tension: roughly 1.5 %, or 0.15 mNm at this drum
  for 10°. A free ball in a quiet room (`ASM-005`) does not impress a useful
  0.35 Hz tone on `SG_RESULT`. A vertical guide adds Coulomb friction that
  reverses with direction; it is not present in the installation, and a guided
  run can manufacture an "up/down separation" by itself.

Because `SG_RESULT` is a magnitude, it could separate two different load levels.
On a cruise the two levels are almost the same, so there is almost nothing to
separate. The mechanics predict a small effect, correlated from sample to
sample, and only while StealthChop is inside its valid speed window. Sample the
ramps and the two cruises as separate segments, report the distributions next
to the predicted torques. Do not claim a classifier.

The free hang is the case that counts. If the distributions overlap, the result
is "StallGuard does not see direction on this mechanism," and the experiment
ends. No load cell, strain gauge, or other tension sensor is added: that is a
hardware change, and up/down classification is not a requirement.

`SG_RESULT` updates once per full step. On a 40 mm drum at 1/16 the update
period is ~2.1 ms at 0.3 m/s, ~1.05 ms at 0.6 m/s, ~6.3 ms at 0.1 m/s and
~12.6 ms at 0.05 m/s. Twenty consecutive updates are one short interval of a
correlated signal, not twenty independent trials. Use one sample per full step,
or a block mean, and report means, standard deviations, and the predicted
torque.

### 3.6 UART telemetry cost

A 5 ms sampler is not available for four axes on this UART. The budget has to
be counted in transactions, not in "one read":

- One bit-banged TMC read is roughly 0.6–0.7 ms at 250 kBaud, with interrupts
  masked for most of it (`doc/motion_streaming_whitepaper.md` §5.6.1).
- A full `refresh()` is six transactions: `DRV_STATUS`, `SG_RESULT`, `TSTEP`,
  `CHOPCONF`, `PWM_SCALE`, `PWM_AUTO`. At the worst-case 688 µs that is about
  4 ms for one axis before a reply ends early. Four axes of that sample do not
  fit in 5 ms.
- The existing `monitor` floor of 20 ms matches one axis of `refresh()` plus
  the print. It clamps the interval to 20–2000 ms, so it is a legal session-1
  recorder and it is not a 5 ms sampler.
- IRQ masking during a read does not stop the PIO step generator, but it does
  stall USB, the CAN IRQ, and any queue refill running on the CPU. A jitter
  measurement that only looks at the STEP pin can miss a PIO underrun and a
  CAN delay.
- `TSTEP` is stored in a `uint16_t`. The datasheet value at standstill is
  `0xFFFFF` (20 bits), so a stationary `status` can print `0xFFFF` for a
  register that is `0xFFFFF`. Record the raw 32-bit read in any new trace.

Split the measurement into two numbers on the current UART, with no baud-rate
hardware change: mask time for one `SG_RESULT` read, and mask time for a full
`refresh()`. Time the actual `refresh()` mask, the PIO queue level (or an
underrun indicator), and a CAN frame, at the period being proposed. Propose a
production period only after those numbers exist. The production default stays
the DIAG edge plus the existing 1 Hz health read.

### 3.7 A failed configuration write

A TMC2209 write has no reply. The firmware treats `IFCNT` as the
acknowledgement: it reads `IFCNT`, writes, reads `IFCNT` again, and retries
up to three times. The write landed only when `IFCNT` advanced by one
(`doc/motion_streaming_whitepaper.md` §5.6.1). The console shadow is updated
only after that success. A failed write leaves the previous shadow in place,
so the value just typed is not the value in the chip.

| Console result | What happened | What the session does |
|----------------|---------------|------------------------|
| `ok`, or `raw` prints a value and does not say `write refused` | `IFCNT` advanced. The shadow matches the value sent | Log the value and continue |
| `FAILED` from `cur`, `sg`, `tcool`, `micro`, `chop`, `pwm`, or `senddelay` | The verified write failed, or `cur` / `micro` could not read `CHOPCONF` first. The shadow did not change | Do not `run`. `stats <axis>`, record `wr_fail`. Discard the point. Retry once; a second failure ends the session |
| `write refused` from `raw` | A policy refusal, or the same verified-write failure | `stats`. If `wr_fail` did not increase, the write was never sent: OTP, `VACTUAL`, `IHOLD_IRUN`, or an unsafe `GCONF` (bit 0 or 1 set, or bit 6 or 7 clear). Use `cur` for current. If `wr_fail` increased, it is a lost write and the row above applies |
| `read FAILED` after a write that was not refused | `IFCNT` already acknowledged the write. The read failed, or the register is write-only | See below |

Write-only registers are `SLAVECONF`, `IHOLD_IRUN`, `TPOWERDOWN`,
`TPWMTHRS`, `TCOOLTHRS`, `VACTUAL`, `SGTHRS`, and `COOLCONF` (`0x42`). `raw`
always reads after a write, so a successful write of one of these still prints
`read FAILED` or zero. That read-back is not evidence. Log the value sent.
`COOLCONF` is not `0x6D`. The firmware never writes it, and the reset value is
0, so `SEMIN` stays 0 unless a note records a write to `0x42`. Do not try to
confirm `SEMIN` by reading.

`GCONF`, `CHOPCONF`, and `PWMCONF` are readable. After a write, the printed
read-back has to equal the value sent (`CHOPCONF` keeps the firmware's
`VSENSE` bit). A mismatch, or a second `read FAILED` on a retry, discards the
point. Do not start the motor in a mode the read-back did not show. For the
session-1 SpreadCycle poke, `raw 0 0x00 0x1C4` must print `0x1C4`, and the
return write must print `0x1C0`.

Take `stats` at the start of every session and again after any `FAILED` or
`write refused`. A new `mism` since the previous `stats` means the driver
accepted a different number of writes than the firmware sent. Discard every
point since that previous `stats`. Phase 0's "no IFCNT mismatches" check is
this same counter.

### 3.8 Resonance, and how far microstepping can go

A speed at which the tach loses synchronism and then matches the command
again a few hundred step/s higher is a resonance band, not `f_stall`.
`f_stall` remains the highest rate that stays synchronous for a 2 s dwell.
Record the band (step rate, RPM, mode, microsteps) and continue the staircase.
Stop climbing only when the shaft stays unsynchronous, a driver fault appears,
or a safety limit fires. Crossing a band on purpose takes a higher
acceleration; that measurement is Phase 3, after a synchronous speed above
the band is known. No accelerometer is added. The tach and the noise note are
the instruments.

The session-1 RPM match separates the two causes. 18 kstep/s at 1/16 and
9 kstep/s at 1/8 are the same 337.5 RPM. The same bad RPM points at the rotor.
The same step rate points at the driver or the STEP train. The hanging ball
adds inertia, so session 6 repeats any band found on the unloaded shaft.

Homing sits at the bottom of the same sweep. Whitepaper §5.7 uses
40–50 mm/s. On the 40 mm / 1/16 worked case, 40 mm/s is the 1,000 step/s row
of §2 (0.039 m/s, 18.8 RPM), which is also where StallGuard has the least
back-EMF. Phase 6 tries that speed first.

Microstepping in this plan:

- The worked operating point is 1/16 with interpolation on. Interpolation
  fills each STEP out to 1/256 inside the driver. The STEP rate does not
  change. Phase 4 compares interpolation on and off at the last good RPM.
- 1/8 is a diagnostic of RPM versus step rate. At 40 mm it is 78.5 µm and
  fails `FR-MOT-002`. It is not an operating point.
- 1/32 is the finest setting that still fits the optional tier. Whitepaper
  §5.3 counts 122 steps in a 2 ms chunk at 1.2 m/s on a 40 mm drum, inside
  the 128-step chunk and under the 200 kstep/s PIO ceiling. Run it only when
  a session note names it, and label the file `1/32 candidate`.
  `doc/mechanical_analysis.md` still owns the selection.
- Native 1/256 multiplies the 1/16 rates by 16. The 30.6 kstep/s point becomes
  489 kstep/s, and the 15.3 kstep/s row becomes 245 kstep/s. Both are above
  the 64 kstep/s chunk and the 200 kstep/s PIO ceiling. Do not run 1/256 as
  an operating point. `micro` accepts the number; this plan does not use it.
  At 1/256, interpolation has nothing left to fill, and `SG_RESULT` still
  updates once per full step.

---

## 4. Hypotheses

| ID | Hypothesis | Test |
|----|------------|------|
| H1 | The 20 kstep/s events are StallGuard false triggers, not true stalls | Compare DIAG with an independent tach; repeat with `SGTHRS = 0` |
| H2 | StealthChop is the speed limit; SpreadCycle raises it | Speed staircase in both modes, same current and microsteps |
| H3 | The limit is a common electrical/supply limit, not motor torque | Both motors reach the same step rate; measure supply and `DRV_STATUS` |
| H4 | `SG_RESULT` separates lift from lower only through friction and ramp acceleration; on a free-hang cruise the effect is small | Predicted-torque table, ramps and cruises sampled separately, free hang first |
| H5 | UART cost, not the 5 ms period, sets the telemetry ceiling | Time one `SG_RESULT` read and one full `refresh()`; predict PIO/CAN jitter |

The expected outcome of H1/H2 is that SpreadCycle or `SGTHRS = 0` moves the
limit well above 20 kstep/s; if it does not, H3 becomes the leading
explanation and the supply, wiring, STEP pulse train and `DRV_STATUS` must be
checked before any further tuning.

H4 is an experiment, not a requirement. If the distributions overlap, the
result is "StallGuard does not see direction on this mechanism," and the
experiment ends. H5 decides only whether a future sampler is affordable; the
production path reads UART on a DIAG edge and on an explicit `monitor` or
`status`, not during motion, so a 5 ms sampler cannot explain the 20 kstep/s
observation.

---

## 5. Definitions and figures of merit

- **Commanded step rate** `f_step` [step/s]: STEP pulses per second at the
  TMC input, after microstepping.
- **Full-step rate**: `f_step / microsteps`; `SG_RESULT` updates at this rate.
- **Maximum speed before stall** `f_stall`: the highest step rate at which the
  rotor remains synchronised for a dwell of at least 2 s, tach-confirmed, and
  taken as the minimum over the repetitions. The failure speed is not
  `f_stall` and is not a capability.
- **Maximum acceleration** `a_stall`: the highest acceleration at which the
  motor can ramp from rest to a stated target speed without losing
  synchronisation, again the minimum over repetitions. Acceleration is not a
  single number: record the speed at which the stall occurred.
- **True stall**: the shaft stops or slips poles, confirmed by an independent
  tach, a shaft mark, or a high-speed camera. The commanded STEP train
  continues; `TSTEP` therefore does not prove that the rotor is moving. A true
  stall drops at least four full steps (2.5 mm of thread at 40 mm), so after a
  tach-confirmed stall the open-loop position is wrong by at least that amount.
- **StallGuard event**: `SG_RESULT <= 2 * SGTHRS` with the DIAG gate open.
  This is an indication, not proof of a true stall.
- **Driver fault**: a non-stall `DRV_STATUS` flag (`otpw`, `ot`, `short`,
  `open load`, `uv_cp`, `reset`).
- **Operating margin**: one rule, from the tach-confirmed minimum over
  repetitions: `f_op <= 0.7 * f_stall` and `a_op <= 0.5 * a_stall`. The
  nominal tier is the acceptance target. The optional tier is filled in with
  the same inequalities and reported as an extra; failure there lowers or drops
  the optional tier and does not fail the nominal tier.
- **Safety limit**: a written set of maxima for the session — motor case and
  driver temperature, supply current, tach RPM (1000 RPM is well above the
  optional tier on a 40 mm drum), and run time. On a loaded rig it adds a short
  travel and the catch. The log names which limit fired.

A sweep should record the **cause** of every stop, not only the speed. A stop
at 20 kstep/s caused by `otpw` is a thermal result; the same speed caused by a
false StallGuard pulse is a tuning result.

---

## 6. Test rig and instrumentation

Minimum:

- The Fly-Micro4 on USB serial with `quiet on` during motion. `quiet on`
  silences the periodic console logs; the DIAG line is printed from
  `tmc2209.cpp` and is not gated by that flag, so a saved serial log still
  contains it.
- A current-limited motor supply; record setpoint, measured open-circuit
  voltage, and current limit. The limit is a property of the supply used that
  day, not a motor rating. Do not change it between runs unless that is the
  variable under test.
- A guard around the shaft and any load. For the loaded rig, a captive weight,
  a low drop height, or a catch that does not rely on the driver holding
  current. Enabling the pancake with no catch can drop the ball; the catch is
  in place before the first loaded enable.
- A way to confirm the shaft is turning: an optical tach on a stand, a slotted
  disc, a shaft mark with a slow-motion camera, or a magnetic encoder. The tach
  is strongly recommended: without it, a DIAG event cannot be classified as a
  true stall.
- A scope or logic analyser on STEP, DIR, DIAG and the supply rail. Capture the
  STEP frequency at 18 and 20 kstep/s; confirm the emitted rate matches the
  command.
- Temperature: motor case, driver/board, and ambient as three separate numbers.
  The TMC2209 has no numeric temperature register; use `otpw`/`ot` plus an
  external meter.

The board is frozen. The tach is a separate instrument — optical tach, slotted
disc on a stand, shaft mark and camera. It does not connect to a Fly-Micro4
pin. A scope probe on the existing STEP, DIR, DIAG, and supply pins is a clip,
not a modification. Do not solder a wire, add a pull-up, or repurpose a
production pin. The board returns to the bench with nothing added. The first
session records a board photo or a one-line "no solder, no added wire"
confirmation.

For the loaded rig, the free hang is the case that counts. Guided runs are
labeled `guided` and are only a contamination check; a vertical guide adds
Coulomb friction that reverses with direction and can manufacture an up/down
signal. Guided runs cannot close G5.

---

## 7. Session order

Each session has one note (§12.6). Later sessions are not started to fill empty
days; they are started because the previous note names them.

**Session 0 — identity, no motion.** Fill the identity header before any
command that moves a shaft. `reinit`, `scan`, `read` on M0, M1 and M2. Do not
read COOLCONF: on this chip it is `0x42` and write-only (§3.7). The firmware
never writes it, so SEMIN stays at the reset value 0; write that sentence in
the header. `stats` on M0, M1 and M2; any IFCNT mismatch stops the session.
`status` with one axis enabled and stationary. Record ambient and the supply.
Confirm M3 is absent. Write the safety limits (§8) at the top of the note.
500 mA and 800 mA are allowed on M0, M1, and M2, one axis at a time. Stop
the run if `DRV_STATUS` shows `otpw` or `ot`.

**Session 1 — classify 18 and 20 kstep/s. No new firmware.** Only after the
session-0 note exists. One axis, 300 mA, 1/16, interpolation on, unloaded.
`quiet on`. Scope on STEP, DIAG and the supply, clipped. Tach or camera on
the shaft. Every configuration command has to land per §3.7 before the next
`run`.

```text
quiet on
reinit
enable 0 1
cur 0 300              # ok, or FAILED → §3.7, do not run
micro 0 16 1           # ok, or FAILED → do not run
sg 0 0                 # ok, or FAILED → StallGuard is still at the old shadow
monitor 0 120 20
run 0 18000 1          # the 1 is forward, not a duration
```

Watch the shaft during the ramp. At 2000 step/s² the ramp to 18 kstep/s lasts
9 s. A DIAG or a visible loss during the ramp is an event at the tach speed,
not an 18 kstep/s result. If the tach loses synchronism and then matches again
higher up, record a resonance band (§3.8) and keep going. Then `status 0`,
then `run 0 20000 1`, then `status 0`, then `stop 0`. Restart `monitor` before
the SpreadCycle block and before the 1/8 block. The first monitor does not
cover them.

SpreadCycle, shaft stopped. The read-back has to match before `run`:

```text
raw 0 0x00 0x1C4       # must print 0x1C4; write refused → §3.7, do not run
monitor 0 120 20
run 0 18000 1
status 0
run 0 20000 1
status 0
stop 0
raw 0 0x00 0x1C0       # must print 0x1C0
```

RPM match at 1/8, same 337.5 RPM, diagnostic file only:

```text
micro 0 8 1            # ok, or FAILED → do not run
monitor 0 120 20
run 0 9000 1
status 0
stop 0
micro 0 16 1           # must print ok before any later 1/16 point
```

After a tach-confirmed good speed, time the three stop sources in Phase 0.
If that speed was not 18 kstep/s, do not write "9 s". Save the serial log.
`stats 0` at the end; a new `mism` discards the points since the previous
`stats`. Fill G1. Leave G3's acceleration cell as not measured.

This session decides whether the matrix is a tuning exercise or a supply and
STEP investigation, and it replaces the unclassified "stall/DIAG" row.

**Session 2 — `f_stall` at 300 mA.** Only after G1 is filled. M0 and M2,
StealthChop and SpreadCycle, tach, three repetitions, binary search to
100 step/s as in Phase 2. Write the safety limits at the top of the note.
`sg 0 0` has to print `ok`. Fill the StealthChop and SpreadCycle half of G2,
and the speed half of G3. Leave the hybrid cell `not this session`. Attach
the noise note to the mode. Record any resonance band and keep climbing
through a band that recovers (§3.8).

**Session 3 — StallGuard window.** After `f_stall` is known. Phase 5, one
axis, StealthChop, `SGTHRS` left at 0. This session finds the speed range.
It does not choose the homing threshold and it does not close G4.

**Session 4 — homing window.** After the session-3 note names a window.
Phase 6, 20 cycles, 40–50 mm/s first. Fill G4. This is the StallGuard result
the requirements can use.

**Session 5 — UART cost.** Time one `SG_RESULT` read and one full `refresh()`
with a scope or the existing microsecond timestamps. Capture the PIO queue
level or underrun indicator if the firmware exposes it, and include a CAN frame
if the bus is connected; if it is not, write "CAN not connected" and leave G6
partial. No baud-rate change that needs a wire. Fill G6 with the measured mask
times and a recommended period, which may be "DIAG only."

**Session 6 — loaded tension, short travel, catch fitted first.** Free hang.
The predicted-torque table is filled in before the first move. Ramps and
cruises are logged as separate segments, three up and three down, interleaved.
Repeat any resonance band from session 1. Guided only as a contamination
check. Fill G5 with the distributions and the predicted torques. Stop. No
classifier, no sensor.

**Later, and only if a note names them.** Acceleration command and Phase 3,
after firmware grows a real acceleration argument. 500 mA and 800 mA on one
axis, stopped if `otpw` or `ot` asserts. Hybrid crossover, which closes the
open half of G2. Interpolation on versus off at the last good RPM. A 1/32
file labeled `1/32 candidate`; never native 1/256 (§3.8). Warm repeat of the
single chosen operating point. M1 at that same point. A three-axis run on the
supply that will actually feed the board, as a confirmation of the chosen
point, because the shared 24 V rail and the shared UART do not show up in a
one-axis search. M3 stays out until a motor is plugged into the existing
socket.

No firmware change is required to run session 0 or session 1.

### 7.1 Processing checklist

Work top to bottom. One box is one action. Do not tick a motion box unless
every configuration command since the last `stats` followed §3.7. A session
that names a gate is done only when `doc/bench/tmc2209/<run_id>/note.md` and
`serial.log` are both in that directory.

**Session 0 — no motion**

- [ ] Create `doc/bench/tmc2209/<run_id>/` and open `note.md` with the §12.2 header
- [ ] Photograph the board, or write "no solder, no added wire"
- [ ] Copy §2 into the header. M2 is the US-17HS4023S row. M0/M1 stay "unknown" where the label is missing; that does not stop a 300 mA run
- [ ] Record the supply setpoint, the open-circuit voltage, and the current limit
- [ ] Write the safety limits at the top of the note (§8)
- [ ] `reinit`, then `scan`. Confirm M3 is absent
- [ ] `read` M0, M1, and M2. Copy GCONF, CHOPCONF, and PWMCONF into the header
- [ ] Write "COOLCONF 0x42 not read; firmware writes none; SEMIN reset = 0"
- [ ] `stats` on M0, M1, and M2. `mism` is 0 and `wr_fail` is 0, or the session stops
- [ ] Enable one axis and `status` while stationary. No `otpw`, `ot`, `short`, `open load`, or `uv_cp`
- [ ] 500 mA and 800 mA are allowed on M0, M1, and M2. The required first current is still 300 mA
- [ ] Mark G1–G6 `not this session`

**Session 1 — classify, then the three stops**

- [ ] Confirm the session-0 note exists and names this session
- [ ] Clip the scope on STEP, DIAG, and the supply. Put the tach on the shaft. `quiet on`
- [ ] `reinit`, then `enable 0 1`. `reinit` leaves the driver disabled
- [ ] `cur 0 300`, `micro 0 16 1`, and `sg 0 0` each print `ok`. Any `FAILED`: §3.7, do not `run`
- [ ] `stats 0`. Record `wr_ok` and `mism`
- [ ] `monitor 0 120 20`, then `run 0 18000 1`. The `1` is forward. Watch the 9 s ramp
- [ ] Mark a tach loss that recovers as a resonance band (§3.8), with the step rate and the RPM
- [ ] `status 0`, then `run 0 20000 1`, then `status 0`, then `stop 0`
- [ ] Shaft stopped: `raw 0 0x00 0x1C4` prints `0x1C4`. Restart `monitor`. Repeat 18 kstep/s and 20 kstep/s
- [ ] `stop 0`. `raw 0 0x00 0x1C0` prints `0x1C0`
- [ ] `micro 0 8 1` prints `ok`. Restart `monitor`. `run 0 9000 1`. Label the file `1/8 diagnostic`
- [ ] `stop 0`. `micro 0 16 1` prints `ok`
- [ ] At the last tach-confirmed good speed, time `stop`, `enable 0 0`, and supply removal (Phase 0)
- [ ] `stats 0`. A new `mism` discards the points since the previous `stats`
- [ ] Save `serial.log`. Fill G1. G3 acceleration: "not measured; bench command is 0.079 m/s²"

**Session 2 — `f_stall`**

- [ ] G1 is filled. Safety limits are at the top of this note
- [ ] M0, 300 mA, `sg 0 0` prints `ok`, StealthChop, Phase 2 steps 1–6
- [ ] Same axis in SpreadCycle. The GCONF read-back matches before the staircase
- [ ] Repeat both modes on M2
- [ ] Recovered bands are recorded. `f_stall` is the highest 2 s synchronous rate, minimum over three repetitions
- [ ] Noise note attached to the mode. G2 filled for StealthChop and SpreadCycle. Hybrid cell: `not this session`

**Session 3 — window**

- [ ] `f_stall` is in the session-2 note
- [ ] Phase 5 free-run sweep with `SGTHRS` still 0. Do not write a homing threshold
- [ ] Record the low-speed unstable region and the high-speed collapse. Leave G4 open

**Session 4 — homing**

- [ ] Phase 6 at 40–50 mm/s first (the 1,000 step/s row of §2)
- [ ] If that speed is unstable, record it, then try the bottom of the stable window. The note says which speed the gate used
- [ ] Twenty homes. Record current, speed, `SGTHRS`, `TCOOLTHRS`, travel before the assert, and the stop position
- [ ] Each `sg`, `tcool`, and `cur` printed `ok` before the move that uses it
- [ ] Fill G4. No endstop was added

**Session 5 — UART**

- [ ] Time one `SG_RESULT` read and one `refresh()` (§3.6)
- [ ] A CAN frame is in the capture, or the note says "CAN not connected"
- [ ] Fill G6. The recommended period may be "DIAG only"

**Session 6 — load**

- [ ] Catch fitted before the first loaded enable. Predicted-torque table filled, friction line still blank
- [ ] Free-hang ramps and cruises, three up and three down, interleaved (Phase 7)
- [ ] Repeat any resonance band from session 1
- [ ] Guided runs are labeled `guided` and do not close G5
- [ ] Fill G5 with the two distributions and the predicted torques. No classifier. No sensor

**Later — tick a box only when a note names it**

- [ ] Phase 3, after a firmware acceleration command exists. Until then the cell stays "not measured"
- [ ] 500 mA or 800 mA on one axis. Stop if `otpw` or `ot` asserts
- [ ] One low hybrid crossover. Close the G2 hybrid cell, and say whether CHOPCONF was still the StealthChop bring-up
- [ ] Interpolation off versus on, at the last good RPM, at 1/16
- [ ] A `1/32 candidate` file. Do not run native 1/256
- [ ] Thirty-minute hold at the chosen point, then one hot repeat (Phase 8)
- [ ] M1 at that same point
- [ ] Three axes on the installation supply, at the chosen point only

---

## 8. Test phases

Each phase fills rows in the session record of §12. A phase that does not
produce a one-page note of that shape is not finished. The session order in §7
governs: later phases wait until the previous note names them.

Safety limits, written at the top of every note before the first ramp: maximum
motor case and driver temperature, maximum supply current, maximum tach RPM
(1000 RPM is well above the optional tier on a 40 mm drum), and maximum run
time. On a loaded rig the limit adds a short travel and the catch. The log names
which limit fired. On a loss the tach does not recover from, stop the
staircase. A band the tach leaves and then re-enters is a resonance band
(§3.8), not a stop.

### Phase 0 — Preflight and identity

No motion in this phase. The identity header and the safety limits are written
before the first ramp. The stop-source timings are the last part of session 1,
after a tach-confirmed good speed exists.

1. Fill the identity header (§12.2), ambient temperature, and the supply
   setpoint, measured open-circuit voltage, and current limit. An empty
   nameplate cell stops the session.
2. Write the safety limits (above) at the top of the note.
3. `reinit`; `scan`; confirm every fitted driver reports ready and `stats`
   shows no IFCNT mismatches and `wr_fail` is 0. Confirm M3 is absent. A
   mismatch here is a lost write (§3.7); do not continue.
4. `read <axis>` on M0, M1 and M2. Record GCONF, CHOPCONF, PWMCONF, TSTEP,
   DRV_STATUS, `SG_RESULT` and the current shadow. Do not read COOLCONF
   (§3.7). SEMIN = 0 is the reset default because the firmware never writes
   `0x42`. A nonzero SEMIN is possible only after a recorded write to that
   address, and every StallGuard number in that log is then suspect.
5. `status <axis>` with the motor enabled but stationary. Confirm no `otpw`,
   `ot`, `short`, `open load` or `uv_cp`.
6. Stop here. Session 1 owns the first motion.

Stop-source timings, once session 1 has a tach-confirmed good speed. Scope on
STEP and DIAG, shaft unloaded and the driver enabled before each run. A
disabled motor is not moving, so there is nothing to time until `run`. Record
each source separately, at that good speed:

1. `run`, then `stop`. The latency is the deceleration ramp at 2000 step/s².
   From 18 kstep/s that ramp is 9 s. Record it as a ramp, not an emergency
   stop. If the good speed was not 18 kstep/s, record the ramp from the speed
   that was actually reached and do not write "9 s".
2. `run`, then `enable <axis> 0`. Record whether STEP stops and whether the
   shaft coasts.
3. `run`, then remove the motor supply. Record the same.

Nothing in the current build calls `forceStop()` from the DIAG ISR. Do not
describe it as available until a build contains it and a scope trace is in
the run directory.

### Phase 1 — Static electrical and thermal

- Enable one axis at a time; keep the other motors disabled so the bench supply
  is not shared.
- At 300 mA, hold for 5 min. Record supply current, driver temperature, motor
  case temperature, `DRV_STATUS`.
- Repeat at 500 mA and 800 mA on M0, M1, and M2, one axis at a time. Stop if
  `DRV_STATUS` shows `otpw` or `ot`. 800 mA is an optional bench experiment,
  not an operating recommendation.
- This phase establishes the thermal baseline before any sweep.

### Phase 2 — Speed limit at the design current, StallGuard disabled

Set `SGTHRS = 0` so a StallGuard comparison cannot cause a false DIAG, and use
the tach as the stall reference. Do not start the matrix until the session-1
note exists for the 18 and 20 kstep/s points. Start with M0 at 300 mA only, in
both modes.
That one axis decides whether the rest of the matrix is a tuning exercise or a
supply/STEP investigation. Extend to M2 and to 500/800 mA only after the first
answer exists and the session note names the extension.

For each configuration:

1. Set microsteps 16, interpolation on, `TPWMTHRS = 0` for StealthChop or
   `GCONF.en_spreadCycle = 1` for SpreadCycle. The command prints `ok`, or the
   GCONF read-back matches (§3.7), before the first point.
2. Start at `f0 = 1000 step/s`. Accelerate slowly (2000 step/s²) to the first
   point, dwell 2 s, then increase by a coarse step (500 step/s, or 5 % of the
   current rate, whichever is larger).
3. At each point record: `f_step`, actual tach RPM, `TSTEP`, `DRV_STATUS`,
   `CS_ACTUAL`, `IRUN`, `PWM_SCALE`, `PWM_AUTO`, supply voltage/current,
   temperatures, and the DIAG state.
4. A loss the tach recovers from within a few hundred step/s is a resonance
   band (§3.8): record it and continue. Stop when the tach stays
   unsynchronous, a driver fault occurs, or a safety limit is reached. Record
   `f_stall` as the highest rate that held a 2 s dwell. Do not continue above
   a loss that did not recover.
5. Refine with a binary search between the last good and the first bad point
   (three repetitions at each candidate) to get `f_stall` within 100 step/s.
6. Stop, wait at least 2 s for the driver and any StallGuard flag to clear,
   and proceed to the next configuration.

Session 1 already covers the immediate question at 18 and 20 kstep/s. Phase 2
fills out the curve around it.

A true stall drops at least four full steps, which is 2.5 mm of thread at
40 mm. After a tach-confirmed stall the open-loop position is wrong by at least
that amount, so re-establish the reference before the next loaded point.

### Phase 3 — Acceleration limit

Fix a target speed from the intended envelope (start with 15.3 kstep/s, the
0.6 m/s / 40 mm case, and repeat at 50 %, 70 % and 90 % of the measured
`f_stall`). Set `SGTHRS = 0` and use the tach.

The current bench acceleration is 2000 step/s², which is only 0.079 m/s² at
40 mm and 1/16. Nominal acceleration is 1.0–1.5 m/s² (25–38 kstep/s²);
optional acceleration is 2–4 m/s² (51–102 kstep/s²). The existing commands
never enter the acceleration envelope, so an unmeasured acceleration cell is
not a pass. Write "not measured; bench command is 0.079 m/s²."

Search one target speed at a time. Start at 2000 step/s².

1. Start from standstill. Command that acceleration and run forward.
2. If the tach stays synchronous through the ramp and a 2 s dwell, the
   acceleration is good. The next candidate may be higher: add 2000 step/s²
   or 25 %, whichever is larger, up to the safety limit.
3. On the first tach loss, fault, or safety limit, stop climbing. Record the
   speed at which synchronism was lost. Binary-search between the last good
   acceleration and that first bad one, three repetitions at each candidate.
   Do not command an acceleration above the first bad point.
4. `a_stall` is the minimum over repetitions of the highest good acceleration,
   together with the speed at which a higher one failed. An unmeasured cell
   reads "not measured; bench command is 0.079 m/s²." It is not a pass.

With no load, the free-shaft acceleration limit may be high and may be limited
by the step generator rather than the motor. Treat the loaded result as the
meaningful one. The current `move`/`run` commands hard-code 2000 step/s²; a
new command is needed for this phase (§10).

### Phase 4 — Chopper-mode A/B (incremental)

After Phase 2, this is the incremental A/B, not a repeat of the full staircase:
interpolation, the RPM-matched microstep pair, supply voltage only if the
supply can be set without modifying the board, and one hybrid crossover.

First experiment: change only `en_spreadCycle` and `TPWMTHRS` at the design
current, leaving the StealthChop bring-up CHOPCONF in place. That is the right
first experiment because one thing changes, but it is a weak negative result:
SpreadCycle's torque depends on its blanking times. A conclusion that
"SpreadCycle does not raise the limit" needs a second run with a
SpreadCycle-oriented CHOPCONF, or the sheet must say the chopper was left at
the StealthChop bring-up values.

Modes to compare at the design current:

- StealthChop (`en_spreadCycle = 0`, `TPWMTHRS = 0`)
- SpreadCycle (`en_spreadCycle = 1`)
- Hybrid (`en_spreadCycle = 0`, `TPWMTHRS = TSTEP(f_cross)`) with a low
  crossover, for example 2–7.6 kstep/s (38–143 RPM at 1/16); prefer the low
  end. Do not choose a crossover near the high-speed limit: the datasheet
  warns about the switching jerk and a possible temporary overcurrent.

`TPWMTHRS` is write-only, so §3.7 applies: log the value sent and confirm
`stealth` (`DRV_STATUS` bit 30) with the shaft both above and below the
crossover. A `raw` read-back is not evidence. A `FAILED` or `write refused`
discards the point before any comparison run.

Record the same telemetry plus a noise note. Cumulative motor noise is risk 2
in `doc/RISK.md`, and a faster mode that is louder has to be visible on the
decision sheet. Use one method for every session — same meter or phone, same
distance, same written background line, A-weighting if the meter has it. An
uncalibrated "dB" is comparative inside this bench and is not an installation
prediction; 120 motors are not 20 dB of one motor.

Diagnostic A/B at the last tach-confirmed good RPM, not at a failure speed.
Session 1 uses 18 kstep/s at 1/16, which is 9 kstep/s at 1/8 (337.5 RPM). If
`f_stall` later moves that point, repeat the pair at the new good RPM.

- 1/16 vs 1/8 at the same **RPM** (the step rate halves at 1/8). If the limit
  follows RPM, it is a motor/voltage limit; if it follows step rate, it is a
  driver/step-rate limit.
- Interpolation on vs off, at 1/16. This is the 256-fill inside the driver.
  It does not change the STEP rate (§3.8).
- `TPWMTHRS = 0` vs a low hybrid crossover.
- Supply voltage, if adjustable without a board change: 12 V vs 24 V (record
  the actual rail).

At 40 mm, 1/8 is 78.5 µm and fails `FR-MOT-002` (50 µm). A speed result at 1/8
stays a diagnostic. The operating point remains the 1/16 worked case until
microstepping is selected in `doc/mechanical_analysis.md`. Label every file
`1/16 worked case`, `1/8 diagnostic`, or `1/32 candidate`. Do not run native
1/256 (§3.8).

### Phase 5 — StallGuard window (session 3)

Only in StealthChop, after `f_stall` is known, and before homing. `SGTHRS`
stays 0. `sg 0 0` has to print `ok` (§3.7). This phase finds the speed range
where `SG_RESULT` is stable. It does not choose the homing threshold and it
does not close G4. A friction brake is not part of this phase: it can
manufacture a load the installation does not have.

1. Sweep `f_step` from 500 to the Phase 2 StealthChop limit with `SGTHRS = 0`.
   Record `SG_RESULT` and `TSTEP` at each point. This is the free-run SG
   baseline; find the low-speed region where SG is unstable and the high-speed
   region where it collapses. The 1,000 step/s row (about 40 mm/s) is the
   homing speed Phase 6 will try first; say whether it sits inside the stable
   region.
2. Repeat at 500 and 800 mA on the same axis. 300 mA is the design current
   and the required point. Stop if `otpw` or `ot` asserts.
3. With `SGTHRS` still 0, a DIAG edge is a fault or a real loss of torque, not
   a StallGuard trip. Compare it with the tach and with `DRV_STATUS`.

Use one `SG_RESULT` sample per full step, or a block mean. Twenty consecutive
samples are one short interval of a correlated signal, not twenty independent
trials. The result is a table of the usable SG window per motor, current and
speed. If the window does not cover the intended operating range, do not use SG
for stall detection outside it. The time from a DIAG edge to the last STEP
pulse is unmeasured: DIAG does not stop the step train (§9.3).

### Phase 6 — Homing window (session 4, the StallGuard job)

After the session-3 note names a window. One axis, StealthChop, the upper hard
stop or a bench block that is not attached electrically. This phase writes
`SGTHRS`. Every `cur`, `sg`, and `tcool` has to print `ok` before the move
that uses it (§3.7).

Try 40–50 mm/s first. That is the installation homing speed (whitepaper §5.7)
and, at 40 mm and 1/16, the 1,000 step/s row of §2. If session 3 found
`SG_RESULT` unstable there, record that and only then try the lowest speed
inside the stable window. The note says which of the two speeds the gate used.
Do not silently substitute 0.3 m/s and close G4.

Command the move the installation will use: low current, that speed, upper
stop, reject an assert before a minimum travel, accept an assert after it,
back off, and repeat. Compare every DIAG with the tach. A DIAG while the shaft
is still turning is a false trip. A tach stall with no DIAG is a miss.

Record current, speed, `SGTHRS`, `TCOOLTHRS`, travel before the assert, and the
step position of the stop over 20 cycles; note the current at which an
immediate assert disappears. Twenty homes are the repeatability figure. Time
the mechanical stall to the DIAG edge once `SGTHRS` is nonzero. That interval
is at least four full steps. Do not time DIAG-to-last-STEP: the step train
does not stop on DIAG (§9.3). No endstop is added. The cruise up/down
experiment does not substitute for this.

### Phase 7 — Load-direction experiment (later)

Add the intended drum, ~2 m thread and 50 g ball. Keep the travel short for
the first runs and fit the catch before the first loaded enable. Disabling the
pancake during a loaded test drops the ball; `FR-MOT-011` allows that descent
in the installation, but on the bench the catch is in place and the note names
the stop under test: hold current on, or driver disabled.

Before the first move, fill the predicted torque of each segment in the run
note: the table in §3.5 plus a friction line left blank until measured. Sample
the ramps and the two cruises as separate segments.

For each speed (0.05, 0.1, 0.3, 0.6 m/s; optionally 1.2 m/s) and both
directions, free hang first:

1. Move to a start position with a low acceleration.
2. Run a constant-velocity segment, up or down, long enough for at least 20
   SG updates. Log `SG_RESULT`, `TSTEP`, `DRV_STATUS`, `CS_ACTUAL`,
   `PWM_SCALE`, supply and temperature.
3. Repeat three times per direction; interleave up and down to reduce thermal
   drift.
4. Compute the mean and standard deviation of `SG_RESULT` for up and down,
   and report them next to the predicted torques. Use one sample per full step,
   or a block mean, so the standard deviation describes the load rather than
   the sampler.
5. Repeat guided only as a contamination check. Check whether the 0.35 Hz
   pendulum modulation is filtered by a median over 5–10 samples. A guided run
   cannot close G5.

Do not claim a classifier. If the distributions overlap, the result is
"StallGuard does not see direction on this mechanism," and the experiment
ends. No load cell, strain gauge, or other sensor is added.

The expected outcome is that SG is usable at low/medium StealthChop speeds and
not at high speed or in SpreadCycle. Up/down detection, if it exists at all,
is a small effect from friction and ramps, not a 19.6 mNm signal.

### Phase 8 — Repeatability and thermal drift

- Hold the single chosen operating point for 30 min. Log temperature,
  `SG_RESULT`, `CS_ACTUAL`, `PWM_SCALE` and the tach. This is useful at the
  chosen point, not as a repeat of the whole staircase.
- Repeat the chosen point after warm-up. A lower `f_stall` when hot is a
  thermal/torque result, not a tuning result.
- Run M1 at the chosen point to see unit-to-unit variation. Do not assume M0
  and M1 are identical, and do not run a second full matrix by default.
- M3 stays out of every phase until a motor is plugged into the existing
  driver socket. Plugging a motor in is ordinary bench setup, not a reason to
  start M3 now.

---

## 9. Stall detection and immediate stop

### 9.1 Event sources

| Source | Meaning | Action |
|--------|---------|--------|
| DIAG edge | StallGuard, `otpw`, `ot`, short, open load | Read `DRV_STATUS` and `SG_RESULT`; classify; stop |
| Tach error | Measured RPM differs from commanded by more than a threshold, or drops to zero | True loss of synchronisation; stop |
| Supply monitor | `uv_cp`, supply below a threshold | Stop; check supply and wiring |
| Operator, `stop`, `enable 0`, supply removed | The stops that exist today (§9.3). `abort` is not in this build | Record which one. `stop` is the deceleration ramp, not an immediate stop |
| Safety limit | A written limit from §8 fired | Stop; record which limit |

### 9.2 Classification

At a DIAG event, read `DRV_STATUS` before deciding:

- If any non-stall fault bit is set, record a driver fault. Stop and do not
  retry until the cause is cleared.
- If `stealth = 0` (SpreadCycle), StallGuard is not available; DIAG may be a
  different fault. Use the tach for stall detection.
- If `stealth = 1`, `TSTEP` is inside the `TCOOLTHRS`/`TPWMTHRS` window, and
  `SG_RESULT <= 2 * SGTHRS`, classify a StallGuard stall. Confirm with the
  tach before calling it a true stall.
- If the tach shows the rotor still turning, the DIAG was a false StallGuard
  trigger or a fault; record it as such.

A DIAG that remains after `sg 0 0` is a fault or a real loss of torque, which
`CS_ACTUAL`, `PWM_SCALE` and `DRV_STATUS` then separate. A high-speed loss of
synchronism can show a high `SG_RESULT` and no DIAG.

### 9.3 Stop action

DIAG today does not stop the motor. `stepper_axes::stop()` calls `stopMove()`
only, and `forceStop()` is not wired. Until a build contains an immediate stop
and a scope trace is in the run directory, treat the stop as one of `operator`,
`stop`, or `supply`.

- `stopMove()` is a normal deceleration. It is not an immediate stop. At
  2000 step/s² from 18 kstep/s the ramp is 9 s.
- `forceStop()` stops issuing new steps and can be called from an interrupt,
  but it is not in the current build. The library documents a stop "within
  approx. 20 ms" including queued work; that has not been measured on this
  board. `forceStopAndNewPosition()` keeps the position bookkeeping consistent
  if such a stop is added.
- For a hanging load, an abrupt stop is not automatically safe: the ball may
  swing or the thread may go slack. Use a mechanical catch and decide whether
  to keep the driver enabled (hold current) or disable it. Never rely on the
  driver to hold a load after a stall.
- If a faster stop is added, measure the latency from the DIAG edge to the last
  STEP pulse with a scope. Target the larger of 5 ms and four full-step
  periods. If queued work makes this impossible, implement the faster stop at
  the step source and re-measure.
- Record the stop action in the run log. A test that stops by `stopMove()` and
  coasts is not the same as an immediate stop.

### 9.4 Settling between points

After a stall or a false DIAG, wait at least 2 s before the next point. The
driver may need time to clear its internal StallGuard indicator, and a stalled
motor may be hot. Clear the latched DIAG flags with `diag clear`. For loaded
tests, re-establish the position reference before the next run.

---

## 10. Proposed serial commands

These are not implemented. They extend `rp2040/src/bench_console.cpp` and
`rp2040/src/tmc2209.cpp`; the production CAN path is unchanged.

| Command | Purpose |
|---------|---------|
| `mode <axis> <stealth\|spread\|hybrid> [hz]` | Set `GCONF.en_spreadCycle` and, for hybrid, `TPWMTHRS = TSTEP(hz)`; require the axis to be stopped |
| `sweep speed <axis> <f0> <df> <fmax> <dwell_ms> [accel] [repeats]` | Speed staircase with immediate stop on the event |
| `sweep accel <axis> <ftarget> <a0> <da> <amax> <dwell_ms> [repeats]` | Acceleration staircase from rest |
| `sgtrace <axis> <interval_ms> <duration_ms> [hz]` | Buffered `SG_RESULT`/`TSTEP`/`DRV_STATUS`/PWM trace; print CSV after the run |
| `ldtrace <axis> <hz> <up\|down> <seconds> [interval_ms]` | Constant-velocity trace for the load-direction experiment |
| `abort` | Not in this build. Force-stop all axes, latch the cause, print a summary. Until then the stops are §9.3 |
| `report` | Print the last sweep summary and the raw CSV row count |

Implementation notes:

- `df` and `da` are absolute increments. Choose `df` near 5 % of the current
  rate with a 500 step/s floor, and `da` near 25 % of the current acceleration.
- Keep the motor running between speed points: set the next speed and call
  `applySpeedAcceleration()` instead of stopping and restarting. A stop and
  restart would add a full ramp at every point and make the sweep very slow.
  The current `run` command does not expose this; the new test command must.
- The commands must be non-blocking. `bench_console::poll()` sets a state and
  returns; the main loop calls a `motor_test::poll(now)` function. Do not use
  `delay()` in a sweep.
- Buffer samples in RAM and print after the run. Do not use 5 ms as the trace
  period. One axis of `refresh()` is about 4 ms at the worst-case 688 µs per
  read, and four axes of that sample do not fit in 5 ms (§3.6). A live
  100-character line at 115200 baud takes about 9 ms, so the print is not a
  5 ms sampler either. The `sgtrace` interval has to be at least the mask time
  measured in session 5. The 20 ms `monitor` floor is the session-1 recorder.
- `TSTEP` is stored in a `uint16_t`; the datasheet standstill value is
  `0xFFFFF`. A new trace stores the raw 32-bit read. Until it does, note on the
  sheet that a printed standstill `TSTEP` may be the low 16 bits.
- An immediate stop would have to stop the step source. The current build does
  not wire `forceStop()`; do not list it as available until a build contains it
  and a scope trace is in the run directory. If the ISR only sets a flag,
  remove the `delay(1)` from the test loop so the main loop can react within a
  few hundred microseconds.
- The production `tmc2209::poll()` also reads UART on a DIAG edge. Add a test
  mode that lets the test collector own the event read; otherwise the two
  readers race and the first one consumes the useful values.
- Abort the test if a CAN motion command arrives. The test claims the axes;
  the CAN path must not drive them concurrently.
- Print a machine-readable header once per run, using the sample row in §12.4.
- Restore the boot configuration (`reinit`) after the test, or print exactly
  what was changed so the next operator can restore it.

The existing `monitor` command is a useful stopgap for a single axis at a
time, but it is time-limited and prints live; it is not a substitute for the
buffered trace.

---

## 11. Immediate triage with the existing commands

These commands are available today and are session 1 of §7. Keep the mechanism
clear and enable only one axis. The current `move`/`run` acceleration is fixed
at 2000 step/s², so this triage is about speed and mode, not the acceleration
limit. At 2000 step/s² the ramp to 18 kstep/s lasts 9 s; watch the shaft during
the ramp, and treat a DIAG or visible loss during the ramp as an event at the
tach speed, not as an 18 kstep/s result.

The keystrokes are the session-1 blocks in §7. `run <axis> <hz> 1` is
forward, not a one-second dwell. `monitor` is the legal session-1 recorder:
it clamps to 20–2000 ms, so it is not a 5 ms sampler but it fits the UART.
Restart it before the SpreadCycle block and before the 1/8 block. Save the
serial log; a scroll that is not saved does not count. `quiet on` silences
the periodic logs but not the DIAG line.

The current GCONF is `0x1C0` (pdn_disable, mstep_reg_select, multistep_filt).
Set bit 2 for SpreadCycle (`0x1C4`) and clear it to return (`0x1C0`). Both
are readable writes: the console has to print the value just sent (§3.7)
before the next `run`. A `FAILED` from `cur`, `sg`, or `micro` means the
shadow did not change. Do not run on the value that was typed.

`raw` writes through `tmc2209::writeRegister()`, which preserves VSENSE for
CHOPCONF and refuses unsafe GCONF bits; do not use it for current control (use
`cur`). With `SGTHRS = 0` and the tach as reference: if the event disappears
while the shaft keeps turning, the original DIAG was a StallGuard false
trigger; if a DIAG remains with the shaft turning, read `DRV_STATUS` for a
fault; if the tach shows the shaft stop, it is a true stall. If the event
disappears only in SpreadCycle, it was a StealthChop speed limit. If it
persists in both, check the supply, STEP waveform and `DRV_STATUS`.

Also run the microstep diagnostic at the same RPM: 18 kstep/s at 1/16 is
337.5 RPM; the same RPM at 1/8 is 9 kstep/s. If the limit tracks RPM, it is a
motor/voltage limit; if it tracks step rate, it is a driver/step-rate limit.
At 40 mm, 1/8 is 78.5 µm and fails `FR-MOT-002` (50 µm), so this is a
diagnostic file, not an operating point.

The 1/8 block is in §7. `micro 0 8 1` has to print `ok` before `run 0 9000 1`,
and `micro 0 16 1` has to print `ok` before any later 1/16 point.

---

## 12. Result record

This is the product of the bench. The phases are how the rows get filled. A
phase that does not produce a note of this shape is not finished.

### 12.1 Where it lives

One directory per session:

```
doc/bench/tmc2209/<run_id>/
    note.md          # the decision note; this is what a later session reads
    serial.log       # the USB session, unedited
    samples.csv      # one row per sample, written after the run when firmware grows a buffer
    events.csv       # tach, supply, temperature, scope file names, stop cause
```

`run_id` keeps the form `2026-11-10-M0-SC-300-16-01`: date, axis, mode, run
current in mA, microsteps, repeat. `SC` / `SP` / `HY` mean StealthChop /
SpreadCycle / hybrid. Add `-diag` when the microstep column is an RPM-matched
diagnostic rather than a candidate operating point.

`note.md` is technical markdown and belongs in `doc/`. The log and the CSV are
the audit trail for that note. A summary pasted into chat is not a copy of
`note.md`. A session is finished when its gate row is filled and the raw log is
in the same directory. A session whose stop cause is blank is discarded.

### 12.2 Identity header

Written once, before motion. Empty cells are filled or the session stops.

| Field | Source |
|-------|--------|
| run_id, date, operator | session |
| git revision of the firmware that is actually running | `git describe` / hash |
| board | Fly-Micro4, unmodified |
| axis, UART address from `scan` | firmware |
| motor MPN, full steps/rev | nameplate |
| rated current, phase resistance, phase inductance, holding torque | nameplate |
| detent, if used in the stop decision | `doc/mechanical_analysis.md` §3.1 or a new measurement |
| drum diameter, thread, load mass | meter / scale. `none` is a legal load |
| hang | `unloaded` / `free` / `guided`. Guided cannot close G5 |
| supply setpoint, measured open-circuit voltage, current limit | supply front panel and a meter |
| UART `bit` µs, `idle` bits, `SENDDELAY` | console |
| GCONF, CHOPCONF, PWMCONF | `read`. Read-back must match any value this session wrote (§3.7) |
| COOLCONF | Not read. Address `0x42`, write-only. Reset SEMIN = 0 unless the note records a write |
| SGTHRS, TCOOLTHRS, TPWMTHRS | Console shadow after the command prints `ok`. Not a write-only read-back |
| ambient °C | meter |
| board photo or "no solder, no added wire" | first session and after any setup change |

`COOLCONF` belongs here as the sentence in the table, not as a read-back.
The firmware never programs CoolStep. A note that records a write to `0x42`
makes every StallGuard number in that log suspect, because SEMIN would then
be reducing current from `SG_RESULT`. `TCOOLTHRS = 0xFFFFF` opens the CoolStep
velocity window, but with SEMIN at 0 it does not reduce current.

### 12.3 Predicted load

Required for every loaded session, computed before the run, for the drum and
mass of that session. For the 50 g / 40 mm case the predictions are the table
in §3.5 plus a friction line left blank until measured. The friction term is
filled from the measurement, not invented in advance. An unloaded session
writes `torque = 0` and skips the table.

### 12.4 Sample row

One schema. Do not keep a second table or header.

```
t_ms,axis,mode,current_ma,microsteps,intpol,cmd_hz,tach_rpm,tstep,sg,sgthrs,
tcoolthrs,drv_status,cs_actual,irun,pwm_scale,pwm_ofs,pwm_grad,event
```

`event` is one of `none`, `diag`, `tach`, `otpw`, `ot`, `short`, `open`,
`uv_cp`, `supply`, `operator`, `safety`. A DIAG line's `DRV_STATUS` is copied
into the row. `stall` is not an event name. The cause is.

`samples.csv` may be produced by saving `monitor` output for session 1. The
columns that `monitor` does not print (supply, temperature, tach) go in
`events.csv` with the same `t_ms` origin, which is the firmware millisecond
printed on the `mon` line. A row with no time base is not joinable and does not
count. Log `TSTEP` from the raw 32-bit read in any new trace; until that
exists, a printed standstill `TSTEP` may be the low 16 bits.

### 12.5 Events row

```
t_ms,supply_V,supply_A,motor_case_C,driver_C,ambient_C,tach_rpm,scope_file,stop_source,notes
```

Temperature is three numbers. The TMC2209 has no temperature register; `otpw`
and `ot` stay in `drv_status`. One undifferentiated "temp" column cannot be
interpreted a week later.

### 12.6 Decision note

`note.md` is one page. It contains, in order:

1. The identity header.
2. The predicted torques, or `unloaded`.
3. The commands that were actually typed.
4. The events, each with a cause. Quote the DIAG line and the tach at that
   moment.
5. The gate table below, with one row updated and the others explicitly
   `not this session`.
6. The relative paths of `serial.log`, `samples.csv`, `events.csv`, and any
   scope capture.

Gate table, copied into every note so a single file stands alone:

| Gate | Question this session | Result | Envelope consequence | Not tested |
|------|-----------------------|--------|----------------------|------------|
| G1 | True stall or DIAG with the shaft turning? | | | |
| G2 | Does SpreadCycle or a low hybrid move `f_stall`? | | mode, plus noise note | |
| G3 | Nominal tier inside `0.7 × f_stall` and `0.5 × a_stall`? | | commanded limits | acceleration, if still 0.079 m/s² |
| G4 | Homing window: current, speed, `SGTHRS`, repeatability | | homing parameters | |
| G5 | `SG_RESULT` versus the predicted tension | | no hardware action | |
| G6 | Measured UART mask, and the period that fits | | poll period, or DIAG-only | |

A gate with no run_id is open. Closing G3 with an acceleration cell of "not
measured" is a partial close and must say so in the envelope consequence.

### 12.7 Rules that keep the record honest

- The minimum over repetitions is the value that enters the margin. Median and
  maximum are reported beside it.
- A stop without a cause is a discarded run.
- A configuration command that printed `FAILED` or `write refused`, or a new
  `stats` mismatch, discards every point since the previous good `stats`
  (§3.7). The typed value is not logged as the active value.
- Free-hang cruises are the only cruises that speak about the installation.
  Guided runs are labeled `guided` and cannot close G5.
- 1/8 files are labeled diagnostic.
- Noise readings share one written method.
- The board photo or a one-line "no solder, no added wire" confirmation is part
  of the identity header for the first session and after any setup change.
- `reinit` after the session, or the note lists every register that was
  changed and the value restored.

---

## 13. Decision gates and expected outcomes

Each gate's allowed outcomes are a register value, a commanded envelope, or a
measured limit on this board. Hardware is not an outcome.

| Gate | Question | Allowed outcomes |
|------|----------|------------------|
| G1 | True stall or DIAG with the shaft turning? | False DIAG: restrict `TCOOLTHRS` / `SGTHRS`, or leave StallGuard off outside the measured window. True stall: the limit is mechanical/electrical on this driver. |
| G2 | Does SpreadCycle or a low hybrid move `f_stall`? | Choose StealthChop, SpreadCycle, or a low hybrid crossover. Record the noise note with the choice. If SpreadCycle was left on the StealthChop chopper, say so. |
| G3 | Nominal tier inside `0.7 × f_stall` and `0.5 × a_stall`? | Accept the nominal envelope, or lower the commanded envelope to the measured margin. The optional tier is a reported extra. |
| G4 | Homing window: current, speed, `SGTHRS`, repeatability | Homing uses StallGuard inside the measured window, at a stated current and speed. Outside that window, stall detection is the tach on the bench and is absent in the installation. |
| G5 | `SG_RESULT` versus the predicted tension | Report whether `SG_RESULT` moves with the predicted tension. No sensor is added if it does not. |
| G6 | Measured UART mask, and the period that fits | Keep event-driven DIAG reads. Add a periodic read only at a measured period that fits the UART budget. |

A likely outcome, based on the datasheet and field experience, is:

- The 20 kstep/s limit is partly a StallGuard false trigger and partly a
  StealthChop limit; `SGTHRS = 0` and SpreadCycle should separate them.
- SpreadCycle may raise the usable speed but loses StallGuard. Homing then
  runs in the StealthChop window, and above it there is no sensorless stall
  detection.
- `SG_RESULT` is usable at low/medium speed in StealthChop, not at high speed
  and not in SpreadCycle. A 5 ms sampler is not available for four axes; the
  production default is the DIAG edge plus the existing 1 Hz health read, with
  a periodic read only after its mask time is measured.
- Up/down from `SG_RESULT` is at best a small friction-and-ramp effect on a free
  hang. If it is not visible, the result is "StallGuard does not see direction
  on this mechanism," and no sensor is bought.

The driver selection is closed: the Fly-Micro4 with its onboard TMC2209s is
the selected node (`doc/controller_analysis.md` §11.4). The earlier TMC5160
discussion in §6 of that document is not reopened by this characterization. If
a future requirement needs sensorless stall detection across the full envelope,
that is a new project decision, not an outcome of this bench.

---

## 14. Risks and pitfalls

- **False StallGuard at high speed.** `TCOOLTHRS = 0xFFFFF` leaves the stall
  output enabled at every speed. A high-speed SG collapse or a resonance band
  can pulse DIAG. Always read `DRV_STATUS` and confirm with a tach. A band
  the tach recovers from is not `f_stall` (§3.8).
- **Mode switching jerk.** Do not change `en_spreadCycle` or `TPWMTHRS` while
  moving. A hybrid crossover near the high-speed limit can produce a jerk or a
  temporary overcurrent (datasheet §6.5).
- **Failed writes.** A `FAILED` or `write refused` leaves the previous shadow
  in place (§3.7). Do not run, and do not log the typed value as the active
  one. `read FAILED` after an acknowledged write of a write-only register is
  not a failed write.
- **Write-only registers.** `SLAVECONF`, `IHOLD_IRUN`, `TPOWERDOWN`,
  `TPWMTHRS`, `TCOOLTHRS`, `VACTUAL`, `SGTHRS`, and `COOLCONF` (`0x42`) have
  no readable value. Log the value sent. Confirm the chopper mode from
  `DRV_STATUS`, not from a `TPWMTHRS` read-back.
- **Supply current limit.** The 1 A figure belongs to the bench supply used
  that day, not to the motor or the installation. It may be enough at 300 mA
  but not during acceleration or a StealthChop mis-guess. Measure the rail; do
  not infer it, and do not treat the figure as a motor rating.
- **No position feedback.** A stalled open-loop motor loses at least four full
  steps. After a stall, re-home or reset the reference before the next loaded
  run.
- **Hanging load.** An abrupt stop or a driver disable can drop the ball. Use
  a catch and decide the stop policy before the loaded tests. The pancake holds
  only 22 g on detent at 40 mm (`doc/mechanical_analysis.md` §3.1).
- **UART blocking and scope budget.** A TMC read masks interrupts for most of
  a millisecond, and `refresh()` is six transactions. Measure the mask time for
  one read and one `refresh()` before proposing a periodic sampler. A jitter
  measurement that only looks at the STEP pin can miss a PIO underrun and a CAN
  delay.
- **`TSTEP` width.** The driver value is 20 bits; the firmware stores a
  `uint16_t`. A printed standstill `TSTEP` may be the low 16 bits. Record the
  raw read in new traces.
- **Statistics.** Consecutive `SG_RESULT` updates are correlated. Twenty
  samples are one short interval, not twenty independent trials. Report means,
  standard deviations, and the predicted torque. Do not claim a classifier.
- **Thermal drift.** Motor torque and SG amplitude change with temperature.
  Repeat the chosen point hot and cold.
- **Pendulum and guide friction.** A 2 m ball swings at ~0.35 Hz; a guide adds
  Coulomb friction that reverses with direction. Both contaminate the
  load-direction signal. Free hang is the case that counts; record which was
  used.
- **Noise method.** An uncalibrated dB with no distance or weighting cannot be
  compared across sessions. Write one method down and use it every time; the
  number is comparative inside this bench, not a prediction for 120 motors.

---

## 15. References

- TMC2209 datasheet, Rev. 1.09, 2023-02-16 (Analog Devices/Trinamic):
  §5.2–5.3 (TSTEP, TPWMTHRS, TCOOLTHRS, SGTHRS, SG_RESULT), §6.5
  (StealthChop/SpreadCycle combination), §11 (StallGuard4 limits and tuning).
  `https://www.analog.com/media/en/technical-documentation/data-sheets/tmc2209_datasheet_rev1.09.pdf`
- Klipper, "TMC drivers":
  `https://www.klipper3d.org/TMC_Drivers.html` — SpreadCycle default,
  StealthChop trade-offs, sensorless homing limits.
- Klipper discourse, "SpreadCycle and StealthChop (an advanced guide)":
  `https://klipper.discourse.group/t/spreadcycle-and-stealthchop-an-advanced-guide/23938`
- 1c3d, "Tuning Trinamic TMC2209 Stepper Drivers":
  `https://1c3d.com/howtos/tuning-tmc2209-stepper-drivers-stealthchop-spreadcycle-sensorless-homing`
- FluidNC issue #698, "TMC2209 & SpreadCycle & Interpolation":
  `https://github.com/bdring/FluidNC/issues/698`
- `doc/motion_streaming_whitepaper.md` §5.6.1 and §5.7 — bring-up, bench
  console, StallGuard calibration, UART timing, homing through DIAG.
- `doc/mechanical_analysis.md` §3, §3.1, §4 and §9 — torque budget, power-off
  hold, step rates and drum speed, pendulum period.
- `doc/controller_analysis.md` §11.4 — selected Fly-Micro4 node. The earlier
  driver comparison in §6 is background and does not reopen the selection.
- `doc/RISK.md` — noise (risk 2) and motor-selection risks.
- `doc/bench/tmc2209/<run_id>/` — session notes, serial logs, and sample
  records produced by this plan.
