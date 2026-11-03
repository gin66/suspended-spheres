# Motor Selection — Cheap Variants Compared

> Trade study of low-cost motor options against the current requirement set
> (system_requirements.md): 100–120 hollow ornaments (≤ 50 g), ~2 m
> travel, slow motion with a nominal fast tier of 0.44–0.59 m/s and an
> optional 0.8–1.3 m/s short-effect tier (ASM-015), single position reference.
>
> **Prices checked on AliExpress (DE, EUR) on 2026-11-01.** They are
> indicative and fluctuate; shipping and import treatment are noted
> separately in §7. Datasheet values are typical ranges, not guaranteed
> minima.
>
> **Drive:** the stepper shaft is the spool. There is no gearbox. RPM and
> step rate in §1.2 are shaft figures (`mechanical_analysis.md` §4). The
> geared rows below are rejected candidates, kept so the comparison stays
> on the page.

---

## 1. What the requirements actually demand

### 1.1 Torque

Static hold force per ball:
```
F_hold = m g = 0.050 × 9.81 = 0.491 N
```

`FR-MOT-008` requires a 2× holding margin. The nominal lift adds ≈ 0.08 N and
the optional 4 m/s² tier ≈ 0.20 N, but the reflected motor/drum inertia is the
larger term at speed. The sizing torque is still essentially the holding
torque:

| Drum Ø | Radius | Static torque | With 2× margin | At 4 m/s² + inertia |
|--------|--------|---------------|----------------|---------------------|
| 10 mm | 5 mm | 0.0025 Nm | **0.005 Nm** | 0.005 Nm |
| 20 mm | 10 mm | 0.0049 Nm | **0.010 Nm** | 0.011 Nm |
| 30 mm | 15 mm | 0.0074 Nm | **0.015 Nm** | 0.016 Nm |
| 40 mm | 20 mm | 0.0098 Nm | **0.020 Nm** | 0.022 Nm |

Even at the largest drum this is tiny. Every candidate below exceeds it;
**torque is not the discriminator** — speed, noise, and integration are.

### 1.2 Speed

| Drum Ø | RPM @ 0.6 m/s (nominal) | RPM @ 0.8 m/s (3 s) | RPM @ 1.2 m/s (2 s) |
|--------|--------------------------|----------------------|----------------------|
| 20 mm | 573 | 764 | 1,146 |
| 25 mm | 458 | 611 | 917 |
| 30 mm | 382 | 509 | 764 |
| 40 mm | 286 | 382 | 573 |

The 40 mm drum keeps the optional tier below 600 RPM; the 20 mm drum needs
1,146 RPM, where torque and noise must be verified.

### 1.3 Position

The spool is on the stepper shaft, so position error at the ball is shaft
angle times drum radius. The scale limit is 50 µm per ~300 mm grid pitch
(`NFR-PHYS-007`). At a 20 mm drum, 50 µm is an angle of:
```
θ = 50 µm / 10 mm = 0.005 rad ≈ 0.29°
```
A gearbox with 1–2° of output backlash would show as a dead band at
reversals. That, and output speed, is why the geared candidates in §4 are
rejected. The motors that remain have the spool on the shaft.

### 1.4 Power-off behaviour

`FR-MOT-011` allows balls to be either held by de-energised detent torque or
to descend to the lower limit, so detent is a preference, not a hard
pass/fail.

### 1.5 Noise

