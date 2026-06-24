# FitnessAI — Mobile tracker

A lightweight **native Android** app that tracks steps (and, later, GPS routes) in the
background and uploads them to your FitnessAI account. **No analysis runs on the phone** —
it only collects your data and POSTs it to the backend, which stores it in Supabase under
your account and analyzes it on the web when you ask.

- **Why native (not a PWA):** background sensor tracking with the screen off is impossible
  on a web app. A native foreground service is the only way to track without a watch.
- **Why Android only:** iOS locks background sensors + Apple Watch data behind HealthKit,
  which only a native iOS app can read. On iPhone, connect a source (Apple Health export,
  Strava, Fitbit) from the web dashboard instead.

```
mobile/
  version.json          # single source of truth for app version + APK URL (read from GitHub)
  android/              # the native Android Studio project (Kotlin)
```

## How the pieces fit

```
Phone (TrackingService) --steps/GPS--> Uploader --POST /ingest (Bearer fit_…)-->
   backend (auth.get_user_id_flexible) --> Supabase watch_data (under your user_id)
   --> analyzed on demand by POST /analyze (web app)
```

- **Pairing:** web app → `POST /device/pair` (Clerk-authed) issues a `fit_…` token →
  user pastes it into the app → stored in the Android Keystore (`Prefs.kt`).
- **Auth:** `backend/auth.py::get_user_id_flexible` accepts that token OR a Clerk JWT.
- **Updates:** the app reads `mobile/version.json` from GitHub (`UpdateChecker.kt`) and
  offers the new APK when `versionCode` there is higher than the installed build.

## Build the APK (you, on a machine with Android Studio)

> ⚠️ This was scaffolded but **not compiled or tested** — treat it as a strong starting
> point, not a finished app. Expect to resolve a few resource/icon issues on first build.

1. Install **Android Studio** (free). Open `mobile/android/` as a project; let it download
   the Gradle wrapper + SDKs.
2. Add a launcher icon (`app/src/main/res/mipmap-*/ic_launcher`) — Studio's Image Asset
   wizard does this in 2 clicks. (Referenced by the manifest; missing icons fail the build.)
3. Build a debug APK to test on your phone:
   ```
   ./gradlew assembleDebug         # outputs app/build/outputs/apk/debug/app-debug.apk
   ```
4. For a release APK to publish, create a keystore once and sign:
   ```
   keytool -genkey -v -keystore fitnessai.keystore -alias fitnessai -keyalg RSA -keysize 2048 -validity 10000
   ./gradlew assembleRelease       # configure signingConfigs in app/build.gradle.kts first
   ```
5. **Publish + wire the download:**
   - Create a GitHub Release tagged `mobile-v0.1.0`, attach the APK as `fitness-ai.apk`.
   - Confirm the URL matches `mobile/version.json -> androidApp.apkUrl`. The website's
     `/app` page and the in-app updater both read that file, so a release + a bumped
     `version.json` is all it takes to ship an update.

## Releasing an update later

1. Bump `versionCode` (and `versionName`) in **both** `app/build.gradle.kts` and
   `mobile/version.json`.
2. Build + sign the new APK, attach to a new GitHub Release, update `apkUrl`/`releaseTag`
   in `version.json`, commit, push. Existing installs will detect it on next launch.

---

## TODO — what's left to finish (for a person or an LLM)

Done so far: backend ingest + flexible auth + `/device/pair|list|revoke`; the website
`/app` download page + GitHub version manifest; this Android scaffold (pairing, encrypted
token, step-counter foreground service, offline upload queue, GitHub update check).

**Backend (small, testable):**
- [ ] Apply the `device_tokens` table from `backend/schema.sql` to the Supabase project
      (run the `CREATE TABLE device_tokens …` block once in the SQL editor).
- [ ] Redeploy the backend HF Space so `/device/*` + the `get_user_id_flexible` change go live.
- [ ] Smoke test: `POST /device/pair` with a Clerk JWT → get a `fit_…` token; then
      `POST /ingest/` with `Authorization: Bearer fit_…` and a `{readings:[…]}` body →
      confirm a row lands in `watch_data` under your `user_id`.

**Web app (frontend):**
- [ ] Add a **Settings → Pair a device** UI: call `api` → `POST /device/pair`, show the
      returned token as copyable text + a QR (the `/app` page already explains this step).
- [ ] List paired devices (`GET /device/list`) with a Revoke button (`POST /device/revoke`).
- [ ] Add `pairDevice`, `listDevices`, `revokeDevice` to `frontend/src/lib/api.ts`.

**Android app (needs an Android Studio build environment):**
- [ ] First compile: add launcher icons + verify Material3 theme resolves; fix any missing
      resources. The UI in `MainActivity.kt` is built in code — move it to
      `res/layout/activity_main.xml` with ViewBinding (already enabled).
- [ ] GPS routes: add `FusedLocationProviderClient` in `TrackingService.kt` during an active
      workout; POST the lat/lng array to `/routes` (see backend `routes/gps.py`) and add a
      workout object to the `/ingest` payload (`Uploader.buildPayload` has a TODO marker).
- [ ] Step baseline persistence: `TYPE_STEP_COUNTER` resets on reboot — persist the last
      cumulative baseline so reboots don't drop/double-count (currently in-memory only).
- [ ] Battery: batch readings + use WorkManager for retry/backoff instead of per-interval
      POSTs (the `work-runtime-ktx` dep is already added).
- [ ] Background-location permission flow (Android 10+) if you want route tracking with the
      screen off; request `ACCESS_BACKGROUND_LOCATION` separately with a rationale.
- [ ] Pair via QR scan (CameraX + ML Kit barcode) so users don't type the `fit_…` token.
- [ ] CI: a GitHub Actions workflow to build + sign the APK and attach it to the Release
      automatically when `mobile/version.json` changes.

**Repo hygiene:**
- [ ] Add a `.gitignore` under `mobile/android/` for `build/`, `.gradle/`, `local.properties`,
      and any `*.keystore` (never commit signing keys).
