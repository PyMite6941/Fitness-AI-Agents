/*
 * FitnessAI Watch — DISPLAY MEASUREMENT sketch (not the product firmware).
 * Board: ESP32-C3 SuperMini.  Flash with CDCOnBoot=cdc so Serial is on USB.
 *
 * WHY THIS EXISTS
 * ---------------
 * fitness_watch.ino assumes a 128x64 SSD1306. If the real panel is a different
 * size or controller, everything draws too big and runs off the glass. This
 * sketch does not assume — it cycles through every plausible driver and draws
 * a pattern you can COUNT, so the panel tells us its own size.
 *
 * HOW TO USE IT
 * -------------
 *  1. Flash this sketch. It auto-advances through the driver modes every 6 s.
 *  2. Press either button (or send any character over serial) to FREEZE on the
 *     mode currently showing. Press again to resume auto-advance.
 *  3. Find the mode where the frame sits neatly just inside the glass edges,
 *     the text is readable, and the corner block is fully visible.
 *  4. On that mode, use pattern 1 (RULER) and count the dots:
 *
 *        dots across the top  x 8  =  panel WIDTH in pixels
 *        dots down the left   x 8  =  panel HEIGHT in pixels
 *
 *     (Count the corner dot — the small square — once in each direction.)
 *  5. Report: the mode number, the two dot counts, and which patterns looked
 *     right. That is everything needed to fix config.h.
 *
 * CONTROLS
 *   Button B (GPIO5) tap  -> next driver mode
 *   Button A (GPIO4) tap  -> next test pattern
 *   any button / any key  -> freeze or resume auto-advance
 *   serial 'n'            -> next mode      (works with no buttons soldered)
 *   serial 'p'            -> next pattern
 *   serial 'r'            -> re-print the worksheet
 */

#include <Wire.h>
#include <U8g2lib.h>

// Same bus as config.h. This sketch hard-codes them so it stands alone.
static const int PIN_SDA   = 7;
static const int PIN_SCL   = 8;
static const int PIN_BTN_A = 4;
static const int PIN_BTN_B = 5;
static const uint8_t OLED_ADDR = 0x3C;   // 0x3D on some modules — the scan reports the truth

// ── Candidate drivers ────────────────────────────────────────────────────────
// All full-buffer hardware-I2C. U8g2 keeps one static buffer per buffer size,
// so declaring all of these costs far less RAM than it looks.
U8G2_SSD1306_128X64_NONAME_F_HW_I2C    d0(U8G2_R0, U8X8_PIN_NONE);
U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C d1(U8G2_R0, U8X8_PIN_NONE);
U8G2_SH1106_128X64_NONAME_F_HW_I2C     d2(U8G2_R0, U8X8_PIN_NONE);
U8G2_SSD1306_128X64_ALT0_F_HW_I2C      d3(U8G2_R0, U8X8_PIN_NONE);
U8G2_SH1107_64X128_F_HW_I2C            d4(U8G2_R0, U8X8_PIN_NONE);
U8G2_SSD1306_64X48_ER_F_HW_I2C         d5(U8G2_R0, U8X8_PIN_NONE);
U8G2_SSD1306_72X40_ER_F_HW_I2C         d6(U8G2_R0, U8X8_PIN_NONE);

struct Candidate {
  U8G2       *dev;
  const char *label;   // kept short — must fit a 64px-wide panel at 5x7
  const char *note;    // serial only, room to be descriptive
};

static Candidate CAND[] = {
  {&d0, "SSD1306 128x64", "0.96in SSD1306 128x64 NONAME  (what the firmware assumes now)"},
  {&d1, "SSD1306 128x32", "0.91in SSD1306 128x32 UNIVISION"},
  {&d2, "SH1106  128x64", "1.3in SH1106 128x64 (132px RAM, 2px offset -- very common clone)"},
  {&d3, "SSD1306 ALT0",   "0.96in SSD1306 128x64 ALT0 init sequence"},
  {&d4, "SH1107  64x128", "0.96in SH1107 64x128 PORTRAIT (tall watch-shaped panel)"},
  {&d5, "SSD1306 64x48",  "0.66in SSD1306 64x48 ER"},
  {&d6, "SSD1306 72x40",  "0.42in SSD1306 72x40 ER"},
};
static const int N_CAND = (int)(sizeof(CAND) / sizeof(CAND[0]));

enum Pattern { PAT_RULER, PAT_ROWS, PAT_FILL, PAT_CORNERS, PAT_COUNT };
static const char *PAT_NAME[] = {"RULER", "ROWS", "FILL", "CORNER"};

