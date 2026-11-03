# Suspended Spheres — Kinetic Sculpture

A real-time kinetic art installation featuring **100–120 hollow chrome-finished plastic spheres** (Ø 60–80 mm, ≤ 50 g each) suspended in a grid (10 × 10 or 10 × 12, layout a proposal), each driven by an independent motor. The spheres move vertically over ~2 m, very slowly and smoothly, producing flowing waves, interference patterns, and choreographed visual symphonies. The system has three parts: a PC pattern-development and 3D-simulation environment, a Waveshare 7" ESP32 panel that runs the show and the operator UI, and the physical motor installation.

```
  ┌──────────────────────────────────────────┐
  │  ROW 1  :  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  │  ← 10 balls per row
  │  ROW 2  :  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  │
  │  ...                                     │
  │  ROW 12 :  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  │
  │                                          │
  │  12 rows × 10 columns = 120 balls        │
  │  Vertical travel: ~2000 mm (2 m)        │
  └──────────────────────────────────────────┘
```

---

## Quick Specs

| | |
|---|---|
| **Balls** | 100–120 hollow chrome-finish plastic (Ø 60–80 mm, ≤ 50 g) |
| **Grid** | 10 × 10 or 10 × 12 (layout proposal) |
| **Travel** | ~2000 mm (2 m) |
| **Footprint** | ~3 × 3 m (3 × 3.5 m possible), under a 3 m ceiling |
| **Motion** | mostly very slow and smooth; nominal fast tier 2 m in 4–5 s, optional short effects 2–3 s |

> The piece is designed for slow, hypnotic motion, with a faster tier for occasional effects. Free-fall and high-speed catch behaviour are not required.

---

## Documentation

All technical Markdown lives in [`doc/`](doc/). Only this file and [`AGENTS.md`](AGENTS.md) sit at the repository root.

| Document | Description |
|----------|-------------|
| [**System Requirements**](doc/system_requirements.md) | Formal functional and non-functional requirements, interfaces, constraints, assumptions |
| [**Mechanical Analysis**](doc/mechanical_analysis.md) | Speed ceiling, force/torque budgets, thread dynamics, inertia, drum and frame design |
| [**Controller Analysis**](doc/controller_analysis.md) | Platform and driver selection, stepping resolution, bus budget, clock drift |
| [**Motor Selection**](doc/motor_selection.md) | Trade study of cheap motor variants against the requirement set |
| [**Motion Streaming White Paper**](doc/motion_streaming_whitepaper.md) | Fly-Micro4 motion payload, RP2040/FastAccelStepper implementation, PC simulation architecture |
| [**ESP32 panel white paper**](doc/esp32gui_whitepaper.md) | Waveshare 7" ESP32-S3 show controller and LVGL operator UI; sources in `esp32gui/` |
| [**Motion streaming host TDD**](doc/motion_streaming_tdd.md) | Test-first plan for the motion core and the designer loop, with no hardware |
| [**Fly-Micro4 addressing**](doc/Fly_Micro4_addressing.md) | Node identity, commissioning and CAN-ID allocation for the selected board |
| [**Risk Register**](doc/RISK.md) | Dynamic loads, noise, architecture risks, and mitigations |
| [**Fly-Micro4 GPIO Map**](boards/Fly-Micro4/Fly-Micro4.h) | RP2040 pin assignments for the motion controller board |
| [**AGENTS.md**](AGENTS.md) | Mission and development-environment notes |

---

## Repository Structure

```
Suspended_Spheres/
├── README.md                   # This file — project overview and entry point
├── AGENTS.md                   # Mission, overview, development notes
├── lib/
│   ├── motion_core/            # Shared integer motion core (PC and firmware)
│   └── pattern_core/           # Pattern bake, limits, report and .hbp export
├── tests/                      # Host-only harness (CMake; no hardware)
├── wasm/                       # Native/WASM parity driver
├── rp2040/                     # Fly-Micro4 firmware (PlatformIO)
├── webapp/                     # Designer 3D viewer (Vite + Three.js)
├── patterns/                   # .anim.json example patterns
├── esp32gui/                   # Show controller and LVGL UI (not created yet)
├── boards/
│   ├── Fly_Micro4_pinmap.md         # Pin reference for the vendor files
│   ├── Fly_Micro4_klipper_reference.cfg
│   └── Fly-Micro4/
│       ├── Fly-Micro4.h            # GPIO map for firmware
│       ├── Fly-Micro4-schematic.pdf
│       ├── Fly-Micro4.step
│       └── pin.webp                # Connector pinout
└── doc/
    ├── system_requirements.md
    ├── mechanical_analysis.md
    ├── controller_analysis.md
    ├── motor_selection.md
    ├── motion_streaming_whitepaper.md
    ├── esp32gui_whitepaper.md     # Waveshare panel: show controller and UI
    ├── motion_streaming_tdd.md    # Host tests that need no hardware
    ├── Fly_Micro4_addressing.md   # Node identity and CAN-ID allocation
    ├── BOM_summary.md
    ├── BOM_priced.csv
    └── RISK.md
```

