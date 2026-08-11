# FitnessAI Watch — Schematic

KiCad 10 project for the watch. Moved into the repo from
`OneDrive/ドキュメント/kicard schematics/` so the schematic versions alongside the
firmware it describes.

```
fitness watch.kicad_sch     the schematic  (GENERATED — see below)
fitness watch.kicad_pro     project file
fitness-watch.kicad_sym     symbol library for the breakout modules
sym-lib-table               registers that library with the project
generate_schematic.py       the netlist, as code
export/                     PDF / SVG / netlist / BOM
esp-c3-super-mini.pretty/   footprint library
```

**ERC: 0 errors, 0 warnings.**

---

## The schematic is generated — edit the script, not the sheet

`fitness watch.kicad_sch` is written by `generate_schematic.py`. Change the
netlist there and re-run it:

```bash
python generate_schematic.py
```

The previous version of this schematic had every symbol placed and **zero
wires** — ERC reported around 40 unconnected pins and the file documented
nothing. It was a parts list arranged on a page. Rather than hand-wire it once
and let it drift, the connectivity now lives as data next to the pin
assignments it has to agree with (`watch/firmware/fitness_watch/config.h`).

Connections are made by **net label**: every pin gets a short stub and a label,
and identically-named labels are the same net. The one exception is the battery
sense divider, which is drawn with real wires and a junction — a divider is
where label-style connection hides the topology, and it is the node most likely
to be built wrong.

Editing the sheet by hand in KiCad works fine; just know the next run of the
script overwrites it.

## Regenerating the exports

```bash
KICAD=~/AppData/Local/Programs/KiCad/10.0/bin/kicad-cli.exe
"$KICAD" sch erc            --output erc.rpt --severity-error --severity-warning "fitness watch.kicad_sch"
"$KICAD" sch export pdf     --output export/fitness-watch-schematic.pdf "fitness watch.kicad_sch"
"$KICAD" sch export svg     --output export "fitness watch.kicad_sch"
"$KICAD" sch export netlist --output export/fitness-watch.net "fitness watch.kicad_sch"
"$KICAD" sch export bom     --output export/fitness-watch-bom.csv "fitness watch.kicad_sch"
```

---

## Power: battery unless USB-C is plugged in

```
  cell ── TP4056 (own USB) ── MT3608 boost @5.1V ── D1 ──┬── ESP32 "5V" pin
                                                          │
                                    the module's USB-C ───┘  (same copper)
```

**D1 is the switchover.** The ESP32-C3 SuperMini's `5V` pad *is* USB VBUS, so:

| USB-C | Voltage at the `5V` pin | Battery branch | Result |
|---|---|---|---|
| plugged in | 5.0 V from VBUS | 5.1 − 0.35 = **4.75 V** | D1 reverse-biased → battery cut out |
| unplugged | falls to 4.75 V | boost drives it | D1 conducts → battery runs the watch |

The higher supply simply wins. No firmware, no relay, no switching glitch — and
D1 also stops USB back-feeding into the boost converter's output.

Two things that must be right, both on the sheet as warnings:

1. **The MT3608 must be set to 5.1 V before it is wired to anything.** These
   boards ship at an arbitrary setting and can output up to 28 V.
2. **The TP4056's `IN+`/`IN−` are its own USB socket** and are marked
   no-connect here on purpose. Wiring them to the ESP32's `5V` pin creates a
   loop that charges the battery from itself (boost → 5V → charger → cell →
   boost). It never terminates and quietly flattens the cell.

Full reasoning, efficiency numbers and build order: **[../POWER.md](../POWER.md)**.

## Notes on the design

- **Parts are breakout modules, not bare ICs**, because that is what gets
  soldered. The symbols carry module pinouts (`VIN/GND/SCL/SDA`), not the QFN
  pinout of a raw MAX30102.
- **No I2C pull-ups on the sheet.** All three modules carry their own; together
  they already measure ~1.1 kΩ, which is at the strong end of usable. Adding
  more would make it worse. See `watch/README.md` → Resistors.
- **The buttons are active-high** (`GPIO → 330 Ω → switch → 3V3`) to match
  `BTN_ACTIVE_HIGH 1` in `config.h`, with the pin in `INPUT_PULLDOWN`. The
  330 Ω is not a pull-up — it limits current if the pin is ever driven LOW
  while the button is held.
- **`AD0` is tied to GND** so the MPU6050 address is deterministically `0x68`
  rather than relying on the breakout's internal pulldown.
- **The sense divider taps `OUT+`, not `BAT+`**, so it stops drawing current
  when the protection circuit trips.
- **The sensor is labelled MAX30102** here while the firmware says MAX30105.
  Both answer at `0x57`, both are driven by the SparkFun MAX3010x library, and
  the firmware's `setup(..., ledMode = 2, ...)` selects Red+IR — exactly the two
  LEDs a MAX30102 has. Either part works.
