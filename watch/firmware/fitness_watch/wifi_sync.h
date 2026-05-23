#pragma once
#include <Arduino.h>
#include "sensors.h"

struct WorkoutRecord {
    String  workout_type;
    String  started_at;     // ISO8601
    String  ended_at;
    float   avg_heart_rate;
    float   max_heart_rate;
    float   calories_burned;
    float   distance_meters;
    int     steps;
};

bool wifi_connect();
bool wifi_is_connected();
void wifi_sync_reading(const SensorData& data);     // queues a reading
void wifi_sync_workout(const WorkoutRecord& w);     // queues a completed workout
bool wifi_flush();                                  // POST queued data, returns true on success
void wifi_set_time_ntp();
String wifi_iso_now();
