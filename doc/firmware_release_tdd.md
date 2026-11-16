# Firmware release — host TDD plan

> Test-first work for `firmware_release_whitepaper.md`. No Fly-Micro4, no
> panel, no CAN adapter and no call to the GitHub API. The whitepaper owns
> the contract. This file owns the order and the assertions. Frame layouts
> stay in `motion_streaming_whitepaper.md`, `Fly_Micro4_addressing.md` and
> `katapult_whitepaper.md`; do not copy them here.
>
> Write one failing test, then the smallest code that makes it pass. Do not
> start a phase until the previous phase's host binary exits 0. Firmware
> phases end with `pio run` and nothing uploaded.

---

## 0. How a phase runs

The harness is the one already in `doc/motion_streaming_tdd.md` §0.1: C++17,
CMake, one `ASSERT`, `./build/motion_core_tests` exits 0. New sources are
`lib/release_core/` and `lib/fleet_core/`. `tools/release_pack.cpp` is built
the same way as `tools/pattern_check.cpp`.

Tests do not sleep, do not read the wall clock and do not open a socket. A
queue test advances time by passing `now_ms`. A GitHub response test reads a
fixture string.

`SS_RELEASE` combined with `SS_CAN_MOCK` is a compile failure. The release
workflow passes `SS_RELEASE` and does not pass `SS_CAN_MOCK`.

---

## 0.1 Closed for this plan

These match `firmware_release_whitepaper.md` §7.1. Do not reopen them in a
test.

- One annotated tag `vMAJOR.MINOR.PATCH` is one release. Both images carry
  that version and the tagged commit's `%ct` as the build id.
- `release_pack` writes a directory. The workflow is the only publisher, and
  it is added in phase R8, after both images can be stamped.
- `kind: factory` is a CH9102 asset. Katapult and the panel OTA writer reject
  it.
- The Katapult UF2 is not a workflow asset.
- A dirty board is replaced at any build id. A clean board newer than the
  image is skipped. Unknown and unassigned boards are shown and excluded.
- SHA-256 lives in `lib/release_core` and is the function the panel calls.
- The fleet writer is Katapult, one board at a time. About 3.5 minutes for
  30 boards is accepted. There is no second flasher.
- The upgrade view is one page of 30 tiles. Waiting is red, the board in
  Katapult is yellow, a verified board is green. Exactly one tile is yellow
  while the queue runs.

## 0.2 Deferred

§7.2 stays deferred. Do not add a signature, an HTTP server, a WiFi stack or
a panel GitHub client before phase R9. Prerelease tags stay rejected.

---

## Phase R0 — UUID and motor field 63

The addressing note and `katapult_whitepaper.md` §9 already require this.
`motion_core` and `ss_config_t` version 1 still use the 8-byte flash id, and
`motion_streaming_tdd.md` still locks that map. This phase moves those tests
with the code. The byte layout stays in `Fly_Micro4_addressing.md` §1 and §2.

- [ ] **U1** `fasthash64` of the 8 bytes `00 01 02 03 04 05 06 07` with seed
  `0xA16231A7` yields UUID `65 ef aa 4c f6 5d`. The load is little-endian
  through a byte copy, including on an unaligned address.
- [ ] **U2** UUID bytes are compared in full. A one-bit difference is a
  different board.
- [ ] **U3** Motor field for `board_id × 4 + axis` matches the skip table in
  the addressing note: board 0 owns 0–3, board 15 owns 60, 61, 62 and 64,
  field 63 is never a motor.
- [ ] **U4** Update the host checks named in `katapult_whitepaper.md` §9
  (`motion_streaming_tdd.md` §3, P7, P8, §9) in the same change, so the tree
  has one map.

Gate: `./build/motion_core_tests` exits 0, and a reader of the addressing
note finds the same fields the tests assert.

## Phase R1 — `lib/release_core`

Pure identity. No filesystem beyond what the test buffer holds, no `git`.

Known SHA-256 answers, hex:

| Input | Digest |
|-------|--------|
| empty | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `abc` | `ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad` |

