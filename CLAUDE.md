# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A fitness data intelligence platform with an AI analysis pipeline:

- `backend/` — FastAPI API (Supabase DB, Clerk auth) and a CrewAI analysis crew. Deployed as a HuggingFace Space
- `frontend/` — Vite + React 19 dashboard (Clerk auth, Leaflet maps, Chart.js). Deployed on Vercel. Public pages: `/demo` (no-auth sample dashboard), `/app` (Android tracker download + GitHub version check)
- `mobile/` — native **Android** (Kotlin) + **iOS** (SwiftUI/HealthKit) tracker apps that record steps/GPS/Health and POST to `/ingest`. **Built free in GitHub Actions** (`.github/workflows/android.yml` / `ios.yml`) and auto-published to the site: the Android APK is live + downloadable from `/app`; iOS builds a sideloadable `.ipa`. Not yet tested on physical hardware. `frontend/public/version.json` is the public source of truth for app version/availability/URLs.

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
- **`user_integrations`** holds OAuth tokens (Strava/Fitbit). **`device_tokens`** = hashed phone-pairing tokens. **`coach_plans`** = AI Coach plans (JSONB `plan`, RLS on). **`ai_usage`** = per-user/day AI call counter for rate limiting.
- All rows are keyed by Clerk `user_id` (a TEXT `sub` claim). There is no users table. **RLS is ON (deny-all) for every table** — only the backend's `service_role` key reads/writes; the anon key returns nothing.

### API (`backend/main.py` mounts routers under prefixes)
`/watch`, `/ingest` (generic device ingestion, any wearable — accepts a device token), `/routes` (gps.py), `/user` (incl. `/user/sources` and `DELETE /user/data` = GDPR erase), `/charts`, `/analyze` (heavy CrewAI — **503 on Vercel**, runs in the demo Space / locally), `/integrations`, `/demo`, `/device` (phone pairing tokens), `/coach` (AI Coach plans), `/insights` (Readiness + Watchdog), `/chat` (chat with your data), `/health`.

### AI features beyond `/analyze` (the light, Vercel-friendly ones)
`/analyze` is the heavy 8-agent CrewAI pipeline (demo Space only). The newer AI features are **single LLM calls** via `backend/llm_lite.py` (rotates across a pool of free Groq + OpenRouter models on 429/error, reads rate-limit headers, returns remaining quota) so they run on the light Vercel backend:
- **`/coach`** (`routes/coach.py`): goal → AI weekly plan (`coach_plans`), progress matched to real workouts, `POST /coach/adapt` re-plans. JSON-mode call.
- **`/insights`** (`routes/insights.py`): **deterministic** (no AI) — Readiness score (HRV/resting-HR trend, sleep, acute:chronic load) + Watchdog alerts.
- **`/chat`** (`routes/chat.py`): conversational, grounded in a server-built data summary.
- **Rate limiting** (`backend/ratelimit.py` + Postgres `bump_ai_usage()`): generous **200 AI calls/user/day** guardrail so one account can't drain the shared free model pool. Coach/Chat call `enforce_ai_limit`; usage is returned in responses.

### Device pairing & flexible auth (`backend/auth.py`, `backend/routes/device.py`)
The Android tracker can't get a Clerk JWT, so it uses an opaque **device token**. `POST /device/pair` (Clerk-authed) issues a `fit_…` token stored in `device_tokens` (see `schema.sql`); `GET /device/list` + `POST /device/revoke` manage them. `get_user_id_flexible` accepts EITHER a Clerk JWT OR a `fit_…` token and is used by `/ingest`, so phone uploads land in `watch_data` under the user's `user_id`. Tokens are stored as **SHA-256 hashes** (`backend/auth.py::hash_device_token`) — a DB leak can't forge uploads. **This is live** (`device_tokens` applied + backend deployed; verified end-to-end).

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
Strava OAuth + Fitbit/Google-Health OAuth + file imports. **Universal `/integrations/file/import`** takes `.fit` (via `fitdecode`) / `.tcx` / `.gpx` from any app (COROS, Suunto, Wahoo, Polar, Zwift, Peloton, MapMyRun, Garmin…), tagged by a `source` field — plus the format-specific Nike/Apple/Google importers. The parsers (`_parse_gpx`/`_parse_tcx`/`_parse_fit`) take a `source` arg for attribution. (Oura/WHOOP OAuth was removed — they need paid accounts to register a dev app.) GOTCHA fixed here: a childless ElementTree element is falsy, so `el.find(...) or el.find(...)` silently dropped GPX heart rate — use explicit `is None`.

### Frontend (`frontend/src`)
`main.jsx` wires Clerk + react-router. **Protected** routes: `/dashboard`, `/routes`, `/log`, `/coach` (AI Coach + Readiness + Watchdog hub), `/chat` (chat with your data). **Public**: `/` (landing), `/demo` (no-auth sample dashboard), `/app` (PWA install + Android download), `/privacy` (policy + data-delete button). The Dashboard sidebar links to Coach/Chat/Routes/Log and shows an onboarding card when empty. All API calls go through `src/lib/api.ts`; `VITE_API_URL` points at the Vercel backend. The whole app is an installable **PWA** (manifest + service worker + heartbeat icon).

