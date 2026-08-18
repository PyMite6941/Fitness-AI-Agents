#pragma once
/*
 * config.h — hardware configuration for the FitnessAI Watch.
 *
 * Scope right now: SCREEN + heart-rate/SpO2 + accel/gyro (Milestone 3).
 * Edit these to match your wiring, then flash. (GPS, battery, buttons,
 * Wi-Fi come in later milestones.)
 *
 * Board: ESP32-C3 SuperMini. I2C idles HIGH, so GPIO 8 (a strapping pin) is
 * safe for the bus. Set a pin to -1 to disable.
 */

#include <stdint.h>   // uint32_t, used by the shared types at the bottom

// ── Board ────────────────────────────────────────────────────────────────────
#define BOARD_NAME      "ESP32-C3 SuperMini"
#define DEVICE_NAME     "fitness_watch"

// ── I2C bus (display + MAX30105 + MPU6050 all share this one bus) ───────────
// VERIFIED ON HARDWARE (i2c_scan/general_scan with phantom rejection): the
// MPU6050 answers at 0x68 on SDA=GPIO 7, SCL=GPIO 8. The LCD1602 did NOT answer
// anywhere — the earlier 0x27 reading at 0/2 was a floating-line phantom. An
// unpowered backpack is invisible on I2C, and a 5 V module fed 3V3 is
// effectively unpowered, so read the LCD wiring note below before re-scanning.
// If the LCD is to join this bus it must be on THESE same pins.
// Set a pin to -1 to disable.
#define PIN_I2C_SDA     7               // data
#define PIN_I2C_SCL     8               // clock
#define I2C_CLOCK_HZ    100000          // 100 kHz standard-mode. 400 kHz fast-mode
                                        // corrupts this board's marginal bus (blank
                                        // OLED, MAX30105 fails). Do not raise.

// ── Display type ─────────────────────────────────────────────────────────────
// Which panel the firmware drives:
//   DISPLAY_OLED     0 → SSD1306 128x64 OLED over I2C (U8g2). The panel this
//                        firmware was verified on. The default — do not change
//                        unless you are actually swapping the hardware.
//   DISPLAY_LCD1602  1 → HD44780 1602 (or 2004) character LCD with a PCF8574
//                        I2C backpack (LiquidCrystal_I2C library). Shares the
//                        same two I2C wires as the sensors. Auto-rotation is
//                        forced off (a character LCD has no rotation).
//
// To switch, change the value on the LAST line and rebuild. Everything else in
// this file applies to both.
#define DISPLAY_OLED      0
#define DISPLAY_LCD1602   1
#define DISPLAY_TYPE      DISPLAY_LCD1602

// ── OLED display (SSD1306 128x64) ────────────────────────────────────────────
// Confirmed panel: "0.96" I2C OLED SSD1306 128x64" (Shopee #7216498277).
// Address is 0x3C on the overwhelming majority of these modules.
#define OLED_ADDR       0x3C
#define OLED_WIDTH      128
#define OLED_HEIGHT     64

// ── Character LCD (HD44780 via PCF8574 backpack) ─────────────────────────────
// Only compiled when DISPLAY_TYPE == DISPLAY_LCD1602. A 2004 (20x4) module works
// too: set LCD_COLS/LCD_ROWS.
//
// WIRING — the backpack's 4-pin header, nothing else. The LCD's own 16-pin
// header is already soldered to the backpack; you never wire those 16 yourself.
//   LCD backpack VCC -> ESP32 5V     <-- 5V, NOT 3V3. See below.
//   LCD backpack GND -> ESP32 GND
//   LCD backpack SDA -> GPIO 7  (PIN_I2C_SDA)
//   LCD backpack SCL -> GPIO 8  (PIN_I2C_SCL)
//
// WHY 5V: an HD44780 module built for 5 V shows NOTHING at 3.3 V — the backlight
// LED barely glows behind its 5 V-sized series resistor and the contrast bias
// never drives the segments. "Not even lit" on 3V3 is this, not a code bug.
// THE CATCH: the backpack pulls SDA/SCL up to its own VCC, so at 5 V those two
// lines idle at 5 V into 3.3 V GPIOs. Pick one of the mitigations in
// watch/README.md ("Wiring the LCD1602") before leaving it powered for long.
//
// ADDRESS: not fixed by the part. PCF8574T backpacks land in 0x20-0x27 (usually
// 0x27), PCF8574AT ones in 0x38-0x3F (usually 0x3F), shifted by the A0/A1/A2
// solder jumpers. The value below is only the FIRST address tried — dispBegin()
// probes both blocks and uses whatever answers, so a mismatch is not fatal and
// the boot log prints the address it found. Type `i2c` on the serial console to
// scan on demand.
#define LCD_I2C_ADDR     0x27
#define LCD_COLS         16
#define LCD_ROWS         2

