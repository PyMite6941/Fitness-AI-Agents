# FitnessAI Watch — Battery & Power

How to run the watch off a Li-ion cell, charge it, and switch automatically between
USB and battery. Firmware side (`power.h` / `power.cpp`) is done; this is the hardware
it expects.

> **Status:** firmware ready and compiling. Hardware **not built yet**, so
> `PIN_BATT_ADC` ships as `-1` (disabled) — an unwired ADC pin floats and would report a
> random percentage. The status bar keeps showing `USB` exactly as before.
> **Set `PIN_BATT_ADC` to `3` the moment the divider is soldered.**

---

## ⚠️ The one thing to get right: don't let the charger see the boost converter

The obvious wiring — "tap USB 5 V for the charger, and inject the boost into the 5 V pin" —
creates a **loop that charges the battery from itself**.

On the ESP32-C3 SuperMini the **`5V` pin *is* USB VBUS** — the same copper. So if the
charger's input is tied to that pin, then with USB unplugged:

```
battery → boost → diode → 5V pin → charger input → battery → boost → ...
```

The charger sees ~4.7 V on its input and ~3.7 V on the cell, decides that's a valid supply,
and starts "charging" the battery from the battery. It never terminates, the cell drains
faster than if you had no charger at all, and the TP4056 gets hot. Nothing blows up, but
the watch dies in a few hours and the fault is very hard to spot.

**The fix is simple: the charger module gets its own USB port.** TP4056 boards ship with a
micro-USB or USB-C connector on them — use it, and never connect the charger's `IN+` to
the ESP32's `5V` pin. The two supplies then only ever meet at the `5V` pin, where the
Schottky arbitrates them.

> Some SuperMini revisions put a diode between USB VBUS and the `5V` pad, which would
> break the loop on its own — but the clones vary and the silkscreen never says which you
> have. Giving the charger its own port costs nothing, needs no probing, and is correct on
> every revision. Don't rely on a diode you can't confirm.

---

## The design

> This is now drawn as a real schematic: **[hardware/](hardware/)** (KiCad 10,
> ERC clean), exported to `hardware/export/fitness-watch-schematic.pdf`.
> The diagram below is the same circuit in text.

```
        ┌──────────────── charging cable (module's own port) ───────┐
        │                                                            │
        ▼                                                            │
  ┌──────────────┐                                                   │
  │   TP4056     │  IN+ ── its own USB connector ─────────────────────┘
  │ + DW01/8205  │
  │  protection  │  B+ / B− ─────────► Li-ion cell
  └──────┬───────┘
         │ OUT+ / OUT−   (protected: cuts off on over-discharge)
         │
         ├─────────────────────────► battery divider → GPIO3 (sense)
         │
         ▼
  ┌──────────────┐
  │ 5 V BOOST    │  set to 5.1 V BEFORE connecting anything
  │  (MT3608)    │
  └──────┬───────┘
         │
      1N5819          stripe (cathode) toward the board
         │
         ▼
  ┌─────────────────────────────────────────┐
  │ ESP32-C3 SuperMini    5V ◄──────────────┘
  │                      GND ◄───────────── battery GND (common)
  │  USB-C = PROGRAMMING ONLY               │
  └─────────────────────────────────────────┘
```

**Automatic switchover, no firmware involved.** USB-C plugged in puts 5.0 V on the `5V`
pin. The boost delivers 5.1 V minus the Schottky's ~0.35 V ≈ **4.75 V**, which is lower,
so the diode is reverse-biased and the boost is cut out. Unplug USB and the pin falls to
4.75 V from the battery. The higher supply simply wins.

Two rules that make it work:

- **Set the boost to 5.1 V, not 5.0 V.** After the diode you want ~4.75 V — comfortably
  above the onboard LDO's dropout, and comfortably below USB's 5.0 V so the diode reliably
  turns off when USB is present.
- **Boost input goes to the charger's `OUT+/OUT−`, not `B+/B−`.** `OUT` is downstream of
  the DW01A protection FETs, so over-discharge cutoff actually protects the cell. Wiring
  to `B+` bypasses the protection entirely — that's how Li-ion cells get destroyed.

### Diode: 1N5819, not the 1N4007 in the ELEGOO kit

The pasted plan has this right. Numbers for this build:

| Diode | Drop @ ~70 mA | Voltage at the `5V` pin | Verdict |
|---|---|---|---|
| **1N5819** (Schottky) | ~0.32 V | 4.78 V | **use this** |
| 1N4007 (silicon) | ~0.85 V | 4.25 V | works, wastes ~4% of the pack |

The 1N4007 still clears the LDO's dropout, so it will run — it's fine for proving the
concept on the bench. It just burns ~60 mW as heat that the Schottky doesn't.

---

## Honest efficiency note

Boosting 3.7 V up to 5 V so the board's LDO can drop it back to 3.3 V is lossy:

```
boost (~88%) × LDO (3.3 / 4.78 = 69%)  ≈  61% overall
```

