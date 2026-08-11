# FitnessAI Watch — Simulator

Runs the **real watch firmware** on an emulated ESP32-C3, with an emulated
SSD1306, MPU6050, buttons and a battery slider. No hardware, no soldering, no
USB cable.

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

| Piece | In the simulator |
|---|---|
| ESP32-C3 core, flash, NVS, timers | **emulated** — real instructions |
| I2C bus (GPIO 7/8) | **emulated** — real transactions |
| SSD1306 128x64 OLED | **emulated**, and screenshottable |
| MPU6050 accel/gyro | **emulated**, driven by scenario controls |
| Buttons on GPIO 4/5 | **emulated**, with 330 Ω series resistors |
| Battery divider on GPIO 3 | **emulated** as a slide potentiometer |
| WiFi | emulated (opt in with `build --wifi`) |
| **MAX30105 heart rate** | **synthetic** — no such part exists in Wokwi |
| **BLE** | **absent** — Wokwi has no Bluetooth controller |

Only the last two are substitutes, and both are honest about it:

- **Heart rate.** `sim.h` generates a fingertip-PPG-shaped IR waveform. That
  waveform is fed to the *real* `checkForBeat()`, the *real* interval maths and
  the *real* rolling average. The simulator does not hand the UI a BPM — if beat
  detection breaks, the tests fail.
- **BLE** is stubbed to no-ops in `ble.cpp`. Calling into a Bluetooth controller
  that does not exist hangs the emulator with no output, which is indistinguishable
  from a firmware bug.

---

## Getting a token

`build` and `lint` work offline. Running the simulator needs a free Wokwi CI
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

| Scenario | What it proves | Runtime |
|---|---|---|
| `boot` | setup() completes; the MPU6050 answers on I2C at the pins in `config.h`; loop() runs. Screenshots the home screen. | ~10 s |
| `heartrate` | All four HR fixes: no BPM on an empty sensor, beats detected at the commanded rate, reading cleared when the finger leaves, and a re-touch re-measures instead of reporting a quartered average. | ~40 s |
| `buttons` | Debounce, tap-on-release, hold-to-home, and the double-tap display mute. Catches the regression where a hold advanced a screen *and then* went home. | ~10 s |
| `battery` | The M7 thresholds — healthy / USB / LOW / critical-radio-shed — by sliding the pot instead of draining a cell for hours. | ~60 s |
| `motion` | Step counting with debounce, and auto-rotation to R3 when gravity moves onto +X. | ~15 s |

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

**Serial must be on UART0.** The hardware build uses `CDCOnBoot=cdc`, which makes
`Serial` the USB-CDC device. Wokwi's monitor listens on UART0, so with CDC-on-boot
every `Serial.print` vanishes and every `wait-serial` times out. `simctl.py`
builds the simulator with `CDCOnBoot=default` for exactly this reason — the one
difference in the build that is a property of the emulator, not the firmware.

**Firmware must be the `.merged.bin`.** The plain `fitness_watch.ino.bin` is the
application image only; without the bootloader and partition table the emulator
starts at a blank reset vector and prints nothing.

**Run `simctl.py lint` after any wiring change.** Wokwi silently ignores a
connection naming a pin that does not exist — no error, just a board where a
part is quietly unpowered. The linter caught four such mistakes in the first
draft of `diagram.json` (`wokwi-ssd1306` uses `DATA`/`CLK`, not `SDA`/`SCL`; the
C3's 3.3 V pad is `3V3.1`, not `3V3`).

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
