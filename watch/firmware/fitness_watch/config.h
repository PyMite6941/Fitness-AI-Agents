#pragma once
/*
 * config.h — central hardware/port configuration for the FitnessAI Watch.
 *
 * THIS IS THE ONE FILE YOU EDIT to match your physical wiring. Every pin and
 * port lives here so the rest of the firmware never hard-codes a GPIO number.
 * Values below are the defaults for an ESP32-C3 SuperMini; change them to match
 * your build, then re-flash. Set a pin to -1 to disable that peripheral.
 *
 * ── ESP32-C3 SuperMini usable GPIOs ──────────────────────────────────────────
 *   Safe for general use: 0,1,2,3,4,5,6,7,8,9,10,20(RX),21(TX)
 *   Notes: GPIO 2/8/9 affect boot strapping — fine once running, avoid holding
 *   them low at reset. ADC1 = GPIO 0-4. GPIO 8 has the onboard LED on some boards.
 */

// ── Board ────────────────────────────────────────────────────────────────────
#define BOARD_NAME        "ESP32-C3 SuperMini"
#define DEVICE_NAME       "fitness_watch"     // reported to the backend as `device`

// ── I2C bus (shared: OLED + IMU + heart-rate sensor) ─────────────────────────
#define PIN_I2C_SDA       8
#define PIN_I2C_SCL       9
#define I2C_CLOCK_HZ      400000               // 400 kHz fast-mode

// ── OLED display (SSD1306 128x64) ────────────────────────────────────────────
#define OLED_ADDR         0x3C
#define OLED_WIDTH        128
#define OLED_HEIGHT       64

// ── Buttons (active-low, use INPUT_PULLUP) ───────────────────────────────────
#define PIN_BUTTON_A      4      // "Back"   — tap: cycle screen | hold: pairing
#define PIN_BUTTON_B      5      // "Select" — start/stop workout
#define BUTTON_DEBOUNCE_MS    40
#define BUTTON_HOLD_MS      1200

// ── Sensors (I2C addresses on the shared bus; pins above) ────────────────────
#define IMU_ADDR          0x68   // MPU6050 (accel/gyro → steps)
#define HR_ADDR           0x57   // MAX30102 (heart rate / SpO2)

// ── GPS (optional, UART1) ────────────────────────────────────────────────────
#define PIN_GPS_RX        6      // ESP32 RX <- GPS TX   (set -1 to disable GPS)
#define PIN_GPS_TX        7      // ESP32 TX -> GPS RX
#define GPS_BAUD          9600

// ── Battery monitor (optional ADC) ───────────────────────────────────────────
#define PIN_BATT_ADC      3      // voltage-divider tap (set -1 if not wired)
#define BATT_DIVIDER      2.0f   // (R1+R2)/R2

// ── Haptics (optional) ───────────────────────────────────────────────────────
#define PIN_VIBE         -1      // vibration motor (-1 = none)

// ── Power / UX timing ────────────────────────────────────────────────────────
#define DISPLAY_TIMEOUT_MS    15000   // screen off after idle
#define SENSOR_INTERVAL_MS     1000   // workout stat roll-up period
#define SYNC_INTERVAL_MS      300000  // backend sync cadence (5 min)
