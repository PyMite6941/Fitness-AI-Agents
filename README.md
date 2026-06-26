# Fitness AI Agents

Multi-source fitness data intelligence platform. Connect your existing wearables (Apple Watch, Garmin, Fitbit, Strava), import from any major platform, and get AI-powered analysis with zero setup.

Built for the 2026 Hack America Hackathon.

---

## Inspiration

The inspiration for this project came with the use of a different project Matt designed the backend to, using CrewAI to conduct unbiased research. We have adapted this to now pertain to the context of Fitness and how Fitness can connect to Tech.

Most people already own at least one wearable or fitness app. The data is there — heart rate, sleep, steps, GPS, HRV — but it's completely fragmented across platforms that don't talk to each other, and even when you can see the numbers, they don't mean much without context. The people who can actually interpret that data — sports scientists, personal coaches — are expensive and inaccessible. We wanted to close that gap and put real, evidence-based training intelligence in everyone's pocket, free, using the data they're already generating.

---

## What it does

Fitness AI Agents lets you connect your fitness data from any source — OAuth sync with Strava or Fitbit, file imports from Garmin, Apple Health, COROS, Wahoo, Polar, and a dozen others — and then runs it through a pipeline of AI agents that work together the way a team of domain experts would.

From there you get five things:

- **AI Analysis** — an 8-agent CrewAI pipeline turns your raw data into metrics, charts, trend comparisons, training-load reports, and a QA-scored summary grounded in exercise physiology
- **AI Coach** — set a goal, get a week-by-week training plan that tracks your real workouts as they come in and adapts if you fall behind or overtrain
- **Daily Readiness Score** — the same recovery score Whoop and Oura charge subscriptions for, computed from HRV trend, resting HR, sleep, and acute:chronic load ratio, completely free, from any data source
- **Health Watchdog** — proactive alerts when your resting HR trends up, HRV drops, or training load spikes — no AI calls, pure deterministic sports-science logic
- **Chat with your data** — ask anything in plain English and get answers grounded in your actual history, not generated from scratch

It's installable as a PWA on any device, secured with row-level Supabase isolation and hashed device tokens, and has one-click GDPR data deletion built in.

---

## How we built it

We started with the multi-agent backend. The core AI pipeline is built on **CrewAI** — eight specialized agents that run in sequence, each passing context to the next: a Context Agent that interprets your question, a Data Cleaner that validates physiological ranges, a Prompt Engineer that writes the analysis plan, a Data Analyst that runs the numbers, a Sports Scientist that applies exercise physiology, a Trend Analyzer that detects patterns over time, a Formatter that produces structured JSON, and a QA Critic that scores the whole output for accuracy.

The backend is **FastAPI** deployed on Vercel — always-on, free tier. The database is **Supabase Postgres** with row-level security enabled on every table, so even with the right URL the anonymous key returns nothing. Authentication is **Clerk** for web users, with a separate hashed device-token system for the Android app so it can POST to `/ingest` without a browser JWT.

For the lighter AI features — Coach, Chat, and the Watchdog — we wrote `llm_lite.py`, a custom multi-model failover router that cycles across free Groq and OpenRouter models, reads rate-limit headers, and enforces a 200-call-per-user-per-day guardrail so no single account can drain the shared model pool.

The frontend is **React 19** with Vite, Chart.js for graphs, Leaflet for GPS route maps, and Clerk for auth. It's deployed on Vercel and ships as an installable PWA.

The heavy 8-agent pipeline runs in a dedicated **HuggingFace Gradio Space** because it needs the compute — it makes 20+ LLM calls per run. The lighter Vercel backend handles everything else.

---

## Challenges we ran into

**Free model rate limits.** The 8-agent pipeline makes upward of 20 LLM calls per analysis run. Free Groq and OpenRouter quotas run out fast. We had to build a proper failover router that reads the `x-ratelimit-remaining` headers and cycles models mid-run instead of failing hard.

**Cross-platform file parsing.** `.fit` files from Garmin encode data completely differently than `.fit` files from a Wahoo or COROS device. `.gpx` files from different apps have different heart rate field names and some don't include HR at all. We hit a subtle Python bug where a childless XML element evaluates as falsy, so `el.find(...) or el.find(...)` silently dropped GPX heart rate data — had to rewrite every file parser to use explicit `is None` checks.

