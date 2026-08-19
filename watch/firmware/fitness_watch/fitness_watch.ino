/*
 * FitnessAI Watch — main program
 * Board:   ESP32-C3 SuperMini
 * Display: SSD1306 128x64 OLED (I2C), rendered with U8g2
 * Sensors: MAX30105 heart rate/SpO2 (SparkFun MAX3010x lib), MPU6050 accel/gyro
 *          (Adafruit_MPU6050 lib) — both on the same I2C bus as the OLED.
 *
 * Scope: screen + program skeleton + heart rate + step counting + buttons.
 *   boot  →  loading screen (animated bar)  →  home screen (live)
 *   Buttons: A (GPIO4) tap = prev screen, hold = home; B (GPIO5) single = home,
 *            double = display mute (screen off, vitals keep running).
 * All wiring is in config.h. Libraries: "U8g2" by oliver, "SparkFun MAX3010x
 * Pulse and Proximity Sensor Library", "Adafruit MPU6050" (+ Adafruit Sensor,
 * Adafruit BusIO).
 */

// config.h FIRST — DISPLAY_TYPE is defined there, and the conditional includes
// below select which display library to pull in.
#include "config.h"

#include <Wire.h>
#if DISPLAY_TYPE == DISPLAY_OLED
#include <U8g2lib.h>
#else
#include <LiquidCrystal_I2C.h>
#include <new>                  // placement new -- see lcdStorage below
#endif
#include <MAX30105.h>
#include <heartRate.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <esp_sleep.h>
#include "net.h"   // settings, clock, WiFi, pairing portal, sync
#include "ble.h"   // BLE control peripheral (pair / time / commands)
#include "power.h" // battery monitor (cell voltage -> % / USB detection)
#include "sim.h"   // synthetic MAX30105 — compiles to nothing unless -DSIM_BUILD=1

#if SIM_BUILD
// Scenario/serial-driven knobs for the simulator (see sim.h). Declared here so
// there is exactly one definition; sim.h only extern-declares them.
int g_simBpm = SIM_HR_BPM;
int g_simFingerForce = -1;   // -1 = follow sim.h's schedule
#endif

// Serial diagnostics: toggling NET_DEBUG to 0 silences the chatty [net]/[watch]
// logs, leaving only errors and one-liners. Leave on while bring-up is ongoing.
#define DBG(fmt, ...) do { if (DEBUG_SERIAL) Serial.printf("[watch] " fmt "\n", ##__VA_ARGS__); } while (0)

// ── I2C bus probing ──────────────────────────────────────────────────────────
// Every panel/sensor on this build shares one bus, so "is it even there?" is the
// first question for any bring-up problem. These helpers answer it from the
// firmware itself, so a dark screen no longer means flashing a separate scanner
// sketch — the boot log already says what is on the wire.

// Does a device ACK its address on the CURRENT Wire pins?
static bool i2cAck(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// Log every address that answers. Returns how many did.
//
// A count of 0 with nothing wired is normal; a count in the dozens means SDA is
// stuck LOW (miswired, or a module powered off while its pull-ups drag the line)
// and EVERY address "answers" — the floating-line phantom that made the earlier
// hand-run scans report an LCD at 0x27 that was never there.
static int i2cScanLog(const char *tag) {
  int found = 0;
  Serial.printf("[watch] I2C scan (%s) SDA=%d SCL=%d @%d Hz:\n",
                tag, PIN_I2C_SDA, PIN_I2C_SCL, I2C_CLOCK_HZ);
  for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
    if (!i2cAck(addr)) continue;
    found++;
    const char *what = "?";
    if (addr == OLED_ADDR)              what = "SSD1306 OLED";
    else if (addr == MPU6050_ADDR)      what = "MPU6050";
    else if (addr == MAX30105_ADDR)     what = "MAX30102/05";
    else if ((addr >= 0x20 && addr <= 0x27) ||
             (addr >= 0x38 && addr <= 0x3F)) what = "PCF8574 (LCD backpack)";
    Serial.printf("    0x%02X  %s\n", addr, what);
  }
  if (found == 0) Serial.println("    (nothing answered - check power + SDA/SCL wiring)");
  else if (found > 8) Serial.println("    !! too many hits: SDA is stuck LOW, these are phantoms");
  return found;
}

// Full-buffer, hardware-I2C SSD1306. This 0.96" panel needs the standard NONAME
// init. (ALT0 made the sparse measurement pattern look OK but interleaves the
// rows with real text -> overlapping lines.) See OLED_INIT_ALT0 in config.h.
#if DISPLAY_TYPE == DISPLAY_OLED
  #if OLED_INIT_ALT0
  U8G2_SSD1306_128X64_ALT0_F_HW_I2C u8g2(U8G2_R0, /*reset=*/U8X8_PIN_NONE);
  #else
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /*reset=*/U8X8_PIN_NONE);
  #endif
#else
// The panel is constructed at RUNTIME, not statically, because its I2C address
// is discovered rather than assumed: PCF8574 backpacks ship as either the 'T'
// part (0x20-0x27, usually 0x27) or the 'AT' part (0x38-0x3F, usually 0x3F), and
// which one you have is not printed on the board. lcdFindAddr() probes for it.
static LiquidCrystal_I2C *lcd = nullptr;
static uint8_t lcdAddr = 0;      // address it actually answered on (0 = absent)
static bool    lcdOk   = false;  // false = no panel on the bus; all draws no-op

// The panel object lives in a ZEROED static buffer and is placement-new'd, not
// heap-allocated. That is not a style preference -- it is required for
// correctness, because the library leaves _displayfunction uninitialised:
//
//   LiquidCrystal_I2C.cpp:62   _displayfunction = LCD_4BITMODE|LCD_1LINE|LCD_5x8DOTS
//                              ^ the ONLY assignment, and it lives in init_priv()
//   LiquidCrystal_I2C.cpp:68   _displayfunction |= LCD_2LINE      (OR, not assign)
//   LiquidCrystal_I2C.cpp:107  command(LCD_FUNCTIONSET | _displayfunction)
//
// The constructor sets _Addr/_cols/_rows/_backlightval and nothing else. We call
// begin() rather than init() on purpose (init() would call no-arg Wire.begin()
// and drag the bus back to GPIO 8/9), but that skips line 62 -- so whatever is
// in that byte is what gets sent to the HD44780 as its function set. Zeroed
// storage makes it 0x00, which is exactly LCD_4BITMODE|LCD_1LINE|LCD_5x8DOTS, so
// the panel receives 0x28 = 4-bit, 2-line, 5x8. On uninitialised heap memory a
// stray LCD_8BITMODE (0x10) bit would tell the panel it is in 8-bit mode
// immediately after we put it in 4-bit mode, and it would never display anything.
//
// Static storage also keeps the hot-plug retry from churning the heap: it
// re-runs every DISPLAY_SELF_HEAL_MS for as long as the panel is missing.
static uint8_t lcdStorage[sizeof(LiquidCrystal_I2C)];

// Write one line of text to the character LCD, padded/truncated to LCD_COLS so
// a shorter string never leaves ghost characters from the previous frame.
//
// No-ops when the panel is absent. That matters for more than tidiness: each
// write to a missing device costs a full I2C timeout, and a 2-row frame is ~40
// of them — enough blocking to stall beat detection and drop button presses.
static void lcdRow(uint8_t row, const char *s) {
  if (!lcdOk || !lcd) return;
  char buf[LCD_COLS + 1];
  int n = (int)strlen(s);
  if (n > LCD_COLS) n = LCD_COLS;
  memset(buf, ' ', LCD_COLS);
  memcpy(buf, s, (size_t)n);
  buf[LCD_COLS] = 0;
  lcd->setCursor(0, row);
  lcd->print(buf);
}
#endif

// Panel init / power — one wrapper for both backends so setup() and the
// standby logic don't care which panel is configured.
#if DISPLAY_TYPE == DISPLAY_OLED
static void dispBegin() {
  u8g2.setI2CAddress(OLED_ADDR << 1);   // U8g2 uses the 8-bit address form
  u8g2.begin();
}
static void dispReinit() { u8g2.begin(); }
static void dispPower(bool on) {
  u8g2.setPowerSave(on ? 0 : 1);
}
#else
// Find the backpack. LCD_I2C_ADDR from config.h is tried first (so an explicit
// setting always wins), then both PCF8574 address blocks. Returns 0 if nothing
// on the bus looks like a backpack.
//
// The SSD1306's 0x3C/0x3D sit INSIDE the PCF8574AT block (0x38-0x3F) and must be
// skipped: on a bus carrying both panels — which is exactly what the Wokwi
// diagram and a bench setup mid-swap look like — a blind sweep would return the
// OLED's address and then drive an SSD1306 as if it were an HD44780. Nothing
// would appear on either panel and the address in the log would look correct,
// which is the worst kind of bug to hit at a deadline.
static bool lcdAddrUsable(uint8_t a) {
  return a != 0x3C && a != 0x3D;    // SSD1306 primary / alternate
}

static uint8_t lcdFindAddr() {
  if (i2cAck(LCD_I2C_ADDR)) return LCD_I2C_ADDR;
  for (uint8_t a = 0x20; a <= 0x27; a++)                            // PCF8574T
    if (lcdAddrUsable(a) && i2cAck(a)) return a;
  for (uint8_t a = 0x38; a <= 0x3F; a++)                            // PCF8574AT
    if (lcdAddrUsable(a) && i2cAck(a)) return a;
  return 0;
}

