#pragma once
/*
 * SSD1306 128x64 OLED — panel settings.
 *
 * Selected by  #define DISPLAY_MODULE  DISPLAY_SSD1306_OLED  in config.h.
 * Everything specific to THIS panel lives here; config.h holds the wiring and
 * behaviour shared by every display.
 *
 * WIRING (shares the one I2C bus with the sensors — no extra GPIOs)
 *   OLED VCC -> ESP32 3V3      (this module is a true 3.3 V part)
 *   OLED GND -> ESP32 GND
 *   OLED SDA -> GPIO 7   (PIN_I2C_SDA)
 *   OLED SCL -> GPIO 8   (PIN_I2C_SCL)
 *
 * Confirmed panel: 0.96" I2C OLED SSD1306 128x64 (Shopee #7216498277).
 * Verified working on hardware — this is the panel the firmware was developed
 * against, and the only one whose graphical screens are fully exercised.
 */

#define DISPLAY_TYPE    DISPLAY_OLED

// Address is 0x3C on the overwhelming majority of these modules; a few are 0x3D.
#define OLED_ADDR       0x3C
#define OLED_WIDTH      128
#define OLED_HEIGHT     64

// Two SSD1306 init sequences exist for the 0.96" panel. ALT0 looked plausible
// on the sparse measurement pattern, but with real text it interleaves/squashes
// the rows (lines overlap vertically on the pair screen) -> this panel wants
// NONAME. The earlier "blank" NONAME run was the 400 kHz I2C bug, not this.
#define OLED_INIT_ALT0  0   // 0 = NONAME (measured correct), 1 = ALT0

// How often to force a full re-init + repaint. A WiFi burst landing mid-frame
// (or a marginal bus) can desync the panel's internal address counter, and the
// SSD1306 has no read-back, so re-sending the init sequence is the only way to
// recover. OLED-only: an HD44780 has no equivalent failure mode.
#define DISPLAY_SELF_HEAL_MS    5000
