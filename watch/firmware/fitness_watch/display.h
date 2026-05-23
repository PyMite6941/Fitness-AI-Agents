#pragma once
#include <Arduino.h>
#include "sensors.h"

enum DisplayMode {
    MODE_CLOCK = 0,
    MODE_HEART,
    MODE_STEPS,
    MODE_WORKOUT,
    MODE_SYNC,
    MODE_COUNT
};

bool        display_init();
void        display_set_mode(DisplayMode mode);
DisplayMode display_get_mode();
void        display_cycle_mode();
void        display_update(const SensorData& data, bool wifi_ok, bool syncing,
                           int workout_seconds, float workout_dist_m);
void        display_off();
void        display_on();
void        display_notify(const char* msg, uint16_t ms = 1500);