## Deployment

- **Frontend → Vercel**, project `fitness-ai-agents`, **auto-deploys on push to `main`** (the entire deploy history is git-triggered). Just push to `main`.
- **Backend API → Vercel** (project `backend`, `https://backend-seven-topaz-23.vercel.app`), **always-on, free**. Vercel's FastAPI preset serves `main.py:app`; deps are the LIGHT `requirements.txt` (no crewai). Env vars are set on the Vercel project (mirrors `backend/.env`); deployment protection is OFF (it's a public API, auth is Clerk inside). The frontend's `VITE_API_URL` points here. Deploy from `backend/` with `vercel --prod` (see `backend/DEPLOY-VERCEL.md`). **Moved off HuggingFace** (the old `pymite6941/data-analyst-ai-agent` Docker Space) because free HF runs only one Space and the agent demo uses it. `/analyze` (heavy crew) is NOT on Vercel — it returns a 503 pointing at the demo Space; the full pipeline runs in the demo / locally with `requirements-crew.txt`.
- **Standalone Gradio demo → a separate HuggingFace Gradio Space** `pymite6941/fitness-ai-agents-demo` (`https://pymite6941-fitness-ai-agents-demo.hf.space`). Public, no-auth showcase of the same agent pipeline. It vendors a copy of `backend/bots.py` (run `python huggingface_space/prepare_space.py` before each deploy); `huggingface_space/README.md` has the full deploy steps. The vendored `bots.py` is gitignored in this repo but committed to the Space. `OPENROUTER_API_KEY` + `GROQ_API_KEY` are set as Space secrets. Deploy/update with `huggingface_hub.upload_folder(...)` or `huggingface-cli upload`.

## Gotchas

- The live 8-agent pipeline makes ~20+ LLM calls/run, which exceeds free-tier quotas (OpenRouter ~50 free req/day without credits, Groq per-minute). The Gradio demo's "Use sample data" therefore returns a **cached** result (`huggingface_space/app.py::_SAMPLE_RESULT`); only real uploads run live. For reliable live runs, use a dedicated OpenRouter key with $10 credit (=1000 req/day).
- `backend/.env` ships with **placeholder** model keys. Real keys live in the deployed Space secrets (and were borrowed from sibling projects during setup). HF Space secrets can't be read back via API.
- Gradio Space dep set that builds: **gradio 5.27.1 + crewai 1.14.5 + litellm 1.85.x**, Python 3.11 (see `huggingface_space/requirements.txt` comments for why each pin). Don't gitignore `bots.py` inside `huggingface_space/` — the HF uploader honors it and drops the file.

## Status & Handoff TODO (2026-06-25)

**The product is complete and live** (frontend auto-deploys on push to `main`; backend deployed with `cd backend && vercel --prod`). Shipped + verified:
- Pivot off the custom watch → multi-source platform (firmware archived in `watch-archive/`).
- **Backend on Vercel, always-on** (moved off HF — free HF runs only one Space, used by the demo). Light `requirements.txt`; `/analyze` proxies/degrades to the demo Space.
- **5 AI features:** `/analyze` (8-agent CrewAI, demo Space), **AI Coach** (`/coach`), **Readiness + Watchdog** (`/insights`, deterministic), **Chat** (`/chat`) — the latter three via `llm_lite.py` (multi-model failover, quota-aware) on the light backend.
- **Security:** RLS deny-all on all tables (anon key returns 0 rows — verified); device tokens hashed; **per-user AI rate limit 200/day** (`ratelimit.py`); **`DELETE /user/data`** GDPR erase + `/privacy` page.
- **13+ import sources** incl. universal `.fit/.tcx/.gpx`; Strava/Fitbit OAuth; manual + GPS.
- Live Gradio agent demo (HF) embedded on the landing; `/demo` sample dashboard; `/app` PWA-install + Android download page; installable **PWA** (heartbeat icon); empty-state onboarding.
- Phone pairing (`/device/*`, hashed tokens) live + verified end-to-end, with a web UI at `/devices`. **Native apps build free in CI** (`.github/workflows/`): Android APK is live + downloadable from `/app`; iOS produces a sideloadable `.ipa`.
- **Stress-tested** every component alone and together (DB → summary → LLM → output).

**Remaining (config / hardware / optional — not blockers):**
1. **OAuth redirect URIs** — register the exact callback in the Google + Strava consoles (`backend/DEPLOY-VERCEL.md` has them: `…/integrations/fitbit/callback` + Strava domain). Backend already builds them from `BACKEND_URL`. File imports need none of this.
2. **Native apps** — now built **free in GitHub Actions** and auto-published to `/app` (Android APK live + downloadable; iOS sideloadable `.ipa` via `ios.yml`). Remaining: test on a real device, and a paid Apple Developer account for full HealthKit on iOS. The first iOS CI run may need a small fix (the Swift had never been compiled). Run builds from the repo's **Actions** tab.
3. **Optional polish:** CI test suite, error monitoring (Sentry).
