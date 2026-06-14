#pragma once
#include <Arduino.h>
#include "sensors.h"

struct WorkoutRecord {
    char    workout_type[20];
    char    started_at[25];
    char    ended_at[25];
    float   avg_heart_rate;
    float   max_heart_rate;
    float   calories_burned;
    float   distance_meters;
    int     steps;
};

bool  wifi_connect();
bool  wifi_is_connected();

// Write ISO8601 UTC timestamp into caller-supplied buffer (no heap alloc)
void  wifi_iso_now_buf(char* buf, size_t len);

// Legacy helper returning String — use sparingly
String wifi_iso_now();

// Queue data for next sync
void  wifi_sync_reading(const SensorData& data);
void  wifi_sync_workout(const WorkoutRecord& w);

// Queue a completed GPS route (coordinates + workout meta) for upload to
// /routes/. Serialises immediately (main-loop context, GPS data stable) but the
// HTTP POST happens in the background sync task. The backend derives distance,
// pace and calories from it, so the watch sends no separate workout for GPS runs.
void  wifi_queue_route(const WorkoutRecord& w);

// Start background FreeRTOS sync task
void  wifi_init_sync_task();

// Trigger a background flush (non-blocking, returns false if already syncing)
bool  wifi_flush_async();

// Blocking flush — use only during setup or explicit user action
bool  wifi_flush();

bool  wifi_is_syncing();
void  wifi_set_time_ntp();