// Probe, then (re)build and initialise the panel. Safe to call repeatedly — it
// is the hot-plug path as well as the boot path.
static bool lcdTryBegin() {
  uint8_t a = lcdFindAddr();
  if (!a) { lcdOk = false; return false; }
  if (!lcd || a != lcdAddr) {                 // first build, or the address moved
    memset(lcdStorage, 0, sizeof(lcdStorage));   // see the note on lcdStorage
    lcd = new (lcdStorage) LiquidCrystal_I2C(a, LCD_COLS, LCD_ROWS);
  }
  lcdAddr = a;
  // NOTE: use begin(cols, rows), NOT init(). This library's init() calls
  // Wire.begin() with NO arguments, which resets the ESP32-C3's I2C bus to the
  // board's DEFAULT pins (GPIO 8/9) instead of our config's 7/8. begin() skips
  // that Wire re-init, so the shared bus stays on PIN_I2C_SDA/SCL.
  lcd->begin(LCD_COLS, LCD_ROWS);
  lcd->backlight();
  lcd->clear();
  lcdOk = true;
  return true;
}

// Everything worth checking when the panel does not answer, printed where the
// person holding the board will actually see it.
static void lcdReportMissing() {
  Serial.printf("[watch] LCD1602 NOT found on I2C (SDA=%d SCL=%d). Check, in order:\n",
                PIN_I2C_SDA, PIN_I2C_SCL);
  Serial.println("    1. VCC -> the board's 5V pin, NOT 3V3. A 5 V HD44780 module shows");
  Serial.println("       nothing at 3.3 V: the backlight barely glows and the contrast");
  Serial.println("       bias never reaches the segments. This is the usual cause of");
  Serial.println("       'not even lit'. (See 'Wiring the LCD1602' in watch/README.md");
  Serial.println("       for the pull-up caveat that comes with running the backpack at 5 V.)");
  Serial.println("    2. GND -> GND, shared with the ESP32.");
  Serial.println("    3. SDA/SCL on the backpack -> GPIO 7 / GPIO 8 (not swapped).");
  Serial.println("    4. Backlight jumper present on the backpack.");
  Serial.println("    5. Contrast pot: turn it until faint blocks appear on row 0.");
  Serial.println("    The panel is re-probed every few seconds - fix the wiring and it");
  Serial.println("    lights up on its own, no reflash needed.");
}

static void dispBegin() {
  if (lcdTryBegin()) {
    Serial.printf("[watch] LCD1602 %dx%d found at 0x%02X (SDA=%d SCL=%d)\n",
                  LCD_COLS, LCD_ROWS, lcdAddr, PIN_I2C_SDA, PIN_I2C_SCL);
  } else {
    lcdReportMissing();
  }
}
static void dispReinit() { lcdTryBegin(); }
static void dispPower(bool on) {
  if (!lcdOk || !lcd) return;
  if (on) lcd->backlight(); else lcd->noBacklight();
}
#endif

MAX30105 particleSensor;
Adafruit_MPU6050 mpu;
static bool maxOk = false;
static bool mpuOk = false;

// ── Heart rate (SparkFun's beat-averaging approach) ──────────────────────────
static const byte RATE_SIZE = 4;
static byte rates[RATE_SIZE];
static byte rateSpot = 0;
static byte rateFilled = 0;      // how many slots hold a real reading (avoid averaging in zeros)
static uint32_t lastBeatMs = 0;
static int beatAvg = 0;
static bool fingerPresent = false;

// ── MAX30105 LED power ───────────────────────────────────────────────────────
// The IR LED is the sensor's whole power budget, and it only needs to be bright
// while a pulse is actually being measured. Between readings it drops to a
// proximity-detect level, which is enough to notice a finger arriving.
static bool maxDimmed = false;        // true = IR running at MAX_LED_IR_IDLE
static uint32_t lastFingerMs = 0;

static void maxSetLeds(bool full) {
  maxDimmed = !full;
#if !SIM_BUILD
  if (!maxOk) return;
  particleSensor.setPulseAmplitudeRed(MAX_LED_RED);
  particleSensor.setPulseAmplitudeIR(full ? MAX_LED_IR : MAX_LED_IR_IDLE);
#endif
}

// The presence threshold has to track the LED current: reflected IR scales with
// how hard the LED is driven, so a fixed 50000 would never be reached once the
// LED is dimmed and a finger would go unnoticed forever.
static long fingerThreshold() {
  return maxDimmed ? MAX_FINGER_THRESH_IDLE : MAX_FINGER_THRESHOLD;
}

// Clear every derived HR value. Called when the finger leaves so a stale BPM
// can't reappear the instant it comes back — the next reading starts from zero.
static void resetHeartRate() {
  for (byte i = 0; i < RATE_SIZE; i++) rates[i] = 0;
  rateSpot = 0;
  rateFilled = 0;
  beatAvg = 0;
  lastBeatMs = 0;
}

// ── Step counting (simple threshold/debounce on accel magnitude) ────────────
static uint32_t stepCount = 0;

// Latest motion figures, kept for the MOTION screen. pollAccelStep() already
// computes all three every cycle for step detection and auto-orientation; they
// were simply being discarded afterwards.
static float lastAccelMs2 = 0.0f;   // |acceleration| including gravity (~9.81 at rest)
static float lastDynMs2   = 0.0f;   // deviation from gravity = actual movement
static float lastGyroRads = 0.0f;   // |rotation rate|, rad/s
static bool aboveStepThreshold = false;
static uint32_t lastStepMs = 0;
static const float STEP_THRESHOLD_MS2 = 2.0f;  // deviation from gravity to count as motion
static const uint32_t STEP_DEBOUNCE_MS = 250;  // min gap between steps (caps ~240 spm)

// ── Auto-orientation (MPU6050 accel + gyro) ─────────────────────────────────
// The watch is a square-ish display worn on a rotating wrist, so the UI is
// re-rotated to always read upright. HOW: the accelerometer measures gravity,
// which tells us which edge is "up" (the gyro alone would drift — integration
// error accumulates); the gyro's rotation-rate gates the switch so the screen
// doesn't flip while you're actively twisting your wrist. A time-based hold
// gives ~0.5 s of hysteresis so it can't flicker around a 45° boundary.
static int curRotation = 0;              // 0..3 -> U8G2_R0..R3
static int rotCandidate = -1;            // rotation the sensor currently wants
static uint32_t rotSinceMs = 0;          // when that candidate first appeared
#if DISPLAY_TYPE == DISPLAY_OLED
static const u8g2_cb_t *ROT_CBS[4] = {U8G2_R0, U8G2_R1, U8G2_R2, U8G2_R3};
#endif

static void applyRotation(int r) {
  // LCD builds have no rotation support; orientScreen() is compiled out via
  // ORIENT_ENABLE=0, so this is never reached — but it still needs to compile.
#if DISPLAY_TYPE == DISPLAY_OLED
  if (r == curRotation) return;
  curRotation = r;
  u8g2.setDisplayRotation(ROT_CBS[r]);
  DBG("orientation -> R%d", r);
#endif
}

static void orientScreen(float ax, float ay, float az, float gx, float gy, float gz) {
  if (!ORIENT_ENABLE) return;

  // Trust the reading only if its magnitude is plausible gravity. A disconnecting
  // MPU can return zeros, huge values, or NaN — those would drive random rotation
  // and look like the screen "tweaking" mid-use. Reject and hold current rotation.
  float amag = sqrtf(ax * ax + ay * ay + az * az);
  if (!isfinite(amag) || amag < ORIENT_MIN_G_MS2 || amag > ORIENT_MAX_G_MS2) {
    rotCandidate = -1;
    return;
  }

  // Moving fast? Don't re-orient mid-gesture — wait until the wrist settles.
  float gyroMag = sqrtf(gx * gx + gy * gy + gz * gz);
  if (gyroMag > ORIENT_MOTION_RAD_S) {
    rotCandidate = -1;
    return;
  }

  // Lying flat (screen up/down): gravity sits almost entirely on z, so there's
  // no meaningful "top edge". Keep the last rotation until the watch is stood up.
  float horiz = sqrtf(ax * ax + ay * ay);
  if (horiz < ORIENT_FLAT_MS2) {
    rotCandidate = -1;
    return;
  }

  // "Up" in the sensor frame = -g (an accelerometer at rest reads +1 g toward
  // the sky). Map the up vector onto the four rotations. Default mounting:
  // sensor +y points at the screen top when worn normally, so:
  //   R0 -> up=+y   R1 -> up=+x   R2 -> up=-y   R3 -> up=-x
  // (If the MPU6050 is soldered rotated on the panel, swap this table.)
  static const float TARGET_DEG[4] = {90.0f, 0.0f, -90.0f, 180.0f};
  float angle = atan2f(-ay, -ax) * RAD_TO_DEG;

  int want = 0;
  float best = 1e9f;
  for (int r = 0; r < 4; r++) {
    float d = fabsf(angle - TARGET_DEG[r]);
    if (d > 180.0f) d = 360.0f - d;
    if (d < best) { best = d; want = r; }
  }

  if (want == rotCandidate) {
    if (millis() - rotSinceMs >= ORIENT_HOLD_MS) applyRotation(want);
  } else {
    rotCandidate = want;
    rotSinceMs = millis();
  }
}

