# Panel UI design — operator-first navigation

Status: implemented; host PNGs and the layout audit reviewed 2026-11-17.
Complements `doc/esp32gui_whitepaper.md`; this document owns the screen
information architecture, the layout tokens and the WiFi interaction rules.

## 1. Why change: frequency before data source

The panel is not a directory of everything the firmware knows.  A destination
earns a place in the rail only when an operator needs it often enough to reach
for it without reading the screen first.

The two real jobs are:

- **Show operator** — start, stop and confirm the show is healthy.  Used
  every session.
- **Builder/commissioner** — lay out the grid, discover boards, align motors
  and run a test.  Used while the piece is built or serviced.

Two other tasks are real but rare:

- **Connect** — join a network for GitHub or browser upload.  Used during an
  update, not during a show.
- **Service** — node firmware, CAN scope, log, system information.  Used when
  something is wrong or when firmware changes.

The old flat rail mixed these frequencies.  Software upgrade sat beside
SHOW even though it is used a few times a year, and diagnostics were buried
under SETTINGS even though SETTINGS is an active control surface, not a
passive monitoring one.  Both make the common path slower and the rare path
ambiguous.

## 2. Navigation model

Four icon-only destinations fill the rail, ordered by frequency.  There are
no text labels; the icons are the navigation.

| Icon | Destination | Detail screens | Frequency |
| --- | --- | --- | --- |
| Play | **SHOW** | now playing, transport, playlist, library, health | every session |
| Sliders | **BUILD** hub | grid, boards, align motors, test/config | commissioning |
| WiFi | **CONNECT** | OFF/CONNECT/AP, scan, service, AP password/QR | occasional |
| Chip | **SERVICE** hub | firmware, CAN scope, log, system | rare / fault |

The four 72 × 72 targets are 20 px from the top and bottom, 8 px from the
sides, and distributed evenly over the remaining rail height.  The active
target is a rounded pill with an accent icon; there is no full-height tab and
no label.

`BUILD` and `SERVICE` open a card hub.  SHOW and CONNECT open directly.
Tapping the active hub icon again returns to its list.

Every detail screen carries a back chevron in its header.  It returns to the
same place as re-tapping the active rail icon, so a sub-screen is never a
dead end.

### 2.1 Hub cards

**BUILD**

1. **GRID & LAYOUT** — dimensions and position map.
2. **BOARDS** — discovery, board IDs and health.
3. **ALIGN MOTORS** — jog, select axis, assign/unassign positions.
4. **TEST & CONFIG** — drop, speed, gap, save/reset/clear map.

**SERVICE**

1. **FIRMWARE** — node image versions and fleet upgrade, the panel image
   (SD and GitHub), and the GitHub release check.
2. **CAN SCOPE** — live traffic by identifier.
3. **LOG** — boot, CAN and commissioning events.
4. **SYSTEM** — build/runtime, SD card state and display timeout.

SHOW is the performance console, not a commissioning dashboard.  It owns the
animation playlist and the transport: `PLAY`, `PAUSE`, `STOP`, `CYCLE`.  The
playlist lists only playable animations.  A file that fails HBP2 validation
is not a playlist entry; it is counted as invalid and kept out of the list.
Health moves to a single status line at the bottom.

Library is part of SHOW.  While an animation is playing the selected playlist
stays visible and the library slot becomes the large square **STOP** target,
so the choice is never hidden by the safety control.

Firmware is deliberately not a top-level destination: it is a rare operation
and it needs a network, so it lives in SERVICE where the reason and the image
can be shown together.  Diagnostics likewise live in the SERVICE hub, not in
SYSTEM: SYSTEM is a set of values and setup controls, while CAN scope and the
log are passive observations used while servicing.

## 3. Network interaction

### 3.1 Boot state

WiFi is **OFF after every reset**.  Stored credentials are remembered, but
they never start a radio by themselves.  The HTTP service is off after
every reset.

### 3.2 Explicit modes

The Connect screen has three explicit modes:

| Mode | Behaviour | Leaves mode when |
| --- | --- | --- |
| **OFF** | Radio off, HTTP service off | operator chooses CONNECT or AP |
| **CONNECT** | STA join to the selected or stored SSID | operator chooses OFF/AP, or join succeeds |
| **AP** | Setup AP + HTTP service for browser upload | operator chooses OFF/CONNECT, or AP window expires |

`FORGET` removes the stored SSID/password and returns to **OFF**; it never
opens the AP as a side effect.

### 3.3 CONNECT retry

A failed CONNECT does **not** fall back to AP.  The panel stays in
`CONNECTING`, reports the ESP32 disconnect reason (`wrong password`,
`SSID not found`, `beacon lost`, with the numeric code), and retries every
5 s.  The operator can
stop it with **OFF** or switch to **AP**.  The retry loop is intentionally
visible: a wrong password is a configuration error, not an invitation to
start an access point.

