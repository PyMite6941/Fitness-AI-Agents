#pragma once
#include <Arduino.h>

struct SensorData {
    float   heart_rate;     // bpm, 0 if no finger detected
    float   spo2;           // %, 0 if no finger detected
    int     steps;          // cumulative since boot
    float   temperature;    // °C from MAX30102 die temp
    float   accel_mag;      // g — last raw acceleration magnitude
    bool    hr_valid;
};

bool     sensors_init();
void     sensors_update();          // call every SENSOR_INTERVAL_MS
const SensorData& sensors_get();
void     sensors_reset_steps();
