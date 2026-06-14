#pragma once

// ── WiFi ──────────────────────────────────────────────────────────────────
// Normally you DON'T edit these — the watch pairs with your phone's hotspot
// on first boot (or after a long-press of button A) and stores the network in
// flash. These are only a fallback: leave them as "YourSSID" to force pairing,
// or hard-code a network here to skip the portal entirely.
#define WIFI_SSID       "YourSSID"
#define WIFI_PASSWORD   "YourPassword"

// ── Backend ───────────────────────────────────────────────────────────────
// Production: https://pymite6941-data-analyst-ai-agent.hf.space
// Local:      http://192.168.x.x:8000
#define API_BASE_URL    "https://pymite6941-data-analyst-ai-agent.hf.space"

// Clerk session token — generate from browser devtools (localStorage → __clerk_db_jwt)
// Lasts ~1 month. Replace when expired.
#define CLERK_TOKEN     "your_clerk_session_token_here"

// ── Hardware Pins (ESP32-C3 SuperMini) ─────────────────────────────────────
// Matches watch/wiring.md. All three I2C devices share GPIO 8 (SDA) / 9 (SCL).
#define PIN_SDA          8      // I2C SDA — MPU6050 + MAX30102 + OLED (shared)
#define PIN_SCL          9      // I2C SCL — shared
#define PIN_BUTTON_A     4      // "Back" button  — tap: cycle screen, hold: Wi-Fi pairing
#define PIN_BUTTON_B     5      // "Select" button — start/stop workout
#define PIN_GPS_RX       6      // UART1 RX ← GPS TX  (optional NEO-6M; not in base wiring)
#define PIN_GPS_TX       7      // UART1 TX → GPS RX  (unused by the NEO-6M)
#define PIN_BATT_ADC     3      // ADC1 channel — battery divider (optional; unused in firmware)
#define PIN_VIBE        -1      // Vibration motor (optional). -1 = none wired (disabled)
// If the two buttons feel swapped, exchange the GPIO numbers on BUTTON_A / BUTTON_B.

// ── OLED ──────────────────────────────────────────────────────────────────
#define OLED_ADDR       0x3C
#define OLED_WIDTH      128
#define OLED_HEIGHT     64

// ── Intervals ─────────────────────────────────────────────────────────────
#define SENSOR_INTERVAL_MS      1000    // Workout HR-stat roll-up period (raw HR/step sampling runs every loop)
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
