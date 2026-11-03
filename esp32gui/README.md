# esp32gui — installation master and operator UI

Waveshare **ESP32-S3-Touch-LCD-7** (800 × 480, GT911 touch, CH422G expander)
running LVGL 9.2. The panel is the CAN master and the operator UI. It does not
author or bake patterns; that stays on the PC. Design decisions are in
[`../doc/esp32gui_whitepaper.md`](../doc/esp32gui_whitepaper.md).

**No WiFi, no OTA, no Telegram and no `.private` secrets.** This project does
not include `template.h`, `tpl_wifi`, `wifi_secrets.*`, `private_bot.h` or
`private_sha.h`. The service path is USB/UART flashing through the CH9102.

## Current stage

| Done | Deferred |
|------|----------|
| PlatformIO board bring-up (RGB panel, GT911, CH422G, `USB_SEL`) | HBP2 SD playback, PSRAM ring, block CRC/SHA |
| Local PSRAM log ring, boot/watchpoint watchdog, CAN link watchdog | Real TWAI transport at 1 Mbit/s (code present, compile-guarded) |
| CAN transport boundary with a mock backend | Node discovery on a real bus |
| Discovery/ASSIGN registry, grid map, SPIFFS config | Full HBP2 show scheduler |
| LVGL screens: Show, Grid, Nodes, Motor, Config, Log | Panel brightness PWM (on/off today) |
| Host harness renders every screen to PNG | |

The bench has no CAN wiring yet, so `platformio.ini` defines `SS_CAN_MOCK=1`.
`src/can_transport_mock.cpp` then accepts frames and lets the UI be exercised.
When the TJA1051 and the 1 Mbit/s bus exist, remove `-D SS_CAN_MOCK=1`;
`src/can_transport_twai.cpp` takes over and switches CH422G IO5 to the
transceiver.

## Build

Firmware (first build downloads LVGL and the ESP-IDF component set):

```sh
cd esp32gui
pio run -e esp32usb
# flashing is a separate, explicit step:
# pio run -e esp32usb --target upload
```

Host preview, at 800 × 480, writing one PNG per screen:

```sh
cmake -S esp32gui/host -B esp32gui/host/build
cmake --build esp32gui/host/build
./esp32gui/host/build/esp32gui_host esp32gui/host/png
```

The harness builds the same `ui/ui.cpp` as the firmware. Its LVGL source tree
defaults to the one already downloaded for the house panel; override it with
`-DLVGL_ROOT_DIR=/path/to/lvgl`. The checked-in PNGs in `host/png/` are the
review artifacts for screen changes.

## Operator flow

1. **Nodes** — the panel sends `ASK` (type 11) every 5 s. Each unidentified
   board answers `DISC` (type 8) with its 64-bit QSPI flash ID. The master
   answers `ASSIGN` (type 10) with the board ID. The list shows the flash ID,
   board ID, online state and how many positions each board owns.
2. **Motor** — select a board, select axis 0–3, then `HOME`, `UP`, `DOWN` or
   `STOP`. `UP`/`DOWN` jog 10 mm at the configured test speed. Assign the
   selected axis to a grid position with `ASSIGN`; `UNASSIGN` clears it.
3. **Grid** — one cell per position. Green = assigned, red = assigned board has
   a fault, dark = free. Row 0 is the back row, column 0 is the left column.
4. **Config** — grid size (10 × 10 or 10 × 12), test-run drop/speed/gap,
   serpentine, and `SAVE TO SPIFFS`. The mapping and
   `(flash_id → board_id)` map persist in `/config.bin`.
5. **Show** — discovered boards, online boards, assigned positions, remaining
   assigns, and `START TEST RUN`. The test run requires every grid position to
   be assigned. It walks rows **back to front**; within a row it goes
   left→right, then right→left on the next row (serpentine). Each sphere drops
   `test_drop_mm` (default 100 mm) slowly and the next one starts after the
   previous move plus the configured gap.

The test run is a commissioning cascade, **not** HBP2 show playback. It uses
the same commissioning `COMMAND` frames as the Motor screen.

## Commissioning frame layout (proposed)

These payloads are not in the motion streaming baseline. They are the
commissioning extension in the whitepaper §9 and must be implemented in the
Fly-Micro4 firmware in lockstep.

```
HOME (type 5), field = board 0–29:
  [0] axis mask, bit0 = Drive0 … bit3 = Drive3
  [1..7] zero

COMMAND (type 4), field = board 0–29 or 127:
  [0] op: 1 = UP, 2 = DOWN, 3 = STOP
  [1] axis mask
  [2..3] u16 distance_mm, little-endian
  [4..5] u16 speed_mm_s, little-endian
  [6] sequence
  [7] zero
```

`ASSIGN` (type 10) carries the 64-bit flash ID and the board ID in the field.
Grid positions are host-side only; the board never learns one.

## Files

```
esp32gui/
├── platformio.ini          # pioarduino, 16 MB, qio_opi, LVGL 9.2, no WiFi
├── partitions_16m.csv      # OTA slots + SPIFFS
├── include/
│   ├── board_ws7.h         # CH422G, RGB panel, GT911, USB_SEL
│   ├── app_log.h           # PSRAM ring log (no WebSocket/HTTP)
│   ├── app_watchdog.h      # boot count, WATCH(), task/CAN liveness
│   ├── can_transport.h     # the only CAN boundary
│   ├── can_monitor.h       # SPSC RX ring and per-ID statistics
│   ├── commission.h        # discovery, ASSIGN, grid map, test run
│   ├── config_store.h      # SPIFFS /config.bin
│   └── lv_conf.h           # LVGL 9.2 build config
├── src/
│   ├── main.cpp            # setup/loop, snapshot builder, actions
│   ├── board_ws7.cpp       # Waveshare 7" bring-up
│   ├── app_log.cpp
│   ├── app_watchdog.cpp
│   ├── can_transport_mock.cpp
│   ├── can_transport_twai.cpp  # compiled when SS_CAN_MOCK is absent
│   ├── can_monitor.cpp
│   ├── commission.cpp
│   ├── config_store.cpp
│   └── ui_build.cpp        # compiles ui/ui.cpp into the firmware
├── ui/                     # LVGL screens; no Arduino/ESP-IDF includes
│   ├── ui_model.h
│   ├── ui.h
│   └── ui.cpp
└── host/                   # Mac CMake harness, renders PNGs
```

`lib/motion_core/` is shared from the repository root through
`lib_extra_dirs = ../lib`. It is never copied here.
