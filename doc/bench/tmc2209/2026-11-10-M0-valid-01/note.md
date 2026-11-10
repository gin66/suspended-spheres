# TMC2209 firmware validation — commit `60acd5a`

> Flashed by the operator. Purpose: check the step-2 firmware changes on
> hardware. Not a characterization session; no new `f_stall` claim.

## Build under test

- Commit `60acd5a` — SpreadCycle motion default, armed stall, IFCNT/TSTEP fixes.
- `pio run` success; host tests 115 pass (before flash).

## Non-motion checks

| Check | Result |
|-------|--------|
| Boot/`reinit` mode | `GCONF=0x1C4`, log says "SpreadCycle written" — pass |
| `mode 0 stealth` | `ok`; `status` shows `stealth=1` — pass |
| `mode 0 spread` | `ok`; `status` shows `stealth=0` — pass |
| `sg 0 off` | `ok`; `status` shows `tcoolthrs=0x0` — pass |
| Standstill `TSTEP` | `1048575` (0xFFFFF), not 65535 — pass (20-bit store) |
| IFCNT semantics | `mism=0` throughout; a genuine failed write raised `wr_fail`, not `mism` — pass |

One transient UART write-verification failure occurred on M0 during a
`reinit`: after three attempts `wr_fail=1` and M0 stayed `ready=0` (outputs
disabled), which is the intended fail-safe. A second `reinit` brought all four
to `ready=0xF`; `mism` remained 0. This is the known bench UART reliability
limit, not a regression from the change.

## Motion smoke (operator-confirmed)

| point | TSTEP | mode | result |
|------:|------:|------|--------|
| 18.0 kstep/s | 42 | SpreadCycle (`stealth=0`) | in sync, no fault bits |
| 74.0 kstep/s | 10 | SpreadCycle (`stealth=0`) | in sync, no fault bits, `CURRENT_LIMITED` (PWM_SCALE 255) |

Board stopped and re-initialised after each point; axes left disabled.

## Files

- `serial.log` (kept on the bench host, not committed)
- `note.md` (this file)
