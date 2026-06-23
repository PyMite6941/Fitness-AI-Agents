---
title: Fitness AI Agents API
emoji: 🏃
colorFrom: orange
colorTo: red
sdk: docker
pinned: false
app_port: 7860
---

# Fitness AI Agents API

Multi-source fitness data aggregation + CrewAI analysis pipeline.

**Endpoints:**
- `POST /ingest/` — push data from any wearable/app
- `POST /analyze/` — run the 8-agent AI analysis pipeline
- `GET /user/sources` — connected data sources breakdown
- `GET /charts/` — aggregated chart data
- `POST /demo/seed` — one-click demo data

Full docs: [github.com/pymite6941/fitness-ai-agents](https://github.com/pymite6941/fitness-ai-agents)
