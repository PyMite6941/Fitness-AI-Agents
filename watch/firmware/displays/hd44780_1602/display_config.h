#pragma once
/*
 * HD44780 1602 (16x2 character LCD) on a PCF8574 I2C backpack.
 *
 * Selected by  #define DISPLAY_MODULE  DISPLAY_HD44780_1602  in config.h.
 * The 16x2 variant. Same driver and same wiring as the 2004A; only the
 * geometry below differs. Screens fall back to their two-row layouts.
 *
 * WIRING — the backpack's 4-pin header, nothing else. The LCD's own 16-pin
 * header is already soldered to the backpack; you never wire those 16 yourself.
 *   LCD backpack VCC -> ESP32 5V     <-- 5V, NOT 3V3. See WHY 5V below.
 *   LCD backpack GND -> ESP32 GND
 *   LCD backpack SDA -> GPIO 7  (PIN_I2C_SDA)
 *   LCD backpack SCL -> GPIO 8  (PIN_I2C_SCL)
 *
 * WHY 5V: an HD44780 module built for 5 V shows NOTHING at 3.3 V -- the
 * backlight LED barely glows behind its 5 V-sized series resistor and the
 * contrast bias never drives the segments. It also stops acking on I2C, so the
 * firmware cannot even see it. "Not even lit" on 3V3 is this, not a code bug.
 *
 * THE CATCH: the backpack pulls SDA/SCL up to its own VCC, so at 5 V those two
 * lines idle at 5 V into 3.3 V GPIOs. Mitigations are in watch/README.md
 * ("Wiring the LCD1602"); the cleanest is desoldering the two 472 resistors.
 */

#define DISPLAY_TYPE     DISPLAY_LCD1602   // the HD44780 driver, whatever the size

// Panel geometry. Every LCD screen in the firmware has a two-row layout and a
// four-row layout and picks between them on LCD_ROWS, so this pair of numbers
// is the ONLY difference between a 1602 and a 2004A build.
#define LCD_COLS         16
#define LCD_ROWS         2

// Only the FIRST address tried. PCF8574T backpacks land in 0x20-0x27 (usually
// 0x27), PCF8574AT ones in 0x38-0x3F (usually 0x3F), shifted further by the
// A0/A1/A2 solder jumpers. dispBegin() probes both blocks and uses whatever
// answers, so a mismatch is not fatal -- the boot log prints what it found.
// Type `i2c` on the serial console to scan on demand.
#define LCD_I2C_ADDR     0x27

// A character LCD cannot rotate its frame, so the accelerometer-driven
// auto-rotation is forced off for any HD44780 build.
#define DISPLAY_SELF_HEAL_MS    5000
