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

// ── Board ────────────────────────────────────────────────────────────────────
#define BOARD_NAME      "ESP32-C3 SuperMini"
#define DEVICE_NAME     "fitness_watch"

// ── I2C bus (OLED + MAX30105 + MPU6050 all share this one bus) ───────────────
// Per the wiring diagram: SCL -> GPIO 8, SDA -> GPIO 7.
// (SDA moved off GPIO 9 to GPIO 7 so only one strapping pin (8) is used for I2C.)
#define PIN_I2C_SDA     7               // data
#define PIN_I2C_SCL     8               // clock
#define I2C_CLOCK_HZ    400000          // 400 kHz fast-mode

// ── OLED display (SSD1306 128x64) ────────────────────────────────────────────
#define OLED_ADDR       0x3C
#define OLED_WIDTH      128
#define OLED_HEIGHT     64

// ── Heart rate / SpO2 (MAX30105, SparkFun MAX3010x library) ──────────────────
// INT pin is unwired (polling only) — leave at -1.
#define MAX30105_ADDR   0x57
#define PIN_MAX30105_INT (-1)

// ── Accelerometer / gyro (MPU6050, Adafruit_MPU6050 library) ─────────────────
// AD0 is tied low on the breakout (or floating with its onboard pulldown) ->
// default address 0x68. INT pin is unwired (polling only) — leave at -1.
#define MPU6050_ADDR    0x68
#define PIN_MPU6050_INT (-1)

// ── UI timing ────────────────────────────────────────────────────────────────
#define BOOT_BAR_MS     2000            // how long the loading bar takes to fill
