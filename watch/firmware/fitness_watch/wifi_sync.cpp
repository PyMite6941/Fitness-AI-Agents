#include "wifi_sync.h"
#include "config.h"
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
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("[wifi] Connecting to %s", WIFI_SSID);
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

// ── Flush (HTTP POST) ─────────────────────────────────────────────────────

bool wifi_flush() {
    if (!_buf_mutex) return false;
    if (xSemaphoreTake(_buf_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;

    if (_reading_count == 0 && _workout_count == 0) {
        xSemaphoreGive(_buf_mutex);
        return true;
    }

    // Snapshot the payload while holding the mutex, then release
    JsonDocument payload;
    payload["device"]   = DEVICE_NAME;
    payload["readings"] = _readings_buf;
    payload["workouts"] = _workouts_buf;

    // Serialise to fixed char buffer to avoid String heap churn
    static char body[4096];
    size_t written = serializeJson(payload, body, sizeof(body));
    if (written == 0 || written >= sizeof(body)) {
        Serial.println("[wifi] Payload too large — truncated, skipping sync");
        xSemaphoreGive(_buf_mutex);
        return false;
    }

    xSemaphoreGive(_buf_mutex);

    if (!wifi_is_connected()) {
        Serial.println("[wifi] No connection — skipping sync");
        return false;
    }

    HTTPClient http;
    http.begin(String(API_BASE_URL) + "/watch/sync");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", String("Bearer ") + CLERK_TOKEN);
    http.setTimeout(10000);

    int code = http.POST((uint8_t*)body, written);
    bool ok = (code == 200);
    Serial.printf("[wifi] Sync → HTTP %d (%d readings, %d workouts)\n",
                  code, _reading_count, _workout_count);
    http.end();

    if (ok) {
        if (xSemaphoreTake(_buf_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            _readings_buf.clear();
            _workouts_buf.clear();
            _reading_count = 0;
            _workout_count = 0;
            xSemaphoreGive(_buf_mutex);
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
    // Pin sync task to core 0; main loop runs on core 1
    xTaskCreatePinnedToCore(_sync_task_fn, "wifi_sync", 8192, nullptr, 1, &_syncHandle, 0);
    Serial.println("[wifi] Sync task started on core 0");
}

bool wifi_flush_async() {
    if (!_syncHandle || _syncing) return false;
    xTaskNotifyGive(_syncHandle);
    return true;
}
