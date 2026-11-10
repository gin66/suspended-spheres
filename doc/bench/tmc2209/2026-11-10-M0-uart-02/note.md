# TMC2209 — baud-rate investigation (`s_sample_cycles`)

> **Conclusion: keep 250 kBaud** (`s_sample_cycles = clock/1e6` = 133 cycles at
> 133 MHz). It is the only high rate that is an exact integer on both clocks:
> 48 TMC clocks/bit (12 MHz / 250 k) and 133 CPU cycles/sample (4 samples/bit).
> A near-500 k baud is not reachable with the current 4×-sample capture loop,
> not because of the TMC.

## Datasheet facts

- Internal clock is factory-trimmed to **12 MHz**; max baud `fCLK/16` = 750 k;
  the datasheet recommends up to 500 k with the internal clock.
- The divider re-synchronises on every datagram and the slave replies at the
  master's baud. A fixed internal clock means a master bit time that is not an
  integer number of 12 MHz cycles is quantised by the slave.
- The measured reply is 0.95–0.98 of the request at 250 k, consistent with
  that quantisation.

## Why 250 kBaud is the robust point

At 133 MHz, 250 kBaud gives 4.000 µs/bit = **48 TMC clocks**, and the 4×
sampler uses **133 CPU cycles/sample** — both exact. Any other rate is
non-integer on at least one clock. The capture loop's worst case is ~85 CPU
cycles/sample; at `s_sample_cycles = 80` `bbprobe` showed the capture grid slip
by 15 µs over 251 µs (expected 236 µs), so the master itself is overrunning.
There is no integer 4× grid at 133 MHz that reaches 500 k (it would need 66.5
cycles/sample).

## Measurements (settled rate, `senddelay 0 2`, n = 100–200)

| cycles | baud | SG read min / mean | refresh mean | attempts/read | verdict |
|-------:|-----:|-------------------:|-------------:|--------------:|---------|
| **133** | **250 k** | **1257 / 1409 µs** | **8828 µs** | **1.18** | **robust** |
| 120 | 277 k | 1224 / 1340 | 8123 | 1.07 | clean |
| 110 | 302 k | 1211 / 1322 | 7506 | 1.02 | clean |
| 100 | 333 k | 1164 / 2080 | 7857 | 1.33 | 17 % failed |
| 90 | 369 k | 1059 / 1243 | 7093 | 1.00 | clean in short run |
| 85 | 391 k | 1109 / 1400 | 6953 | 1.01 | clean in short run |
| 80 | 416 k | — | — | — | grid slip, fails |

`timeuart` includes retries; the min tracks the shorter frame (1164 µs at
333 k vs 1257 at 250 k), which is why the earlier "mean increased" reading was
misleading. The mean is retry-dominated at bad rates.

## What would unlock a higher baud

Only a change to the sampler, not a constant: 2 samples/bit, dithering a
non-integer sample period, or a faster capture loop (inline tracker, no
per-sample function call). The TMC itself can go to 500 k (fCLK/20) and to
750 k (fCLK/16). The current 4× bit-banged loop caps the robust rate at about
85 cycles/sample (~390 k), and even that is close to the 80-cycle cliff.

90 and 110 cycles were clean in short runs but are not exact 12 MHz divisors
and sit near the loop limit; they are not adopted without a long soak.

## SENDDELAY

`SLAVECONF = 0x200` → `SENDDELAY = 2` bit times, written at bring-up and
re-forced with the verified `senddelay 0 2` (`ok`) before these tests. It is in
bit times, so it scales with the baud; there is nothing to change.

## Files

- `serial.log` (this directory and `../2026-11-10-M0-uart-03/` … `-06/`, kept on
  the bench host, not committed)
- `note.md` (this file)
