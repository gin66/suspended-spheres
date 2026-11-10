# TMC2209 Session 5 — UART cost (G6)

> **Status: measured.** One `SG_RESULT` read costs ~1.5–1.9 ms and one
> `refresh()` ~8.6–10.7 ms on this bench, including retries. A 5 ms sampler for
> four axes is not available. Keep the DIAG edge plus the 1 Hz health read.

## Setup

Firmware `60acd5a` + uncommitted `timeuart` command. M0, 300 mA, 1/16,
SpreadCycle (the cost is mode-independent). `timeuart` times one
`readDriverReg(SG_RESULT)` and one `refresh()` around `micros()`, so the
interrupt-masked window and any retries are included.

## Results

| condition | sample | SG read min | mean | max | refresh min | mean | max |
|-----------|-------:|------------:|-----:|----:|------------:|-----:|----:|
| standstill | 50 | 1284 µs | 1529 µs | 10360 µs | 7922 µs | 8589 µs | 10013 µs |
| 18k moving | 20 | 1291 µs | 1886 µs | 12119 µs | 8039 µs | 10721 µs | 12549 µs |

All sampled reads succeeded (`ok=50/50`, `20/20`). The moving case adds the
PIO load and is ~10–25 % slower; same order.

## Why it is higher than the plan's estimate

The plan uses 0.6–0.7 ms for one read and ~4 ms for a refresh, i.e. one
attempt. On this board the mean is ~2× that because a read averages more than
one attempt (the bit-banged UART has a nonzero frame-failure rate; see
`stats`). `refresh()` is six transactions, so ~2× the single-read mean, as
expected. The 688 µs mask figure is a single attempt, not the delivered cost.

## Consequence for G6

- Four axes of one refresh = ~34–43 ms; a 5 ms four-axis sampler is impossible.
- One axis at the 20 ms `monitor` floor already spends ~43–54 % of the period
  with interrupts masked, stalling USB and CAN for most of it.
- Production default stays: **DIAG edge + the existing 1 Hz health read**.
- If a periodic read is ever added, the measured budget allows one axis per
  20 ms at most, or one full four-axis round-robin per ~50–100 ms. There is no
  measurement that supports 5 ms.
- CAN was active on the bus but no CAN frame was timed (no hook); the moving
  case already includes its CPU load. PIO underrun was not observable; the
  step train held (`tstep=42` at 18k during the timed refresh).

## Gate table

| Gate | Question this session | Result | Envelope consequence | Not tested |
|------|-----------------------|--------|----------------------|------------|
| G6 | Measured UART mask, and the period that fits | SG read 1.5–1.9 ms mean, refresh 8.6–10.7 ms mean; retries included | Keep DIAG-only; periodic at most 1 axis / 20 ms or ~50–100 ms round-robin | CAN frame timing |

## Files

- `serial.log` (kept on the bench host, not committed)
- `note.md` (this file)
