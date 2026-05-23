#include "config.h"
#include "sensors.h"
#include "display.h"
#include "wifi_sync.h"
#include "gps_tracker.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ── State ─────────────────────────────────────────────────────────────────
enum AppState { STATE_IDLE, STATE_WORKOUT };
static AppState _state = STATE_IDLE;

static unsigned long _lastSensorMs  = 0;
static unsigned long _lastSyncMs    = 0;
static unsigned long _lastDisplayMs = 0;
static unsigned long _lastActivityMs = 0;

// Workout tracking
static unsigned long _workoutStartMs = 0;
static String        _workoutStartISO;
static float         _maxHR = 0;
static float         _hrSum = 0;
static int           _hrSamples = 0;
static int           _stepsAtStart = 0;

// Button debounce
static unsigned long _btnALast = 0;
static unsigned long _btnBLast = 0;
static bool          _btnAWas  = HIGH;
static bool          _btnBWas  = HIGH;
#define BTN_DEBOUNCE_MS 200

// ── Helpers ───────────────────────────────────────────────────────────────
static void _start_workout(const String& type = "running") {
    _state           = STATE_WORKOUT;
    _workoutStartMs  = millis();
    _workoutStartISO = wifi_iso_now();
    _maxHR           = 0;
    _hrSum           = 0;
    _hrSamples       = 0;
    _stepsAtStart    = sensors_get().steps;
    gps_start_route();
    display_set_mode(MODE_WORKOUT);
    display_notify("Workout started!");
    Serial.println("[app] Workout started");
}

static void _finish_workout() {
    gps_stop_route();
    String endISO = wifi_iso_now();
    int elapsed_s = (millis() - _workoutStartMs) / 1000;

    WorkoutRecord w;
    w.workout_type    = "running";   // TODO: selectable via button combo
    w.started_at      = _workoutStartISO;
    w.ended_at        = endISO;
    w.avg_heart_rate  = (_hrSamples > 0) ? (_hrSum / _hrSamples) : 0;
    w.max_heart_rate  = _maxHR;
    w.distance_meters = gps_route_distance_m();
    w.steps           = sensors_get().steps - _stepsAtStart;

    // Rough calorie estimate: MET 8 × weight 70kg × hours
    float hours = elapsed_s / 3600.0f;
    w.calories_burned = 8.0f * 70.0f * hours;

    wifi_sync_workout(w);

    // Also save route if we have GPS points
    if (gps_point_count() > 1) {
        _post_route(w, endISO);
    }

    _state = STATE_IDLE;
    display_set_mode(MODE_CLOCK);
    display_notify("Saved!");
    Serial.println("[app] Workout finished");
}

static void _post_route(const WorkoutRecord& w, const String& endISO) {
    if (!wifi_is_connected()) return;

    JsonDocument doc;
    doc["workout_type"] = w.workout_type;
    doc["started_at"]   = w.started_at;
    doc["ended_at"]     = endISO;
    doc["coordinates"]  = serialized(gps_route_json());

    String body;
    serializeJson(doc, body);

    HTTPClient http;
    http.begin(String(API_BASE_URL) + "/routes/");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", String("Bearer ") + CLERK_TOKEN);
    http.setTimeout(10000);
    int code = http.POST(body);
    Serial.printf("[app] Route POST → HTTP %d\n", code);
    http.end();
}

static void _check_buttons() {
    unsigned long now = millis();

    bool aVal = digitalRead(PIN_BUTTON_A);
    if (_btnAWas == HIGH && aVal == LOW && now - _btnALast > BTN_DEBOUNCE_MS) {
        _btnALast = now;
        display_cycle_mode();
        display_on();
        _lastActivityMs = now;
    }
    _btnAWas = aVal;

    bool bVal = digitalRead(PIN_BUTTON_B);
    if (_btnBWas == HIGH && bVal == LOW && now - _btnBLast > BTN_DEBOUNCE_MS) {
        _btnBLast = now;
        _lastActivityMs = now;
        display_on();
        if (_state == STATE_IDLE) {
            _start_workout();
        } else {
            _finish_workout();
        }
    }
    _btnBWas = bVal;
}

// ── Setup ─────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("\n[app] Fitness Watch booting...");

    pinMode(PIN_BUTTON_A, INPUT_PULLUP);
    pinMode(PIN_BUTTON_B, INPUT_PULLUP);
    if (PIN_VIBE >= 0) { pinMode(PIN_VIBE, OUTPUT); digitalWrite(PIN_VIBE, LOW); }

    bool dispOk    = display_init();
    bool sensorsOk = sensors_init();
    bool gpsOk     = gps_init();

    if (dispOk) {
        display_notify("Connecting WiFi...", 3000);
    }

    bool wifiOk = wifi_connect();

    if (dispOk) {
        display_notify(wifiOk ? "WiFi OK!" : "No WiFi", 1500);
    }

    Serial.printf("[app] Ready — display:%d sensors:%d gps:%d wifi:%d\n",
                  dispOk, sensorsOk, gpsOk, wifiOk);

    _lastActivityMs = millis();
}

// ── Loop ──────────────────────────────────────────────────────────────────
void loop() {
    unsigned long now = millis();

    _check_buttons();
    gps_update();

    // Read sensors
    if (now - _lastSensorMs >= SENSOR_INTERVAL_MS) {
        _lastSensorMs = now;
        sensors_update();
        SensorData data = sensors_get();

        // Track workout HR stats
        if (_state == STATE_WORKOUT && data.hr_valid && data.heart_rate > 0) {
            _hrSum += data.heart_rate;
            _hrSamples++;
            if (data.heart_rate > _maxHR) _maxHR = data.heart_rate;
        }
    }

    // Update display
    if (now - _lastDisplayMs >= 500) {
        _lastDisplayMs = now;

        // Auto-off after idle timeout
        if (now - _lastActivityMs > DISPLAY_TIMEOUT_MS) {
            display_off();
        }

        SensorData data  = sensors_get();
        int elapsed_s    = (_state == STATE_WORKOUT) ? (now - _workoutStartMs) / 1000 : 0;
        float dist_m     = (_state == STATE_WORKOUT) ? gps_route_distance_m() : 0;

        display_update(data, wifi_is_connected(), false, elapsed_s, dist_m);
    }

    // Periodic sync
    if (now - _lastSyncMs >= SYNC_INTERVAL_MS) {
        _lastSyncMs = now;

        // Queue a passive reading
        wifi_sync_reading(sensors_get());

        bool ok = wifi_flush();
        if (!ok && !wifi_is_connected()) {
            wifi_connect();     // attempt reconnect
        }
    }
}
