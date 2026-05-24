#include "sensors.h"
#include "config.h"
#include <Wire.h>
#include <cmath>
#include "MAX30105.h"
#include "heartRate.h"
#include <MPU6050_light.h>

static MAX30105 _max;
static MPU6050  _mpu(Wire);
static SensorData _data = {};

static const uint8_t RATE_SIZE = 4;
static float _rates[RATE_SIZE] = {};
static uint8_t _rateSpot = 0;
static long _lastBeat = 0;      // 0 = never seen a beat yet
static float _beatsPerMinute = 0;
static float _beatAvg = 0;

static int   _steps = 0;
static long  _lastStepMs = 0;
static bool  _stepArmed = true;

bool sensors_init() {
    Wire.begin(PIN_SDA, PIN_SCL);

    if (!_max.begin(Wire, I2C_SPEED_FAST)) {
        Serial.println("[sensors] MAX30102 not found");
        return false;
    }
    _max.setup();
    _max.setPulseAmplitudeRed(0x3E);
    _max.setPulseAmplitudeGreen(0);
    Serial.println("[sensors] MAX30102 OK");

    byte mpuStatus = _mpu.begin();
    if (mpuStatus != 0) {
        Serial.printf("[sensors] MPU6050 error %d\n", mpuStatus);
        return false;
    }
    Serial.println("[sensors] Calibrating MPU6050... keep still");
    _mpu.calcOffsets(true, true);
    Serial.println("[sensors] MPU6050 OK");

    return true;
}

void sensors_update() {
    // ── Heart rate ─────────────────────────────────────────────────────
    long irValue = _max.getIR();
    _data.hr_valid = (irValue > 50000);

    if (_data.hr_valid) {
        if (checkForBeat(irValue)) {
            long now = millis();
            if (_lastBeat == 0) {
                // First beat — record timestamp but don't calculate BPM yet
                // (delta would be millis-since-boot which is nonsensical)
                _lastBeat = now;
            } else {
                float delta = (float)(now - _lastBeat);
                _lastBeat = now;
                _beatsPerMinute = 60000.0f / delta;  // ms → BPM directly

                if (_beatsPerMinute > 20.0f && _beatsPerMinute < 255.0f) {
                    _rates[_rateSpot % RATE_SIZE] = _beatsPerMinute;
                    _rateSpot = (_rateSpot + 1) % RATE_SIZE;

                    // Only average slots that have been filled
                    uint8_t filled = (_rateSpot < RATE_SIZE) ? _rateSpot : RATE_SIZE;
                    float sum = 0;
                    for (uint8_t i = 0; i < filled; i++) sum += _rates[i];
                    _beatAvg = (filled > 0) ? sum / filled : 0;
                }
            }
        }
        _data.heart_rate  = _beatAvg;
        _data.temperature = _max.readTemperature();
    } else {
        _data.heart_rate  = 0;
        _data.spo2        = 0;
        _data.temperature = 0;
        // Reset state so next finger placement starts fresh
        memset(_rates, 0, sizeof(_rates));
        _rateSpot     = 0;
        _beatAvg      = 0;
        _beatsPerMinute = 0;
        _lastBeat     = 0;
    }

    // ── Step counting ──────────────────────────────────────────────────
    _mpu.update();
    float ax = _mpu.getAccX();
    float ay = _mpu.getAccY();
    float az = _mpu.getAccZ();
    float mag = sqrtf(ax*ax + ay*ay + az*az);
    _data.accel_mag = mag;

    long now = millis();
    if (_stepArmed && mag > STEP_THRESHOLD && (now - _lastStepMs) > STEP_DEBOUNCE_MS) {
        _steps++;
        _lastStepMs = now;
        _stepArmed = false;
    }
    // Re-arm when magnitude returns within 5% of 1g (gravity)
    if (!_stepArmed && fabsf(mag - 1.0f) < 0.10f) {
        _stepArmed = true;
    }

    _data.steps = _steps;
}

const SensorData& sensors_get() {
    return _data;
}

void sensors_reset_steps() {
    _steps = 0;
}
