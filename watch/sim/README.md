# FitnessAI Watch — Simulator

Runs the **real watch firmware** on an emulated ESP32-C3, with an emulated
SSD1306, **LCD1602**, MPU6050, buttons and a battery slider. No hardware, no
soldering, no USB cable.

This is not a mock of the watch. Wokwi emulates the RISC-V core and the I2C bus,
and the thing it executes is the same `.bin` that would be flashed to the board.
The display driver, the beat detector, the button state machine, the step
counter and the battery curve all run as written.

```bash
python simctl.py build     # compile with -DSIM_BUILD=1
python simctl.py lint      # validate diagram.json  (no token needed)
python simctl.py test      # run the scenario suite (needs a token)
python simctl.py run       # interactive session    (needs a token)
```

---

## What is real and what is faked

| Piece | In the simulator | Verified |
|---|---|---|
| ESP32-C3 core, flash, NVS, timers | **emulated** — real instructions | ✅ boots, `millis()` tracks real time |
| I2C bus (GPIO 7/8) | **emulated** — real transactions | ✅ OLED/LCD + IMU both respond |
| SSD1306 128x64 OLED | **emulated**, and screenshottable | ✅ every screen renders |
| **LCD1602 (I2C backpack)** | **emulated**, and screenshottable | ✅ every screen renders |
| MPU6050 accel/gyro | **emulated**, driven by scenario controls | ✅ shaking it counts steps |
| Buttons on GPIO 4/5 | **emulated**, with 330 Ω series resistors | ✅ taps change screens |
| **Serial** | **not delivered** — see below | ❌ 0 bytes, always |
| **ADC (battery pot on GPIO 3)** | **reads 0** — see below | ❌ pot has no effect |
| WiFi | emulated (opt in with `build --wifi`) | not tried |
| **MAX30105 heart rate** | **synthetic** — no such part exists in Wokwi | ⚠️ detects, but low (see below) |
| **BLE** | **absent** — Wokwi has no Bluetooth controller | n/a, stubbed out |

### Serial does not come through (measured)

Wokwi delivers **zero** serial bytes for this firmware. Tested both
`CDCOnBoot=cdc` (Serial = USB Serial/JTAG) and `CDCOnBoot=default`
(Serial = UART0), with and without a scenario, on both
`board-esp32-c3-devkitm-1` and `board-aitewinrobot-esp32c3-supermini`. Every
combination gives an empty `--serial-log-file` — not even the ROM boot banner.

The firmware is fine: the OLED draws, buttons respond, steps count. This is a
limit of the emulator's C3 serial. **So every scenario that asserts with
`wait-serial` cannot run yet.** They are kept because they document the intended
behaviour and will work the moment serial does; `visual.test.yaml` is the one
that runs today.

### The ADC reads 0

The slide potentiometer on GPIO3 has no effect: the Sensors screen reports
`0.00V 0% bat` no matter where the slider is set. `set-control` is accepted
without error, so either Wokwi's C3 ADC is not emulated or the pot does not
drive it. **The M7 battery thresholds therefore cannot be exercised here** —
`battery.test.yaml` needs real hardware or a bench supply.

### The two deliberate substitutes

Everything above is emulated hardware. Only these two are stand-ins, and both
are honest about it:

- **Heart rate.** `sim.h` generates a fingertip-PPG-shaped IR waveform. That
  waveform is fed to the *real* `checkForBeat()`, the *real* interval maths and
  the *real* rolling average. The simulator does not hand the UI a BPM — if beat
  detection breaks, the tests fail.
- **BLE** is stubbed to no-ops in `ble.cpp`. Calling into a Bluetooth controller
  that does not exist hangs the emulator with no output, which is indistinguishable
  from a firmware bug.

### Both display parts are wired, one is driven