// Two SSD1306 init sequences exist for the 0.96" panel. ALT0 looked plausible
// on the sparse measurement pattern, but with real text it interleaves/squashes
// the rows (lines overlap vertically on the pair screen) -> this panel wants
// NONAME. The earlier "blank" NONAME run was the 400 kHz I2C bug, not this.
#define OLED_INIT_ALT0  0   // 0 = NONAME (measured correct), 1 = ALT0

// How often to force a full re-init + repaint of the SSD1306. A WiFi radio burst
// landing mid-frame-write (or a marginal bus) can desync the panel's internal
// address counter once in a while; the SSD1306 has no read-back, so the only way
// to recover is to re-send the init sequence. Too slow (= the old hard-coded
// 30 s) means a corrupted frame stays on screen a long time; too fast = a brief
// clear-flash every interval. 5000 ms is a good stopgap.
#define DISPLAY_SELF_HEAL_MS    5000

// DIAGNOSTIC — 1 = run with the RADIO COMPLETELY OFF (no BLE, no WiFi AP/portal,
// no NTP) and force the home screen. Use to isolate the display "tweaking":
// still corrupt with this on = electrical/bus/drawing issue; clean = the radio
// preempting the ~82 ms frame write is the corruption source. Set back to 0 for
// normal operation.
#define DISPLAY_RADIO_TEST      0

// ── Heart rate / SpO2 (MAX30105, SparkFun MAX3010x library) ──────────────────
// INT pin is unwired (polling only) — leave at -1.
#define MAX30105_ADDR   0x57
#define PIN_MAX30105_INT (-1)

// ── Accelerometer / gyro (MPU6050, Adafruit_MPU6050 library) ─────────────────
// AD0 is tied low on the breakout (or floating with its onboard pulldown) ->
// default address 0x68. INT pin is unwired (polling only) — leave at -1.
#define MPU6050_ADDR    0x68
#define PIN_MPU6050_INT (-1)

// ── Buttons (tactile) ────────────────────────────────────────────────────────
// Button A = Back / previous screen (hold = home). Button B = single-press home /
// double-press display mute (vitals keep running). Both are non-strapping GPIOs,
// so holding them at reset is safe.
#define PIN_BTN_A       4
#define PIN_BTN_B       5

// Legacy B hold-to-standby window — no longer used by the sketch (the double
// tap replaced it). Kept only for reference / config-hygiene checks.
#define BTN_STANDBY_HOLD_MS  1500

// Double-tap window for button B (GPIO5): two presses inside this time = the
// display-only screen-off toggle. Vitals (HR sampling, steps, BLE/WiFi) keep
// running — this is display mute, not sleep. Same double press wakes it back.
//
// RAISED FROM 400 ms after simulator testing showed the gesture was
// UNREACHABLE at 400: no press width worked (100/150/200/250/300 ms all failed).
// A full 128x64 frame at 100 kHz blocks loop() for ~90 ms every 200 ms, so a
// press has to be long (>=250 ms) just to be observed, while its release can
// then be confirmed up to ~140 ms late (debounce + one blind window). Two taps
// could not fit in 400 ms. See lab-notes/2026-08-12-findings.md.
//
// NOT YET VERIFIED ON HARDWARE OR IN THE SIM — Smart App Control is currently
// blocking the RISC-V compiler, so this value has not been rebuilt and re-run.
#define BTN_DOUBLE_TAP_MS    700

// Wiring polarity. The firmware reads "pressed" on either edge — pick the one
// that matches how the buttons are physically wired:
//   BTN_ACTIVE_HIGH  0 = button throws the pin LOW (GPIO→button→GND rail; the
//                        classic INPUT_PULLUP build). This is the README default.
//   BTN_ACTIVE_HIGH  1 = button throws the pin HIGH (GPIO→330R→button→3.3V rail);
//                        the pin then uses INPUT_PULLDOWN so it sits LOW until
//                        the button is pressed.
#define BTN_ACTIVE_HIGH  1

// ── Standby / display-off (battery saving) ───────────────────────────────────
// Hold button B (GPIO5) for BTN_HOLD_MS to put the watch to sleep: the OLED is
// powered down, the sensors stop being polled, and the CPU enters light sleep.
// A quick press of button B wakes it back up.

