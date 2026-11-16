# Firmware release, image loading and fleet update

> **Status: proposal, for review.** This paper defines how a build becomes a
> release image, how the Katapult CAN bootloader is installed on a Fly-Micro4,
> how the esp32gui panel is loaded with a release image, and how the panel's
> node list upgrades every outdated Fly-Micro4 (up to 30) without removing a
> board. The Katapult build menu, admin identifiers and byte stream remain in
> `katapult_whitepaper.md`. The panel bring-up and screens remain in
> `esp32gui_whitepaper.md`. Node identity remains in
> `Fly_Micro4_addressing.md`. This paper adds the release identity and the
> fleet-update contract; it does not repeat those layouts.
>
> The panel currently may hold bench software (`esp32cantx` or an
> `SS_CAN_MOCK` build). Neither is a field image. Section 3 replaces it. The
> node list is changed from a single-board version view to an identified,
> version-ordered upgrade queue in section 4.
>
> WiFi onboarding, browser upload and the panel's GitHub pull are **planned**.
> They change where an image comes from, not what an image is: the identity,
> SHA-256, OTA slot, self-test and rollback rules in sections 1 and 3 apply to
> every source. Until the WiFi stack and web server are in the tree, those
> paths stay planned and the microSD card remains the field baseline.
>
> A GitHub Release is created only by `.github/workflows/release.yml` when an
> annotated tag `vMAJOR.MINOR.PATCH` is pushed (§1.6). The workflow tests,
> builds and publishes. It does not flash a board. The panel client that later
> pulls that release is §3.9. The host test order is
> `firmware_release_tdd.md`.

---

## 0. Decisions at a glance

| Topic | Decision |
|-------|----------|
| Release identity | Every image carries `(target, version, build, sha256)` and a UTC-stamped filename. |
| Ordering key | The 32-bit `build` id. Semantic version is display text; it is never compared as a string. |
| Filename order | Fixed-width UTC timestamp, `YYYYMMDDTHHMMSSZ`, taken from the tagged commit. One tag produces one filename per artifact. |
| Katapult install | One-time, at the bench, one board at a time, over BOOTSEL. The CAN Katapult replaces the factory USB Katapult. |
| Panel image load | CH9102 UART at the bench; microSD in the field; planned authenticated browser upload and confirmed GitHub release pull. Same two OTA slots, SHA-256 and ESP-IDF rollback. |
| WiFi onboarding | Planned: scan and select an SSID or type one on the panel or web UI. WPA2-PSK, 2.4 GHz only, credentials in NVS, never in the repository. |
| Browser upload | Planned: local HTTP UI, off after every power cycle, enabled from Settings for this session. Admin password and CSRF. Same OTA writer. |
| GitHub Release | The only publisher is `.github/workflows/release.yml` on an annotated tag `vMAJOR.MINOR.PATCH`. Both images share that tag's version and the commit's build id. |
| GitHub pull | Planned panel client of that release: pinned repository, HTTPS, SNTP, SHA-256 and operator confirmation. The client never installs by itself. |
| Network trust | SHA-256 is mandatory; a detached manifest signature or a pinned release hash authenticates the publisher. The choice is a decision in section 7. |
| Board list key | The 6-byte Katapult/Klipper UUID, not the 8-byte raw flash id. Board ID stays the volatile runtime alias. |
| Upgrade scope | `ALL`, `OUTDATED` or `SELECTED`; at most 30 boards; one Katapult session at a time. About 3.5 minutes for 30 boards is accepted (`katapult_whitepaper.md` §8.1). |
| Outdated rule | `current_build < image_build`. Unknown, dirty, unassigned and offline boards are shown but excluded from the automatic set. |
| Test software | The release panel image has no mock transport and no mock injection. `esp32cantx` is never released. |

---

## 1. One image, one identity

### 1.1 Targets and artifacts

| Target | Built by | Artifact | Entry point | Written by |
|--------|----------|----------|-------------|------------|
| `fly_micro4` | `rp2040/`, env `fly_micro4_katapult` | raw `.bin` | `0x10004000` | Katapult over CAN |
| `esp32gui_s3` | `esp32gui/`, env `esp32can` | app `.bin` | inactive OTA slot | panel SD updater |
| `esp32gui_s3` | same | factory `.bin` | `0x0` | esptool over CH9102 |
| `katapult_rp2040` | Katapult tree | `.uf2` | `0x10000000` | RP2040 ROM (`RPI-RP2`) |

The Fly application is the raw Katapult payload at `0x10004000`
(`katapult_whitepaper.md` §6.1). The panel app image is the Espressif OTA
image; the factory image is the full-flash image for the CH9102 path. They are
different files from the same build and are never interchanged. A release
build of the two `esp32gui_s3` files and the `fly_micro4` file is produced by
the tag workflow in §1.6. The Katapult UF2 is not. The "Written by" column is
who writes the flash on a board.

### 1.2 Filename convention

```
<target>_<utc>_v<semver>_<git8>_<kind>.<ext>

target   fly_micro4 | esp32gui_s3
utc      YYYYMMDDTHHMMSSZ, UTC, fixed width
semver   vMAJOR.MINOR.PATCH
git8     first 8 hex characters of the source commit
kind     app | factory
ext      bin
```

Examples:

```
fly_micro4_20261116T143205Z_v1.4.2_9f3c1ab7_app.bin
esp32gui_s3_20261116T143205Z_v1.4.2_9f3c1ab7_app.bin
esp32gui_s3_20261116T143205Z_v1.4.2_9f3c1ab7_factory.bin
```

A tag release uses one timestamp, one version and one git prefix for every
artifact. The panel version is not numbered on its own.

Katapult has no semantic version of its own. Its provisioning image is named
for the target, its configuration and the build time:

```
katapult_rp2040_20261101T090000Z_<config8>.uf2
```

`config8` is the first 8 hex characters of a hash over the Katapult `.config`
that produced the UF2. The `.config` and the hash are kept beside the UF2.

Rules:

- Only `A-Z`, `a-z`, `0-9`, `.`, `_` and `-`; no spaces.
- The timestamp is the tagged commit's commit time, UTC, exported as
  `SOURCE_DATE_EPOCH`. It is not the runner's wall clock.
- The timestamp is fixed width, so a directory listing sorts chronologically.
- One annotated tag produces one filename per artifact. Packing that commit
  again keeps the same name, version and build id.
- A bench build from a dirty tree adds the suffix `_dirty` and is not a
  release. The workflow checks out the tag, so it never produces that suffix.
