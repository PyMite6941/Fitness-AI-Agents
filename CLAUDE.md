# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A fitness data intelligence platform with an AI analysis pipeline:

- `backend/` — FastAPI API (Supabase DB, Clerk auth) and a CrewAI analysis crew. Deployed as a HuggingFace Space
- `frontend/` — Vite + React 19 dashboard (Clerk auth, Leaflet maps, Chart.js). Deployed on Vercel. Public pages: `/demo` (no-auth sample dashboard), `/app` (Android tracker download + GitHub version check)
- `mobile/` — native **Android** tracker app (Kotlin) that records steps/GPS in the background and POSTs to `/ingest`. **Scaffolded, not yet built/tested** — see `mobile/README.md` for build steps + the full TODO. `mobile/version.json` is the GitHub-read source of truth for app version/APK URL.

## Commands

### Frontend (`cd frontend`)
```bash
npm install
npm run dev        # Vite dev server on :5173
npm run build      # production build (Vercel runs this)
npm run lint       # ESLint — the only check; there is no test suite
```
Env: `VITE_CLERK_PUBLISHABLE_KEY`, `VITE_API_URL` (see `.env.example`). Vite bakes these at build time.

### Backend (`cd backend`)
```bash
bash setup.sh                          # creates .venv, installs requirements.txt (full CrewAI stack)
uvicorn main:app --reload --port 8000  # run the API (after activating .venv)
python test.py                         # smoke-test the AI crew against test_data.csv (needs model keys)
```
- **Two requirements files:** `requirements.txt` is the full stack (CrewAI, litellm, fastembed — needed to actually run `/analyze`); `requirements-deploy.txt` is a slimmer subset.
- DB schema lives in `backend/schema.sql` — run it against the Supabase/Postgres project.
- Required env (see `.env.example`): `SUPABASE_URL`, `SUPABASE_KEY`, `CLERK_JWKS_URL`, `CLERK_ISSUER_URL`, `ALLOWED_ORIGINS` (comma-separated origins for CORS), `OAUTH_STATE_SECRET`, `STRAVA_CLIENT_ID`/`STRAVA_CLIENT_SECRET`, and at least one of `OPENROUTER_API_KEY` / `GROQ_API_KEY`.

### Archived watch firmware (`watch-archive/firmware/fitness_watch/`)
The custom ESP32-C3 smartwatch firmware has been archived. It can be rebuilt later but is not part of the current project scope.

## Architecture

### Data model (Supabase / Postgres — `backend/schema.sql`)
- **`watch_data`** holds BOTH readings and workouts, discriminated by a `type` column (`'reading'` | `'workout'`). Reading columns (heart_rate, steps, sleep…) and workout columns (workout_type, duration_minutes, avg/max_heart_rate, calories_burned, distance_meters…) live in the same table.
- **`routes`** holds GPS workouts — a `coordinates` JSONB array plus computed `distance_meters`/`duration_seconds`/`pace`.
- **`analyses`** stores AI results (the full `FormattedOutput` payload for rich rendering).
- **`user_integrations`** holds OAuth tokens (Strava/Fitbit).
- All rows are keyed by Clerk `user_id` (a TEXT `sub` claim). There is no users table.

### API (`backend/main.py` mounts routers under prefixes)
`/watch` (watch.py), `/ingest` (ingest.py — generic device data ingestion, any wearable), `/routes` (gps.py), `/user` (user.py — includes `/user/sources` for connected device/integration breakdown), `/charts` (charts.py), `/analyze` (analysis.py), `/integrations` (integrations.py), `/demo` (demo.py), `/device` (device.py — pairing tokens for the Android app), `/health`.

### Device pairing & flexible auth (`backend/auth.py`, `backend/routes/device.py`)
The Android tracker can't get a Clerk JWT, so it uses an opaque **device token**. `POST /device/pair` (Clerk-authed) issues a `fit_…` token stored in `device_tokens` (see `schema.sql`); `GET /device/list` + `POST /device/revoke` manage them. `get_user_id_flexible` accepts EITHER a Clerk JWT OR a `fit_…` token and is used by `/ingest`, so phone uploads land in `watch_data` under the user's `user_id`. **`device_tokens` table must be applied to Supabase + backend redeployed before this works (see TODO).**