---

## Patterns

Sine Wave · Standing Wave · Spiral · Random Cascade · Music Reactive · Breathing · Diagonal Wave · Custom Choreography

---

## Run the 3D Viewer

The designer preview is the `webapp/` folder. No Git account is needed: you can
download the project as a ZIP, then open it in a browser. You do **not** need to
build anything by hand.

### 1. Download the project

1. Open <https://github.com/gin66/suspended-spheres> in a browser.
2. Click the green **Code** button, then **Download ZIP**.
3. Unzip the downloaded file (usually `suspended-spheres-main.zip`).
4. Open the unzipped folder and find the `webapp` subfolder.

### 2. Install Node.js (one time)

The viewer needs Node.js. Install the **LTS** version from
<https://nodejs.org/> and accept the default options.

To check the install, open a terminal and run:

```sh
node --version
npm --version
```

If either command is not found, close the terminal, open a new one, and try
again.

### 3. Start the viewer

Open a terminal **in the `webapp` folder**:

- **macOS:** right-click the `webapp` folder → **Services** → **New Terminal at Folder**
- **Windows:** Shift-right-click the `webapp` folder → **Open PowerShell window here**
- **Linux:** right-click inside the folder → **Open in Terminal**

Then run these two commands:

```sh
npm install
npm run dev
```

`npm install` downloads the 3D library and may take a minute. `npm run dev`
prints a local address, normally <http://localhost:5173/>. Open that address in
a browser. Leave the terminal open while you use the viewer; press `Ctrl+C` in
the terminal to stop it.

The shared integer motion core is prebuilt in `webapp/src/wasm/pattern_core.js`,
so you do not need Emscripten to run the viewer. After changing code under
`lib/`, rebuild it with `./wasm/build_viewer.sh`.

### 4. Use the viewer

- Click an animation under **Animations** on the left to switch to it.
- **Play / Pause**, **Loop**, the speed slider, and the timeline control the show.
- **Fidelity** switches between the ideal curve, the 16-bit wire values, and the microstep grid.
- **Load Pattern File** opens a `.anim.json` file. Try `patterns/golden_sine.anim.json` from the project folder.
- Drag to orbit the camera and scroll to zoom.
- Red spheres and the violation banner mark positions outside the allowed limits.

### If you cannot or do not want to install npm

If Python 3 is already installed, you can serve the same folder without Node.js.
In a terminal opened in `webapp/`, run:

```sh
python3 -m http.server 8000
```

Then open <http://localhost:8000/>. This works because the viewer loads Three.js
from a CDN, so an internet connection is required either way.

### Common problems

| Problem | Fix |
|---------|-----|
| `npm: command not found` | Install Node.js LTS, then open a new terminal |
| Blank page when opening `index.html` directly | Do not double-click the file; use `npm run dev` or the Python server above |
| Address already in use | `npm run dev -- --port 5174`, or pick another Python port |
| The 3D view does not load | Check the internet connection; Three.js comes from a CDN |

---

## Status

| Area | Status |
|------|--------|
| **Concept & Documentation** | ✅ Complete |
| **Mechanical Design** | 🔄 In Progress |
| **Controller Hardware** | Fly-Micro4 selected; desolder R39/R58 on every board. Waveshare CAN jumper set to 120 Ω; solder 120 Ω into a cable terminator |
| **Show controller** | Waveshare ESP32-S3-Touch-LCD-7 selected; sources will live in `esp32gui/` |
| **Firmware** | ⏳ Planned |
| **Simulation** | ✅ 3D viewer uses the shared WASM core; pattern export in progress |
| **Prototype** | ⏳ Planned |

---

*Built with curiosity, physics, and a love of kinetic art.*