// ── Buttons (debounced, tap vs hold) ─────────────────────────────────────────
// 25 ms, not 50: every edge costs this much latency, and BOTH edges of BOTH
// taps sit inside the double-tap window. A tactile switch settles in under
// ~5 ms, so 25 ms is still generous, and halving it buys 50 ms of margin in a
// gesture that measurement showed had none. See lab-notes/2026-08-12-findings.md.
// UNVERIFIED: not rebuilt since (the RISC-V compiler is blocked by Smart App
// Control). If bounce ever shows up as phantom taps, put this back to 50.
static const uint32_t BTN_DEBOUNCE_MS = 25;    // stable read before trusting a level change
static const uint32_t BTN_HOLD_MS    = 600;    // press longer than this counts as a HOLD

// struct ButtonState is declared in config.h — see the note there about the
// Arduino builder's auto-generated prototypes.
static ButtonState btnA = {false, false, 0, 0, false, false, false};
static ButtonState btnB = {false, false, 0, 0, false, false, false};

static void updateButton(ButtonState &b, int pin, uint32_t now) {
  bool raw = digitalRead(pin) == (BTN_ACTIVE_HIGH ? HIGH : LOW);

  if (raw != b.lastRaw) {
    b.lastRaw = raw;
    b.lastChange = now;
  }
  // Level changed and has now been stable for the full debounce window.
  if (raw != b.stable && now - b.lastChange >= BTN_DEBOUNCE_MS) {
    b.stable = raw;
    if (b.stable) {          // press begins — nothing fires yet
      b.holdSince = now;
      b.holdEdge = false;
      b.held = false;
    } else if (!b.held) {    // released before BTN_HOLD_MS → that was a tap
      b.tapEdge = true;
    }
  }
  // Still holding past the hold threshold → report the hold once. `held` then
  // suppresses the tap on release, so a hold does NOT also cycle the screen.
  if (b.stable && !b.held && now - b.holdSince >= BTN_HOLD_MS) {
    b.holdEdge = true;
    b.held = true;
  }
}

// ── Screen state machine ─────────────────────────────────────────────────────
static Screen screen = SCREEN_BOOT;
static uint32_t bootStartMs = 0;
static bool pairShowToken = false;   // PAIR screen: show full token instead of steps

// ── Display power state ──────────────────────────────────────────────────────
// Declared up here, not next to enterStandby()/exitStandby() further down,
// because pollAccelStep() above reads them for the optional wake-on-motion hook
// and the Arduino builder only auto-forward-declares FUNCTIONS, never variables.
static bool standby = false;            // true = panel is off (timeout or mute)
static uint32_t lastActivityMs = 0;     // last button interaction
static bool redrawNow = false;          // force a repaint on the next pass
static void exitStandby();              // defined with the rest of the display code

static void goScreen(Screen s) {
  screen = s;
  bootStartMs = millis();
  // Repaint immediately rather than waiting out DRAW_INTERVAL_MS. This is what
  // lets the redraw rate drop to 1 Hz without the UI feeling laggy: periodic
  // repaints are only there for the clock, while anything the user actually
  // triggered is drawn at once.
  redrawNow = true;
}

// ── Loading screen ───────────────────────────────────────────────────────────
#if DISPLAY_TYPE == DISPLAY_OLED
static void drawLoadingOled(uint8_t pct) {
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_helvB12_tr);
  const char *title = "FitnessAI";
  u8g2.drawStr((OLED_WIDTH - u8g2.getStrWidth(title)) / 2, 22, title);

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
#endif  // DISPLAY_TYPE == DISPLAY_OLED

// ── Demo heart rate ──────────────────────────────────────────────────────────
// A demo unit usually has no MAX30105 fitted, which would leave the HR screen
// reading "--" forever. This supplies a gently drifting resting pulse so there
// is something to show.
//
// It is NOT a measurement and is never presented as one: every screen that
// shows it prints "demo" beside it, the boot log says so, and DEMO_MODE
// transmits nothing at all -- there is no path from here to the backend. A real
// sensor, when fitted, always wins (demoHrActive() is false whenever maxOk).
#if DEMO_MODE && DEMO_HR_SYNTH
static bool demoHrActive() { return !maxOk; }

static int demoHr() {
  // Triangle wave, one full sweep a minute, 62-74 bpm. Integer maths only --
  // no float, and nothing that pretends to be a physiological model.
  uint32_t t = (millis() / 500) % 120;            // 0..119 over 60 s
  int tri = (t < 60) ? (int)t : (int)(120 - t);   // 0..60..0
  return 62 + (tri * 12) / 60;
}
#else
static bool demoHrActive() { return false; }
static int  demoHr()       { return 0; }
#endif

// ── Loading screen (LCD) ─────────────────────────────────────────────────────
#if DISPLAY_TYPE == DISPLAY_LCD1602
static void drawLoadingLcd(uint8_t pct) {
  char line[LCD_COLS + 1];
  char bar[LCD_COLS + 1];
  int fill = (int)((long)LCD_COLS * pct / 100);
  for (int i = 0; i < LCD_COLS; i++) bar[i] = (i < fill) ? '#' : ' ';
  bar[LCD_COLS] = 0;

  if (LCD_ROWS < 3) {
    // 1602: the percentage has to share row 0 with the title.
    snprintf(line, sizeof(line), "FitnessAI  %u%%", pct);
    lcdRow(0, line);
    lcdRow(1, bar);
    return;
  }

  // 2004: title, bar, and room to say what is actually happening.
  lcdRow(0, "FitnessAI Watch");
  lcdRow(1, bar);
  snprintf(line, sizeof(line), "starting up... %u%%", pct);
  lcdRow(2, line);
  snprintf(line, sizeof(line), "v%s  %dx%d", APP_VERSION, LCD_COLS, LCD_ROWS);
  lcdRow(3, line);
}
#endif  // DISPLAY_TYPE == DISPLAY_LCD1602

static void drawLoading(uint8_t pct) {
#if DISPLAY_TYPE == DISPLAY_OLED
  drawLoadingOled(pct);
#else
  drawLoadingLcd(pct);
#endif
}

// ── Sensor polling ───────────────────────────────────────────────────────────
// Sampled every loop() iteration (no throttling) — the beat-detection algorithm
// needs frequent IR samples to time pulses accurately.
static void pollHeartRate() {
  if (!maxOk) return;

  // Budget the I2C reads. The MAX30105 lives on the SAME bus as the OLED, and
  // getIR() is a register transaction; running it on every loop pass (potentially
  // 1000+ reads/s with an empty finger) hammers the shared bus and makes the
  // SSD1306 frame address drift/corrupt. 1 ms wall = at most ~1 kHz; a finger
  // only needs ~200-400 Hz sampling for beat detection, and no finger needs none.
  //
  // KNOWN DEFECT (found in the simulator, applies equally to hardware):
  // sendBuffer() pushes a full 128x64 frame over I2C at 100 kHz, which BLOCKS
  // loop() for ~90 ms, and it runs every 200 ms. So ~45% of the time no IR
  // samples are taken at all. At 72 bpm the systolic peak is only ~58 ms wide,
  // so peaks land inside that blind window and are missed — and one missed beat
  // doubles the measured interval, halving the reported rate. The simulator
  // reads 24 bpm against a synthetic 72. Worse, beats are timestamped with
  // millis() at READ time rather than capture time, so bursty reading skews the
  // intervals even when a peak is not missed.
  //
  // The fix is to drain the MAX30105's hardware FIFO (32 samples at the
  // configured 400 Hz) via check()/available()/getFIFOIR() instead of sampling
  // the latest value: a 90 ms stall then costs nothing, because the samples are
  // still queued in the sensor with their true spacing. Not done yet — it needs
  // a rebuild to test, and the toolchain is currently blocked.
  // See lab-notes/2026-08-12-findings.md.
  static uint32_t lastReadMs = 0;
  uint32_t nMs = millis();
  if (nMs - lastReadMs < 5) return;
  lastReadMs = nMs;

#if SIM_BUILD
  long irValue = simIR();              // synthetic PPG; the detection path below is real
#else
  long irValue = particleSensor.getIR();
#endif
  bool nowPresent = irValue > fingerThreshold();   // low IR = no finger

  // Finger just left → drop every derived value rather than holding the last BPM.
  if (fingerPresent && !nowPresent) resetHeartRate();
  fingerPresent = nowPresent;

  // LED duty management. Full current whenever a finger is there, and back down
  // to proximity level once it has been gone a while. The delay stops the LED
  // flapping between levels while a finger hovers around the threshold.
  if (nowPresent) {
    lastFingerMs = nMs;
    if (maxDimmed) maxSetLeds(true);
  } else if (MAX_IDLE_DIM && !maxDimmed && (nMs - lastFingerMs) > MAX_IDLE_AFTER_MS) {
    maxSetLeds(false);
  }

  // Only look for beats when there is actually a finger. On an empty sensor the
  // IR signal is noise, and checkForBeat() happily reports beats in noise.
  if (!fingerPresent) return;

  if (checkForBeat(irValue)) {
    uint32_t now = millis();

    // First beat of a contact has no previous beat to measure against — the
    // interval would be "time since reset", so record it and wait for the next.
    if (lastBeatMs == 0) {
      lastBeatMs = now;
      return;
    }

    float delta = (float)(now - lastBeatMs);
    lastBeatMs = now;

    float bpm = 60000.0f / delta;
    if (bpm > 20 && bpm < 255) {
      rates[rateSpot++] = (byte)bpm;
      rateSpot %= RATE_SIZE;
      if (rateFilled < RATE_SIZE) rateFilled++;

      // Average only the slots actually written. Dividing by RATE_SIZE before
      // the buffer fills averages in zeros and reports ~1/4 of the true rate.
      int sum = 0;
      for (byte i = 0; i < rateFilled; i++) sum += rates[i];
      beatAvg = sum / rateFilled;
    }
  }
}

