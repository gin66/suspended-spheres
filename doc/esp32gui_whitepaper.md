# Installation Controller — Waveshare 7" ESP32 Panel

> The show controller and the operator UI.
> Motion payloads, timing, and node playback stay in
> `motion_streaming_whitepaper.md`. Node identity and CAN-ID allocation stay
> in `Fly_Micro4_addressing.md`. This note names the panel, the process
> split, and the directory the sources live in.

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

- Plays an HBP2 playback file from microSD: precomputed cubic frames, sync,
  and the ASK / DISC / ASSIGN commissioning exchange. The panel does not
  bake or fit motion.
- Shows run / stop, the active pattern, grid health, and faults on the
  800 × 480 panel.
- Stops the show on the panel when a node or the bus reports a fault. The
  hardwired E-stop remains the power removal in `FR-MOT-011`.

The panel does not author patterns and does not draw the Three.js view.
Those stay on the PC.

---

## 2.1 Storage and SD-card playback

Authoring and baking stay on the PC. The panel reads one **HBP2** file from
microSD (CH422G IO4 is the card-select line). The file is the single playback
format:

```
HBP2 header      # grid, cadence, window, codec, calibration, sha256
block table      # offsets, sizes, CRC32 per block
frames           # 8-byte cubic wire frames, window-major
```

- The body is already the wire stream (`p0:u16, v0:s16, a0:s16, j:s16`), one
  frame per motor per `N·Δt`. No trajectory fitting or pattern bake runs on
  the panel.
- **Playback does not depend on continuous SD streaming.** The panel reads
  whole HBP2 blocks into a PSRAM chunk cache ahead of the playback window;
  the `show` task consumes only PSRAM. A low-priority refill task replaces
  consumed chunks from SD. If the file fits in the cache, the whole body is
  loaded before start and SD is idle during the show.
- One second of frames is about 32 KB at `N=3`, `Δt=10 ms`. The 4 MiB cache
  target covers about 131 s of playback; the exact internal-RAM, flash and
  PSRAM budget is in `memory_map.md`.
- Missing SD, a corrupt block, a short read, a cache underrun, or a SHA-256
  mismatch stops the show with a safe hold and a log entry. It never
  continues with stale or partial frames.
- `codec` is `none` in the first firmware. Compression, if added, is a
  per-block codec: an 8–16 KB block decompresses independently, so one bad
  block does not take down the file.
- The panel verifies the header and block table at load time and shows the
  pattern name, duration and codec on the Show screen.

The PC still watches and bakes `.anim.json`; it only writes HBP2 when the
report is clean.

---

## 3. Where the source lives

```
esp32gui/                  PlatformIO project
├── platformio.ini         Waveshare panel flags; see §5
├── src/                   ESP32-only: RGB panel, GT911, CH422G, TWAI
├── ui/                    LVGL screens, no ESP-IDF includes
├── host/                  Mac build of ui/ against LVGL; writes PNGs
└── include/
```

The first implemented stage is the board bring-up, the commissioning model
and the UI. The CAN transport boundary selects a mock at build time because
the bench has no bus wiring yet; the TWAI implementation is present and
compile-guarded (`SS_CAN_MOCK`). No WiFi, OTA, Telegram or `.private` secret
is compiled into this project.

The local `app_log`, `app_watchdog` and `can_monitor` adapt the useful
concepts from `../ESP32_projects/ESP32/prj_template` (`tpl_debug_log`,
`tpl_system` `WATCH()`, `tpl_net_watchdog`) and
`28_hauszentrale/src/can_monitor.cpp`. They are local modules; no template
header and no secret is linked.

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
| Module | ESP32-S3, 240 MHz, 16 MB flash, 8 MB octal PSRAM (`qio_opi`) |
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
- **No WiFi and no secrets in `esp32gui/`.** `template.h`, `tpl_wifi`,
  `wifi_secrets.*`, `private_bot.h` and `private_sha.h` are not used. The
  service path is USB/UART flashing through the CH9102 (UART switch to ESP32,
  hold BOOT, tap RESET). A future OTA path must keep credentials outside the
  repository; none are present today.
- Diagnostics that must be read while the show runs are drawn on the panel.
  The UART0 log remains available; native USB does not, once CAN is on.
- The pixel clock that stayed stable on this panel is 12 MHz
  (`TPL_WS7_PCLK_HZ`), about 29 Hz scanout. The stock PlatformIO ESP32
  platform made the RGB/LVGL path several times slower; the house panel
  uses pioarduino `55.03.312-1` (Arduino 3.3.12 on ESP-IDF 5.5.5).

The installation bitrate is 1 Mbit/s, the rate in `controller_analysis.md`.
The TJA1051 is a high-speed transceiver and is specified for that rate. It
has not been measured yet on the Fly-Micro4 bus.

