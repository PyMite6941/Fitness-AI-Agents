# FitnessAI Watch — next features

Planning document. Nothing here is implemented yet; the roadmap of what *is*
built lives in [README.md](README.md#roadmap).

Ordered into tiers by what they cost you in hardware, because that is the real
constraint right now — most of the highest-value work needs no new parts at all.

---

## Where the watch stands

Built and working: 2004A display driven over I²C, screen rotation, demo mode,
BLE pairing, WiFi captive portal, `/ingest` sync, NTP clock, auto-orientation,
step detection, heart-rate pipeline.

Blocked on hardware: the MPU6050 is almost certainly counterfeit (ACKs writes,
retains none) and the MAX30102 never appears on the bus. Both need replacing
before the sensor-dependent features below can be finished.

---

## Tier 1 — firmware only, no new hardware

These are the ones to do first. Seven of the ten original picks land here.

### 1. Persist the offline queue to flash

**This is a bug, not a feature.** `g_q[]` in `net.cpp` is a plain RAM array of
`SYNC_QUEUE_MAX` (240) readings. NVS stores only ssid/pass/token/name. A reboot,
a flat battery or a brownout silently destroys up to four hours of banked
readings, and nothing reports the loss.

Write the queue through to NVS or LittleFS on enqueue, reload it at boot. Until
this exists, "offline queue" overstates what the watch actually does.

### 2. OTA firmware update over WiFi

Every update currently requires USB — and USB has been the single biggest time
sink in this build (a board that vanishes mid-flash, a blocked local toolchain).
`ArduinoOTA` or an HTTP pull from the backend would have saved hours.

Prerequisite for the mesh work below, where OTA to many nodes is the main prize.

### 3. SpO₂

The MAX30105 has the red LED needed for it; the firmware currently disables it
outright (`MAX_LED_RED 0x00`) because only the IR channel is read. The roadmap
already flags SpO₂ as uncomputed. Second vital sign, no new parts.

Costs power — the red LED was turned off deliberately — so gate it behind an
on-demand measurement rather than running it continuously.

### 4. Explicit workout mode

Start/stop a session; show elapsed time, average and max HR. The backend is
already waiting for this: `watch_data` carries `workout_type`,
`duration_minutes`, `avg_heart_rate`, `max_heart_rate`, `calories_burned`.

Right now the watch only ever emits `'reading'` rows. This unlocks the other
half of a schema that already exists.

### 5. Readiness score on the watch

`/insights` computes readiness server-side (HRV/resting-HR trend, sleep, acute:
chronic load) and it is deterministic — no AI quota involved. Fetching and
displaying it turns the watch from a sensor into a window onto the platform,
which is the thing that makes it feel like a product rather than a datalogger.

### 6. Sleep tracking

The `sleep` column in `watch_data` is unused. Low motion plus low HR over a
sustained window is a serviceable first pass, and it feeds readiness (#5).

### 7. Phone notifications over BLE

The BLE peripheral already exists and is already the control channel. Pushing
notifications to the wrist is the single most-expected smartwatch feature and
mostly reuses plumbing that is built.

### 8. Step goal, progress and streaks

Cheap to build, and the reason people look at a fitness watch more than twice.
Pairs naturally with the haptics in Tier 2.

### 9. Heart-rate zones and alerts

Zone bands off max-HR, with an alert when you leave the target zone. Turns
passive HR display into something that changes behaviour mid-workout.

### 10. Idle / move reminders

"You have been still for an hour." Trivial with the step counter, and one of the
highest-retention features in commercial trackers.

### 11. Alarm, timer, stopwatch

Basic watch functions. Users expect a thing on their wrist to do these, and
their absence reads as unfinished more than any missing health metric.

### 12. Watchdog timer and crash recovery

The ESP32 hardware WDT with automatic reboot on hang. A wearable cannot be
babysat — right now a lockup means a dead watch until someone notices and
power-cycles it.

### 13. IMU calibration routine

Zero-offset capture at rest, stored in NVS. Improves step detection and
auto-orientation, and gives an honest answer to "why does it count steps while
sitting still."

### 14. Night mode / do-not-disturb

Dim or blank the backlight on a schedule. On the LCD the backlight is the
dominant continuous load after the radio, so this is a battery feature as much
as a comfort one.

### 15. Security hardening

The device token currently sits in NVS in the clear. Flash encryption and secure
boot are supported on the C3 and matter the moment a watch leaves the bench —
a stolen device should not hand over a credential that can write to your account.

---

## The clock — three real defects

Called out separately because the watch does not currently tell the time, and
two of the three are bugs rather than missing features.

### C1. The displayed clock is uptime, not the time

`drawHomeLcd()` and `drawHome()` both do:

```c
uint32_t s = millis() / 1000;
int hh = (s / 3600) % 24, mm = (s / 60) % 60, ss = s % 60;
```

That is **time since boot**, formatted to look like a clock. It reads 00:00:00
at power-on and has never shown the actual time on either panel.

`clockNow()` — the real NTP-backed epoch — is used in exactly one place,
`syncTick()`, to timestamp uploaded readings. So NTP genuinely works; it simply
never reaches the screen.

The README's M2 note ("the home clock is a real NTP clock now") describes the
data path, not the display, and should be corrected along with the code.

**Fix:** use `clockNow()` + `localtime()`/`strftime()` on the home screen, and
fall back to uptime only while `clockSynced()` is false, labelled as such.

### C2. No timezone, no DST

```c
configTime(0, 0, "pool.ntp.org", "time.google.com");
```

Both offsets are zero, so even once #C1 is fixed the watch would display **UTC**.

**Fix:** `configTzTime()` with a POSIX TZ string, which gives DST transitions for
free and needs no firmware change twice a year. Make the zone a setting —
delivered over BLE at pairing, or from the backend, so the same firmware works
anywhere.

### C3. Nothing holds time across a reboot

Cold boot with no network means no time at all until NTP lands. Two gaps:

- **Offline drift** — the C3 keeps time on an internal RC oscillator, which
  drifts meaningfully over days without a resync.
- **No battery-backed source** — a DS3231 (#22) with a coin cell keeps correct
  time through reboots and flat batteries, and matters much more once the watch
  is battery powered, or is a mesh leaf node that only reaches a gateway
  occasionally.

**Fix:** set an explicit SNTP resync interval rather than relying on the default,
persist the last known good epoch to NVS at shutdown so a cold boot starts from
something plausible, and add the DS3231 when convenient.

---

## Internet-delivered notifications (no phone)

The natural pairing with the mesh work: notifications that reach the wrist
straight from the backend rather than being relayed by a phone over BLE (#7).

Most of this already exists server-side. `/cron/daily-insights` builds a daily
notification per user — readiness score plus the top Watchdog alert — through
`build_daily_notification()` in `routes/insights.py`, and delivers it to
browsers via web push. The content is already written; only the wrist is missing
as a destination.

**Shape:**

1. A small backend endpoint — say `GET /device/notifications` — authenticated
   with the existing `fit_…` device token, returning any pending short messages.
2. The watch polls it on the sync tick it already runs (`SYNC_INTERVAL_MS`,
   5 minutes) — no new radio wake, so effectively free in power terms.
3. A notification screen plus a haptic buzz (#17), and an acknowledge so the
   same message is not shown forever.

**Why this is the better version of #7:** it needs no phone, no app and no BLE
session. It works anywhere the watch has a network path — including through a
mesh gateway — and it reuses insight text the platform already generates.
Combined with OTA (#2) it means a watch that never needs to be touched.

Worth keeping messages short and few. A wrist is a bad place for a busy inbox,
and each poll costs radio time on a battery device.

---

## Tier 2 — small, cheap hardware

### 16. Battery monitor (M7)

`power.cpp` is written and tested; it needs **two 100 kΩ resistors** in a divider
to an ADC1 pin (GPIO 0–4). Until this exists the watch cannot honestly leave a
USB cable, which caps everything else.

Optional second divider on the 5 V rail for true USB-present detection
(`PIN_USB_SENSE`, currently `-1`).

### 17. Haptics

A coin vibration motor plus a driver transistor and flyback diode — about a
dollar. Needed by the alarms (#11), zone alerts (#9), move reminders (#10) and
goal notifications (#8). A watch with no output channel besides a screen you
have to be looking at is limited.

### 18. Buttons

Two tactile switches and two 330 Ω resistors. `PIN_BTN_A`/`PIN_BTN_B` (GPIO 4/5)
are mapped and the full navigation state machine is written — tap, hold,
double-tap — but nothing is wired, which is why demo mode had to auto-advance
the screens.

### 19. Battery and charging

LiPo cell plus a TP4056 protection/charge module. See
[POWER.md](POWER.md) for the wiring and the hard rule about never feeding the
5 V pin while USB is connected.

---

## Tier 3 — new modules

### 20. Replacement IMU

Do **not** rebuy an MPU6050 — it is discontinued, which is precisely why the
counterfeit rate is high and why the current one is dead. Better:

| Part | Why |
|---|---|
| **LSM6DS3** | Cheap, and does step detection and tap recognition *in hardware* — offloads work the firmware currently does by hand |
| **ICM-20948** | Direct upgrade, better on every metric, well suited to ESP32 |
| **MPU6500** | Closest drop-in if minimal code change matters more |

### 21. Working heart-rate sensor

The purple GY-MAX30102 boards route their I²C pull-ups to the internal **1.8 V**
rail instead of 3.3 V, so they never reach a valid logic high and stay invisible
on the bus. Buy a revised board (sensor alone on one side of the PCB) or a
known-brand breakout. **It is a 3.3 V part — several revisions do not tolerate
5 V on VCC.**

### 22. Real-time clock (DS3231)

The clock currently free-runs from `millis()` and depends on NTP or a BLE time
sync to be correct. A DS3231 with a coin cell keeps time across reboots and
without a network — which matters a great deal once the watch is battery
powered and offline, and even more in a mesh where not every node reaches the
internet.

### 23. GPS (M6)

Already on the roadmap; `/routes` exists server-side and computes distance, pace
and calories from the coordinate array. Blocked on a free GPIO for TX — see the
pin budget below.

### 24. Body temperature

MPU6050 and MAX30105 both expose *die* temperature, which is not body
temperature. A real skin-temperature reading needs a dedicated part (MAX30205 is
purpose-built for body temp). Feeds readiness and illness detection.

### 25. Fall detection

Accelerometer-based, so no new hardware if the IMU is replaced — listed here
because it is only credible with a *working* IMU. Genuinely valuable for a
health device and a strong differentiator.

### 26. Larger or colour display

An ST7789 240×240 IPS would allow graphs, trends and a real watch face rather
than four rows of text. A significant step up in perceived quality, and the
display abstraction added in `displays/` makes adding one a contained change.

### 27. Enclosure and strap

Not firmware, but the thing that separates "a project" from "a watch". Nothing
else on this list changes the impression as much.

---

## Mesh networking — phone-free operation

**Not to be built yet.** Recorded here because it changes the architecture and
should be designed before it is coded.

### The goal

Data and firmware updates flow continuously without a phone and without each
watch needing its own router credentials. Watches reach a gateway; the gateway
reaches the backend.

### The two options, both supported on the ESP32-C3

**ESP-NOW** — connectionless peer-to-peer on the WiFi radio. No router, no
association, ~250-byte payloads, unicast and broadcast, and it can coexist with
BLE. Very low overhead and quick to bring up. No routing and no internet path of
its own: something has to bridge it.

**ESP-WIFI-MESH** — a self-organising multi-hop tree. The root node holds the
internet uplink and the rest reach it through however many hops it takes.
Supports **OTA to every node from the root**, which is the single most valuable
property here given how painful USB flashing has been. Scales to ~1000 devices
(≤512 recommended for stable links), with roughly <100 m between nodes, and up
to ~170 m when throughput is low.

### The design tension that decides it

The C3 draws **~350 mA transmitting and ~93 mA receiving**. A mesh node that
relays for others must keep its radio listening — which a battery watch cannot
afford. So:

- **Watches should be leaf / sleepy nodes.** They wake, push a batch, and sleep.
- **Relaying belongs to mains-powered nodes** — a dedicated gateway, or a spare
  ESP32 on a charger.

A flat mesh of battery watches all relaying for each other would be elegant and
would flatten every battery in the network. This constraint should shape the
design before any code is written.

### Suggested shape

1. Watches speak **ESP-NOW** to whichever gateway is in range — cheapest in
   power, simplest to implement, no credentials on the watch.
2. A mains-powered **gateway** holds the WiFi credentials and forwards to
   `/ingest`, and serves firmware images back down.
3. Move to **ESP-WIFI-MESH** only if multi-hop coverage is genuinely needed —
   its payoff is mesh-wide OTA and range, and its cost is complexity plus
   always-on relays.

### What it unlocks

- No phone, and no per-watch WiFi setup — the captive portal becomes optional
- Fleet OTA: update every watch from one place
- Watches that keep syncing anywhere within reach of a gateway
- A credible multi-device story (a gym, a team, a household)

---

## Staying connected without a SIM

Worth being blunt first: **"connected at all times" is not achievable with WiFi
alone.** WiFi reaches the internet only where a known access point is in range,
so a WiFi-only watch is *eventually* connected, not continuously. Three
architectures actually deliver on this, in increasing order of what they cost.

### Option 1 — WiFi plus durable store-and-forward (nearly built)

The watch already queues readings offline and flushes them when it associates.
Make that queue survive a reboot (#1) and this becomes genuinely reliable: data
is never lost, it is just delayed until the watch is next near a known network.

- **Coverage:** home, gym, office — anywhere you have entered credentials.
- **Power:** the radio is the dominant load. The C3 draws ~350 mA transmitting
  and ~93 mA receiving; continuous receive would flatten a 500 mAh cell in about
  five hours. `wifiSetup()` already enables modem sleep (`WiFi.setSleep(true)`),
  so the radio wakes on the AP's DTIM beacons instead of staying pinned awake,
  which drops the average by roughly an order of magnitude.
- **Verdict:** the right default. Cheapest, already mostly written.
- **Gap:** says nothing about being out of the house.

### Option 2 — ESP-NOW gateways (the mesh plan above)

Mains-powered gateways carry the internet uplink; watches reach whichever is in
range. Extends coverage to wherever you place gateways, and removes per-watch
WiFi credentials entirely.

- **Coverage:** a radius around each gateway you deploy — under ~100 m per hop.
- **Power:** the cheapest option for the *watch*, because a leaf node transmits
  briefly and sleeps.
- **Verdict:** excellent for a controlled space (a home, a gym, a team room).
  Still not "anywhere".

### Option 3 — LoRaWAN (the real wide-area answer)

Long-range sub-GHz radio, no SIM and no carrier. Range is kilometres rather than
metres, and **The Things Network** is a free community network you can join —
or you run your own gateway and cover your own area.

- **Coverage:** 2–5 km per gateway; genuinely wide-area.
- **Power:** an SX1262 node with duty cycling and deep sleep can average around
  **175 µA**, which is wearable-grade — better than WiFi by orders of magnitude.
- **Costs:** tiny payloads (tens of bytes), regulatory duty-cycle limits on how
  often you may transmit, and **firmware updates are not practical over it** —
  far too slow.
- **Verdict:** the only option that plausibly means "always reachable" without
  cellular.

### Recommended: hybrid

Use both, for what each is good at:

| Path | Carries |
|---|---|
| **LoRaWAN** | Continuous trickle — heartbeat, step count, HR summary, alerts. Small, frequent, everywhere. |
| **WiFi / ESP-NOW** | Bulk sync of the detailed queue, and OTA firmware. When in range. |

The watch is then always *reachable*, and fully *synchronised* whenever it comes
near a known network.

### Hardware note

The search results for SX1262 wearable nodes are ESP32-**S3** boards (Heltec
WiFi LoRa 32 V3, XIAO ESP32S3 + Wio-SX1262). The C3 can drive an SX1262 over
SPI, but SPI needs about four pins and the SuperMini has only 0, 1, 2, 9, 10
free — see the pin budget below. Going LoRa most likely means moving to a board
with the radio integrated, which is a bigger decision than adding a module.

---

## How long the watch can hold data

"Server sends it, the watch holds it as long as necessary" is the right model,
and the limit is comfortable once the queue lives in flash (#1).

A packed reading — timestamp, HR, step delta, a few flags — runs about 24 bytes.
At the current one-per-minute cadence:

| Cadence | Per day | 512 KB partition | 1 MB partition |
|---|---|---|---|
| 1/min | ~35 KB | ~15 days | ~30 days |
| 1/5 min | ~7 KB | ~70 days | ~140 days |

Today's RAM queue holds 240 readings — **about four hours** — and loses all of
it on a reboot. Moving to flash turns "a few hours if nothing goes wrong" into
"weeks, reliably", which is what makes an intermittently-connected watch
actually viable.

Two things worth building alongside it:

- **Back off when offline.** Drop to one reading every five minutes after a few
  hours with no uplink. Detail matters least for the period you were not
  wearing it near a network, and this multiplies the buffer several times over.
- **Drop oldest, not newest, when full.** And report the loss rather than
  discarding silently, which is what the current queue does.

The same store-and-hold applies downward: schedules, alert times and settings
sent by the server are written to flash and act from there, so the watch keeps
doing the right thing with no connection at all.

---

## The easiest path to the internet

Ranked by what you would actually have to build, given what already exists.

### 1. WiFi at known networks — already built, already easy

The captive portal and BLE pairing both work; enter credentials once and the
watch syncs whenever it is in range. For a fitness watch this covers most of
real life: home, gym, office.

**Nothing to build.** Pair it with the durable queue (#1) and you have a watch
that never loses data and catches up whenever you walk in the door.

### 2. Phone as a BLE bridge — mostly built, and how every commercial tracker works

This is the cheapest route to "connected wherever you are", because the phone
already carries cellular and goes everywhere with the wearer. Fitbit, Garmin and
Apple all do exactly this; the watch is rarely the thing holding the connection.

**What already exists:**

- The watch runs a **BLE GATT peripheral** (`ble.cpp`) — the control link is live.
- The Android app already **authenticates with a `fit_…` device token** and
  **posts to `/ingest`** (`Uploader.kt`, `MainActivity.kt`).

**What is missing:** BLE *client* code in the Android app — connect to the
watch's service, drain its queue, hand it to the uploader it already has. The
two hard halves are done; the bridge between them is not.

This also gives the downlink for free: the phone fetches schedules and pushes
them over the same BLE session, so the scheduled-haptic design works with no
new backend transport at all.

### 3. Mesh or LoRa — only if phone-free is a hard requirement

Real infrastructure, real effort, covered in the sections above. Worth it when
the point is that **no phone exists** — a shared gym, a team deployment, a user
who does not carry a phone. Not worth it as a way to avoid writing BLE client
code.

### Recommendation

Do **1 and 2**. They are largely built, they cover the overwhelming majority of
real use, and together they already deliver "always connected" in the sense that
matters — data always arrives, and schedules always land.

Treat **3** as a product decision rather than an engineering one. It is the
right answer if phone-free operation is the point of the product; it is an
expensive detour if it is not.

---

## Server-scheduled haptics — buzz at an exact time

The requirement is a buzz at a precise moment, commanded by the server. The
design point that makes this work:

> **The server must not send "buzz now". It sends "buzz at 14:30".**

You cannot reliably reach a sleeping, intermittently-connected device at an exact
instant. Polling every five minutes gives up to five minutes of latency; holding
a persistent connection open to remove that latency costs the radio power that a
wrist-worn battery does not have. Every real device solves this the same way —
alarms fire *locally*, from a schedule delivered in advance.

### The flow

1. **Server holds a schedule** per device — a list of `{time, message, pattern}`.
2. **Watch fetches upcoming entries** on the sync tick it already runs every five
   minutes. No extra radio wake, so effectively free.
3. **Watch persists the schedule** to NVS/flash, so it survives a reboot.
4. **A local timer fires the haptic** at the exact second, from the watch's own
   clock.
5. **Watch acknowledges** on its next sync so the server knows it fired.

### What this buys

- **Exact.** Accuracy is limited by the watch's clock, not by network latency.
- **Works offline.** Once downloaded, the buzz fires with no connectivity at all.
- **Cheap in power.** No persistent connection, no extra wake-ups.

### The dependency this creates

An exact-time alarm is only as good as the clock behind it — which makes the
three clock defects above (#C1–C3) blocking work for this feature, not optional
polish. A watch whose clock is uptime cannot fire an alarm at 14:30, and one
that free-runs on an RC oscillator will drift away from it. **The DS3231 (#22)
stops being a nice-to-have** the moment alarms matter.

### If you genuinely need instant, unscheduled alerts

Scheduling covers alarms, reminders and coaching prompts — anything known in
advance. For a truly immediate push there is no free lunch; pick your cost:

| Approach | Latency | Cost |
|---|---|---|
| Shorter poll interval | = interval | Battery, linearly |
| Persistent MQTT keepalive | Seconds | Radio never fully sleeps |
| LoRaWAN **Class A** | Until next uplink | Very low power — downlink only in the window after the watch transmits |
| LoRaWAN **Class C** | Immediate | Continuous receive; mains-powered devices only |

LoRaWAN's class model maps onto this exactly: **Class A plus scheduled alerts**
is the combination that gives a battery-powered watch both wide-area reach and
exact-time buzzing.

---

## Pin budget — the real constraint

The C3 SuperMini is pin-starved and several features above compete for the same
GPIOs. Current map: I²C on 7/8, buttons on 4/5, GPS RX on 6, battery ADC on 3.
Free after that: **0, 1, 2, 9, 10**.

| Want | Needs | Note |
|---|---|---|
| Battery divider | GPIO 3 | Must be ADC1 (0–4); ADC2 is unusable with WiFi on |
| GPS TX | one of 0, 1, 2, 10 | The stated blocker on M6 |
| Haptics | 1 pin | Any free GPIO |
| USB sense | ADC1 pin | Competes with the battery divider for 0–4 |

GPIO 2, 8 and 9 are strapping pins — never put a button on them. If this gets
tight, an I²C GPIO expander adds pins on the bus that is already there.

---

## What actually makes this watch successful

Ranked by impact on whether it reads as a product:

1. **It has to stay running.** Flash-persisted queue (#1), watchdog (#12),
   battery (#16, #19). A watch that loses data or hangs is not a watch.
2. **It has to be updatable without a cable.** OTA (#2), then mesh. This build
   lost hours to USB; that will not improve at scale.
3. **It has to show the correct time.** The clock defects above (#C1–C3) are
   the most basic promise a watch makes, and it is currently not kept.
4. **It has to tell you something you did not already know.** Readiness (#5),
   sleep (#6), zones (#9). Raw step counts are a commodity; the platform's
   analysis is the differentiator, and the watch is how it reaches your wrist.
5. **It has to talk back.** Haptics (#17), notifications (#7), alarms (#11).
   Output, not just input.
6. **It has to be wearable.** Battery, enclosure (#27), a display worth looking
   at (#26).

The first two are unglamorous and matter most. Everything in Tier 1 is
achievable with the hardware already on the desk — including, notably, while
both sensors are still broken.
