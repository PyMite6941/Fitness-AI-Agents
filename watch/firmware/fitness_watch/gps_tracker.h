#pragma once
#include <Arduino.h>

struct GpsPoint {
    double lat;
    double lng;
    float  speed_mps;
    float  altitude_m;
    String timestamp;   // ISO8601
};

bool   gps_init();
void   gps_update();            // call in loop — feeds UART chars to TinyGPS
bool   gps_has_fix();
GpsPoint gps_current();

// Route recording
void   gps_start_route();
void   gps_stop_route();
bool   gps_is_recording();
float  gps_route_distance_m();  // haversine sum of recorded points
int    gps_point_count();
// Returns all points as JSON array string for /routes/ endpoint
String gps_route_json();
String gps_route_started_at();
