# BOM cost summary

Source: EasyEDA BOM export `Export_BOM.csv` (2026-11-02 10:48), 69 lines, one board. Updated after U18 → `TJA1051T/3/1J` and after **DNP** marking of U24, C117, R90–R101 and J18–J29.
Pricing: JLCPCB SMT component price at the BOM quantity (1–9 tier), EUR.
Scope: **parts only**. Excludes PCB, PCBA setup/feeders/stencil, assembly labour, shipping, and tax.

**DNP = Do Not Populate:** the schematic/PCB footprint stays, the part is excluded from the BOM and assembly. DNP parts cost €0 in the populated BOM; if ever fitted, add their listed cost.

Detailed line items: [`BOM_priced.csv`](BOM_priced.csv).

## Totals

| Build | Parts cost |
|---|---:|
| Populated BOM (DNP excluded) | **€50.87** |
| Populated BOM, stock-corrected | **~€50.87** |
| If the DNP endstop parts are populated (U24 + C117 + R90–R101 + J18–J29) | +€1.62 as listed / +€1.23 with C158012 |
| Core only (no RS485, expansion, sensors) | **€48.15** |
| Core + thermal sensors | **€49.77** |
| Populated BOM + DIAG/Saleae headers populated | **€51.31** |

## Cost by group

| Group | Parts | Share |
|---|---:|---:|
| Core (MCU, power, 12 drivers, CAN, USB, IO expander U23, motor connectors, safety) | €48.15 | 94.7% |
| Thermal sensors (4 × TMP1075 + passives) | €1.62 | 3.2% |
| RS485 (discrete SP3485, TVS, bias, termination) | €0.78 | 1.5% |
| Expansion/service headers + test points | €0.32 | 0.6% |
| **Populated BOM total** | **€50.87** | **100%** |
| DNP (not populated): U24, C117, R90–R101, J18–J29 | €0.00 | — |
| Probe/Saleae headers (J37–J39, J41–J52), DNP and not in exported BOM | €0.44 | — |

## Option deltas

| Option | Delta | After removal |
|---|---:|---:|
| RS485 | −€0.78 | €50.09 |
| Expansion/service headers + test points | −€0.32 | €50.54 |
| Thermal sensors | −€1.62 | €49.25 |
| RS485 + expansion + thermal sensors | −€2.71 | **€48.15** |
| DNP endstop parts, if populated | +€1.62 as listed / +€1.23 with C158012 | €52.49 |
| DIAG probe headers (12 × 1×4), if populated | +€0.33 | €51.20 |
| Saleae headers (J37–J39), if populated | +€0.11 | €50.98 |
| DIAG + Saleae populated | +€0.44 | **€51.31** |

U23 carries all 12 DIAG inputs and has 4 spare GPIO (`P14`–`P17`). U24, its 12 pull-ups, and the 12 endstop connectors are DNP; the board uses sensorless homing via DIAG. If hard endstops are needed later, populate U24 + C117 + R90–R101 + J18–J29 (C158012 substitute).

## Main cost drivers

| Part | Qty | Extended | Share |
|---|---:|---:|---:|
| TMC2209-LA-T (C2150710) | 12 | €33.98 | 66.8% |
| 0154005.DR (C206916) | 1 | €2.23 | 4.4% |
| TPS54360BQDDARQ1 (C2687968) | 1 | €2.09 | 4.1% |
| TMP1075DGKR (C2864807) | 4 | €1.60 | 3.1% |
| RP2354B (C39843328) | 1 | €1.52 | 3.0% |
| MCP2518FDT-E/SL (C626758) | 1 | €1.51 | 3.0% |
| TCA9555PWR (C465732) | 1 | €0.71 | 1.4% |
| B4B-XH-A(LF)(SN) (C144395) | 12 | €0.68 | 1.3% |
| TJA1051T/3/1J (C38695) | 1 | €0.58 | 1.1% |
| 22uF (C45783) | 3 | €0.55 | 1.1% |

Removing RS485, expansion headers, and sensors saves €2.71; the core is €48.15. The 12 TMC2209s alone are 66.8% of the populated BOM.

## Stock-corrected substitutes

| Ref | Current | Stock | Substitute | Price | Stock | Note |
|---|---|---:|---|---:|---:|---|
| C15, C41, … | C9900110116 100 µF 35 V | 0 | C970686 RVT1V101M0607 | €0.0409 | 65k | footprint changes to D6.3 × L7.7 |
| D6 | C143073 PESD1CAN | 0 | C284094 PESD1CAN | €0.0393 | 50k | SOT-23 |
| J3 | C9900012665 USB-C 16P | 0 | C2927029 USB-TYPE-C-009 | €0.0435 | 22k | verify 16-pin and footprint |
| Y2 | C47089419 40 MHz | 10 | C9010 X322540MPB4SI | €0.1096 | 66k | 3225 4-pad |
| U18 | C38695 TJA1051T/3/1J | 154k | — | €0.5775 | 154k | default; MCP2562FD C191415 (€2.9039, 48 stock) is the pin-compatible CAN-FD alternative |
| U1 | C39843328 RP2354B | 34 | — | €1.5209 | 34 | enough for 1–15 boards; low for larger builds |
| C13 | C698133 220 µF | 89 | — | €0.2314 | 89 | enough for 1–15 boards |
| U24/J18–J29 | DNP | — | C158012 B2B-XH-A(LF)(SN) | €0.0409 | 628k | only if the endstop option is ever populated |

Three mandatory lines remain `stockCount = 0` (100 µF, PESD1CAN, USB-C). The stock-corrected populated BOM is ~€50.87; with the DNP endstop parts populated it is ~€52.10.

## Manufacturing overhead (estimate)

Assumptions: 4-layer 120 × 160 mm board, ENIG, 1.6 mm, JLCPCB/PCBA, 30–40 through-hole connectors hand-soldered, parts BOM ≈ €50.9/board. These are planning numbers; a firm quote needs Gerbers, BOM, CPL, and the final board size.

### One-time NRE

| Item | Estimate |
|---|---:|
| Stencil | €10–20 |
| PCBA setup / unique-part feeders | €50–200 |
| First-article debug (engineering time) | €200–1,600 |
| Test fixture (optional) | €0–500 |
| **One-time total** | **€260–2,320** |

### Per-board recurring overhead

| Item | 1 board | 10–15 boards |
|---|---:|---:|
| Bare PCB | €25–40 | €8–12 |
| SMT assembly | €10–20 | €10–15 |
| THT connector soldering | €10–30 | €10–30 |
| Functional test | €20–60 | €10–40 |
| Yield / rework allowance (5–10%) | €3–10 | €3–10 |
| **Per-board overhead** | **€68–160** | **€41–107** |

### All-in planning range

| Quantity | BOM | Overhead | Total | Per board |
|---:|---:|---:|---:|---:|
| 1 (prototype) | €50.87 | €170–405 | **€220.87–455.87** | €220.87–455.87 |
| 10 | €508.65 | €525–1,330 | **€1033.65–1838.65** | €103.37–183.87 |
| 15 | €762.98 | €745–1,915 | **€1507.97–2677.97** | €100.56–178.56 |

The existing controller-analysis estimate of **€700–800 for 10 driver boards** is BOM + PCB + SMT assembly only. It excludes THT soldering, test, yield, NRE, shipping, and tax; adding those gives the €1,034–1,839 range above.

Largest uncertainties: first-article debug of the RP2354B QFN-80 and 12 × TMC2209 QFN-28, and the labour for the through-hole connectors. BOM pricing uses 1–9 tiers; 10–15 board orders may reduce the driver and passive cost by roughly 5–15%, which is not included above.
