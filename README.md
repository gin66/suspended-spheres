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
│   └── motion_core/            # Shared integer motion core (PC and firmware)
├── tests/                      # Host-only harness (CMake; no hardware)
├── rp2040/                     # Fly-Micro4 firmware (PlatformIO)
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

## Status

| Area | Status |
|------|--------|
| **Concept & Documentation** | ✅ Complete |
| **Mechanical Design** | 🔄 In Progress |
| **Controller Hardware** | Fly-Micro4 selected; desolder R39/R58 on every board. Waveshare CAN jumper set to 120 Ω; solder 120 Ω into a cable terminator |
| **Show controller** | Waveshare ESP32-S3-Touch-LCD-7 selected; sources will live in `esp32gui/` |
| **Firmware** | ⏳ Planned |
| **Simulation** | ⏳ Planned |
| **Prototype** | ⏳ Planned |

---

*Built with curiosity, physics, and a love of kinetic art.*