// Button B is the perf/confirm button. In the default layout that is GPIO 5.
#define PIN_SLEEP_BTN   PIN_BTN_B

// ── Battery monitor (M7) ─────────────────────────────────────────────────────
// Wiring:  BAT+ ──[100k]──┬──> PIN_BATT_ADC        (+ 100nF from that pin to GND)
//                         └──[100k]── GND
// The divider halves the cell so a full 4.2 V lands at 2.1 V, inside ADC1's
// 11 dB range. 2x100k draws only 21 uA, so it can stay connected permanently.
// MUST be an ADC1 pin (GPIO 0-4) — ADC2 stops working the moment WiFi is on.
// DEFAULTS TO -1 (disabled) because the divider is not built yet — an unwired
// ADC pin floats and would report a random percentage. Change this to 3 the
// moment the two 100k resistors are soldered on. See POWER.md.
#define PIN_BATT_ADC        (-1)
#define BATT_DIVIDER_RATIO  2.0f    // (R_top + R_bot) / R_bot  -> 100k/100k = 2.0
#define BATT_CAL_SCALE      1.00f   // trim: measured_by_multimeter / reported_by_watch

// Optional TRUE USB detection: a second divider off the board's 5V pin.
//   5V ──[100k]──┬──> PIN_USB_SENSE      └──[100k]── GND     (5.0 V -> 2.5 V)
// Without it the firmware guesses from cell voltage alone, which cannot tell a
// just-charged cell from an actively powered one. -1 = not wired.
#define PIN_USB_SENSE       (-1)    // GPIO 0 or 1 are free ADC1 pins if you add it
#define USB_DIVIDER_RATIO   2.0f
#define USB_PRESENT_MV      4000    // 5V rail this high = USB really is plugged in

#define BATT_SAMPLES        16      // ADC reads averaged per measurement (C3 ADC is noisy)
#define BATT_INTERVAL_MS    10000   // re-measure this often
#define BATT_FULL_MV        4200    // a Li-ion cell at 100%
#define BATT_LOW_MV         3500    // show "LOW" below this
#define BATT_CRIT_MV        3400    // below this, shed load (radio off) to protect the cell
#define BATT_USB_MV         4250    // fallback USB guess: a resting cell can't exceed 4.2 V

// ── Power optimisation (M8) ──────────────────────────────────────────────────
// Measured baseline before these: ~48 mA unpaired, and roughly DOUBLE that once
// WiFi associates, because the radio was pinned awake. On a 500 mAh cell that
// was ~10 h idle and ~4 h paired. See lab-notes/ and POWER.md.

// CPU clock. 80 MHz is the lowest the WiFi radio will run at, and this workload
// (a 5 Hz UI and two slow I2C sensors) is nowhere near compute-bound. The C3's
// APB stays at 80 MHz either way, so I2C/UART timing is unaffected.
// Set to 160 to go back to the default.
#define CPU_FREQ_MHZ            80

// Blank the OLED after this long with no button activity. The panel is the
// single biggest continuous load after the radio (~12 mA), and a watch spends
// almost all of its time unobserved. 0 disables the timeout.
// Any button press wakes it; the vitals, sync and BLE all keep running.
#define DISPLAY_TIMEOUT_MS      30000

// Wake the display on motion, using the accelerometer already being read for
// step counting. DEFAULTS OFF, and that is deliberate: the step detector fires
// at 2.0 m/s2 of deviation, so any threshold low enough to catch a wrist-raise
// also fires on ordinary walking — which would hold the screen on for the whole
// walk and undo the timeout entirely. Telling a raise apart from a stride needs
// gesture detection (orientation change followed by stillness), not a
// threshold, and that is not something to enable untested. Set to 1 to
// experiment; the hook is wired up in pollAccelStep().
#define WAKE_ON_MOTION          0
#define WAKE_MOTION_MS2         6.0f    // deviation from rest that counts as a raise

// UI repaint interval. A full 128x64 frame at 100 kHz takes ~90 ms of blocking
// I2C, so the old 200 ms (5 fps) left the loop unable to sample for ~45% of
// wall-clock time — which is what dropped button presses and made the heart-rate
// detector miss beats. Nothing on screen changes faster than the 1 Hz seconds
// counter, so 1 fps loses no information and cuts the bus duty cycle to ~9%.
// Screen changes repaint IMMEDIATELY regardless, so the UI still feels instant.
#define DRAW_INTERVAL_MS        1000

// MAX30105 LED currents. The firmware only ever reads the IR channel (getIR()),
// so the RED LED is pure waste — it was pulsing at full current for a value
// nothing reads. 0 turns it off.
#define MAX_LED_RED             0x00
#define MAX_LED_IR              0x1F    // full current while a finger is present

