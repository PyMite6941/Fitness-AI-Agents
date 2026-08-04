/*
 * FitnessAI Watch — main program
 * Board:   ESP32-C3 SuperMini
 * Display: SSD1306 128x64 OLED (I2C), rendered with U8g2
 * Sensors: MAX30105 heart rate/SpO2 (SparkFun MAX3010x lib), MPU6050 accel/gyro
 *          (Adafruit_MPU6050 lib) — both on the same I2C bus as the OLED.
 *
 * Scope: screen + program skeleton + heart rate + step counting + buttons.
 *   boot  →  loading screen (animated bar)  →  home screen (live)
 *   Buttons: A (GPIO4) = Back/prev, B (GPIO5) = Select/next; hold = home.
 * All wiring is in config.h. Libraries: "U8g2" by oliver, "SparkFun MAX3010x
 * Pulse and Proximity Sensor Library", "Adafruit MPU6050" (+ Adafruit Sensor,
 * Adafruit BusIO).
 */

#include <Wire.h>
#include <U8g2lib.h>
#include <MAX30105.h>
#include <heartRate.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include "config.h"
#include "net.h"   // settings, clock, WiFi, pairing portal, sync
#include "ble.h"   // BLE control peripheral (pair / time / commands)

// Serial diagnostics: toggling NET_DEBUG to 0 silences the chatty [net]/[watch]
// logs, leaving only errors and one-liners. Leave on while bring-up is ongoing.
#define DBG(fmt, ...) do { if (DEBUG_SERIAL) Serial.printf("[watch] " fmt "\n", ##__VA_ARGS__); } while (0)

// Full-buffer, hardware-I2C SSD1306. This 0.96" panel needs the standard NONAME
// init. (ALT0 made the sparse measurement pattern look OK but interleaves the
// rows with real text -> overlapping lines.) See OLED_INIT_ALT0 in config.h.
#if OLED_INIT_ALT0
U8G2_SSD1306_128X64_ALT0_F_HW_I2C u8g2(U8G2_R0, /*reset=*/U8X8_PIN_NONE);
#else
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /*reset=*/U8X8_PIN_NONE);
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
static const u8g2_cb_t *ROT_CBS[4] = {U8G2_R0, U8G2_R1, U8G2_R2, U8G2_R3};

