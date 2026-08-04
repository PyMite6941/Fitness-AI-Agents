/* ble.cpp — BLE GATT peripheral for the FitnessAI Watch.
 *
 * See ble.h for the contract. Uses the Bluedroid stack bundled in the ESP32
 * core (no extra library). The watch always advertises so ANY BLE device can
 * find and drive it; pairing here means "give the watch its WiFi + device token"
 * so it can upload data directly to the backend.
 */

#include "ble.h"
#include "config.h"
#include "net.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <sys/time.h>

#define BLE_DBG(fmt, ...) do { if (DEBUG_SERIAL) Serial.printf("[ble] " fmt "\n", ##__VA_ARGS__); } while (0)

static BLEServer  *g_server = nullptr;
static BLECharacteristic *g_stateChar = nullptr;
static BLEAdvertising *g_adv = nullptr;
static bool g_connected = false;
static bool g_stackUp = false;
static uint32_t g_lastStateMs = 0;

// Build the short status string pushed on STATE: pairs this with logWatch().
static String buildState() {
  String s = "paired:";
  s += settings().paired ? "1" : "0";
  s += " wifi:";
  s += wifiConnected() ? "1" : "0";
  s += " q:";
  s += syncQueued();
  s += " up:";
  s += syncUploaded();
  s += " http:";
  s += syncLastHttp();
  if (netApplying()) { s += " apply:"; s += netApplyStatus(); }
  return s;
}

class WatchServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *s) override {
    g_connected = true;
    BLE_DBG("client connected");
    s->getAdvertising()->start();   // keep the window open for other scanners
    bleNotifyState();
  }
  void onDisconnect(BLEServer *s) override {
    g_connected = false;
    BLE_DBG("client disconnected");
    s->getAdvertising()->start();   // Bluetooth stops advertising after a link
  }
};

class WatchCharCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    String uuid = c->getUUID().toString();
    String val  = c->getValue();
    WatchSettings &s = settingsMut();

    if (uuid == BLE_UUID_SSID) {
      strncpy(s.ssid, val.c_str(), sizeof(s.ssid) - 1);
      s.ssid[sizeof(s.ssid) - 1] = 0;
      settingsSave();
      BLE_DBG("ssid <- %s", s.ssid);
    } else if (uuid == BLE_UUID_PASS) {
      strncpy(s.pass, val.c_str(), sizeof(s.pass) - 1);
      s.pass[sizeof(s.pass) - 1] = 0;
      settingsSave();
      BLE_DBG("pass <- (set)");
    } else if (uuid == BLE_UUID_TOKEN) {
      String t(val.c_str());
      t.trim();
      if (!t.startsWith("fit_")) {
        BLE_DBG("token rejected (must start fit_)");
        bleNotifyState();
        return;
      }
      strncpy(s.token, t.c_str(), sizeof(s.token) - 1);
      s.token[sizeof(s.token) - 1] = 0;
      s.paired = false;
      settingsSave();
      BLE_DBG("token <- %s", s.token[0] ? "set" : "empty");
      bleNotifyState();
    } else if (uuid == BLE_UUID_TIME) {
      if (val.length() >= 8) {
        uint64_t epoch = 0;
        for (int i = 7; i >= 0; i--) epoch = (epoch << 8) | (uint8_t)val[i];  // little-endian
        struct timeval tv = { (time_t)epoch, 0 };
        settimeofday(&tv, nullptr);
        BLE_DBG("clock <- %lu (%s)", (unsigned long)epoch, clockIso(epoch));
        bleNotifyState();
      }
    } else if (uuid == BLE_UUID_CMD) {
      String cmd(val.c_str());
      cmd.trim();
      BLE_DBG("cmd <- %s", cmd.c_str());
      if (cmd == "apply") {
        netApply();
      } else if (cmd == "sync") {
        syncWant();
      } else if (cmd == "stat") {
        // fallthrough -> notify below
      } else if (cmd == "unpair") {
        settingsClear();
        BLE_DBG("unpaired — rebooting to setup");
        delay(200);
        ESP.restart();
      } else if (cmd == "reboot") {
        delay(100);
        ESP.restart();
      }
      bleNotifyState();
    }
    bleNotifyState();   // always reflect the new state after any write
  }
};

void bleNotifyState() {
  if (!g_stackUp || !g_stateChar) return;
  String s = buildState();
  g_stateChar->setValue((uint8_t *)s.c_str(), s.length());
  if (g_connected) g_stateChar->notify();
}

void bleStart() {
  if (g_stackUp) return;

  BLEDevice::init(BLE_DEVICE_NAME);
  BLEDevice::setPower(ESP_PWR_LVL_P3);   // a few dBm — plenty for wrist-to-pocket range

  g_server = BLEDevice::createServer();
  g_server->setCallbacks(new WatchServerCallbacks());

  BLEService *svc = g_server->createService(BLE_SERVICE_UUID);

  BLECharacteristic *ssid  = svc->createCharacteristic(BLE_UUID_SSID,  BLECharacteristic::PROPERTY_WRITE);
  BLECharacteristic *pass  = svc->createCharacteristic(BLE_UUID_PASS,  BLECharacteristic::PROPERTY_WRITE);
  BLECharacteristic *token = svc->createCharacteristic(BLE_UUID_TOKEN, BLECharacteristic::PROPERTY_WRITE);
  BLECharacteristic *time_ = svc->createCharacteristic(BLE_UUID_TIME,  BLECharacteristic::PROPERTY_WRITE);
  BLECharacteristic *cmd   = svc->createCharacteristic(BLE_UUID_CMD,   BLECharacteristic::PROPERTY_WRITE);
  BLECharacteristic *name  = svc->createCharacteristic(BLE_UUID_NAME,  BLECharacteristic::PROPERTY_READ);
  g_stateChar = svc->createCharacteristic(BLE_UUID_STATE, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);

  BLE2902 *cccd = new BLE2902();
  g_stateChar->addDescriptor(cccd);          // needed before a client can subscribe
  name->setValue((uint8_t *)BLE_DEVICE_NAME, strlen(BLE_DEVICE_NAME));

  WatchCharCallbacks *cb = new WatchCharCallbacks();
  ssid->setCallbacks(cb);
  pass->setCallbacks(cb);
  token->setCallbacks(cb);
  time_->setCallbacks(cb);
  cmd->setCallbacks(cb);

  svc->start();

  // The device name the phone shows in its Bluetooth list comes from the
  // advertisement data, so set it explicitly (not just on the init call).
  BLEAdvertisementData adv;
  adv.setName(BLE_DEVICE_NAME);
  adv.setCompleteServices(BLEUUID(BLE_SERVICE_UUID));
  adv.setFlags(0x06);   // LE General Discoverable + no BR/EDR (classic)

  g_adv = BLEDevice::getAdvertising();
  g_adv->setAdvertisementData(adv);
  g_adv->setScanResponse(true);
  g_adv->setMinPreferred(0x06);
  g_adv->setMaxPreferred(0x12);   // advertise fairly often so phones find it quickly
  g_server->getAdvertising()->start();
  BLEDevice::startAdvertising();

  g_stackUp = true;
  bleNotifyState();
  BLE_DBG("advertising as %s", BLE_DEVICE_NAME);
}

void bleTick() {
  if (!g_stackUp) return;
  uint32_t now = millis();
  if (now - g_lastStateMs >= 2000) {   // ~0.5 Hz state beacon while connected
    g_lastStateMs = now;
    bleNotifyState();
  }
}

bool bleActive() { return g_stackUp; }