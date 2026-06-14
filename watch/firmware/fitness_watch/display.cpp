#include "display.h"
#include "config.h"
#include "icons.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <cstring>

static Adafruit_SSD1306 _oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
static DisplayMode _mode = MODE_CLOCK;
static bool _on = true;

static unsigned long _notifyUntil = 0;
static char _notifyMsg[32] = {};

bool display_init() {
    Wire.begin(PIN_SDA, PIN_SCL);   // ensure I2C is on the configured pins first
    if (!_oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("[display] SSD1306 not found");
        return false;
    }
    _oled.clearDisplay();
    _oled.setTextColor(SSD1306_WHITE);
    _oled.display();
    Serial.println("[display] SSD1306 OK");
    return true;
}

void display_set_mode(DisplayMode m) { _mode = m; }
DisplayMode display_get_mode()       { return _mode; }
void display_cycle_mode()            { _mode = (DisplayMode)((_mode + 1) % MODE_COUNT); }
void display_off()                   { _oled.ssd1306_command(SSD1306_DISPLAYOFF); _on = false; }
void display_on()                    { _oled.ssd1306_command(SSD1306_DISPLAYON);  _on = true; }

void display_notify(const char* msg, uint16_t ms) {
    strncpy(_notifyMsg, msg, sizeof(_notifyMsg) - 1);
    _notifyUntil = millis() + ms;
}

static void _draw_status_bar(bool wifi_ok, bool syncing) {
    // Wi-Fi state, top-right corner
    const int wx = OLED_WIDTH - ICON_WIFI_W;
    if (wifi_ok) {
        _oled.drawBitmap(wx, 0, icon_wifi, ICON_WIFI_W, ICON_WIFI_H, SSD1306_WHITE);
        // Active-sync indicator sits just to the left of the bars
        if (syncing) {
            _oled.drawBitmap(wx - ICON_SYNCMIN_W - 2, 0,
                             icon_sync_min, ICON_SYNCMIN_W, ICON_SYNCMIN_H, SSD1306_WHITE);
        }
    } else {
        _oled.drawBitmap(wx, 0, icon_wifi_off, ICON_WIFI_W, ICON_WIFI_H, SSD1306_WHITE);
    }
}

static void _draw_hr(float hr, bool valid) {
    _oled.drawBitmap(4, 22, icon_heart, ICON16_W, ICON16_H, SSD1306_WHITE);
    _oled.setTextSize(3);
    _oled.setCursor(28, 18);
    if (valid && hr > 0) {
        _oled.printf("%3.0f", hr);
    } else {
        _oled.print("---");
    }
    _oled.setTextSize(1);
    _oled.setCursor(4, 56);
    _oled.print("BPM  place finger");
}

static void _draw_steps(int steps) {
    _oled.drawBitmap(4, 22, icon_steps, ICON16_W, ICON16_H, SSD1306_WHITE);
    _oled.setTextSize(3);
    _oled.setCursor(26, 18);
    // Cap display at 99999 — higher values compress to "100k+"
    if (steps <= 99999) _oled.printf("%5d", steps);
    else                _oled.print("100k+");
    _oled.setTextSize(1);
    _oled.setCursor(4, 56);
    _oled.print("STEPS");
}

static void _draw_clock() {
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    _oled.drawBitmap(2, 0, icon_clock, ICON16_W, ICON16_H, SSD1306_WHITE);
    _oled.setTextSize(3);
    _oled.setCursor(8, 18);
    _oled.printf("%02d:%02d", t->tm_hour, t->tm_min);
    _oled.setTextSize(1);
    _oled.setCursor(20, 52);
    char buf[16];
    strftime(buf, sizeof(buf), "%a %b %d", t);
    _oled.print(buf);
}