static void pollAccelStep() {
  if (!mpuOk) return;

  // Throttle to 50 Hz. The MPU6050 is on the SAME I2C bus as the MAX30105, and
  // getEvent() reads ~14 registers per call; hammering it every loop iteration
  // steals bus time from the heart-rate sensor and jitters its beat timing.
  static uint32_t lastAccelMs = 0;
  uint32_t nowMs = millis();
  if (nowMs - lastAccelMs < 20) return;
  lastAccelMs = nowMs;

  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);

  // Keep the UI upright whichever way the watch is turned (uses the gyro + the
  // same accel events we already paid for).
  orientScreen(accel.acceleration.x, accel.acceleration.y, accel.acceleration.z,
               gyro.gyro.x, gyro.gyro.y, gyro.gyro.z);

  float mag = sqrtf(accel.acceleration.x * accel.acceleration.x +
                     accel.acceleration.y * accel.acceleration.y +
                     accel.acceleration.z * accel.acceleration.z);
  float dynamic = fabsf(mag - 9.80665f);   // deviation from gravity at rest

  lastAccelMs2 = mag;
  lastDynMs2   = dynamic;
  lastGyroRads = sqrtf(gyro.gyro.x * gyro.gyro.x +
                       gyro.gyro.y * gyro.gyro.y +
                       gyro.gyro.z * gyro.gyro.z);

  if (dynamic > STEP_THRESHOLD_MS2) {
    if (!aboveStepThreshold && (nowMs - lastStepMs) > STEP_DEBOUNCE_MS) {
      stepCount++;
      lastStepMs = nowMs;
    }
    aboveStepThreshold = true;
  } else {
    aboveStepThreshold = false;
  }

#if WAKE_ON_MOTION
  // Off by default — see WAKE_ON_MOTION in config.h for why a bare threshold
  // cannot tell a wrist-raise from a stride.
  if (standby && dynamic > WAKE_MOTION_MS2) exitStandby();
#endif
}

// ── Home screen ──────────────────────────────────────────────────────────────
#if DISPLAY_TYPE == DISPLAY_OLED
// Placeholder clock (counts seconds since boot) so we can see the loop is alive.
// Real time arrives in a later milestone (Wi-Fi sync). HR + steps are live.
static void footerText(char *out, size_t len) {
  if (!maxOk || !mpuOk) {
    snprintf(out, len, "%s%s missing", !maxOk ? "HR " : "", !mpuOk ? "IMU" : "");
  } else if (!fingerPresent || beatAvg <= 0) {
    snprintf(out, len, "-- bpm  %lu steps", (unsigned long)stepCount);
  } else {
    snprintf(out, len, "%d bpm  %lu steps", beatAvg, (unsigned long)stepCount);
  }
}

static void drawHome() {
  uint32_t s = millis() / 1000;
  int hh = (s / 3600) % 24, mm = (s / 60) % 60, ss = s % 60;
  bool colon = (s % 2) == 0;   // blink the colon every second

  u8g2.clearBuffer();

  // Top status bar
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(2, 7, "FitnessAI");
  const char *batt = battLabel();   // "USB" / "CHG" / "87%" / "LOW"
  u8g2.drawStr(OLED_WIDTH - u8g2.getStrWidth(batt) - 2, 7, batt);
  u8g2.drawHLine(0, 10, OLED_WIDTH);

  // Big clock
  char t[9];
  snprintf(t, sizeof(t), "%02d%c%02d", hh, colon ? ':' : ' ', mm);
  u8g2.setFont(u8g2_font_logisoso28_tn);   // large numeric font
  u8g2.drawStr((OLED_WIDTH - u8g2.getStrWidth(t)) / 2, 44, t);

  // Seconds
  u8g2.setFont(u8g2_font_5x7_tr);
  char sec[4];
  snprintf(sec, sizeof(sec), ":%02d", ss);
  u8g2.drawStr(OLED_WIDTH - u8g2.getStrWidth(sec) - 2, 44, sec);
  u8g2.drawHLine(0, 53, OLED_WIDTH);

  // Footer: heart rate + steps, or which sensor(s) failed to init.
  // 32 bytes: "255 bpm  4294967295 steps" is 25 chars + NUL, so 24 truncated.
  char foot[32];
  footerText(foot, sizeof(foot));
  u8g2.drawStr(2, 63, foot);

  u8g2.sendBuffer();
}
#endif  // DISPLAY_TYPE == DISPLAY_OLED

// ── Home screen (LCD) ────────────────────────────────────────────────────────
// Mirrors the OLED home: a title + battery status bar, the clock, and a footer
// with live HR + steps. A 1602 has only two rows, so those pack onto row 1;
// a 2004 spreads the same info across four labelled rows.
#if DISPLAY_TYPE == DISPLAY_LCD1602
static void drawHomeLcd() {
  uint32_t s = millis() / 1000;
  int hh = (s / 3600) % 24, mm = (s / 60) % 60, ss = s % 60;
  char line[LCD_COLS + 1];

  if (LCD_ROWS < 3) {
    // 1602: two rows. Row 0 = clock + battery, row 1 = HR + steps.
    snprintf(line, sizeof(line), "%02d:%02d:%02d %s", hh, mm, ss, battLabel());
    lcdRow(0, line);

    if (!maxOk || !mpuOk) {
      snprintf(line, sizeof(line), "%s%s missing", !maxOk ? "HR " : "", !mpuOk ? "IMU" : "");
    } else if (!fingerPresent || beatAvg <= 0) {
      snprintf(line, sizeof(line), "-- bpm  %lu st", (unsigned long)stepCount);
    } else {
      snprintf(line, sizeof(line), "%d bpm  %lu st", beatAvg, (unsigned long)stepCount);
    }
    lcdRow(1, line);
    return;
  }

  // 2004: one labelled row per item — status bar, clock, HR, steps.
  snprintf(line, sizeof(line), "FitnessAI  %s", battLabel());
  lcdRow(0, line);

  snprintf(line, sizeof(line), "%02d:%02d:%02d", hh, mm, ss);
  lcdRow(1, line);

  if (demoHrActive()) snprintf(line, sizeof(line), "HR   %d bpm (demo)", demoHr());
  else if (!maxOk) snprintf(line, sizeof(line), "HR   sensor missing");
  else if (!fingerPresent) snprintf(line, sizeof(line), "HR   no finger --");
  else if (beatAvg <= 0) snprintf(line, sizeof(line), "HR   measuring...");
  else snprintf(line, sizeof(line), "HR   %d bpm", beatAvg);
  lcdRow(2, line);

#if DEMO_MODE
  // A demo unit is offline by definition, so say so on the face of it rather
  // than showing a step count nobody is going to look at.
  snprintf(line, sizeof(line), "DEMO - offline");
#else
  if (!mpuOk) snprintf(line, sizeof(line), "IMU  missing");
  else snprintf(line, sizeof(line), "STEPS %lu", (unsigned long)stepCount);
#endif
  lcdRow(3, line);
}
#endif  // DISPLAY_TYPE == DISPLAY_LCD1602

// ── Heart-rate screen ────────────────────────────────────────────────────────
#if DISPLAY_TYPE == DISPLAY_OLED
static void drawHR() {
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(2, 7, "HEART RATE");
  u8g2.drawHLine(0, 10, OLED_WIDTH);

  char big[5];
  // "--" until a real average exists: no sensor, no finger, or fewer than two
  // beats timed since contact started.
  if (!maxOk || !fingerPresent || beatAvg <= 0) snprintf(big, sizeof(big), "--");
  else                                          snprintf(big, sizeof(big), "%d", beatAvg);
  u8g2.setFont(u8g2_font_logisoso42_tn);
  u8g2.drawStr((OLED_WIDTH - u8g2.getStrWidth(big)) / 2, 50, big);

  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr((OLED_WIDTH - u8g2.getStrWidth("BPM")) / 2, 63, "BPM");
  if (maxOk && !fingerPresent)      u8g2.drawStr(2, 63, "no finger");
  else if (maxOk && beatAvg <= 0)   u8g2.drawStr(2, 63, "measuring");

  u8g2.sendBuffer();
}
#endif  // DISPLAY_TYPE == DISPLAY_OLED

// ── Heart-rate screen (LCD) ──────────────────────────────────────────────────
// Mirrors the OLED HR screen: title + big BPM + a status hint.
#if DISPLAY_TYPE == DISPLAY_LCD1602
static void drawHrLcd() {
  char line[LCD_COLS + 1];
  lcdRow(0, "HEART RATE");

  if (LCD_ROWS < 3) {
    // 1602: reading and reason have to share the one remaining row.
    if (!maxOk) snprintf(line, sizeof(line), "-- BPM  sensor off");
    else if (!fingerPresent) snprintf(line, sizeof(line), "-- BPM  no finger");
    else if (beatAvg <= 0) snprintf(line, sizeof(line), "-- BPM  measuring");
    else snprintf(line, sizeof(line), "%d BPM  live", beatAvg);
    lcdRow(1, line);
    return;
  }

  // 2004: the reading gets its own row, and the two things that explain a
  // missing reading (no sensor / no finger) each get one too, so a dash is
  // never unexplained.
  if (demoHrActive())                        snprintf(line, sizeof(line), "%d BPM", demoHr());
  else if (maxOk && fingerPresent && beatAvg > 0) snprintf(line, sizeof(line), "%d BPM", beatAvg);
  else                                       snprintf(line, sizeof(line), "-- BPM");
  lcdRow(1, line);

  if (demoHrActive()) {
    // Never let a generated number read as a reading.
    lcdRow(2, "source: demo");
    lcdRow(3, "not a measurement");
    return;
  }

  snprintf(line, sizeof(line), "sensor: %s", maxOk ? "OK" : "MISSING");
  lcdRow(2, line);

  if (!maxOk)                 snprintf(line, sizeof(line), "finger: n/a");
  else if (!fingerPresent)    snprintf(line, sizeof(line), "finger: none");
  else if (beatAvg <= 0)      snprintf(line, sizeof(line), "finger: measuring");
  else                        snprintf(line, sizeof(line), "finger: detected");
  lcdRow(3, line);
}
#endif  // DISPLAY_TYPE == DISPLAY_LCD1602

