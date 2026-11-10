# TMC2209 Session 1 — classify 18 and 20 kstep/s

> **CORRECTION (operator observation, after the later staircase runs): the
> rotor was seen to stall.** The result below that "20 kstep/s is fine" is
> **withdrawn**. It rested on TSTEP (step-source only) and `SG_RESULT`, and
> `SG_RESULT` reads *high* while the rotor is stalled, so neither can prove
> motion. G1 is **not closed**; `f_stall` is not established. Per the plan, a
> tach or shaft-mark camera is required for every speed conclusion. The
> surviving result is that **`sg 0 0` does not silence DIAG**.
>
> Original session-1 status: no tach, scope, thermal or supply meter (operator
> waived them). No driver fault was seen.

## Identity header

| Field | Value |
|-------|-------|
| run_id | `2026-11-10-M0-SC-SP-300-16-01` |
| date, operator | 2026-11-10; agent on USB serial, human operator present |
| git revision (repo) | `6817ab4-dirty`; running build has no version banner → unverified |
| board | Fly-Micro4, unmodified; `no solder, no added wire` pending confirmation |
| axis | M0 only; address 0. M1–M3 stayed disabled |
| motor | full NEMA-17, unlabelled; §2 candidate: 200 steps/rev, 1.5 A, 1.6 Ω, 3.8 mH, 0.42 Nm |
| load, hang | none, `unloaded` |
| supply, ambient | not measured (no meters) |
| UART | 4 µs/bit (250000 Baud), SENDDELAY=2 |
| GCONF / CHOPCONF / PWMCONF | 0x1C0 → 0x1C4 (SP) → 0x1C0 / 0x14020054 / 0xC10D0024 |
| SGTHRS / TCOOLTHRS / TPWMTHRS | 0 (test) → 40 (restored) / 0xFFFFF / 0 |
| COOLCONF | 0x42 not read; firmware writes none; SEMIN reset = 0 |

## Predicted load

`unloaded`, torque = 0.

## Commands typed

```text
quiet on
stats 0
reinit
enable 0 1
cur 0 300
micro 0 16 1
sg 0 0
stats 0
diag clear

# Block A, StealthChop
monitor 0 45 20
run 0 18000 1
status 0
run 0 20000 1
status 0
stop 0
monitor 0 0

# Block B, SpreadCycle
raw 0 0x00 0x1C4          # printed 0x1C4, stealth=0 confirmed in status
diag clear
monitor 0 45 20
run 0 18000 1
status 0
run 0 20000 1
status 0
stop 0
monitor 0 0
raw 0 0x00 0x1C0          # printed 0x1C0

# Block C, 1/8 diagnostic
micro 0 8 1
monitor 0 30 20
run 0 9000 1
status 0
stop 0
monitor 0 0
micro 0 16 1

stop 0
enable 0 0
reinit
stats 0
```

## Events

| Time | Event | Reading |
|------|-------|---------|
| 5.5–17.5 s | 18 kstep/s, StealthChop | TSTEP 42 (0x2A) held; SG mean 373, max 478; **0 DIAG** |
| 18.8 s | 18k → 20k transition, StealthChop | 2 DIAG pulses (`sg=0`, `sg=340`), TSTEP then 39–38 |
| 18.2–22.2 s | 20 kstep/s, StealthChop, steady | TSTEP 38 (0x26), SG mean 347, min 22; **no further DIAG** for the last ~3 s |
| 22.8–33.8 s | `stop 0` from 20 kstep/s | **128 DIAG pulses**, concentrated at TSTEP 50–67 (≈11.2–15 kstep/s); SG samples hit 0 twelve times |
| 35.9–47.9 s | 18 kstep/s, SpreadCycle | TSTEP 42; SG ≈13 (invalid in SP); **0 DIAG** |
| 48.5–52.5 s | 20 kstep/s, SpreadCycle | TSTEP 38; SG ≈33 (invalid in SP); **0 DIAG** |
| 53.1–64.1 s | `stop 0` from 20 kstep/s, SpreadCycle | **0 DIAG** |
| 66.3–73.3 s | 9000 step/s at 1/8 (337.5 RPM), StealthChop | TSTEP 42; SG mean 346; 4 DIAG pulses at 72.2–72.4 s; then quiet |
| 73.9–79.9 s | `stop 0` from 9000 step/s, 1/8 | **72 DIAG pulses**, TSTEP 42→76 |

