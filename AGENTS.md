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
│   └── pattern_core/           # Pattern bake, limits, report, .hbp export, live reload
├── tests/                      # Host-only harness (CMake, no hardware)
│   ├── CMakeLists.txt
│   ├── assert.h
│   ├── test_runner.cpp
│   └── abi_c.c
├── tools/                      # Headless pattern checker (pattern_check)
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
    ├── esp32gui_whitepaper.md          # Waveshare panel: show controller/UI
    ├── memory_map.md                   # RAM, flash and PSRAM budget
    ├── Fly_Micro4_addressing.md        # Node identity and CAN-ID allocation
    └── RISK.md                 # Risk register and technical review
```

All technical Markdown belongs in `doc/`. Only `README.md` and `AGENTS.md` sit at the repository root. Vendor board files and the pin reference stay in `boards/`. PC build and test code lives in `tests/`; `rp2040/` and `esp32gui/` are firmware-only. `esp32gui/` has no WiFi, OTA, Telegram or `.private` secrets. The shared core in `lib/motion_core/` is the only source exchanged between the firmware builds.

---

## 3. Development Environment

- **Host build (CMake).** The PC harness has no Pico SDK, no PIO and no CAN:
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
- **Show-controller firmware (PlatformIO).** `esp32gui/` is the Waveshare 7"
  panel. It builds with the mock CAN transport because the bus is not wired:
  ```sh
  cd esp32gui && pio run -e esp32usb
  ```
  Remove `-D SS_CAN_MOCK=1` from `platformio.ini` only when the TJA1051 and
  the 1 Mbit/s bus are connected. Never upload or flash without an explicit
  request.
- **Screen preview (CMake, no hardware).** The host harness builds the same
  `ui/ui.cpp` and writes one PNG per screen:
  ```sh
  cmake -S esp32gui/host -B esp32gui/host/build
  cmake --build esp32gui/host/build
  ./esp32gui/host/build/esp32gui_host esp32gui/host/png
  ```
  Review `esp32gui/host/png/output_*.png` before treating a screen change as
  done. The harness defaults to the LVGL tree already downloaded for the
  house panel; override with `-DLVGL_ROOT_DIR=/path/to/lvgl`.
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
  file. With `npm run dev`
  in `webapp/`, the viewer lists the files in `patterns/`, watches the selected
  one, and rebakes on save. The agent loop is: edit the JSON or generator, run
  `pattern_check`, read the report, repeat until `violations` is empty.
- **Markdown + SI units.** Use metres, seconds, kilograms, newtons, and watts. Keep tables aligned with the surrounding style.
- **Source of truth:** requirements in `doc/system_requirements.md`; physics in `doc/mechanical_analysis.md`; platform and bus decisions in `doc/controller_analysis.md`; motion payloads in `doc/motion_streaming_whitepaper.md`; node identity and CAN-ID allocation in `doc/Fly_Micro4_addressing.md`; the internal-RAM, flash and PSRAM budget in `doc/memory_map.md`; the host test plan in `doc/motion_streaming_tdd.md`; risks in `doc/RISK.md`.
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
