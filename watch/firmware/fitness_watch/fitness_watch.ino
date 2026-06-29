/*
 * FitnessAI Watch — main program
 * Board:   ESP32-C3 SuperMini
 * Display: SSD1306 128x64 OLED (I2C), rendered with U8g2
 *
 * Scope: the screen + program skeleton.
 *   boot  →  loading screen (animated bar)  →  home screen (live)
 * All wiring is in config.h. Library: "U8g2" by oliver.
 */

#include <Wire.h>
#include <U8g2lib.h>
#include "config.h"

// Full-buffer, hardware-I2C SSD1306.
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /*reset=*/U8X8_PIN_NONE);

// ── Screen state machine ─────────────────────────────────────────────────────
enum Screen { SCREEN_BOOT, SCREEN_HOME };
static Screen screen = SCREEN_BOOT;
static uint32_t bootStartMs = 0;

// ── Loading screen ───────────────────────────────────────────────────────────
static void drawLoading(uint8_t pct) {
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_helvB14_tr);
  const char *title = "FitnessAI";
  u8g2.drawStr((OLED_WIDTH - u8g2.getStrWidth(title)) / 2, 24, title);

  u8g2.setFont(u8g2_font_6x10_tr);
  const char *sub = "Loading...";
  u8g2.drawStr((OLED_WIDTH - u8g2.getStrWidth(sub)) / 2, 38, sub);

  const int x = 14, y = 48, w = 100, h = 10;
  u8g2.drawRFrame(x, y, w, h, 3);
  int fill = (int)((long)(w - 4) * pct / 100);
  if (fill > 0) u8g2.drawRBox(x + 2, y + 2, fill, h - 4, 2);

  u8g2.setFont(u8g2_font_5x7_tr);
  char buf[6];
  snprintf(buf, sizeof(buf), "%u%%", pct);
  u8g2.drawStr((OLED_WIDTH - u8g2.getStrWidth(buf)) / 2, 63, buf);

  u8g2.sendBuffer();
}

// ── Home screen ──────────────────────────────────────────────────────────────
// Placeholder clock (counts seconds since boot) so we can see the loop is alive.
// Real time/data arrives in later milestones.
static void drawHome() {
  uint32_t s = millis() / 1000;
  int hh = (s / 3600) % 24, mm = (s / 60) % 60, ss = s % 60;
  bool colon = (s % 2) == 0;   // blink the colon every second

  u8g2.clearBuffer();

  // Top status bar
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(2, 7, "FitnessAI");
  const char *batt = "USB";
  u8g2.drawStr(OLED_WIDTH - u8g2.getStrWidth(batt) - 2, 7, batt);
  u8g2.drawHLine(0, 10, OLED_WIDTH);

  // Big clock
  char t[9];
  snprintf(t, sizeof(t), "%02d%c%02d", hh, colon ? ':' : ' ', mm);
  u8g2.setFont(u8g2_font_logisoso28_tn);   // large numeric font
  u8g2.drawStr((OLED_WIDTH - u8g2.getStrWidth(t)) / 2, 44, t);

  // Seconds + footer
  u8g2.setFont(u8g2_font_5x7_tr);
  char sec[4];
  snprintf(sec, sizeof(sec), ":%02d", ss);
  u8g2.drawStr(OLED_WIDTH - u8g2.getStrWidth(sec) - 2, 44, sec);
  u8g2.drawHLine(0, 53, OLED_WIDTH);
  u8g2.drawStr(2, 63, "Ready");

  u8g2.sendBuffer();
}

void setup() {
  Serial.begin(115200);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(I2C_CLOCK_HZ);
  u8g2.setI2CAddress(OLED_ADDR << 1);   // U8g2 uses the 8-bit address form
  u8g2.begin();

  // Animate the loading bar over BOOT_BAR_MS, then hand off to the home screen.
  const int steps = 26;
  for (int i = 0; i <= steps; i++) {
    drawLoading((uint8_t)(100L * i / steps));
    delay(BOOT_BAR_MS / steps);
  }

  screen = SCREEN_HOME;
  bootStartMs = millis();
  Serial.println("Boot complete -> HOME");
}

void loop() {
  switch (screen) {
    case SCREEN_HOME:
      drawHome();
      break;
    default:
      break;
  }
  delay(100);   // ~10 fps refresh; plenty for a clock
}
