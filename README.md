# Fitness AI Agents

Multi-source fitness data intelligence platform. Connect your existing wearables (Apple Watch, Garmin, Fitbit, Strava), import from any major platform, and get AI-powered analysis with zero setup.

Built for the 2026 Hack America Hackathon.

## What it does

A pipeline of specialized AI agents ingests your fitness data from **any source**, analyzes it, and returns visual insights — metrics, charts, comparisons, tables, heatmaps, reports. No hardware required.

**Supported sources:**
- Strava (OAuth sync)
- Fitbit / Google Health (OAuth sync)
- Nike Run Club (file import)
- Garmin Connect (GPX/TCX import)
- Apple Health (export.xml import)
- Google Fit (Takeout import)
- Manual workout logging (web form)

## Architecture

```
User Data → Multi-Agent AI Pipeline → Visual Insights
  (8 agents: context → clean → engineer → analyst →
             sports scientist → trend → format → QA)
```

| Component | Stack |
|---|---|
| Frontend | React 19, Vite, Clerk Auth, Chart.js, Leaflet |
| Backend | FastAPI, Supabase (Postgres), CrewAI |
| AI | CrewAI pipeline with 8 agents, Groq + OpenRouter models |

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

## Mobile tracker (Android)

A native Android app (`mobile/`) tracks steps/GPS in the background — even with no watch —
and uploads to your account via a paired-device token; analysis stays server-side. Download
it from the site's **/app** page (no app store; sideload + GitHub auto-update). The phone
tracks locally and syncs when Wi-Fi/data returns. iPhone has no tracker (Apple restricts
background sensors) — connect Apple Health/Strava/Fitbit instead.

> Status: backend ingestion + device pairing are built; the Android app is **scaffolded but
> not yet compiled**. See [`mobile/README.md`](mobile/README.md) for build steps and the full
> TODO, and the "Status & Handoff TODO" in [`CLAUDE.md`](CLAUDE.md).

## License

See [license.md](license.md)