### Data source tracking
Every row in `watch_data` has a `device` column that records the source (e.g. `strava`, `apple_health`, `garmin`, `fitbit`, `manual`). The `/user/sources` endpoint returns a breakdown with counts and recency per source. The AI analysis pipeline receives the `device` column in its CSV input and gets a `source_hint` appended to the user's context so results can reference specific sources.

### Authentication (`backend/auth.py`)
Every protected route depends on `get_user_id`, which verifies a **Clerk RS256 JWT** via JWKS and returns the `sub`. The **web app** sends a live Clerk session token from `@clerk/react`.

### Server computes derived metrics, clients send raw data
Distance, pace, and calories are computed **server-side**, not on the device:
- `routes/gps.py` computes `distance`/`pace` (haversine) and calories (`backend/calories.py`, MET-based, default 70 kg).
- A `record_workout` flag on `POST /routes/` controls whether a route ALSO inserts a `watch_data` workout. Route-only clients (the watch, the web GPS tracker) set it `true`; clients that post their own workout (manual entry, Strava/Nike/Garmin imports) leave it `false` to avoid double-counting.
- `routes/watch.py` estimates calories only for watch-originated workouts (device starts with `fitness_watch`), so manual web entries stay as typed.

### AI analysis pipeline (`backend/bots.py`, doc: `AGENTS.md`)
`POST /analyze` dumps the user's `watch_data` to a temp CSV, then runs a **CrewAI** crew of 8 domain-specialized agents:

1. **context_agent** — clarifies fitness question, metrics, sources
2. **data_cleaner** — validates fitness data ranges (HR, steps, sleep)
3. **prompt_engineer** — writes fitness-specific analysis plan
4. **data_analyst** — computes weekly trends, HR zone time, multi-source comparisons
5. **sports_scientist** — exercise physiology interpretation, training load, recovery
6. **trend_analyzer** — time-series pattern detection (overload, plateau, fatigue)
7. **output_formatter** — renders structured JSON with source-aware fitness insights
8. **qa_critic** — scores domain accuracy, specificity, evidence quality

Returns a structured JSON `FormattedOutput` with 7 output types (chart/table/metrics/comparison/heatmap/code/report). CrewAI is synchronous, so `analysis.py` runs it in a thread executor (`run_in_executor`) to avoid blocking the event loop. Models are **rotated across free Groq + OpenRouter pools** (`_FAST_MODELS` / `_SMART_MODELS`) via litellm, with patches for Groq quirks. The result is persisted to `analyses` and rendered by the Dashboard's many `output_type` branches.

### Integrations (`backend/routes/integrations.py`)
Strava OAuth + Fitbit OAuth + file imports (Nike/Garmin/Apple/Google). These insert workout rows and route rows **directly** into the tables (they do not call `gps.save_route`), so they are unaffected by the `record_workout` mirror logic.

### Frontend (`frontend/src`)
`main.jsx` wires Clerk + react-router (`/`, `/dashboard`, `/routes`, `/log`, with `ProtectedRoute`). All API calls go through `src/lib/api.ts` (single `request()` helper that injects the Clerk bearer token). `VITE_API_URL` selects the backend.

## Deployment

- **Frontend → Vercel**, project `fitness-ai-agents`, **auto-deploys on push to `main`** (the entire deploy history is git-triggered). Just push to `main`.
- **Backend API → a HuggingFace Docker Space** (`pymite6941/data-analyst-ai-agent`, at `https://pymite6941-data-analyst-ai-agent.hf.space`). This is what the frontend's production `VITE_API_URL` points to, and it is where the 8-agent pipeline actually runs (`/analyze`). Built from `backend/Dockerfile` (`uvicorn main:app` on port 7860). There is **no CI for it in this repo** — deploy it manually. Set the same env vars as `backend/.env` in the Space secrets.
- **Standalone Gradio demo → a separate HuggingFace Gradio Space** `pymite6941/fitness-ai-agents-demo` (`https://pymite6941-fitness-ai-agents-demo.hf.space`). Public, no-auth showcase of the same agent pipeline. It vendors a copy of `backend/bots.py` (run `python huggingface_space/prepare_space.py` before each deploy); `huggingface_space/README.md` has the full deploy steps. The vendored `bots.py` is gitignored in this repo but committed to the Space. `OPENROUTER_API_KEY` + `GROQ_API_KEY` are set as Space secrets. Deploy/update with `huggingface_hub.upload_folder(...)` or `huggingface-cli upload`.

