#pragma once

// ── WiFi ──────────────────────────────────────────────────────────────────
#define WIFI_SSID       "YourSSID"
#define WIFI_PASSWORD   "YourPassword"

// ── Backend ───────────────────────────────────────────────────────────────
// Production: https://pymite6941-data-analyst-ai-agent.hf.space
// Local:      http://192.168.x.x:8000
#define API_BASE_URL    "https://pymite6941-data-analyst-ai-agent.hf.space"

// Clerk session token — generate from browser devtools (localStorage → __clerk_db_jwt)
// Lasts ~1 month. Replace when expired.
#define CLERK_TOKEN     "your_clerk_session_token_here"

// ── Hardware Pins (ESP32) ──────────────────────────────────────────────────
#define PIN_SDA         21
#define PIN_SCL         22
#define PIN_GPS_RX      16      // UART2 RX ← GPS TX
#define PIN_GPS_TX      17      // UART2 TX → GPS RX (unused)
#define PIN_BUTTON_A     0      // Boot button — cycle display mode
#define PIN_BUTTON_B    35      // Optional second button — start/stop workout
#define PIN_BATT_ADC    34      // Battery voltage divider (3.3V ref, 100k+100k)
#define PIN_VIBE        25      // Vibration motor (optional)

// ── OLED ──────────────────────────────────────────────────────────────────
#define OLED_ADDR       0x3C
#define OLED_WIDTH      128
#define OLED_HEIGHT     64

// ── Intervals ─────────────────────────────────────────────────────────────
#define SENSOR_INTERVAL_MS      1000    // Read sensors every 1 s
#define SYNC_INTERVAL_MS       300000   // Sync to backend every 5 min
#define GPS_POINT_INTERVAL_MS    5000   // Record GPS point every 5 s during workout
#define DISPLAY_TIMEOUT_MS      15000   // Screen off after 15 s idle

// ── Step counting ─────────────────────────────────────────────────────────
#define STEP_THRESHOLD      1.2f    // g-force threshold for step detection
#define STEP_DEBOUNCE_MS     300    // Min ms between steps

// ── Misc ──────────────────────────────────────────────────────────────────
#define DEVICE_NAME     "fitness_watch_esp32"
#define NTP_SERVER      "pool.ntp.org"
#define TZ_OFFSET_SEC   0       // UTC offset in seconds (e.g. 25200 for UTC+7)