- The filename is for humans and for a quick sort. The manifest and the build
  id are authoritative.

### 1.3 Build id and the embedded stamp

The `build` id is a `uint32`. For a release it is the commit timestamp of the
tagged commit (`git log -1 --format=%ct`), and the workflow exports that value
as `SOURCE_DATE_EPOCH`. Fly and panel images in the same release carry the
same id. `0` means "unstamped" and is never a release. A component outside
`0..255` cannot be a tag, because `VERSION_RSP` stores each component in a
`uint8`.

| Target | Where the build id lives |
|--------|--------------------------|
| `fly_micro4` | `mc_version_t.build`, returned in `VERSION_RSP` (type 13), little-endian bytes 4–7 (`motion_streaming_whitepaper.md` §4.5) |
| `esp32gui_s3` | compile-time `SS_BUILD_ID`, shown on Settings, and the ESP-IDF application descriptor version |
| `katapult_rp2040` | no frame; the UF2 hash and `.config` hash are the record |

The Fly image also carries `major`, `minor`, `patch` and the dirty flag in the
same `VERSION_RSP`. The panel image carries `SS_FW_VERSION` and
`SS_BUILD_ID`. A release is built with `SOURCE_DATE_EPOCH` set; `__DATE__` and
`__TIME__` are not used.

A dirty working tree may be built for the bench. It sets
`MC_VERSION_FLAG_DIRTY` and the filename carries `_dirty`. `release_pack` can
write that directory for the bench. It has no publish command. The workflow
refuses a dirty stamp, and the panel refuses a dirty image for a fleet
upgrade.

The build id must increase from one published release to the next. Before
creating the release, the workflow reads `release.json` from the newest
existing GitHub Release and stops when the new id is equal or lower. Both
targets share the id, so one comparison covers the release.

### 1.4 Manifest

Every image is shipped with a JSON manifest of the same basename:

```json
{
  "schema": 1,
  "target": "fly_micro4",
  "kind": "app",
  "version": "1.4.2",
  "build": 1794839525,
  "utc": "2026-11-16T14:32:05Z",
  "git": "9f3c1ab7...",
  "dirty": false,
  "file": "fly_micro4_20261116T143205Z_v1.4.2_9f3c1ab7_app.bin",
  "size": 187392,
  "sha256": "9c1e...",
  "load_address": 268451840,
  "mcu": "rp2040",
  "katapult_min": "1.0.0"
}
```

Panel app manifest:

```json
{
  "schema": 1,
  "target": "esp32gui_s3",
  "kind": "app",
  "version": "1.4.2",
  "build": 1794839525,
  "utc": "2026-11-16T14:32:05Z",
  "git": "9f3c1ab7...",
  "dirty": false,
  "file": "esp32gui_s3_20261116T143205Z_v1.4.2_9f3c1ab7_app.bin",
  "size": 742400,
  "sha256": "4a7d...",
  "chip": "esp32s3",
  "flash_size": 16777216,
  "partition_table_sha256": "b31f...",
  "slot": "app",
  "rollback": true
}
```

Fields that the consumer checks before writing:

| Field | Fly | Panel |
|-------|-----|-------|
| `kind` | `app` for a Katapult write. `factory` is rejected | `app` for OTA. `factory` is the CH9102 image only |
| `target` | must be `fly_micro4` | must be `esp32gui_s3` |
| `build` | `> 0` and greater than the board's, unless the board is dirty | `> 0` |
| `dirty` | must be `false` for fleet use | must be `false` |
| `sha256` | over the exact file bytes | over the exact file bytes |
| `load_address` | `268451840` (`0x10004000`) | not used |
| `mcu` / `chip` | `rp2040` | `esp32s3` |
| `flash_size` | not used | `16777216` |
| `partition_table_sha256` | not used | must match the running table |
| `katapult_min` | must be supported by the installed bootloader | not used |

The SHA-256 covers the file as shipped. The last Fly block is padded with
`0xFF` only on the wire and in the verify buffer, exactly as in
`katapult_whitepaper.md` §7.3; the padding is not part of the file hash.

The factory manifest uses `kind: "factory"` and the factory filename. It is
attached to the same GitHub Release. The panel OTA writer and the Katapult
writer both reject it. The CH9102 procedure in §3.2 is the only consumer.

`lib/release_core` is the SHA-256 used by `release_pack` and by the panel.
The digest of the empty input and of the three-byte input `abc` are the
known answers in `firmware_release_tdd.md`. The panel does not carry a
second implementation.

### 1.5 SD-card layout

The release bundle is copied to the panel's microSD card:

```
/firmware/
├── panel/
│   ├── esp32gui_s3_..._app.bin
│   ├── esp32gui_s3_..._app.json
│   └── esp32gui_s3_..._factory.bin
├── fly/
│   ├── fly_micro4_..._app.bin
│   └── fly_micro4_..._app.json
└── katapult/
    ├── katapult_rp2040_..._<config8>.uf2
    └── katapult_rp2040_..._<config8>.config
```

The card is FAT32 and may be read-only during a show. The panel scans only
`/firmware/panel` for its own image and `/firmware/fly` for node images. It
ignores a file whose manifest is missing, invalid, of the wrong target, or
whose SHA-256 fails.

Selection is by highest valid `build` for the target. If two manifests claim
the same build with different SHA-256 values, the panel refuses both and
reports the conflict. Keep the previous release on the card; it is the
rollback image.

### 1.6 GitHub Release, from an annotated tag

The GitHub Release is the published form of one commit. Pushing an annotated
tag is the publish action. `release_pack` writes the directory and does not
call the GitHub API. Nothing else creates or replaces a release.

