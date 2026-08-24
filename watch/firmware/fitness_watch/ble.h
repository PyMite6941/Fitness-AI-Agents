/* ble.h — Bluetooth LE CONTROL peripheral.
 *
 * BLE is the controller, not the data path. A phone app (or the web app via
 * Web Bluetooth) connects to the watch and:
 *   1. writes the WiFi SSID/password + the "fit_" device token  -> pairing
 *   2. writes the current unix time (8 bytes LE)                 -> clock sync
 *   3. writes ASCII commands: apply | sync | stat | unpair | reboot
 *   4. reads / subscribes to STATE (a short status string)
 *
 * BLE now ALSO carries health data, which the original design excluded. A phone
 * can write CMD "pull" to have the watch stream its offline queue over the DATA
 * characteristic, upload it, and write the highest accepted sequence back to
 * ACK. The watch keeps every reading until that ack arrives -- delivery over
 * BLE alone proves nothing, since the phone may have no signal -- so a failed
 * upload costs nothing and redelivery is harmless.
 *
 * The watch still uploads directly over WiFi whenever it can; the bridge is for
 * when it cannot.
 */

#pragma once

#include <Arduino.h>

void bleStart();      // init stack + advertise (call once in setup)
void bleTick();       // push STATE notifications (call every main loop)
bool bleActive();     // stack is up
void bleNotifyState();// immediately push the current STATE to a connected peer
void bleSleep();      // drop advertising + power the BT controller off (standby)
void bleWake();       // power the controller back on + re-advertise (out of standby)
void bleRelayRequest();// phone offered to relay: start streaming the queue over DATA