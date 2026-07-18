/*
 * FitnessAI Watch — hardware DIAGNOSTIC sketch (not the product firmware).
 * Board: ESP32-C3 SuperMini.  Flash with CDCOnBoot=cdc so Serial is on USB.
 *
 * Tests every component on the shared I2C bus one at a time and streams live
 * values over USB serial so each part can be verified interactively:
 *   1. I2C scan          — lists every address that ACKs on SDA=7 / SCL=8
 *   2. SSD1306 OLED       — draws a visible DIAG test screen
 *   3. MAX30105 (HR)      — part ID + live IR/RED (put a finger on it → IR jumps)
 *   4. MPU6050 (IMU)      — live accel/gyro/temp (tilt/shake → values change)
 *
 * Wiring lives in the product config.h; this sketch hard-codes the same pins
 * so it stands alone.
 */

#include <Wire.h>
#include <U8g2lib.h>
#include <MAX30105.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// Same bus as the product firmware (config.h): SDA=7, SCL=8.
static const int PIN_SDA = 7;
static const int PIN_SCL = 8;
static const uint8_t OLED_ADDR = 0x3C;
static const uint8_t MAX_ADDR  = 0x57;
static const uint8_t MPU_ADDR  = 0x68;

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /*reset=*/U8X8_PIN_NONE);
MAX30105 particleSensor;
Adafruit_MPU6050 mpu;

static bool oledOk = false, maxOk = false, mpuOk = false;

static void i2cScan() {
  Serial.println(F("\n[1] I2C scan on SDA=7 SCL=8 ..."));
  int found = 0;
  for (uint8_t a = 0x08; a <= 0x77; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("    ACK 0x%02X", a);
      if (a == OLED_ADDR) Serial.print(F("  <- SSD1306 OLED"));
      else if (a == MAX_ADDR) Serial.print(F("  <- MAX3010x HR"));
      else if (a == MPU_ADDR) Serial.print(F("  <- MPU6050 IMU"));
      Serial.println();
      found++;
    }
  }
  Serial.printf("    %d device(s) found.\n", found);
}

void setup() {
  Serial.begin(115200);
  delay(1500);                       // give USB CDC time to enumerate
  Serial.println(F("\n===== FitnessAI Watch — HARDWARE DIAGNOSTIC ====="));

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);

  i2cScan();

  Serial.println(F("\n[2] SSD1306 OLED init ..."));
  u8g2.setI2CAddress(OLED_ADDR << 1);
  oledOk = u8g2.begin();
  Serial.println(oledOk ? F("    OLED begin() OK — check the screen for 'DIAG'")
                        : F("    OLED begin() FAILED"));
  if (oledOk) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_helvB14_tr);
    u8g2.drawStr(30, 22, "DIAG");
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(6, 40, "OLED test OK");
    u8g2.drawFrame(0, 0, 128, 64);
    u8g2.sendBuffer();
  }

  Serial.println(F("\n[3] MAX30105 (heart rate) init ..."));
  maxOk = particleSensor.begin(Wire, I2C_SPEED_FAST, MAX_ADDR);
  if (maxOk) {
    particleSensor.setup(0x1F, 4, 2, 400, 411, 4096);
    Serial.printf("    MAX3010x found. part ID = 0x%02X, rev = 0x%02X\n",
                  particleSensor.readPartID(), particleSensor.getRevisionID());
    Serial.println(F("    -> place a finger on the sensor; IR should exceed ~50000"));
  } else {
    Serial.println(F("    MAX3010x NOT found on the bus"));
  }

  Serial.println(F("\n[4] MPU6050 (accel/gyro) init ..."));
  mpuOk = mpu.begin(MPU_ADDR, &Wire);
  if (mpuOk) {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    Serial.println(F("    MPU6050 found. -> tilt/shake the board; accel should change"));
  } else {
    Serial.println(F("    MPU6050 NOT found on the bus"));
  }

  Serial.printf("\nSUMMARY  OLED:%s  HR:%s  IMU:%s\n",
                oledOk ? "OK" : "FAIL",
                maxOk  ? "OK" : "FAIL",
                mpuOk  ? "OK" : "FAIL");
  Serial.println(F("Streaming live values (one line/500ms) ...\n"));
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last < 500) return;
  last = millis();

  Serial.print("HR ");
  if (maxOk) {
    long ir = particleSensor.getIR();
    long red = particleSensor.getRed();
    Serial.printf("IR=%-7ld RED=%-7ld %s", ir, red,
                  ir > 50000 ? "[finger]" : "[  no  ]");
  } else Serial.print("--");

  Serial.print("   IMU ");
  if (mpuOk) {
    sensors_event_t a, g, t;
    mpu.getEvent(&a, &g, &t);
    Serial.printf("ax=%+.2f ay=%+.2f az=%+.2f  gz=%+.2f  T=%.1fC",
                  a.acceleration.x, a.acceleration.y, a.acceleration.z,
                  g.gyro.z, t.temperature);
  } else Serial.print("--");

  Serial.println();
}