| Rule | Contract |
|------|----------|
| Tag | Annotated. Name `vMAJOR.MINOR.PATCH`. Each component is a decimal integer in `0..255` with no leading zero. |
| Rejected tags | Lightweight tags, `v01.2.3`, `v1.4`, `v1.4.2-rc.1`, and any name outside that pattern. The workflow does not run for them. |
| Notes | The tag message is the release body. An empty message fails the workflow before a build. |
| Identity | Both images take version, build id, UTC and git8 from that tag and that commit. |
| Build id | `%ct` of the tagged commit, exported as `SOURCE_DATE_EPOCH`. |
| Order | Read `release.json` from the newest existing release. The new build id must be strictly greater, or the workflow publishes nothing. |
| Once | A release that already exists for the tag is left unchanged. The workflow fails. |
| Tests | `./build/motion_core_tests` exits 0 before any image is packed. A failing test publishes nothing. |
| Images | `pio run -e fly_micro4_katapult` and `pio run -e esp32can` with `SS_RELEASE=1`, without `SS_CAN_MOCK`, and with the version and build flags. |
| Factory | Merge the ESP32 image at `0x0` from the bootloader, partition table, otadata and the app. `kind` is `factory`. The OTA asset is the app `.bin`. |
| Publish | The last step is `gh release create` with the tag, the tag message and the packed assets. |
| Katapult UF2 | Built from the Katapult tree at the bench. This workflow does not build it and does not attach it. |
| Token | `GITHUB_TOKEN` with `contents: write`. No personal access token is stored in the repository. |
| Trigger | `push` of a matching annotated tag. A branch push does not publish. `workflow_dispatch` does not publish. |
| Hardware | The workflow does not flash a board and does not open a serial port. |

The first successful run of a tag is the release. Packing that commit again
keeps the same filename, version and build id. `SOURCE_DATE_EPOCH` keeps the
stamp off the runner clock. The published SHA-256 is the identity of the
file that was uploaded; a later local rebuild does not replace it.

An operator downloads those assets into the microSD layout in §1.5. That
download is the field path until the panel client in §3.9 exists. A workflow
that fails before `gh release create` may be re-run. A failure after the
release exists does not replace the assets.

---

## 2. Install Katapult on a Fly-Micro4

The detailed build and protocol are in `katapult_whitepaper.md` §4, §5 and §7.
This section is the provisioning procedure that turns a factory board into a
field-updatable node and records its identity.

### 2.1 Build once

Build the CAN Katapult with the menuconfig in `katapult_whitepaper.md` §4:
RP2040, CAN on GPIO 4 / GPIO 5, 1 Mbit/s, `GENERIC_03H with CLKDIV 4`, no
deployment application, no USB-to-CAN bridge. Keep the `.config`, the UF2 and
the SHA-256 of both. The same UF2 is used for every board.

### 2.2 Provision one board

Do this at the bench with **one** Katapult node on the bus. A second Katapult
node is not present yet.

1. Power off. Hold **BOOT**. Connect USB-C. Release BOOT when `RPI-RP2`
   appears.
2. Copy `katapult_rp2040_..._<config8>.uf2` onto `RPI-RP2`. The drive ejects.
3. Power-cycle on the 1 Mbit/s trunk. There is no application, so Katapult
   stays in the bootloader.
4. Learn the identity. This is the only point in the procedure where the
   master broadcasts Katapult's `QUERY_UNASSIGNED`: exactly one Katapult node
   can answer, so there is no collision. Record the 6-byte UUID.
5. Send `SET_NODEID` with that UUID and node byte `116`
   (`katapult_whitepaper.md` §7.1).
6. `CONNECT`. Require start address `0x10004000`, block size 64 and
   `rp2040`. Any other value aborts.
7. `GET_CANBUS_ID`. It must equal the UUID from step 4.
8. Write the first `fly_micro4` release app with the sequence in
   `katapult_whitepaper.md` §7.3, including the read-back verify and
   `COMPLETE`.
9. The app boots without a board ID. `ASSIGN` the UUID to its board ID.
   `VERSION_REQ` must return the release `major.minor.patch` and `build`.
10. Record the board: UUID, physical label, board ID, date, Katapult `.config`
    hash, first app build.

Do not use `pio run -t upload`, and do not copy a UF2 linked at address `0` to
a provisioned board. Both write from `0x10000000` and remove Katapult. The
only field image path is Katapult over CAN.

### 2.3 Verification

| Check | Pass condition |
|-------|----------------|
| Katapult answer | `CONNECT` reports `0x10004000`, 64-byte blocks, `rp2040` |
| Identity | `GET_CANBUS_ID` equals the UUID stored in the board record |
| First app | `VERSION_RSP` reports the release version and build id |
| Boot | The board answers `ASK`/`DISC` after the next re-identify, or already has its assigned ID |
| Recovery | BOOTSEL still enumerates `RPI-RP2` |

### 2.4 Fleet rule

Provision every Fly-Micro4 before the installation is dressed. From the moment
a second Katapult node exists, `QUERY_UNASSIGNED` is forbidden: two bootloader
nodes answer the same identifier at once. The master already knows every UUID
from discovery, so normal updates never need the query.

A board that is later suspected of a bad bootloader is recovered with BOOTSEL
and the same UF2. A board that is left in Katapult after an interrupted write
is recovered by the same procedure, or by the panel retry in section 4.

---

## 3. Load a release image into the esp32gui panel

The image can arrive over the CH9102 UART, from microSD, from a browser upload,
or from a GitHub release. The UART and SD paths exist today; the network paths
are planned and are specified in §3.7–§3.9. Every source ends in the same
place: a verified image, the inactive OTA slot, a self-test and a rollback
image.

### 3.1 Retire the bench software first

The panel is currently allowed to contain one of two bench images:

| Bench image | Why it exists | Field status |
|-------------|---------------|--------------|
| `esp32cantx` | TX-only 1 Mbit/s transmitter for an RX-only RP2040 capture | Never released |
| `esp32gui` with `SS_CAN_MOCK=1` | UI and CAN task exercised without the TJA1051 | Never released |

The field image is `esp32gui` built with the real TWAI transport, no
`SS_CAN_MOCK`, `SS_RELEASE=1` and a release build id. The `esp32can`
environment is the release environment; `esp32usb` and any `SS_CAN_MOCK`
combination are bench builds. The release build fails at compile time if the
mock is present:

```c
#if defined(SS_CAN_MOCK) && defined(SS_RELEASE)
#error "release image must not contain the mock CAN transport"
#endif
```

`esp32cantx` remains a bench project. Its README already says to restore the
esp32gui image when the bench test is done; this section is that restore.

### 3.2 Bench and recovery load over CH9102

The CH9102 is the service UART. The USB-C native path is not available once
GPIO 19/20 are the CAN pins, so this is the only cable path that always works.

1. Set the mechanical UART switch to the ESP32 position.
2. Hold **BOOT**, tap **RESET**, release BOOT.
3. Write the factory image at `0x0`:

```sh
esptool.py --chip esp32s3 --port /dev/tty.usbserial-XXXX --baud 921600 \
  write_flash 0x0 esp32gui_s3_20261116T143205Z_v1.4.2_9f3c1ab7_factory.bin
esptool.py --chip esp32s3 --port /dev/tty.usbserial-XXXX \
  verify_flash 0x0 esp32gui_s3_20261116T143205Z_v1.4.2_9f3c1ab7_factory.bin
```