- [ ] **A1** Those two digests, and a digest that changes when one input byte
  changes.
- [ ] **A2** `v1.4.2` parses to `{1, 4, 2}`. Reject `v01.4.2`, `v1.4`,
  `v1.4.2-rc.1`, `V1.4.2`, `v256.0.0`, an empty string and a lightweight
  name with a suffix.
- [ ] **A3** Filename for target `fly_micro4`, commit time `1794839525`,
  version `1.4.2`, git `9f3c1ab7`, kind `app` is
  `fly_micro4_20261116T143205Z_v1.4.2_9f3c1ab7_app.bin`. The same inputs
  produce the same name on a second call. `1794839525` is
  `2026-11-16T14:32:05Z`.
- [ ] **A4** Build id `0` is rejected. A new id equal to or less than the
  previous published id is rejected. A greater id is accepted. Previous `0`
  means no published release and accepts any non-zero id.
- [ ] **A5** The manifest emitter writes schema 1 with a fixed key order and
  no insignificant whitespace. The parser accepts that text and rejects a
  missing `sha256`, a wrong `target`, `dirty: true` for a fleet image, a
  `load_address` other than `268451840` on `fly_micro4`, and
  `flash_size` other than `16777216` on `esp32gui_s3`.
- [ ] **A6** `kind: factory` passes the parser for `esp32gui_s3` and is
  rejected by the OTA selector and by the Katapult selector. `kind: app` is
  accepted by the matching selector only.
- [ ] **A7** Two manifests with the same build and different SHA-256 values
  are a conflict: the selector returns neither.
- [ ] **A8** A dirty stamp forces the filename suffix `_dirty` and the fleet
  selector rejects the manifest.
- [ ] **A9** `release.json` for one tag lists `fly_micro4` and `esp32gui_s3`
  with the same `version` and the same `build`. The factory files are assets
  of `esp32gui_s3`. A Katapult UF2 name is rejected as an asset.

Gate: the new tests pass inside `motion_core_tests`. No workflow file yet.

## Phase R2 — `tools/release_pack`

The tool reads flags and files and writes a directory. It has no network
code and no `--publish` flag.

- [ ] **P1** A clean invocation with the A3 inputs writes the `.bin` copy,
  the `.json` manifest and `release.json`. The manifest `sha256` matches
  `release_core` over the file bytes.
- [ ] **P2** `--dirty` writes the `_dirty` names and returns a non-zero status
  when the caller also asks for a release directory. A bench directory is
  still written, so a developer can see the files. The release directory is
  absent.
- [ ] **P3** `--previous-build` equal to the new build returns non-zero and
  writes nothing.
- [ ] **P4** A fly image whose first word is outside SRAM
  (`0x20000000`–`0x20042000`) or whose reset vector is outside the Katapult
  application window is rejected. The test uses an 8-byte fixture, not a
  firmware build.
- [ ] **P5** A factory image is accepted only with `kind: factory`. Passing
  it as `--ota` returns non-zero.

Gate: `./build/release_pack` exists and the assertions above are in
`motion_core_tests` through the library, plus a small CLI test that runs the
binary on a temporary directory.

## Phase R3 — enter-bootloader command

`esp32gui_whitepaper.md` §9.3 and `katapult_whitepaper.md` §6.2 already name
action 4. `motion_core` rejects it. Add it there and add the row to
`motion_streaming_whitepaper.md` §4.5 in the same change.

- [ ] **C1** `mc_pack_command` / `mc_unpack_command` round-trip action 4 with
  distance 0, speed 0 and a sequence byte.
- [ ] **C2** A helper used by both ends accepts action 4 only for a board
  field `0..29`. Field 127 is rejected. Actions 1, 2, 3 and `0x7F` keep
  their current field rules.
- [ ] **C3** The Katapult request check is a pure function over a supplied
  signature and alignment. The constants are
  `CANBOOT_SIGNATURE` `0x21746f6f426e6143` and request
  `0x5984E3FA6CA1589B`. A bad signature or a misaligned pointer returns
  "do not write". The host test does not reset the machine.