Credentials are written to NVS only after a successful association.
`CONNECT` may use a selected SSID and typed password, or the stored pair
when no new pair was entered.

### 3.4 What needs which network

| Task | Network |
| --- | --- |
| Fly node upgrade from SD | none |
| Panel image from SD | none |
| GitHub release check/install | CONNECT (internet) |
| Animation download from a server | CONNECT (internet) |
| Animation upload from a browser | AP |
| Fly/panel image upload from a browser | AP |

The UI never asks for WiFi before an SD-only task.  Actions that need a
network are disabled, with a short reason, until the required mode is
active.

## 4. Disabled and hidden elements

Prefer **disabled with an inline reason** over hiding.  Hidden controls
make the screen jump and remove the explanation.

- `SCAN` is disabled while scanning or connecting.
- `CONNECT` is disabled when there is neither a stored pair nor a selected
  SSID with a non-empty password.
- `SERVICE` is disabled unless CONNECTED or AP.
- `SET ADMIN` is available in AP mode; it is not needed for STA.
- GitHub check/install lives in **SERVICE → FIRMWARE**.  It is disabled
  unless CONNECTED; it has no meaning in AP mode.
- `PANEL FROM SD` in SERVICE → FIRMWARE is disabled when no panel image is
  present on SD.  Panel update has exactly one home: the firmware screen.
- Firmware actions are disabled while a fleet upgrade is active; `STOP` is
  disabled when idle.
- Show actions that would conflict with an active upgrade are disabled.
- AP-specific fields (SSID, key and QR) are hidden outside AP mode.  The
  scan list and STA password form are hidden in AP mode; the rest of the
  layout stays fixed.  Hide the whole NETWORKS panel, not only its rows.
- While playing, the library panel is hidden and replaced by the safety
  STOP.  The playlist remains visible.

Disabled controls use the muted text colour and a disabled button style.
The reason line remains visible so the operator knows what to do next.

## 5. Connect screen layout

800 × 480 panel, 712 × 452 content area after the status strip and the
88 px rail.

- Header: `NETWORK`, current state, IP.
- Mode panel: `OFF`, `CONNECT`, `AP`, active mode highlighted.
- In AP mode the full content area becomes three columns:
  - **left:** white card with the join QR and the AP SSID;
  - **middle:** AP password, `NEW KEY` (new random password), `SET KEY`
    (full-screen keypad), URL, service/forget/admin;
  - **right:** white card with the browser QR and `192.168.4.1`.
  The password is shown in plain text, and the join QR is rebuilt whenever
  the password changes.  The join QR uses
  `WIFI:T:WPA;S:<ssid>;P:<password>;H:false;;`; the browser QR uses
  `http://192.168.4.1`.
- Outside AP mode: scan list with `SCAN` on the left, selected SSID/password
  and `CONNECT` on the right, and service/forget/admin below.  There is no
  GitHub control here.  Each scan row shows the security mode and RSSI.
