# Fly-Micro4 — pin map and board reference

> Reference only. Source: the official Mellow Fly-Micro4 pin map and Klipper
> configuration (`Fly_Micro4_klipper_reference.cfg`) plus the schematic
> (`Fly_Micro4_schematic.pdf`). This is not our firmware configuration.
> Verify the exact board revision before using these pins in
> `rp2040/include/board_pins.h`.

---

## 1. Stepper drivers — board silkscreen names

| Signal | Drive0 | Drive1 | Drive2 | Drive3 |
|--------|:------:|:------:|:------:|:------:|
| STEP | GPIO6 | GPIO1 | GPIO26 | GPIO23 |
| DIR | GPIO3 | GPIO0 | GPIO25 | GPIO22 |
| EN (active low) | GPIO7 | GPIO2 | GPIO27 | GPIO24 |
| TMC UART | GPIO9, addr 0 | GPIO9, addr 1 | GPIO9, addr 2 | GPIO9, addr 3 |
| DIAG | GPIO13 | GPIO14 | GPIO15 | GPIO12 |

The board strap, the silkscreen and `pin.webp` give the identity mapping
**0, 1, 2, 3** for Drive0–Drive3. The shipped Klipper reference config happens
to address the Z socket (`gpio23/22/24`, labelled Drive3) as `uart_address: 2`
and the E0 socket (`gpio26/25/27`, labelled Drive2) as `uart_address: 3`; that
swaps Drive2/Drive3 relative to the board labels. Our firmware follows the
hardware strap (identity), not that config. Verify with the `read <axis>`
`IOIN`/`ENN` check after any board revision change.

**All four DIAG are MCU-routed** (GPIO13, GPIO14, GPIO15, GPIO12). The official
config uses Drive0/1 DIAG for X/Y sensorless homing and GPIO12 (Drive3 DIAG) as
the Z endstop input. Drive2 DIAG (GPIO15) is unused by the official config but
is available. For 4-axis StallGuard homing, all four are usable; **GPIO12 is
shared with the official Z endstop header**, so do not use it as both Drive3
DIAG and a separate endstop.

GPIO9 is one half-duplex wire shared by all four `PDN_UART` pins. GPIO9 is a
UART1 RX pin, not a TX pin, and both PIO blocks are allocated (can2040 and
four step generators), so the core's PIO `SoftwareSerial` cannot be used. The
node firmware bit-bangs both the request and the oversampled reply through
SIO; no UART peripheral or TMC library is used. See
`doc/motion_streaming_whitepaper.md` §5.6.1.

## 2. Other connectors

| Function | Pin / connector | Notes |
|----------|-----------------|-------|
| RGB LED | GPIO11, 5V, GND | WS2812/NeoPixel data on GPIO11; not in the official Klipper config. GPIO11 is also listed for an optional filament runout, so do not use both |
| Laser | GPIO10, VCC, GND | not used |
| CAN | CAN L/H | MCP2542 transceiver; can2040 uses PIO. Termination jumper J3: insert only on the last bus node; open on all others (no 120 Ω). R39/R58 stay fitted; no desoldering |
| USB | USB-C | USB serial or CAN firmware (`canbus_uuid`) |
| Fan0 | GPIO16 | |
| Fan1 | GPIO17 | |
| Fan2 | always-on VCC/GND | no GPIO |
| Bed heater | GPIO21 | |
| Heat0 | GPIO20 | |
| E0 thermistor | GPIO28 | external NTC input, 4.7 kΩ pull-up; no NTC fitted |
| Bed thermistor | GPIO29 | external NTC input, 4.7 kΩ pull-up; no NTC fitted |
| BLTouch | GPIO18 signal, GPIO19 servo, 5V/GND | not used |
| GPIO12–15 header | DIAG pins plus VCC/GND | exposes the four DIAG signals |

## 3. TMC2209 reference values (official printer config)

| Setting | Value | Note |
|---------|-------|------|
| Sense resistor | 0.110 Ω | Board value; use it for current calculations |
| Run current | 0.8 A | Printer default. Our design uses 0.25–0.3 A; do not copy |
| Microsteps | 32 | Printer default; our baseline is 1/16 with MicroPlyer |
| Interpolation | `False` | Klipper setting; our firmware may enable the TMC2209 `intpol` bit |
| `SGTHRS` | 80 | Calibrated at 0.8 A; our current/threshold differ |
| Homing current ratio | 0.7 | Official macro drops current during homing |
| Homing speed | 40 mm/s | Klipper's sensorless-homing recommendation |

## 4. Homing implications

- All four DIAG are available, so 4-axis StallGuard is feasible.
- The official `HOME_CURRENT_RATIO = 0.7` supports the low-current homing idea:
  reduce current for the upward move, then restore normal current.
- Our derived procedure starts lower than 0.7 × run current and escalates only
  on an immediate StallGuard trigger; calibrate `SGTHRS` at each current step.
- At 40 mm/s, a 2.0 m worst-case search takes 50 s; this is inside the 60 s
  homing budget but leaves little margin for retries. A 2.5 m search would take
  62.5 s and exceed the budget. Use 40–50 mm/s and verify StallGuard
  repeatability; the white paper's earlier 0.1 m/s figure is faster but may be
  too fast for reliable StallGuard.
- `SGTHRS = 80` is an 0.8 A printer value. Do not reuse it at 0.25–0.3 A;
  calibrate from `SG_RESULT` telemetry.
- GPIO12/Drive3 DIAG is the official Z endstop pin; if Drive3 uses StallGuard,
  do not fit a separate endstop on that header.

## 5. Local status indicator

The local status indicator is the on-board single-colour LED **D5 on GPIO8**,
active high through `R65` (1 kΩ). The blink scheme for `FR-MON-005`
(ready/OK, no board ID, homing, playing, holding, fault) is in
`../../doc/motion_streaming_whitepaper.md` §5.8.1. The schematic lists D1–D5;
the fitted population may vary by board revision.

The other on-board LEDs are not free status outputs:

- D1 is the `HEAT0` indicator (`GPIO20`).
- D2 is the `BED` indicator (`GPIO21`).
- D3 is the `FAN0` indicator (`GPIO16`).
- D4 is the `FAN1` indicator (`GPIO17`).

Each sits on the corresponding MOSFET gate net, so driving it also enables
that heater/fan output.

The RGB connector (GPIO11, 5V, GND) is a 3-pin header for an optional external
WS2812/NeoPixel; no RGB LED is fitted and the firmware does not drive it.
GPIO11 is also listed for an optional filament runout, so only one function
can be used.

## 6. Files

- `Fly-Micro4/Fly-Micro4-schematic.pdf` — board schematic.
- `Fly-Micro4/Fly-Micro4.step` — board mechanical model.
- `Fly_Micro4_klipper_reference.cfg` — official Mellow Klipper config, kept for
  pin/settings reference only.
- `../doc/Fly_Micro4_addressing.md` — node identity and CAN-ID allocation.
