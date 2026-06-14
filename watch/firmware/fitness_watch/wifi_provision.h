#pragma once
#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Wi-Fi provisioning — lets the watch pair with a phone's personal hotspot
//  without re-flashing. Credentials are stored in NVS (flash) and survive
//  reboots. On first boot (or when the user long-presses to re-pair) the watch
//  raises its own open access point + captive portal; the phone joins it, a
//  setup page opens, and the user types in their hotspot's name + password.
// ─────────────────────────────────────────────────────────────────────────────

// SoftAP name the phone connects to, and the address of the setup page.
extern const char* WIFI_PROV_AP_SSID;
extern const char* WIFI_PROV_URL;

// True if a hotspot SSID has been saved to flash.
bool wifi_prov_has_creds();

// Load saved credentials into the caller's strings. Returns true if an SSID
// was stored (password may be empty for open hotspots).
bool wifi_prov_load(String& ssid, String& pass);

// Persist credentials to flash, and wipe them.
void wifi_prov_save(const String& ssid, const String& pass);
void wifi_prov_clear();

// Run the captive-portal pairing flow. Brings up the SoftAP + web server,
// renders the pairing instructions on the OLED, and blocks — pumping the
// portal — until either:
//   * the user submits credentials AND the watch joins that network  → true
//   * allow_skip is true and BTN_A is pressed (use the watch offline) → false
// The AP and web server are always torn down before returning.
bool wifi_prov_portal(bool allow_skip);
