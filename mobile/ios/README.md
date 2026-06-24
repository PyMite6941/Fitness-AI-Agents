# FitnessAI — iOS app (HealthKit)

Native SwiftUI app that mirrors the Android tracker. Instead of running its own
sensors, it reads **HealthKit**, which already collects steps / distance / heart rate
from the iPhone **and a paired Apple Watch** in the background — then uploads them to
your account. No analysis on-device; the server analyzes on request.

```
mobile/ios/FitnessAI/
  FitnessAIApp.swift     # @main SwiftUI app
  ContentView.swift      # pairing UI, sync, update check
  Keychain.swift         # token in the Keychain (this-device-only, background-readable)
  HealthSync.swift       # HealthKit read + HKObserverQuery background delivery + upload
  Uploader.swift         # POST /ingest (Bearer pairing token, HTTPS-only)
  UpdateChecker.swift    # reads /version.json (iosApp)
  Config.swift           # backend + version URLs (no secrets)
  Info.plist             # HealthKit usage strings + ATS (HTTPS forced)
  FitnessAI.entitlements # HealthKit + background delivery
```

> ⚠️ **Scaffolded, NOT compiled or tested.** Building/running iOS requires **macOS +
> Xcode + an iPhone** — it cannot be built on Windows/Linux. Treat this as a faithful
> starting point; expect to wire it into an Xcode project and fix a few things on first run.

## Build & test (you, on a Mac)

1. Open **Xcode** → New → App (SwiftUI, name `FitnessAI`, bundle id `studio.tin.fitnessai`).
2. Delete the template `ContentView`/`App` files and **drag in everything from
   `mobile/ios/FitnessAI/`**. Set the target's Info.plist + entitlements to the ones here
   (or copy the keys in).
3. Target → **Signing & Capabilities** → add the **HealthKit** capability (turn on
   *Background Delivery*). Pick your Apple ID under **Team** (a free personal team works).
4. Plug in your iPhone, select it as the run destination, press **Run**. Approve the
   Health permission prompt.
5. Test the flow: web app → Settings → Pair a device → copy code → paste in the app →
   "Sync Apple Health now" → confirm rows appear in `watch_data` under your account.

## Distribution — read this before promising an install link

iOS has **no Android-style sideload**. Options, cheapest first:
- **Free personal team (your own phone only):** the steps above. The app is signed for
  *your* Apple ID and **expires after 7 days** — re-run from Xcode to renew. Can't be
  given to other people.
- **TestFlight (up to 10k testers):** needs the **Apple Developer Program ($99/yr)**.
- **App Store:** also needs the $99/yr program + review.

There is **no free way to distribute an iOS app to other people** — that's an Apple
platform rule, not a project limitation. The website's `/app` page reflects this: iPhone
users install the **PWA** (Add to Home Screen) + connect Apple Health, which needs no
Mac and no fee.

## Security notes

- Pairing token lives in the **Keychain** (`kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly`):
  never synced to iCloud, never leaves the device, readable in the background for sync.
- **ATS on** (`NSAllowsArbitraryLoads=false`) — all traffic is HTTPS.
- The app holds **only** the user's revocable pairing token — never a service key. Backend
  stores only a **hash** of that token (a DB leak can't forge uploads).
- HealthKit access is **read-only** (`toShare: []`); the app never writes to Health.

## TODO

- [ ] Wire into an `.xcodeproj` and compile; fix first-run issues (no project file is
      committed — generating a valid one by hand is error-prone).
- [ ] App icon + launch screen assets.
- [ ] Upload distance / heart rate / active energy too (HealthSync only does steps now);
      anchor queries (`HKAnchoredObjectQuery`) so you only send new samples, not re-totals.
- [ ] Offline retry queue in `Uploader` (persist failed batches, like Android's `Prefs`).
- [ ] Optional GPS workout recording via Core Location for routes.
- [ ] If you join the Developer Program: TestFlight pipeline + bump `iosApp` in
      `frontend/public/version.json` on each release.
