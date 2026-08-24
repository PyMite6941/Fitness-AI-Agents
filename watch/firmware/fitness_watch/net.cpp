/* net.cpp — settings, clock, WiFi, captive-portal pairing and cloud sync.
 * See net.h for the contract. Nothing here blocks for long: WiFi re-connects and
 * uploads are throttled so the sensor polling in fitness_watch.ino stays fast.
 */

#include "net.h"
#include "config.h"

#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#define DBG(fmt, ...) do { Serial.printf("[net] " fmt "\n", ##__VA_ARGS__); } while (0)

// ── Settings (NVS-backed) ────────────────────────────────────────────────────
static WatchSettings g_settings;
static bool g_loaded = false;

bool settingsLoaded() { return g_loaded; }

const WatchSettings &settings() {
  if (!g_loaded) settingsLoad();
  return g_settings;
}

WatchSettings &settingsMut() {
  if (!g_loaded) settingsLoad();
  return g_settings;
}

void settingsLoad() {
  if (g_loaded) return;
  g_loaded = true;
  memset(&g_settings, 0, sizeof(g_settings));
  strncpy(g_settings.name, "fitness_watch", sizeof(g_settings.name) - 1);

  Preferences p;
  p.begin(NVS_NAMESPACE, true /* read-only */);
  g_settings.paired = p.getBool(NVS_KEY_PAIRED, false);
  p.getString(NVS_KEY_SSID, g_settings.ssid, sizeof(g_settings.ssid));
  p.getString(NVS_KEY_PASS, g_settings.pass, sizeof(g_settings.pass));
  p.getString(NVS_KEY_TOKEN, g_settings.token, sizeof(g_settings.token));
  p.getString(NVS_KEY_NAME, g_settings.name, sizeof(g_settings.name));
  p.getString(NVS_KEY_APPASS, g_settings.apPass, sizeof(g_settings.apPass));
  p.end();

  DBG("settings: paired=%d ssid=\"%s\" token=%s",
      g_settings.paired, g_settings.ssid,
      g_settings.token[0] ? "set" : "empty");
}

void settingsSave() {
  Preferences p;
  p.begin(NVS_NAMESPACE, false /* read-write */);
  p.putBool(NVS_KEY_PAIRED, g_settings.paired);
  p.putString(NVS_KEY_SSID, g_settings.ssid);
  p.putString(NVS_KEY_PASS, g_settings.pass);
  p.putString(NVS_KEY_TOKEN, g_settings.token);
  p.putString(NVS_KEY_NAME, g_settings.name);
  p.putString(NVS_KEY_APPASS, g_settings.apPass);
  p.end();
  DBG("settings saved (paired=%d)", g_settings.paired);
}

void settingsClear() {
  Preferences p;
  p.begin(NVS_NAMESPACE, false);
  p.clear();
  p.end();
  g_loaded = false;
  memset(&g_settings, 0, sizeof(g_settings));
  strncpy(g_settings.name, "fitness_watch", sizeof(g_settings.name) - 1);
  DBG("settings cleared");
}

// ── Phone link SoftAP ────────────────────────────────────────────────────────
// The watch broadcasts its own WPA2 network for a phone to join. The phone keeps
// its internet over cellular and uses this link purely for local traffic to the
// watch, so no shared router is involved and no credentials for someone else's
// network are needed.
//
// Kept deliberately separate from pairingBegin()'s provisioning portal: that one
// is OPEN by design because its whole job is to collect credentials from someone
// who has none yet. This one must never be open.

static bool     g_linkApUp      = false;
static int      g_linkApClients = 0;
static uint32_t g_linkApUpMs    = 0;
static uint32_t g_linkApLastJoin= 0;
static char     g_linkSsid[33]  = {0};

const char *linkApSsid()  { return g_linkSsid; }
bool linkApActive()       { return g_linkApUp; }
int  linkApClients()      { return g_linkApClients; }