// ── Steps screen ─────────────────────────────────────────────────────────────
#if DISPLAY_TYPE == DISPLAY_OLED
static void drawSteps() {
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(2, 7, "STEPS");
  u8g2.drawHLine(0, 10, OLED_WIDTH);

  char big[12];
  snprintf(big, sizeof(big), "%lu", (unsigned long)stepCount);
  u8g2.setFont(u8g2_font_logisoso42_tn);
  u8g2.drawStr((OLED_WIDTH - u8g2.getStrWidth(big)) / 2, 50, big);

  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr((OLED_WIDTH - u8g2.getStrWidth("steps")) / 2, 63, "steps");

  u8g2.sendBuffer();
}
#endif  // DISPLAY_TYPE == DISPLAY_OLED

// ── Steps screen (LCD) ───────────────────────────────────────────────────────
#if DISPLAY_TYPE == DISPLAY_LCD1602
static void drawStepsLcd() {
  char line[LCD_COLS + 1];
  lcdRow(0, "STEPS");

  if (LCD_ROWS < 3) {
    snprintf(line, sizeof(line), "%lu  total", (unsigned long)stepCount);
    lcdRow(1, line);
    return;
  }

  // 2004: count, a rough distance, and whether the counter can even run.
  snprintf(line, sizeof(line), "%lu  total", (unsigned long)stepCount);
  lcdRow(1, line);

  // Integer maths on purpose: AVR-style float printf is not linked in, and a
  // 0.76 m average stride is well inside the error of a wrist step counter
  // anyway. Metres = steps * 76 / 100.
  unsigned long metres = (unsigned long)stepCount * 76UL / 100UL;
  snprintf(line, sizeof(line), "~%lu m walked", metres);
  lcdRow(2, line);

  snprintf(line, sizeof(line), "IMU: %s", mpuOk ? "OK" : "MISSING");
  lcdRow(3, line);
}
#endif  // DISPLAY_TYPE == DISPLAY_LCD1602

// ── Motion screen (LCD) ──────────────────────────────────────────────────────
// Live accelerometer + gyroscope. `move` is the figure the step detector
// actually thresholds on (deviation from gravity), so watching it is the
// quickest way to tell whether the IMU is responding to being picked up.
#if DISPLAY_TYPE == DISPLAY_LCD1602
static void drawMotionLcd() {
  char line[LCD_COLS + 1];
  lcdRow(0, "MOTION");

  if (!mpuOk) {
    lcdRow(1, "IMU not connected");
    if (LCD_ROWS >= 3) lcdRow(2, "wire SDA->7 SCL->8");
    if (LCD_ROWS >= 4) lcdRow(3, "VCC->3V3  GND->GND");
    return;
  }

  if (LCD_ROWS < 3) {
    snprintf(line, sizeof(line), "g%.1f a%.1f", lastGyroRads, lastAccelMs2);
    lcdRow(1, line);
    return;
  }

  snprintf(line, sizeof(line), "accel %5.2f m/s2", lastAccelMs2);
  lcdRow(1, line);
  snprintf(line, sizeof(line), "move  %5.2f m/s2", lastDynMs2);
  lcdRow(2, line);
  snprintf(line, sizeof(line), "gyro  %5.2f rad/s", lastGyroRads);
  lcdRow(3, line);
}
#endif  // DISPLAY_TYPE == DISPLAY_LCD1602

// ── Sensor-status screen ─────────────────────────────────────────────────────
#if DISPLAY_TYPE == DISPLAY_OLED
static void drawStatus() {
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(2, 7, "SENSORS");
  u8g2.drawHLine(0, 10, OLED_WIDTH);

  u8g2.drawStr(2, 22, "Display: OK");
  u8g2.drawStr(2, 33, maxOk ? "MAX30105: OK" : "MAX30105: MISSING");
  u8g2.drawStr(2, 44, mpuOk ? "MPU6050:  OK" : "MPU6050:  MISSING");
  u8g2.drawStr(2, 55, battDetail());   // "4.06V 78% bat", or "Battery: not wired"

  u8g2.sendBuffer();
}
#endif  // DISPLAY_TYPE == DISPLAY_OLED

// ── Sensor-status screen (LCD) ───────────────────────────────────────────────
// Mirrors the OLED status screen: title + per-sensor lines + battery detail.
// A 1602 only has two rows, so the sensors share them (one per row).
#if DISPLAY_TYPE == DISPLAY_LCD1602
static void drawStatusLcd() {
  char line[LCD_COLS + 1];
  if (LCD_ROWS >= 3) {
    lcdRow(0, "SENSORS");
    lcdRow(1, maxOk ? "MAX30105: OK" : "MAX30105: MISSING");
    lcdRow(2, mpuOk ? "MPU6050:  OK" : "MPU6050:  MISSING");
    if (LCD_ROWS >= 4) { snprintf(line, sizeof(line), "%s", battDetail()); lcdRow(3, line); }
  } else {
    // 1602: sensors only, one per row.
    lcdRow(0, maxOk ? "MAX30105: OK" : "MAX30105: MISSING");
    lcdRow(1, mpuOk ? "MPU6050:  OK" : "MPU6050:  MISSING");
  }
}
#endif  // DISPLAY_TYPE == DISPLAY_LCD1602

// ── Sync / network screen ────────────────────────────────────────────────────
#if DISPLAY_TYPE == DISPLAY_OLED
static void drawSync() {
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(2, 7, "SYNC");
  u8g2.drawHLine(0, 10, OLED_WIDTH);

  char line[26];
  snprintf(line, sizeof(line), "wifi %s  %s", wifiConnected() ? "UP" : "off", wifiIp());
  u8g2.drawStr(2, 20, line);

  snprintf(line, sizeof(line), "queued %d   sent %lu",
           syncQueued(), (unsigned long)syncUploaded());
  u8g2.drawStr(2, 30, line);

  snprintf(line, sizeof(line), "last http %d", syncLastHttp());
  u8g2.drawStr(2, 40, line);

  snprintf(line, sizeof(line), "ssid %s", settings().ssid);
  u8g2.drawStr(2, 50, line);

  // The 5x7 font is 5 px per glyph, so the panel fits 25 characters at x=2.
  // The old hint here was 32 characters (160 px): it was clipped mid-glyph and
  // "= mute" never appeared at all. Caught by screenshotting the simulator.
  // The divider also moved up 2 px — at y=57 it collided with the ascenders of
  // a baseline-63 line, which occupies y=57..63.
  u8g2.drawHLine(0, 55, OLED_WIDTH);
  u8g2.drawStr(2, 63, "A hold=home  Bx2=mute");
  u8g2.sendBuffer();
}
#endif  // DISPLAY_TYPE == DISPLAY_OLED

// ── Sync / network screen (LCD) ──────────────────────────────────────────────
// Mirrors the OLED sync screen: wifi state + IP, queued/sent counts, last HTTP
// result and the SSID.
#if DISPLAY_TYPE == DISPLAY_LCD1602
static void drawSyncLcd() {
  char line[LCD_COLS + 1];

  if (LCD_ROWS < 3) {
    // 1602: two rows — wifi state + queue.
    snprintf(line, sizeof(line), "wifi %s %s", wifiConnected() ? "UP" : "off", wifiIp());
    lcdRow(0, line);
    snprintf(line, sizeof(line), "q %d sent %lu", syncQueued(), (unsigned long)syncUploaded());
    lcdRow(1, line);
    return;
  }

  lcdRow(0, "SYNC");
  snprintf(line, sizeof(line), "wifi %s  %s", wifiConnected() ? "UP" : "off", wifiIp());
  lcdRow(1, line);

  snprintf(line, sizeof(line), "queued %d  sent %lu", syncQueued(), (unsigned long)syncUploaded());
  lcdRow(2, line);

  if (LCD_ROWS >= 4) {
    snprintf(line, sizeof(line), "http %d  ssid %s", syncLastHttp(), settings().ssid);
    lcdRow(3, line);
  }
}
#endif  // DISPLAY_TYPE == DISPLAY_LCD1602

