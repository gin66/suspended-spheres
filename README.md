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
| **Max speed** | 4.5 m/s (upward) / 4.43 m/s (downward, free-fall) |
| **Motor** | NEMA-17 stepper, 20–30 mm drum |
| **Controller** | ESP32-S3 with CAN-FD @ 1 Mbit/s |
| **Drivers** | TMC6300 (native CAN-FD) or TMC5160 |
| **Controllers** | 10 units (12 motors each, one per column) |
| **Thread** | Dyneema or Kevlar, 0.5 mm |

---

## Documentation

Detailed technical documents are in the [`doc/`](doc/) directory:

| Document | Description |
|----------|-------------|
| [**Controller Analysis**](doc/controller_analysis.md) | ESP32-S3 vs. Pico 2 vs. ATmega328P — CAN, stepping resolution (1/16 µ-step vs. I2S mux), driver comparison |
| [**System Requirements**](doc/system_requirements.md) | Formal functional & non-functional requirements, interfaces, constraints, traceability matrix |
| [**Mechanical Analysis**](doc/mechanical_analysis.md) | Physics verification of 4.5 m/s claim, force/torque budgets, thread dynamics, inertia, drum design |

## Project Files

| File | Description |
|------|-------------|
| [`AGENTS.md`](AGENTS.md) | Development guidelines, controller/motor selection, CAN protocol, firmware structure |
| [`README.md`](README.md) | This file — project overview and entry point |

---

## Patterns

Sine Wave · Standing Wave · Spiral · Random Cascade · Music Reactive · Breathing · Custom Choreography

---

## Status & Roadmap

| Phase | Status |
|-------|--------|
| **Concept & Documentation** | ✅ Complete |
| **Mechanical Design** | 🔄 In Progress |
| **Controller Hardware** | ⏳ Planned |
| **Firmware** | ⏳ Planned |
| **Simulation** | ⏳ Planned |
| **Prototype (1 ball)** | ⏳ Planned |
| **Alpha (12 balls)** | ⏳ Planned |
| **Full Installation** | ⏳ Planned |

---

*Built with curiosity, physics, and a love of kinetic art.*