4. Reset. The panel boots the release image. Check Settings: the firmware
   version is the manifest version and the build is the manifest build, and
   the CAN state is the real TWAI transport, not `mock`.

A factory write without `erase_flash` leaves the SPIFFS config partition in
place. `erase_flash` wipes `/config.bin`; back it up first if the grid map is
worth keeping. A partition-table change also invalidates the old SPIFFS
offset; re-check the map after such a release.

### 3.3 Field load from microSD

The panel has two OTA slots (`partitions_16m.csv`). The microSD card is the
field baseline and works without a network. The planned WiFi paths in
§3.7–§3.9 reuse the same OTA writer; they do not add a second update
mechanism.

1. Stop the show. Confirm every node is in a safe hold or is holding position;
   the panel resets during this procedure.
2. Insert the card with `/firmware/panel/` from section 1.5.
3. On the Upgrade screen, select the panel row. The panel lists valid panel
   images by build, highest first, and shows version, UTC and SHA-256 prefix.
4. Verify target `esp32gui_s3`, chip `esp32s3`, flash size 16 MiB and the
   partition-table hash.
5. Write the image to the inactive slot with the ESP-IDF OTA API:
   `esp_ota_get_next_update_partition`, `esp_ota_begin`,
   `esp_ota_write` (4–16 KiB chunks), `esp_ota_end`,
   `esp_ota_set_boot_partition`.