// Station join/leave arrive as WiFi events, so the watch knows a phone is on the
// network without polling for it -- this is the "autodetect" half.
static void linkApEvent(arduino_event_id_t event, arduino_event_info_t info) {
  if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
    g_linkApClients = WiFi.softAPgetStationNum();
    g_linkApLastJoin = millis();
    DBG("link AP: phone joined (%d client%s)",
        g_linkApClients, g_linkApClients == 1 ? "" : "s");
  } else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
    g_linkApClients = WiFi.softAPgetStationNum();
    DBG("link AP: phone left (%d client%s)",
        g_linkApClients, g_linkApClients == 1 ? "" : "s");
  }
}

bool linkApBegin() {
  if (g_linkApUp) return true;

  const char *pw = settings().apPass;
  // WPA2 has an 8-character floor. softAP() would silently fall back to an OPEN
  // network on a shorter key, which is worse than not starting at all -- anyone
  // in range could then join and reach the watch.
  if (strlen(pw) < LINK_AP_MIN_PASS) {
    Serial.printf("[net] link AP refused: password is %u chars, need >= %d.\n",
                  (unsigned)strlen(pw), LINK_AP_MIN_PASS);
    Serial.println("[net] set it from the web app's device settings, or "
                   "`ap <password>` on this console.");
    return false;
  }

  // Suffix the SSID with the low 3 bytes of the MAC so two watches in the same
  // room are distinguishable.
  uint8_t mac[6] = {0};
  WiFi.macAddress(mac);
  snprintf(g_linkSsid, sizeof(g_linkSsid), "%s-%02X%02X%02X",
           LINK_AP_PREFIX, mac[3], mac[4], mac[5]);

  WiFi.onEvent(linkApEvent, ARDUINO_EVENT_WIFI_AP_STACONNECTED);
  WiFi.onEvent(linkApEvent, ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);

  // AP_STA rather than AP: the watch keeps the option of its own uplink, which
  // is what lets it sync directly when a known network is also in range.
  if (WiFi.getMode() != WIFI_AP_STA) WiFi.mode(WIFI_AP_STA);
  IPAddress ip(PORTAL_IP);
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  if (!WiFi.softAP(g_linkSsid, pw)) {
    Serial.println("[net] link AP: softAP() failed");
    return false;
  }

  g_linkApUp = true;
  g_linkApUpMs = millis();
  g_linkApLastJoin = 0;
  g_linkApClients = 0;
  Serial.printf("[net] link AP up: SSID \"%s\" at %s\n",
                g_linkSsid, ip.toString().c_str());
  return true;
}

void linkApEnd() {
  if (!g_linkApUp) return;
  WiFi.softAPdisconnect(true);
  g_linkApUp = false;
  g_linkApClients = 0;
  DBG("link AP down");
}

// A SoftAP holds the radio in continuous receive (~93 mA on this chip), which is
// the most expensive thing a battery watch can do. Drop it again if no phone
// turns up, so an accidental activation cannot quietly flatten the cell.
void linkApTick() {
  if (!g_linkApUp || LINK_AP_IDLE_MS == 0) return;
  if (g_linkApClients > 0) return;                 // someone is using it
  uint32_t since = g_linkApLastJoin ? g_linkApLastJoin : g_linkApUpMs;
  if (millis() - since >= (uint32_t)LINK_AP_IDLE_MS) {
    Serial.println("[net] link AP idle -> dropping to save power");
    linkApEnd();
  }
}

// ── Clock (NTP) ──────────────────────────────────────────────────────────────
void clockBegin() {
  // UTC only — the backend stores instants, display timezone is the user's job.
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  DBG("clockBegin: NTP sync requested");
}

bool clockSynced() {
  // A real epoch is >= 2020; anything else means the RTC hasn't seen NTP yet.
  return time(nullptr) > 1577836800L;
}

time_t clockNow() { return time(nullptr); }

const char *clockIso(time_t t) {
  // "+00:00" instead of "Z" — accepted by every Python/Pydantic parse path.
  static char buf[32];
  struct tm *tmv = gmtime(&t);
  if (!tmv) {   // gmtime returns NULL for out-of-range t — don't deref it
    snprintf(buf, sizeof(buf), "1970-01-01T00:00:00+00:00");
    return buf;
  }
  snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d+00:00",
           tmv->tm_year + 1900, tmv->tm_mon + 1, tmv->tm_mday,
           tmv->tm_hour, tmv->tm_min, tmv->tm_sec);
  return buf;
}