// ── Pairing portal screen (shown whenever the watch is not paired) ───────────
#if DISPLAY_TYPE == DISPLAY_OLED
static void drawPair() {
  u8g2.clearBuffer();

  // Token view: show the full "fit_…" token, wrapped across a few 5x7 lines.
  if (pairShowToken) {
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(2, 8, "DEVICE TOKEN");
    u8g2.drawHLine(0, 11, OLED_WIDTH);

    const char *tok = settings().token;
    if (!tok[0]) tok = "(none set)";
    size_t len = strlen(tok);
    for (size_t i = 0, ln = 0; i < len && ln < 5; i += 18, ln++) {
      char tmp[19];
      size_t n = (len - i) < 18 ? (len - i) : 18;
      memcpy(tmp, tok + i, n);
      tmp[n] = 0;
      u8g2.drawStr(2, 20 + ln * 8, tmp);
    }

    u8g2.drawHLine(0, 60, OLED_WIDTH);
    u8g2.drawStr(2, 63, "press A/B to hide");
    u8g2.sendBuffer();
    return;
  }

  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(2, 8, "PAIR THIS WATCH");
  u8g2.drawHLine(0, 11, OLED_WIDTH);

  u8g2.drawStr(2, 22, "1 Phone wifi:");
  u8g2.drawStr(2, 31, pairingSsid());
  u8g2.drawStr(2, 42, "2 Browser 192.168.4.1");
  u8g2.drawStr(2, 53, "3 Enter token fit_...");

  u8g2.drawHLine(0, 60, OLED_WIDTH);
  u8g2.drawStr(2, 63, pairingBusy() ? "connecting..." : "waiting...");
  u8g2.sendBuffer();
}
#endif  // DISPLAY_TYPE == DISPLAY_OLED

// ── Pairing portal screen (LCD) ──────────────────────────────────────────────
// Mirrors the OLED pairing screen: instructions in order + live connection state.
#if DISPLAY_TYPE == DISPLAY_LCD1602
static void drawPairLcd() {
  char line[LCD_COLS + 1];

  if (pairShowToken) {
    lcdRow(0, "DEVICE TOKEN");
    const char *tok = settings().token;
    if (!tok[0]) tok = "(none set)";
    snprintf(line, sizeof(line), "%s", tok);
    lcdRow(1, line);
    return;
  }

  if (LCD_ROWS < 3) {
    // 1602: two rows — SSID + live state.
    snprintf(line, sizeof(line), "wifi %s", pairingSsid());
    lcdRow(0, line);
    lcdRow(1, pairingBusy() ? "connecting..." : "browser 192.168.4.1");
    return;
  }

  lcdRow(0, "PAIR THIS WATCH");
  snprintf(line, sizeof(line), "wifi: %s", pairingSsid());
  lcdRow(1, line);
  if (LCD_ROWS >= 3) lcdRow(2, "browser 192.168.4.1");
  if (LCD_ROWS >= 4) lcdRow(3, pairingBusy() ? "connecting..." : "enter token fit_...");
}
#endif  // DISPLAY_TYPE == DISPLAY_LCD1602

// ── Display power ────────────────────────────────────────────────────────────
// The OLED is the largest continuous load after the radio (~12 mA), and a watch
// spends nearly all of its life unobserved — so it blanks itself after
// DISPLAY_TIMEOUT_MS and on a double-press of button B.
//
// This is display-only. HR sampling, step counting, the battery monitor, BLE and
// cloud sync all keep running while the panel is dark; see loop(), where only
// the draw step is gated.
// State lives higher up the file — see the "Display power state" block.
static void drawScreen();               // defined below; used to repaint on wake

static void enterStandby(const char *why) {
  if (standby) return;
  standby = true;
  dispPower(false);                 // SSD1306 display OFF / LCD backlight OFF
  DBG("display off (%s) — vitals, sync and BLE keep running", why);
}

static void exitStandby() {
  lastActivityMs = millis();
  if (!standby) return;
  standby = false;
  // Wake with the display ON command — no full begin() re-init flash here. The
  // periodic displaySelfHeal() already re-syncs a corrupt panel if one develops.
  dispPower(true);                  // SSD1306 display ON / LCD backlight ON
  redrawNow = true;
  DBG("display on");
}

// ── Navigation ───────────────────────────────────────────────────────────────
static void handleButtons() {
  uint32_t now = millis();
  updateButton(btnA, PIN_BTN_A, now);
  updateButton(btnB, PIN_BTN_B, now);

  if (screen == SCREEN_BOOT) return;

  // Any interaction counts as activity and restarts the blank timeout.
  if (btnA.tapEdge || btnB.tapEdge || btnA.holdEdge || btnB.holdEdge)
    lastActivityMs = now;

  // First press on a dark panel just wakes it — it must NOT also navigate.
  // Otherwise reaching for the watch to check the time silently changes screen.
  if (standby) {
    btnA.tapEdge = btnB.tapEdge = false;
    btnA.holdEdge = btnB.holdEdge = false;
    exitStandby();
    return;
  }

  // Hold button A (GPIO4) → back to the home screen (unchanged quick-hold).
  if (btnA.holdEdge) {
    btnA.holdEdge = btnB.holdEdge = false;
    btnA.tapEdge = btnB.tapEdge = false;
    goScreen(SCREEN_HOME);
    return;
  }
  btnB.holdEdge = false;   // B holds are not used anymore

  // GPIO5 double-press: two taps inside BTN_DOUBLE_TAP_MS toggle the screen
  // off/on (vitals keep running). A single press returns to the home screen —
  // but wait BTN_DOUBLE_TAP_MS so a "double" doesn't first hop home: the first
  // tap records a timestamp, and its single-press action fires only if no
  // second tap arrives inside the window.
  static uint32_t bFirstTapMs = 0;
  static uint32_t bCooldownMs = 0;    // ignore B taps right after a toggle
  if (btnB.tapEdge) {
    btnB.tapEdge = false;
    btnA.tapEdge = false;
    uint32_t nowTap = millis();

    if (nowTap < bCooldownMs) return;   // residual press from the gesture

    if (bFirstTapMs && (int32_t)(nowTap - bFirstTapMs) <= BTN_DOUBLE_TAP_MS) {
      bFirstTapMs = 0;                    // ── DOUBLE press → toggle screen
      bCooldownMs = nowTap + BTN_DOUBLE_TAP_MS;  // swallow the rest of the gesture
      if (standby) exitStandby();
      else         enterStandby("double-tap");
      return;
    }
    bFirstTapMs = nowTap;                 // ── first trip of a possible double
  }

  // Single-press action for B fires when the window expires with no second tap.
#if DEMO_MODE
  // Demo unit: everything the sensors can actually show. SYNC and PAIR are the
  // only screens dropped -- both describe a network this build does not have.
  static const Screen ORDER[] = {SCREEN_HOME, SCREEN_HR, SCREEN_STEPS,
                                 SCREEN_MOTION, SCREEN_STATUS};
#else
  static const Screen ORDER[] = {SCREEN_HOME, SCREEN_HR, SCREEN_STEPS, SCREEN_SYNC, SCREEN_STATUS};
#endif
  const int N = (int)(sizeof(ORDER) / sizeof(ORDER[0]));

  if (btnA.tapEdge) {
    btnA.tapEdge = false;
    bFirstTapMs = 0;                      // A keeps pushing from pending-B too
    for (int i = 0; i < N; i++) {
      if (screen == ORDER[i]) { goScreen(ORDER[(i + N - 1) % N]); return; }
    }
    goScreen(SCREEN_HOME);
    return;
  }
  if (bFirstTapMs && (int32_t)(millis() - bFirstTapMs) > BTN_DOUBLE_TAP_MS) {
    bFirstTapMs = 0;
#if !DEMO_MODE
    if (screen == SCREEN_PAIR) { pairShowToken = !pairShowToken; return; }
#endif
    goScreen(SCREEN_HOME);                // ── single press → home
  }
}