6. Reboot. The new image starts in `PENDING_VERIFY` when
   `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is set.
7. Run the self-test in §3.4. On success call
   `esp_ota_mark_app_valid_cancel_rollback`. On failure or a reset before that
   call, the bootloader rolls back to the previous slot.

The panel updater never runs while the show task is transmitting. It reads
the card in chunks, not as a live stream, and it does not use the PSRAM HBP2
cache.

### 3.4 Panel self-test

The new panel image is marked valid only when all of these pass:

| Check | Pass condition |
|-------|----------------|
| Boot | setup() completes and the UI is drawn |
| CAN | `can_transport_start()` succeeds at 1 Mbit/s |
| Config | SPIFFS mounts; `/config.bin` loads or defaults are written |
| Watchdog | No reset for 30 s after the UI is ready |
| Identity | Settings shows the manifest version and build |

A failure leaves the slot unmarked, so the next reset returns to the previous
image. The failure is logged in PSRAM and on the UART; it is not silently
retried forever.

### 3.5 Verification after a panel load

| Check | Pass condition |
|-------|----------------|
| Version | Settings version and build equal the manifest |
| Transport | Show/Settings report TWAI, not `mock` |
| CAN | The scope shows received frames; the bus state is not forced offline |
| Nodes | `QUERY ALL` reaches the Fly-Micro4 boards |
| Config | The grid map and `(uuid → board_id)` map are unchanged |
| Recovery | The CH9102 path still enumerates and can reflash |

### 3.6 Wrong-target protection

A Fly `.bin` is not an ESP32 image and an esp32gui `.bin` is not a Katapult
payload. Before any write, the consumer checks the manifest `target`, the
`chip`/`mcu`, the flash size and, for the panel, the partition-table hash.
A mismatch refuses the whole operation; there is no partial write.

### 3.7 WiFi onboarding (planned)

The current panel build has no WiFi stack. This is the contract for the WiFi
package that adds it. The radio is **2.4 GHz only** (ESP32-S3); a 5 GHz SSID is
not listed and cannot be joined. The network is WPA2-PSK. WPA3-only and
enterprise access points are out of scope. The ESP32-S3 stack can speak
WPA3-SAE; this panel does not join it.

The panel is usable without WiFi. Joining a network is an explicit operator
action and never a condition for the show, CAN or the board list.

| State | Entry | Exit |
|-------|-------|------|
| `OFF` | WiFi disabled by the operator | operator enables setup |
| `AP_SETUP` | no stored credentials, `FORGET`, or the setup button | credentials entered, 10-minute window expires |
| `SCANNING` | panel or web UI asks for a list | scan result, timeout |
| `CONNECTING` | SSID and password submitted | `CONNECTED`, `FAILED` |
| `CONNECTED` | association and DHCP complete | link loss, `FORGET` |
| `FAILED` | wrong password, no AP, DHCP timeout | retry, edit, `AP_SETUP` |

Setup flow:

1. With no credentials, start an access point named
   `SuspendedSpheres-XXXX`, where `XXXX` is derived from the board UUID. The
   AP uses WPA2 with a per-device password shown on the panel for the setup
   window. It is not left open after setup.
2. On the panel, the Settings screen scans and shows SSID, RSSI and security
   mode. The operator selects an SSID from the list or types a hidden SSID.
   The web UI offers the same list once the setup AP is joined.
3. Enter the password. It is write-only: the field is never redisplayed, and
   the value never appears in the log, the status JSON or a screenshot.
4. Connect. On success, store the credentials in NVS, close the setup AP, run
   SNTP and show the IP address and RSSI on Settings.
5. On failure, keep the stored credentials editable, retry with a bounded
   backoff, and reopen the setup AP after the operator presses `SETUP` or
   `FORGET`. A wrong password must not strand the panel.
6. `FORGET` wipes the WiFi credentials and the admin password hash from NVS
   and returns to `AP_SETUP`.

Rules:

- Credentials live in NVS, never in `config.bin`, the repository or a log.
  `wifi_secrets.*` is not reintroduced as a compiled file.
- The setup AP password is generated per device and is not a shared default.
- WiFi runs in its own task. It does not block the CAN task and it never sits
  in the LVGL draw path.
- The clock is set by SNTP after association. A valid time is required for the
  GitHub TLS path in §3.9; the SD and web paths work without it.
- Optional mDNS name: `suspended-spheres.local`. If mDNS fails, the panel shows
  the IPv4 address and the operator uses that.

### 3.8 Browser upload (planned)

The panel serves a small local web UI from its own flash. There is no cloud
service and no inbound port forward. The UI is for the installation LAN only.

| Element | Contract |
|---------|----------|
| Server | ESP-IDF HTTP server in a task, not the CAN or show task. The task exists only while the Settings switch is on |
| Enable | Settings switch, held in RAM. Boot and power cycle start with the server off. The switch is not written to NVS or `config.bin` |
| Access | Device admin password, set on the panel during onboarding, salted hash in NVS; the setup AP keeps its own per-device password |
| Transport | HTTP on the installation LAN. No TLS stack for this UI |
| Upload | One at a time; `POST` only; CSRF token; size limit checked before write |
| Busy | Refused while the show is running or a board queue is active |
| Trust | Target, version, build, SHA-256, partition table and optional signature checked before the first write |

Endpoints:

| Method and path | Purpose |
|-----------------|---------|
| `GET /` | Status page and upload form |
| `GET /api/status` | JSON: panel version/build, WiFi, CAN, selected image, board summary |
| `POST /api/update/panel` | Upload a panel release bundle; stream to the inactive OTA slot |
| `POST /api/images/fly` | Store a Fly release bundle in `/firmware/fly` on the SD card |
| `POST /api/upgrade/all` | Optional; start the same board queue as §4.7 |
| `POST /api/wifi` | Optional; set or forget credentials; the panel Settings path is sufficient |

The upload body is either a `.zip` containing `<basename>.bin` and
`<basename>.json`, or a multipart pair with the same two files. A release-level
`release.json` may carry several targets; the panel installs only
`esp32gui_s3`, stores `fly_micro4` for the board list, and ignores any other
target.

Panel image: stream the body into `esp_ota_begin`/`esp_ota_write`, hash while
writing, call `esp_ota_end`, then `esp_ota_set_boot_partition` and reboot. A
verification failure calls `esp_ota_abort`; the active slot is untouched.

Fly image: the upload is stored on SD so that the previous image remains
available for rollback. Without a card the panel refuses the Fly upload and
says why; it does not hold an unbounded image in RAM.

Error responses are explicit: `401` unauthenticated, `409` show or queue
active, `413` too large, `422` wrong target/build/hash, `500` write failure.
No request partially updates a slot. The web UI may show the board list, but
the upgrade trigger uses the same queue and the same safety rules as the panel.

### 3.9 GitHub release pull (planned client)

The release this client downloads is the one §1.6 published. The owner and
repository are compiled or provisioned once; the web UI does not let an
unauthenticated visitor change them. Until this client is in the tree, an
operator downloads the same assets and copies them to the card (§1.5).

| Item | Contract |
|------|----------|
| API | GitHub Releases over HTTPS, following redirects to the asset host |
| TLS | ESP-IDF certificate bundle; SNTP time must be valid, otherwise the pull is disabled with a reason |
| Repository | Public by default; a private repository needs a read-only token in NVS, never in the repository |
| Rate limit | Unauthenticated API is limited; use `ETag`/`If-None-Match` and cache the last response |
| Poll | Manual `CHECK FOR UPDATES`; an optional daily check may download and verify a release, but only raises an "update available" flag |
| Install | Never automatic. The operator confirms the install |
| Identity | The same `(target, version, build, sha256)` and manifest as every other source |

The release carries a small `release.json` plus the per-target assets from
section 1.2:

```json
{
  "schema": 1,
  "tag": "v1.4.2",
  "published": "2026-11-16T14:35:00Z",
  "targets": {
    "fly_micro4": {
      "version": "1.4.2",
      "build": 1794839525,
      "assets": [
        "fly_micro4_20261116T143205Z_v1.4.2_9f3c1ab7_app.bin",
        "fly_micro4_20261116T143205Z_v1.4.2_9f3c1ab7_app.json"
      ]
    },
    "esp32gui_s3": {
      "version": "1.4.2",
      "build": 1794839525,
      "assets": [
        "esp32gui_s3_20261116T143205Z_v1.4.2_9f3c1ab7_app.bin",
        "esp32gui_s3_20261116T143205Z_v1.4.2_9f3c1ab7_app.json",
        "esp32gui_s3_20261116T143205Z_v1.4.2_9f3c1ab7_factory.bin",
        "esp32gui_s3_20261116T143205Z_v1.4.2_9f3c1ab7_factory.json"
      ]
    }
  }
}
```

Flow:

1. Fetch the latest release metadata and `release.json`. Compare the target
   `build` with the running panel build and the selected Fly image.
2. Show version, build, UTC, release notes, asset sizes and SHA-256 prefixes.
   A build that is equal or lower is not offered as an update.
3. On operator confirmation, download the asset over HTTPS. Verify SHA-256 and,
   if enabled, the detached manifest signature. A wrong target, size or hash
   aborts before a write.
4. Panel asset: write the inactive OTA slot, reboot, self-test and roll back on
   failure. Fly asset: store in `/firmware/fly` for the board list in §4.
5. A failed network fetch, TLS error, rate limit, missing asset or hash
   mismatch leaves the current image and the rollback slot untouched.

The tag is for humans. The `build` id is the order. A release that is pulled
from GitHub is not automatically selected for a board queue; the operator
selects the image, then runs `UPGRADE ALL OUTDATED`. A `katapult_rp2040` asset
is a bench provisioning file: the panel may store it on the card, but it never
writes a bootloader over CAN.

### 3.10 Trust and source arbitration

The panel may hold images from four sources:

| Source | Trust boundary |
|--------|----------------|
| CH9102 UART | Physical service access |
| microSD | Physical access to the card |
| Browser upload | Device admin password and the installation LAN |
| GitHub pull | Pinned repository, HTTPS, SNTP and operator confirmation |

The selected image per target is stored with its `source`, `build`, SHA-256 and
UTC. The panel displays the highest valid build; the operator chooses the image
for the next queue. It never silently changes the selection while a queue is
running.

SHA-256 is mandatory and detects corruption and wrong files. It does not prove
who published the file. For the web and GitHub sources, prefer one of these:

- a detached signature over the manifest, verified with a public key compiled
  into the firmware; or
- a release hash pinned in the panel configuration and changed only by an
  authenticated operator action.

If neither is present, the web endpoint is the trust boundary and must be
authenticated, and the GitHub pull must fetch only from the pinned repository
and require explicit confirmation. A signature is the preferred long-term
answer because it lets a release be mirrored or uploaded without trusting the
transport. The algorithm is chosen with the crypto library that ships with
ESP-IDF.

---

## 4. The identified board list

### 4.1 What the list has today

The Upgrade screen already lists every discovered node with the raw 8-byte
flash id, board ID, the `sw_version` string, and `online`/`no version` state.
`QUERY ALL` sends `VERSION_REQ`; `UPGRADE SELECTED` reports that the CAN
transfer is not implemented. The gaps against this paper:

| Gap | Effect |
|-----|--------|
| Key is the 8-byte flash id | It is not the 6-byte UUID that Katapult uses |
| `VERSION_RSP` build id is discarded | The list cannot order builds; only `x.y.z` text is kept |
| The Fly-Micro4 does not answer `VERSION_REQ` yet | The list shows `--` for every board |
| `VERSION_REQ`/`RSP` handler is not in `rp2040/` | No version, no outdated decision |
| Only `UPGRADE SELECTED` exists | No `ALL`, no `OUTDATED`, no queue |
| No image selection or manifest | The press action cannot name a target build |
| No per-board upgrade state | The operator cannot see which board is in Katapult |
| Last version is not persisted | A rebooted panel loses the comparison |

### 4.2 Identity in the list

The list key is the **6-byte UUID** from `Fly_Micro4_addressing.md` §1, the
same value `DISC` and Katapult `GET_CANBUS_ID` carry. The board ID is the
volatile alias from `ASSIGN` and is displayed next to it. Grid positions are
host-side and are shown as the assigned position count or as the position
numbers.

| Display field | Source | Purpose |
|---------------|--------|---------|
| UUID (12 hex) | `DISC`, `GET_CANBUS_ID` | Permanent identity, survives reflash and board-ID loss |
| Board ID | `ASSIGN` | Runtime alias for `ENTER_BOOTLOADER`, status and version |
| Position(s) | `(uuid, axis) → position` map | Tells the operator which ball is about to stop |
| Current version | `VERSION_RSP` major.minor.patch | Display only |
| Current build | `VERSION_RSP` build, plus dirty flag | Ordering and outdated decision |
| Age | Time since the last `VERSION_RSP` | A stale row is not eligible |
| Upgrade state | Panel queue | Pending, writing, verifying, done, failed |

The raw 8-byte flash id may remain in the provisioning record, but it is not
the list key and is not shown as the identity.

### 4.3 Data model

The node entry gains the fields below. `ui_node_t` and `ss_node_t` carry the
display copy; the config store carries the persistent part.

| Field | Type | Meaning |
|-------|------|---------|
| `uuid[6]` | bytes | Katapult/Klipper UUID; list key |
| `board_id` | int8 | `-1` until assigned |
| `online` | bool | Seen within the online window |
| `version_valid` | bool | A `VERSION_RSP` has been received since the last query |
| `version_major/minor/patch` | uint8 | Semantic version |
| `version_flags` | uint8 | Bit 0 = dirty |
| `current_build` | uint32 | 0 = unknown |
| `version_age_ms` | uint32 | Freshness of the version data |
| `assigned_positions` | int | Count of map entries for this UUID |
| `upgrade_state` | enum | `IDLE`, `QUEUED`, `ENTERING`, `CONNECTING`, `WRITING`, `VERIFYING`, `REBOOTING`, `DONE`, `FAILED`, `SKIPPED` |
| `upgrade_progress` | uint8 | 0–100 for the current board |
| `upgrade_result` | char[] | Short reason or `build N` |
| `last_upgrade_ms` | uint32 | Last completed attempt |

The snapshot also carries the selected image per target: version, build, UTC,
SHA-256 prefix and source (`SD`, `web`, `GitHub` or `UART`). `ss_config_t`
moves to version 2 and stores, per UUID, the last confirmed `current_build`,
`version_flags` and last upgrade result, plus the selected Fly image identity.
The rest is RAM.

### 4.4 Acquiring versions

`QUERY ALL` sends `VERSION_REQ` (type 12) to field 127. Each board answers
`VERSION_RSP` (type 13) after `1 ms × (board_id mod 10)`, as in
`motion_streaming_whitepaper.md` §4.5. The panel:

1. Clears `version_valid` for every row before the query.
2. Waits 2 s for answers.
3. Repeats the query to the missing board IDs only, up to 3 attempts.
4. Marks a row `version unknown` when no answer arrives. The row remains
   visible but cannot enter the automatic outdated set.

The panel also sends a single-board `VERSION_REQ` after each upgrade, as the
final acceptance check. A board that never answers `VERSION_REQ` is a
candidate for a one-time bench update; the automatic fleet path cannot verify
it.

### 4.5 Outdated decision

The image build and the board build are unsigned integers. Compare them, never
the version strings. The dirty flag is tested first: a dirty board is replaced
by the release image at any build id, including a build newer than the image.
A forced downgrade of a clean board is a separate service action.

| Board state | Image state | Decision |
|-------------|-------------|----------|
| dirty flag set | release | **replace**; development build, any build id |
| `current_build == 0` | release | `unknown`; manual only |
| `version unknown` | release | show `unknown`; manual only |
| `current_build < image.build`, clean | release | **outdated**, auto-select |
| `current_build == image.build`, clean | release | current |
| `current_build > image.build`, clean | release | newer than the image; skip |
| Not in Katapult / no answer | release | recover with BOOTSEL; not in the queue |

A forced downgrade exists only as an explicit service action. It is not part
of `UPGRADE ALL OUTDATED` and requires a confirmation on the panel.

### 4.6 Selection

| Action | Meaning |
|--------|---------|
| `SELECT ALL` | Every eligible board, up to 30 |
| `SELECT OUTDATED` | Boards with a known build lower than the image build |
| `SELECT ROW` | One board, for rework or a retry |
| `UPGRADE SELECTED` | The current manual selection |
| `UPGRADE ALL OUTDATED` | The default fleet action |

An eligible board is **assigned** (`board_id >= 0`), **online** (seen within
3 s), has a **fresh** version answer for the automatic sets, and its UUID is in
the config map. Unassigned, offline, unknown-version and wrong-target rows are
shown with a reason and excluded. The list holds at most 30 rows
(`SS_MAX_BOARDS`); a 31st discovery is a table-full fault, not a silent drop.

An unassigned board is not upgraded automatically. The operator assigns it
first. A board that is already in Katapult is finished or recovered with the
same writer; a second board is not sent into Katapult until this one has
booted and answered.

### 4.7 Upgrade-all sequence

The writer is Katapult, one board at a time. The byte stream is
`katapult_whitepaper.md` §7. There is no second flasher. §8.1 of that paper
is the accepted duration: about 7 seconds per board, about 3.5 minutes for
30, at typical NOR timings.

```
IDLE
  └─ operator: UPGRADE ALL OUTDATED
       ├─ load and verify manifest + SHA-256 + target
       ├─ QUERY ALL, rebuild the outdated set
       ├─ show the 30 tiles and require confirmation
       ├─ stop show, stop test run, confirm safe hold
       └─ for each board, ascending board ID:
            ENTERING    COMMAND opcode 4 to that board only. Tile turns yellow
            CONNECTING  SET_NODEID(uuid, 116), CONNECT, GET_CANBUS_ID == uuid
            WRITING     SEND_BLOCK from 0x10004000, 64 B blocks, in order
            VERIFYING   EOF, REQUEST_BLOCK, compare with the padded image
            REBOOTING   COMPLETE, wait for the app, ASSIGN(uuid → board ID)
            CHECK       VERSION_REQ; build == image.build
            DONE        tile turns green; record build and result
          stop on the first failure. That tile stays marked failed.
          tiles not yet started stay red
       └─ final QUERY ALL, summary, leave the show stopped