`diagram.json` carries **both** the SSD1306 (`oled`, `0x3C`) and the LCD1602
(`lcd`, `0x27`) on the same emulated GPIO 7/8 bus, mirroring the real hardware
where either panel can be soldered in. Which one the firmware talks to is decided
by `DISPLAY_TYPE` in `config.h` — so `python simctl.py build` + `test` exercises
whichever display is configured. Point `visual.test.yaml`'s `part-id` at `lcd`
or `oled` to match. (The unused panel just sits on the bus and stays blank.)

Note the LCD is wired to `esp:5V`, not `3V3`, to mirror the hardware: a 5 V
HD44780 module fed 3.3 V goes dark and stops answering on I2C entirely. The
emulator does not model that — it will happily drive the panel either way — so
the diagram matches the real wiring on purpose, to keep the two from drifting.
See "Wiring the LCD1602" in `watch/README.md`.

---

## When the local compiler is blocked (Smart App Control)

`simctl.py build` needs the local riscv32 toolchain, and Windows **Smart App
Control** blocks it (`An Application Control policy has blocked this file`).
There is no exclusion list for SAC, and turning it off is irreversible. The way
around it is the cloud build, which needs neither a local toolchain nor a token:

1. Run **Build Watch Firmware** from the repo's Actions tab (or push to
   `watch/firmware/**`).
2. Download the **`fitness-watch-fitness_watch_sim-esp32c3`** artifact -- that is
   the `-DSIM_BUILD=1` variant, the one Wokwi needs (BLE off, battery on GPIO3).
   The plain `fitness_watch` artifact is the HARDWARE build and will hang in the
   emulator on `bleStart()`, which Wokwi cannot service.
3. Create a project at <https://wokwi.com/projects/new/esp32-c3>, paste this
   directory's `diagram.json` into the diagram tab, and upload the artifact's
   `fitness_watch.ino.merged.bin`.

That runs the real firmware in the browser with no token and no local compiler.

---

## Getting a token

`lint` works offline; `build` needs the local toolchain (see above). Running the simulator needs a free Wokwi CI
token:

1. Sign in at <https://wokwi.com/>
2. Create a token at <https://wokwi.com/dashboard/ci>
3. `$env:WOKWI_CLI_TOKEN = 'wok_...'` (PowerShell) or
   `export WOKWI_CLI_TOKEN=wok_...` (bash)

Prefer the browser? Create a project at <https://wokwi.com/projects/new/esp32-c3>,
paste `diagram.json` into the diagram tab, and upload
`build/fitness_watch.ino.merged.bin`. Same emulator, no token, no automation.

---

## The scenarios

Automated tests in `scenarios/`, run by `simctl.py test`. Each is a regression
test for something that actually broke during bring-up.

| Scenario | What it proves | Runs today? |
|---|---|---|
| `visual` | Boot, all five screens, button navigation, step counting, HR, display mute — captured as 12 OLED screenshots in `scenarios/shots/`. | **yes** |
| `boot` | setup() completes; the MPU6050 answers on I2C at the pins in `config.h`. | no — needs serial |
| `heartrate` | All four HR fixes: no BPM on an empty sensor, beats at the commanded rate, cleared when the finger leaves, re-touch re-measures. | no — needs serial |
| `buttons` | Debounce, tap-on-release, hold-to-home, double-tap mute. | no — needs serial |
| `battery` | The M7 thresholds by sliding the pot instead of draining a cell. | no — needs serial **and** a working ADC |
| `motion` | Step counting with debounce, auto-rotation to R3. | no — needs serial |

### What the visual run established

Run it with `python simctl.py test visual`, then look at `scenarios/shots/`.

Confirmed working end to end: the boot loading bar, the home screen (clock,
status bar, footer), the Sensors screen (`MAX30105: OK`, `MPU6050: OK`), the
Sync screen, button-A navigation, **step counting** (three commanded shakes of
the emulated IMU produced exactly `3 steps`), and heart-rate **detection** off
the synthetic waveform.

### The bug it found: the OLED write starves everything else

`u8g2.sendBuffer()` pushes a full 128×64 frame over I2C at 100 kHz — a **~90 ms
blocking call** — and `loop()` runs it every 200 ms. **Roughly 45% of the time
the main loop is inside that write, sampling nothing.** This is not a simulator
artifact: the real watch uses the same clock and the same 5 fps redraw.

