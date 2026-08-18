/*
 * FitnessAI Watch — Arduino Uno standalone display build.
 *
 * WHAT THIS IS, AND WHAT IT IS NOT
 * This is a FALLBACK, not the product. The real firmware is
 * watch/firmware/fitness_watch (ESP32-C3): WiFi, BLE, device pairing and
 * /ingest sync to the backend. None of that can exist here — an Uno has no
 * radio, 32 KB of flash against that firmware's 1.44 MB, and 2 KB of RAM.
 * What it CAN do is drive the LCD1602 and show live sensor data, which is
 * enough to demonstrate the watch's screen and step counting on hardware that
 * actually works.
 *
 * WHY AN UNO IS THE EASY CASE FOR THIS PANEL
 *   - The Uno is 5 V natively, so the LCD1602's PCF8574 backpack runs at 5 V
 *     with NO level shifting and NO pull-up surgery. That entire class of
 *     problem — which is what stalled the ESP32-C3 build — does not apply.
 *   - I2C is fixed at A4 (SDA) / A5 (SCL). It is not remappable, so there is
 *     no "wrong pins" failure mode either.
 *   - The USB link is a separate bridge chip, not the MCU's own USB, so the
 *     board does not drop off the port the way the C3 has been doing.
 *
 * WIRING (this is the whole build)
 *   LCD1602 backpack VCC -> Uno 5V
 *   LCD1602 backpack GND -> Uno GND
 *   LCD1602 backpack SDA -> Uno A4
 *   LCD1602 backpack SCL -> Uno A5
 *   MPU6050 (optional)   -> VCC 5V, GND GND, SDA A4, SCL A5   (same two wires)
 *   MAX30102 (optional)  -> VCC 5V, GND GND, SDA A4, SCL A5   (same two wires)
 *
 * Everything on I2C shares those two wires; the bus tells devices apart by
 * address, not by pin. Sensors are OPTIONAL — the sketch probes the bus at boot
 * and shows whatever is actually present, so a bare LCD still gives a working
 * screen.
 *
 * Build:  arduino-cli compile --fqbn arduino:avr:uno watch/firmware/uno_display
 * Flash:  arduino-cli upload -p COMx --fqbn arduino:avr:uno watch/firmware/uno_display
 * Serial: 115200
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <new>          // placement new — see lcdStorage below

// ── Configuration ────────────────────────────────────────────────────────────
#define LCD_COLS        16
#define LCD_ROWS        2
#define LCD_I2C_ADDR    0x27    // first address tried; the bus is probed anyway

#define MPU6050_ADDR    0x68
#define MAX3010X_ADDR   0x57

#define I2C_CLOCK_HZ    100000  // 100 kHz standard mode, same as the ESP32 build

#define SCREEN_DWELL_MS 4000    // auto-advance; no button needed to demo it
#define SAMPLE_MS       50      // accelerometer poll interval (20 Hz)

// Step detector. Deviation from 1 g that counts as a stride, matching the
// ESP32 firmware's threshold so both builds behave the same.
#define STEP_THRESHOLD_G  0.20f
#define STEP_MIN_GAP_MS   250   // refractory period; stops one stride double-counting

// ── Display ──────────────────────────────────────────────────────────────────
// Built by placement-new into a ZEROED static buffer rather than declared
// directly, because the address is discovered at runtime. The zeroing is not
// incidental — LiquidCrystal_I2C leaves _displayfunction uninitialised:
//
//   LiquidCrystal_I2C.cpp:62   _displayfunction = LCD_4BITMODE|LCD_1LINE|LCD_5x8DOTS
//                              ^ the ONLY assignment, and it sits in init_priv()
//   LiquidCrystal_I2C.cpp:68   _displayfunction |= LCD_2LINE     (OR, not assign)
//   LiquidCrystal_I2C.cpp:107  command(LCD_FUNCTIONSET | _displayfunction)
//
// The constructor sets _Addr/_cols/_rows/_backlightval and nothing else. Zeroed
// storage makes that byte 0x00, which IS LCD_4BITMODE|LCD_1LINE|LCD_5x8DOTS, so
// the panel receives a correct 0x28 (4-bit, 2-line, 5x8). Uninitialised memory
// with a stray LCD_8BITMODE (0x10) bit would tell the panel it is in 8-bit mode
// straight after being put into 4-bit mode, and it would never display anything.
static uint8_t lcdStorage[sizeof(LiquidCrystal_I2C)];
static LiquidCrystal_I2C *lcd = 0;
static uint8_t lcdAddr = 0;
static bool    lcdOk   = false;

// ── Detected hardware ────────────────────────────────────────────────────────
static bool mpuOk = false;
static bool maxOk = false;

// ── State ────────────────────────────────────────────────────────────────────
static uint32_t stepCount   = 0;
static uint32_t lastStepMs  = 0;
static bool     aboveThresh = false;
static float    lastMagG    = 1.0f;
static uint8_t  screen      = 0;
static uint32_t lastSwitch  = 0;
static uint32_t lastSample  = 0;

// ── I2C helpers ──────────────────────────────────────────────────────────────
static bool i2cAck(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// Log every address that answers. A count of 0 with things wired means power or
// a swapped SDA/SCL; a count in the dozens means SDA is stuck LOW and every
// address is a phantom rather than a real chip.
static uint8_t i2cScanLog() {
  uint8_t found = 0;
  Serial.println(F("[uno] I2C scan (SDA=A4 SCL=A5):"));
  for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
    if (!i2cAck(addr)) continue;
    found++;
    Serial.print(F("    0x"));
    if (addr < 16) Serial.print('0');
    Serial.print(addr, HEX);
    Serial.print(F("  "));
    if (addr == MPU6050_ADDR)            Serial.println(F("MPU6050"));
    else if (addr == MAX3010X_ADDR)      Serial.println(F("MAX30102/05"));
    else if ((addr >= 0x20 && addr <= 0x27) ||
             (addr >= 0x38 && addr <= 0x3F)) Serial.println(F("PCF8574 (LCD backpack)"));
    else                                 Serial.println(F("unknown"));
  }
  if (found == 0)     Serial.println(F("    (nothing answered - check 5V, GND, and A4/A5)"));
  else if (found > 8) Serial.println(F("    !! too many hits: SDA stuck LOW, these are phantoms"));
  return found;
}

// ── LCD ──────────────────────────────────────────────────────────────────────
// 0x3C/0x3D are skipped: an SSD1306 OLED sits inside the PCF8574AT block, and
// driving one as an HD44780 would leave both panels blank while the log looked
// perfectly healthy.
static uint8_t lcdFindAddr() {
  if (i2cAck(LCD_I2C_ADDR)) return LCD_I2C_ADDR;
  for (uint8_t a = 0x20; a <= 0x27; a++) if (i2cAck(a)) return a;
  for (uint8_t a = 0x38; a <= 0x3F; a++) {
    if (a == 0x3C || a == 0x3D) continue;
    if (i2cAck(a)) return a;
  }
  return 0;
}

static bool lcdTryBegin() {
  uint8_t a = lcdFindAddr();
  if (!a) { lcdOk = false; return false; }
  if (!lcd || a != lcdAddr) {
    memset(lcdStorage, 0, sizeof(lcdStorage));   // see the note on lcdStorage
    lcd = new (lcdStorage) LiquidCrystal_I2C(a, LCD_COLS, LCD_ROWS);
  }
  lcdAddr = a;
  // begin(), not init(): init() adds a no-arg Wire.begin(). Harmless on an Uno
  // (A4/A5 are the only option) but it re-inits the bus for no reason, and
  // keeping the two builds identical means one less difference to reason about.
  lcd->begin(LCD_COLS, LCD_ROWS);
  lcd->backlight();
  lcd->clear();
  lcdOk = true;
  return true;
}

// Pad/truncate to the panel width so a shorter string never leaves ghost
// characters behind from the previous frame.
static void lcdRow(uint8_t row, const char *s) {
  if (!lcdOk || !lcd) return;
  char buf[LCD_COLS + 1];
  uint8_t n = strlen(s);
  if (n > LCD_COLS) n = LCD_COLS;
  memset(buf, ' ', LCD_COLS);
  memcpy(buf, s, n);
  buf[LCD_COLS] = 0;
  lcd->setCursor(0, row);
  lcd->print(buf);
}

static void lcdReportMissing() {
  Serial.println(F("[uno] LCD NOT found on I2C. Check, in order:"));
  Serial.println(F("    1. VCC -> Uno 5V, GND -> Uno GND"));
  Serial.println(F("    2. SDA -> A4, SCL -> A5 (not swapped)"));
  Serial.println(F("    3. Backlight jumper fitted on the backpack"));
  Serial.println(F("    4. Contrast pot: turn it until blocks appear on row 0"));
  Serial.println(F("    Re-probed every few seconds - fix it and it comes up on its own."));
}

// ── MPU6050 (raw register access — no library, to stay inside 32 KB) ─────────
static bool mpuBegin() {
  if (!i2cAck(MPU6050_ADDR)) return false;
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x6B);            // PWR_MGMT_1
  Wire.write(0x00);            // wake from sleep
  return Wire.endTransmission() == 0;
}

// Magnitude of the acceleration vector, in g. At the default +/-2 g range the
// scale is 16384 LSB/g. At rest this reads ~1.0 (gravity alone).
static float mpuReadMagnitudeG() {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x3B);                       // ACCEL_XOUT_H
  if (Wire.endTransmission(false) != 0) return lastMagG;
  if (Wire.requestFrom(MPU6050_ADDR, 6) != 6) return lastMagG;

  int16_t x = (Wire.read() << 8) | Wire.read();
  int16_t y = (Wire.read() << 8) | Wire.read();
  int16_t z = (Wire.read() << 8) | Wire.read();

  float fx = x / 16384.0f, fy = y / 16384.0f, fz = z / 16384.0f;
  return sqrt(fx * fx + fy * fy + fz * fz);
}

// Count a step on the rising edge past the threshold, with a refractory gap so
// a single stride cannot register twice.
static void pollSteps() {
  if (!mpuOk) return;
  float mag = mpuReadMagnitudeG();
  lastMagG = mag;
  float dev = fabs(mag - 1.0f);
  uint32_t now = millis();

  if (dev > STEP_THRESHOLD_G) {
    if (!aboveThresh && (now - lastStepMs) > STEP_MIN_GAP_MS) {
      stepCount++;
      lastStepMs = now;
      aboveThresh = true;
    }
  } else if (dev < STEP_THRESHOLD_G * 0.6f) {
    aboveThresh = false;               // hysteresis: must fall well back to re-arm
  }
}

// ── Screens ──────────────────────────────────────────────────────────────────
static void drawBoot(uint8_t pct) {
  lcdRow(0, "FitnessAI Watch");
  char bar[LCD_COLS + 1];
  uint8_t fill = (uint16_t)LCD_COLS * pct / 100;
  for (uint8_t i = 0; i < LCD_COLS; i++) bar[i] = (i < fill) ? '#' : ' ';
  bar[LCD_COLS] = 0;
  lcdRow(1, bar);
}

static void drawHome() {
  char line[LCD_COLS + 1];
  uint32_t secs = millis() / 1000;
  lcdRow(0, "FitnessAI Watch");
  snprintf(line, sizeof(line), "up %lum%02lus", secs / 60, secs % 60);
  lcdRow(1, line);
}

static void drawSteps() {
  char line[LCD_COLS + 1];
  lcdRow(0, "STEPS");
  if (mpuOk) snprintf(line, sizeof(line), "%lu", (unsigned long)stepCount);
  else       snprintf(line, sizeof(line), "no sensor");
  lcdRow(1, line);
}

static void drawMotion() {
  char line[LCD_COLS + 1];
  lcdRow(0, "MOTION");
  if (mpuOk) {
    // dtostrf, not %f: AVR's printf has no float support compiled in.
    char g[8];
    dtostrf(lastMagG, 4, 2, g);
    snprintf(line, sizeof(line), "%s g", g);
  } else {
    snprintf(line, sizeof(line), "no sensor");
  }
  lcdRow(1, line);
}

static void drawSensors() {
  char line[LCD_COLS + 1];
  snprintf(line, sizeof(line), "MPU6050 %s", mpuOk ? "OK" : "--");
  lcdRow(0, line);
  snprintf(line, sizeof(line), "MAX3010x %s", maxOk ? "OK" : "--");
  lcdRow(1, line);
}

static void drawScreen() {
  switch (screen) {
    case 0: drawHome();    break;
    case 1: drawSteps();   break;
    case 2: drawMotion();  break;
    default: drawSensors(); break;
  }
}

// ── Serial console ───────────────────────────────────────────────────────────
static void handleSerial() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == 'i') {
    i2cScanLog();
  } else if (c == 'd') {
    Serial.print(F("[uno] LCD ok="));
    Serial.print(lcdOk ? 1 : 0);
    Serial.print(F(" addr=0x"));
    Serial.println(lcdAddr, HEX);
    if (!lcdOk && lcdTryBegin()) Serial.println(F("[uno] LCD recovered"));
  } else if (c == 'r') {
    stepCount = 0;
    Serial.println(F("[uno] steps reset"));
  } else if (c == 'p') {
    Serial.print(F("[uno] steps="));
    Serial.print(stepCount);
    Serial.print(F(" mpu="));
    Serial.print(mpuOk ? 1 : 0);
    Serial.print(F(" max="));
    Serial.println(maxOk ? 1 : 0);
  } else if (c == 'h') {
    Serial.println(F("  i = I2C scan   d = display state   r = reset steps"));
    Serial.println(F("  p = status     h = this help"));
  }
}

// ── Setup / loop ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n[uno] FitnessAI Watch - Uno standalone display build"));

  Wire.begin();                    // Uno I2C is fixed at A4/A5
  Wire.setClock(I2C_CLOCK_HZ);

  i2cScanLog();

  if (lcdTryBegin()) {
    Serial.print(F("[uno] LCD found at 0x"));
    Serial.println(lcdAddr, HEX);
  } else {
    lcdReportMissing();
  }

  mpuOk = mpuBegin();
  Serial.println(mpuOk ? F("[uno] MPU6050 OK") : F("[uno] MPU6050 absent"));

  maxOk = i2cAck(MAX3010X_ADDR);
  Serial.println(maxOk ? F("[uno] MAX3010x present") : F("[uno] MAX3010x absent"));

  for (uint8_t i = 0; i <= 20; i++) {   // boot bar, same idea as the ESP32 build
    drawBoot(i * 5);
    delay(60);
  }

  lastSwitch = millis();
  Serial.println(F("[uno] ready - type h for commands"));
}

void loop() {
  uint32_t now = millis();

  handleSerial();

  if (now - lastSample >= SAMPLE_MS) {
    lastSample = now;
    pollSteps();
  }

  if (now - lastSwitch >= SCREEN_DWELL_MS) {
    lastSwitch = now;
    screen = (screen + 1) & 0x03;
    // Re-probe a missing panel on the same cadence, so fixing the wiring brings
    // the display up on its own without a reset or a reflash.
    if (!lcdOk && lcdTryBegin()) {
      Serial.print(F("[uno] LCD appeared at 0x"));
      Serial.println(lcdAddr, HEX);
    }
    drawScreen();
  }
}