```

`WRITING` through `CHECK` are `katapult_whitepaper.md` §7.3. If the read-back
fails, the previous image is written back before `COMPLETE`. The board does
not boot an unverified image.

`STOP` finishes the board that is already yellow, then stops. It does not
abort a half-written block. Tiles not yet started stay red. A board left in
Katapult is reported on its tile.

`UPGRADE SELECTED` is the same writer with one tile.

### 4.8 Failure handling

| Failure | Action |
|---------|--------|
| Manifest missing or SHA-256 mismatch | Refuse before any board is touched. Tiles stay as they are |
| Wrong target, chip or flash size | Refuse before any board is touched |
| No `VERSION_RSP` after 3 queries | Mark unknown, dim the tile, exclude it |
| Board offline or unassigned | Dim the tile and skip it |
| `ENTER_BOOTLOADER` timeout | Retry once, then mark the tile failed and stop |
| `SET_NODEID` or `CONNECT` timeout | Retry once, then mark failed and stop |
| `CONNECT` start address not `0x10004000` | Abort. Wrong bootloader or wrong target |
| `GET_CANBUS_ID` differs from the row UUID | Abort. Do not write |
| NACK `0xF1` | Resend the block, bounded retries |
| Busy `0xF3` | Wait, then resend |
| Read-back mismatch | Rewrite the previous image, `COMPLETE` it, mark the tile failed, stop |
| Post-update build differs | Mark the tile failed and stop |
| Bus-off or panel reset | Tiles already green stay current. Re-query before another pass |
| Board left in Katapult | That tile shows bootloader. The next pass or BOOTSEL finishes it |

### 4.9 Persistence and resume

The panel stores, keyed by UUID:

- the board ID, so a returning board keeps the same motor fields;
- the last confirmed version and build;
- the last upgrade result and time.

After a panel reset, an interrupted queue is not resumed automatically. The
panel re-queries and rebuilds the outdated set. A board that already reached
`DONE` is current by build and drops out. A board left in Katapult does not
answer `VERSION_REQ`; it appears as offline/unknown and is handled by the
recovery row, not by silently guessing.

### 4.10 Upgrade screen

All 30 boards are on this one page. There is no second page and no scrolling
list. The content area is the 712 × 452 px region left of the navigation
rail. Under the header and the buttons, the tiles are 6 columns by 5 rows.

A tile shows the board ID in large type, and the phase word while it is
yellow (`enter`, `write 46%`, `verify`, `reboot`). The header carries the
image version, the build, the source, and `12 / 30` plus the phase of the
yellow tile. That is the view for the 3.5 minutes.

| Tile | Colour | When |
|------|--------|------|
| Waiting | Red | Outdated, not started |
| Flashing | Yellow | The one board in Katapult now. Only one tile is yellow |
| Done | Green | `VERSION_RSP` build matches the image |
| Excluded | Dim | Unknown, offline, unassigned, or already current |
| Failed | Red with a cross | This board stopped the queue |

```
NODE FIRMWARE     fly_micro4 1.4.2   build 1794839525   SD    12/30  write
[ QUERY ALL ] [ UPGRADE ALL OUTDATED ] [ STOP ]

  00 green   01 green   02 yellow   03 red   04 red   05 red
  06 red     07 red     08 red      09 red   10 red   11 red
  12 red     13 red     14 red      15 red   16 red   17 red
  18 red     19 red     20 red      21 red   22 red   23 red
  24 red     25 dim     26 red      27 red   28 red   29 red