static void _draw_workout(int seconds, float dist_m, float hr) {
    int m = seconds / 60, s = seconds % 60;
    _oled.drawBitmap(4, 2, icon_run, ICON16_W, ICON16_H, SSD1306_WHITE);
    _oled.setTextSize(2);
    _oled.setCursor(26, 4);
    _oled.printf("%02d:%02d", m, s);
    _oled.setTextSize(1);
    _oled.setCursor(4, 28);
    if (dist_m >= 1000) _oled.printf("%.2f km", dist_m / 1000.0f);
    else                _oled.printf("%.0f m",  dist_m);
    _oled.setCursor(4, 42);
    if (hr > 0) _oled.printf("HR: %.0f bpm", hr);
    else        _oled.print("HR: ---");
    _oled.setCursor(4, 56);
    if (dist_m > 0 && seconds > 0) {
        float pace = (seconds / 60.0f) / (dist_m / 1000.0f);
        int pm = (int)pace, ps = (int)((pace - pm) * 60);
        _oled.printf("Pace %d:%02d /km", pm, ps);
    }
}

static void _draw_sync(bool syncing) {
    _oled.drawBitmap((OLED_WIDTH - ICON16_W) / 2, 6, icon_sync,
                     ICON16_W, ICON16_H, SSD1306_WHITE);
    _oled.setTextSize(1);
    const char* line = syncing ? "Syncing..." : "Press B to sync";
    int16_t x1, y1; uint16_t w, h;
    _oled.getTextBounds(line, 0, 0, &x1, &y1, &w, &h);
    _oled.setCursor((OLED_WIDTH - w) / 2, 34);
    _oled.print(line);
    _oled.getTextBounds("to Fitness AI", 0, 0, &x1, &y1, &w, &h);
    _oled.setCursor((OLED_WIDTH - w) / 2, 48);
    _oled.print("to Fitness AI");
}

void display_update(const SensorData& data, bool wifi_ok, bool syncing,
                    int workout_seconds, float workout_dist_m) {
    if (!_on) return;

    _oled.clearDisplay();

    // Notification overlay
    if (millis() < _notifyUntil) {
        _oled.setTextSize(1);
        _oled.setCursor(4, 24);
        _oled.print(_notifyMsg);
        _oled.display();
        return;
    }

    _draw_status_bar(wifi_ok, syncing);

    switch (_mode) {
        case MODE_CLOCK:   _draw_clock(); break;
        case MODE_HEART:   _draw_hr(data.heart_rate, data.hr_valid); break;
        case MODE_STEPS:   _draw_steps(data.steps); break;
        case MODE_WORKOUT: _draw_workout(workout_seconds, workout_dist_m, data.heart_rate); break;
        case MODE_SYNC:    _draw_sync(syncing); break;
        default: break;
    }

    _oled.display();
}

// ── Full-screen pairing / status screens ────────────────────────────────────

void display_pairing(const char* ap_ssid, const char* url) {
    _on = true;
    _oled.ssd1306_command(SSD1306_DISPLAYON);
    _oled.clearDisplay();
    _oled.drawBitmap(2, 0, icon_wifi, ICON_WIFI_W, ICON_WIFI_H, SSD1306_WHITE);
    _oled.setTextSize(1);
    _oled.setCursor(18, 0);
    _oled.print("PAIR WATCH");
    _oled.drawFastHLine(0, 11, OLED_WIDTH, SSD1306_WHITE);
    _oled.setCursor(0, 16);
    _oled.print("1 Join Wi-Fi:");
    _oled.setCursor(8, 26);
    _oled.print(ap_ssid);
    _oled.setCursor(0, 40);
    _oled.print("2 Open browser:");
    _oled.setCursor(8, 50);
    _oled.print(url);
    _oled.display();
}

void display_centered(const char* line1, const char* line2) {
    _on = true;
    _oled.ssd1306_command(SSD1306_DISPLAYON);
    _oled.clearDisplay();
    _oled.setTextSize(1);
    int16_t x1, y1; uint16_t w, h;
    if (line1 && line1[0]) {
        _oled.getTextBounds(line1, 0, 0, &x1, &y1, &w, &h);
        _oled.setCursor((OLED_WIDTH - w) / 2, 24);
        _oled.print(line1);
    }
    if (line2 && line2[0]) {
        _oled.getTextBounds(line2, 0, 0, &x1, &y1, &w, &h);
        _oled.setCursor((OLED_WIDTH - w) / 2, 40);
        _oled.print(line2);
    }
    _oled.display();
}