Gate: host tests pass. The firmware call that writes the request and resets
is phase R5. It is not executed here.

## Phase R4 — `lib/fleet_core`

The queue in `firmware_release_whitepaper.md` §4.5–§4.8. The test supplies
nodes, an image manifest and a script of step results. It records the
commands the queue asked to send.

- [ ] **F1** Image build `0`, a dirty image, a SHA-256 mismatch, a wrong
  target and `kind: factory` each refuse the queue before any command.
- [ ] **F2** Dirty flag selects the board at a lower, equal or higher build.
- [ ] **F3** Clean and lower is outdated. Clean and equal is current. Clean
  and higher is skipped.
- [ ] **F4** Build `0`, a missing version, an offline board and
  `board_id < 0` are visible and excluded. Each exclusion carries a reason.
- [ ] **F5** The selected order is ascending board id. A 31st discovery is a
  table-full fault and is not stored. `SS_MAX_BOARDS` stays 30.
- [ ] **F6** Two manifests, same build, different SHA-256: the queue refuses
  to start.
- [ ] **F7** After confirmation the log contains one `ENTER_BOOTLOADER`, for
  the current board id only. It contains no field 127 and no
  `QUERY_UNASSIGNED`. The next board's command is absent until this board
  reaches done or failed.
- [ ] **F8** `GET_CANBUS_ID` differing from the row UUID aborts with no
  `SEND_BLOCK` in the log.
- [ ] **F9** A read-back mismatch schedules the previous image and does not
  mark the new image complete.
- [ ] **F10** A post-update version whose build differs marks that board
  failed and does not start the next board.
- [ ] **F11** `STOP` during a block lets that board reach a terminal state
  and does not dequeue the next board.
- [ ] **F12** A failed board stops the queue. Later boards stay not-started.
- [ ] **F13** Rebuilding the set after a reset does not resume a stored
  queue. A board whose build now equals the image drops out. A board with
  no version is excluded.
- [ ] **F14** The show-running flag refuses the first `ENTER_BOOTLOADER`.
- [ ] **F15** Tile colour comes from the queue state. Outdated and waiting
  is red. The active phase (`entering`, `writing`, `verifying`,
  `rebooting`) is yellow. Verified is green. Excluded is dim. Failed is
  the failed colour. A snapshot of 30 boards contains 30 tiles.
- [ ] **F16** While the queue is running, exactly one tile is yellow. Before
  start, every outdated tile is red and none is yellow. After the last
  board verifies, none is yellow.

Gate: `motion_core_tests` exits 0. No LVGL and no TWAI in this library.

## Phase R5 — node firmware stamp and version answer

Host tests from R1 and R3 are already green. This phase is the firmware
that uses them.

- [ ] **N1** `pio run -e fly_micro4` and `pio run -e fly_micro4_katapult`
  succeed with `-DSS_FW_VERSION=\"1.4.2\" -DSS_BUILD_ID=1794839525`.
  The Katapult env stays linked at `0x10004000`.
- [ ] **N2** A `VERSION_REQ` to the board's id and to field 127 produces one
  `VERSION_RSP` via `mc_pack_version_rsp`, delayed by
  `1 ms × (board_id mod 10)`. The bench review of that delay is a comment
  next to the existing whitepaper rule, not a new timing number.
- [ ] **N3** A dirty bench build sets `MC_VERSION_FLAG_DIRTY`. The flags used
  by the workflow omit it.
- [ ] **N4** Action 4 disables the four drivers, then calls the R3 check. On
  success it writes the request and resets. On failure it stays in the
  application. Review this function; do not execute the reset on the host.
- [ ] **N5** Do not upload.

Gate: both PlatformIO envs build. Host tests still exit 0.

## Phase R6 — panel list, config version 2, upgrade screen

`ss_config_t` is version 1 and keys rows by an 8-byte flash id. Version 2
keys them by the 6-byte UUID and adds the last confirmed build, flags and
upgrade result. A version-1 file loads as defaults for those new fields
(version unknown) and keeps the grid only when the migration can name a
UUID; otherwise the positions are kept only if a test says how the 8-byte
id maps. The first implementation loads version 1 as "version unknown" and
does not invent UUIDs for old rows: the operator re-assigns. State that in
the config comment.