static int      modeIdx    = 0;
static Pattern  pattern    = PAT_RULER;
static bool     autoCycle  = true;
static uint32_t lastSwitch = 0;
static const uint32_t AUTO_MS = 6000;

// ── I2C scan (which addresses actually ACK) ──────────────────────────────────
static void i2cScan() {
  Serial.println(F("\n[I2C] scanning SDA=7 SCL=8 ..."));
  int found = 0;
  for (uint8_t a = 0x08; a <= 0x77; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("      0x%02X ACK", a);
      if (a == 0x3C || a == 0x3D) Serial.print("   <- OLED");
      if (a == 0x57)              Serial.print("   <- MAX30102");
      if (a == 0x68 || a == 0x69) Serial.print("   <- MPU6050");
      Serial.println();
      found++;
    }
  }
  if (!found) Serial.println(F("      nothing responded -- check SDA/SCL/3V3/GND before trusting anything below"));
}

// ── Test patterns ────────────────────────────────────────────────────────────
// Every pattern draws to the ACTIVE driver's declared W/H, so a wrong driver
// visibly overflows or under-fills the glass.

// The counting pattern. One dot per 8 pixels, so dots x 8 = size.
static void drawRuler(U8G2 &g, int W, int H) {
  g.drawFrame(0, 0, W, H);                 // hugs the outermost pixel row/column

  g.drawBox(2, 2, 3, 3);                   // origin marker: counts as dot #1 both ways

  for (int x = 3 + 8; x < W; x += 8) {     // top ruler
    bool group = (((x - 3) / 8) % 4) == 0; // every 4th dot = 32px group tick
    if (group) g.drawVLine(x, 1, 5);
    else       g.drawPixel(x, 3);
  }
  for (int y = 3 + 8; y < H; y += 8) {     // left ruler
    bool group = (((y - 3) / 8) % 4) == 0;
    if (group) g.drawHLine(1, y, 5);
    else       g.drawPixel(3, y);
  }

  g.drawBox(W - 6, H - 6, 5, 5);           // far corner: visible = height/width are right

  g.setFont(u8g2_font_5x7_tr);
  char l1[40], l2[40];
  snprintf(l1, sizeof(l1), "M%d %s", modeIdx, CAND[modeIdx].label);
  snprintf(l2, sizeof(l2), "%dx%d  %dx%d dots", W, H, (W - 3 + 7) / 8, (H - 3 + 7) / 8);
  g.drawStr(8, H / 2 - 1, l1);
  g.drawStr(8, H / 2 + 9, l2);
}

// Page test: the SSD1306 addresses memory in 8-pixel pages. A numbered band per
// page shows immediately if the panel has fewer pages than the driver assumes.
static void drawRows(U8G2 &g, int W, int H) {
  g.setFont(u8g2_font_4x6_tr);
  for (int p = 0; p * 8 < H; p++) {
    int y = p * 8;
    if (p & 1) g.drawBox(0, y, W, 8);      // alternate solid/empty bands
    g.setDrawColor(p & 1 ? 0 : 1);
    char n[8];
    snprintf(n, sizeof(n), "%d", p);
    g.drawStr(2, y + 6, n);
    g.drawStr(W - 10, y + 6, n);
    g.setDrawColor(1);
  }
}

// Every pixel lit — shows the true illuminated area of the glass at a glance.
static void drawFill(U8G2 &g, int W, int H) {
  g.drawBox(0, 0, W, H);
  g.setDrawColor(0);
  g.setFont(u8g2_font_5x7_tr);
  char s[16];
  snprintf(s, sizeof(s), "M%d %dx%d", modeIdx, W, H);
  g.drawStr(6, H / 2 + 3, s);
  g.setDrawColor(1);
}

// Corner brackets + centre crosshair — quickest "is anything clipped" check.
static void drawCorners(U8G2 &g, int W, int H) {
  const int L = 10;
  g.drawHLine(0, 0, L);         g.drawVLine(0, 0, L);
  g.drawHLine(W - L, 0, L);     g.drawVLine(W - 1, 0, L);
  g.drawHLine(0, H - 1, L);     g.drawVLine(0, H - L, L);
  g.drawHLine(W - L, H - 1, L); g.drawVLine(W - 1, H - L, L);

  g.drawHLine(W / 2 - 6, H / 2, 13);
  g.drawVLine(W / 2, H / 2 - 6, 13);

  g.setFont(u8g2_font_4x6_tr);
  char s[16];
  snprintf(s, sizeof(s), "M%d %dx%d", modeIdx, W, H);
  g.drawStr(W / 2 - 20 < 0 ? 2 : W / 2 - 20, H - 4, s);
}

