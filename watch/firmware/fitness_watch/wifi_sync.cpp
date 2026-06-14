#include "wifi_sync.h"
#include "config.h"
#include "wifi_provision.h"
#include "gps_tracker.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <time.h>
#include <cstring>

#define MAX_READINGS  60
#define MAX_WORKOUTS  10

// Buffer access is shared between main loop (writers) and sync task (reader/flusher)
static SemaphoreHandle_t _buf_mutex  = nullptr;
static TaskHandle_t      _syncHandle = nullptr;
static volatile bool     _syncing    = false;

static JsonDocument _readings_buf;
static JsonDocument _workouts_buf;
static int _reading_count = 0;
static int _workout_count = 0;

// Pre-serialised GPS route awaiting upload to /routes/. Built in main-loop
// context (wifi_queue_route), POSTed by the background task (wifi_flush).
// 16 KB holds ~220 coordinate points (~18 min at one point / 5 s); longer
// routes are dropped with a log line rather than truncated.
static char          _route_body[16384];
static size_t        _route_len     = 0;
static volatile bool _route_pending = false;

// ── Time helpers ──────────────────────────────────────────────────────────

void wifi_iso_now_buf(char* buf, size_t len) {
    time_t now;
    time(&now);
    struct tm* t = gmtime(&now);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", t);
}

String wifi_iso_now() {
    char buf[25];
    wifi_iso_now_buf(buf, sizeof(buf));
    return String(buf);
}

// ── WiFi ──────────────────────────────────────────────────────────────────

bool wifi_connect() {
    // Prefer the hotspot credentials saved during pairing; fall back to the
    // compile-time defaults in config.h only if they've been customised.
    String ssid, pass;
    if (!wifi_prov_load(ssid, pass)) {
        ssid = WIFI_SSID;
        pass = WIFI_PASSWORD;
    }
    if (ssid.length() == 0 || ssid == "YourSSID") {
        Serial.println("[wifi] No saved network — long-press button A to pair");
        return false;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    Serial.printf("[wifi] Connecting to %s", ssid.c_str());
    uint8_t tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 20) {
        delay(500);
        Serial.print(".");
        tries++;
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(" FAILED");
        return false;
    }
    Serial.printf(" OK (%s)\n", WiFi.localIP().toString().c_str());
    wifi_set_time_ntp();
    return true;
}

bool wifi_is_connected() {
    return WiFi.status() == WL_CONNECTED;
}

void wifi_set_time_ntp() {
    configTime(TZ_OFFSET_SEC, 0, NTP_SERVER);
    Serial.print("[wifi] NTP sync");
    time_t now = 0;
    for (uint8_t i = 0; i < 10 && now < 1000000000L; i++) {
        delay(500);
        time(&now);
        Serial.print(".");
    }
    Serial.println(now > 1000000000L ? " OK" : " TIMEOUT");
}

bool wifi_is_syncing() { return _syncing; }

// ── Buffer writers (called from main loop) ────────────────────────────────

