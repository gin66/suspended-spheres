# Panel UI design — operator-first navigation

Status: implemented; host PNGs reviewed 2026-11-17.  Complements
`doc/esp32gui_whitepaper.md`; this document owns the screen information
architecture and the WiFi interaction rules.

## 1. Why change

The current panel exposes eleven top-level rail entries.  Most are not
peers: `WIFI` is a service for `UPGRADE`, `LOG` and `CAN` are diagnostics,
and `CONFIG` is part of setup.  A flat eleven-item rail makes the operator
scan every screen to find one task and hides the dependency between a task
and the network it needs.

The UI is used in two situations:

- **Show operator** — start, stop, and check that the show is healthy.
- **Builder/commissioner** — lay out the grid, discover boards, align
  motors, load an animation, and service firmware.

The rail should reflect those jobs, not every data source.

## 2. Navigation model

Five icon-only destinations fill the rail.  There are no text labels; the
icons are the navigation.

| Icon | Destination | Detail screens |
| --- | --- | --- |
| Play | **SHOW** | now playing, transport, playlist, health |
| Sliders | **SETUP** hub | grid, boards, motor assign, test/config |
| Download | **FIRMWARE** | node image, versions, fleet upgrade, panel update, GitHub |
| WiFi | **NETWORK** | OFF/CONNECT/AP, service, AP password/QR |
| Pulse | **DIAGNOSTICS** hub | CAN scope, log, system |

The five 72 × 72 targets are spaced with 20 px top and bottom margins, 8 px
side margins and 20 px between targets.  They fill the rail evenly and do
not touch a corner or side.  The active target is a rounded pill with an
accent icon; no full-height tab and no label.

`SETUP` and `DIAGNOSTICS` open a card hub.  Tapping their active icon again
returns to the hub.  SHOW, FIRMWARE and NETWORK open directly.

SHOW is the performance console, not a commissioning dashboard.  It owns
the animation playlist and the transport: `PLAY`, `PAUSE`, `STOP`, `CYCLE`.
The playlist lists only playable animations.  A file that fails HBP2
validation is not a playlist entry; it is counted as invalid and kept out
of the list.  Health moves to a single status line at the bottom.

Library is part of SHOW.  `SETTINGS` is **DIAGNOSTICS → SYSTEM**: build and
runtime information plus the screen-timeout control.

### 2.1 Hub cards

**SETUP**

1. **GRID & LAYOUT** — dimensions and position map.
2. **BOARDS** — discovery, board IDs and health.
3. **ALIGN MOTORS** — jog, select axis, assign/unassign positions.
4. **TEST & CONFIG** — drop, speed, gap, save/reset/clear map.

**DIAGNOSTICS**

1. **CAN SCOPE** — live bus traffic.
2. **LOG** — recent events.
3. **SYSTEM** — firmware version, boot count, uptime, capacity,
   watchpoint, CAN state.

FIRMWARE and NETWORK are direct rail destinations, not diagnostics cards.

## 3. Network interaction

### 3.1 Boot state

WiFi is **OFF after every reset**.  Stored credentials are remembered, but
they never start a radio by themselves.  The HTTP service is off after
every reset.

### 3.2 Explicit modes

The Network screen has three explicit modes:

| Mode | Behaviour | Leaves mode when |
| --- | --- | --- |
| **OFF** | Radio off, HTTP service off | operator chooses CONNECT or AP |
| **CONNECT** | STA join to the selected or stored SSID | operator chooses OFF/AP, or join succeeds |
| **AP** | Setup AP + HTTP service for browser upload | operator chooses OFF/CONNECT, or AP window expires |

`FORGET` removes the stored SSID/password and returns to **OFF**; it never
opens the AP as a side effect.

### 3.3 CONNECT retry

A failed CONNECT does **not** fall back to AP.  The panel stays in
`CONNECTING`, reports the reason, and retries every 5 s.  The operator can
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
- GitHub check/install lives in **FIRMWARE**, not **NETWORK**.  It is
  disabled unless CONNECTED; it has no meaning in AP mode.
