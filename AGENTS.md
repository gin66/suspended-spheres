# AGENTS.md — Suspended Spheres

> Mission and development-environment notes.
> System decisions, calculations, trade studies, requirements, and risk work live in `doc/`.

---

## 1. Mission

Suspended Spheres is a real-time kinetic art installation: 100–120 hollow chrome-finished plastic spheres (Ø 60–80 mm, ≤ 50 g) suspended in a grid (10 × 10 or 10 × 12, layout a proposal), each independently driven vertically over ~2 m, very slowly and smoothly, together producing travelling waves, interference patterns, and choreographed sequences. The system is delivered as a PC pattern-development/3D-simulation environment plus the physical installation. The installation occupies roughly 3 × 3 m (possibly 3 × 3.5 m) under a 3 m ceiling; the supporting steel structure is the designer's responsibility.

The visual result is the product. Motion must look smooth, stay synchronized across the grid, and remain safe when something fails.

---

## 2. Repository Structure

```
Suspended_Spheres/
├── README.md                   # Project overview and entry point
├── AGENTS.md                   # This file — mission and dev notes
├── lib/
│   ├── motion_core/            # Shared integer core (PC and firmware)
│   │   ├── include/motion_core.h
│   │   └── src/motion_core.cpp
│   ├── pattern_core/           # Pattern bake, limits, report, .hbp export, live reload
│   ├── release_core/           # SHA-256, tag/filename, manifest, selectors
│   ├── fleet_core/             # Outdated set, Katapult queue, tile colour
│   ├── panel_core/             # Config v2, version retry, SD list, OTA policy
│   └── net_core/               # WiFi/web/GitHub release host policy
├── tests/                      # Host-only harness (CMake, no hardware)
│   ├── CMakeLists.txt
│   ├── assert.h
│   ├── test_runner.cpp
│   └── abi_c.c
├── tools/                      # pattern_check and release_pack
├── .github/workflows/          # release.yml: the only tag publisher
├── wasm/                       # Native/Emscripten parity driver
├── webapp/                     # Vite + Three.js designer viewer
├── patterns/                   # .anim.json sources and the golden sine
├── rp2040/                     # Fly-Micro4 firmware-only PlatformIO project
│   ├── platformio.ini
│   └── src/main.cpp
├── esp32gui/                   # Waveshare 7" show controller + LVGL UI
│   ├── platformio.ini
│   ├── src/                    # board bring-up, CAN boundary, commission, log
│   ├── ui/                     # LVGL screens, no ESP-IDF includes
│   └── host/                   # Mac CMake harness, writes screen PNGs
├── boards/                     # Fly-Micro4 vendor files and the pin reference
└── doc/
    ├── system_requirements.md  # Behavioural and non-functional requirements
    ├── mechanical_analysis.md  # Physics: speed, force, thread, inertia, drum, frame
    ├── controller_analysis.md  # Platforms, drivers, stepping, bus, clock drift
    ├── motion_streaming_whitepaper.md  # Motion payload and node playback
    ├── motion_streaming_tdd.md         # Host tests that need no hardware
    ├── tmc2209_characterization.md     # TMC2209/stepper speed, stall, SG test plan
    ├── esp32gui_whitepaper.md          # Waveshare panel: show controller/UI
    ├── memory_map.md                   # RAM, flash and PSRAM budget
    ├── Fly_Micro4_addressing.md        # Node identity and CAN-ID allocation
    ├── katapult_whitepaper.md          # CAN bootloader and in-place firmware update
    ├── firmware_release_whitepaper.md  # Image identity, panel load, fleet upgrade
    ├── firmware_release_tdd.md         # Host tests for that release contract
    └── RISK.md                 # Risk register and technical review
```

All technical Markdown belongs in `doc/`. Only `README.md` and `AGENTS.md` sit at the repository root. Vendor board files and the pin reference stay in `boards/`. PC build and test code lives in `tests/`; `rp2040/` and `esp32gui/` are firmware-only. The `esp32gui/` build has explicit WiFi modes, network OTA and browser upload, with credentials in NVS and no `.private` secrets; the onboarding and web/GitHub update path is in `doc/firmware_release_whitepaper.md` §3.7–§3.9 and the UI contract in `doc/esp32gui_ui_design.md`. The shared core in `lib/motion_core/` is the only source exchanged between the firmware builds.

---

## 3. Development Environment

- **Host build (CMake).** The PC harness has no Pico SDK, no PIO and no CAN.
  `release_pack` is built beside `pattern_check`:
  ```sh
  cmake -S tests -B build
  cmake --build build
  ./build/motion_core_tests
  ```
  The binary exits 0 when all host assertions pass. There is no hardware attached; no test uploads or runs on a board.
