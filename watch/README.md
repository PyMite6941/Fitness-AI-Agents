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
| **SSD1306 128×64 OLED** | display | I2C (shared) | `0x3C` |
| **MPU6050** | accel/gyro → steps | I2C (shared) | `0x68` |
| **MAX30102** | heart rate / SpO₂ | I2C (shared) | `0x57` |
| 2 × tactile buttons | navigation | GPIO (active-low) | — |
| **NEO-6M GPS** *(optional)* | route tracking | UART1 | — |
| Battery + divider *(optional)* | charge level | ADC1 | — |

All three I2C devices share **one two-wire bus** — critical on the pin-starved C3.

---

## Pin map (defaults in `config.h`)

| Signal | GPIO | Why this pin |
|---|---:|---|
| I2C **SCL** | 8 | I2C idles HIGH, so using this strapping pin is safe at boot. (Onboard LED may flicker — harmless.) Shared by OLED + MAX30105 + MPU6050. |
| I2C **SDA** | 7 | Non-strapping; moved off the old GPIO 9 so only one strapping pin (8) is used for I2C. Shared bus, same as SCL. |
| MAX30105 **INT** | −1 | Unwired — firmware polls the sensor every loop() instead of using the interrupt line. |
| MPU6050 **INT** | −1 | Unwired — same, polling only. |
| **Button A** (Back) | 4 | Non-strapping — safe even if held at reset. |
| **Button B** (Select) | 5 | Non-strapping. |
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

## Resistors — what each one is for

### Buttons: 330 Ω in series to GND, no pull-up

Wire each button `GPIO → button → 330 Ω → GND`. The firmware sets `INPUT_PULLUP`, so the
**external pull-up is not needed** — the C3's internal one (~45 kΩ) already holds the pin
HIGH when the button is open.

The series resistor to GND is the one worth adding. It costs nothing and protects the pin:

| Value | Pressed logic level | Fault current if the pin is ever driven HIGH | Verdict |
|---|---|---|---|
| 0 Ω (wire) | 0 mV | **1 A limited only by the pad** — kills the GPIO | works, but unprotected |
| **330 Ω** | 24 mV | 10 mA | **recommended** |
| 1 kΩ | 72 mV | 3.3 mA | safest, still fine |
| 4.7 kΩ | 312 mV | 0.7 mA | too close to V_IL — don't |

The pressed level has to stay under **V_IL ≈ 0.25 × 3.3 V = 0.83 V**. With the internal
45 kΩ pull-up, a 330 Ω leg to ground gives `3.3 × 330/45330 = 24 mV` — a very solid LOW
with ~35× margin. At 4.7 kΩ you are at 312 mV, still technically low but with no margin for
a dirty contact, which is why the table stops there.

The fault case is real: if a future firmware change ever configures GPIO 4 or 5 as an
`OUTPUT` and drives it HIGH while the button is pressed, a bare wire to GND is a dead short
across the pin driver. 330 Ω caps that at 10 mA, well inside the ESP32-C3's 40 mA per-pin
limit, and turns a destroyed board into a non-event.

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

### Battery divider (M7, not wired yet): 2 × 100 kΩ

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
`FitnessAI-<xxxxxx>`. Join it from your phone, open **http://192.168.4.1** (captive
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

### Serial console (bring-up)

115200 baud over USB. Commands: `p` status, `s` force sync, `r` reboot, `clear` wipe
pairing and reboot, `h` help. All boot/network/sync events log with a `[watch]`/`[net]`/
`[ble]` prefix; set `DEBUG_SERIAL 0` in `config.h` to silence the chatty logs.

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
- [x] **M2** — button navigation (tap = cycle screens, hold = home) across Home/HR/Steps/Status.
      (The home clock is a real NTP clock now — syncs over WiFi or the BLE `TIME` char.)
- [x] **M3** — sensors: MPU6050 step counter + MAX30105 heart rate (SpO2 not yet computed)
- [x] **M3.5** — auto-orientation: the UI rotates to stay upright via the MPU6050 accel+gyro
- [x] **M4** — pairing: WiFi captive-portal **and** a Bluetooth LE control peripheral
      (pair / time sync / commands), both storing a device token in NVS
- [x] **M5** — sync to backend `/ingest` with the paired device token (offline RAM queue)
- [ ] **M6** — GPS workout recording → `/routes` (NEED a free GPIO for GPS TX)
- [ ] **M7** — battery monitor, sleep/display-timeout, haptics, aggressive BLE/Wi-Fi power saving

Pairing reuses the platform's device-token system (web app → **Devices** → generate code →
enter on the watch via BLE or the portal). Data lands in Supabase under your account;
analysis runs server-side.