After the cleanup `reinit` (SGTHRS back to 40) with the shaft stopped, `diag`
read `1000`: M0's DIAG pin was high at standstill, consistent with
`SG_RESULT=0 <= 2*40` while `TSTEP=0xFFFFF <= TCOOLTHRS`. Session 0 read
`0000` after the same reinit, so the level is state/phase dependent; the
bring-up default can leave DIAG asserted when stopped.

No `otpw`, `ot`, `s2g`, `ola`, `olb` or `uv_cp` appeared. `CS_ACTUAL=8/8`
throughout, `stealth=1` in StealthChop and `stealth=0` in SpreadCycle.
`stats` at the end: `wr_fail=0`, M0 `mism=9` (same retry-counter behaviour as
session 0).

### The Session-1 premise is wrong: `SGTHRS = 0` is not "StallGuard off"

The physical DIAG pin still pulses with `SGTHRS=0`, because the driver
compares `SG_RESULT` against `2 × SGTHRS = 0` and `SG_RESULT` does reach 0.
The floods during both `stop 0` ramps are the clearest instance; the 2 pulses
at the 18k→20k change and the 4 pulses at steady 1/8 are the same mechanism.
`updateMotionFlags()` itself treats `sgthrs == 0` as "off" for its own `STALL`
flag, so the console prints no `STALL`; but the GPIO ISR and the event path
still fire, and the plan's §7 instruction `sg 0 0 # StallGuard is still at the
old shadow` does not remove false DIAG. A real "off" needs the `TCOOLTHRS`
window (or SpreadCycle), not `SGTHRS=0`.

### Classification (no tach)

- 20 kstep/s is **not** a steady-state stall in either mode on this unloaded
  bench: TSTEP held 38 for 4 s, and StealthChop `SG_RESULT` stayed high
  (mean 347), which is a low-load indication, not a stall.
- The 20 kstep/s DIAGs in StealthChop occur **during the 18→20k transition**,
  not at the steady point. The same speed in SpreadCycle is quiet, and no
  fault bits are set. This is a StallGuard false/instability event, not a
  driver fault.
- The 128 DIAG pulses during the StealthChop `stop` ramp (11–15 kstep/s) are
  the same comparator firing on `SG_RESULT == 0`; they are not a fault.
- Without a tach, "true stall" cannot be excluded at the exact DIAG instants.
  The steady-state evidence above is the basis for the call.

## Gate table

| Gate | Question this session | Result | Envelope consequence | Not tested |
|------|-----------------------|--------|----------------------|------------|
| G1 | True stall or DIAG with the shaft turning? | **Withdrawn / not closed.** Operator saw the rotor stall in the later runs. `SG_RESULT` is high while stalled; TSTEP is only the step source. The earlier "20k fine" call is invalid. `SGTHRS=0` does not disable DIAG (that finding stands) | Do not use these counts as `f_stall`; all speed points need an independent motion reference | tach/shaft-mark confirmation, supply, temperature |
| G2 | Does SpreadCycle or a low hybrid move `f_stall`? | not this session (SP reached 20k clean; `f_stall` not measured) | — | hybrid, f_stall |
| G3 | Nominal tier inside `0.7 × f_stall` and `0.5 × a_stall`? | not this session | — | acceleration; bench command is 0.079 m/s² |
| G4 | Homing window: current, speed, `SGTHRS`, repeatability | not this session | — | — |
| G5 | `SG_RESULT` versus the predicted tension | not this session | — | — |
| G6 | Measured UART mask, and the period that fits | not this session | — | — |

## Files

- `doc/bench/tmc2209/2026-11-10-M0-SC-SP-300-16-01/note.md`
- `doc/bench/tmc2209/2026-11-10-M0-SC-SP-300-16-01/serial.log` (unedited)
- `doc/bench/tmc2209/2026-11-10-M0-SC-SP-300-16-01/samples.csv` (monitor rows;
  `tach_rpm`, `drv_status`, supply and temperatures blank because no
  instruments)
- `events.csv` — not produced
- Stop-source timings: **not measured** (no scope). The `stop` deceleration was
  not quiet in StealthChop.
