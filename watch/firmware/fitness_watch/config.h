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

// ── I2C bus (OLED + MAX30105 + MPU6050 all share this one bus) ───────────────
// Per the wiring diagram: SCL -> GPIO 8, SDA -> GPIO 7.
// (SDA moved off GPIO 9 to GPIO 7 so only one strapping pin (8) is used for I2C.)
#define PIN_I2C_SDA     7               // data
#define PIN_I2C_SCL     8               // clock
#define I2C_CLOCK_HZ    100000          // 100 kHz standard-mode. 400 kHz fast-mode
                                        // corrupts this board's marginal bus (blank
                                        // OLED, MAX30105 fails). Do not raise.

// ── OLED display (SSD1306 128x64) ────────────────────────────────────────────
// Confirmed panel: "0.96" I2C OLED SSD1306 128x64" (Shopee #7216498277).
// Address is 0x3C on the overwhelming majority of these modules.
#define OLED_ADDR       0x3C
#define OLED_WIDTH      128
#define OLED_HEIGHT     64

// Two SSD1306 init sequences exist for the 0.96" panel. ALT0 looked plausible
// on the sparse measurement pattern, but with real text it interleaves/squashes
// the rows (lines overlap vertically on the pair screen) -> this panel wants
// NONAME. The earlier "blank" NONAME run was the 400 kHz I2C bug, not this.
#define OLED_INIT_ALT0  0   // 0 = NONAME (measured correct), 1 = ALT0

// ── Heart rate / SpO2 (MAX30105, SparkFun MAX3010x library) ──────────────────
// INT pin is unwired (polling only) — leave at -1.
#define MAX30105_ADDR   0x57
#define PIN_MAX30105_INT (-1)

// ── Accelerometer / gyro (MPU6050, Adafruit_MPU6050 library) ─────────────────
// AD0 is tied low on the breakout (or floating with its onboard pulldown) ->
// default address 0x68. INT pin is unwired (polling only) — leave at -1.
#define MPU6050_ADDR    0x68
#define PIN_MPU6050_INT (-1)

// ── Buttons (tactile, active-low with internal pull-up) ─────────────────────
// Button A = Back / previous screen. Button B = Select / next screen.
// Both are non-strapping GPIOs, so holding them at reset is safe.
#define PIN_BTN_A       4
#define PIN_BTN_B       5

// ── UI timing ────────────────────────────────────────────────────────────────
#define BOOT_BAR_MS     2000            // how long the loading bar takes to fill

// ── Diagnostics ──────────────────────────────────────────────────────────────
#define DEBUG_SERIAL    1   // 1 = verbose [watch]/[net] logs over USB; 0 = quiet (errors only)

// ── Auto-orientation (MPU6050 accel + gyro) ─────────────────────────────────
// The UI is re-rotated so it always reads upright, whichever way the watch is
// turned. Gravity direction from the accelerometer gives the absolute angle;
// the gyro gates it (no re-orienting mid-gesture). Hysteresis is time-based.
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
