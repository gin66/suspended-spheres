# Installation Controller — Waveshare 7" ESP32 Panel

> The show controller and the operator UI.
> Motion payloads, timing, and node playback stay in
> `motion_streaming_whitepaper.md`. Node identity and CAN-ID allocation stay
> in `Fly_Micro4_addressing.md`. This note names the panel, the process
> split, and the directory the sources will live in.

The reference implementation is the house panel at
`../ESP32_projects/ESP32/28_hauszentrale`. That project already runs LVGL on
this board, switches GPIO 19/20 between native USB and the CAN transceiver,
and renders the same UI sources on the Mac. Copy that board bring-up. Do
not copy the heater, power, or sensor application.

---

## 1. Decision

The installation master is a **Waveshare ESP32-S3-Touch-LCD-7**. It is both
the CAN controller for the show and the touch UI. The UI is **LVGL 9**.

| Role | Machine |
|------|---------|
| Pattern authoring and 3-D preview | PC (`motion_streaming_whitepaper.md` §6) |
| Show playback, commissioning, operator UI | This panel, sources in `esp32gui/` |
| Per-ball motion | 30 × Fly-Micro4 (`controller_analysis.md`) |

`motion_streaming_whitepaper.md` §6.6 already prefers an embedded master for
the installation and keeps the PC for authoring and bench work. This note
names that master. The panel joins the same classic CAN 2.0B bus as the
nodes, at **1 Mbit/s**. The 125 kbit/s rate in the house panel is that
project's heater bus.

---

## 2. What the panel does

- Plays a baked pattern by calling `lib/motion_core` and sending the frames
  that whitepaper defines: cubic motion, sync, and the ASK / DISC / ASSIGN
  commissioning exchange.
- Shows run / stop, the active pattern, grid health, and faults on the
  800 × 480 panel.
- Stops the show on the panel when a node or the bus reports a fault. The
  hardwired E-stop remains the power removal in `FR-MOT-011`.

The panel does not author patterns and does not draw the Three.js view.
Those stay on the PC.

---

## 3. Where the source lives

```
esp32gui/                  PlatformIO project, not created yet
├── platformio.ini         Waveshare panel flags; see §5
├── src/                   ESP32-only: RGB panel, GT911, CH422G, TWAI
├── ui/                    LVGL screens, no ESP-IDF includes
├── host/                  Mac build of ui/ against LVGL; writes PNGs
└── include/
```

`lib/motion_core/` stays the shared integer core. `esp32gui/platformio.ini`
points at it with `lib_extra_dirs = ../lib`, the same way `rp2040/` does.
Do not copy the core into `esp32gui/`.

`rp2040/` remains the Fly-Micro4 firmware. `esp32gui/` is the master.

The house panel's useful split is:

- `tpl_ws7` and `tpl_display` own the RGB panel, the GT911, and the CH422G.
- Application screens are plain LVGL and compile in `host_harness/` with a
  stub for the bus.
- The CAN task is separate from the LVGL task. A display flush does not run
  inside the TWAI receive path.

`esp32gui/host/` follows that harness: one CMake build, the same `ui/`
sources, PNG output, no board. Screen changes are reviewed from those PNGs
before a flash.

---

## 4. Board facts the firmware keeps

These are measured or fixed in `28_hauszentrale`, not re-derived here.
Pins come from Waveshare `waveshare_lcd_port.h` and `waveshare_twai_port.h`.

| Item | Value |
|------|-------|
| Module | ESP32-S3, 240 MHz, 16 MB flash, octal PSRAM (`qio_opi`) |
| Panel | ST7262, RGB565, 800 × 480, parallel RGB, 16 data pins |
| Orientation | Landscape, `TPL_WS7_ROTATION=1` (LVGL rotation 0) |
| Touch | GT911, I²C GPIO 8 / 9, shared with the CH422G |
| Expander | CH422G. IO1 touch reset, IO2 backlight, IO3 panel reset, IO4 SD CS, IO5 `USB_SEL` |
| CAN transceiver | TJA1051, high-speed, behind an FSUSB42UMX |
| TWAI pins | TX GPIO 20, RX GPIO 19 |
| Console | CH9102 on UART0, GPIO 43 / 44 |

`USB_SEL` steers GPIO 19/20:

| CH422G IO5 | GPIO 19/20 connected to |
|------------|-------------------------|
| LOW | Native USB on the USB-C connector |
| HIGH | TJA1051 CAN transceiver |

