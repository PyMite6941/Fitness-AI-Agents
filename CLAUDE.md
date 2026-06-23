# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A fitness data intelligence platform with an AI analysis pipeline:

- `backend/` — FastAPI API (Supabase DB, Clerk auth) and a CrewAI analysis crew. Deployed as a HuggingFace Space
- `frontend/` — Vite + React 19 dashboard (Clerk auth, Leaflet maps, Chart.js). Deployed on Vercel

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
`/watch` (watch.py), `/ingest` (ingest.py — generic device data ingestion, any wearable), `/routes` (gps.py), `/user` (user.py — includes `/user/sources` for connected device/integration breakdown), `/charts` (charts.py), `/analyze` (analysis.py), `/integrations` (integrations.py), `/demo` (demo.py), `/health`.

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
