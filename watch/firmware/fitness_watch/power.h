#pragma once
/*
 * power.h — battery monitor (Milestone 7).
 *
 * Reads the Li-ion cell through a resistor divider on an ADC1 pin and converts
 * it to a state-of-charge percentage. Optionally reads a second divider off the
 * 5V rail to know for certain whether USB is plugged in.
 *
 * All pins and thresholds are in config.h. Set PIN_BATT_ADC to -1 to disable
 * the whole module (battValid() then returns false and the UI shows "USB").
 *
 * Nothing here blocks: powerTick() samples at most once per BATT_INTERVAL_MS.
 */

#include <stdint.h>

void powerBegin();
void powerTick();

bool battValid();        // false until the first sample, or if the monitor is off
int  battMilliVolts();   // cell voltage in mV (0 when !battValid())
int  battPercent();      // 0..100 state of charge (-1 when !battValid())
bool usbPresent();       // true when running from USB rather than the cell
bool battLow();          // below BATT_LOW_MV and NOT on USB
bool battCritical();     // below BATT_CRIT_MV and not on USB — stop using the radio

// Ready-to-draw status-bar text: "USB", "CHG", "87%", or "LOW".
const char *battLabel();

// Longer form for the status screen: "4.06V 78% USB".
const char *battDetail();
