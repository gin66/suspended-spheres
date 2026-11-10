# TMC2209 Session 0 — identity, no motion

> **Status: STOPPED at the Session-0 IFCNT gate.** Do not start Session 1.
> The stop is per `doc/tmc2209_characterization.md` §7 ("`mism` is 0 and
> `wr_fail` is 0, or the session stops") and §3.7. See "Gate event" below.
> No shaft moved in this session.

## Identity header

| Field | Value |
|-------|-------|
| run_id | `2026-11-10-identity-01` |
| date, operator | 2026-11-10; agent driving USB serial, human operator present |
| git revision (repo) | `6817ab4-dirty`. The running build prints no version banner, so the revision actually running is **unverified** |
| board | Fly-Micro4, unmodified. "No solder, no added wire" **pending operator confirmation** |
| axis, UART address from `scan` | M0@0, M1@1, M2@2, M3@3 — **all four addresses reply** |
| motor MPN, full steps/rev | M0/M1: unknown, no label. §2 candidate 200 steps/rev. M2: Usongshine US-17HS4023S, 200 |
| rated current, R_phase, L_phase, holding torque | §2 candidate rows only, not a nameplate: M0/M1 1.5 A, 1.6 Ω, 3.8 mH, 0.42 Nm; M2 1.0 A, 3.5 Ω, 3.2 mH, 0.13 Nm |
| detent, if used in the stop decision | not used |
| drum diameter, thread, load mass | none (unloaded) |
| hang | `unloaded` |
| supply setpoint, measured OCV, current limit | **not measured — no meter on the bench.** Not filled; operator to supply |
| UART bit µs, idle bits, SENDDELAY | 4 µs/bit (250000 Baud, from init line); idle bits not read; SENDDELAY=2 |
| GCONF, CHOPCONF, PWMCONF | 0x1C0 / 0x14020054 / 0xC10D0024 (M0–M2, read-back matched) |
| COOLCONF | 0x42 not read; firmware writes none; SEMIN reset = 0 |
| SGTHRS, TCOOLTHRS, TPWMTHRS | 40 / 0xFFFFF / 0 |
| ambient °C | **not measured — no meter** |
| board photo / "no solder, no added wire" | **pending operator confirmation** |

## Predicted load

`unloaded`, torque = 0.

## Safety limits (written before the first ramp)

Session 0 has no motion. No temperature, supply-current or tach limit can be
armed because the bench has no meter, scope or tach. This is recorded as a
limitation, not as a limit that fired.

## Commands typed

```text
quiet on
stats 0 / stats 1 / stats 2 / stats 3
reinit
stats 0 / stats 1 / stats 2 / stats 3
scan
read 0 / read 1 / read 2
diag
enable 0 1
status 0
enable 0 0
diag
raw 2 0x6C
raw 2 0x00
stats 0
```

## Events

1. **IFCNT gate event (stop).** Baseline `mism` was M0=6, M1=2, M2=3, M3=2
   (cumulative since boot), `wr_fail=0` everywhere. `reinit` then wrote and
   verified the bring-up set. After it, M0 `mism` went 6 → 7 (+1); M1/M2/M3
   did not change. Per §7/§3.7 a new mismatch means discard-and-stop, so the
   session stopped before any motion.
2. **Diagnosis of that +1 (does not overrule the stop).** M0 over the
   `reinit`: `wr` +9 attempts, `wr_ok` +8 verified writes, IFCNT +8. The one
   extra attempt was a dropped TX frame recovered by the retry; all eight
   registers verified and the read-back matched. `writes_since_check` counts
   **attempts** (`writeAddressReg`), while IFCNT advances only for frames that
   land, so a recovered retry inflates the pending count and trips `mism`.
   The same reinit produced M1/M2/M3 `wr` +11 vs `wr_ok` +8 with **no** new
   `mism`, so the counter is timing-sensitive. Treat `mism` as "a frame was
   dropped and retried", not necessarily "the configuration is wrong";
   however the plan's literal gate is not met.
3. `reinit`: `ready=0xF/0xF`, all four drivers at 0x1C0. `tmc2@2: DRV_STATUS
   read failed` during init (telemetry bit stays clear); the same register read
   normally immediately afterwards.
4. `read 2` printed `CHOPCONF read FAILED` once. Re-read with
   `raw 2 0x6C` → `0x14020054`, matching the value written.
5. `enable 0 1` → `ok`; `status 0` clean: `otpw=0 ot=0 s2g=0 ola=0 olb=0`,
   `stst=1 stealth=1`, `cs=8/8`, `sg_result=0/80`, `tstep=65535` (the printed
   low 16 bits of the standstill `0xFFFFF`, as §3.6 predicts), `diag=0`.
   Axis returned to disabled (`enable 0 0` → `ok`).
6. **M3.** The plan says "confirm M3 is absent", but the driver at address 3
   replies, reports `ready`, and holds GCONF 0x1C0. Motor presence cannot be
   determined over UART. M3 stayed disabled; no motor assumption is made.
7. Unsaved preliminary observation (excluded from the record because it is not
   in `serial.log`): at first contact, before the saved capture, `scan` read
   GCONF=0x101 (reset default) on all four drivers. If it is real, the boot
   bring-up had not applied. It was not reproduced and must not be used.

No `otpw`, `ot`, `short`, `open load` or `uv_cp` was seen.

## Gate table

| Gate | Question this session | Result | Envelope consequence | Not tested |
|------|-----------------------|--------|----------------------|------------|
| G1 | True stall or DIAG with the shaft turning? | not this session | — | — |
| G2 | Does SpreadCycle or a low hybrid move `f_stall`? | not this session | — | — |
| G3 | Nominal tier inside `0.7 × f_stall` and `0.5 × a_stall`? | not this session | — | — |
| G4 | Homing window: current, speed, `SGTHRS`, repeatability | not this session | — | — |
| G5 | `SG_RESULT` versus the predicted tension | not this session | — | — |
| G6 | Measured UART mask, and the period that fits | not this session | — | — |

## Files

- `doc/bench/tmc2209/2026-11-10-identity-01/note.md`
- `doc/bench/tmc2209/2026-11-10-identity-01/serial.log` (unedited USB session,
  including one accidental `unknown command: daytime` line)
- `samples.csv` — not produced (session 1 artifact)
- `events.csv` — not produced (no supply/temperature/tach instruments)