The CH9102 console is not on that mux. With the mechanical UART switch in
the ESP32 position, esptool and the serial log still work while CAN is
running. Native USB on GPIO 19/20 does not.

Consequences, already paid for in the house panel:

- `ARDUINO_USB_MODE=0`. GPIO 19/20 are the TWAI pins, not a USB-OTG CDC.
- `setup()` drives `USB_SEL` low before anything else, then raises it only
  when the config says CAN is on and `twai_start()` runs.
- The in-service update path is WiFi OTA (ArduinoOTA and an HTTP `/update`).
  USB flashing is the bench recovery path: UART switch to ESP32, hold BOOT,
  tap RESET.
- Diagnostics that must be read while the show runs are drawn on the panel.
  The UART0 log remains available; native USB does not, once CAN is on.
- The pixel clock that stayed stable on this panel is 12 MHz
  (`TPL_WS7_PCLK_HZ`), about 29 Hz scanout. The stock PlatformIO ESP32
  platform made the RGB/LVGL path several times slower; the house panel
  uses pioarduino `55.03.312-1` (Arduino 3.3.12 on ESP-IDF 5.5.5).

The installation bitrate is 1 Mbit/s, the rate in `controller_analysis.md`.
The TJA1051 is a high-speed transceiver and is specified for that rate. It
has not been measured yet on the Fly-Micro4 bus.

The bus has two 120 Ω ends, and neither is a Fly-Micro4. R39 and R58 are
desoldered from every Fly board (`controller_analysis.md`). C63 stays, and
J3 stays fitted. This panel needs no soldering for termination: the onboard
CAN jumper is set to 120 Ω. The other position, NC, disconnects that
resistor. The far end is a 120 Ω resistor soldered into a cable terminator.

The panel's CAN connector is CANH/CANL. The nodes already share one 24 V
star ground; the panel ground joins that same star.

---

## 5. Build flags worth keeping

Start `esp32gui/platformio.ini` from the house panel's `[common]` block,
then drop the heater libraries (`libsml`, Matter, the web chart stack).
Keep:

```
board = esp32-s3-devkitc-1
board_build.flash_size = 16MB
board_build.arduino.memory_type = qio_opi
-DARDUINO_USB_MODE=0
-DARDUINO_USB_CDC_ON_BOOT=0
-DIS_WAVESHARE_LCD7
-DTPL_WS7_ROTATION=1
-DTPL_WS7_PCLK_HZ=12000000
-DLV_CONF_PATH=include/lv_conf.h
lib_deps = lvgl/lvgl@~9.2.0
lib_extra_dirs = ../lib
```

LVGL is 9.2, matching the house panel, so `tpl_display` and the host
harness move across without an API rewrite.

---

## 6. Tasks on the panel

| Task | Owns | Does not own |
|------|------|----------------|
| `ui` | LVGL screens, touch, fault text | CAN sockets, step timing |
| `can` | TWAI at 1 Mbit/s, frame queues | Drawing |
| `show` | Pattern clock, `lib/motion_core`, sync and motion frames | LVGL objects |

The show task is the master scheduler in `motion_streaming_whitepaper.md`
§6.6: sample ticks at `1/Δt`, sync frames, buffering, and a safe stop. It
runs from a hardware timer or a pinned FreeRTOS task. LVGL's handler runs
on the other core and never calls `twai_transmit`.

---

## 7. Screens

Enough to operate the piece. The host harness renders each one to a PNG
before it is treated as done.

| Screen | Shows |
|--------|--------|
| Show | Pattern name, play / stop, elapsed time, bus state |
| Grid | One cell per ball, fault and following-error summary |
| Nodes | Discovery: unanswered boards, ASSIGN in progress, board ID and flash ID |
| Log | Faults and the last safe stop, stored on the panel |

Commissioning text uses the rules in `Fly_Micro4_addressing.md`. The panel
is the master that sends ASK and ASSIGN. It stores the host map
`(flash_id, axis) → position`. The nodes still do not know the grid
position.

---

## 8. What stays out of `esp32gui/`

- The Three.js simulator and the `.anim.json` authoring loop.
- Fly-Micro4 step generation, PIO, and TMC2209 UART. That is `rp2040/`.
- House-panel application code: heater curves, SML power, water model,
  Matter, Telegram.
- A second copy of the CAN ID map or the cubic frame layout.