// Drop the IR LED to a proximity-detect level after this long with no finger.
// Full current only matters once you are actually measuring a pulse. Set
// MAX_IDLE_DIM to 0 if finger detection ever becomes unreliable on hardware.
#define MAX_IDLE_DIM            1
#define MAX_LED_IR_IDLE         0x0F    // ~half current while idle
#define MAX_IDLE_AFTER_MS       10000
// Finger-present threshold on the raw IR reading. The idle threshold is scaled
// down with the LED current, since the reflected signal scales with it.
#define MAX_FINGER_THRESHOLD    50000
#define MAX_FINGER_THRESH_IDLE  24000

// ── UI timing ────────────────────────────────────────────────────────────────
#define BOOT_BAR_MS     2000            // how long the loading bar takes to fill

// ── Diagnostics ──────────────────────────────────────────────────────────────
#define DEBUG_SERIAL    1   // 1 = verbose [watch]/[net] logs over USB; 0 = quiet (errors only)

// ── Auto-orientation (MPU6050 accel + gyro) ─────────────────────────────────
// The UI is re-rotated so it always reads upright, whichever way the watch is
// turned. Gravity direction from the accelerometer gives the absolute angle;
// the gyro gates it (no re-orienting mid-gesture). Hysteresis is time-based.
//
// OLED-only: a character LCD cannot rotate its frame, so an LCD build forces
// this off no matter what is written below.
#if DISPLAY_TYPE == DISPLAY_LCD1602
  #undef  ORIENT_ENABLE
  #define ORIENT_ENABLE 0
#endif
#define ORIENT_ENABLE         1   // 0 to force the natural mounting and skip all of this
#define ORIENT_HOLD_MS        500  // candidate must persist this long before rotating
#define ORIENT_MOTION_RAD_S   2.5f // gyro speed above this = "moving, don't flip"
#define ORIENT_FLAT_MS2       5.0f // |g| projected on x-y below this = watch lying flat
// A flaky/disconnecting MPU can return garbage (zeros, huge or NaN values). Only
// trust the reading for a rotation when its magnitude looks like real gravity.
#define ORIENT_MIN_G_MS2      4.0f // readings below this are noise, not gravity
#define ORIENT_MAX_G_MS2      20.0f // readings above this are garbage too

// ── Bluetooth LE control (M4 controller link) ────────────────────────────────
// The watch advertises a GATT service that ANY BLE device (a phone app, or the
// web app via Web Bluetooth) can connect to. BLE is the CONTROLLER, not the data
// path: it pairs the watch (sets WiFi SSID/pass + the device token), syncs its
// clock ("dates and etc"), and issues commands. Health data still travels to the
// backend DIRECTLY over WiFi (/ingest) — BLE never carries readings.
#define BLE_ENABLE        1
#define BLE_DEVICE_NAME   "FitnessAI Watch"
#define BLE_SERVICE_UUID  "0000F1A0-0000-1000-8000-00805F9B34FB"
#define BLE_UUID_SSID     "0000F1A1-0000-1000-8000-00805F9B34FB"
#define BLE_UUID_PASS     "0000F1A2-0000-1000-8000-00805F9B34FB"
#define BLE_UUID_TOKEN    "0000F1A3-0000-1000-8000-00805F9B34FB"
#define BLE_UUID_TIME     "0000F1A4-0000-1000-8000-00805F9B34FB"
#define BLE_UUID_CMD      "0000F1A5-0000-1000-8000-00805F9B34FB"
#define BLE_UUID_NAME     "0000F1A6-0000-1000-8000-00805F9B34FB"
#define BLE_UUID_STATE    "0000F1A7-0000-1000-8000-00805F9B34FB"

// ── Cloud sync (Milestone 4/5) ───────────────────────────────────────────────
// Device tokens are issued by the web app (Devices page -> /device/pair) and
// entered into the watch's setup portal. Readings POST to BACKEND_URL/ingest/
// with `Authorization: Bearer fit_...`. The backend is FastAPI (Vercel).
#define BACKEND_URL         "https://backend-seven-topaz-23.vercel.app"
#define INGEST_PATH         "/ingest/"  // POST WatchSyncPayload JSON (Bearer token)
#define APP_VERSION         "0.4.0"      // reported in the payload's app_version

