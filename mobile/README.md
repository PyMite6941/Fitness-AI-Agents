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
  android/              # the native Android Studio project (Kotlin)
frontend/public/
  version.json          # source of truth for app version + APK URL — served by the
                        # PUBLIC website (the GitHub repo is private, so raw.github /
                        # Releases won't load for anonymous users)
  fitness-ai.apk        # the signed APK once built (drop it here; Vercel serves it)
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
- **Updates:** the app reads `version.json` from the public website (`UpdateChecker.kt`,
  URL baked into `app/build.gradle.kts`) and offers the new APK when `versionCode` there is
  higher than the installed build. **Not GitHub raw — the repo is private.**

## Building the APK

> ✅ **The APK now builds automatically in GitHub Actions** (`.github/workflows/android.yml`)
> and auto-publishes to `frontend/public/fitness-ai.apk`, so it's downloadable from the
> site's `/app` page — no phone, Mac, or local toolchain needed. Just run the workflow
> from the repo's **Actions** tab. Not yet tested on a physical device.
>
> The steps below are the **local** alternative (Android Studio) if you prefer.

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
5. **Publish + wire the download (private-repo friendly):**
   - Copy the signed APK to `frontend/public/fitness-ai.apk` (Vercel serves it publicly at
     `https://fitness-ai-agents.vercel.app/fitness-ai.apk` — already the `apkUrl` in
     `frontend/public/version.json`).
   - Commit + push → Vercel deploys → the `/app` page and the in-app updater both pick it up.
   - (Alternative: make the GitHub repo public and use GitHub Releases instead — then point
     `apkUrl` at the release asset. Don't, until you've confirmed no secrets are committed.)

## Releasing an update later

1. Bump `versionCode` (and `versionName`) in **both** `mobile/android/app/build.gradle.kts`
   and `frontend/public/version.json`.
2. Build + sign the new APK, replace `frontend/public/fitness-ai.apk`, commit, push.
   Vercel redeploys and existing installs detect the update on next launch.

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

**iOS (no native tracker — by Apple design):**
- [ ] The web app is now an installable PWA (Add to Home Screen). For richer iOS data,
      consider an **Apple Shortcuts** recipe that reads Health samples and POSTs to `/ingest`
      with the pairing token (a shared iCloud Shortcut link = "install via the website",
      no App Store). Document the Shortcut steps on the `/app` page.

**Repo hygiene:**
- [ ] `mobile/android/.gitignore` already covers `build/`, `.gradle/`, `local.properties`,
      `*.keystore` — never commit signing keys.
- [ ] When the APK is built, drop it at `frontend/public/fitness-ai.apk` (the public,
      private-repo-friendly host). The repo is currently **private**, so GitHub raw/Releases
      do NOT serve to anonymous users — that's why `version.json` lives in `frontend/public/`.
