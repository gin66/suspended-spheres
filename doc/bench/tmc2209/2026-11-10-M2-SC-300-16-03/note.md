# TMC2209 Session 3 — StallGuard window, M2, 300 mA

> **Status: stable from ~1000 step/s; weaker than M0.** At 500 step/s the value
> collapses to 0; at 750 it still has zeros. From 1000 up it is monotonic and
> zero-free. The pancake's SG level is roughly half the full-size motor's, so a
> shared `SGTHRS` will not work; calibrate per axis.

## Setup

M2 US-17HS4023S, unloaded, 300 mA, 1/16, interpolation on, StealthChop
(`GCONF=0x1C0`, read back and `stealth=1`), `SGTHRS=0`, `TCOOLTHRS=0xFFFFF`,
`TPWMTHRS=0`, acceleration 2000 step/s². Runs on firmware `60acd5a`.
Free run; no hard stop. Monitor 20 ms.

## Free-run SG sweep

| f_step | SG mean | SG sd | min | max | zeros | n | diag |
|-------:|--------:|------:|----:|----:|------:|--:|-----:|
| 500 | 18.4 | 19.6 | 0 | 58 | 38 | 120 | 0 |
| 750 | 34.0 | 23.2 | 0 | 72 | 3 | 131 | 0 |
| 1000 | 48.3 | 23.7 | 4 | 84 | 0 | 120 | 0 |
| 1250 | 59.4 | 23.2 | 18 | 96 | 0 | 121 | 0 |
| 1500 | 71.0 | 23.3 | 30 | 108 | 0 | 130 | 0 |
| 1750 | 84.2 | 25.1 | 42 | 122 | 0 | 121 | 0 |
| 2000 | 92.2 | 25.1 | 56 | 134 | 0 | 130 | 0 |
| 2500 | 120.5 | 24.6 | 66 | 156 | 0 | 121 | 0 |
| 3000 | 141.1 | 24.2 | 90 | 184 | 0 | 130 | 0 |
| 3500 | 158.6 | 26.0 | 112 | 200 | 0 | 120 | 0 |
| 4000 | 172.8 | 24.7 | 130 | 216 | 0 | 131 | 0 |
| 5000 | 203.3 | 26.4 | 140 | 248 | 0 | 130 | 0 |
| 6000 | 229.5 | 26.5 | 172 | 270 | 0 | 131 | 0 |
| 7000 | 251.8 | 26.6 | 190 | 290 | 0 | 140 | 0 |
| 8000 | 269.9 | 30.4 | 214 | 312 | 0 | 131 | 0 |
| 10000 | 300.1 | 24.2 | 242 | 344 | 0 | 160 | 0 |
| 12000 | 322.0 | 23.6 | 264 | 368 | 0 | 162 | 0 |
| 14000 | 341.9 | 22.6 | 294 | 388 | 0 | 160 | 0 |
| 16000 | 358.1 | 23.0 | 304 | 400 | 0 | 162 | 0 |
| 17000 | 367.9 | 22.2 | 320 | 408 | 0 | 130 | 0 |

No DIAG in the sweep.

## Homing band

| drum | 40–50 mm/s | SG mean (M2) | SG mean (M0) |
|-----:|-----------:|-------------:|-------------:|
| 40 mm | 1.0–1.25k | 48–59 | 139–164 |
| 20 mm | 2.0–2.5k | 92–121 | 228–262 |
| 10 mm | 4.0–5.0k | 173–203 | 353–349 |

At 40 mm the pancake's homing signal is only ~50 counts with a similar spread;
a 20 or 10 mm spool gives a much better margin. Set `SGTHRS` per axis and per
drum from these baselines; do not reuse the M0 value.

## Gate table

| Gate | Question this session | Result | Envelope consequence | Not tested |
|------|-----------------------|--------|----------------------|------------|
| G4 | Homing window: current, speed, `SGTHRS`, repeatability | Free-run window for M2 found (stable ≥1000 step/s); threshold/repeatability still open | Per-axis `SGTHRS` required; prefer 20/10 mm for the pancake | hard-stop homing |

## Files

- `serial.log` (kept on the bench host, not committed)
- `note.md` (this file)
