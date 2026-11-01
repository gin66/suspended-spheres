# AGENTS.md — Hanging Balls

> Mission and development-environment notes.
> System decisions, calculations, trade studies, requirements, and risk work live in `doc/`.

---

## 1. Mission

Hanging Balls is a real-time kinetic art installation: 120 polished steel balls suspended in a 10 × 12 grid, each independently driven vertically over a 1 m range, together producing travelling waves, interference patterns, and choreographed sequences.

The visual result is the product. Motion must look smooth, stay synchronized across the grid, and remain safe when something fails.

---

## 2. Repository Structure

```
Hanging_Balls/
├── README.md                   # Project overview and entry point
├── AGENTS.md                   # This file — mission and dev notes
└── doc/
    ├── system_requirements.md  # Behavioural and non-functional requirements
    ├── mechanical_analysis.md  # Physics: speed, force, thread, inertia, drum, frame
    ├── controller_analysis.md  # Platforms, drivers, stepping, bus, clock drift
    └── RISK.md                 # Risk register and technical review
```

All technical Markdown belongs in `doc/`. Only `README.md` and `AGENTS.md` sit at the repository root.

---

## 3. Development Environment

- **Documentation-only at present.** There is no firmware, build system, test runner, or linter in the repository. Do not assume commands that do not exist.
- **Markdown + SI units.** Use metres, seconds, kilograms, newtons, and watts. Keep tables aligned with the surrounding style.
- **Source of truth:** requirements in `doc/system_requirements.md`; physics in `doc/mechanical_analysis.md`; platform and bus decisions in `doc/controller_analysis.md`; risks in `doc/RISK.md`.
- **No duplicated calculations outside `doc/`.** `AGENTS.md` and `README.md` describe the project; they do not carry datasheets, torque tables, or protocol frames.
- **Verification is manual.** Cross-check a changed number against the derivation in the relevant analysis document. There are no automated checks to run.
- **No requirement IDs outside `doc/system_requirements.md`.** If another document cites a requirement, keep the ID and the requirement text in sync.