Three symptoms, one cause:

| Symptom | Measured |
|---|---|
| Short button presses dropped | 150 ms presses unreliable; 400 ms presses 4/4 |
| **Double-tap mute unreachable** | **no** press width works — 100/150/200/250/300 ms all fail |
| HR reads ~⅓ of the true rate | 24 bpm against a synthetic 72 |

The double-tap is the sharpest one. It is crushed between needing *long* presses
to be observed at all and needing both releases inside `BTN_DOUBLE_TAP_MS`. Ruled
out the alternative explanation (Wokwi ignoring the display-off command) by
checking whether the panel *froze*: `enterStandby()` stops `drawScreen()`, so a
muted panel cannot keep ticking — and it kept ticking.

Full measurements, reasoning and the recommended fixes (MAX30105 FIFO for HR;
hold-instead-of-double-tap for the mute) are in `lab-notes/`.

Also measured: **boot takes ~4.8 s**, not the 2 s `BOOT_BAR_MS` implies — the 27
bar frames each cost ~90 ms to draw on top of their delay.

### Test hooks

`SIM_BUILD` adds serial commands used by the scenarios (and handy interactively):

| Command | Effect |
|---|---|
| `bpm 140` | set the synthetic pulse rate |
| `finger 0` / `finger 1` | force the finger off/on (`-1` restores the schedule) |
| `hr` | print BPM, plus an `INRANGE`/`OUTOFRANGE` verdict vs the commanded rate |
| `steps` / `steps reset` | read / zero the step counter |
| `screen` | current screen enum, standby flag, rotation |
| `batt` | validity, mV, percent, USB flag, status-bar label |

The `INRANGE` verdict is computed in firmware on purpose: Wokwi's `wait-serial`
is a plain substring match and cannot express "between 110 and 129", so the
tolerance lives in one place instead of being smeared across five yaml files.

---

## Gotchas worth knowing

**Firmware must be the `.merged.bin`.** The plain `fitness_watch.ino.bin` is the
application image only; without the bootloader and partition table the emulator
starts at a blank reset vector and prints nothing.

**Run `simctl.py lint` after any wiring change.** Wokwi silently ignores a
connection naming a pin that does not exist — no error, just a board where a
part is quietly unpowered. The linter caught four such mistakes in the first
draft of `diagram.json` — `wokwi-ssd1306` uses `DATA`/`CLK`, not `SDA`/`SCL`,
and pad names differ per board (`3V3.1` on `board-esp32-c3-devkitm-1`, plain
`3V3` on the SuperMini part this diagram now uses).

**The serial console is dead while the display is muted.** `loop()` returns
before `handleSerialCmd()` in standby, so only a button can wake it. That is why
`buttons.test.yaml` asserts on the `SCREEN OFF`/`SCREEN ON` log lines rather than
querying state.

**Battery values are pot counts, not volts.** The slider is 0–1023 across the
3.3 V rail and the firmware doubles it (`BATT_DIVIDER_RATIO`):

```
cell_mV = (value / 1023) * 3300 * 2        value = cell_mV * 1023 / 6600
```

so 635 ≈ 4.10 V, 535 ≈ 3.45 V (LOW), 450 ≈ 2.90 V (critical). `BATT_INTERVAL_MS`
is 10 s, so give every change more than 10 s before asserting.

---

## Files

```
sim/
  diagram.json        the emulated board — lint it after every edit
  wokwi.toml          points Wokwi at the built firmware + elf
  simctl.py           build / lint / test / run
  scenarios/*.yaml    automated tests
  scenarios/shots/    screenshots (generated; gitignored)
  build/              compiled firmware (generated; gitignored)
```

The simulator adds no files to the hardware build path. `SIM_BUILD` is defined
only by `simctl.py`; a normal `arduino-cli compile` or an Arduino IDE build
produces byte-for-byte the firmware it always did.
