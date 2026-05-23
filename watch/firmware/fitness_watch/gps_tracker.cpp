#include "gps_tracker.h"
#include "config.h"
#include "wifi_sync.h"
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <ArduinoJson.h>

static TinyGPSPlus  _gps;
static HardwareSerial _gpsSerial(2);  // UART2

// Route recording
#define MAX_ROUTE_POINTS 500
static GpsPoint _route[MAX_ROUTE_POINTS];
static int      _route_len    = 0;
static bool     _recording    = false;
static float    _route_dist_m = 0;
static String   _started_at;
static unsigned long _last_point_ms = 0;

static double _haversine(double lat1, double lon1, double lat2, double lon2) {
    const double R = 6371000.0;
    double p = M_PI / 180.0;
    double a = pow(sin((lat2-lat1)*p/2), 2)
             + cos(lat1*p) * cos(lat2*p) * pow(sin((lon2-lon1)*p/2), 2);
    return 2 * R * asin(sqrt(a));
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

    if (_route_len >= MAX_ROUTE_POINTS) return;

    GpsPoint pt;
    pt.lat        = _gps.location.lat();
    pt.lng        = _gps.location.lng();
    pt.speed_mps  = _gps.speed.mps();
    pt.altitude_m = _gps.altitude.meters();
    pt.timestamp  = wifi_iso_now();

    if (_route_len > 0) {
        _route_dist_m += _haversine(
            _route[_route_len-1].lat, _route[_route_len-1].lng,
            pt.lat, pt.lng
        );
    }
    _route[_route_len++] = pt;
}

bool gps_has_fix()  { return _gps.location.isValid(); }
bool gps_is_recording() { return _recording; }
float gps_route_distance_m() { return _route_dist_m; }
int   gps_point_count()      { return _route_len; }
String gps_route_started_at(){ return _started_at; }

GpsPoint gps_current() {
    GpsPoint pt;
    pt.lat        = _gps.location.lat();
    pt.lng        = _gps.location.lng();
    pt.speed_mps  = _gps.speed.mps();
    pt.altitude_m = _gps.altitude.meters();
    pt.timestamp  = wifi_iso_now();
    return pt;
}

void gps_start_route() {
    _route_len    = 0;
    _route_dist_m = 0;
    _recording    = true;
    _last_point_ms = 0;
    _started_at   = wifi_iso_now();
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
        p["lat"]       = serialized(String(_route[i].lat, 6));
        p["lng"]       = serialized(String(_route[i].lng, 6));
        p["timestamp"] = _route[i].timestamp;
        if (_route[i].altitude_m != 0) p["altitude"] = _route[i].altitude_m;
        if (_route[i].speed_mps  != 0) p["speed"]    = _route[i].speed_mps;
    }
    String out;
    serializeJson(doc, out);
    return out;
}
