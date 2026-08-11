#pragma once
/*
 * sim.h — synthetic sensors for the Wokwi simulator build.
 *
 * Everything in this file is compiled OUT unless the sketch is built with
 * -DSIM_BUILD=1 (see watch/sim/simctl.py). The hardware build is byte-identical
 * to what it was before this file existed.
 *
 * WHY this exists: Wokwi emulates the ESP32-C3, the SSD1306 and the MPU6050,
 * but there is no MAX30105 part — a simulated I2C bus simply has nothing at
 * 0x57. Without a stand-in, every simulator run would boot into the
 * "HR missing" branch and the heart-rate screens could never be exercised.
 * So in SIM_BUILD the MAX30105's ONE input to the rest of the firmware —
 * the raw IR sample — is generated here instead of read over I2C.
 *
 * Note what is NOT faked: checkForBeat(), the beat-interval maths, the rolling
 * average, the finger-present gating and the resetHeartRate() path all still
 * run for real. The simulator feeds the real pipeline a real-shaped waveform;
 * it does not hand the UI a pre-cooked BPM. If beat detection is broken, the
 * simulator shows it broken.
 */

#if SIM_BUILD

#include <Arduino.h>
#include <math.h>

// ── Tunables ─────────────────────────────────────────────────────────────────
#ifndef SIM_HR_BPM
#define SIM_HR_BPM            72      // pulse rate the synthetic waveform beats at
#endif
#ifndef SIM_PPG_AMPL
#define SIM_PPG_AMPL          9000    // pulsatile (AC) swing in IR counts
#endif
#ifndef SIM_PPG_DC
#define SIM_PPG_DC            105000  // baseline (DC) IR with a finger on the sensor
#endif
#ifndef SIM_PPG_NOISE
#define SIM_PPG_NOISE         120     // +/- counts of sensor noise
#endif
#ifndef SIM_FINGER_START_MS
#define SIM_FINGER_START_MS   3000    // finger arrives this long after boot
#endif
#ifndef SIM_FINGER_CYCLE_MS
#define SIM_FINGER_CYCLE_MS   0       // >0 = finger on/off every N ms (tests the
#endif                                //      reset-on-release path); 0 = stays on

// Runtime overrides so a Wokwi scenario can drive the sim over serial
// ("bpm 140", "finger 0"). Defined in fitness_watch.ino.
extern int  g_simBpm;
extern int  g_simFingerForce;   // -1 = follow the schedule, 0 = off, 1 = on

// ── Finger presence ──────────────────────────────────────────────────────────
static inline bool simFingerPresent(uint32_t now) {
  if (g_simFingerForce >= 0) return g_simFingerForce != 0;
  if (now < SIM_FINGER_START_MS) return false;
  // #if, not if: with the default SIM_FINGER_CYCLE_MS of 0 the modulo below is a
  // literal division by zero. It is dead code at runtime, but the compiler still
  // parses it and warns (-Wdiv-by-zero), so the branch has to disappear before
  // codegen rather than after.
#if SIM_FINGER_CYCLE_MS > 0
  uint32_t t = (now - SIM_FINGER_START_MS) % (uint32_t)(SIM_FINGER_CYCLE_MS * 2);
  return t < (uint32_t)SIM_FINGER_CYCLE_MS;
#else
  return true;
#endif
}

// ── Synthetic MAX30105 ───────────────────────────────────────────────────────
// Shape: a sharp systolic upstroke followed by a smaller dicrotic bump, which
// is what a real fingertip PPG looks like and what checkForBeat()'s DC-removal
// + threshold filter is tuned for. A plain sine also detects, but a two-peak
// waveform is the honest test — it proves the detector locks onto the systolic
// peak and does not double-count the dicrotic notch as a second beat.
static inline long simIR() {
  uint32_t now = millis();

  // No finger: the sensor sees ambient IR only. Must stay well under the
  // firmware's 50000 presence threshold.
  if (!simFingerPresent(now)) return 18000 + random(-SIM_PPG_NOISE, SIM_PPG_NOISE);

  int bpm = (g_simBpm > 20 && g_simBpm < 255) ? g_simBpm : SIM_HR_BPM;
  float periodMs = 60000.0f / (float)bpm;
  float ph = fmodf((float)now, periodMs) / periodMs;   // 0..1 through one beat

  float systolic = expf(-powf((ph - 0.12f) / 0.070f, 2.0f));
  float dicrotic = expf(-powf((ph - 0.38f) / 0.105f, 2.0f)) * 0.32f;

  return (long)SIM_PPG_DC
       + (long)((systolic + dicrotic) * (float)SIM_PPG_AMPL)
       + random(-SIM_PPG_NOISE, SIM_PPG_NOISE);
}

// Stands in for particleSensor.begin(). Always succeeds — the point of the
// simulator is to reach the code behind a working sensor.
static inline bool simMaxBegin() { return true; }

#endif  // SIM_BUILD