// Sync cadence. Readings are captured locally while offline and flushed in
// batches when the watch is connected, so a lost signal never loses data.
#define READING_INTERVAL_MS 60000        // capture one reading every minute
#define SYNC_INTERVAL_MS    300000       // attempt a batch upload every 5 min
#define SYNC_RETRY_MS       30000        // if an upload fails, retry sooner than the interval
#define SYNC_MAX_BATCH      24           // readings per HTTP POST (keeps payloads small)
#define SYNC_QUEUE_MAX      240          // in-RAM offline queue depth (~= 4 h of readings)

// ── Setup captive portal (M4) ────────────────────────────────────────────────
// When unpaired the watch broadcasts an open SoftAP named `FitnessAI Watch` (the
// same name as the BLE advertisement — see BLE_ADV_NAME) and serves a
// phone-friendly page on 192.168.4.1 for WiFi credentials + device token.
#define AP_SSID_NAME       "FitnessAI Watch"
#define AP_PASSWORD        nullptr        // open AP (throwaway provisioning network)
#define PORTAL_IP          192, 168, 4, 1 // SoftAP DHCP gateway (host of the portal)

// NVS namespace + keys — where WiFi creds, the device token and the `paired`
// flag survive reboots.
#define NVS_NAMESPACE       "fitnessai"
#define NVS_KEY_PAIRED      "paired"
#define NVS_KEY_SSID        "ssid"
#define NVS_KEY_PASS        "pass"
#define NVS_KEY_TOKEN       "token"
#define NVS_KEY_NAME        "devname"

// ── Simulator build (Wokwi) ──────────────────────────────────────────────────
// Activated ONLY by -DSIM_BUILD=1 on the compiler command line, which is what
// watch/sim/simctl.py passes. A normal `arduino-cli compile` or an Arduino IDE
// build never defines it, so the hardware firmware is completely unaffected by
// everything below. See watch/sim/README.md.
//
// Three things differ in the simulator, and each is a limit of the emulator,
// not a choice:
//   1. BLE off      — Wokwi does not emulate the Bluetooth controller at all.
//                     bleStart() would block forever on a stack that never
//                     comes up. WiFi *is* emulated, so only BLE is dropped.
//   2. Battery ADC  — pointed at GPIO3, where diagram.json puts a slide pot.
//                     This is the one place the sim is AHEAD of the hardware:
//                     it exercises the M7 power code before the divider exists.
//   3. HR synthetic — no MAX30105 part exists in Wokwi. See sim.h.
#ifndef SIM_BUILD
#define SIM_BUILD 0
#endif

#if SIM_BUILD
  #undef  BLE_ENABLE
  #define BLE_ENABLE          0

  #undef  PIN_BATT_ADC
  #define PIN_BATT_ADC        3       // slide potentiometer stands in for the divider

  // The pot spans the full 0-3.3 V rail, and BATT_DIVIDER_RATIO doubles it, so
  // the firmware sees a 0-6.6 V "cell". Handy for testing: sliding down walks
  // the UI through 100% -> LOW -> critical-radio-shed without a bench supply.

  // Optional: let the simulated watch join Wokwi's virtual network. Off by
  // default so a sim run is deterministic and offline. Turn on to exercise
  // wifiTick()/syncTick() against the real backend from inside the simulator.
  #ifndef SIM_WIFI
  #define SIM_WIFI            0
  #endif
  #define SIM_WIFI_SSID       "Wokwi-GUEST"   // Wokwi's open virtual AP
  #define SIM_WIFI_PASS       ""
#endif

// ── Shared types ─────────────────────────────────────────────────────────────
// These live here, not in the .ino, because the Arduino builder auto-generates
// forward prototypes for every function and inserts them straight after the
// #includes — i.e. BEFORE any type declared in the sketch body. Any function
// whose signature mentions one of these types would then fail to compile
// ("was not declared in this scope"). Declaring them in a header dodges that
// entirely and keeps the sketch safe to reorder.

enum Screen { SCREEN_BOOT, SCREEN_HOME, SCREEN_HR, SCREEN_STEPS, SCREEN_STATUS,
              SCREEN_SYNC, SCREEN_PAIR };

// One debounced tactile button. Taps fire on RELEASE so that a hold does not
// also register as a tap; see updateButton() in the sketch.
struct ButtonState {
  bool lastRaw;         // last raw read (for change detection)
  bool stable;          // debounced level (true = pressed)
  uint32_t lastChange;  // when the raw level last changed
  uint32_t holdSince;   // when the press started (for hold detection)
  bool tapEdge;         // set on RELEASE of a short press — cleared by consumer
  bool holdEdge;        // set when a press crosses BTN_HOLD_MS — cleared by consumer
  bool held;            // this press already counted as a hold (suppresses the tap)
};