- **Firmware build (PlatformIO).** `rp2040/` is firmware-only:
  ```sh
  cd rp2040 && pio run
  ```
  PlatformIO finds the shared core through `lib_extra_dirs = ../lib`; do not copy `motion_core` into `rp2040/`.
- **TMC2209 bring-up console.** USB serial on the Fly-Micro4 accepts `help`.
  Current and StallGuard commands are in `doc/motion_streaming_whitepaper.md`
  §5.6.1. It can command motion and is a bench tool, not a production interface.
- **BOOTSEL over USB.** The same Fly-Micro4 console has `bootsel`, which calls
  `reset_usb_boot(0, 0)` and enumerates `RPI-RP2` with no BOOT-button press.
  This works only while an application is running; a dead board still needs
  the physical BOOT gesture. Load a Katapult payload with
  `picotool load <file.uf2>`, or copy the UF2 to `/Volumes/RPI-RP2`. A UF2
  linked at `0x10004000` (env `fly_micro4_katapult`) keeps Katapult; one
  linked at `0x10000000` removes it. After installing Katapult, invalidate
  the old first word at `0x10004000` if no valid app is present, otherwise
  Katapult jumps into the stale image and does not stay on CAN.
- **Show-controller firmware (PlatformIO).** `esp32gui/` is the Waveshare 7"
  panel. `esp32usb` is the bench build, `esp32can` is the release build, and
  `esp32can_bench` adds the serial loader and an embedded Fly image for the
  bench:
  ```sh
  pio run -d esp32gui -e esp32usb
  SS_FW_VERSION=1.4.2 SS_BUILD_ID=1794848904 SS_BENCH_CONSOLE=1 \
    SS_FLY_IMAGE="$PWD/rp2040/.pio/build/fly_micro4_katapult/firmware.bin" \
    pio run -d esp32gui -e esp32can_bench -t upload \
    --upload-port /dev/cu.usbmodem5C930885641
  ```
  `esp32can` never defines `SS_CAN_MOCK` or `SS_BENCH_CONSOLE`; its image has
  no serial loader and no embedded Fly image. The bench console's
  `panelota` command and the Settings **PANEL UPDATE** button write the SD
  panel image to the inactive OTA slot and reboot; the boot self-test marks
  it valid or rolls it back. Settings **SETUP** starts the per-device setup AP
  (`SuspendedSpheres-XXXX`, key shown on the panel) and the HTTP service;
  **SCAN**, **FORGET** and **SRV** control the same state. Credentials and
  the admin hash live in NVS. The setup page is `http://192.168.4.1` while the
  AP is up. NETWORK **AP/SCAN/FORGET/SRV** and the **NETWORK** screen
  (SSID list, full-screen password/admin keypad with DONE/CANCEL, SET ADMIN)
  are the panel path. The bench
  console has `wifi ap|off|forget|scan|retry|status|list|key`, `service on|off`,
  `github check|install|status` and `panelota`.  The GitHub owner/repository
  are compiled from `SS_GITHUB_OWNER`/`SS_GITHUB_REPO`; local builds default
  to `gin66/suspended-spheres` and the release workflow overrides them from
  the repository that ran the tag, so no extra variables need to be
  configured.  An empty value disables the pull. LVGL objects, the UI snapshot and the CAN scratch copy are in
  PSRAM so internal DRAM remains for lwIP and the WiFi driver. `release_gate.h` rejects a
  build that sets both `SS_RELEASE` and `SS_CAN_MOCK`. Never upload or flash
  without an explicit request.
- **Screen preview (CMake, no hardware).** The host harness builds the same
  `ui/ui.cpp` and writes one PNG per screen:
  ```sh
  cmake -S esp32gui/host -B esp32gui/host/build
  cmake --build esp32gui/host/build
  ./esp32gui/host/build/esp32gui_host esp32gui/host/png
  ```
  Review `esp32gui/host/png/output_*.png` before treating a screen change as
  done. The harness walks the live LVGL tree after every render and exits
  non-zero on overlapping widgets or content crossing the rail; a clean run
  prints `layout audit: clean`. The harness defaults to the LVGL tree already
  downloaded for the house panel; override with
  `-DLVGL_ROOT_DIR=/path/to/lvgl`.
