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

// Full-screen pairing / status screens (used by the Wi-Fi setup flow)
void        display_pairing(const char* ap_ssid, const char* url);
void        display_centered(const char* line1, const char* line2);