void wifi_sync_reading(const SensorData& data) {
    if (!_buf_mutex) return;
    if (xSemaphoreTake(_buf_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;

    if (_reading_count < MAX_READINGS) {
        JsonObject r = _readings_buf.add<JsonObject>();
        char ts[25];
        wifi_iso_now_buf(ts, sizeof(ts));
        r["timestamp"] = ts;
        if (data.hr_valid && data.heart_rate > 0) r["heart_rate"] = (int)data.heart_rate;
        r["steps"] = data.steps;
        _reading_count++;
    }

    xSemaphoreGive(_buf_mutex);
}

void wifi_sync_workout(const WorkoutRecord& w) {
    if (!_buf_mutex) return;
    if (xSemaphoreTake(_buf_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;

    if (_workout_count < MAX_WORKOUTS) {
        JsonObject wj = _workouts_buf.add<JsonObject>();
        wj["timestamp"]    = w.started_at;
        wj["workout_type"] = w.workout_type;

        // Calculate duration from ISO timestamps
        struct tm s = {}, e = {};
        strptime(w.started_at, "%Y-%m-%dT%H:%M:%SZ", &s);
        strptime(w.ended_at,   "%Y-%m-%dT%H:%M:%SZ", &e);
        double dur_min = difftime(mktime(&e), mktime(&s)) / 60.0;
        wj["duration_minutes"] = (dur_min > 0) ? dur_min : 1.0;

        if (w.avg_heart_rate  > 0) wj["avg_heart_rate"]  = w.avg_heart_rate;
        if (w.max_heart_rate  > 0) wj["max_heart_rate"]  = w.max_heart_rate;
        if (w.calories_burned > 0) wj["calories_burned"] = w.calories_burned;
        if (w.distance_meters > 0) wj["distance_meters"] = w.distance_meters;
        _workout_count++;
    }

    xSemaphoreGive(_buf_mutex);
}

void wifi_queue_route(const WorkoutRecord& w) {
    if (!_buf_mutex) return;
    if (gps_point_count() < 2) return;     // nothing worth a route
    if (xSemaphoreTake(_buf_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;

    JsonDocument doc;
    doc["workout_type"]   = w.workout_type;
    doc["started_at"]     = w.started_at;
    doc["ended_at"]       = w.ended_at;
    doc["record_workout"] = true;   // route-only post — backend records the workout
    if (w.avg_heart_rate > 0) doc["avg_heart_rate"] = w.avg_heart_rate;
    if (w.max_heart_rate > 0) doc["max_heart_rate"] = w.max_heart_rate;
    // gps_route_json() returns the coordinates array; serialized() embeds it as
    // raw JSON (ArduinoJson copies the String contents into the document).
    doc["coordinates"]  = serialized(gps_route_json());

    _route_len = serializeJson(doc, _route_body, sizeof(_route_body));
    if (_route_len == 0 || _route_len >= sizeof(_route_body)) {
        Serial.println("[wifi] Route JSON too large — not queued");
        _route_len     = 0;
        _route_pending = false;
    } else {
        _route_pending = true;
    }
    xSemaphoreGive(_buf_mutex);
}

// ── Flush (HTTP POST) ─────────────────────────────────────────────────────

static int _http_post_json(const char* path, const uint8_t* body, size_t len) {
    if (!wifi_is_connected()) return -1;
    HTTPClient http;
    http.begin(String(API_BASE_URL) + path);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", String("Bearer ") + CLERK_TOKEN);
    http.setTimeout(10000);
    int code = http.POST((uint8_t*)body, len);
    http.end();
    return code;
}

bool wifi_flush() {
    if (!_buf_mutex) return false;
    bool ok = true;

    // ── Readings + workouts → /watch/sync ──────────────────────────────
    if (xSemaphoreTake(_buf_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        static char body[4096];
        size_t written  = 0;
        bool   haveBatch = (_reading_count > 0 || _workout_count > 0);
        if (haveBatch) {
            JsonDocument payload;
            payload["device"]   = DEVICE_NAME;
            payload["readings"] = _readings_buf;
            payload["workouts"] = _workouts_buf;
            written = serializeJson(payload, body, sizeof(body));
            if (written == 0 || written >= sizeof(body)) {
                Serial.println("[wifi] Sync payload too large — skipping");
                haveBatch = false;
                ok = false;
            }
        }
        int rcount = _reading_count, wcount = _workout_count;
        xSemaphoreGive(_buf_mutex);

        if (haveBatch) {
            int code = _http_post_json("/watch/sync", (uint8_t*)body, written);
            Serial.printf("[wifi] Sync → HTTP %d (%d readings, %d workouts)\n",
                          code, rcount, wcount);
            if (code == 200) {
                if (xSemaphoreTake(_buf_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    _readings_buf.clear();
                    _workouts_buf.clear();
                    _reading_count = 0;
                    _workout_count = 0;
                    xSemaphoreGive(_buf_mutex);
                }
            } else {
                ok = false;
            }
        }
    } else {
        ok = false;
    }

    // ── Queued GPS route → /routes/ ────────────────────────────────────
    if (_route_pending) {
        int code = _http_post_json("/routes/", (uint8_t*)_route_body, _route_len);
        Serial.printf("[wifi] Route → HTTP %d (%u bytes)\n", code, (unsigned)_route_len);
        if (code == 200 || code == 201) {
            if (xSemaphoreTake(_buf_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                _route_pending = false;
                _route_len     = 0;
                xSemaphoreGive(_buf_mutex);
            }
        } else {
            ok = false;
        }
    }

    return ok;
}

// ── FreeRTOS background sync task ─────────────────────────────────────────

static void _sync_task_fn(void*) {
    for (;;) {
        // Block until main loop signals a sync is needed
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        _syncing = true;
        if (!wifi_is_connected()) wifi_connect();
        wifi_flush();
        _syncing = false;
    }
}

void wifi_init_sync_task() {
    _buf_mutex = xSemaphoreCreateMutex();
    // The ESP32-C3 is single-core, so there is no second core to offload to —
    // the old "main loop runs on core 1" assumption doesn't hold. Use
    // tskNO_AFFINITY and let the scheduler time-slice this against the main
    // loop (and, on a dual-core ESP32, run it on whichever core is free). The
    // task is blocked on a notify almost all the time, so it's cheap.
    xTaskCreatePinnedToCore(_sync_task_fn, "wifi_sync", 8192, nullptr, 1, &_syncHandle, tskNO_AFFINITY);
    Serial.println("[wifi] Sync task started");
}

bool wifi_flush_async() {
    if (!_syncHandle || _syncing) return false;
    xTaskNotifyGive(_syncHandle);
    return true;
}
