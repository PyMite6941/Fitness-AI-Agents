#include "config.h"
#include "sensors.h"
#include "display.h"
#include "wifi_sync.h"
#include "gps_tracker.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ── Forward declarations ───────────────────────────────────────────────────
static void _start_workout();
static void _finish_workout();
static void _post_route(const WorkoutRecord& w);

// ── State ─────────────────────────────────────────────────────────────────
enum AppState { STATE_IDLE, STATE_WORKOUT };
static AppState _state = STATE_IDLE;

static unsigned long _lastSensorMs   = 0;
static unsigned long _lastSyncMs     = 0;
static unsigned long _lastDisplayMs  = 0;
static unsigned long _lastActivityMs = 0;

// Workout tracking
static unsigned long _workoutStartMs  = 0;
static char          _workoutStartISO[25] = {};
static float         _maxHR      = 0;
static float         _hrSum      = 0;
static int           _hrSamples  = 0;
static int           _stepsAtStart = 0;

// Button debounce
static unsigned long _btnALast = 0;
static unsigned long _btnBLast = 0;
static bool          _btnAWas  = HIGH;
static bool          _btnBWas  = HIGH;
#define BTN_DEBOUNCE_MS 200

// ── Workout helpers ────────────────────────────────────────────────────────

static void _start_workout() {
    _state          = STATE_WORKOUT;
    _workoutStartMs = millis();
    wifi_iso_now_buf(_workoutStartISO, sizeof(_workoutStartISO));
    _maxHR        = 0;
    _hrSum        = 0;
    _hrSamples    = 0;
    _stepsAtStart = sensors_get().steps;
    sensors_reset_steps();
    gps_start_route();
    display_set_mode(MODE_WORKOUT);
    display_notify("Workout started!");
    Serial.println("[app] Workout started");
}

static void _finish_workout() {
    gps_stop_route();

    char endISO[25];
    wifi_iso_now_buf(endISO, sizeof(endISO));

    int elapsed_s = (int)((millis() - _workoutStartMs) / 1000UL);

    WorkoutRecord w = {};
    strncpy(w.workout_type, "running", sizeof(w.workout_type) - 1);
    strncpy(w.started_at,   _workoutStartISO, sizeof(w.started_at) - 1);
    strncpy(w.ended_at,     endISO,           sizeof(w.ended_at)   - 1);
    w.avg_heart_rate  = (_hrSamples > 0) ? (_hrSum / _hrSamples) : 0;
    w.max_heart_rate  = _maxHR;
    w.distance_meters = gps_route_distance_m();
    w.steps           = sensors_get().steps;

    // Rough calorie estimate: MET 8 × assumed weight 70 kg × hours
    w.calories_burned = 8.0f * 70.0f * (elapsed_s / 3600.0f);

    wifi_sync_workout(w);

    if (gps_point_count() > 1) {
        _post_route(w);
    }

    _state = STATE_IDLE;
    display_set_mode(MODE_CLOCK);
    display_notify("Saved!");
    Serial.printf("[app] Workout done: %.0fm, %ds, HR avg %.0f\n",
                  w.distance_meters, elapsed_s, w.avg_heart_rate);
}

static void _post_route(const WorkoutRecord& w) {
    if (!wifi_is_connected()) {
        Serial.println("[app] No WiFi — route not posted");
        return;
    }

    // Serialise into a fixed buffer to avoid heap fragmentation
    static char body[8192];
    JsonDocument doc;
    doc["workout_type"] = w.workout_type;
    doc["started_at"]   = w.started_at;
    doc["ended_at"]     = w.ended_at;
    // Embed pre-serialised coordinates array as raw JSON
    doc["coordinates"]  = serialized(gps_route_json());

    size_t written = serializeJson(doc, body, sizeof(body));
    if (written == 0 || written >= sizeof(body)) {
        Serial.println("[app] Route JSON too large");
        return;
    }

    HTTPClient http;
    http.begin(String(API_BASE_URL) + "/routes/");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", String("Bearer ") + CLERK_TOKEN);
    http.setTimeout(10000);
    int code = http.POST((uint8_t*)body, written);
    Serial.printf("[app] Route POST → HTTP %d\n", code);
    http.end();
}

// ── Button handling ────────────────────────────────────────────────────────

static void _check_buttons() {
    unsigned long now = millis();

    bool aVal = digitalRead(PIN_BUTTON_A);
    if (_btnAWas == HIGH && aVal == LOW && (now - _btnALast) > BTN_DEBOUNCE_MS) {
        _btnALast = now;
        _lastActivityMs = now;
        display_cycle_mode();
        display_on();
    }
    _btnAWas = aVal;

    bool bVal = digitalRead(PIN_BUTTON_B);
    if (_btnBWas == HIGH && bVal == LOW && (now - _btnBLast) > BTN_DEBOUNCE_MS) {
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
    if (PIN_VIBE >= 0) {
        pinMode(PIN_VIBE, OUTPUT);
        digitalWrite(PIN_VIBE, LOW);
    }

    bool dispOk    = display_init();
    bool sensorsOk = sensors_init();
    bool gpsOk     = gps_init();

    if (dispOk) display_notify("Connecting WiFi...", 3000);

    bool wifiOk = wifi_connect();

    // Start background sync task AFTER WiFi so it can reconnect if needed
    wifi_init_sync_task();

    if (dispOk) display_notify(wifiOk ? "WiFi OK!" : "No WiFi", 1500);

    Serial.printf("[app] Ready — display:%d sensors:%d gps:%d wifi:%d\n",
                  dispOk, sensorsOk, gpsOk, wifiOk);

    _lastActivityMs = millis();
}

// ── Loop ──────────────────────────────────────────────────────────────────

void loop() {
    unsigned long now = millis();

    _check_buttons();
    gps_update();

    // ── Sensor read ────────────────────────────────────────────────────
    if ((now - _lastSensorMs) >= SENSOR_INTERVAL_MS) {
        _lastSensorMs = now;
        sensors_update();

        // Accumulate HR stats during workout
        if (_state == STATE_WORKOUT) {
            const SensorData& d = sensors_get();
            if (d.hr_valid && d.heart_rate > 0) {
                _hrSum += d.heart_rate;
                _hrSamples++;
                if (d.heart_rate > _maxHR) _maxHR = d.heart_rate;
            }
        }
    }

    // ── Display update ─────────────────────────────────────────────────
    if ((now - _lastDisplayMs) >= 500) {
        _lastDisplayMs = now;

        if ((now - _lastActivityMs) > DISPLAY_TIMEOUT_MS) {
            display_off();
        } else {
            const SensorData& d = sensors_get();
            int elapsed_s   = (_state == STATE_WORKOUT) ? (int)((now - _workoutStartMs) / 1000UL) : 0;
            float dist_m    = (_state == STATE_WORKOUT) ? gps_route_distance_m() : 0.0f;
            display_update(d, wifi_is_connected(), wifi_is_syncing(), elapsed_s, dist_m);
        }
    }

    // ── Periodic sync (non-blocking) ───────────────────────────────────
    if ((now - _lastSyncMs) >= SYNC_INTERVAL_MS) {
        _lastSyncMs = now;
        wifi_sync_reading(sensors_get());   // queue current reading
        wifi_flush_async();                 // hand off to background task
    }
}
