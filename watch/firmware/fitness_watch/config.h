#pragma once
/*
 * config.h — hardware configuration for the FitnessAI Watch.
 *
 * Scope right now: the SCREEN only. Edit these to match your wiring, then flash.
 * (Sensors, GPS, battery, buttons, Wi-Fi come in later milestones.)
 *
 * Board: ESP32-C3 SuperMini. I2C idles HIGH, so GPIO 8/9 (strapping pins) are
 * safe for the bus. Set a pin to -1 to disable.
 */

// ── Board ────────────────────────────────────────────────────────────────────
#define BOARD_NAME      "ESP32-C3 SuperMini"
#define DEVICE_NAME     "fitness_watch"

// ── I2C bus (the OLED, plus future sensors, share this) ──────────────────────
#define PIN_I2C_SDA     8
#define PIN_I2C_SCL     9
#define I2C_CLOCK_HZ    400000          // 400 kHz fast-mode

// ── OLED display (SSD1306 128x64) ────────────────────────────────────────────
#define OLED_ADDR       0x3C
#define OLED_WIDTH      128
#define OLED_HEIGHT     64

// ── UI timing ────────────────────────────────────────────────────────────────
#define BOOT_BAR_MS     2000            // how long the loading bar takes to fill