Cumulative motor noise is a significant risk (`RISK.md` #2). With the spool
on the shaft, microstepping at slow speed is the quiet case. Gear whine is
one reason the geared candidates were rejected.

---

## 2. Candidates and actual AliExpress prices

Representative single-unit prices from listings found on 2026-11-01. Many
listings offer free shipping, or free above 10 €; some show "no import fees"
and others warn of import duties (see §7).

| Motor | Type | Typical holding torque | Max output speed | Backlash | AliExpress price (unit) | 120-unit parts cost |
|-------|------|------------------------|------------------|----------|--------------------------|---------------------|
| **28BYJ-48** | unipolar, 1:64 gearbox | 0.03–0.10 Nm | 15–25 RPM | **1–2°** | 1.0–3.5 € (kits ~1.9 €/pc) | 120–420 € |
| **24BYG48** | geared, 24 mm | ~0.02–0.05 Nm | ~20–40 RPM | 1–2° | ~5.2 € | ~620 € |
| **NEMA-8** (20 mm) | bipolar direct | 0.012–0.030 Nm | 2,000–4,000 RPM | ~0 | 12–22 € | 1,440–2,640 € |
| **NEMA-11** (28 mm) | bipolar direct | 0.06–0.20 Nm | 3,000–6,000 RPM | ~0 | **5.9–19 €** (generic ~6–7 €) | 710–2,280 € |
| **NEMA-14** (35 mm) | bipolar direct | 0.11–0.18 Nm | 2,000–5,000 RPM | ~0 | **2.5–12.6 €** (typical 6–9 €) | 300–1,510 € |
| **NEMA-17** (42 mm) | bipolar direct | 0.40–0.70 Nm | 2,000–4,000 RPM | ~0 | **12–20 €** (bulk 5-packs ~15 €/pc) | 1,440–2,400 € |
| **NEMA-17 pancake** (42×23 mm) | bipolar, short stack | 0.16 Nm | 1,500–3,000 RPM | ~0 | **~6.6–7 €/pc** (5-packs 33–35 €) | 790–840 € |
| **BLDC gimbal + FOC** | 3-phase + encoder | 0.05–0.15 Nm | high | ~0 | 8–15 € + driver | 1,500–3,000 € + drivers |

> The NEMA-17 *pancake* is much cheaper than a full-size NEMA-17 and is a
> different proposition: low profile, 16 N·cm, direct drive.

### 2.1 What the search added

Beyond the three motors you listed, the search turned up:

- **NEMA-14 (35 mm)** — often the cheapest direct-drive option (a 3.55 € mini
  and a 7–9 € standard variant).
- **NEMA-17 pancake (42×23 mm)** — 16 N·cm, sold in 5-packs at ≈ 6.6–7 €/pc;
  the best cost/performance compromise found.
- **24BYG48** — a smaller geared motor like the 28BYJ-48; same backlash and
  speed problem.
- **NEMA-11 variants** — planetary-geared (~24 €), with brake (~33 €), hollow
  shaft (~27 €). A planetary gear is out: the spool is on the stepper shaft.
  The brake and hollow-shaft versions are dearer and unused here.
- **NEMA-8** — surprisingly expensive per unit (12–22 €) and only
  12–30 mN·m; poor value for this duty.

---

## 3. Suitability matrix

Rating: 5 = excellent, 1 = unsuitable. "Speed" is scored against the nominal
0.6 m/s tier and the optional 1.2 m/s tier at a 25–40 mm drum.

| Criterion | 28BYJ-48 | 24BYG48 | NEMA-8 | NEMA-11 | NEMA-14 | NEMA-17 | NEMA-17 pancake | BLDC+FOC |
|-----------|:--------:|:-------:|:------:|:-------:|:-------:|:-------:|:---------------:|:--------:|
| Torque margin | 4 | 3 | 2 | 4 | 4 | 5 (overkill) | 4 | 3 |
| Speed (0.6 / 1.2 m/s) | **1** | **1** | 3 | 4 | 5 | 5 | 4 | 5 |
| Backlash / position | **1** | **1** | 5 | 5 | 5 | 5 | 5 | 5 |
| Power-off hold | 4 | 4 | 2 | 3 | 3–4 | 4 | 3 | 1 |
| Noise | 2 | 2 | 4 | 4 | 4 | 4 | 4 | 5 |
| Cost | 5 | 4 | 3 | 4 | **5** | **2** | **5** | 3 |
| Availability | 5 | 3 | 3 | 4 | 4 | 5 | 4 | 2 |
| Integration effort | 3 | 3 | 3 | 4 | 4 | 4 | 4 | **2** |
| **Overall** | **No** | **No** | Poor value | **Recommended** | **Recommended** | Over-specified | **Best value** | Noise option |

---

## 4. Notes per candidate

### 28BYJ-48 / 24BYG48 — not suitable
- **Speed:** 28BYJ output ≈ 15–25 RPM gives only ≈ 0.016–0.039 m/s on a
  20–30 mm drum; the 24BYG48 is similar. Cannot meet the 0.6 m/s nominal tier.
- **Backlash:** 1:64 gearboxes have ≈ 1–2° of backlash (≈ 0.2–0.5 mm at a
  20–30 mm drum), larger than the 50 µm scale budget and visible at
  reversals.
- **Noise:** geared whine works against the quiet, hypnotic intent.
- Cheapest by far, but wrong on the two criteria that matter.

### NEMA-8 — poor value
- Torque is only 12–30 mN·m; at a 30 mm drum the 2× margin is marginal.
- Per-unit price (12–22 €) is higher than NEMA-11/NEMA-14 and close to
  NEMA-17, with much less torque.
- Detent is low; balls likely descend on power-off.
- Not recommended.

### NEMA-11 — recommended
- Direct drive: negligible backlash, quiet with microstepping.
- Torque margin ≈ 5–10× the 2× holding requirement at low speed.
- Speed: 458–573 RPM nominal and ~917 RPM optional on a 25 mm drum. Verify
  high-speed torque and noise on the specific unit; it is the least
  comfortable candidate for the 1.2 m/s tier.
- Generic units around 6–7 €; branded/UMOT units 19–27 €.
- Detent is marginal; accept the power-off descent allowed by `FR-MOT-011`,
  or add a brake if that behaviour is unwanted.

### NEMA-14 — recommended, often cheapest
- A 3.55 € mini and 7–9 € standard units make it the cheapest direct-drive
  option found.
- Torque 0.11–0.18 Nm; ample.
- Speed: 382–509 RPM nominal and 764 RPM optional on a 30 mm drum; the
  1.2 m/s tier is only ~917 RPM on a 25 mm drum.
- Check the specific listing: some 2.5–3.5 € units are very small and may
  have lower torque and shorter leads.
- Functionally equivalent to NEMA-11 for this duty, with a better speed
  margin.

### NEMA-17 (full) — over-specified and most expensive
- Meets every requirement with the largest margin and best detent.
- Torque is 25–45× what is needed; at 12–20 €/unit (≈ 1,440–2,400 € for
  120) it is the most expensive practical choice, as you noted.
- Justified only by sourcing or a future speed/torque increase.

### NEMA-17 pancake — best value
- 42×23 mm short-stack, 16 N·cm, ≈ 6.6–7 €/pc in 5-packs.
- Direct drive, low profile, ample torque, adequate speed.
- The cheapest direct-drive option with real margin; verify the specific
  listing's torque at ~900 RPM and its detent.

### BLDC gimbal + FOC — noise option
- Quietest, smooth at low speed.
- No detent: balls fall on power-off.
- Needs encoder/FOC and more integration; fails the "cheap and simple" goal
  unless noise proves unacceptable.

---

## 5. Impact of drum choice

| Drum Ø | Torque needed (2×) | RPM @ 0.6 / 1.2 m/s | Turns for 2.5 m | Single-layer width |
|--------|--------------------|----------------------|-----------------|--------------------|
| 10 mm | 0.005 Nm | 1,146 / 2,292 | ≈ 80 | ≈ 40 mm (wide) |
| 20 mm | 0.010 Nm | 573 / 1,146 | ≈ 40 | ≈ 20 mm |
| 25 mm | 0.012 Nm | 458 / 917 | ≈ 32 | ≈ 16 mm |
| 30 mm | 0.015 Nm | 382 / 764 | ≈ 27 | ≈ 13 mm |
| 40 mm | 0.020 Nm | 286 / 573 | ≈ 20 | ≈ 10 mm |

A **40 mm drum** is the best fit for the fuller envelope: it keeps the optional
1.2 m/s tier at 573 RPM and narrows the winding to ~10 mm. Microstepping is
TBD. In the 1/16 worked case the linear step is 39.3 µm and 1.2 m/s is
30.6 kstep/s (`mechanical_analysis.md` §4). The Fly-Micro4 step generator
tops out at 200 kstep/s; a 2 ms playback chunk tops out at 64 kstep/s. At
1/16 a 30 mm drum also covers 1.2 m/s (40.7 kstep/s), and a 20 mm drum
covers the step rate at 1,146 RPM.

28BYJ-48 and 24BYG48 stay rejected. Their geared output is far below 0.6 m/s
at every drum size, and this drive has the spool on the stepper shaft.

---

## 6. Recommendation

1. **Shortlist:** NEMA-14, NEMA-11, and NEMA-17 pancake, spool on the shaft,
   6–9 €/unit.
2. **Best value:** NEMA-17 pancake (~6.6–7 €, 16 N·cm) if the listing's
   speed and detent check out.
3. **Cheapest direct drive:** NEMA-14 (3.5–9 €), verifying the small/weak
   variants.
4. **NEMA-11** remains the safe, widely available baseline at ~6–7 €
   generic, but verify ~900 RPM torque/noise before using it for the 1.2 m/s
   tier.
5. **Drum:** 40 mm is the working diameter (573 RPM at 1.2 m/s, ~10 mm
   winding). **Microstepping is TBD.** The 1/16 worked case is 39.3 µm and
   30.6 kstep/s at 1.2 m/s. The PIO ceiling is 200 kstep/s; a 2 ms chunk
   caps playback at 64 kstep/s. At 1/16 a 30 mm drum also covers 1.2 m/s
   (40.7 kstep/s); a 20 mm drum covers the step rate at 1,146 RPM.
6. **NEMA-17 full-size:** over-specified and the most expensive; keep only
   as a fallback.
7. **28BYJ-48 / 24BYG48, and any planetary-geared NEMA-11:** reject. The
   spool is on the stepper shaft.
8. **NEMA-8:** poor value (price close to NEMA-17, torque much lower).
9. Revisit **BLDC + FOC** only if measured noise from a direct-drive stepper
   is unacceptable.

> Before committing, buy one of each shortlisted type and test at the chosen
> drum: achievable speed at 1.2 m/s (30.6 kstep/s on a 40 mm drum at the
> 1/16 worked case; microstepping is TBD),
> play between shaft and spool at reversals, audible noise, and detent
> hold with a 50 g load.
> Those measurements decide the final choice. The step-rate ceilings are
> 200 kstep/s (PIO) and 64 kstep/s (2 ms chunk), both above this point.

---

## 7. Shipping, VAT, and import (important)

AliExpress pricing is not just the unit price:

- Many listings show **free shipping** or **free above 10 €**; for a
  120-unit order from one seller, shipping is usually free or a small flat
  fee, but confirm before ordering.
- Some listings explicitly show **"Keine Einfuhrkosten"** (no import fees);
  others warn **"Für Sendungen außerhalb der EU fallen Einfuhrabgaben an"**.
- For the EU/Germany, VAT is normally collected by AliExpress at checkout.
  A bulk order above €150 may still attract customs duty and carrier
  handling, and large orders are often split into several parcels.
- **Planning allowance:** add ~10 % to the parts cost for shipping, duty,
  handling, and split-shipment effects, and ask the seller to consolidate
  or confirm the incoterm for a 100–120 unit order. On a €800 motor order
  that is ≈ €80 — small relative to the €4,000 system budget, but it should
  not be forgotten.
- Prices fluctuate and vary by seller/shaft/lead; re-check at purchase time.

---

## 8. Representative listings (for re-checking prices)

| Item | Price seen | AliExpress item |
|------|-----------|-----------------|
| NEMA-11 28 mm, 0.67 A (Jkongmotor, 6/9.5/12 N·cm) | 6.89 € | 1005001992873246 |
| NEMA-11 2-phase, 34 mm | 5.89 € | 32801798556 |
| NEMA-14 mini 35 mm (14h33hm) | 3.55 € | 1005006495308030 |
| NEMA-14 1.8°, 35 mm, 5 mm shaft | 7.09 € | 1005012897664883 |
| NEMA-14 14 N·cm (STEPPERONLINE) | 8.29 € | 1005009708319948 |
| NEMA-17 pancake 42×23 mm, 16 N·cm, 5 pcs | 34.72 € | 1005012910473503 |
| NEMA-17 pancake 42×23 mm, 16 N·cm, 5 pcs (EFUTURETIME) | 32.94 € | 1005013373296337 |
| NEMA-17 17HS8401, 1.8 A, 5 pcs | 73.39 € | 1005012691767434 |
| NEMA-17 17HS4401S, 1.5 A, 40 N·cm | 18.39 € | 1005012334638409 |
| NEMA-8 20HS24, 0.8 A, 12 mN·m | 22.39 € | 4001349995361 |
| 28BYJ-48 + ULN2003 kit | 3.49 € | 1005009285175953 |
| 28BYJ-48, 10 pcs | 18.59 € | 1005012922688164 |

Item IDs and prices are as displayed on 2026-11-01 and will drift.