- [ ] **G1** Pack and unpack a version-2 record. A version-1 blob does not
  pass the version-2 check and does not yield a board build.
- [ ] **G2** `QUERY ALL` clears `version_valid`, waits, and retries only the
  missing board ids, three times. The wait is a supplied `now_ms`, not a
  sleep. After three misses the row is unknown and F4 excludes it.
- [ ] **G3** The upgrade screen calls `fleet_core` for `UPGRADE ALL
  OUTDATED`. `UPGRADE SELECTED` uses the same queue with one tile.
- [ ] **G4** The host PNG for the upgrade screen shows 30 tiles in 6 columns
  and 5 rows, with no second page. The fixture has boards 00 and 01 green,
  board 02 yellow and labelled with a write percentage, board 25 dim, and
  the rest red. The header shows the image build and `n / 30`. Review
  `esp32gui/host/png/output_*.png` at 800 × 480.
- [ ] **G5** `pio run -e esp32usb` and `pio run -e esp32can` succeed. The
  release flags are not on `esp32usb`. Do not upload.

Gate: host tests exit 0, both panel envs build, PNGs reviewed.

## Phase R7 — SD select and panel OTA policy

The writer that calls `esp_ota_*` stays in `esp32gui/`. The accept/reject
decisions stay in `release_core`, already tested in A5–A8.

- [ ] **S1** A directory listing fixture with two valid panel manifests
  selects the higher build. A broken SHA-256 is ignored. A factory manifest
  is listed for the service screen and is not selectable for OTA.
- [ ] **S2** The show-running flag and an active queue each refuse the panel
  OTA start.
- [ ] **S3** `partition_table_sha256` mismatch refuses before any write
  callback. The test records whether the write callback ran.
- [ ] **S4** Self-test failure leaves the slot unmarked. The test asserts the
  "mark valid" callback is skipped. Rollback itself is the ESP-IDF
  bootloader; do not reimplement it.
- [ ] **S5** Release compile of `esp32can` defines `SS_RELEASE`, does not
  define `SS_CAN_MOCK`, and fails the preprocessor check when both are set.
  The mock inject control is absent from that build's screen source.

Gate: host tests exit 0. `pio run -e esp32can` with `SS_RELEASE` builds.
Do not upload.

## Phase R8 — `.github/workflows/release.yml`

This is the first phase that can publish, because R5 and R7 have stamped
images. Do not push a `v*` tag until this workflow is on `main` and a dry
run of its build steps has succeeded locally.

A host test reads the workflow file as text and locks the contract. It does
not run Actions.

- [ ] **W1** The file triggers on `push` tags matching
  `v[0-9]+.[0-9]+.[0-9]+` and has no `workflow_dispatch`.
- [ ] **W2** Permissions are `contents: write`. The file contains no personal
  access token and no secret name other than `GITHUB_TOKEN`.
- [ ] **W3** `motion_core_tests` appears before `pio run`, and `pio run`
  appears before `gh release create`.
- [ ] **W4** The PlatformIO steps use `fly_micro4_katapult` and `esp32can`,
  pass `SS_RELEASE`, and do not pass `SS_CAN_MOCK`.
- [ ] **W5** `gh release create` is the publish step. The file contains no
  `gh release delete` and no `gh release edit` that would replace assets.
- [ ] **W6** Local dry run, recorded in the phase notes: cmake tests exit 0,
  both `pio run` invocations exit 0, `release_pack` writes a directory from
  that commit's `%ct`. Do not pass `--dirty`. Do not create a release from
  the workstation.
- [ ] **W7** The workflow checks that the tag is annotated and that its
  message is non-empty, and it stops when a release for that tag already
  exists or the build id is not greater than the newest `release.json`.

Gate: W1–W5 pass in `motion_core_tests`. W6 has been run locally. The first
real tag is pushed only when a maintainer asks for that release.

## Phase R9 — WiFi and browser upload

