/*
 * FitnessAI Watch — firmware
 * Board:   ESP32-C3 SuperMini
 * Display: SSD1306 128x64 OLED (I2C)  — rendered with U8g2
 *
 * Milestone 1: pin/port setup + boot loading screen.
 * All wiring lives in config.h — edit that, not this file.
 *
 * Libraries (Arduino Library Manager): "U8g2" by oliver.
 */

#include <Wire.h>
#include <U8g2lib.h>
#include "config.h"

// Full-buffer, hardware-I2C SSD1306. We pass our own Wire pins in setup().
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /*reset=*/U8X8_PIN_NONE);

// ── Loading screen ──────────────────────────────────────────────────────────
// Title + a progress bar filled to `pct` (0-100), drawn with U8g2 fonts.
static void drawLoading(uint8_t pct) {
  u8g2.clearBuffer();

  // Title (big, bold)
  u8g2.setFont(u8g2_font_helvB14_tr);
  const char *title = "FitnessAI";
  int tw = u8g2.getStrWidth(title);
  u8g2.drawStr((OLED_WIDTH - tw) / 2, 24, title);

  // Subtitle
  u8g2.setFont(u8g2_font_6x10_tr);
  const char *sub = "Loading...";
  int sw = u8g2.getStrWidth(sub);
  u8g2.drawStr((OLED_WIDTH - sw) / 2, 38, sub);

  // Progress bar
  const int x = 14, y = 48, w = 100, h = 10;
  u8g2.drawRFrame(x, y, w, h, 3);
  int fill = (int)((long)(w - 4) * pct / 100);
  if (fill > 0) u8g2.drawRBox(x + 2, y + 2, fill, h - 4, 2);

  // Percentage
  u8g2.setFont(u8g2_font_5x7_tr);
  char buf[6];
  snprintf(buf, sizeof(buf), "%u%%", pct);
  int pw = u8g2.getStrWidth(buf);
  u8g2.drawStr((OLED_WIDTH - pw) / 2, 63, buf);

  u8g2.sendBuffer();
}

void setup() {
  Serial.begin(115200);

  // Buttons — set up now, used by later milestones.
  pinMode(PIN_BUTTON_A, INPUT_PULLUP);
  pinMode(PIN_BUTTON_B, INPUT_PULLUP);

  // I2C on the configured pins, then bring up the display.
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(I2C_CLOCK_HZ);
  u8g2.setI2CAddress(OLED_ADDR << 1);   // U8g2 wants the 8-bit address
  u8g2.begin();

  // Animate the loading bar over ~2 s.
  for (uint8_t pct = 0; pct <= 100; pct += 4) {
    drawLoading(pct);
    delay(70);
  }
  drawLoading(100);

  Serial.println("Boot complete.");
}

void loop() {
  // Milestone 1 ends at the full loading screen. Real screens come next.
  delay(1000);
}
