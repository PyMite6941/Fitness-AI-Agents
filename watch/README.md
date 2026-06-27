# FitnessAI Watch — Firmware

Open-hardware smartwatch that pairs to the [FitnessAI](https://fitness-ai-agents.vercel.app)
platform: it tracks heart rate, steps, and GPS workouts on-device and syncs them to your
account, where the AI does the analysis. Built on a **$3 ESP32-C3** and a handful of I2C
modules — no proprietary ecosystem.

> **Status:** Milestone 1 — screen + program skeleton: boot loading screen → live home
> screen (placeholder clock). Compiles clean for `esp32:esp32:esp32c3` (25% flash, 4% RAM);
> not yet tested on hardware. **`config.h` is currently screen-only** (I2C + OLED); the
> full pin map below is the target wiring, added back as each peripheral milestone lands.

```
watch/firmware/fitness_watch/
  fitness_watch.ino   ← main sketch (setup/loop, UI)
  config.h            ← ALL wiring lives here — the only file you edit for your build
watch-archive/
  wiring.md           ← physical wiring reference (legacy, being refreshed)
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
| I2C **SDA** | 8 | I2C idles HIGH, so using this strapping pin is safe at boot. (Onboard LED may flicker — harmless.) |
| I2C **SCL** | 9 | Same; 8/9 is the standard SuperMini I2C pair. |
| **Button A** (Back) | 4 | Non-strapping — safe even if held at reset. |
| **Button B** (Select) | 5 | Non-strapping. |
| **GPS RX** | 6 | Any GPIO works as UART via the C3 matrix. |
| **GPS TX** | 7 | — |
| **Battery ADC** | 3 | Must be **ADC1** (GPIO 0–4); ADC2 is unusable with Wi-Fi on. |
| Vibration | −1 | Disabled (not wired). |
| Serial / USB debug | 20/21 | Reserved by USB-serial; left free. |

### ESP32-C3 pin rules (why the map is what it is)
- **Usable GPIOs:** 0–10, 20, 21 (GPIO 20/21 are USB-serial — keep for debug).
- **Strapping pins: 2, 8, 9.** Don't let them be driven LOW *at reset*. I2C is fine (pulled up); **never put a button on 2/8/9** (a press during reset = wrong boot mode).
- **ADC:** only **ADC1 = GPIO 0–4** works while Wi-Fi is on. Battery sense must live there.
- **Free pins after this map:** 0, 1, 2, 10 — room for extra buttons, a buzzer, or a charge-status line.

**To change wiring:** edit `config.h` only. Every GPIO is `#define`d there; the rest of the
firmware never hard-codes a pin. Set any optional pin to `-1` to disable it.

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
arduino-cli lib install "U8g2"

# compile
arduino-cli compile --fqbn esp32:esp32:esp32c3 watch/firmware/fitness_watch

# flash (replace the port)
arduino-cli upload  --fqbn esp32:esp32:esp32c3 -p COM5 watch/firmware/fitness_watch
```

**Arduino IDE:** Boards Manager → install *esp32* → select **ESP32C3 Dev Module** →
Library Manager → install **U8g2** → open `fitness_watch.ino` → Upload.

---

## Display library: U8g2 (not Adafruit)

We use **U8g2** because a watch lives or dies on its fonts — big, crisp HR/step/clock
digits (e.g. `u8g2_font_logisoso*`). Adafruit_GFX only scales one 5×7 font, which looks
blocky when enlarged. Fonts live in flash (4 MB), so the richness is essentially free.
Full-buffer mode (`U8G2_..._F_HW_I2C`) is used — the C3 has ample RAM.

---

## Roadmap

- [x] **M1** — screen + program skeleton: boot loading screen → live home screen
- [ ] **M2** — clock / home screen + button navigation (tap = cycle, hold = action)
- [ ] **M3** — sensors: MPU6050 step counter + MAX30102 heart rate
- [ ] **M4** — Wi-Fi pairing (phone hotspot captive portal) + device-token storage
- [ ] **M5** — sync to backend `/ingest` with the paired device token (offline queue)
- [ ] **M6** — GPS workout recording → `/routes`
- [ ] **M7** — battery monitor, sleep/display-timeout, haptics

Pairing reuses the platform's device-token system (web app → **Devices** → generate code →
enter on the watch). Data lands in Supabase under your account; analysis runs server-side.