void setup() {
  // Clock down BEFORE Serial.begin(), so the UART divider is computed against
  // the frequency we are actually going to run at. Doing it afterwards garbles
  // the console. 80 MHz is the floor for the WiFi radio, and this workload — a
  // 1 Hz UI and two slow I2C sensors — is nowhere near compute-bound. The C3's
  // APB clock stays at 80 MHz regardless, so I2C timing is untouched.
  setCpuFrequencyMhz(CPU_FREQ_MHZ);

  Serial.begin(115200);
  delay(200);
  settingsLoad();

  pinMode(PIN_BTN_A, BTN_ACTIVE_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);
  pinMode(PIN_BTN_B, BTN_ACTIVE_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);

  powerBegin();   // battery divider — safe no-op when PIN_BATT_ADC is -1

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(I2C_CLOCK_HZ);
  i2cScanLog("boot");   // one line per device actually on the wire
  dispBegin();   // SSD1306 (U8g2) or HD44780 (LiquidCrystal_I2C) — see config.h

#if SIM_BUILD
  // No MAX30105 part exists in Wokwi, so there is no device to configure —
  // simIR() feeds the real beat-detection path instead. See sim.h.
  maxOk = simMaxBegin();
  DBG("SIM: synthetic MAX30105 @ %d bpm", g_simBpm);
#else
  maxOk = particleSensor.begin(Wire, I2C_CLOCK_HZ, MAX30105_ADDR);
  if (maxOk) {
    // powerLevel, sampleAverage, ledMode(2=Red+IR), sampleRate, pulseWidth, adcRange
    particleSensor.setup(MAX_LED_IR, 4, 2, 400, 411, 4096);
    // Kill the RED LED outright. Mode 2 pulses Red and IR, but this firmware
    // only ever reads the IR channel (getIR()) — it computes heart rate, not
    // SpO2 — so Red was burning ~6 mA continuously to produce a number nothing
    // reads. Turn it back on only if SpO2 is ever implemented.
    maxSetLeds(true);
    DBG("MAX30105 init OK (addr 0x%02X, red off)", MAX30105_ADDR);
  } else {
    Serial.println("[watch] MAX30105 NOT found on I2C bus");
  }
#endif

  mpuOk = mpu.begin(MPU6050_ADDR, &Wire);
  if (mpuOk) {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    DBG("MPU6050 init OK (addr 0x%02X)", MPU6050_ADDR);
  } else {
    Serial.println("[watch] MPU6050 NOT found on I2C bus");
  }

  // Sensor init above is hostile to the display on a custom pin pair:
  //   - MAX30105::begin()  -> _i2cPort->begin()   = Wire.begin() with NO args
  //   - Adafruit I2CDevice  -> _wire->begin()     = Wire.begin() with NO args
  // On the ESP32-C3, Wire.begin() with no arguments resets the bus to the
  // board's DEFAULT pins (8/9). Both run above, so the shared bus has silently
  // moved off PIN_I2C_SDA/SCL by the time the boot loading bar draws — the panel
  // gets nothing and stays dark (an LCD backlight latches on, but no text
  // arrives). Re-assert the pins AND the clock so the display is driven where
  // the wiring actually is, then re-init the panel: on the wrong pins its own
  // controller setup may have been half-written, and re-running it is cheap.
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(I2C_CLOCK_HZ);
  dispReinit();

  // Animate the loading bar over BOOT_BAR_MS, then hand off to the right start.
  const int steps = 26;
  for (int i = 0; i <= steps; i++) {
    drawLoading((uint8_t)(100L * i / steps));
    delay(BOOT_BAR_MS / steps);
  }

#if SIM_BUILD && SIM_WIFI
  // Wokwi's virtual AP is open and always present. Preload it (without touching
  // `paired`, so the pairing state machine still runs for real) so the sim can
  // reach wifiTick()/syncTick() without a captive-portal round trip.
  if (!settings().ssid[0]) {
    strlcpy(settingsMut().ssid, SIM_WIFI_SSID, sizeof(settingsMut().ssid));
    strlcpy(settingsMut().pass, SIM_WIFI_PASS, sizeof(settingsMut().pass));
    settingsSave();
    DBG("SIM: preloaded wifi '%s'", SIM_WIFI_SSID);
  }
#endif

  if (DEMO_MODE) {
    // Demo unit: nothing is paired, nothing connects, nothing is transmitted.
    // No wifiSetup(), no syncBegin(), no portal, and BLE_ENABLE is forced 0 in
    // config.h so bleStart() below never runs either.
    screen = SCREEN_HOME;
    Serial.println("[watch] DEMO MODE: offline, radios off, clock + HR only");
    if (DEMO_HR_SYNTH) Serial.println("[watch] DEMO MODE: synthetic HR when no MAX30105 fitted (not a measurement)");
  } else if (DISPLAY_RADIO_TEST) {
    // Diagnostic build: radios never come up; force the home screen so the
    // only thing touching the display is the 5 fps repaint + self-heal.
    screen = SCREEN_HOME;
    DBG("RADIO TEST: radios off, home screen only");
  } else if (settings().paired && settings().ssid[0]) {
    wifiSetup(settings().ssid, settings().pass);
    syncBegin();
    screen = SCREEN_HOME;
    DBG("paired -> HOME (wifi %s)", settings().ssid);
  } else {
    // Pairing is NOT required. An unpaired watch boots straight into the full
    // working UI (clock, HR, steps, status) with the radio idle; the app can
    // still pair it later over BLE (netApply flips the paired flag).
    screen = SCREEN_HOME;
    DBG("unpaired -> HOME (standalone, radio off)");
  }

  // NTP is a network service, so a demo unit skips it and free-runs its clock
  // from millis(). Nothing on screen depends on wall-clock accuracy.
  if (!DEMO_MODE) clockBegin();

  if (BLE_ENABLE && !DISPLAY_RADIO_TEST) bleStart();   // BLE is always on — the controller link to phone/web

  bootStartMs = millis();
  lastActivityMs = millis();   // start the blank timeout from a lit screen
  Serial.printf("[watch] Boot complete (cpu %u MHz, display timeout %d ms)\n",
                (unsigned)getCpuFrequencyMhz(), DISPLAY_TIMEOUT_MS);
}

// ── Serial command console (bring-up aid) ────────────────────────────────────
// drawScreen() is defined below this point; the `d` command repaints after
// recovering a panel, so declare it explicitly rather than relying on the
// Arduino builder's auto-prototypes (which skip static functions).
static void drawScreen();

static void handleSerialCmd() {
  static String buf;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      buf.trim();
      if (buf.length()) {
        Serial.print("[watch] cmd > "); Serial.println(buf);
        if (buf == "h") {
          Serial.println("  p  = status line\n  s  = force sync now\n  t  = print device token\n  i2c = scan the I2C bus\n  d  = display state (+ retry a missing panel)\n  r  = reboot\n  clear = wipe pairing + reboot to portal");
        } else if (buf == "p") {
          logWatch();
        } else if (buf == "m") {
          // Live motion readout. The MOTION screen shows the same three
          // figures, but reading them over serial is how you check the IMU is
          // actually responding without having to watch the panel.
          Serial.printf("[watch] imu=%d accel=%.2f move=%.2f gyro=%.2f steps=%lu
",
                        (int)mpuOk, lastAccelMs2, lastDynMs2, lastGyroRads,
                        (unsigned long)stepCount);
        } else if (buf == "test") {
#if DISPLAY_TYPE == DISPLAY_LCD1602
          // Fill every cell with the HD44780's solid-block glyph (0xFF). This is
          // the highest-contrast thing the panel can show, so it is what to turn
          // the contrast trimmer against: too low and the blocks are invisible,
          // too high and the UNLIT cells darken too. Text is clearest just below
          // the point where the blank rows start to shadow.
          char blocks[LCD_COLS + 1];
          memset(blocks, 0xFF, LCD_COLS);
          blocks[LCD_COLS] = 0;
          for (uint8_t r = 0; r < LCD_ROWS; r++) lcdRow(r, (r % 2) ? "" : blocks);
          Serial.println("[watch] contrast pattern: alternating solid/blank rows.");
          Serial.println("[watch] turn the blue trimmer until the solid rows are dark");
          Serial.println("[watch] and the blank rows stay clear. Any key redraws.");
#else
          Serial.println("[watch] test pattern is LCD-only");
#endif
        } else if (buf == "i2c") {
          i2cScanLog("manual");
        } else if (buf == "d") {
#if DISPLAY_TYPE == DISPLAY_LCD1602
          Serial.printf("[watch] LCD %dx%d ok=%d addr=0x%02X\n",
                        LCD_COLS, LCD_ROWS, (int)lcdOk, lcdAddr);
          if (!lcdOk) { if (lcdTryBegin()) { Serial.printf("[watch] now up at 0x%02X\n", lcdAddr); drawScreen(); } else lcdReportMissing(); }
#else
          Serial.printf("[watch] OLED %dx%d addr=0x%02X ack=%d\n",
                        OLED_WIDTH, OLED_HEIGHT, OLED_ADDR, (int)i2cAck(OLED_ADDR));
#endif
        } else if (buf == "t") {
          const char *tok = settings().token;
          Serial.printf("[watch] token: %s\n", tok[0] ? tok : "(none set)");
        } else if (buf == "s") {
          syncWant();
        } else if (buf == "r") {
          ESP.restart();
        } else if (buf == "clear") {
          settingsClear();
          delay(300);
          ESP.restart();
#if SIM_BUILD
        // Simulator-only test hooks. A Wokwi scenario drives these with
        // `write-serial`, so an automated test can assert that a commanded
        // 140 bpm actually comes back out of the beat detector as ~140.
        } else if (buf.startsWith("bpm ")) {
          g_simBpm = buf.substring(4).toInt();
          Serial.printf("[sim] bpm -> %d\n", g_simBpm);
        } else if (buf.startsWith("finger ")) {
          g_simFingerForce = buf.substring(7).toInt();
          Serial.printf("[sim] finger -> %d\n", g_simFingerForce);
        } else if (buf == "hr") {
          // Wokwi's `wait-serial` is a plain substring match, so a scenario
          // cannot express "between 110 and 129". The verdict is computed here
          // instead and the scenario just asserts on INRANGE — which keeps the
          // tolerance in one place rather than smeared across the yaml files.
          int want = g_simBpm;
          int err = beatAvg - want;
          if (err < 0) err = -err;
          int tol = want / 8;                 // +/-12.5% of the commanded rate
          if (tol < 6) tol = 6;               // floor, for slow pulses
          bool ok = fingerPresent && beatAvg > 0 && err <= tol;
          Serial.printf("[sim] hr=%d want=%d finger=%d filled=%d %s\n",
                        beatAvg, want, (int)fingerPresent, (int)rateFilled,
                        ok ? "INRANGE" : "OUTOFRANGE");
        } else if (buf == "screen") {
          Serial.printf("[sim] screen=%d standby=%d rot=%d\n",
                        (int)screen, (int)standby, curRotation);
        } else if (buf == "batt") {
          Serial.printf("[sim] batt valid=%d mv=%d pct=%d usb=%d label=%s\n",
                        (int)battValid(), battMilliVolts(), battPercent(),
                        (int)usbPresent(), battLabel());
        } else if (buf == "steps reset") {
          stepCount = 0;            // deterministic baseline for the motion test
          aboveStepThreshold = false;
          Serial.printf("[sim] steps=%lu\n", (unsigned long)stepCount);
        } else if (buf == "steps") {
          Serial.printf("[sim] steps=%lu\n", (unsigned long)stepCount);
#endif
        } else {
          Serial.println("  ? (try h for help)");
        }
      }
      buf = "";
    } else {
      buf += c;
    }
  }
}