Still host-tested policy first. Firmware after the tests. WPA2-PSK, HTTP,
and the Settings enable switch are closed in the whitepaper §7.1. The
server starts off. The switch lives in RAM and is not part of the config
record.

- [ ] **B1** Link states `OFF`, `AP_SETUP`, `SCANNING`, `CONNECTING`,
  `CONNECTED`, `FAILED` follow the table in the whitepaper §3.7. A wrong
  password reaches `FAILED` and can return to `AP_SETUP`. `FORGET` clears
  the credential flag.
- [ ] **B2** The setup window supplied as `now_ms` expires at 10 minutes and
  leaves `AP_SETUP`.
- [ ] **B3** An upload with the server off, a bad password, a running show,
  a body over the size limit, or a bad manifest is refused. The test records
  that the OTA write callback did not run.
- [ ] **B6** The enable flag is false at boot, becomes true only after the
  Settings action, and is absent from the version-2 config bytes. A modelled
  power cycle clears it.
- [ ] **B4** A valid panel bundle streams through the write callback and
  hashes to the manifest. A Fly bundle is stored as an SD image and is not
  an OTA write. No SD card refuses the Fly bundle.
- [ ] **B5** Firmware task split: WiFi and HTTP are not on the CAN task and
  not in the LVGL draw path. Review the task creation. Credentials are
  written to NVS and are absent from the status JSON fixture.

Gate: host tests exit 0, then `pio run -e esp32can` with `SS_RELEASE`.
Do not upload. Screen PNGs if the Settings screen gains a setup control.

## Phase R10 — panel GitHub client

The client parses a release the workflow already publishes. It does not
install by itself.

- [ ] **H1** A fixture `release.json` with a higher build is "update
  available". An equal or lower build is not offered.
- [ ] **H2** A bad SHA-256, a wrong target or `kind: factory` offered as the
  OTA asset aborts before the write callback.
- [ ] **H3** SNTP-invalid disables the pull and sets a reason the Settings
  snapshot can show. The SD path does not consult that flag.
- [ ] **H4** The parsed owner and repository come from the build flags. A
  response whose asset URL is outside the GitHub release host is rejected.
  The test uses fixture URLs, not the network.
- [ ] **H5** Confirm, then the same OTA and SD stores as R7 and R9. Cancel
  leaves both untouched.

Gate: host tests exit 0, then the release env builds. Do not upload.

---

## What this plan does not do

- It does not flash a board. Upload stays a separate, explicit request.
- It does not build the Katapult UF2.
- It does not move motion payloads, CAN identifiers or the cubic frame.
- It does not treat a green host test as a bus test. The first release tag
  is still a maintainer action after W6.

---

## Phase notes (implementation record)

Local dry run on 2026-11-16, commit `3a7d6db1`:

- `cmake --build build && ./build/motion_core_tests` — all host tests pass.
- `SS_FW_VERSION=1.4.2 SS_BUILD_ID=1794848847 SS_RELEASE=1 pio run -e
  fly_micro4_katapult` — success; the image links at `0x10004000`.
- `SS_FW_VERSION=1.4.2 SS_BUILD_ID=1794848847 SS_RELEASE=1 pio run -e
  esp32can` — success.
- `release_pack` wrote a clean directory from the commit's `%ct` and the
  archive's `partitions.bin` SHA-256, without `--dirty`.
- The 800 × 480 upgrade PNG was reviewed: 30 tiles, 6 × 5, boards 00 and 01
  green, 02 yellow at 46 %, 25 dim, the rest red, header `2/30`.

Bench flash (explicit operator permission): the panel boots the release
image with TWAI at 1 Mbit/s, loads config version 2, discovers the node by
UUID and assigns board 0. The node boots the stamped application and answers
on CAN. The release workflow was not pushed and no GitHub Release was
created.

### Bench correction (2026-11-16)

The hardware run found three details that the earlier text did not have:

- Katapult's admin `SET_NODEID` takes an **8-bit** node byte. Node 372
  cannot be encoded; `116` gives the real data pair `0x1E8` / `0x1E9`, both
  invalid show identifiers. The addressing and Katapult documents now use
  that pair.
