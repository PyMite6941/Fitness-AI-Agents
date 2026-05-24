#pragma once
#include <Arduino.h>

struct GpsPoint {
    double lat;
    double lng;
    float  speed_mps;
    float  altitude_m;
    char   timestamp[25];   // ISO8601 — fixed-size, no heap alloc
};

bool   gps_init();
void   gps_update();
bool   gps_has_fix();
GpsPoint gps_current();

void   gps_start_route();
void   gps_stop_route();
bool   gps_is_recording();
float  gps_route_distance_m();
int    gps_point_count();
String gps_route_json();
const char* gps_route_started_at();