## Gotchas

- The live 8-agent pipeline makes ~20+ LLM calls/run, which exceeds free-tier quotas (OpenRouter ~50 free req/day without credits, Groq per-minute). The Gradio demo's "Use sample data" therefore returns a **cached** result (`huggingface_space/app.py::_SAMPLE_RESULT`); only real uploads run live. For reliable live runs, use a dedicated OpenRouter key with $10 credit (=1000 req/day).
- `backend/.env` ships with **placeholder** model keys. Real keys live in the deployed Space secrets (and were borrowed from sibling projects during setup). HF Space secrets can't be read back via API.
- Gradio Space dep set that builds: **gradio 5.27.1 + crewai 1.14.5 + litellm 1.85.x**, Python 3.11 (see `huggingface_space/requirements.txt` comments for why each pin). Don't gitignore `bots.py` inside `huggingface_space/` — the HF uploader honors it and drops the file.

## Status & Handoff TODO (2026-06-24)

**Shipped + pushed to `main` (frontend auto-deploys on Vercel):**
- Pivot off the custom watch → multi-source platform; archived firmware in `watch-archive/`.
- Live Gradio agent demo (HF), embedded on the landing page; `/demo` public sample dashboard; AI-analysis Export/Copy buttons.
- `/app` Android download page + `mobile/version.json` + GitHub-based update flow.
- Backend: `device_tokens` schema, `/device/pair|list|revoke`, `get_user_id_flexible`, `/ingest` accepts device tokens. **Device tokens are stored as SHA-256 hashes** (a DB leak can't forge uploads).
- Android tracker **scaffold** in `mobile/android/`; iOS (HealthKit) **scaffold** in `mobile/ios/`.
- **Supabase security verified (2026-06-24):** RLS is **ON for all 7 tables with deny-all** (anon/public key returns 0 rows — proven empirically); backend's `service_role` key bypasses RLS so the app works. The `device_tokens` table **has been applied to Supabase** (with RLS). The frontend does NOT use Supabase directly (auth is Clerk), so the anon key isn't even shipped to clients.

**Not done yet — full, ordered TODO lives in [`mobile/README.md`](mobile/README.md) + [`mobile/ios/README.md`](mobile/ios/README.md). The critical-path items:**
1. **Backend go-live for pairing:** the `device_tokens` table is already applied; just **redeploy the backend HF Space** so `/device/*` + flexible auth + token-hashing are live. Smoke-test pair → ingest.
2. **Web pairing UI:** add Settings → "Pair a device" (calls `POST /device/pair`, shows token + QR) and a paired-device list with revoke; add `pairDevice/listDevices/revokeDevice` to `frontend/src/lib/api.ts`.
3. **Android app:** open `mobile/android/` in Android Studio, add launcher icons, build a debug APK, test on a phone; then add GPS routes + step-baseline persistence (details in `mobile/README.md`).
3b. **iOS app:** open `mobile/ios/FitnessAI/` in Xcode (a Mac), add the HealthKit capability, build to an iPhone. iOS has **no free distribution to others** (personal-team signing = your own phone, 7-day expiry; TestFlight/App Store need the $99/yr program). Details in `mobile/ios/README.md`.
4. **Publish APK:** drop the signed APK at `frontend/public/fitness-ai.apk`; keep `frontend/public/version.json` in sync (version + apkUrl). The repo is **private**, so the version manifest + APK are served by the **public Vercel site**, NOT GitHub raw/Releases (those 404 for anonymous users). `/app` reads `/version.json`; the Android app reads `https://fitness-ai-agents.vercel.app/version.json`.

The web app is also an installable **PWA** (manifest + service worker), so iPhone/desktop users can "Add to Home Screen"; the `/app` page has an install spot per device (Android APK / iOS PWA + Apple Health / desktop PWA).
