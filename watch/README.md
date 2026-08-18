# FitnessAI Watch — Firmware

Open-hardware smartwatch that pairs to the [FitnessAI](https://fitness-ai-agents.vercel.app)
platform: it tracks heart rate, steps, and GPS workouts on-device and syncs them to your
account, where the AI does the analysis. Built on a **$3 ESP32-C3** and a handful of I2C
modules — no proprietary ecosystem.

> **Status:** Milestones 1–5 + auto-orientation. Four button-navigable screens
> (Home / Heart Rate / Steps / Sync / Sensor-status), on-device BPM/step counting,
> a real NTP clock, **direct WiFi upload to the backend `/ingest` (M5)**, and two ways
> to pair/control it from your phone: a **self-hosted WiFi captive portal** and a
> **Bluetooth LE control peripheral** (M4) that shows up as *"FitnessAI Watch"* in
> any BLE device's list. The display **auto-rotates** to stay upright from the
> MPU6050's accelerometer + gyro. Compiles clean for
> `esp32:esp32:esp32c3:PartitionScheme=huge_app` (46% flash, 14% RAM).
>
> **Confirmed on hardware (0.96" SSD1306 128×64, I2C `0x3C`):** the drawing driver
> must be **NONAME** (`U8G2_SSD1306_128X64_NONAME_F_HW_I2C`). The ALT0 init makes the
> sparse measurement pattern look fine but **interleaves the rows with real text** —
> every UI line ends up squashed on top of the next. Second gotcha: the shared I2C
> bus must run at **100 kHz**, and `MAX30105.begin(Wire, I2C_SPEED_FAST, ...)` silently
> raises it back to 400 kHz — so the clock is re-asserted after the sensor init
> block, or the OLED flickers/blanks. Both are enforced in `config.h` / setup(),

```
watch/firmware/fitness_watch/
  fitness_watch.ino   ← main sketch (setup/loop, UI, auto-orientation)
  config.h            ← ALL wiring lives here — the only file you edit for your build
  net.h / net.cpp     ← settings (NVS), NTP clock, WiFi, pairing portal, sync to /ingest
  ble.h / ble.cpp     ← Bluetooth LE control peripheral (pair / time sync / commands)
watch/firmware/screen_measure/
  screen_measure.ino  ← display size / driver identification (see "Measuring the display")
watch/firmware/watch_diag/
  watch_diag.ino      ← per-component hardware diagnostic over serial
watch/firmware/i2c_scan/
  i2c_scan.ino        ← brute-force I2C pin finder (when nothing responds at all)
watch-archive/
  wiring.md           ← physical wiring reference (legacy — SDA/SCL are STALE, see banner)
  libraries.md        ← library list (legacy)
```

---

## Hardware

| Part | Role | Bus / pins | I2C addr |
|---|---|---|---|
| **ESP32-C3 SuperMini** | MCU + Wi-Fi | — | — |
| **SSD1306 128×64 OLED** *(default)* | display | I2C (shared) | `0x3C` |
| **LCD1602 + PCF8574 backpack** *(alternative)* | display | I2C (shared, same pins) — **VCC on 5V** | `0x27`/`0x3F` (auto-detected) |
| **MPU6050** | accel/gyro → steps | I2C (shared) | `0x68` |
| **MAX30102** | heart rate / SpO₂ | I2C (shared) | `0x57` |
| 2 × tactile buttons | navigation | GPIO (active-high) | — |
| **NEO-6M GPS** *(optional)* | route tracking | UART1 | — |
| Battery + divider *(optional)* | charge level | ADC1 | — |

All I2C devices share **one two-wire bus** — critical on the pin-starved C3. You pick
the display in `config.h` (`DISPLAY_TYPE`): either the SSD1306 OLED *or* the
LCD1602, wired to the **same** SDA/SCL pins. See [Wiring the LCD1602](#wiring-the-lcd1602).

---

## Pin map (defaults in `config.h`)

| Signal | GPIO | Why this pin |
|---|---:|---|
| I2C **SCL** | 8 | I2C idles HIGH, so using this strapping pin is safe at boot. (Onboard LED may flicker — harmless.) Shared by OLED/LCD + MAX30105 + MPU6050. |
| I2C **SDA** | 7 | Non-strapping; moved off the old GPIO 9 so only one strapping pin (8) is used for I2C. Shared bus, same as SCL. |
| MAX30105 **INT** | −1 | Unwired — firmware polls the sensor every loop() instead of using the interrupt line. |
| MPU6050 **INT** | −1 | Unwired — same, polling only. |
| **Button A** (Back) | 4 | Tap → previous screen; hold → home. Non-strapping — safe even if held at reset. |
| **Button B** (Select) | 5 | Single tap → home; double tap → display mute (vitals keep running). |
| **GPS RX** | 6 | Any GPIO works as UART via the C3 matrix. |
| **GPS TX** | — | Needs a free pin once GPS lands (0, 1, 2, or 10). |
| **Battery ADC** | 3 | Must be **ADC1** (GPIO 0–4); ADC2 is unusable with Wi-Fi on. |
| Vibration | −1 | Disabled (not wired). |
| Serial / USB debug | 20/21 | Reserved by USB-serial; left free. |

All three I2C devices (OLED `0x3C`, MAX30105 `0x57`, MPU6050 `0x68`) sit on the **same
two wires** — I2C tells them apart by address, not by pin, so sharing one bus across
different vendors' modules is the normal, correct way to wire this (and simpler/faster
than bit-banging a second bus, which the C3's single hardware I2C controller can't do
natively). MPU6050's `AD0` pin is tied low (or left floating on its onboard pulldown) for
the default `0x68` address; `XDA`/`XCL` (its auxiliary I2C-master pins, for daisy-chaining
a magnetometer) are unused.

### ESP32-C3 pin rules (why the map is what it is)
- **Usable GPIOs:** 0–10, 20, 21 (GPIO 20/21 are USB-serial — keep for debug).
- **Strapping pins: 2, 8, 9.** Don't let them be driven LOW *at reset*. I2C is fine (pulled up); **never put a button on 2/8/9** (a press during reset = wrong boot mode).
- **ADC:** only **ADC1 = GPIO 0–4** works while Wi-Fi is on. Battery sense must live there.
- **Free pins after this map:** 0, 1, 2, 9, 10 — room for extra buttons, a buzzer, GPS, or a charge-status line.

**To change wiring:** edit `config.h` only. Every GPIO is `#define`d there; the rest of the
firmware never hard-codes a pin. Set any optional pin to `-1` to disable it.

---

## Wiring the LCD1602

The LCD1602 (HD44780 with a **PCF8574 I2C backpack**) replaces the OLED **on the same
two I2C wires** — GPIO 7 (SDA) and GPIO 8 (SCL). No extra pins needed. Only one of the
two displays is installed at a time (both would work on the bus, but the firmware drives
whichever `DISPLAY_TYPE` says).

### The four wires

Your module has a **16-pin header** along the top and the backpack soldered across it,
leaving **4 pins on the side**. You wire *only those 4*. The 16 are already connected to
the PCF8574 by the backpack — see [what the 16 pins do](#what-the-16-pin-header-does) if
you're curious, but you never touch them.

**On the ESP32-C3 SuperMini** (`config.h`: `PIN_I2C_SDA 7`, `PIN_I2C_SCL 8`):

| LCD1602 backpack pin | → | ESP32-C3 SuperMini | Notes |
|---|---|---|---|
| `GND` | → | `GND` | Do this one **first**. Any GND pin on the board works. |
| `VCC` | → | `5V` | **5 V, not 3V3.** See below. The `5V` pin is live whenever USB is plugged in. |
| `SDA` | → | `GPIO 7` | Data. Labelled `7` on the silkscreen. |
| `SCL` | → | `GPIO 8` | Clock. Labelled `8`. |

The backpack's 4 pins are usually printed in the order **`GND` `VCC` `SDA` `SCL`** —
check the silkscreen rather than assuming, because a few batches use `VCC GND SDA SCL`
and swapping those two is the one mistake that can kill the module.

### Why 5 V (this was the "not even lit" bug)

An HD44780 module built for 5 V shows **nothing** at 3.3 V: the backlight LED sits behind
a series resistor sized for 5 V so it barely glows, and the contrast bias never gets high
enough to drive the segments. On I2C the backpack then doesn't answer at all, so the
firmware can't even see it. An earlier version of this file said to use 3V3 — that was
wrong, and it's what kept the panel dark.

**The catch that comes with 5 V:** the backpack pulls SDA and SCL up to *its own* VCC, so
at 5 V those two lines idle at 5 V while the C3's GPIOs are 3.3 V parts (absolute max
≈ 3.6 V). Pick one before leaving it powered for long:

| Option | What to do | Trade-off |
|---|---|---|
| **Remove the pull-ups** *(recommended)* | Desolder the two resistors marked `472` (4.7 kΩ) on the backpack. The ESP32's internal pull-ups then drive the bus. | Free, permanent, keeps one bus. Fine at 100 kHz with short wires — which is what `I2C_CLOCK_HZ` is set to. |
| **Level shifter** | Put a BSS138 4-channel module between the C3 and the backpack: LV → `3V3`, HV → `5V`, and run SDA/SCL through it. | Textbook-correct, no soldering on the LCD. Costs a part and two more jumpers. |
| **Direct, as-is** | Nothing. | Works, and plenty of people run it this way — but it *is* out of spec and stresses GPIO 7/8 over time. Fine to prove the panel works; don't leave it. |

### Powering both rails from the USB-C port alone

Nothing to build. On this board the **5V pin is USB VBUS passed straight through, with no
OR-ing diode** (see [Powering the board from a battery](#powering-the-board-from-a-battery)),
so plugging in the USB-C makes the 5V pin live at ~5 V. The **3V3 pin** is the onboard
**ME6211 LDO** (500 mA) fed from that same VBUS, so it comes up at the same moment. Both
rails are powered by the one cable.

The two rails do **not** compete, which is the point of putting the LCD on 5V:

| Rail | Source | What draws from it | Budget |
|---|---|---|---|
| `5V` | USB VBUS direct | LCD backlight + HD44780 + PCF8574 — **~25 mA** | limited by the USB port (500 mA min) |
| `3V3` | ME6211 LDO off VBUS | ESP32-C3 (~40–55 mA idle, **300 mA Wi-Fi TX peaks**), MPU6050, MAX30105 | LDO max **500 mA** |

Moving the LCD to 5V takes its backlight current *off* the LDO instead of adding to it — the
3V3 rail has more headroom this way, not less. Worst case (Wi-Fi transmitting, backlight on)
is roughly **330 mA total** against a 500 mA USB port. Comfortable.

Three practical notes:

- **Use a decent supply.** A phone charger brick beats an unpowered hub or a long thin cable —
  VBUS sag shows up as a dim backlight and unreadable contrast before it shows up as a crash.
  You want **≥ 4.7 V** measured at the board's 5V pin under load.
- **Never feed the 5V pin from anything else while USB-C is plugged in.** No diode means the
  two sources collide. USB-only is automatically safe.
- **If the 5V pin measures 0 V**, your clone populated a diode/jumper the reference design
  doesn't. Then either run the LCD at 3V3 and wind the contrast pot up (dim but sometimes
  legible), or power the backpack from a separate USB supply with **grounds tied together**.

### Powering the LCD from a separate 5 V supply

If the board's `5V` pin can't drive the panel — a dead pin on your clone, or you're running
the ESP32 from a battery where no 5 V rail exists — the backpack can take 5 V from anywhere.
A USB charger with a sacrificial USB-A cable is the easiest source: **red = +5 V,
black = GND**. A power bank, a bench supply or an MB102 breadboard PSU all work the same way.

```
   5 V supply (charger / power bank / bench PSU)
     +5V ──────────────► LCD backpack VCC
     GND ──────┬───────► LCD backpack GND
               └───────► ESP32-C3 GND pin      ◄── THE COMMON GROUND. Not optional.

   ESP32-C3 GPIO 7 ────► LCD backpack SDA
   ESP32-C3 GPIO 8 ────► LCD backpack SCL
   ESP32-C3 powered separately over its own USB-C
```

Three rules, in order of how badly it goes wrong if you skip them:

1. **Tie the grounds together.** I2C signalling is referenced to ground; two supplies with
   no shared GND means SDA/SCL have no reference, the bus reads garbage or nothing at all,
   and current can find its way back through the signal pins. This is the step people skip
   and then spend an evening debugging.
2. **Do NOT connect the external 5 V to the board's `5V` pin.** That pin is USB VBUS with no
   OR-ing diode — feeding it while the USB-C is plugged in collides two supplies and can
   destroy the board or the host port. External 5 V goes to the *backpack's* VCC only.
3. **The pull-up caveat still applies** — the backpack pulls SDA/SCL to its 5 V rail
   regardless of where that rail comes from. Same mitigations as above (remove the two `472`
   resistors, or use a level shifter).

A separate supply also removes the LCD's ~25 mA from the USB port's budget entirely, which
is useful if you're on a weak port and seeing VBUS sag.

### Making sure the wires are actually connected

The firmware tells you this now — you don't need a multimeter to get an answer. Flash it,
open the serial monitor at **115200**, and read the boot log:

```
[watch] I2C scan (boot) SDA=7 SCL=8 @100000 Hz:
    0x27  PCF8574 (LCD backpack)
    0x68  MPU6050
[watch] LCD1602 16x2 found at 0x27 (SDA=7 SCL=8)
```

You can re-run that scan any time by typing **`i2c`** into the serial monitor, or **`d`**
to print display state and retry a panel that wasn't found. Read the result like this:

| What the scan says | What it means | Fix |
|---|---|---|
| `0x27` (or `0x3F`, or anything in `0x20-0x27` / `0x38-0x3F`) listed | Backpack is powered and both data wires are good. | Nothing — if the screen is still blank it's **contrast**, see below. |
| `0x68` listed but no backpack address | The bus itself works (the MPU proves SDA/SCL are fine) — the LCD alone is unpowered or its 4 wires aren't landing. | Check `VCC` is on **5V** and `GND` is shared. Re-seat the backpack's 4 jumpers. |
| `(nothing answered)` | No device at all — the bus is broken, not just the LCD. | SDA/SCL swapped, or a dead GND. Verify GPIO 7 → `SDA` and GPIO 8 → `SCL`, not reversed. |
| `!! too many hits: SDA is stuck LOW` | SDA is shorted to GND, or a module is half-powered and dragging the line. | Unplug modules one at a time until the count drops. |

Two checks the scan can't make for you:

- **Backlight jumper.** The two-pin jumper at the corner of the backpack must be fitted,
  or the backlight never lights no matter how good the wiring is.
- **Contrast pot.** The blue trimmer on the backpack. If the address shows up in the scan
  but the screen is blank or solid-blocks, turn the pot slowly through its range — there's
  a narrow band where text becomes readable. Do this with the firmware running.

**If you'd rather check with a multimeter:** measure between the backpack's `VCC` and
`GND` pins with USB plugged in — you want **~5 V** (≈4.7 V is normal). Then continuity
(beep) from GPIO 7's silkscreen pad to the backpack's `SDA` pin, and GPIO 8 to `SCL`.
With power **off**, SDA-to-GND and SCL-to-GND must *not* beep; if either does, that line
is shorted.

The panel is re-probed every `DISPLAY_SELF_HEAL_MS` (5 s), so **you can fix wiring with
the watch running** — reconnect the wire and the screen comes up on its own within a few
seconds. No reset, no reflash.

### Address

Not fixed by the part. PCF8574**T** backpacks land in `0x20`–`0x27` (usually `0x27`);
PCF8574**AT** ones in `0x38`–`0x3F` (usually `0x3F`); the A0/A1/A2 solder jumpers shift
it further. `LCD_I2C_ADDR` in `config.h` is only the *first* address tried — the firmware
probes both blocks and uses whatever answers, then prints it. A mismatch is not fatal and
needs no config change.

### What the 16-pin header does

Reference only — the backpack occupies all 16, and none of them go to the ESP32.

| LCD pin | Name | Backpack drives it from |
|---:|---|---|
| 1 | `VSS` (GND) | GND |
| 2 | `VDD` (+5 V) | VCC |
| 3 | `V0` (contrast) | the blue trimmer pot |
| 4 | `RS` | PCF8574 `P0` |
| 5 | `RW` | PCF8574 `P1` (tied for write) |
| 6 | `E` (enable) | PCF8574 `P2` |
| 7–10 | `D0`–`D3` | unused (4-bit mode) |
| 11–14 | `D4`–`D7` | PCF8574 `P4`–`P7` |
| 15 | `A` (backlight +) | VCC via the backlight jumper |
| 16 | `K` (backlight −) | PCF8574 `P3` — this is how `lcd.backlight()` works |

`DISPLAY_TYPE` in `config.h` selects the driver: `DISPLAY_OLED` (default, unchanged) or
`DISPLAY_LCD1602`. A **2004 (20×4)** module also works — just set `LCD_COLS`/`LCD_ROWS`.

---

## Resistors — what each one is for

### Buttons: 330 Ω in series to 3.3V, active-high

Wire each button `GPIO → button → 330 Ω → 3.3 V`. The firmware sets `INPUT_PULLDOWN`
(`BTN_ACTIVE_HIGH 1` in `config.h`), so the C3's internal pulldown (~45 kΩ) holds the pin
LOW when the button is open — **no external pulldown needed**. Pressing throws the pin
HIGH through the 330 Ω series resistor.

The series resistor is the one worth adding. It costs nothing and protects the pin:

| Value | Pressed logic level | Fault current if the pin is ever driven LOW | Verdict |
|---|---|---|---|
| 0 Ω (wire) | 3.30 V | **3.3 V limited only by the pad** | works, but unprotected |
| **330 Ω** | 3.28 V | 10 mA | **recommended** |
| 1 kΩ | 3.23 V | 3.3 mA | safest, still fine |
| 4.7 kΩ | 2.99 V | 0.7 mA | low margin against V_IH — don't |

The pressed level needs to clear V_IH ≈ 0.75 × 3.3 V ≈ 2.48 V. With 330 Ω in series against
the 45 kΩ internal pulldown, the pin sees `3.3 × 45000/(45000+330) = 3.28 V` — a very
solid HIGH, and even at 4.7 kΩ you still clear V_IH but with less margin on a dirty
contact, which is why the table stops there.

The fault case is real: if a future firmware change ever configures GPIO 4 or 5 as an
`OUTPUT` and drives it LOW while the button is pressed, a bare wire to 3.3 V is a dead
short across the pin driver. 330 Ω caps that at 10 mA, well inside the ESP32-C3's 40 mA
per-pin limit, and turns a destroyed board into a non-event.

**Optional:** 100 nF from each GPIO to GND for hardware debounce (τ = 45 kΩ × 100 nF ≈
4.5 ms). Not required — `fitness_watch.ino` already debounces in software over 50 ms — but
if you add it, the 330 Ω also limits the cap's discharge spike on each press.

### I2C bus: total pull-up should land near 2.2 kΩ

This is the "total resistance of the whole circuit" number that actually matters. Every I2C
breakout ships with its own pull-ups, and putting three modules on one bus wires all of them
**in parallel** — so the bus gets *stronger* (lower resistance) with each module you add:

| Module | Typical onboard pull-ups |
|---|---|
| SSD1306 OLED | 4.7 kΩ |
| MAX30102 breakout | 4.7 kΩ |
| MPU6050 (GY-521) | 2.2 kΩ |

```
1/R = 1/4700 + 1/4700 + 1/2200  →  R ≈ 1.14 kΩ on each of SDA and SCL
```

The legal window for a 3.3 V bus:

- **Minimum ≈ 967 Ω** — each device must be able to pull the line to V_OL (0.4 V) while
  sinking no more than the I²C-spec 3 mA: `(3.3 − 0.4) / 0.003`.
- **Maximum ≈ 2.4 kΩ at 400 kHz** — the line has to rise within the 300 ns fast-mode limit
  against ~150 pF of bus capacitance (3 modules + short wires): `300 ns / (0.8473 × 150 pF)`.

So **1.14 kΩ is legal but sits right on the floor** — about 170 Ω above the minimum. It will
work, and if the bus is flaky the cause is wiring, not this. What it costs you is power:
2.9 mA flows every time a line is pulled low, which on a battery-powered watch is real.

**Recommendation:** desolder or scrape the pull-up pair off **two** of the three modules
(easiest on the OLED and the MAX30102) and keep the GY-521's 2.2 kΩ. That lands the bus at
2.2 kΩ — dead centre of the window — and roughly halves the bus's idle current. If you'd
rather not touch the hardware, leave it: drop `I2C_CLOCK_HZ` to `100000` in `config.h` if you
ever see errors, and accept the extra draw.

Do **not** add series resistors on SDA/SCL. They are open-drain lines; series resistance
slows edges and buys nothing.

### Battery divider (M7): 2 × 100 kΩ

> Full battery/charger/boost design — including the charge-from-itself loop to avoid —
> is in **[POWER.md](POWER.md)**. The firmware side is done (`power.h` / `power.cpp`);
> the hardware is not built yet.


A LiPo's 4.2 V exceeds the ADC's range, so halve it: `BAT+ → 100 kΩ → GPIO 3 → 100 kΩ → GND`,
giving 2.1 V at full charge, inside ADC1's 11 dB range. That pair draws 21 µA continuously
(`4.2 V / 200 kΩ`) — negligible next to the ~45 mA below, and low enough to leave permanently
connected. Add 100 nF from GPIO 3 to GND to steady the reading. Must be **ADC1 (GPIO 0–4)**;
ADC2 is unusable while Wi-Fi is on.

### Current budget (the number that sizes the battery)

| Load | Draw |
|---|---|
| ESP32-C3 active, Wi-Fi off | 20–25 mA |
| SSD1306 OLED (content-dependent) | 10–20 mA |
| MPU6050 | ~3.9 mA |
| MAX30105 @ `setup(0x1F, 4, 2, 400, 411, 4096)` | ~3 mA |
| I2C pull-ups @ 1.14 kΩ | 1.5–2.9 mA |
| **Steady total, Wi-Fi off** | **≈ 40–55 mA** |
| Wi-Fi TX bursts (M4/M5) | 300 mA peaks |

A 500 mAh LiPo therefore gives roughly **10 hours** as the firmware stands — fine for bench
work, not for a watch. The three levers, in order of payoff: blank the OLED on a timeout
(M7), drop the MAX30105 LED power and put it in shutdown when `fingerPresent` is false, and
batch Wi-Fi syncs rather than holding an association. Sizing the battery is an M7 decision;
the peaks matter more than the average, so the cell also needs to source 300 mA without
browning out the LDO.

---

## Powering the board from a battery

The SuperMini has **no onboard charger** and, critically, its **5 V pin is wired straight to
USB VBUS with no OR-ing diode**. Two hard rules follow:

1. **Never power the 5 V pin while the USB-C is plugged in** — the two sources collide and
   can destroy the board, your battery, or the PC's USB port.
2. **Never feed a raw Li-ion into the 3 V3 pin** — a full cell is 4.2 V, past the ESP32-C3's
   3.6 V absolute max. The 3 V3 pin is only safe as an input from a *regulated* 3.3 V source.

The board's own regulator is a **ME6211 LDO (max 500 mA)**, which is exactly what the
300 mA Wi-Fi TX peaks need — so the simplest battery setup just uses it:

```
Li-ion / LiPo 3.7 V (e.g. 500 mAh)
   │
   └─ TP4056 charger module (DW01 + FS8205 protection)
        OUT+  ──► board 5V pin
        OUT−  ──► board GND
        charge through the TP4056's own USB port
```

- The TP4056's OUT is the cell itself (3.0–4.2 V) — fine for the 5 V pin's 3.3–6 V input
  range. Its onboard LDO is fed straight to the 3.3 V rail everything (chip + OLED +
  MAX30102 + MPU6050) runs off; the ME6211's 500 mA budget covers the whole watch.
- Charge through the **TP4056's USB port**, never the board's — that keeps rule 1 enforced
  automatically.
- **Set the charge current to match the cell.** The TP4056 ships at 1 A (`Rprog` = 1.2 kΩ),
  too hot for a watch cell. For a 500 mAh cell use ~0.5C ≈ 250 mA → `Rprog ≈ 4.7 kΩ`
  (`I = 1200 / Rprog`); a 1 A charge into a small cell risks overheating. One catch: both
  the charger and the board drain the same battery node while USB is plugged in, so a
  slow-charge cell can't "charge up" and run the watch at full tilt at once — fine for
  bench work, just don't expect net charging under load.
- **Caveat:** the ME6211's dropout means the board browns out once the cell drops below
  ~3.5–3.6 V, wasting the last ~20% of capacity. To use the full cell range you'd instead
  run the battery through a low-dropout 3.3 V regulator (MCP1700 / HT7333) into the 3 V3
  pin — but those only source ~250 mA, too weak for Wi-Fi TX, so this path is the better
  trade for a build that syncs. (An alternative that keeps full range *and* the 500 mA
  budget: a 5 V boost converter between battery and 5 V pin.)
- For M7 battery monitoring: the divider above (`BAT+ → 100 kΩ → GPIO 3 → 100 kΩ → GND`,
  or read straight off the TP4056 OUT+ divider) must live on **ADC1 (GPIO 0–4)**.

---

## Measuring the display

> **Resolved for this build (0.96" SSD1306 128×64, I2C `0x3C`):** the correct U8g2
> constructor is **`U8G2_SSD1306_128X64_NONAME_F_HW_I2C`** (`OLED_INIT_ALT0 0`). Watch
> out: the ALT0 init passes a *sparse* ruler pattern but with real text it **interleaves
> the rows** — every UI line overlaps the next. If you ever get a *different* panel,
> use the measurement sketch below.

If the UI renders too large, runs off the glass, or shows doubled/interleaved rows, the
firmware's `128×64 SSD1306` assumption is wrong for your panel. **Don't guess — measure it.**

`watch/firmware/screen_measure/screen_measure.ino` cycles through every plausible driver
(SSD1306 128×64 / 128×32 / 64×48 / 72×40, SH1106 128×64, SH1107 64×128 portrait, and the
ALT0 init) and draws patterns you can count.

```bash
arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc watch/firmware/screen_measure
arduino-cli upload  --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc -p COM5 watch/firmware/screen_measure
```
(Or run the **Build Watch Firmware** action and flash the artifact — no local toolchain needed.)

It auto-advances every 6 s. Press either button, or send any character at 115200 baud, to
freeze on the mode that looks right. Then:

**The RULER pattern puts one dot every 8 pixels**, with a longer tick on every 4th dot to
mark 32-pixel groups, and a small filled square in the top-left corner as dot #1:

```
dots across the top  × 8  =  panel WIDTH  in pixels
dots down the left   × 8  =  panel HEIGHT in pixels
```

A true 128×64 shows **16 dots across** (4 group ticks) and **8 dots down** (2 group ticks).
16 across but only 4 down means you have a 128×32 panel.

Three other patterns cross-check the count — cycle them with button A or `p`:

| Pattern | What it tells you |
|---|---|
| **RULER** | exact pixel dimensions, by counting |
| **ROWS** | numbered 8-pixel bands — the highest band you can see is the panel's real page count |
| **FILL** | every pixel lit, so you see the true illuminated area of the glass at a glance |
| **CORNER** | corner brackets + centre crosshair — fastest "is anything clipped" check |

The sketch also runs an I2C scan at boot and prints a fill-in worksheet over serial (`r`
reprints it). **Report back the mode number and the two dot counts** — that's enough to
correct `OLED_WIDTH` / `OLED_HEIGHT` in `config.h` and the U8g2 constructor in
`fitness_watch.ino`.

---

## Phone connectivity (M4 + M5)

The watch talks to your phone two ways. **BLE is the controller; WiFi is the data path.**

1. **Bluetooth LE** — the watch *always* advertises as **"FitnessAI Watch"**. Any BLE
   device (phone app, or the web app via Web Bluetooth) can connect and pair it, sync
   its clock, and send commands.
2. **WiFi** — once paired, the watch uploads heart-rate/step readings **directly** to the
   backend (`POST https://backend-seven-topaz-23.vercel.app/ingest/` with
   `Authorization: Bearer <device token>`), so the phone never has to carry the data.
   Readings are buffered in RAM while offline and flushed automatically when the link
   returns.

> **Steps are sent as a per-reading DELTA, not the cumulative counter.** The backend
> *sums* the `steps` field across readings (`routes/charts.py` → `steps_by_day[...] +=`,
> `routes/user.py` → `total_steps=sum(steps)`), and the Android tracker already posts a
> delta — so this is the platform-wide contract. Posting the running total once a minute
> would multiply a day's steps by the number of readings (~1440×). `syncTick()` in
> `net.cpp` keeps the baseline and sends the difference; `stepCount` in the sketch stays
> cumulative for the on-screen Steps display.

Both pairing paths store the same thing in flash (NVS): WiFi SSID/password, the device
token, and a `paired` flag. There is no "master" — pick whichever is convenient.

### Pairing path 1 — BLE control peripheral

Scan for **FitnessAI Watch** from your phone, connect, and write:

| Characteristic | UUID | Access | Meaning |
|---|---|---|---|
| SSID | `0000F1A1-…-00805F9B34FB` | write | the phone-hotspot / home WiFi name |
| PASS | `0000F1A2-…` | write | that WiFi's password |
| TOKEN | `0000F1A3-…` | write | the `fit_…` code from the web app's **Devices** page |
| TIME | `0000F1A4-…` | write | 8-byte little-endian unix epoch → sets the watch clock |
| CMD  | `0000F1A5-…` | write | `apply` \| `sync` \| `stat` \| `unpair` \| `reboot` |
| NAME | `0000F1A6-…` | read  | `FitnessAI Watch` |
| STATE| `0000F1A7-…` | read / notify | live status: `paired:1 wifi:1 q:3 up:12 http:200` |

After writing SSID + PASS + TOKEN, send `CMD=apply`: the watch connects to that WiFi,
pings the backend, and marks itself paired (visible in the STATE string). `CMD=sync`
forces an upload now; `CMD=unpair` wipes the settings and reboots into setup.

(Service UUID `0000F1A0-0000-1000-8000-00805F9B34FB`.)

### Pairing path 2 — WiFi captive portal

If you'd rather use a browser: when unpaired, the watch also boots an open SoftAP called
**`FitnessAI Watch`** (same name as the BLE advertisement). Join it from your phone, open
**http://192.168.4.1** (captive
portals on iOS/Android open it automatically), fill in SSID/password/token, and the page
shows live connect progress. The portal shuts itself down ~12 s after a successful pair.

### The clock ("dates and everything")

No RTC chip needed. The watch gets real time from **NTP** over WiFi when connected, and
you can also **sync it instantly over BLE** by writing the current epoch to the `TIME`
characteristic. Only if the watch must know the date after being powered *off* for days
with no phone would a battery-backed RTC (DS3231, not DS1307 — the DS1307 shares the
MPU6050's `0x68` address) be worth adding; for this build it isn't.

### Auto-orientation

The UI re-rotates itself so it always reads upright. The MPU6050's **accelerometer**
provides the absolute gravity vector (gyro integration alone would drift), and the
**gyro** gates the switch so the screen never flips mid-gesture. `setDisplayRotation()`
applies U8G2_R0–R3. Tune `ORIENT_*` in `config.h`; if the sensor is soldered rotated on
your panel, adjust the direction table in `orientScreen()`.

### Display stability ("the screen tweaks")

Two symptoms — the screen looks like it "changed orientation" while sitting still, and
text looks garbled/interleaved — are both **corrupted frames**, not the orientation
logic. When the MPU is missing, `orientScreen()` never even runs, and a half-written or
column-shifted SSD1306 frame can *look* rotated. There's no read-back from the panel, so
`displaySelfHeal()` in `fitness_watch.ino` periodically re-runs `u8g2.begin()` +
repaints to force the controller back in sync. The interval is **`DISPLAY_SELF_HEAL_MS`**
in `config.h` (default 5000 ms). The corruption is triggered by a radio burst landing
mid-frame or a marginal bus — the lasting cure is electrical: 4.7 kΩ pull-ups, a 100 nF
cap on the OLED's supply, and short wires (see "Resistors"). U8g2's `begin()` does *not*
reset the display rotation, so the self-heal can't flip orientation by itself.

### Serial console (bring-up)

115200 baud over USB. Commands: `p` status, `s` force sync, `t` print the paired token,
`r` reboot, `clear` wipe pairing and reboot, `h` help. All boot/network/sync events log
with a `[watch]`/`[net]`/`[ble]` prefix; set `DEBUG_SERIAL 0` in `config.h` to silence the
chatty logs.

---

## Build & flash

Uses **arduino-cli** (or the Arduino IDE — same board + library).

```bash
# one-time setup
arduino-cli config init
arduino-cli config add board_manager.additional_urls \
  https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32
arduino-cli lib install "U8g2" "SparkFun MAX3010x Pulse and Proximity Sensor Library" \
  "Adafruit MPU6050" "Adafruit Unified Sensor" "Adafruit BusIO" "ArduinoJson"

# compile (huge_app = 3 MB app partition; the BLE + HTTPS stacks need it)
arduino-cli compile --fqbn esp32:esp32:esp32c3:PartitionScheme=huge_app watch/firmware/fitness_watch

# flash (replace the port)
arduino-cli upload  --fqbn esp32:esp32:esp32c3:PartitionScheme=huge_app -p COM5 watch/firmware/fitness_watch
```

### Schematic

The full circuit — including the automatic USB/battery switchover — is a KiCad
10 project in **[hardware/](hardware/)**, ERC clean, with PDF/SVG/netlist/BOM in
`hardware/export/`. The sheet is generated from `hardware/generate_schematic.py`,
so the netlist and this firmware's `config.h` stay in step.

### No hardware? Run it in the simulator

The firmware also runs on an emulated ESP32-C3 with an emulated OLED, IMU,
buttons and battery slider — see **[sim/README.md](sim/README.md)**.

```bash
cd watch/sim
python simctl.py build     # compile with -DSIM_BUILD=1
python simctl.py lint      # validate the emulated board (offline)
python simctl.py test      # automated scenarios (free Wokwi token)
```

It runs the same `.bin` this section builds, so the display driver, beat
detector, button state machine and battery curve are all exercised for real.
`SIM_BUILD` is set only by `simctl.py`; the commands above are unaffected by it.

**Arduino IDE:** Boards Manager → install *esp32* → select **ESP32C3 Dev Module** (set
*Partition Scheme → Huge APP (3MB No OTA/1MB SPIFFS)* in Tools) → Library Manager →
install **U8g2**, **SparkFun MAX3010x Pulse and Proximity Sensor Library**, **Adafruit
MPU6050** (pulls in Adafruit Unified Sensor + Adafruit BusIO), **ArduinoJson** → open
`fitness_watch.ino` → Upload.

---

## Display library: U8g2 (not Adafruit)

We use **U8g2** because a watch lives or dies on its fonts — big, crisp HR/step/clock
digits (e.g. `u8g2_font_logisoso*`). Adafruit_GFX only scales one 5×7 font, which looks
blocky when enlarged. Fonts live in flash (4 MB), so the richness is essentially free.
Full-buffer mode (`U8G2_..._F_HW_I2C`) is used — the C3 has ample RAM.

---

## Roadmap

- [x] **M1** — screen + program skeleton: boot loading screen → live home screen
- [x] **M2** — button navigation (A tap = prev screen, A hold = home, B single = home, B double = display mute) across Home/HR/Steps/Status.
      (The home clock is a real NTP clock now — syncs over WiFi or the BLE `TIME` char.)
- [x] **M3** — sensors: MPU6050 step counter + MAX30105 heart rate (SpO2 not yet computed)
- [x] **M3.5** — auto-orientation: the UI rotates to stay upright via the MPU6050 accel+gyro
- [x] **M4** — pairing: WiFi captive-portal **and** a Bluetooth LE control peripheral
      (pair / time sync / commands), both storing a device token in NVS
- [x] **M5** — sync to backend `/ingest` with the paired device token (offline RAM queue)
- [ ] **M6** — GPS workout recording → `/routes` (NEED a free GPIO for GPS TX)
- [ ] **M7** — battery monitor **(firmware done — `power.h`/`power.cpp`, see [POWER.md](POWER.md);
      hardware not built)**, sleep/display-timeout, haptics, aggressive BLE/Wi-Fi power saving

Pairing reuses the platform's device-token system (web app → **Devices** → generate code →
enter on the watch via BLE or the portal). Data lands in Supabase under your account;
analysis runs server-side.
