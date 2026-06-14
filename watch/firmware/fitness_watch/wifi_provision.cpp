#include "wifi_provision.h"
#include "config.h"
#include "display.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

const char* WIFI_PROV_AP_SSID = "FitnessWatch-Setup";
const char* WIFI_PROV_URL     = "192.168.4.1";

static Preferences _prefs;
static WebServer   _server(80);
static DNSServer   _dns;

// Set by the /save handler (same task as the portal loop — no locking needed).
static volatile bool _submitted = false;
static String        _newSsid;
static String        _newPass;

// ── NVS credential storage ──────────────────────────────────────────────────

bool wifi_prov_load(String& ssid, String& pass) {
    _prefs.begin("wifiprov", true);   // read-only
    ssid = _prefs.getString("ssid", "");
    pass = _prefs.getString("pass", "");
    _prefs.end();
    return ssid.length() > 0;
}

bool wifi_prov_has_creds() {
    String s, p;
    return wifi_prov_load(s, p);
}

void wifi_prov_save(const String& ssid, const String& pass) {
    _prefs.begin("wifiprov", false);
    _prefs.putString("ssid", ssid);
    _prefs.putString("pass", pass);
    _prefs.end();
}

void wifi_prov_clear() {
    _prefs.begin("wifiprov", false);
    _prefs.clear();
    _prefs.end();
}

// ── Captive-portal web handlers ─────────────────────────────────────────────

static void _handle_root() {
    static const char PAGE[] PROGMEM =
        "<!DOCTYPE html><html><head>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>Fitness Watch Setup</title><style>"
        "body{font-family:sans-serif;background:#0f172a;color:#e2e8f0;margin:0;padding:24px}"
        "h2{margin:0 0 4px}label{font-size:14px;color:#94a3b8}"
        "input{width:100%;box-sizing:border-box;padding:11px;margin:6px 0 16px;"
        "border-radius:10px;border:1px solid #334155;background:#1e293b;color:#fff;font-size:16px}"
        "button{width:100%;padding:13px;border:0;border-radius:10px;background:#16a34a;"
        "color:#fff;font-size:16px;font-weight:600}"
        ".note{font-size:13px;color:#94a3b8;margin-top:18px;line-height:1.5}"
        "</style></head><body>"
        "<h2>&#8986; Pair your watch</h2>"
        "<form method=POST action=/save>"
        "<label>Phone hotspot name (SSID)</label>"
        "<input name=ssid placeholder='My iPhone' required>"
        "<label>Hotspot password</label>"
        "<input name=pass type=password placeholder='hotspot password'>"
        "<button type=submit>Save &amp; Connect</button></form>"
        "<p class=note>Enter the name &amp; password of <b>your phone's personal "
        "hotspot</b>. After saving, leave this page, switch this Wi-Fi off and turn "
        "your hotspot <b>ON</b> &mdash; the watch will join it automatically.</p>"
        "</body></html>";
    _server.send_P(200, "text/html", PAGE);
}

static void _handle_save() {
    _newSsid = _server.arg("ssid");
    _newPass = _server.arg("pass");
    _submitted = true;
    _server.send(200, "text/html",
        "<!DOCTYPE html><html><head><meta name=viewport "
        "content='width=device-width,initial-scale=1'></head>"
        "<body style='font-family:sans-serif;background:#0f172a;color:#e2e8f0;padding:24px'>"
        "<h2>Saved &#10003;</h2><p>Now switch this Wi-Fi off and enable your phone's "
        "hotspot. The watch will connect on its own.</p></body></html>");
}

static void _portal_cleanup() {
    _server.stop();
    _dns.stop();
    WiFi.softAPdisconnect(true);   // drop the setup AP; keep any STA link
}

// ── Pairing flow ────────────────────────────────────────────────────────────

bool wifi_prov_portal(bool allow_skip) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(WIFI_PROV_AP_SSID);          // open network — no password to type
    delay(100);
    IPAddress ip = WiFi.softAPIP();          // 192.168.4.1

    _dns.start(53, "*", ip);                 // redirect every lookup → captive portal
    _server.on("/", _handle_root);
    _server.on("/save", HTTP_POST, _handle_save);
    _server.onNotFound(_handle_root);        // any other path opens the form too
    _server.begin();

    _submitted = false;
    display_pairing(WIFI_PROV_AP_SSID, WIFI_PROV_URL);
    Serial.printf("[prov] Portal up — join '%s', open http://%s\n",
                  WIFI_PROV_AP_SSID, ip.toString().c_str());

    // Don't treat the still-held button (used to *enter* re-pair) as a skip —
    // only honour a skip after button A has been released at least once.
    bool skip_armed = false;

    for (;;) {
        _dns.processNextRequest();
        _server.handleClient();

        if (_submitted) {
            _submitted = false;
            wifi_prov_save(_newSsid, _newPass);
            Serial.printf("[prov] Saved SSID '%s' — connecting\n", _newSsid.c_str());
            display_centered("Saved!", "Turn ON hotspot");

            WiFi.begin(_newSsid.c_str(), _newPass.c_str());
            unsigned long t0 = millis();
            while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 25000) {
                _dns.processNextRequest();
                _server.handleClient();
                delay(50);
            }

            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("[prov] Connected: %s\n", WiFi.localIP().toString().c_str());
                display_centered("Connected!", WiFi.localIP().toString().c_str());
                delay(1200);
                _portal_cleanup();
                return true;
            }

            Serial.println("[prov] Connect failed — back to portal");
            display_centered("No connection", "Check & retry");
            delay(1500);
            display_pairing(WIFI_PROV_AP_SSID, WIFI_PROV_URL);
        }

        // Skip pairing to use the watch offline (clock / HR / steps still work).
        if (allow_skip) {
            if (digitalRead(PIN_BUTTON_A) == HIGH) skip_armed = true;
            if (skip_armed && digitalRead(PIN_BUTTON_A) == LOW) {
                delay(40);
                if (digitalRead(PIN_BUTTON_A) == LOW) {
                    Serial.println("[prov] Pairing skipped — offline mode");
                    display_centered("Offline mode", "");
                    delay(900);
                    _portal_cleanup();
                    return false;
                }
            }
        }
        delay(5);
    }
}