At the measured ~160 mW steady draw, the cell has to supply ~260 mW ≈ **70 mA at 3.7 V**.
A 500 mAh cell gives roughly **7 hours**; 1000 mAh gives ~14.

Feeding a regulated 3.3 V straight to the `3V3` pin would be ~90% efficient (≈10 h on
500 mAh) — but then your regulator and the board's onboard LDO both drive the same node
whenever USB is connected, and they fight. There's no clean way to arbitrate that at 3.3 V,
because a Schottky drop there (3.3 − 0.35 = 2.95 V) browns out the chip. **The 5 V path
buys automatic, safe switchover at the cost of ~40% of the runtime, and that's the right
trade for now.**

The real battery wins are in duty-cycling, not topology: blank the OLED on a timeout
(10–20 mA), shut the MAX30105 down when `fingerPresent` is false (~3 mA), and batch WiFi
instead of holding an association. Those together matter far more than the regulator
choice.

---

## Battery sense divider

```
OUT+ ──[100k]──┬──> GPIO3        ← PIN_BATT_ADC
               ├──[100nF]── GND   (steadies the ADC)
               └──[100k]── GND
```

- Halves the cell, so a full 4.2 V reads 2.1 V — inside ADC1's 11 dB range.
- 2 × 100 kΩ draws **21 µA**, low enough to leave connected forever.
- **Must be ADC1 (GPIO 0–4).** ADC2 stops responding the moment WiFi is on.
- Tap `OUT+`, not `B+`, so the divider stops drawing when protection trips.

The firmware uses `analogReadMilliVolts()` (per-chip eFuse calibration) and averages 16
samples. Percentage comes from an interpolated Li-ion discharge curve, not a straight
line — a linear 3.3–4.2 V map would call a half-empty cell ~55%.

**Calibrate once:** compare the watch's Sensors screen against a multimeter on the cell,
then set `BATT_CAL_SCALE` in `config.h` to `multimeter ÷ watch`.

### Optional: true USB detection

Without it the firmware guesses from cell voltage (`> 4.25 V` must mean external power),
which can't tell "USB in, cell full" from "cell just came off the charger". If you want it
exact, add a second divider from the `5V` pin to a free ADC1 pin and set `PIN_USB_SENSE`:

```
5V ──[100k]──┬──> GPIO1        ← PIN_USB_SENSE (GPIO 0 or 1; both free ADC1)
             └──[100k]── GND
```

That also unlocks the `CHG` (charging) vs `USB` (topped off) distinction in the status bar.

---

## Parts

| Part | Spec | Notes |
|---|---|---|
| Li-ion cell | 1S, 500–1200 mAh | 18650 + holder, or a LiPo pouch (e.g. 503450) |
| Charger | **TP4056 *with* protection** | must have the DW01A + FS8205 pair, and its **own USB port** |
| Boost | MT3608 (adjustable) | **set to 5.1 V before connecting** |
| Diode | **1N5819** | 1 A Schottky |
| Battery divider | 2 × 100 kΩ, 1 × 100 nF | |
| Buttons | 2 × 330 Ω | see README → Resistors |
| USB sense *(optional)* | 2 × 100 kΩ | |

**Total to buy: 2 × 330 Ω, 4 × 100 kΩ, 1 × 100 nF, 1 × 1N5819**, plus the three modules.

### Build order (the step that kills boards)

1. Charge the cell on the TP4056 alone. Confirm it stops at ~4.2 V.
2. Connect the boost to `OUT+/OUT−`. **Power it up with nothing on its output, put a
   multimeter on it, and turn the trimpot until it reads 5.1 V.** MT3608 boards ship at an
   arbitrary setting and can output up to 28 V — connecting one unadjusted is the single
   most common way these boards get destroyed.
3. Only now add the 1N5819 and connect to the `5V` pin, stripe toward the board.
4. Solder the sense divider, set `PIN_BATT_ADC 3`, flash, and calibrate.

Cell polarity into `B+`/`B−` is not reversible — check it twice.

---

## Firmware API (`power.h`)

| Call | Returns |
|---|---|
| `powerBegin()` / `powerTick()` | wired into `setup()` / `loop()`; no-ops when `PIN_BATT_ADC` is `-1` |
| `battMilliVolts()` | cell mV |
| `battPercent()` | 0–100 from the Li-ion curve |
| `usbPresent()` | external power |
| `battLow()` | under `BATT_LOW_MV` (3.5 V) — status bar shows `LOW` |
| `battCritical()` | under `BATT_CRIT_MV` (3.4 V) — **the radio is shut off** |
| `battLabel()` / `battDetail()` | `"87%"` for the status bar; `"4.06V 78% bat"` for the Sensors screen |

`battCritical()` shedding the radio is deliberate: WiFi TX peaks near 300 mA, which is
exactly what collapses a nearly-empty cell into a brownout reset loop. Readings keep
queuing in RAM and flush on the next charge.
