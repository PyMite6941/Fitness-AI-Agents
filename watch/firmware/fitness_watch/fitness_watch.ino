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

#include <Wire.h>
#include <U8g2lib.h>
#include <MAX30105.h>
#include <heartRate.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <esp_sleep.h>
#include "config.h"
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
  u8g2.drawStr(2, 55, battDetail());   // "4.06V 78% bat", or "Battery: not wired"

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

  // The 5x7 font is 5 px per glyph, so the panel fits 25 characters at x=2.
  // The old hint here was 32 characters (160 px): it was clipped mid-glyph and
  // "= mute" never appeared at all. Caught by screenshotting the simulator.
  // The divider also moved up 2 px — at y=57 it collided with the ascenders of
  // a baseline-63 line, which occupies y=57..63.
  u8g2.drawHLine(0, 55, OLED_WIDTH);
  u8g2.drawStr(2, 63, "A hold=home  Bx2=mute");
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

// ── Screen-off toggle (double-press button B = GPIO5) ────────────────────────
// A double-press of GPIO5 turns ONLY the OLED off. Every vitals process — HR
// sampling, step counting, BLE/WiFi — keeps running; this is a display mute, not
// a system sleep. Double-press GPIO5 again brings the screen straight back.
static bool standby = false;
static void drawScreen();   // defined below; used by exitStandby to repaint

static void enterStandby() {
  standby = true;
  u8g2.noDisplay();                 // SSD1306 display OFF command
  DBG("SCREEN OFF (vitals still monitored)");
}

static void exitStandby() {
  standby = false;
  // Wake with the display ON command — no full begin() re-init flash here. The
  // periodic displaySelfHeal() already re-syncs a corrupt panel if one develops.
  u8g2.setPowerSave(0);             // SSD1306 display ON command
  u8g2.clearBuffer();
  drawScreen();
  DBG("SCREEN ON");
}

// ── Navigation ───────────────────────────────────────────────────────────────
static void handleButtons() {
  uint32_t now = millis();
  updateButton(btnA, PIN_BTN_A, now);
  updateButton(btnB, PIN_BTN_B, now);

  if (screen == SCREEN_BOOT) return;

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
      else         enterStandby();
      return;
    }
    bFirstTapMs = nowTap;                 // ── first trip of a possible double
  }

  // Single-press action for B fires when the window expires with no second tap.
  static const Screen ORDER[] = {SCREEN_HOME, SCREEN_HR, SCREEN_STEPS, SCREEN_SYNC, SCREEN_STATUS};
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
    if (screen == SCREEN_PAIR) { pairShowToken = !pairShowToken; return; }
    goScreen(SCREEN_HOME);                // ── single press → home
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  settingsLoad();

  pinMode(PIN_BTN_A, BTN_ACTIVE_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);
  pinMode(PIN_BTN_B, BTN_ACTIVE_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);

  powerBegin();   // battery divider — safe no-op when PIN_BATT_ADC is -1

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(I2C_CLOCK_HZ);
  u8g2.setI2CAddress(OLED_ADDR << 1);   // U8g2 uses the 8-bit address form
  u8g2.begin();

#if SIM_BUILD
  // No MAX30105 part exists in Wokwi, so there is no device to configure —
  // simIR() feeds the real beat-detection path instead. See sim.h.
  maxOk = simMaxBegin();
  DBG("SIM: synthetic MAX30105 @ %d bpm", g_simBpm);
#else
  maxOk = particleSensor.begin(Wire, I2C_CLOCK_HZ, MAX30105_ADDR);
  if (maxOk) {
    // powerLevel, sampleAverage, ledMode(2=Red+IR), sampleRate, pulseWidth, adcRange
    particleSensor.setup(0x1F, 4, 2, 400, 411, 4096);
    DBG("MAX30105 init OK (addr 0x%02X)", MAX30105_ADDR);
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

  if (DISPLAY_RADIO_TEST) {
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

  clockBegin();   // NTP always registered; it syncs once WiFi is up (or via BLE TIME)

  if (BLE_ENABLE && !DISPLAY_RADIO_TEST) bleStart();   // BLE is always on — the controller link to phone/web

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

// Repaint whichever screen is active. Split out so the self-heal re-init below
// can repaint the current frame immediately instead of leaving a blank flash.
static void drawScreen() {
  Screen s = screen;
  switch (s) {
    case SCREEN_HOME:   drawHome();   break;
    case SCREEN_HR:     drawHR();     break;
    case SCREEN_STEPS:  drawSteps();  break;
    case SCREEN_SYNC:   drawSync();   break;
    case SCREEN_STATUS: drawStatus(); break;
    case SCREEN_PAIR:   drawPair();   break;
    default: break;
  }
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
  u8g2.begin();
  drawScreen();
}

void loop() {
  // Screen-off (standby) is a display-only mute — vitals keep getting polled
  // every iteration below. Nothing blocks here; the double-press toggle comes
  // through handleButtons(). The only difference is the draw step is skipped.
  pollHeartRate();    // sampled every iteration — beat timing needs the resolution
  pollAccelStep();
  handleButtons();
  if (standby) return;   // screen off: skip serial/net/draw work this tick
  handleSerialCmd();

  uint32_t now = millis();

  powerTick();                       // battery sample (throttled to BATT_INTERVAL_MS)

  netTick();                         // BLE-triggered "apply" pair verification
  if (BLE_ENABLE) bleTick();         // keep the STATE beacon fresh for connected phones

  // Networking: pairing portal when unpaired, else background wifi + sync.
  // Skipped entirely in the DISPLAY_RADIO_TEST diagnostic build, and shed once
  // the cell is critical — WiFi TX peaks at ~300 mA, which is exactly what
  // collapses a nearly-empty Li-ion into a brownout reset. Readings keep
  // queueing in RAM and flush on the next charge.
  static bool loggedCrit = false;
  bool crit = battCritical();
  if (crit && !loggedCrit) { loggedCrit = true; DBG("battery critical -> radio shed"); }
  if (!crit) loggedCrit = false;

  if (crit) {
    wifiOff();
  } else if (settings().paired && settings().ssid[0] && !DISPLAY_RADIO_TEST) {
    wifiTick();
    syncTick(clockNow(), beatAvg, stepCount);
  } else if (!DISPLAY_RADIO_TEST && !settings().paired && pairingActive()) {
    pairingLoop();   // keep an already-started portal alive (BLE-apply grace)
  }

  displaySelfHeal();

  static uint32_t lastDrawMs = 0;
  if (now - lastDrawMs >= 200) {   // ~5 fps; a 100 kHz frame is slow, so half the
    lastDrawMs = now;              // redraw rate cuts bus exposure in half
    drawScreen();
  }
}
