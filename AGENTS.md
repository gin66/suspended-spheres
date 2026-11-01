# AGENTS.md — Hanging Balls

> Mission, system overview, and development-environment notes.
> Calculations, trade studies, requirements, and risk work live in `doc/`.

---

## 1. Mission

Hanging Balls is a real-time kinetic art installation: 120 polished steel balls suspended in a 10 × 12 grid, each independently driven vertically over a 1 m range, together producing travelling waves, interference patterns, and choreographed sequences.

The visual result is the product. Motion must look smooth, stay synchronized across the grid, and remain safe when something fails.

---

## 2. System Overview

| Aspect | Description |
|--------|-------------|
| Scale | 120 balls, 10 columns × 12 rows, 1 m vertical travel |
| Ball | 50 g polished steel, Ø ≈ 46 mm |
| Drive | One stepper motor per ball; thread wound on a spool |
| Motor | NEMA-17 (42 × 42 mm), 20–30 mm drum |
| Driver | TMC5160 (SPI, StallGuard2, on-driver ramp) or TMC2209 |
| Controller | ESP32-class, one per column |
| CAN-FD option | ESP32-C5 (on-chip CAN-FD) or ESP32-S3 + MCP2518FD |
| Bus | CAN 2.0B at ≥ 1 Mbit/s; CAN-FD is optional headroom |
| Timing | Local trajectories on the column controllers; periodic sync to ≤ 1 ms |
| Homing | Slow descent onto the lower stop; stall current or a switch |
| Safety | Containment of a falling ball; emergency stop that holds or brakes, not just cuts power |

Two physical facts shape the design:

- **The thread only pulls.** Ordinary motion stays taut; a fast descent is an intentional free-fall drop with a short braked catch.
- **4.5 m/s is a ceiling, not a target.** It is the free-fall limit over 1 m of travel (`√(2gh) ≈ 4.43 m/s`, specified as 4.5 m/s). It bounds system speed; it is not a speed every move must reach, and upward acceleration is a separate requirement.

All numbers, derivations, and design choices belong in `doc/`.

---

## 3. Repository Structure

```
Hanging_Balls/
├── README.md                   # Project overview and entry point
├── AGENTS.md                   # This file — mission, overview, dev notes
└── doc/
    ├── system_requirements.md  # Behavioural and non-functional requirements
    ├── mechanical_analysis.md  # Physics: speed, force, thread, inertia, drum, frame
    ├── controller_analysis.md  # Platforms, drivers, stepping, bus, clock drift
    └── RISK.md                 # Risk register and technical review
```

All technical Markdown belongs in `doc/`. Only `README.md` and `AGENTS.md` sit at the repository root.

---

## 4. Development Environment

- **Documentation-only at present.** There is no firmware, build system, test runner, or linter in the repository. Do not assume commands that do not exist.
- **Markdown + SI units.** Use metres, seconds, kilograms, newtons, and watts. Keep tables aligned with the surrounding style.
- **Source of truth:** requirements in `doc/system_requirements.md`; physics in `doc/mechanical_analysis.md`; platform and bus decisions in `doc/controller_analysis.md`; risks in `doc/RISK.md`.
- **No duplicated calculations outside `doc/`.** `AGENTS.md` and `README.md` describe the project; they do not carry datasheets, torque tables, or protocol frames.
- **Verification is manual.** Cross-check a changed number against the derivation in the relevant analysis document. There are no automated checks to run.
- **No requirement IDs outside `doc/system_requirements.md`.** If another document cites a requirement, keep the ID and the requirement text in sync.
