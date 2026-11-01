# Hanging Balls — Kinetic Sculpture

A real-time kinetic art installation featuring **120 polished steel balls** (50 g each) suspended in a 10 × 12 grid, each driven by an individual stepper motor. The balls move vertically over a 1 m range, producing flowing waves, interference patterns, and choreographed visual symphonies.

```
  ┌──────────────────────────────────────────┐
  │  ROW 1  :  ●  ●  ●  ●  ●  ●  ●  ●  ●  ● │  ← 10 balls per row
  │  ROW 2  :  ●  ●  ●  ●  ●  ●  ●  ●  ●  ● │
  │  ...                                    │
  │  ROW 12 :  ●  ●  ●  ●  ●  ●  ●  ●  ●  ● │
  │                                            │
  │  12 rows × 10 columns = 120 balls         │
  │  Vertical travel: 1000 mm (1 m)           │
  └──────────────────────────────────────────┘
```

---

## Quick Specs

| | |
|---|---|
| **Balls** | 120 × 50 g polished steel (Ø ≈ 46 mm) |
| **Grid** | 10 columns × 12 rows |
| **Travel** | 1000 mm (1 m) |
| **Speed ceiling** | 4.5 m/s (free-fall over 1 m is 4.43 m/s; 4.5 m/s is the rounded ceiling) |
| **Motor** | NEMA-17 stepper, 20–30 mm drum |
| **Controller** | ESP32-C5 (on-chip CAN-FD) or ESP32-S3 + MCP2518FD |
| **Drivers** | TMC5160 (SPI, StallGuard2) or TMC2209 |
| **Controllers** | 10 units (12 motors each, one per column) |
| **Bus** | CAN 2.0B at ≥ 1 Mbit/s; CAN-FD optional |
| **Thread** | Dyneema or Kevlar, 0.5 mm |

> 4.5 m/s bounds the system; it is not a speed every move must reach. A free-fall drop with a short braked catch is a legitimate motion.

---

## Documentation

All technical Markdown lives in [`doc/`](doc/). Only this file and [`AGENTS.md`](AGENTS.md) sit at the repository root.

| Document | Description |
|----------|-------------|
| [**System Requirements**](doc/system_requirements.md) | Formal functional and non-functional requirements, interfaces, constraints, assumptions |
| [**Mechanical Analysis**](doc/mechanical_analysis.md) | Speed ceiling, force/torque budgets, thread dynamics, inertia, drum and frame design |
| [**Controller Analysis**](doc/controller_analysis.md) | Platform and driver selection, stepping resolution, bus budget, clock drift |
| [**Risk Register**](doc/RISK.md) | Dynamic loads, noise, architecture risks, and mitigations |
| [**AGENTS.md**](AGENTS.md) | Mission, system overview, and development-environment notes |

---

## Repository Structure

```
Hanging_Balls/
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