- The password keypad has an explicit **SHOW**/**HIDE** toggle.  The field is
  masked by default; showing it is an operator action, and the AP key modal
  stays visible by design.

Every panel has 12 px inner padding.  Controls stop at the inner edge; do
not place a 32 px button at `y` such that `y + height` exceeds the panel
content height.  This was the cause of the previous Settings overlap.

## 6. Workflow examples

### First setup

1. **BUILD → BOARDS**, discover and assign board IDs.
2. **BUILD → GRID & LAYOUT**, set dimensions.
3. **BUILD → ALIGN MOTORS**, assign each axis to a position.
4. **BUILD → TEST & CONFIG**, run a short drop/speed test, save.
5. **SHOW**, select an animation from the playlist and press `PLAY`.

No WiFi mode is entered.

### GitHub panel update

1. **CONNECT**: select SSID, enter password, `CONNECT`; wait for `CONNECTED`.
2. **SERVICE → FIRMWARE**: `CHECK`.
3. If `newer`, `INSTALL` from the same row.  A failed join retries and stays
   in CONNECT; GitHub controls are disabled while disconnected.
4. If the panel is already current but the Fly node image is missing or
   older, the status reads `node <version>` and `INSTALL` stores only the node
   bundle; the panel does not reboot.

### Browser upload without a public network

1. **CONNECT → AP**.
2. Scan the join QR with the phone camera; it joins the panel AP.
3. Open `http://192.168.4.1`, authenticate with the admin key, upload.
4. `OFF` when finished.

## 7. Layout rules and acceptance checks

### 7.1 Tokens

- Panels: 8 px radius, 12 px padding, 1 px line border, 8 px between panels.
- Type: 20 px screen title, 14 px section title, 12 px label, 14–16 px value.
- Colour carries meaning: blue = active/primary, green = ok/done,
  yellow = in progress/attention, red = fault or safety stop, muted grey =
  inactive or queued.  Red is never used for "queued".
- One filled accent action per screen.  Everything else is a neutral button.
- A detail screen has a 32 × 32 back chevron at the header's left edge and
  the title starts at x = 44.

### 7.2 Host layout audit

The host harness walks the live LVGL tree after every render and fails on:

- any two visible, unrelated widgets whose boxes intersect;
- any content box crossing the 712 px rail boundary or the 800 × 480 edge;
- a hidden ancestor still contributing children to the audit.

Rows below a scroll viewport are clipped by their scroller and are excluded.
The PNG run exits non-zero on a violation, so a visual regression is a build
failure, not a review note.

### 7.3 Acceptance checks

- The rail shows exactly four icon-only buttons, evenly spaced, no text
  labels and no cropped icons.
- Squeezing the firmware queue under SERVICE does not remove it from the
  operator path: SERVICE → FIRMWARE is one tap deeper than SHOW.
- SHOW contains the playlist and transport; while playing it keeps the
  selected playlist visible and puts the square **STOP** in the library slot.
- No WiFi radio starts after reset.
- CONNECT failure stays in CONNECTING and retries; no AP appears.
- AP only starts from an explicit **AP** action; the AP screen shows a
  join QR and a browser QR, the current password, and `NEW KEY`/`SET KEY`.
- GitHub check/install and the SD panel image are both in SERVICE →
  FIRMWARE; GitHub is disabled unless CONNECTED.  No other screen updates
  the panel.
- `FORGET` ends in OFF, not AP.
- Network-only actions are disabled with an inline reason when not
  connected.
- No control overlaps its neighbour or crosses a panel border; the host
  audit reports `layout audit: clean`.
- Host PNGs for Show (idle/playing), Build, Service, System, Connect
  (connected/off/AP), Firmware and the keypad are reviewed before flashing.

## 8. Implementation notes

- The rail is four icon-only rows: show, build, connect, service.  `BUILD`
  and `SERVICE` are card hubs; SYSTEM is a sub-screen of SERVICE.
- SHOW merges the former Library list: now playing, transport, playlist and
  a one-line health status.
- Firmware tiles use the fleet-core colour contract but map `queued`
  (`FL_COLOR_RED`) to a neutral tile.  The panel reserves red for faults and
  the safety stop, so a pending board never reads as a failure.
- The 30 firmware tiles use 6 × 5 at 107 × 44 px to keep both ID and phase
  text inside each tile and a 12 px bottom margin.
- The panel/GitHub source row sits directly under the header, above the node
  fleet controls: the GitHub check is the primary update action, not a
  footnote below the fleet buttons.  Offline it reads `connect to check`
  instead of leaving a stale result on screen.  The `release.json` request
  carries a timestamp so the edge cache cannot serve an older tag.
- `CONNECT` is the network screen.  The Network screen exposes OFF, CONNECT
  and AP; GitHub check/install is disabled unless CONNECTED; AP-only admin/key
  fields are disabled outside AP.
- SYSTEM is the read-only build/runtime screen; the former footer buttons
  that overlapped the panel border are gone.  Display timeout is a four-way
  segmented control (OFF, 30 s, 60 s, 300 s), not a hidden cycle.  Panel
  update lives in FIRMWARE, not SYSTEM.
- Local builds compile the GitHub owner/repository defaults; the tag workflow
  overrides them from the repository that ran the tag.
- While an animation is running, the library panel yields to the square red
  **STOP** target.  It sends the immediate stop; the transport `STOP`
  remains the decelerating stop.  The playlist never moves.
- A panel update shows a full-screen overlay with the stage and download
  percentage.  While it is up, the snapshot task stops redrawing the hidden
  screens so the download does not compete with the RGB panel.  After the
  image is downloaded, the overlay stays visible for 1.5 s, then the main loop
  cuts the backlight before `esp_ota_write`, so the cache stall cannot draw a
  garbled frame.  On failure the backlight returns and the overlay shows the
  reason with `DISMISS`.
- SYSTEM shows the SD card state: present/absent, Fly and panel image counts,
  and free space.  A blank card is shown as `0 fly / 0 panel`, not as an
  error; the firmware creates `/firmware/{fly,panel,katapult}` at mount.
- The status strip carries the running panel version and build, so an update
  that rolls back is visible immediately.  Boards show each node's running
  `sw` version.  SYSTEM → `Last update` keeps the boot-check outcome
  (`validated; node ready`, `rollback: CAN off; node missing`, ...), which
  survives the rollback reset in RTC memory.  A node-only install records
  `node stored; panel current`.
- The display backlight turns off after inactivity; the first touch only
  wakes the display and is consumed, so it cannot activate the control
  underneath.
- Host PNGs reviewed: `output_show.png`, `output_show_playing.png`,
  `output_setup.png`, `output_service.png`, `output_settings.png`,
  `output_wifi.png` (AP QRs), `output_wifi_connected.png`,
  `output_wifi_off.png`, `output_wifi_ap_key.png`, `output_upgrade.png`,
  `output_wifi_keyboard.png`.