// ── WiFi station (normal operation) ──────────────────────────────────────────
void wifiSetup(const char *ssid, const char *pass) {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  // Modem sleep ON. This was the single most expensive line in the firmware:
  // setSleep(false) pins the radio awake between packets, which costs ~70-80 mA
  // CONTINUOUSLY for the entire time the watch is associated — more than every
  // other component put together. With sleep enabled the radio wakes on the
  // AP's DTIM beacons instead.
  //
  // Nothing here needs the radio hot. The watch only ever makes OUTBOUND
  // requests (a batched POST every 5 minutes); transmits wake the radio anyway,
  // so throughput and reliability are unchanged. It accepts no inbound
  // connections in station mode, so the extra inbound latency costs nothing.
  WiFi.setSleep(true);
  WiFi.begin(ssid, pass);
  DBG("wifiSetup(%s)", ssid);
}

bool wifiConnected() { return WiFi.status() == WL_CONNECTED; }

static uint32_t g_wifiTickMs = 0;
void wifiTick() {
  if (wifiConnected()) return;
  uint32_t now = millis();
  if (now - g_wifiTickMs < 15000) return;   // 15 s between reconnect attempts
  g_wifiTickMs = now;
  if (!settings().ssid[0]) return;
  if (WiFi.getMode() != WIFI_STA) WiFi.mode(WIFI_STA);
  WiFi.begin(settings().ssid, settings().pass);
  DBG("wifiTick: STA retry");
}

void wifiOff() {
  // Idempotent — called every loop() while the battery is critical.
  if (WiFi.getMode() == WIFI_OFF) return;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  DBG("wifiOff: radio down");
}