The bus has two 120 Ω ends: this panel and the last Fly-Micro4. This panel needs
no soldering for termination — its onboard CAN jumper is set to 120 Ω; the NC
position disconnects that resistor. The far end is the last Fly-Micro4 with
termination jumper J3 inserted. Every other Fly-Micro4 leaves J3 open and
presents no 120 Ω, and R39/R58 stay fitted — no desoldering
(`controller_analysis.md`).

The panel's CAN connector is CANH/CANL. The nodes already share one 24 V
star ground; the panel ground joins that same star.

---

## 5. Build flags worth keeping

`esp32gui/platformio.ini` takes the house panel's pioarduino platform and
drops every heater, Matter, WiFi and web dependency. It keeps:

```
platform = pioarduino 55.03.312-1
board = esp32-s3-devkitc-1
board_build.flash_size = 16MB
board_build.arduino.memory_type = qio_opi
board_build.partitions = partitions_16m.csv
-DARDUINO_USB_MODE=0
-DARDUINO_USB_CDC_ON_BOOT=0
-DIS_WAVESHARE_LCD7
-DBOARD_WS7_ROTATION=1
-DBOARD_WS7_PCLK_HZ=12000000
-DLV_CONF_PATH=include/lv_conf.h
-DSS_CAN_MOCK=1            ; remove when the TJA1051 bus is wired
lib_deps = lvgl/lvgl@~9.2.0
lib_extra_dirs = ../lib
```

`build_src_filter` is ignored by the ESP-IDF framework on pioarduino, so
`src/ui_build.cpp` includes `ui/ui.cpp` into the firmware build. The host
harness compiles `ui/ui.cpp` directly. LVGL is 9.2, matching the house
panel.

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

In the first stage the UI and commissioning service run in the Arduino
main loop, and the transport owns its own RX task (TWAI build) or delivers
synchronously (mock build). The separate `show` task arrives with HBP2
playback. The SPIFFS config store is mutated only from the main loop; the
CAN callback only updates the RAM registry and queues `ASSIGN`.

---

## 7. Screens

Enough to operate and commission the piece. Navigation is a right-hand
88 px rail with an icon and short label per screen; the content area is
712 × 480. The rail marks the active screen with an accent rule at its left
edge, following the house-panel layout. The host harness renders each screen
to a PNG before it is treated as done; the PNGs live in
`esp32gui/host/png/`.

| Screen | Shows |
|--------|--------|
| Show | Pattern name, play / stop, elapsed time, bus state, discovered/assigned/online counts, unassigned steppers, test-run state |
| Animations | HBP2 files on the card: name, size, duration, validity; refresh, select and load |
| Grid | One cell per ball: assigned, free, fault; click a cell to select its board |
| Nodes | Discovered boards by 6-byte UUID, board ID, online state, assigned positions |
| Motor | Selected board and axis (assigned axes are green); HOME, UP, DOWN, STOP; tap a grid cell to choose the position, then assign/unassign |
| Config | Grid size, test-run drop/speed/gap, serpentine, save/reset/clear, SPIFFS state |
| Log | Recent PSRAM log ring, boot count, last watchpoint, last safe hold |
| CAN | Received-frame scope by identifier (raw ID, type, count, last data), TX sent/rejected, RX drops, bus-off state; clear |
| Upgrade | Per-board software version, `QUERY ALL` (type-12 request), selected board, upgrade status and progress |
| Settings | WiFi/SSID/IP/RSSI and CAN state, firmware, boot, uptime, position capacity, service path |

The Animations screen is the file-selection UI. The SD enumeration and HBP2
load/playback backend are not implemented yet, so the firmware reports that
`REFRESH` and `LOAD SELECTED` are pending; the host harness fills the list with
sample files. The screen reads only from the snapshot, so the backend can fill
it without changing the LVGL code.

The CAN scope keeps 256 identifiers: a running show already uses 120
per-motor type-0 IDs plus 30 status IDs and the sync ID, and commissioning and
version traffic need headroom on top. Only received frames are counted.

`QUERY ALL` sends `VERSION_REQ` (type 12) to field 127. Boards answer with
`VERSION_RSP` (type 13) and the panel keeps the last version per board; see
`motion_streaming_whitepaper.md` §4.5. In-place update is
`katapult_whitepaper.md`: one board at a time, over Katapult. The CAN
transfer is not implemented on the Fly-Micro4 yet, so `UPGRADE SELECTED`
currently reports that the transfer is pending and makes no bus transaction.

This build has no WiFi stack. The Settings network fields are fed from the
snapshot and report `not compiled` until a net-status source exists; the
service path remains the CH9102 UART. There is no language switch: labels are
short and the log is English-only.

Commissioning text uses the rules in `Fly_Micro4_addressing.md`. The panel
is the master that sends ASK and ASSIGN. It stores the host map
`(uuid, axis) → position`. The nodes still do not know the grid
position. The checked-in image still persists an 8-byte id; the move to the
6-byte UUID is `katapult_whitepaper.md` §9.