- Firmware actions are disabled while a fleet upgrade is active; `STOP` is
  disabled when idle.
- Show actions that would conflict with an active upgrade are disabled.
- AP-specific fields (SSID, key and QR) are hidden outside AP mode.  The
  scan list and STA password form are hidden in AP mode; the rest of the
  layout stays fixed.

Disabled controls use the muted text colour and a disabled button style.
The reason line remains visible so the operator knows what to do next.

## 5. Network screen layout

800 × 480 panel, 688 × 452 content area after the status strip.

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
  GitHub control here.

Every panel has 12 px inner padding.  Controls stop at the inner edge; do
not place a 32 px button at `y` such that `y + height` exceeds the panel
content height.  This was the cause of the previous Settings overlap.

## 6. Workflow examples

### First setup

1. **SETUP → BOARDS**, discover and assign board IDs.
2. **SETUP → GRID & LAYOUT**, set dimensions.
3. **SETUP → ALIGN MOTORS**, assign each axis to a position.
4. **SETUP → TEST & CONFIG**, run a short drop/speed test, save.
5. **SHOW**, select an animation from the playlist and press `PLAY`.

No WiFi mode is entered.

### GitHub panel update

1. **NETWORK**: select SSID, enter password, `CONNECT`; wait for `CONNECTED`.
2. **FIRMWARE**: `CHECK`.
3. If `newer`, `INSTALL` from the same row.  A failed join retries and stays
   in CONNECT; GitHub controls are disabled while disconnected.

### Browser upload without a public network

1. **NETWORK → AP**.
2. Scan the join QR with the phone camera; it joins the panel AP.
3. Open `http://192.168.4.1`, authenticate with the admin key, upload.
4. `OFF` when finished.

## 7. Acceptance checks

- The rail shows exactly five icon-only buttons, evenly spaced with 20 px
  top/bottom margins and 8 px side margins; no text labels and no cropped
  icons.
- SHOW contains the playlist and transport; it does not show
  "COMMISSIONING" as the playing pattern.  Unplayable files are not playlist
  entries.
- No WiFi radio starts after reset.
- CONNECT failure stays in CONNECTING and retries; no AP appears.
- AP only starts from an explicit **AP** action; the AP screen shows a
  join QR and a browser QR, the current password, and `NEW KEY`/`SET KEY`.
- GitHub check/install is in FIRMWARE and disabled unless CONNECTED.
- `FORGET` ends in OFF, not AP.
- Network-only actions are disabled with an inline reason when not
  connected.
- Settings/System controls do not overlap their panel borders.
- Host PNGs for Show, Setup, Diagnostics, Network (connected/off), Firmware,
  System and the keypad are reviewed before flashing.

## 8. Implementation notes

- The rail is five icon-only rows: show, setup, firmware, network,
  diagnostics.  `SETUP` and `DIAGNOSTICS` are card hubs.
- SHOW merges the former Library list: now playing, transport, playlist and
  a one-line health status.
- `WIFI` is not in the rail.  The Network screen exposes OFF, CONNECT and AP;
  GitHub check/install is disabled unless CONNECTED; AP-only admin/key fields
  are disabled outside AP; firmware STOP is disabled when idle.
- `SETTINGS` is now the read-only **SYSTEM** screen; the former footer buttons
  that overlapped the panel border are gone.
- The 30 firmware tiles are sized to keep both ID and phase text inside each
  tile and the fifth row inside the 452 px content area.
- While an animation is running, the playlist/library area becomes one large
  red **STOP** target.  It sends the immediate stop; the transport `STOP`
  remains the decelerating stop.
- System has a **SCREEN** timeout control (OFF, 30 s, 60 s, 300 s).  The
  backlight turns off after inactivity; the first touch only wakes the
  display and is consumed, so it cannot activate the control underneath.
- Host PNGs reviewed: `output_show.png`, `output_setup.png`,
  `output_diagnostics.png`, `output_wifi.png`, `output_wifi_connected.png`,
  `output_wifi_off.png`, `output_wifi.png` (AP QR), `output_settings.png`,
  `output_upgrade.png`, `output_wifi_keyboard.png`.