static void render() {
  U8G2 &g = *CAND[modeIdx].dev;
  int W = g.getDisplayWidth();
  int H = g.getDisplayHeight();

  g.clearBuffer();
  switch (pattern) {
    case PAT_RULER:   drawRuler(g, W, H);   break;
    case PAT_ROWS:    drawRows(g, W, H);    break;
    case PAT_FILL:    drawFill(g, W, H);    break;
    case PAT_CORNERS: drawCorners(g, W, H); break;
    default: break;
  }
  g.sendBuffer();
}

static void activate(int idx) {
  modeIdx = (idx + N_CAND) % N_CAND;
  U8G2 &g = *CAND[modeIdx].dev;
  g.setI2CAddress(OLED_ADDR << 1);   // U8g2 wants the 8-bit form
  g.begin();
  g.setContrast(255);

  Serial.printf("\n--- MODE %d/%d  [%s]  driver says %dx%d ---\n",
                modeIdx, N_CAND - 1, PAT_NAME[pattern],
                g.getDisplayWidth(), g.getDisplayHeight());
  Serial.printf("    %s\n", CAND[modeIdx].note);
  if (pattern == PAT_RULER) {
    Serial.printf("    expect %d dots across, %d dots down if this driver is correct\n",
                  (g.getDisplayWidth() - 3 + 7) / 8, (g.getDisplayHeight() - 3 + 7) / 8);
  }
  lastSwitch = millis();
}

static void worksheet() {
  Serial.println(F(
    "\n================ MEASUREMENT WORKSHEET ================\n"
    " Freeze on the mode that looks correct, use pattern RULER,\n"
    " then count the dots (the small square in the top-left\n"
    " corner counts as dot #1 in BOTH directions):\n"
    "\n"
    "   dots across the top  x 8  = WIDTH  px\n"
    "   dots down the left   x 8  = HEIGHT px\n"
    "\n"
    " Long ticks mark every 4th dot (= 32 px) to make counting\n"
    " groups easy: 4 ticks across = 128 px wide.\n"
    "\n"
    " Report back:\n"
    "   mode number ......... M_\n"
    "   dots across ......... __  -> width  = __ px\n"
    "   dots down ........... __  -> height = __ px\n"
    "   RULER frame touches all four glass edges?  y/n\n"
    "   bottom-right corner block fully visible?   y/n\n"
    "   ROWS: highest band number you can see?     __\n"
    "   FILL: does the whole glass light up?       y/n\n"
    "=======================================================\n"));
}

// ── Buttons (simple debounce; freeze on any press) ───────────────────────────
static bool lastA = false, lastB = false;
static uint32_t lastBtnMs = 0;

static void pollButtons() {
  uint32_t now = millis();
  if (now - lastBtnMs < 60) return;

  bool a = digitalRead(PIN_BTN_A) == LOW;
  bool b = digitalRead(PIN_BTN_B) == LOW;

  if (b && !lastB) {
    lastBtnMs = now;
    autoCycle = false;
    activate(modeIdx + 1);
  } else if (a && !lastA) {
    lastBtnMs = now;
    autoCycle = false;
    pattern = (Pattern)((pattern + 1) % PAT_COUNT);
    Serial.printf("    pattern -> %s\n", PAT_NAME[pattern]);
    lastSwitch = now;
  }
  lastA = a;
  lastB = b;
}

static void pollSerial() {
  while (Serial.available()) {
    int c = Serial.read();
    if (c == 'n' || c == 'N') { autoCycle = false; activate(modeIdx + 1); }
    else if (c == 'p' || c == 'P') {
      autoCycle = false;
      pattern = (Pattern)((pattern + 1) % PAT_COUNT);
      Serial.printf("    pattern -> %s\n", PAT_NAME[pattern]);
    } else if (c == 'r' || c == 'R') worksheet();
    else if (c == '\n' || c == '\r') continue;
    else {
      autoCycle = !autoCycle;
      Serial.printf("    auto-advance %s\n", autoCycle ? "ON" : "FROZEN");
      lastSwitch = millis();
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);                 // let USB CDC enumerate before the first print

  pinMode(PIN_BTN_A, INPUT_PULLUP);
  pinMode(PIN_BTN_B, INPUT_PULLUP);

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(100000);       // 100 kHz here — slower is more forgiving on breadboard wiring

  Serial.println(F("\n=== FitnessAI Watch : display measurement ==="));
  i2cScan();
  worksheet();
  Serial.println(F("Auto-advancing every 6s. Press a button or send any key to freeze.\n"));

  activate(0);
}

void loop() {
  pollButtons();
  pollSerial();

  if (autoCycle && millis() - lastSwitch >= AUTO_MS) activate(modeIdx + 1);

  render();
  delay(40);
}