#if DEMO_MODE && DEMO_SCREEN_MS > 0
// Step to the next screen in the demo rotation. Deliberately separate from the
// button handler's ORDER walk: this one moves forward and never needs to know
// about holds, taps or the pair screen.
// Build the rotation from what actually answered on the bus. Hard-coding it
// meant a unit with no MAX30105 spent a fifth of its time showing a heart-rate
// screen full of dashes; now a screen only joins the cycle once the sensor
// behind it is present. Rebuilt on every advance, so a sensor plugged in while
// running is picked up on the next step without a reset.
static int buildDemoOrder(Screen *out) {
  int n = 0;
  out[n++] = SCREEN_HOME;                     // clock: always available
  if (maxOk) out[n++] = SCREEN_HR;            // needs the MAX30105
  if (mpuOk) {
    out[n++] = SCREEN_STEPS;                  // both need the MPU6050
    out[n++] = SCREEN_MOTION;
  }
  out[n++] = SCREEN_STATUS;                   // always: it is what reports the gaps
  return n;
}

static void nextScreenAuto() {
  Screen order[5];
  int n = buildDemoOrder(order);
  for (int i = 0; i < n; i++) {
    if (screen == order[i]) { goScreen(order[(i + 1) % n]); return; }
  }
  goScreen(order[0]);   // current screen just left the rotation (sensor removed)
}
#endif

// Re-probe sensors that were absent at boot, so wiring one up while the watch is
// running just works -- the same way the display already recovers.
//
// Both drivers' begin() calls Wire.begin() with NO arguments, which on the C3
// resets the bus to the default GPIO 8/9 and would silently take the display
// down with it. That is why the pins are re-asserted immediately afterwards.
// The cheap i2cAck() gate keeps this from costing a full driver probe every
// few seconds when nothing is plugged in.
static void sensorRecheck() {
#if !SIM_BUILD
  if (maxOk && mpuOk) return;

  if (!maxOk && i2cAck(MAX30105_ADDR)) {
    if (particleSensor.begin(Wire, I2C_CLOCK_HZ, MAX30105_ADDR)) {
      particleSensor.setup(MAX_LED_IR, 4, 2, 400, 411, 4096);
      maxSetLeds(true);
      maxOk = true;
      Serial.println("[watch] MAX30105 appeared - heart rate live");
    }
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(I2C_CLOCK_HZ);
  }

  if (!mpuOk && i2cAck(MPU6050_ADDR)) {
    if (mpu.begin(MPU6050_ADDR, &Wire)) {
      mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
      mpu.setGyroRange(MPU6050_RANGE_500_DEG);
      mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
      mpuOk = true;
      Serial.println("[watch] MPU6050 appeared - motion live");
    }
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(I2C_CLOCK_HZ);
  }
#endif
}

// Repaint whichever screen is active. Split out so the self-heal re-init below
// can repaint the current frame immediately instead of leaving a blank flash.
static void drawScreen() {
  Screen s = screen;
#if DISPLAY_TYPE == DISPLAY_LCD1602
  switch (s) {
    case SCREEN_HOME:   drawHomeLcd();   break;
    case SCREEN_HR:     drawHrLcd();     break;
    case SCREEN_STEPS:  drawStepsLcd();  break;
    case SCREEN_SYNC:   drawSyncLcd();   break;
    case SCREEN_STATUS: drawStatusLcd(); break;
    case SCREEN_PAIR:   drawPairLcd();   break;
    case SCREEN_MOTION: drawMotionLcd(); break;
    default: break;
  }
#else
  switch (s) {
    case SCREEN_HOME:   drawHome();   break;
    case SCREEN_HR:     drawHR();     break;
    case SCREEN_STEPS:  drawSteps();  break;
    case SCREEN_SYNC:   drawSync();   break;
    case SCREEN_STATUS: drawStatus(); break;
    case SCREEN_PAIR:   drawPair();   break;
    case SCREEN_MOTION: drawStatus();  break;   // no dedicated OLED motion screen yet
    default: break;
  }
#endif
}

// Self-heal the display. At 100 kHz a full 128x64 frame ties up the I2C bus for
// ~80 ms, and a radio interrupt landing mid-frame can leave the SSD1306's state
// machine desynced — it then stays corrupt (the "screen bugging out" symptom)
// until re-initialised. There is no reliable per-transaction error flag on this
// core, so instead we periodically re-run begin() — the init sequence forces the
// controller back into sync — and immediately repaint so it is near-invisible.
static void displaySelfHeal() {
  static uint32_t lastHealMs = 0;
  uint32_t now = millis();
  if (now - lastHealMs < DISPLAY_SELF_HEAL_MS) return;
  lastHealMs = now;
#if DISPLAY_TYPE == DISPLAY_OLED
  u8g2.begin();
#else
  // LCD: HD44780 has no address-desync state to recover from, so a healthy panel
  // is left alone — re-sending the init sequence every 5 s would just flicker it.
  // An ABSENT panel is re-probed instead, which is what makes a wiring or power
  // fix take effect on its own: plug the backpack into 5V and it comes up within
  // one heal interval, no reset and no reflash.
  if (!lcdOk) {
    if (lcdTryBegin()) {
      Serial.printf("[watch] LCD1602 appeared at 0x%02X - display live\n", lcdAddr);
    } else {
      return;   // nothing to repaint into
    }
  }
#endif
  drawScreen();
}

void loop() {
  // Screen-off is a DISPLAY mute, nothing more. Everything below runs whether
  // the panel is lit or dark; only the draw step at the bottom is gated.
  //
  // It used to `return` here, which meant blanking the screen also stopped the
  // serial console, the BLE state beacon, WiFi upkeep AND cloud sync — so a
  // muted watch quietly banked readings forever and never uploaded them. That
  // was survivable when the only way to blank the panel was a deliberate
  // double-tap; with an automatic timeout it would have been a data-loss bug.
  pollHeartRate();    // sampled every iteration — beat timing needs the resolution
  pollAccelStep();
  handleButtons();
  handleSerialCmd();

  uint32_t now = millis();

  powerTick();                       // battery sample (throttled to BATT_INTERVAL_MS)

#if !DEMO_MODE
  netTick();                         // BLE-triggered "apply" pair verification
  if (BLE_ENABLE) bleTick();         // keep the STATE beacon fresh for connected phones
#endif

  // Networking: pairing portal when unpaired, else background wifi + sync.
  // Skipped entirely in the DISPLAY_RADIO_TEST diagnostic build, and shed once
  // the cell is critical — WiFi TX peaks at ~300 mA, which is exactly what
  // collapses a nearly-empty Li-ion into a brownout reset. Readings keep
  // queueing in RAM and flush on the next charge.
  static bool loggedCrit = false;
  bool crit = battCritical();
  if (crit && !loggedCrit) { loggedCrit = true; DBG("battery critical -> radio shed"); }
  if (!crit) loggedCrit = false;

  if (DEMO_MODE) {
    // Demo unit: no radio is ever brought up, so there is nothing to tick,
    // nothing to shed on low battery, and nothing to sync. This is the single
    // guarantee that a demo build transmits nothing.
  } else if (crit) {
    wifiOff();
  } else if (settings().paired && settings().ssid[0] && !DISPLAY_RADIO_TEST) {
    wifiTick();
    syncTick(clockNow(), beatAvg, stepCount);
  } else if (!DISPLAY_RADIO_TEST && !settings().paired && pairingActive()) {
    pairingLoop();   // keep an already-started portal alive (BLE-apply grace)
  }

  {
    // Hot-plug check for sensors, on the display self-heal cadence.
    static uint32_t lastSensorCheckMs = 0;
    if (now - lastSensorCheckMs >= (uint32_t)DISPLAY_SELF_HEAL_MS) {
      lastSensorCheckMs = now;
      sensorRecheck();
    }
  }

#if DEMO_MODE && DEMO_SCREEN_MS > 0
  // Auto-advance. A demo unit has no buttons, so without this it would sit on
  // the home screen forever and none of the sensor screens would be seen.
  {
    static uint32_t lastAdvanceMs = 0;
    if (now - lastAdvanceMs >= (uint32_t)DEMO_SCREEN_MS) {
      lastAdvanceMs = now;
      if (screen != SCREEN_BOOT) nextScreenAuto();
    }
  }
#endif

  // Blank the panel once it has been ignored for long enough.
  if (DISPLAY_TIMEOUT_MS > 0 && !standby &&
      (now - lastActivityMs) >= (uint32_t)DISPLAY_TIMEOUT_MS) {
    enterStandby("timeout");
  }

  // Everything below touches the panel, so it is skipped while dark. That
  // matters for displaySelfHeal() in particular: it calls u8g2.begin(), and the
  // SSD1306 init sequence ends with the display ON — running it during standby
  // would light the screen back up every DISPLAY_SELF_HEAL_MS.
  if (standby) return;

  displaySelfHeal();

  // Repaint at DRAW_INTERVAL_MS (1 Hz — nothing on screen changes faster than
  // the seconds counter), or immediately when something the user did changed
  // it. The old unconditional 5 fps spent ~45% of every second inside a
  // blocking 90 ms I2C frame write, which is what starved button sampling and
  // heart-rate detection.
  static uint32_t lastDrawMs = 0;
  if (redrawNow || now - lastDrawMs >= DRAW_INTERVAL_MS) {
    redrawNow = false;
    lastDrawMs = now;
    drawScreen();
  }
}
