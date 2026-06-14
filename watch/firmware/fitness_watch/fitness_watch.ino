#include "config.h"
#include "sensors.h"
#include "display.h"
#include "wifi_sync.h"
#include "wifi_provision.h"
#include "gps_tracker.h"
#include <Arduino.h>

// ── Forward declarations ───────────────────────────────────────────────────
static void _start_workout();
static void _finish_workout();

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

// Button debounce / hold tracking
static unsigned long _btnBLast    = 0;
static bool          _btnBWas     = HIGH;
static bool          _btnAWas     = HIGH;
static unsigned long _btnADownMs  = 0;
static bool          _btnAHold    = false;
#define BTN_DEBOUNCE_MS 200
#define BTN_HOLD_MS    1200    // hold button A this long → Wi-Fi pairing

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
    strncpy(w.workout_type, "running",        sizeof(w.workout_type) - 1);
    strncpy(w.started_at,   _workoutStartISO, sizeof(w.started_at)   - 1);
    strncpy(w.ended_at,     endISO,           sizeof(w.ended_at)     - 1);
    w.avg_heart_rate = (_hrSamples > 0) ? (_hrSum / _hrSamples) : 0;
    w.max_heart_rate = _maxHR;
    w.steps          = sensors_get().steps;
    // distance + calories are computed by the backend now (no haversine/MET math
    // on the FPU-less C3, and no hardcoded body weight) — left at 0 here.

    if (gps_point_count() > 1) {
        // GPS run: /routes/ derives distance, pace and calories and records the
        // workout. Queue the route; the background task uploads it (non-blocking).
        wifi_queue_route(w);
    } else {
        // No GPS fix: send a plain workout summary; the backend estimates
        // calories from duration + type.
        wifi_sync_workout(w);
    }
    wifi_flush_async();   // upload promptly, off the main loop

    _state = STATE_IDLE;
    display_set_mode(MODE_CLOCK);
    display_notify("Saved!");
    Serial.printf("[app] Workout done: %ds, HR avg %.0f, %d GPS pts\n",
                  elapsed_s, w.avg_heart_rate, gps_point_count());
}

// ── Button handling ────────────────────────────────────────────────────────

static void _check_buttons() {
    unsigned long now = millis();

    bool aVal = digitalRead(PIN_BUTTON_A);
    if (_btnAWas == HIGH && aVal == LOW) {       // press begins
        _btnADownMs = now;
        _btnAHold   = false;
    }
    if (aVal == LOW && !_btnAHold && _btnADownMs != 0 &&
        (now - _btnADownMs) > BTN_HOLD_MS) {     // held → Wi-Fi pairing
        _btnAHold = true;
        _lastActivityMs = now;
        display_centered("Re-pair Wi-Fi", "release button");
        if (wifi_prov_portal(true)) wifi_set_time_ntp();
        _lastSyncMs    = millis();               // avoid an immediate sync burst
        _lastActivityMs = millis();
    }
    if (_btnAWas == LOW && aVal == HIGH) {       // release
        if (!_btnAHold && (now - _btnADownMs) > BTN_DEBOUNCE_MS) {
            _lastActivityMs = now;
            display_cycle_mode();                // short tap → next screen
            display_on();
        }
        _btnADownMs = 0;
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

    bool wifiOk = false;
    if (wifi_prov_has_creds()) {
        // Already paired with a hotspot — just reconnect.
        if (dispOk) display_notify("Connecting WiFi...", 2500);
        wifiOk = wifi_connect();
    } else {
        // First boot — run the pairing portal. Skippable (BTN A) for offline use.
        wifiOk = wifi_prov_portal(true);
        if (wifiOk) wifi_set_time_ntp();
    }

    // Start background sync task AFTER WiFi so it can reconnect if needed
    wifi_init_sync_task();

    if (dispOk) display_notify(wifiOk ? "WiFi OK!" : "Offline mode", 1500);

    Serial.printf("[app] Ready — display:%d sensors:%d gps:%d wifi:%d\n",
                  dispOk, sensorsOk, gpsOk, wifiOk);

    _lastActivityMs = millis();
}

// ── Loop ──────────────────────────────────────────────────────────────────

void loop() {
    unsigned long now = millis();

    _check_buttons();
    gps_update();

    // ── Sensor sampling ─────────────────────────────────────────────────
    // MUST run every loop. The MAX30102 beat detector and the accelerometer
    // step detector need to see the raw waveform at ~100 Hz — sampling once a
    // second (the old behaviour) detects neither a heartbeat nor a step.
    sensors_update();

    // Aggregate workout HR stats at 1 Hz. Only this slow roll-up is gated;
    // the fast beat/step detection happens in sensors_update() above.
    if ((now - _lastSensorMs) >= SENSOR_INTERVAL_MS) {
        _lastSensorMs = now;
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

    // Yield ~1 ms: lets the FreeRTOS IDLE task run (feeds the task watchdog,
    // frees memory) and gives the background Wi-Fi sync task CPU. The C3 is
    // single-core, so without this a tight loop can starve both. 1 ms still
    // leaves the sensor loop running at hundreds of Hz.
    delay(1);
}
