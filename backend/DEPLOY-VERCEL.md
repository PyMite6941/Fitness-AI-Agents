# Deploying the backend to Vercel (always-on, free)

> **LIVE:** deployed at **https://backend-seven-topaz-23.vercel.app** (Vercel project
> `backend`, team `matt-gs-projects-e73d6b76`). Env vars set, deployment protection OFF,
> frontend `VITE_API_URL` points here. `/health` → 200; `/ingest` with a `fit_` token → 401
> (auth chain verified live). The steps below are for re-deploying / reproducing.


The backend moved off HuggingFace (free HF allows only one running Space, which the
agent demo uses). This light FastAPI app runs on Vercel's Python runtime — always-on,
no cold-pause. `/analyze` (the heavy crew) is NOT here; it lives in the demo Space.

## Files that make this work
- `api/index.py` — Vercel ASGI entry (`from main import app`).
- `vercel.json` — routes all paths to the function; `maxDuration: 60`.
- `requirements.txt` — LIGHT deps only (no crewai/litellm/pandas/fastembed).
- `.vercelignore` — keeps `bots.py`/test data out of the deploy.

## First deploy
From `backend/`:
```bash
vercel link            # create/link a NEW project, e.g. "fitness-ai-backend"
vercel --prod          # deploy to production
```
(or non-interactively: `vercel --prod --yes --token=$VERCEL_TOKEN`)

## Environment variables (set on the new Vercel project → Settings → Environment Variables)
Copy these from `backend/.env` (Production scope):
```
SUPABASE_URL, SUPABASE_KEY, SUPABASE_ANON_KEY, SUPABASE_DB_PASSWORD
CLERK_JWKS_URL, CLERK_ISSUER_URL
OAUTH_STATE_SECRET
STRAVA_CLIENT_ID, STRAVA_CLIENT_SECRET
GOOGLE_HEALTH_CLIENT_ID, GOOGLE_HEALTH_CLIENT_SECRET
BACKEND_URL   = https://<this new backend>.vercel.app
FRONTEND_URL  = https://fitness-ai-agents.vercel.app
ALLOWED_ORIGINS = https://fitness-ai-agents.vercel.app,http://localhost:5173
```
(OPENROUTER/GROQ are NOT needed here — `/analyze` degrades to a 503 pointing at the demo.)

Then redeploy so the env vars take effect: `vercel --prod`.

## Point the frontend at the new backend
On the **frontend** Vercel project (`fitness-ai-agents`): set
`VITE_API_URL = https://<new backend>.vercel.app` (Production), then redeploy the frontend
(`git commit --allow-empty` + push, or redeploy from the dashboard). Vite bakes it at build.

## Verify
- `GET https://<new backend>.vercel.app/health` → `{"status":"ok"}`
- A protected route without a token → `401` (auth working).
- From the web app: sign in, open the dashboard → data loads (charts/streak/sources).
- Phone `/ingest` with a pairing token → row in `watch_data`.

## OAuth callback URLs (if you use Strava/Fitbit)
Update the callback URLs in the Strava/Google consoles to the new `BACKEND_URL`
(`{BACKEND_URL}/integrations/strava/callback`, etc.).