```

Board 02 is yellow because it is the one being written. Boards 00 and 01
have already verified and are green. Board 25 is dim because it had no
version. The rest wait in red. The operator watches the yellow tile move
across the page.

### 4.11 Bus and safety rules

- Stop the show and the test run before the first `ENTER_BOOTLOADER`.
- One board at a time. One yellow tile. No second data pair and no
  interleaved blocks.
- `QUERY_UNASSIGNED` stays the single-board bench query from §2.2.
- `ENTER_BOOTLOADER` goes to one board ID. It is never sent to field 127.
- After `COMPLETE` the board boots with no board ID. The panel `ASSIGN`s
  the UUID from that tile so the motor fields stay put.
- A failed board is reported on its tile and the queue stops. Later tiles
  stay red.

---

## 5. End-to-end procedure

| Step | Actor | Result |
|------|-------|--------|
| 1. Publish | maintainer | Push an annotated tag. The workflow runs the host tests, builds both images and creates the GitHub Release (§1.6). The Katapult UF2 stays a bench file |
| 2. Copy | operator | Download the GitHub Release into `/firmware/{panel,fly}` on the microSD card. The Katapult UF2 is copied from the bench record into `/firmware/katapult` |
| 3. Provision | bench | Katapult + first Fly release on each board; UUID recorded |
| 4. Load panel | bench, SD, web or GitHub | esp32gui release image; Settings shows the manifest version/build and source; no `mock` |
| 4a. WiFi setup | operator, once | SSID and credential in NVS; IP and SNTP time shown on Settings |
| 4b. Network image | operator | Browser upload or confirmed GitHub pull; same manifest and SHA-256 checks |
| 5. Verify | operator | `QUERY ALL`; every board reports a version/build |
| 6. Upgrade | operator | `SELECT OUTDATED` → `UPGRADE ALL OUTDATED`; stops on failure |
| 7. Verify | operator | Final `QUERY ALL`; every upgraded board reports the image build |
| 8. Record | operator | Board UUIDs, previous/new build, failures, date |
| 9. Start | operator | Resume the show only after the queue is empty and all rows are green |

The network path replaces steps 2 and 4: the release is uploaded or pulled
directly. Provisioning in step 3 and verification in steps 5–9 are unchanged.
If the network is absent, steps 2 and 4 are the baseline.

Rollback of a Fly board is a second pass with the previous `fly_micro4` image.
Rollback of the panel is automatic on a failed self-test, or a second SD load
of the previous panel image, or the CH9102 factory path. `NFR-MAINT-004`
requires the update to work in the installed position with integrity
verification and rollback; this procedure does that without removing a board.

---

## 6. What the tree still needs

| Area | Work |
|------|------|
| `lib/motion_core` | Skip motor field 63 and carry the 6-byte UUID, as `Fly_Micro4_addressing.md` §1 and `katapult_whitepaper.md` §9 already require. The code and `ss_config_t` version 1 still store the 8-byte flash id. |
| `lib/motion_core` | Accept `COMMAND` action 4 and reject it on field 127. Add that row to `motion_streaming_whitepaper.md` §4.5 in the same change. `motion_core` currently accepts only up, down, stop and re-identify. The reset sequence stays `katapult_whitepaper.md` §6.2. |
| `lib/release_core` | Tag parse, filename, canonical manifest, SHA-256, monotonic build id. Linked by the packer and by the panel. |
| `lib/fleet_core` | Outdated set, one-board Katapult queue, and the tile colour for each board. No sockets and no wall clock. |
| `rp2040/` | Answer `VERSION_REQ`. Enter Katapult on action 4. The image it boots is the Katapult payload at `0x10004000`. |
| `rp2040/` | Keep the `fly_micro4_katapult` link at `0x10004000`; stamp both envs from the same flags the workflow passes. |
| `esp32gui/` | Store the 6-byte UUID; bump `ss_config_t` to version 2; add `current_build`, flags and last result. Migrate a version-1 file as "version unknown". |
| `esp32gui/` | `VERSION_REQ` retry and unknown handling; show build and age. |
| `esp32gui/` | Upgrade page: 30 tiles, 6 × 5, no scroll. Red waiting, one yellow while Katapult writes, green when the build matches. The writer is the §7 byte stream. **Done:** `upgrade_manager.cpp` uses `fl_build_set` and `katapult_flash_ex`. |
| `esp32gui/` | SD image scanner, manifest checks and the panel OTA updater. Reject `kind: factory`. **Done:** `firmware_image.cpp` and `panel_ota.cpp`; the embedded image remains the no-card bench fallback. |
| `esp32gui/` | WiFi onboarding: scan, select or type an SSID, setup AP, NVS credentials, SNTP and the link state machine. |
| `esp32gui/` | Local web server: admin auth, CSRF, panel and Fly upload endpoints, streamed OTA. |
| `esp32gui/` | GitHub release client of the §1.6 release: pinned repository, TLS bundle, ETag cache, `release.json`. |
| `esp32gui/` | Image-source arbitration: source, build and SHA-256 in the header; the selection stays put while a queue runs. |
| `esp32gui/` | Release gate: env `esp32can`, no `SS_CAN_MOCK`, `SS_RELEASE`, compile-time `#error`. Drop the mock inject action from that build. |
| `esp32gui/` | Show the panel image and build on Upgrade and Settings. |
| `tools/release_pack` | Host tool, built like `pattern_check`. Writes the release directory. Refuses dirty, a bad tag, a non-monotonic build and a factory image used as an app. No GitHub API. |
| `.github/workflows/release.yml` | §1.6. Lands when both images can be stamped, so the first tag push is able to publish. |
| `tests/` | The order and the assertions are `firmware_release_tdd.md`. |
| `doc/` | With the code: `motion_streaming_whitepaper.md` command row, `motion_streaming_tdd.md` field-63 checks, `memory_map.md` config size, and the screen notes in `esp32gui_whitepaper.md`. |

