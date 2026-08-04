/* ble.h — Bluetooth LE CONTROL peripheral.
 *
 * BLE is the controller, not the data path. A phone app (or the web app via
 * Web Bluetooth) connects to the watch and:
 *   1. writes the WiFi SSID/password + the "fit_" device token  -> pairing
 *   2. writes the current unix time (8 bytes LE)                 -> clock sync
 *   3. writes ASCII commands: apply | sync | stat | unpair | reboot
 *   4. reads / subscribes to STATE (a short status string)
 *
 * Health data is NOT carried over BLE — the watch uploads directly to the
 * backend over WiFi once paired.
 */

#pragma once

#include <Arduino.h>

void bleStart();      // init stack + advertise (call once in setup)
void bleTick();       // push STATE notifications (call every main loop)
bool bleActive();     // stack is up
void bleNotifyState();// immediately push the current STATE to a connected peer