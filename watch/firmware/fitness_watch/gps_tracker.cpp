#include "gps_tracker.h"
#include "config.h"
#include "wifi_sync.h"
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <ArduinoJson.h>
#include <cmath>
#include <cstring>

static TinyGPSPlus    _gps;
// UART1 — the ESP32-C3 only has UART0 and UART1 (no UART2). Pins are remapped
// in gps_init() via the GPIO matrix, so any valid GPIO works.
static HardwareSerial _gpsSerial(1);

#define MAX_ROUTE_POINTS 500
static GpsPoint _route[MAX_ROUTE_POINTS];
static int      _route_len    = 0;
static bool     _recording    = false;
static float    _route_dist_m = 0;
static char     _started_at[25] = {};
static unsigned long _last_point_ms = 0;

// Single-precision haversine. The ESP32-C3 has no hardware FPU, so float math
// (and avoiding pow()) is markedly cheaper than double here. This only feeds the
// live on-watch distance readout — the backend recomputes the authoritative
// distance from the full coordinate list posted to /routes/.
static float _haversine(double lat1, double lon1, double lat2, double lon2) {
    // Guard identical points — avoids asin(sqrt(0)) edge case
    if (lat1 == lat2 && lon1 == lon2) return 0.0f;
    const float R = 6371000.0f;
    const float p = 0.0174532925f;                 // PI / 180
    // Deltas in double (keeps precision of the small differences), trig in float.
    float dLat = (float)(lat2 - lat1) * p;
    float dLon = (float)(lon2 - lon1) * p;
    float sLat = sinf(dLat * 0.5f);
    float sLon = sinf(dLon * 0.5f);
    float a = sLat * sLat + cosf((float)lat1 * p) * cosf((float)lat2 * p) * sLon * sLon;
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
    return 2.0f * R * asinf(sqrtf(a));
}

bool gps_init() {
    _gpsSerial.begin(9600, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    Serial.println("[gps] NEO-6M UART started at 9600 baud");
    return true;
}

void gps_update() {
    while (_gpsSerial.available()) {
        _gps.encode(_gpsSerial.read());
    }

    if (!_recording || !_gps.location.isValid()) return;

    unsigned long now = millis();
    if (now - _last_point_ms < GPS_POINT_INTERVAL_MS) return;
    _last_point_ms = now;

    if (_route_len >= MAX_ROUTE_POINTS) {
        // Silently drop is bad UX — log once
        static bool warned = false;
        if (!warned) {
            Serial.println("[gps] WARNING: route buffer full, points dropped");
            warned = true;
        }
        return;
    }

    GpsPoint pt;
    pt.lat        = _gps.location.lat();
    pt.lng        = _gps.location.lng();
    pt.speed_mps  = (float)_gps.speed.mps();
    pt.altitude_m = (float)_gps.altitude.meters();
    wifi_iso_now_buf(pt.timestamp, sizeof(pt.timestamp));

    if (_route_len > 0) {
        _route_dist_m += (float)_haversine(
            _route[_route_len-1].lat, _route[_route_len-1].lng,
            pt.lat, pt.lng
        );
    }
    _route[_route_len++] = pt;
}

bool        gps_has_fix()          { return _gps.location.isValid(); }
bool        gps_is_recording()     { return _recording; }
float       gps_route_distance_m() { return _route_dist_m; }
int         gps_point_count()      { return _route_len; }
const char* gps_route_started_at() { return _started_at; }

GpsPoint gps_current() {
    GpsPoint pt;
    pt.lat        = _gps.location.lat();
    pt.lng        = _gps.location.lng();
    pt.speed_mps  = (float)_gps.speed.mps();
    pt.altitude_m = (float)_gps.altitude.meters();
    wifi_iso_now_buf(pt.timestamp, sizeof(pt.timestamp));
    return pt;
}

void gps_start_route() {
    _route_len    = 0;
    _route_dist_m = 0;
    _recording    = true;
    _last_point_ms = 0;
    wifi_iso_now_buf(_started_at, sizeof(_started_at));
    Serial.println("[gps] Route recording started");
}

void gps_stop_route() {
    _recording = false;
    Serial.printf("[gps] Route stopped: %d points, %.1f m\n", _route_len, _route_dist_m);
}

String gps_route_json() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < _route_len; i++) {
        JsonObject p = arr.add<JsonObject>();
        p["lat"]       = _route[i].lat;          // direct float — correct numeric JSON
        p["lng"]       = _route[i].lng;
        p["timestamp"] = _route[i].timestamp;
        if (_route[i].altitude_m != 0.0f) p["altitude"] = _route[i].altitude_m;
        if (_route[i].speed_mps  != 0.0f) p["speed"]    = _route[i].speed_mps;
    }
    String out;
    serializeJson(doc, out);
    return out;
}