Without `VERSION_RSP` the list can only show `--`. Without action 4 the board
cannot be moved into Katapult from the running application. Without the tag
workflow there is no GitHub Release to download.

---

## 7. Decisions

### 7.1 Closed

| Decision | Contract |
|----------|----------|
| Publisher | An annotated tag `vMAJOR.MINOR.PATCH` runs `.github/workflows/release.yml`. That workflow is the only creator of a GitHub Release. |
| One release | The tag's version and the commit's build id stamp both images. There is no separate panel version. |
| Build id | Commit timestamp `%ct`. A later release must be strictly greater. The same tag is not rebuilt into a new id. |
| Packer | `release_pack` writes files. It does not call the GitHub API. |
| Factory image | A release asset, `kind: factory`, used by the CH9102 path only. |
| Katapult UF2 | Bench artifact from the Katapult tree. Not a workflow asset. |
| Prerelease tags | Not accepted. `v1.4.2-rc.1` does not publish. |
| Semver | Display and the release notes. Ordering is the build id. Components are `uint8`. |
| Dirty boards | Replaced by the release image at any build id. Clean boards newer than the image are skipped. |
| Unknown-version boards | Shown and excluded from the automatic set. |
| Panel field path | microSD of the published assets. Browser upload and the panel pull client come later and use the same checks. |
| Credentials | WiFi and admin secrets in NVS. The workflow uses `GITHUB_TOKEN` only. No token in the tree. |
| Node update | Sequential Katapult over CAN, one board at a time. About 3.5 minutes for 30 boards is the accepted duration. USB BOOTSEL remains the last resort. |
| WiFi security | WPA2-PSK, 2.4 GHz. WPA3-only and enterprise networks are out of scope. |
| Web UI | HTTP. A Settings switch starts the server for this power cycle. Boot leaves it off. Admin password and CSRF stay on every write. |
| Image selection | The highest valid build is shown. The operator picks the image for the next queue. A running queue keeps its image. |
| Test run | Stays a commissioning feature. It is stopped before an upgrade and is not part of the upgrade path. |
| Board label | Grid position plus the short UUID. The UUID is the key. |
| Card retention | Two releases per target on the card. GitHub keeps the older releases. |
| Katapult record | UF2 SHA-256 and `.config` hash. No version frame. |

### 7.2 Open, and not required for the first release

These do not change §1.6 or the fleet queue. The TDD leaves them until the
phase that needs them.

| Decision | Proposal left in place |
|----------|------------------------|
| Publisher authentication beyond SHA-256 | A detached manifest signature checked with a key compiled into the panel. Until that phase, SHA-256 plus the pinned repository and the admin upload check are the gate. |
| GitHub polling | A manual check. An optional daily check may raise "update available" and must not install. |
| Read-only GitHub token on the panel | None while the repository is public. A token in NVS is only considered if the unauthenticated rate limit blocks the manual check. |
