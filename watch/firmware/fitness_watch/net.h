/* net.h — settings, clock, WiFi, captive-portal pairing and cloud sync.
 *
 * The ESP32 firmware originally talked to nothing but its I2C sensors. This
 * module bridges the watch to the FitnessAI backend so heart-rate/step samples
 * reach the user's account, and lets a phone pair the watch via a self-hosted
 * captive portal. Everything network-ish lives here so fitness_watch.ino stays
 * a UI/sensor loop.
 *
 * Lifecycle (all driven from the main loop):
 *   - Not paired      -> pairingBegin()/pairingLoop() serve the setup portal.
 *   - Paired          -> wifiSetup(), wifiTick() keep the phone-hotspot link up;
 *                        syncTick() queues readings and uploads batches.
 *
 * Two plain-threads are used by net.cpp and must be kept non-blocking so the
 * watch can sample the MAX30105/MPU6050 fast enough.
 */

#pragma once

#include <Arduino.h>

// ── Settings (NVS-backed) ────────────────────────────────────────────────────
struct WatchSettings {
  bool   paired;     // true once a token is stored AND connectivity verified
  char   ssid[33];   // home / phone-hotspot name the watch connects to
  char   pass[65];   // that hotspot's password
  char   token[64];  // the "fit_…" device token from the web app
  char   name[32];   // device label shown in the app's Devices list
  char   apPass[65]; // WPA2 key for the phone-link SoftAP (from the web settings)
};

bool settingsLoaded();
const WatchSettings &settings();
WatchSettings &settingsMut();                            // mutable handle — call settingsSave() after editing
void settingsLoad();                                 // pulls NVS into RAM (idempotent)
void settingsSave();                                 // pushes RAM into NVS
void settingsClear();                                // factory reset -> unpaired

// ── Phone link SoftAP ────────────────────────────────────────────────────────
// The watch raises its own WPA2 access point for a phone to join, rather than
// both having to be on someone else's network. The phone keeps its internet
// over cellular and reaches the watch on the AP for local traffic, so it can
// drain the queue and push schedules down without any shared infrastructure.
//
// This is NOT the provisioning portal (pairingBegin) -- that one is open and
// exists only to collect credentials.
bool linkApBegin();          // raise the AP; false if no usable password is set
void linkApEnd();            // drop it and release the radio
bool linkApActive();
int  linkApClients();        // phones currently joined
const char *linkApSsid();    // "FitnessAI-A4B1C2"
void linkApTick();           // idle timeout; call from the main loop

// ── Clock (NTP) ──────────────────────────────────────────────────────────────
void  clockBegin();          // start NTP sync attempt (call after WiFi up)
bool  clockSynced();         // got a real epoch yet?
time_t clockNow();           // epoch seconds (0 if not synced)
const char *clockIso(time_t t);  // static buffer: "YYYY-MM-DDTHH:MM:SSZ" (UTC)

// ── WiFi station (normal operation) ──────────────────────────────────────────
void wifiSetup(const char *ssid, const char *pass); // WiFi.begin + autoreconnect
bool wifiConnected();
void wifiTick();                                   // throttled background re-AT
const char *wifiIp();                             // "A.B.C.D" or "-"
void wifiOff();                                    // radio down (critical-battery load shed)

// ── Pairing portal ───────────────────────────────────────────────────────────
void pairingBegin();                              // boot SoftAP + DNS + HTTP setup page
void pairingLoop();                               // pump the servers (call each loop)
bool pairingActive();                             // portal still running?
bool pairingBusy();                               // a connect/verify is in progress
void pairingStop();                               // tear down AP (after successful pair)
const char *pairingSsid();                         // the AP SSID this watch broadcasts
const char *pairingIp();                          // the portal IP the phone browses to

// ── Cloud sync ───────────────────────────────────────────────────────────────
void syncBegin();                                 // reset counters/queue (call after pair)
int  syncQueued();                               // readings buffered, not yet uploaded
uint32_t syncUploaded();                         // readings acknowledged by the backend
int  syncLastHttp();                              // last HTTP status (0 = never / failure)
bool syncBusy();                                  // an upload is mid-flight
void syncTick(time_t ts, int hr, uint32_t steps); // sample once/min + throttle uploads
void syncWant();                                  // kick an upload ASAP (serial 's')
void syncReset();                                 // clear queue + counters

// ── Pair-apply (shared by the BLE controller and the setup portal) ───────────
// "Apply" = connect the saved WiFi, verify the backend is reachable, then mark
// paired. Async: pump netTick() every loop. Driven by the BLE `CMD=apply` char
// or after the captive-portal form save.
void netApply();              // begin the apply using the currently saved settings
bool netApplying();
const char *netApplyStatus();  // "connecting", "verifying", "paired", "error"
void netTick();                // pump background net state (call each main loop)

// ── Serial diagnostics (driven by fitness_watch.ino) ────────────────────────
void logWatch();                                  // one-line status used by serial + SYNC screen