const char *wifiIp() {
  static char ip[16];
  IPAddress a = WiFi.localIP();
  if (a[0] == 0) { strcpy(ip, "-"); return ip; }
  snprintf(ip, sizeof(ip), "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
  return ip;
}

// ── Pairing portal ───────────────────────────────────────────────────────────
static WebServer  g_server(80);
static DNSServer  g_dns;
static bool       g_portalUp = false;
static bool       g_connectPending = false;
static bool       g_backendChecked = false;
static bool       g_backendOk = false;
static uint32_t   g_backendCheckMs = 0;
static uint32_t   g_pairedSince = 0;
static char       g_apSsid[24];
static const uint32_t AP_GRACE_MS = 12000;  // keep the portal up after pairing so the phone sees the result

const char *pairingSsid() { return g_apSsid; }

const char *pairingIp() {
  static char ip[16];
  IPAddress a(PORTAL_IP);
  snprintf(ip, sizeof(ip), "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
  return ip;
}

// A NULL CA cert makes HTTPS connections insecure (no cert pinning) — the
// device token still authorizes every call; the trade-off is documented.
static bool isBackendUp() {
  HTTPClient http;
  http.setTimeout(4000);
  http.begin(String(BACKEND_URL) + "/health", (const char *)NULL);
  int code = http.GET();
  http.end();
  DBG("backend health -> %d", code);
  return code == 200;
}

static void backendCheck() {
  g_backendOk = isBackendUp();
}

static String htmlEscape(const char *s) {
  String out;
  if (!s) return out;
  for (const char *p = s; *p; p++) {
    switch (*p) {
      case '&':  out += "&amp;";  break;
      case '<':  out += "&lt;";   break;
      case '>':  out += "&gt;";   break;
      case '"':  out += "&quot;"; break;
      default:   out += *p;
    }
  }
  return out;
}

static String pageShell(const String &title) {
  String p = "<!doctype html><html><head><meta charset=utf-8>"
             "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
             "<title>FitnessAI Watch</title>"
             "<style>"
             "body{font-family:system-ui;background:#111;color:#eee;margin:0;padding:18px;max-width:460px}"
             "h1{color:#ff3c00;font-size:22px;margin:0 0 6px}"
             "p{color:#aaa;font-size:14px;line-height:1.5;margin:0 0 14px}"
             "label{display:block;font-size:13px;color:#bbb;margin:14px 0 4px}"
             "input{width:100%;box-sizing:border-box;padding:11px;border:1px solid #444;"
             "border-radius:8px;background:#1c1c1c;color:#eee;font-size:16px}"
             "button{width:100%;margin-top:22px;padding:13px;border:0;border-radius:8px;"
             "background:#ff3c00;color:#fff;font-size:16px;font-weight:700}"
             ".ok{color:#3ddc84}.err{color:#ff6b6b}"
             "</style></head><body><h1>FitnessAI Watch</h1>";
  p += title;
  p += "</body></html>";
  return p;
}

static void handleRoot() {
  g_server.sendHeader("Cache-Control", "no-cache");
  String p = pageShell(
    "<p>Enter your phone-hotspot WiFi so the watch can reach the internet, plus the "
    "pairing code from <b>fitness-ai-agents.vercel.app &rarr; Devices</b>.</p>"
    "<form method=post action=/save>"
    "<label>Wi-Fi SSID</label><input name=ssid value=\"" + htmlEscape(settings().ssid) + "\">"
    "<label>Wi-Fi password</label><input name=pass value=\"" + htmlEscape(settings().pass) + "\">"
    "<label>Device token (fit_&hellip;)</label><input name=token value=\"" + htmlEscape(settings().token) + "\">"
    "<label>Device name (optional)</label><input name=name value=\"" + htmlEscape(settings().name) + "\">"
    "<button>Save &amp; connect</button>"
    "</form>");
  g_server.send(200, "text/html", p);
}

static void handleSave() {
  g_server.sendHeader("Cache-Control", "no-cache");

  if (!g_server.hasArg("ssid") || !g_server.hasArg("pass") || !g_server.hasArg("token")) {
    g_server.send(200, "text/html", pageShell("<p class=err>Form was incomplete — go back and fill all three fields.</p>"));
    return;
  }
  String ssid = g_server.arg("ssid");
  String pass = g_server.arg("pass");
  String tok  = g_server.arg("token");
  String name = g_server.hasArg("name") ? g_server.arg("name") : String("fitness_watch");

  if (ssid.length() == 0 || tok.length() == 0) {
    g_server.send(200, "text/html", pageShell("<p class=err>SSID and device token are required.</p>"));
    return;
  }
  if (!tok.startsWith("fit_")) {
    g_server.send(200, "text/html", pageShell(
      "<p class=err>The token must start with <b>fit_</b>. Re-check the code on the Devices page.</p>"));
    return;
  }

  WatchSettings &s = g_settings;
  s.paired = false;
  strncpy(s.ssid, ssid.c_str(), sizeof(s.ssid) - 1); s.ssid[sizeof(s.ssid) - 1] = 0;
  strncpy(s.pass, pass.c_str(), sizeof(s.pass) - 1); s.pass[sizeof(s.pass) - 1] = 0;
  strncpy(s.token, tok.c_str(), sizeof(s.token) - 1); s.token[sizeof(s.token) - 1] = 0;
  strncpy(s.name, name.c_str(), sizeof(s.name) - 1); s.name[sizeof(s.name) - 1] = 0;
  settingsSave();

  g_connectPending = true;
  g_backendChecked = false;
  g_backendOk = false;
  g_backendCheckMs = 0;

  // STA connect while the AP stays up so the phone can watch progress.
  // setSleep(false) is deliberate HERE and only here: pairing is a few seconds
  // long with a user staring at a status page, and running AP + STA together is
  // exactly the case where dozing the radio makes the portal feel broken. The
  // steady-state path in wifiSetup() enables modem sleep.
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.begin(s.ssid, s.pass);
  DBG("portal save: connecting STA to %s", s.ssid);

  g_server.send(200, "text/html", pageShell(
    "<p>Saved. Connecting to <b>" + htmlEscape(s.ssid) + "</b> &mdash; "
    "this page updates by itself. It may take ~20 seconds.</p>"
    "<meta http-equiv=refresh content=2>"));
}

static void handleStatus() {
  g_server.sendHeader("Cache-Control", "no-cache");

  // If a connect is in progress and the STA is up, verify the backend once.
  if (g_connectPending && wifiConnected() && !g_backendChecked &&
      millis() - g_backendCheckMs >= 3000) {
    g_backendCheckMs = millis();
    backendCheck();
    g_backendChecked = true;
  }

  if (wifiConnected() && g_backendOk) {
    if (!g_settings.paired) {
      g_settings.paired = true;
      settingsSave();
      g_pairedSince = millis();
      DBG("pairing complete -> paired");
    }
    g_server.send(200, "text/html", pageShell(
      "<p class=ok>&#10003; <b>Paired!</b> The watch is connected and reaches the FitnessAI "
      "backend. You can close this page &mdash; the watch's own network will switch off "
      "shortly.</p>"));
    return;
  }
  if (wifiConnected()) {
    g_server.send(200, "text/html", pageShell(
      "<p>WiFi connected to <b>" + htmlEscape(g_settings.ssid) + "</b>.<br>"
      "Checking backend &hellip;</p><meta http-equiv=refresh content=2>"));
    return;
  }
  if (g_connectPending) {
    g_server.send(200, "text/html", pageShell(
      "<p>Connecting to <b>" + htmlEscape(g_settings.ssid) + "</b> &hellip;<br>"
      "Double-check the password if this takes more than 30s.</p>"
      "<meta http-equiv=refresh content=2>"));
    return;
  }
  g_server.send(200, "text/html", pageShell(
    "<p>Not configured yet &mdash; fill in the form.</p>"));
}

static void handleAny() {
  // Every unknown path (captive-portal probes from iOS/Android/Windows included)
  // ends up on the setup page.
  g_server.sendHeader("Location", "/", true);
  g_server.send(302, "text/html", "");
}

void pairingBegin() {
  if (g_portalUp) return;

  snprintf(g_apSsid, sizeof(g_apSsid), "%s", AP_SSID_NAME);

  WiFi.mode(WIFI_AP_STA);   // AP (portal) + STA (future uplink) simultaneously
  IPAddress ip(PORTAL_IP);
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  WiFi.softAP(g_apSsid, AP_PASSWORD);

  g_dns.start(53, "*", ip);
  g_server.on("/", HTTP_GET, handleRoot);
  g_server.on("/save", HTTP_POST, handleSave);
  g_server.on("/status", HTTP_GET, handleStatus);
  g_server.onNotFound(handleAny);
  g_server.begin();

  g_portalUp = true;
  g_connectPending = false;
  g_backendChecked = false;
  g_backendOk = false;
  DBG("portal up: SSID=%s IP=%s", g_apSsid, pairingIp());
}

void pairingLoop() {
  if (!g_portalUp) return;
  g_dns.processNextRequest();
  g_server.handleClient();

  // A fresh pair keeps the AP alive a grace period so the phone can read the
  // success page, then it shuts the portal down (STA link is unaffected).
  if (g_settings.paired && millis() - g_pairedSince >= AP_GRACE_MS) {
    pairingStop();
  }
}

void pairingStop() {
  g_dns.stop();
  g_server.stop();
  WiFi.mode(WIFI_STA);   // drop the AP, keep the station uplink
  g_portalUp = false;
  DBG("portal closed (STA retained, ip=%s)", wifiIp());
}

bool pairingActive() { return g_portalUp; }

bool pairingBusy() { return g_connectPending; }

// ── Cloud sync ───────────────────────────────────────────────────────────────
struct QueuedReading {
  uint32_t seq;    // monotonic id -- lets a relay ack a range idempotently
  time_t ts;
  float  hr;
  uint32_t steps;
};

static QueuedReading g_q[SYNC_QUEUE_MAX];
static int    g_qHead = 0;
static int    g_qCount = 0;
static uint32_t g_lastSampleMs = 0;
static uint32_t g_lastUploadMs = 0;
static uint32_t g_uploaded = 0;
static uint32_t g_nextSeq = 1;      // 0 is reserved for "nothing acked yet"
// Cumulative step count at the last enqueued reading. Readings carry the delta
// against this, because the backend sums the field — see syncTick().
static uint32_t g_stepsBaseline = 0;
static int    g_lastHttp = 0;
static bool   g_uploadBusy = false;
static bool   g_wantSync = false;

void syncBegin() {
  g_qHead = g_qCount = 0;
  g_uploaded = 0;
  g_lastHttp = 0;
  g_lastUploadMs = 0;
  DBG("sync reset");
}

int  syncQueued() { return g_qCount; }
uint32_t syncUploaded() { return g_uploaded; }
int  syncLastHttp() { return g_lastHttp; }
bool syncBusy() { return g_uploadBusy; }
void syncWant() { g_wantSync = true; }

void syncReset() {
  g_qHead = g_qCount = 0;
  g_uploaded = 0;
  g_lastHttp = 0;
}

static void enqueue(time_t ts, float hr, uint32_t steps) {
  if (g_qCount >= SYNC_QUEUE_MAX) {   // full: drop the oldest so data stays recent
    g_qHead = (g_qHead + 1) % SYNC_QUEUE_MAX;
    g_qCount--;
  }
  QueuedReading &r = g_q[(g_qHead + g_qCount) % SYNC_QUEUE_MAX];
  r.seq = g_nextSeq++;
  r.ts = ts;
  r.hr = hr;
  r.steps = steps;
  g_qCount++;
  DBG("queued reading #%d (hr=%d steps=%lu ts=%s)",
      g_qCount, (int)hr, (unsigned long)steps, clockIso(ts));
}

// ── Relay access to the queue (phone BLE bridge) ─────────────────────────────
// A phone can drain this queue over BLE and upload on the watch's behalf. The
// contract that makes that safe is END-TO-END acknowledgement: the watch keeps
// every reading until the relay confirms the BACKEND accepted it, not merely
// that BLE delivered it. A phone that receives a batch and then fails to upload
// loses nothing -- the watch still has it and simply sends it again.
//
// Sequence numbers make redelivery harmless: acking a range that was already
// dropped is a no-op, so a retry after a mid-transfer disconnect is safe.

int queueCount() { return g_qCount; }

bool queuePeek(int idx, uint32_t *seq, time_t *ts, float *hr, uint32_t *steps) {
  if (idx < 0 || idx >= g_qCount) return false;
  const QueuedReading &r = g_q[(g_qHead + idx) % SYNC_QUEUE_MAX];
  if (seq)   *seq = r.seq;
  if (ts)    *ts = r.ts;
  if (hr)    *hr = r.hr;
  if (steps) *steps = r.steps;
  return true;
}

// Drop everything up to and including `seq`. Called only once the relay has
// confirmed the backend took it.
int queueDropThrough(uint32_t seq) {
  int dropped = 0;
  while (g_qCount > 0) {
    const QueuedReading &r = g_q[g_qHead];
    if (r.seq > seq) break;              // reached data the relay has not acked
    g_qHead = (g_qHead + 1) % SYNC_QUEUE_MAX;
    g_qCount--;
    dropped++;
  }
  if (dropped) {
    g_uploaded += dropped;
    DBG("relay acked through seq %lu -> dropped %d, %d left",
        (unsigned long)seq, dropped, g_qCount);
  }
  return dropped;
}

static void upload() {
  g_uploadBusy = true;
  g_lastUploadMs = millis();
  g_wantSync = false;

  int n = g_qCount < SYNC_MAX_BATCH ? g_qCount : SYNC_MAX_BATCH;

  JsonDocument doc;
  doc["device"] = "fitness_watch";
  doc["app_version"] = APP_VERSION;
  JsonArray arr = doc["readings"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    const QueuedReading &r = g_q[(g_qHead + i) % SYNC_QUEUE_MAX];
    JsonObject o = arr.add<JsonObject>();
    o["timestamp"] = clockIso(r.ts);
    if (r.hr > 0) o["heart_rate"] = r.hr;   // omit rather than send a meaningless 0
    o["steps"] = (int32_t)r.steps;
  }
  String body;
  serializeJson(doc, body);

  DBG("POST %d readings to %s (body %d B)", n, BACKEND_URL INGEST_PATH, body.length());

  HTTPClient http;
  http.setTimeout(8000);
  http.begin(String(BACKEND_URL) + INGEST_PATH, (const char *)NULL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + settings().token);
  int code = http.POST(body);
  http.end();

  g_lastHttp = code;
  if (code == 200) {
    g_qHead = (g_qHead + n) % SYNC_QUEUE_MAX;
    g_qCount -= n;
    g_uploaded += n;
    DBG("sync OK: %d readings acked (total %lu, queue %d)", n, (unsigned long)g_uploaded, g_qCount);
  } else {
    // Keep the batch queued — the next attempt retries it (offline queue).
    DBG("sync FAIL: http %d (queue held at %d)", code, g_qCount);
    if (code == 401) {
      Serial.println("[net] 401 -> the device token was revoked/invalid. Re-pair from the SYNC screen (hold a button).");
    }
  }
  g_uploadBusy = false;
}

void syncTick(time_t ts, int hr, uint32_t steps) {
  if (!settings().paired || !settings().token[0]) return;
  uint32_t now = millis();

  // Capture one reading per READING_INTERVAL_MS, only once the clock is real.
  if (now - g_lastSampleMs >= READING_INTERVAL_MS) {
    g_lastSampleMs = now;
    if (clockSynced() && ts > 0) {
      // Send the steps taken SINCE THE LAST READING, not the cumulative counter.
      // The backend SUMS the `steps` field of every reading (routes/charts.py
      // steps_by_day[...] += r["steps"], routes/user.py total_steps=sum(steps)),
      // and the Android tracker already sends a delta — so posting the running
      // total once a minute would multiply a day's steps by the reading count.
      // stepCount stays cumulative in the sketch for the on-screen display.
      uint32_t delta = (steps >= g_stepsBaseline) ? (steps - g_stepsBaseline)
                                                  : steps;  // counter reset/wrapped
      g_stepsBaseline = steps;
      enqueue(ts, hr, delta);
    }
  }

  // Upload when a batch is due: right after a pair, on the normal interval, or
  // sooner if the last attempt failed (offline queue backoff).
  uint32_t dueMs = (g_lastHttp != 200 && g_lastUploadMs != 0) ? SYNC_RETRY_MS : SYNC_INTERVAL_MS;
  bool due = (now - g_lastUploadMs >= dueMs) || g_wantSync;
  if (!g_uploadBusy && due && g_qCount > 0 && wifiConnected()) {
    upload();
  }
}

// ── Pair-apply (BLE controller / shared verify) ─────────────────────────────
static bool     g_apply = false;
static bool     g_applyChecked = false;
static uint32_t g_applyCheckAt = 0;

void netApply() {
  if (settings().paired) return;
  if (!g_settings.ssid[0] || !g_settings.token[0]) {
    DBG("netApply: nothing saved yet — set SSID + TOKEN first");
    return;
  }
  g_apply = true;
  g_applyChecked = false;
  DBG("netApply: connecting to %s", g_settings.ssid);
  wifiSetup(g_settings.ssid, g_settings.pass);
}

bool netApplying() { return g_apply; }

const char *netApplyStatus() {
  if (!g_apply) return settings().paired ? "paired" : "idle";
  // While applying: "connecting" until the STA link is up, then "verifying"
  // until the backend health probe succeeds (which flips us to "paired").
  return wifiConnected() ? "verifying" : "connecting";
}

void netTick() {
  if (!g_apply) return;
  // Wait for the STA link. If WiFi drops mid-verify we simply land here again
  // and retry once it returns — g_apply stays set, so no extra recovery path
  // is needed (an earlier `if (!wifiConnected())` after this block was dead
  // code: it could only run when wifiConnected() was already true).
  if (!wifiConnected()) return;

  if (millis() - g_applyCheckAt < 2000) return;   // throttle the health probe
  g_applyCheckAt = millis();

  if (isBackendUp()) {
    g_settings.paired = true;
    settingsSave();
    g_apply = false;
    g_applyChecked = true;
    clockBegin();   // make sure NTP is running for the timestamps we'll send
    DBG("netApply: backend reachable -> PAIRED");
  } else {
    g_applyChecked = false;                       // keep retrying the probe
  }
}

// ── Serial diagnostics ───────────────────────────────────────────────────────
void logWatch() {
  Serial.printf(
    "[watch] paired=%d wifi=%s ip=%s queue=%d uploaded=%lu lastHttp=%d heap=%u uptime=%lus\n",
    settings().paired, wifiConnected() ? "up" : "down", wifiIp(),
    syncQueued(), (unsigned long)syncUploaded(), syncLastHttp(),
    (unsigned int)ESP.getFreeHeap(), (unsigned long)(millis() / 1000));
}