- RP2040 Katapult's vector table is at `0x10000100` (stage-2 occupies the
  first 256 bytes). The CanBoot signature is at `reset_vector - 9` **bytes**.
- Katapult jumps to `0x10004000` when the first word there is non-zero and
  not `0xFFFFFFFF`. After a first install over a stale application, that
  word must be invalidated, or Katapult jumps into the old image instead of
  staying on CAN.

Verified on the bench: app action 4 entered Katapult (CAN stayed error-free,
node traffic stopped), then the panel wrote 1470 blocks with `SEND_BLOCK`,
read them back, sent `COMPLETE`, and the restarted app answered
`VERSION_RSP` with `1.4.2` build `1794848847`. A second CAN pass restored the
app after the action-4 entry.

### Queue integration and env gate (2026-11-16)

- `esp32gui/src/upgrade_manager.cpp` builds the set with `fl_build_set`,
  starts from `UPGRADE ALL OUTDATED` or `UPGRADE SELECTED`, and drives
  `katapult_flash_ex` one board at a time. The worker owns only the Katapult
  byte stream; the main loop waits for `ASSIGN`, sends `VERSION_REQ`, checks
  the build and writes the green/failed tile.
- Bench run: board 0 went from build `1794848903` to `1794848904` through
  the queue. The tile reached `done`, `UPGRADE` reported `complete: 1/1`, and
  the config record held `build 1794848904 / done`.
- The post-upgrade result is held in RAM and marks `config_dirty`; the
  operator's Save persists it. Calling `SPIFFS.open` from the upgrade path
  after LVGL can abort on a fragmented internal heap, so the queue does not
  force a save at completion. This is a follow-up, not a release gate.
- Bench console and embedded image are gated by `SS_BENCH_CONSOLE` and
  `SS_FLY_IMAGE`. `esp32can` (release) contains neither; `esp32can_bench`
  contains both. The host text check in W1–W5 still applies.
- The Katapult byte pair is locked by a host assertion: node byte 116 gives
  `0x1E8/0x1E9` (`mc_katapult_data_id`).

### SD source and panel OTA writer (2026-11-16)

- `firmware_image.cpp` mounts the microSD card through the CH422G IO4
  chip-select, scans `/firmware/fly` and `/firmware/panel`, parses each
  manifest with `rc_manifest_parse`, hashes the file and uses
  `pc_image_select_fly` / `pc_image_select_panel`. A card that is absent or
  unreadable leaves the embedded bench image in place.
- `panel_ota.cpp` checks the manifest, target, size, busy state and partition
  table hash with `pc_ota_check`, writes the inactive slot with `esp_ota_*`,
  sets the boot partition and restarts. The boot path calls
  `panel_ota_boot_check`; a pending image is marked valid after the display
  and CAN are up, otherwise `esp_ota_mark_app_invalid_rollback_and_reboot`.
- Bench console: `image` reports the selected source, `panelota` starts the
  panel write. The Settings screen control is still pending.
- The no-card path was exercised on the bench; a card with the directory
  layout has not been inserted yet.

### W6 dry run (2026-11-16)

On clean commit `bc954a62`, build id `1794858657`:

- `cmake -S tests -B build && cmake --build build && ./build/motion_core_tests`
  — 187 tests pass.
- `SS_FW_VERSION=1.4.2 SS_BUILD_ID=1794858657 SS_RELEASE=1 pio run -d
  rp2040 -e fly_micro4_katapult` — success.
- Same flags for `pio run -d esp32gui -e esp32can` — success; no
  `SS_BENCH_CONSOLE`, no embedded Fly image.
- `release_pack` wrote the Fly app, the panel app/factory pair, their
  manifests and `release.json` to `/tmp/ss-dist`. The filenames carry the
  tagged commit's UTC and `git8`.
- No tag was pushed and no GitHub Release was created from the workstation.

### R9 firmware on the bench (2026-11-16)