- **Native/WASM parity (optional).** Build the same driver natively and with
  Emscripten, then compare the transcripts:
  ```sh
  ./wasm/build.sh
  ./tests/wasm_parity.sh
  ```
  Without `emcc` the script builds the native side and reports that the WASM
  driver is absent. Rebuild the browser core, which is checked in so the viewer
  runs without Emscripten, with `./wasm/build_viewer.sh`.
- **Pattern authoring loop.** Patterns live in `patterns/<name>.anim.json`;
  schema and layer semantics are in the whitepaper §6.9. After editing or
  generating one, run:
  ```sh
  cmake --build build
  ./build/pattern_check patterns/<name>.anim.json
  ```
  It writes `patterns/<name>.report.json` and exits non-zero on violations.
  Add `--hbp patterns/<name>.hbp` to export a clean take as one HBP2 playback
  file.  `tools/export_sd_animations.sh` bakes every pattern into
  `sd/animations/*.hbp` for copying to the panel's `/sdcard/animations`; the
  control page has a **STORE ANIMATION** upload for the same folder. With `npm run dev`
  in `webapp/`, the viewer lists the files in `patterns/`, watches the selected
  one, and rebakes on save. The agent loop is: edit the JSON or generator, run
  `pattern_check`, read the report, repeat until `violations` is empty.
- **Firmware release.** Pushing an tag `vMAJOR.MINOR.PATCH` is the
  publish action.  The workflow drops the Arduino RainMaker, Zigbee and
  diagnostics managed components with `tools/drop_optional_components.py`
  before the panel build; locally, run that script after `pio pkg install`
  if the optional components are not wanted. `.github/workflows/release.yml` creates the GitHub Release
  after the host tests pass. The contract is
  `doc/firmware_release_whitepaper.md` §1.6. The test order is
  `doc/firmware_release_tdd.md`.
- **Markdown + SI units.** Use metres, seconds, kilograms, newtons, and watts. Keep tables aligned with the surrounding style.
- **Source of truth:** requirements in `doc/system_requirements.md`; physics in `doc/mechanical_analysis.md`; platform and bus decisions in `doc/controller_analysis.md`; motion payloads in `doc/motion_streaming_whitepaper.md`; node identity and CAN-ID allocation in `doc/Fly_Micro4_addressing.md`; CAN bootloader and in-place update in `doc/katapult_whitepaper.md`; image identity, the tag-triggered GitHub Release, panel image loading and the fleet upgrade queue in `doc/firmware_release_whitepaper.md`; the host test order for that contract in `doc/firmware_release_tdd.md`; the internal-RAM, flash and PSRAM budget in `doc/memory_map.md`; the host test plan in `doc/motion_streaming_tdd.md`; the TMC2209/stepper characterization and StallGuard test plan in `doc/tmc2209_characterization.md`, with measured results and per-session evidence in `doc/bench/tmc2209/`; risks in `doc/RISK.md`.
- **No duplicated calculations outside `doc/`.** `AGENTS.md` and `README.md` describe the project; they do not carry datasheets, torque tables, or protocol frames.
- **Verification.** Run the host tests for every core change; cross-check any changed number against the derivation in the relevant analysis document.
- **No dedicated software engineer.** Firmware and software are generated by an LLM; a software consultant provides review and sign-off. Prefer clear, reviewable code and explicit real-time assumptions.
- **No requirement IDs outside `doc/system_requirements.md`.** If another document cites a requirement, keep the ID and the requirement text in sync.

### JLCPCB availability check (authoritative)

Do not judge availability by LCSC retail stock and do not trust the EasyEDA `JLCPCB Part Class` property alone. Query the JLCPCB parts API directly — it needs no auth and reports the stock that assembly actually consumes:

```sh
curl -s -X POST \
  'https://jlcpcb.com/api/overseas-pcb-order/v1/shoppingCart/smtGood/selectSmtComponentList' \
  -H 'Content-Type: application/json' \
  -d '{"currentPage":1,"pageSize":5,"keyword":"C1525","searchSource":"search"}'
```

Read `data.componentPageInfo.list[].stockCount` and `.componentLibraryType` (`base` = JLC Basic, `expand` = Extended). A part is orderable when `stockCount > 0`; LCSC's `inventoryLevel` and JLCPCB's `stockCount` are different warehouses, so a part showing LCSC 0 (e.g. some Basic parts) can still be fully stocked for assembly.

- Run the check for every LCSC C-number in the BOM before releasing a board revision.
- Treat `stockCount` below roughly 100 as a supply risk worth an alternate part; below ~10 it is a blocker.
- Resolve MPN → C-number with `easyeda lib search --query <MPN>` or `easyeda lib by-lcsc --lcsc <C-number>`.
