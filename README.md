# Suspended Spheres — Kinetic Sculpture

A real-time kinetic art installation featuring **100–120 hollow chrome-finished plastic spheres** (Ø 60–80 mm, ≤ 50 g each) suspended in a grid (10 × 10 or 10 × 12, layout a proposal), each driven by an independent motor. The spheres move vertically over ~2 m, very slowly and smoothly, producing flowing waves, interference patterns, and choreographed visual symphonies. The system has two parts: a PC pattern-development and 3D-simulation environment, and the physical motor installation.

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
| [**Risk Register**](doc/RISK.md) | Dynamic loads, noise, architecture risks, and mitigations |
| [**AGENTS.md**](AGENTS.md) | Mission and development-environment notes |

---

## Repository Structure

```
Suspended_Spheres/
├── README.md                   # This file — project overview and entry point
├── AGENTS.md                   # Mission, overview, development notes
└── doc/
    ├── system_requirements.md
    ├── mechanical_analysis.md
    ├── controller_analysis.md
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
| **Controller Hardware** | ⏳ Planned |
| **Firmware** | ⏳ Planned |
| **Simulation** | ⏳ Planned |
| **Prototype** | ⏳ Planned |

---

*Built with curiosity, physics, and a love of kinetic art.*
