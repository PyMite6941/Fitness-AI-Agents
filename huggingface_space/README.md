---
title: Fitness AI Agents
emoji: 🏋️
colorFrom: green
colorTo: blue
sdk: gradio
sdk_version: 5.27.1
python_version: "3.11"
app_file: app.py
pinned: false
license: mit
---

# Fitness AI Agents

Multi-source fitness data intelligence powered by a pipeline of 8 specialized AI agents.

## What it does

Upload your fitness data (or use sample data), ask a question, and get:

- **📊 Structured insights** — metrics, charts, comparisons, heatmaps, tables
- **🧠 Expert analysis** — exercise physiology interpretation, training load assessment
- **📈 Pattern detection** — trends, plateaus, fatigue accumulation, recovery impact
- **💡 Coaching recommendations** — evidence-based, specific, actionable

## How to use

1. **Enter your question** — e.g., "Compare my running and cycling performance this month"
2. **Load data** — use sample data (31 days of realistic watch readings) or upload your own CSV
3. **Run analysis** — the 8-agent pipeline processes your request
4. **Review results** — summary, findings, recommendations, and raw JSON

## CSV format

Uploaded files should have these columns (all optional except timestamp):

```
timestamp, heart_rate, steps, sleep_hours, hrv, calories, device, workout_type, duration_min
```

## Architecture

| Agent | Model Class | Role |
|---|---|---|
| Context Agent | fast | Interprets question → precise directive |
| Data Cleaner | fast | Validates data, flags fitness anomalies |
| Prompt Engineer | fast | Writes step-by-step analysis plan |
| Data Analyst | smart | Computes metrics, cross-references sources |
| Sports Scientist | smart | Exercise physiology interpretation |
| Trend Analyst | smart | Pattern detection, correlations |
| Output Formatter | smart | Serialises to structured JSON |
| QA Critic | fast | Scores quality, flags issues |

## Tech stack

- **CrewAI** — multi-agent orchestration
- **Groq + OpenRouter** — LLM inference
- **Gradio** — interactive UI
- **FastEmbed** — local embeddings (unused currently)

## Environment variables

Set these in your Space secrets:

- `OPENROUTER_API_KEY` — (recommended) for smart models
- `GROQ_API_KEY` — (recommended) for fast models

At least one API key is required.

## Deploying this Space

This Gradio demo wraps the same agent pipeline as the production FastAPI backend
(`backend/bots.py`). A Space is a self-contained repo, so `bots.py` must be
vendored in before pushing:

```bash
cd huggingface_space
python prepare_space.py          # copies backend/bots.py into this folder

# Create the Space once (SDK: gradio), then push this folder's contents:
huggingface-cli login            # paste a write token from huggingface.co/settings/tokens
huggingface-cli upload <user>/<space-name> . . --repo-type=space
```

Re-run `prepare_space.py` and re-upload whenever the backend agents change, so the
demo stays in sync. The production backend (the API the web app calls) is a
**separate** Docker Space and is deployed from `backend/`, not this folder.