- `esp32gui/src/net_manager.cpp` owns WiFi in a separate core-0 task.  It
  loads credentials and the admin salt/hash from NVS, raises the setup AP,
  scans, connects, starts SNTP and runs the HTTP server in that task.
- Settings gained SETUP, SCAN, FORGET and SERVER controls plus the AP
  SSID/key rows.  The `wifi` console command mirrors the same actions for
  the bench.
- HTTP endpoints: `GET /`, `GET /api/status`, `POST /api/scan`,
  `/api/wifi`, `/api/service`, `/api/upgrade/all`,
  `/api/update/panel` and `/api/images/fly`.  Uploads are multipart
  `json` + `bin`; admin Basic auth and a per-session CSRF token gate the
  write paths.  Panel uploads call `panel_ota_apply`; Fly uploads call
  `firmware_image_store_fly`.
- Bench result: no credentials -> `OFF`; SETUP raised
  `SuspendedSpheres-5A30` at `192.168.4.1`, service on, and a station scan
  found 9 nearby networks.  The Mac in this session did not list the soft AP
  in its scan, so association and the HTTP body were not exercised from a
  second client.  That is an environment/hardware check, not a host test.
- The HTTP server was exercised over loopback (`webget /api/status` and
  `webget /`): `200 OK`, the status JSON and the 676-byte setup page with its
  CSRF token.  Every upload re-checks SHA-256 before `panel_ota_apply` or
  `firmware_image_store_fly`.
- The panel now has a **WIFI** screen: SCAN fills a 12-entry SSID list,
  tapping a row selects it, the password textarea opens the LVGL keyboard,
  JOIN submits, SET ADMIN stores the salted admin hash, and FORGET clears the
  credentials.  The full content height is used: the network list runs down
  the left side and the selected/password/admin/action block down the right.
  SCAN disables while a scan runs and the header shows `SCANNING...`; the
  keyboard's ready key submits the active field.  The web setup page remains
  the same alternative.
- R10 (GitHub pull) is in the tree; see the R10 note below.
- LVGL object memory now uses PSRAM via `lv_mem_custom.cpp`; the internal
  heap after setting up the AP was about 120 KiB, versus 2 KiB before that
  change.

### R10 firmware (2026-11-16)

- `esp32gui/src/github_client.cpp` uses `WiFiClientSecure` with the built-in
  CA bundle and `HTTPClient` to read
  `.../releases/latest/download/release.json` from the pinned
  `SS_GITHUB_OWNER`/`SS_GITHUB_REPO` build flags.
- SNTP must be valid and the link connected; otherwise the pull is refused
  with a reason.  The parsed release is compared with `SS_BUILD_ID` by
  `net_github_check`.
- Install downloads the panel app manifest/bin and the Fly manifest/bin from
  `.../releases/download/<tag>/<asset>`, rejects an asset URL outside the
  pinned repository, verifies SHA-256, stores the Fly bundle on SD and applies
  the panel image with `panel_ota_apply` (reboot).
- Settings has **CHECK UPDATES**; the web control page has check and
  install (the POST is the confirmation); the bench console has
  `github check|install|status`.
- The panel WIFI screen and keyboard are compiled and shown in the host PNG;
  touch/keyboard interaction on the panel has not been exercised yet.
- Not exercised against GitHub in this session: the setup AP was started and
  the panel scanned 9 networks, but the Mac did not list the soft AP.  R10 is
  therefore compile- and policy-verified, not network-verified.

### Final W6 dry run (2026-11-16, commit `19188357`)

- Host tests: 187 pass.
- `SS_FW_VERSION=1.4.2 SS_BUILD_ID=1794863612 SS_RELEASE=1` built
  `fly_micro4_katapult` and `esp32can`.
- The release `esp32can` image contains no `panel commands` string and no
  embedded Fly image.
- `release_pack` wrote `/tmp/ss-dist-final`: Fly app, panel app/factory pair,
  manifests and `release.json` for both targets at build `1794863612`.
- GitHub live check against `gin66/suspended-spheres` returned `HTTP 404`
  because the repository has no GitHub Release yet.  WiFi, SNTP and the TLS
  request path therefore work; the install path still needs a published
  release.