**The authentication split.** The Android tracker can't get a Clerk JWT — it runs as a background foreground service with no browser. We had to design a second auth path: the web app issues an opaque `fit_…` device token via `POST /device/pair`, the Android app stores it in the Android Keystore, and the backend's `get_user_id_flexible` function accepts either a Clerk JWT or the device token and maps both to the same `user_id`. Getting that handshake right took a few iterations.

**Making the Readiness Score accurate.** We didn't want to just invent a score — we wanted it to reflect actual sports science. That meant researching HRV methodology, acute-to-chronic workload ratios, and resting HR baseline calculations, and tuning the formula weights so the output felt meaningful rather than arbitrary.

**Infrastructure constraints.** Free HuggingFace only runs one Space at a time. We had to split the architecture — the heavy pipeline stays in the demo Space, and the always-on API moved to Vercel — and make the two work together cleanly.

---

## Accomplishments that we're proud of

We're proud that the **Readiness Score** actually works. It produces numbers that make physiological sense — recovery goes down after hard training blocks, goes up after rest days — and it matches what Oura and Whoop produce, for free, from any data source.

We're proud of the **universal file import**. Supporting 13+ sources required writing parsers for three binary and XML formats from devices that each encode data slightly differently. It all works, and every row is tagged with the device that produced it so the AI knows the provenance.

We're proud that this is **100% live and not a demo**. Everything — the full dashboard, the 8-agent pipeline, the Coach, the Chat, the Readiness Score — is deployed and accessible right now at fitness-ai-agents.vercel.app. You can load 31 days of sample data and get a full AI analysis in under two minutes.

We're also proud of the **security model**. RLS deny-all on every Supabase table, SHA-256 hashed device tokens, Clerk JWT verification on every protected route, a per-user AI rate limit, and a GDPR one-click data erase — for a hackathon project, that's a production-grade security posture.

---

## What we learned

We learned a lot more **sports science** than we expected. Terms like TRIMP (Training Impulse), acute-to-chronic workload ratio, HRV methodology, and HR zone distribution aren't things you just know — we had to research and implement them properly so the Readiness Score and Watchdog actually mean something.

We learned how to architect a **multi-agent system that fails gracefully**. When one LLM call times out or a model hits its rate limit mid-pipeline, the whole crew can't just crash. Building the failover router and making the agents resilient to upstream failures was one of the harder engineering problems.

We learned the **real complexity of fitness data formats**. We thought file import would be straightforward. It wasn't. Every device and platform has its own quirks in how they encode timestamps, heart rate, GPS coordinates, and workout metadata. Handling all of those correctly required a lot more defensive parsing than we anticipated.

We also learned how far you can take **free-tier infrastructure** with the right architecture. The entire platform — frontend, backend API, database, and AI pipeline — runs at zero cost. That's only possible because we split the workloads correctly: heavy computation in HuggingFace, always-on API on Vercel, deterministic logic with no model calls at all where possible.

---

## Mobile apps — built in CI, downloadable now

The native apps build **in the cloud for free** via GitHub Actions (no phone, no Mac, no local toolchain) and auto-publish to the site:

- **Android (live, downloadable):** the `.github/workflows/android.yml` job compiles the Kotlin app (`mobile/android/`), then copies the APK to `frontend/public/fitness-ai.apk` and flips it on — so the **`/app` page serves a real installable APK today** (`fitness-ai-agents.vercel.app/fitness-ai.apk`). Background step/GPS tracking, device-token pairing, offline queue. Pending: real-hardware testing.
- **iOS (free sideload):** `ios.yml` compiles on a macOS runner and packages an **unsigned `.ipa`**, published to `/app`. Anyone installs it free with **AltStore / SideStore / Sideloadly** using their own Apple ID (7-day renew) — no App Store, no $99 program to *download*. Full Apple Health access requires a paid Apple Developer account.

### Still on the roadmap
- **Always-on in-dashboard AI analysis** — the 8-agent pipeline runs in the demo today; making it always-on for every user needs a dedicated model key with quota (funding, not engineering).
- **FitnessAI Watch** — a custom open-hardware ESP32-C3 smartwatch (HR, steps, GPS) that pairs directly. Earlier firmware archived in `watch-archive/`.
- **Weekly digest** — scheduled email: training load, readiness trend, coach progress, and a standout AI insight.

---

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
| Mobile | Native Android (Kotlin) + iOS (SwiftUI/HealthKit), built free in GitHub Actions → published to `/app` |

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

**Get the apps:** the **`/app`** page serves the installable **Android APK** today and (when the iOS job has run) a sideloadable **`.ipa`** — both built free in GitHub Actions.

## License

See [license.md](license.md)
