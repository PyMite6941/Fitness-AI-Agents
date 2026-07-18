/*
 * FitnessAI Watch — main program
 * Board:   ESP32-C3 SuperMini
 * Display: SSD1306 128x64 OLED (I2C), rendered with U8g2
 * Sensors: MAX30105 heart rate/SpO2 (SparkFun MAX3010x lib), MPU6050 accel/gyro
 *          (Adafruit_MPU6050 lib) — both on the same I2C bus as the OLED.
 *
 * Scope: screen + program skeleton + heart rate + step counting.
 *   boot  →  loading screen (animated bar)  →  home screen (live)
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

// Full-buffer, hardware-I2C SSD1306.
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /*reset=*/U8X8_PIN_NONE);

MAX30105 particleSensor;
Adafruit_MPU6050 mpu;
static bool maxOk = false;
static bool mpuOk = false;

// ── Heart rate (SparkFun's beat-averaging approach) ──────────────────────────
static const byte RATE_SIZE = 4;
static byte rates[RATE_SIZE];
static byte rateSpot = 0;
static uint32_t lastBeatMs = 0;
static int beatAvg = 0;
static bool fingerPresent = false;

// ── Step counting (simple threshold/debounce on accel magnitude) ────────────
static uint32_t stepCount = 0;
static bool aboveStepThreshold = false;
static uint32_t lastStepMs = 0;
static const float STEP_THRESHOLD_MS2 = 2.0f;  // deviation from gravity to count as motion
static const uint32_t STEP_DEBOUNCE_MS = 250;  // min gap between steps (caps ~240 spm)

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

// ── Sensor polling ───────────────────────────────────────────────────────────
// Sampled every loop() iteration (no throttling) — the beat-detection algorithm
// needs frequent IR samples to time pulses accurately.
static void pollHeartRate() {
  if (!maxOk) return;

  long irValue = particleSensor.getIR();
  fingerPresent = irValue > 50000;   // low IR = no finger on the sensor

  if (checkForBeat(irValue)) {
    uint32_t now = millis();
    float delta = now - lastBeatMs;
    lastBeatMs = now;

    float bpm = 60000.0f / delta;
    if (bpm > 20 && bpm < 255) {
      rates[rateSpot++] = (byte)bpm;
      rateSpot %= RATE_SIZE;
      int sum = 0;
      for (byte i = 0; i < RATE_SIZE; i++) sum += rates[i];
      beatAvg = sum / RATE_SIZE;
    }
  }
}

static void pollAccelStep() {
  if (!mpuOk) return;

  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);

  float mag = sqrtf(accel.acceleration.x * accel.acceleration.x +
                     accel.acceleration.y * accel.acceleration.y +
                     accel.acceleration.z * accel.acceleration.z);
  float dynamic = fabsf(mag - 9.80665f);   // deviation from gravity at rest

  uint32_t now = millis();
  if (dynamic > STEP_THRESHOLD_MS2) {
    if (!aboveStepThreshold && (now - lastStepMs) > STEP_DEBOUNCE_MS) {
      stepCount++;
      lastStepMs = now;
    }
    aboveStepThreshold = true;
  } else {
    aboveStepThreshold = false;
  }
}

// ── Home screen ──────────────────────────────────────────────────────────────
// Placeholder clock (counts seconds since boot) so we can see the loop is alive.
// Real time arrives in a later milestone (Wi-Fi sync). HR + steps are live.
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

  // Footer: heart rate + steps, or which sensor(s) failed to init
  char foot[24];
  if (!maxOk || !mpuOk) {
    snprintf(foot, sizeof(foot), "%s%s missing",
             !maxOk ? "HR " : "", !mpuOk ? "IMU" : "");
  } else if (!fingerPresent) {
    snprintf(foot, sizeof(foot), "-- bpm  %lu steps", (unsigned long)stepCount);
  } else {
    snprintf(foot, sizeof(foot), "%d bpm  %lu steps", beatAvg, (unsigned long)stepCount);
  }
  u8g2.drawStr(2, 63, foot);

  u8g2.sendBuffer();
}

void setup() {
  Serial.begin(115200);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(I2C_CLOCK_HZ);
  u8g2.setI2CAddress(OLED_ADDR << 1);   // U8g2 uses the 8-bit address form
  u8g2.begin();

  maxOk = particleSensor.begin(Wire, I2C_SPEED_FAST, MAX30105_ADDR);
  if (maxOk) {
    // powerLevel, sampleAverage, ledMode(2=Red+IR), sampleRate, pulseWidth, adcRange
    particleSensor.setup(0x1F, 4, 2, 400, 411, 4096);
  } else {
    Serial.println("MAX30105 not found on I2C bus");
  }

  mpuOk = mpu.begin(MPU6050_ADDR, &Wire);
  if (mpuOk) {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  } else {
    Serial.println("MPU6050 not found on I2C bus");
  }

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
  pollHeartRate();    // sampled every iteration — beat timing needs the resolution
  pollAccelStep();

  static uint32_t lastDrawMs = 0;
  uint32_t now = millis();
  if (now - lastDrawMs >= 100) {   // ~10 fps screen refresh; plenty for a clock
    lastDrawMs = now;
    switch (screen) {
      case SCREEN_HOME:
        drawHome();
        break;
      default:
        break;
    }
  }
}