static void applyRotation(int r) {
  if (r == curRotation) return;
  curRotation = r;
  u8g2.setDisplayRotation(ROT_CBS[r]);
  DBG("orientation -> R%d", r);
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
static const uint32_t BTN_DEBOUNCE_MS = 50;    // stable read before trusting a level change
static const uint32_t BTN_HOLD_MS    = 600;    // press longer than this counts as a HOLD

// struct ButtonState is declared in config.h — see the note there about the
// Arduino builder's auto-generated prototypes.
static ButtonState btnA = {false, false, 0, 0, false, false, false};
static ButtonState btnB = {false, false, 0, 0, false, false, false};

static void updateButton(ButtonState &b, int pin, uint32_t now) {
  bool raw = digitalRead(pin) == LOW;   // active-low: pressed = LOW

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

static void goScreen(Screen s) {
  screen = s;
  bootStartMs = millis();
}

// ── Loading screen ───────────────────────────────────────────────────────────
static void drawLoading(uint8_t pct) {
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

// ── Sensor polling ───────────────────────────────────────────────────────────
// Sampled every loop() iteration (no throttling) — the beat-detection algorithm
// needs frequent IR samples to time pulses accurately.
static void pollHeartRate() {
  if (!maxOk) return;

  long irValue = particleSensor.getIR();
  bool nowPresent = irValue > 50000;   // low IR = no finger on the sensor

  // Finger just left → drop every derived value rather than holding the last BPM.
  if (fingerPresent && !nowPresent) resetHeartRate();
  fingerPresent = nowPresent;

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

  if (dynamic > STEP_THRESHOLD_MS2) {
    if (!aboveStepThreshold && (nowMs - lastStepMs) > STEP_DEBOUNCE_MS) {
      stepCount++;
      lastStepMs = nowMs;
    }
    aboveStepThreshold = true;
  } else {
    aboveStepThreshold = false;
  }
}

// ── Home screen ──────────────────────────────────────────────────────────────
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
  const char *batt = "USB";
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

// ── Heart-rate screen ────────────────────────────────────────────────────────
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

// ── Steps screen ─────────────────────────────────────────────────────────────
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

// ── Sensor-status screen ─────────────────────────────────────────────────────
static void drawStatus() {
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(2, 7, "SENSORS");
  u8g2.drawHLine(0, 10, OLED_WIDTH);

  u8g2.drawStr(2, 22, "Display: OK");
  u8g2.drawStr(2, 33, maxOk ? "MAX30105: OK" : "MAX30105: MISSING");
  u8g2.drawStr(2, 44, mpuOk ? "MPU6050:  OK" : "MPU6050:  MISSING");
  u8g2.drawStr(2, 55, "Buttons: A/Back  B/Next");

  u8g2.sendBuffer();
}

// ── Sync / network screen ────────────────────────────────────────────────────
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

  u8g2.drawHLine(0, 57, OLED_WIDTH);
  u8g2.drawStr(2, 63, "HOLD = re-pair");
  u8g2.sendBuffer();
}

// ── Pairing portal screen (shown whenever the watch is not paired) ───────────
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

// ── Navigation ───────────────────────────────────────────────────────────────
static void handleButtons() {
  uint32_t now = millis();
  updateButton(btnA, PIN_BTN_A, now);
  updateButton(btnB, PIN_BTN_B, now);

  if (screen == SCREEN_BOOT) return;

  // Hold either button → back to the home screen. Exception: on the SYNC screen,
  // hold clears the pairing and reboots into the setup portal (re-pair).
  if (btnA.holdEdge || btnB.holdEdge) {
    btnA.holdEdge = btnB.holdEdge = false;
    btnA.tapEdge = btnB.tapEdge = false;
    if (screen == SCREEN_SYNC) {
      settingsClear();
      DBG("pairing cleared - rebooting to setup portal");
      delay(150);
      ESP.restart();
      return;
    }
    goScreen(SCREEN_HOME);
    return;
  }

  // On the pairing screen, taps don't navigate — they toggle between the
  // instructions and the full device token (either button works).
  if (screen == SCREEN_PAIR && (btnB.tapEdge || btnA.tapEdge)) {
    btnB.tapEdge = btnA.tapEdge = false;
    pairShowToken = !pairShowToken;
    return;
  }

  // Tap (fired on release): B = next screen, A = previous (wrap around the list).
  static const Screen ORDER[] = {SCREEN_HOME, SCREEN_HR, SCREEN_STEPS, SCREEN_SYNC, SCREEN_STATUS};
  const int N = (int)(sizeof(ORDER) / sizeof(ORDER[0]));

  if (btnB.tapEdge) {
    btnB.tapEdge = false;
    btnA.tapEdge = false;
    for (int i = 0; i < N; i++) {
      if (screen == ORDER[i]) { goScreen(ORDER[(i + 1) % N]); return; }
    }
    goScreen(SCREEN_HOME);
  } else if (btnA.tapEdge) {
    btnA.tapEdge = false;
    btnB.tapEdge = false;
    for (int i = 0; i < N; i++) {
      if (screen == ORDER[i]) { goScreen(ORDER[(i + N - 1) % N]); return; }
    }
    goScreen(SCREEN_HOME);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  settingsLoad();

  pinMode(PIN_BTN_A, INPUT_PULLUP);
  pinMode(PIN_BTN_B, INPUT_PULLUP);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(I2C_CLOCK_HZ);
  u8g2.setI2CAddress(OLED_ADDR << 1);   // U8g2 uses the 8-bit address form
  u8g2.begin();

  maxOk = particleSensor.begin(Wire, I2C_CLOCK_HZ, MAX30105_ADDR);
  if (maxOk) {
    // powerLevel, sampleAverage, ledMode(2=Red+IR), sampleRate, pulseWidth, adcRange
    particleSensor.setup(0x1F, 4, 2, 400, 411, 4096);
    DBG("MAX30105 init OK (addr 0x%02X)", MAX30105_ADDR);
  } else {
    Serial.println("[watch] MAX30105 NOT found on I2C bus");
  }

  mpuOk = mpu.begin(MPU6050_ADDR, &Wire);
  if (mpuOk) {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    DBG("MPU6050 init OK (addr 0x%02X)", MPU6050_ADDR);
  } else {
    Serial.println("[watch] MPU6050 NOT found on I2C bus");
  }

  // Sensor init above (MAX begin with FAST, MPU with its own speed) may have
  // silently changed the shared I2C clock behind our back. Re-assert the value
  // from config.h so the OLED ALWAYS draws at the stabilised speed. Without
  // this, the display flickers/blanks because it is being written at 400 kHz
  // on this board's marginal bus.
  Wire.setClock(I2C_CLOCK_HZ);

  // Animate the loading bar over BOOT_BAR_MS, then hand off to the right start.
  const int steps = 26;
  for (int i = 0; i <= steps; i++) {
    drawLoading((uint8_t)(100L * i / steps));
    delay(BOOT_BAR_MS / steps);
  }

  if (settings().paired) {
    wifiSetup(settings().ssid, settings().pass);
    syncBegin();
    screen = SCREEN_HOME;
    DBG("paired -> HOME (wifi %s)", settings().ssid);
  } else {
    screen = SCREEN_PAIR;
    pairingBegin();
    DBG("not paired -> setup portal %s", pairingSsid());
  }

  clockBegin();   // NTP always registered; it syncs once WiFi is up (or via BLE TIME)

  if (BLE_ENABLE) bleStart();   // BLE is always on — the controller link to phone/web

  bootStartMs = millis();
  Serial.println("[watch] Boot complete");
}

// ── Serial command console (bring-up aid) ────────────────────────────────────
static void handleSerialCmd() {
  static String buf;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      buf.trim();
      if (buf.length()) {
        Serial.print("[watch] cmd > "); Serial.println(buf);
        if (buf == "h") {
          Serial.println("  p  = status line\n  s  = force sync now\n  t  = print device token\n  r  = reboot\n  clear = wipe pairing + reboot to portal");
        } else if (buf == "p") {
          logWatch();
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

void loop() {
  pollHeartRate();    // sampled every iteration — beat timing needs the resolution
  pollAccelStep();
  handleButtons();
  handleSerialCmd();

  uint32_t now = millis();

  netTick();                         // BLE-triggered "apply" pair verification
  if (BLE_ENABLE) bleTick();         // keep the STATE beacon fresh for connected phones

  // Networking: pairing portal when unpaired, else background wifi + sync.
  if (!settings().paired) {
    if (!pairingActive()) pairingBegin();
    pairingLoop();
  } else {
    if (pairingActive()) pairingLoop();   // grace window right after a fresh pair
    wifiTick();
    syncTick(clockNow(), beatAvg, stepCount);
  }

  static uint32_t lastDrawMs = 0;
  if (now - lastDrawMs >= 100) {   // ~10 fps screen refresh; plenty for a clock
    lastDrawMs = now;
    Screen s = settings().paired ? screen : SCREEN_PAIR;
    switch (s) {
      case SCREEN_HOME:
        drawHome();
        break;
      case SCREEN_HR:
        drawHR();
        break;
      case SCREEN_STEPS:
        drawSteps();
        break;
      case SCREEN_SYNC:
        drawSync();
        break;
      case SCREEN_STATUS:
        drawStatus();
        break;
      case SCREEN_PAIR:
        drawPair();
        break;
      default:
        break;
    }

    // Marginal-bus watchdog: a garbled I2C transaction can desync the SSD1306's
    // internal state, and it stays corrupt until re-init'd — that looks like the
    // screen "tweaking" after a while. If the frame send errored, re-init the
    // display (rate-limited; begin() re-sends the init sequence and re-syncs).
    if (Wire.getWriteError() != 0) {
      static uint32_t lastReinitMs = 0;
      if (now - lastReinitMs > 2000) {
        lastReinitMs = now;
        u8g2.begin();
        DBG("display re-init (i2c err %u)", Wire.getWriteError());
      }
    }
  }
}
