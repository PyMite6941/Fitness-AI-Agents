/* power.cpp — battery monitor. See power.h for the contract.
 *
 * Why analogReadMilliVolts() and not analogRead(): the ESP32-C3's ADC is quite
 * non-linear and varies part-to-part. analogReadMilliVolts() applies the
 * per-chip calibration burned into eFuse and returns real millivolts, which
 * removes most of the error a raw-count conversion would carry.
 */

#include "power.h"
#include "config.h"
#include <Arduino.h>

#define DBG(fmt, ...) do { if (DEBUG_SERIAL) Serial.printf("[pwr] " fmt "\n", ##__VA_ARGS__); } while (0)

static bool     g_valid   = false;
static int      g_mv      = 0;      // cell millivolts
static uint32_t g_lastMs  = 0;
#if PIN_USB_SENSE >= 0
static int      g_busMv   = 0;      // 5V-rail millivolts (only when that pin is wired)
#endif

// ── Li-ion discharge curve ───────────────────────────────────────────────────
// A single cell spends most of its life between 3.7 V and 4.0 V, so a straight
// linear map from 3.3–4.2 V badly misreports the middle of the range (it would
// call a half-empty cell ~55%). This is the usual resting-voltage curve,
// interpolated between points.
struct SocPoint { int mv; int pct; };
static const SocPoint SOC[] = {
  {4200, 100}, {4100, 90}, {4000, 80}, {3930, 70}, {3870, 60}, {3820, 50},
  {3790, 40},  {3770, 30}, {3740, 20}, {3680, 10}, {3450,  5}, {3300,  0},
};
static const int SOC_N = (int)(sizeof(SOC) / sizeof(SOC[0]));

static int socFromMv(int mv) {
  if (mv >= SOC[0].mv) return 100;
  if (mv <= SOC[SOC_N - 1].mv) return 0;
  for (int i = 0; i < SOC_N - 1; i++) {
    if (mv <= SOC[i].mv && mv > SOC[i + 1].mv) {
      int spanMv  = SOC[i].mv  - SOC[i + 1].mv;
      int spanPct = SOC[i].pct - SOC[i + 1].pct;
      if (spanMv <= 0) return SOC[i + 1].pct;
      return SOC[i + 1].pct + ((mv - SOC[i + 1].mv) * spanPct) / spanMv;
    }
  }
  return 0;
}

// Average several conversions — a single ADC1 sample on the C3 is noisy, and
// the divider's high impedance makes it worse. Only compiled when a pin is
// actually configured, otherwise it is an unused static (-Wunused-function).
#if PIN_BATT_ADC >= 0 || PIN_USB_SENSE >= 0
static int readPinMv(int pin, float ratio) {
  uint32_t sum = 0;
  for (int i = 0; i < BATT_SAMPLES; i++) sum += analogReadMilliVolts(pin);
  return (int)((sum / (float)BATT_SAMPLES) * ratio * BATT_CAL_SCALE);
}
#endif

void powerBegin() {
#if PIN_BATT_ADC >= 0
  // 11 dB attenuation puts the ADC's usable ceiling near 2.5 V, which is where
  // a 2:1 divider lands a full 4.2 V cell (2.1 V).
  analogSetPinAttenuation(PIN_BATT_ADC, ADC_11db);
  DBG("battery monitor on GPIO%d (divider %.2fx)", PIN_BATT_ADC, (double)BATT_DIVIDER_RATIO);
#else
  DBG("battery monitor disabled (PIN_BATT_ADC = -1) — UI will show USB");
#endif
#if PIN_USB_SENSE >= 0
  analogSetPinAttenuation(PIN_USB_SENSE, ADC_11db);
  DBG("USB sense on GPIO%d", PIN_USB_SENSE);
#endif
  g_lastMs = 0;   // force a sample on the first tick
  powerTick();
}

void powerTick() {
#if PIN_BATT_ADC >= 0
  uint32_t now = millis();
  if (g_valid && (now - g_lastMs) < BATT_INTERVAL_MS) return;
  g_lastMs = now;

  g_mv = readPinMv(PIN_BATT_ADC, BATT_DIVIDER_RATIO);
#if PIN_USB_SENSE >= 0
  g_busMv = readPinMv(PIN_USB_SENSE, USB_DIVIDER_RATIO);
#endif
  g_valid = true;

  DBG("cell %d mV (%d%%)%s", g_mv, battPercent(), usbPresent() ? " USB" : "");
#endif
}

bool battValid()      { return g_valid; }
int  battMilliVolts() { return g_valid ? g_mv : 0; }
int  battPercent()    { return g_valid ? socFromMv(g_mv) : -1; }

bool usbPresent() {
#if PIN_USB_SENSE >= 0
  // A real measurement of the 5V rail — the only way to know for sure.
  return g_busMv > USB_PRESENT_MV;
#else
  // No sense pin: fall back to a heuristic. A resting cell cannot sit above
  // 4.2 V, so anything higher means the charger is holding it up. This CANNOT
  // distinguish "USB in, battery full" from "USB in, battery charging in CV",
  // and it reads a freshly-charged cell as USB for a few minutes. Wire
  // PIN_USB_SENSE if that matters.
  return g_valid && g_mv > BATT_USB_MV;
#endif
}

bool battLow()      { return g_valid && !usbPresent() && g_mv < BATT_LOW_MV; }
bool battCritical() { return g_valid && !usbPresent() && g_mv < BATT_CRIT_MV; }

const char *battLabel() {
  static char buf[8];
  if (!g_valid) return "USB";        // monitor disabled — don't claim a fake level
  if (usbPresent()) {
    // With a USB-sense pin we can tell charging from topped-off.
#if PIN_USB_SENSE >= 0
    return (g_mv < BATT_FULL_MV - 60) ? "CHG" : "USB";
#else
    return "USB";
#endif
  }
  if (battLow()) return "LOW";
  snprintf(buf, sizeof(buf), "%d%%", battPercent());
  return buf;
}

const char *battDetail() {
  static char buf[24];
  if (!g_valid) return "Battery: not wired";
  snprintf(buf, sizeof(buf), "%d.%02dV %d%% %s",
           g_mv / 1000, (g_mv % 1000) / 10, battPercent(),
           usbPresent() ? "USB" : "bat");
  return buf;
}
