# TMC2209 bench record — index

This directory is the evidence trail for `doc/tmc2209_characterization.md`.
Each `run_id/` holds `note.md` plus the unedited `serial.log` (and
`samples.csv` where generated). Read the note, not just the log. The raw
`serial.log` files stay on the bench host and are intentionally **not
committed**; `note.md` and `samples.csv` are the committed record. Keep the
logs with the bench machine until the session is accepted.

Board: Fly-Micro4 as built, unmodified. Date: 2026-11-10. No tach, scope,
temperature or supply meter; motion calls below `f_stall` and stall calls were
operator-confirmed by the sound/shaft stopping.

## Runs

| run_id | What it established |
|--------|---------------------|
| `2026-11-10-identity-01` | Identity, no motion. IFCNT `mism` is a retry-counter artefact; M3 driver answers, motor absent. |
| `2026-11-10-M0-SC-SP-300-16-01` | Session 1 classification. `sg 0 0` does not silence DIAG; SG high while stalled; the "20k is fine" claim withdrawn after the operator saw the stall. |
| `2026-11-10-M0-SC-300-16-02`, `-03` | Continuous staircases; invalid (rotor stalled, points above the loss meaningless). |
| `2026-11-10-M0-SC-300-16-04` | Coarse StealthChop climb: clean to 18.25k, DIAG burst at 18.5k. |
| `2026-11-10-M0-SC-300-16-05` | 18.3k fails; confirms the limit just above 18.25k. |
| `2026-11-10-M0-SC-300-16-06` | 18.25k marginal (one fail), 18.275k not reached. |
| `2026-11-10-M0-SC-300-16-07` | **StealthChop `f_stall` = 18.0 kstep/s, 3/3 clean.** |
| `2026-11-10-M0-SC-300-16-08` | **StallGuard window:** stable above ~750 step/s; homing-band values; no DIAG in free run. |
| `2026-11-10-M0-SP-300-16-01` | **SpreadCycle `f_stall` ≈74 kstep/s** (76k lost); supply 210 mA at 20 V. |
| `2026-11-10-M0-SP-300-16-02` | 74k repeat with 12 s dwell. |
| `2026-11-10-M2-SP-300-16-01` | **M2 SpreadCycle ≥94 kstep/s**, stopped for mechanical noise. |
| `2026-11-10-M2-SC-300-16-01` | Invalid for StealthChop: harness wrote `0x1C4`, actually SpreadCycle. |
| `2026-11-10-M2-SC-300-16-02` | **M2 StealthChop `f_stall` ≈21 kstep/s.** |

## Gates

| Gate | State |
|------|-------|
| G1 | closed — true stall at ~18.5k; DIAG burst; no fault; stop-flood is re-capture |
| G2 | closed for M0/M2 in StealthChop vs SpreadCycle; **hybrid open**; noise number not measured |
| G3 | partial — speed passes for 40 mm in SpreadCycle; 20/10 mm optional tiers outside margin; **acceleration unmeasured** |
| G4 | partial — free-run window found; **hard-stop repeatability pending** (rig not built) |
| G5 | open — needs drum + 50 g load |
| G6 | open — needs scope/microsecond timing |

## Open items

Homing rig (Session 4), M2 StallGuard window, 500/800 mA, hybrid crossover,
interpolation on/off, acceleration (needs a firmware command), thermal repeat,
three-axis run on the installation supply.

Queued: CHOPCONF/PWMCONF tuning matrix — see
`doc/tmc2209_characterization.md` §8 Phase 4a. The measured SpreadCycle numbers
used the StealthChop bring-up `CHOPCONF`, so they are an untuned lower bound.