The screen flows are in §9.

---

## 8. What stays out of `esp32gui/`

- The Three.js simulator and the `.anim.json` authoring loop.
- Fly-Micro4 step generation, PIO, and TMC2209 UART. That is `rp2040/`.
- House-panel application code: heater curves, SML power, water model,
  Matter, Telegram.
- A second copy of the CAN ID map or the cubic frame layout.
- WiFi, OTA-over-WiFi, Telegram and every `.private` secret. This panel is
  a wired CAN master; the CH9102 UART is the service path.

---

## 9. Commissioning and configuration

### 9.1 Workflow

1. **Discover.** The master sends `ASK` (type 11, field 0) every 5 s. A board
   without a board ID answers `DISC` (type 8, field 0) with its 6-byte UUID
   and two zero bytes. The master sends `ASSIGN` (type 10) with the board ID
   in the field and that same payload. A known UUID keeps the board ID it
   had in the host map. This is `Fly_Micro4_addressing.md` §3.
2. **Assign steppers.** The Nodes list shows every discovered board, its
   board ID and how many positions it owns. The Motor screen selects one
   board and one local axis (0–3) and assigns it to a grid position. The map
   is one-to-one: assigning a position removes the position's previous owner
   and any previous position of the same `(uuid, axis)`.
3. **Jog and home.** The Motor screen sends `HOME` (type 5) or `COMMAND`
   (type 4) with a 10 mm jog at the configured test speed. These are
   commissioning moves, not playback. The node does not use the cubic
   playback evaluator for them.
4. **Test the cascade.** With every position assigned, `START TEST RUN`
   walks rows from the back to the front. Inside a row the order is
   left→right, and the next row is right→left (serpentine). Each sphere drops
   `test_drop_mm` slowly; the next command follows after the move time plus
   `test_step_delay_ms`. The default is 100 mm at 20 mm/s with a 300 ms gap.
   The run refuses to start if a position has no live board.
5. **Save.** `SAVE TO SPIFFS` writes the grid size, the position map, the
   `(uuid → board_id)` map and the test parameters to `/config.bin`.

The grid is row-major with **row 0 = back** and **column 0 = left**. Position
`p = row × cols + column`. The Fly-Micro4 never learns `p`; it only sees the
motor field from `Fly_Micro4_addressing.md` §2 on motion frames and its
board ID on the housekeeping frames.

### 9.2 Persistent configuration

`/config.bin` on SPIFFS is a packed, CRC32-checked `ss_config_t`:

| Field | Meaning |
|-------|---------|
| `grid_cols`, `grid_rows` | 10 × 10 or 10 × 12 in normal use (`1..12` accepted) |
| `test_drop_mm`, `test_speed_mm_s`, `test_step_delay_ms`, `test_serpentine` | Commissioning cascade |
| `brightness` | 10–100 % (reserved; CH422G backlight is on/off today) |
| `positions[120]` | `assigned`, 6-byte `uuid`, `axis` per grid position |
| `boards[30]` | `uuid → board_id`, so a returning board keeps its motor fields |

A missing or invalid file writes defaults: 10 × 12, 100 mm, 20 mm/s, 300 ms,
serpentine on. The HBP2 playback file remains read-only on SD and is not part
of this store. A card failure or a corrupt block still stops the show, as in
§2.1.

### 9.3 Proposed commissioning frames

The motion streaming baseline in `motion_streaming_whitepaper.md` §4.5 does
not define the HOME/COMMAND payloads. This is the proposed extension. It must
be implemented in `rp2040/` before the Motor screen can drive real hardware;
until then the mock accepts the frames and the UI is testable.

```
HOME (type 5), field = board 0–29, exactly 8 bytes:
  [0]      axis mask, bit0 = Drive0 … bit3 = Drive3
  [1..7]   zero

COMMAND (type 4), field = board 0–29 or 127, exactly 8 bytes:
  [0]      op: 1 = UP, 2 = DOWN, 3 = STOP, 4 = ENTER_BOOTLOADER
  [1]      axis mask, bits 0–3
  [2..3]   u16 distance_mm, little-endian
  [4..5]   u16 speed_mm_s, little-endian
  [6]      sequence, wraps at 256
  [7]      zero
```

`ENTER_BOOTLOADER` is sent to one board ID, never to field 127. The distance
and speed bytes are zero. It does not jog: the speed and distance checks
apply to opcodes 1 and 2. The handler is `katapult_whitepaper.md` §6.2.

`STOP` with field 127 and mask `0x0F` is the operational stop-all. It is not
the hardwired E-stop of `FR-MOT-011`. A node must reject an axis mask outside
`0x0F`, a speed outside its safe range, or a move that leaves the 0–2000 mm
range; it must hold and report a fault rather than clamp.

If the Fly-Micro4 firmware chooses a different layout, change this section,
`esp32gui/src/commission.cpp` and the node together. Do not keep two layouts.
