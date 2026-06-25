# Fitness AI Agents

Multi-source fitness data intelligence platform. Connect your existing wearables (Apple Watch, Garmin, Fitbit, Strava), import from any major platform, and get AI-powered analysis with zero setup.

Built for the 2026 Hack America Hackathon.

## What it does

Ingest your fitness data from **any source**, then get AI-powered insights, a personalized
coach, a daily readiness score, proactive health alerts, and a chat you can ask anything.
No hardware required.

**Features:**
- **AI Analysis** — an 8-agent pipeline turns your data into metrics, charts, comparisons, and reports
- **AI Coach** — set a goal → a weekly plan that tracks your real workouts and adapts
- **Daily Readiness Score** — recovery score from HRV, resting HR, sleep & training load (the Whoop/Oura feature, free, from any source)
- **Health Watchdog** — proactive alerts for rising resting HR, dropping HRV, or training-load spikes
- **Chat with your data** — conversational, grounded in your own history

**13+ import sources (no account or device needed for most):**
- OAuth sync: Strava, Fitbit / Google Health
- Universal file import: any `.fit` / `.tcx` / `.gpx` → COROS, Suunto, Wahoo, Polar, Zwift, Peloton, MapMyRun, Garmin
- File import: Nike Run Club, Apple Health, Google Fit
- Manual logging + GPS route tracking

Installable as a **PWA** on any device; secured with row-level isolation, hashed device tokens,
generous per-user rate limits, and one-click data deletion.

## Architecture

```
User Data → Multi-Agent AI Pipeline → Visual Insights
  (8 agents: context → clean → engineer → analyst →
             sports scientist → trend → format → QA)
```

| Component | Stack |
|---|---|
| Frontend | React 19, Vite, Clerk Auth, Chart.js, Leaflet · installable PWA |
| Backend | FastAPI on **Vercel** (always-on), Supabase (Postgres, row-level security) |
| AI — analysis | 8-agent **CrewAI** pipeline (runs in the HF demo Space) |
| AI — coach / chat | Single-call, multi-model failover (`llm_lite.py`), Groq → OpenRouter, quota-aware |
| AI — readiness / alerts | Deterministic sports-science math (no model calls) |

## Quick Start

```bash
# Backend
cd backend
python -m venv .venv
source .venv/bin/activate  # or .venv\Scripts\activate on Windows
pip install -r requirements.txt
cp .env.example .env       # fill in your keys
uvicorn main:app --reload

# Frontend
cd frontend
npm install
npm run dev
```

## Demo

[Live demo](https://fitness-ai-agents.vercel.app) — sign up, click "Load Sample Data", and see the full dashboard populated with 31 days of realistic fitness data and AI analysis results.

Try the agents with **no signup**: the `/demo` sample dashboard, or the live Gradio demo embedded on the landing page (also at https://pymite6941-fitness-ai-agents-demo.hf.space).

## Roadmap — features for the final product

The web platform above is complete and live. These are the planned additions for the full
product; the backend already supports the data path for the phone apps (pairing + ingest),
and scaffolds live in [`mobile/`](mobile/).

- **Native Android tracker** — background steps + GPS (even with no watch), syncing to your
  account via a paired-device token; installed from the site's `/app` page (no app store).
  Scaffold in `mobile/android/`; ships when built/signed in Android Studio.
- **Native iOS app (HealthKit)** — reads steps/HR/sleep from iPhone *and a paired Apple Watch*
  in the background and syncs to your account. Scaffold in `mobile/ios/`.
- **FitnessAI Watch** — a custom open-hardware ESP32 smartwatch (HR, steps, GPS workouts)
  that pairs to the app and uploads directly. Earlier firmware is archived in `watch-archive/`.
- **Live in-dashboard AI analysis at scale** — the full 8-agent pipeline runs in the demo
  today; bringing it always-on in the dashboard needs a dedicated (funded) model key.
- **Ops:** weekly email digest, CI test suite, and error monitoring.

Until the native trackers ship, the installable **PWA** + file imports cover every platform.

## License

See [license.md](license.md)
