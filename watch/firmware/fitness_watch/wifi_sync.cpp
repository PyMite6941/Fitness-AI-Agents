#include "wifi_sync.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// Simple in-memory queue — holds up to 60 readings and 10 workouts between syncs
#define MAX_READINGS  60
#define MAX_WORKOUTS  10

static JsonDocument _readings_buf;
static JsonDocument _workouts_buf;
static int _reading_count  = 0;
static int _workout_count  = 0;

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
    uint8_t tries = 0;
    while (now < 1000000000L && tries < 10) {
        delay(500);
        time(&now);
        tries++;
        Serial.print(".");
    }
    Serial.println(now > 1000000000L ? " OK" : " TIMEOUT");
}

String wifi_iso_now() {
    time_t now;
    time(&now);
    struct tm* t = gmtime(&now);
    char buf[25];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", t);
    return String(buf);
}

void wifi_sync_reading(const SensorData& data) {
    if (_reading_count >= MAX_READINGS) return;
    JsonObject r = _readings_buf.add<JsonObject>();
    r["timestamp"]    = wifi_iso_now();
    if (data.hr_valid && data.heart_rate > 0) r["heart_rate"] = (int)data.heart_rate;
    r["steps"]        = data.steps;
    _reading_count++;
}

void wifi_sync_workout(const WorkoutRecord& w) {
    if (_workout_count >= MAX_WORKOUTS) return;
    JsonObject wj = _workouts_buf.add<JsonObject>();
    wj["timestamp"]        = w.started_at;
    wj["workout_type"]     = w.workout_type;
    wj["duration_minutes"] = (float)(w.ended_at.length() > 0 ? 1 : 0); // calculated below
    if (w.avg_heart_rate > 0)   wj["avg_heart_rate"]  = w.avg_heart_rate;
    if (w.max_heart_rate > 0)   wj["max_heart_rate"]  = w.max_heart_rate;
    if (w.calories_burned > 0)  wj["calories_burned"] = w.calories_burned;
    if (w.distance_meters > 0)  wj["distance_meters"] = w.distance_meters;

    // Derive duration from ISO timestamps
    struct tm s = {}, e = {};
    strptime(w.started_at.c_str(), "%Y-%m-%dT%H:%M:%SZ", &s);
    strptime(w.ended_at.c_str(),   "%Y-%m-%dT%H:%M:%SZ", &e);
    double dur_min = difftime(mktime(&e), mktime(&s)) / 60.0;
    wj["duration_minutes"] = dur_min > 0 ? dur_min : 1.0;

    _workout_count++;
}

bool wifi_flush() {
    if (_reading_count == 0 && _workout_count == 0) return true;
    if (!wifi_is_connected()) {
        Serial.println("[wifi] No connection — skipping sync");
        return false;
    }

    JsonDocument payload;
    payload["device"] = DEVICE_NAME;
    payload["readings"]  = _readings_buf;
    payload["workouts"]  = _workouts_buf;

    String body;
    serializeJson(payload, body);

    HTTPClient http;
    http.begin(String(API_BASE_URL) + "/watch/sync");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", String("Bearer ") + CLERK_TOKEN);
    http.setTimeout(10000);

    int code = http.POST(body);
    bool ok = (code == 200);
    Serial.printf("[wifi] Sync → HTTP %d (%d readings, %d workouts)\n",
                  code, _reading_count, _workout_count);
    http.end();

    if (ok) {
        _readings_buf.clear();
        _workouts_buf.clear();
        _reading_count = 0;
        _workout_count = 0;
    }
    return ok;
}
