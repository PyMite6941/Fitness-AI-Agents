# Speaking Notes — Fitness AI Agents
## 2026 Hack America Hackathon · ~7.5 minutes

These are your word-for-word scripts for each slide. The time targets are written beside each heading — if you hit them you land exactly at 7:20 with 10 seconds to breathe before Q&A.

---

## Slide 1 — Title `[0:00 → 0:30]`

> "This is Fitness AI Agents — a platform built to solve one problem: you're already generating a huge amount of health data every day, but almost none of it is being used to actually help you train smarter.
>
> I'll walk you through why this matters, what we built, how it works under the hood, and why it's genuinely different. About seven and a half minutes total."

**Then advance immediately.** Don't read the three credential pills on the right — the audience will read them while you talk.

---

## Slide 2 — The Problem `[0:30 → 1:30]`
### Judging criterion: IMPACT — "Does it solve a big problem?"

> "Start with the problem. Over a billion people are walking around with wearables — smartwatches, Fitbits, Garmins, phones in their pockets. They're generating heart rate, sleep, GPS, and step data every single day.
>
> But that data is completely fragmented. Your Apple Watch knows your sleep. Strava knows your runs. Garmin knows your heart rate zones. None of them know about each other.
>
> And even when you can see your numbers, they're useless without context. What does a resting heart rate of 58 bpm mean for YOUR training today? Is that good? Is it trending the wrong way? Does it matter that you slept six hours last night and ran 10k yesterday? You don't know — and the apps won't tell you.
>
> The people who can actually answer those questions — sports scientists, personal coaches — charge $150 to $300 a month. That puts real training intelligence completely out of reach for most people.
>
> Billions of data points collected. Almost none of it acted on."

---

## Slide 3 — The Solution `[1:30 → 2:20]`
### Judging criterion: PRESENTATION — "What challenge does it address and how?"

> "The solution is three steps.
>
> **Connect** — you bring your data from wherever it lives. Strava and Fitbit sync over OAuth. Apple Health and Garmin export as files. Drop in any .fit, .tcx, or .gpx from any device — COROS, Wahoo, Polar, Suunto, Peloton, Zwift. Thirteen sources supported at launch. Zero new hardware required.
>
> **Analyze** — the data goes through an 8-agent AI pipeline that reasons like a team of domain experts. I'll show you the architecture in a moment.
>
> **Act** — you get a personalized weekly training plan, a daily readiness score, proactive health alerts when something looks wrong, and a chat interface where you can ask anything in plain English and get an answer grounded in your actual data.
>
> That's the full loop: raw sensor data to a decision you can act on today."

---

## Slide 4 — Creativity `[2:20 → 3:35]`
### Judging criterion: CREATIVITY — "Hasn't been attempted before, or incremental improvement?"

**This is the most important slide for the creativity score. Be specific and confident — don't rush it.**

> "Let me be specific about what's new here, because this isn't a thin wrapper around an API.
>
> First — the multi-agent pipeline. Eight purpose-built agents, each with a defined role and domain knowledge. The Data Cleaner knows that heart rate below 30 bpm or above 250 is invalid. The Sports Scientist calculates your acute-to-chronic training load ratio and interprets overtraining risk. The QA Critic scores every output for physiological accuracy. They run in sequence, each one passing context to the next.
>
> Second — the Readiness Score. Whoop charges $30 a month for this. Oura charges $18 a month. Both sell a 'recovery score' that tells you if you're ready to train. We built the same thing — HRV trend, resting HR baseline, sleep quality, training load ratio — deterministically, from sports-science formulas, at zero cost, from any data source. No subscription. No proprietary hardware.
>
> Third — universal import. Most platforms lock you in. Drop in a .gpx from Wahoo or a .fit from COROS — it parses it, tags the source, and the AI knows which device produced which row and reasons accordingly. Garmin data looks different from Apple Watch data. The pipeline is aware of that difference.
>
> Fourth — the architecture is split intelligently. The heavy 8-agent pipeline runs in a HuggingFace Space where it has the compute it needs. The lighter features — Coach, Chat, Readiness — run on an always-on Vercel backend via a custom multi-model failover router we wrote, with a 200-call-per-user-per-day limit so one account can't drain the shared model pool."

---

## Slide 5 — Five Features `[3:35 → 4:50]`
### Judging criterion: PRESENTATION — "How does it deliver a solution?"

> "Five features — let me walk through each one concretely.
>
> **AI Analysis.** Hit Analyze and the 8-agent pipeline processes your data. You get structured outputs — metric cards with your weekly averages, line charts of HR trends over time, a comparison of your Strava runs versus Garmin workouts, a heatmap of training load by day of week. Every result is scored out of 10 by the QA agent for physiological accuracy.
>
> **AI Coach.** Tell it your goal — say, run a 5K in under 25 minutes in eight weeks. It generates a week-by-week training plan. As your real workouts come in, the plan updates. Overtrain one week, it backs off the next. Miss a session, it rebalances.
>
> **Readiness Score.** Every morning, a score out of 100. Four inputs: your HRV trend over the last seven days, your resting HR versus your personal baseline, last night's sleep, and your acute-to-chronic training load ratio. Same science as Oura and Whoop. Completely free.
>
> **Health Watchdog.** No AI calls at all — pure deterministic logic. Resting heart rate climbing for five straight days? HRV dropped more than 10% this week? Training load spiked past your chronic baseline? You get flagged proactively. You don't have to ask. It tells you.
>
> **Chat.** Ask 'how has my sleep been affecting my runs?' or 'should I take a rest day tomorrow?' in plain English. The answer is grounded in a server-built summary of your actual data — not generated from scratch — so it can't hallucinate."

---

## Slide 6 — Architecture `[4:50 → 5:50]`
### Judging criterion: CREATIVITY + PRESENTATION — technical depth

> "Here's how the pipeline actually works.
>
> Eight agents run in sequence. Context reads your question and identifies which metrics and sources matter. Cleaner validates every row — it knows what valid ranges are for heart rate, steps, sleep. Prompt Engineer writes the analysis plan. Data Analyst executes it. Sports Scientist interprets findings through exercise physiology and calculates training load using TRIMP. Trend Analyzer finds time-series patterns — plateau, progressive overload, fatigue accumulation. Formatter serializes everything into structured JSON. QA Critic scores the whole output and flags physiological errors.
>
> For the stack: React 19 frontend on Vercel. FastAPI backend also on Vercel — always-on, free tier, no cold starts. Supabase Postgres with row-level security enabled on every table — the anonymous key returns zero rows, period. Heavy pipeline in HuggingFace. Light features run through a custom LLM failover router that cycles across free Groq and OpenRouter models and enforces the per-user rate limit."

---

## Slide 7 — Impact `[5:50 → 6:50]`
### Judging criterion: IMPACT — "Will it inspire or help many or a few?"

**Make the numbers land — pause briefly after each one.**

> "How big is the impact?
>
> One billion people own wearables with data they're not using. This is built for exactly that person.
>
> Cost to the user: zero. Whoop, Oura, and a personal trainer together cost somewhere between $50 and $350 a month. This platform replaces all three of those features — free, running on free-tier infrastructure.
>
> Thirteen-plus import sources at launch means almost no one is excluded. If you've used any fitness app or worn any tracker in the last five years, you can import your data today.
>
> And this is not a prototype. It is deployed and live right now at fitness-ai-agents.vercel.app. You can go there in the next five minutes, click 'Load Sample Data', and see the full dashboard — charts, heatmaps, AI analysis, readiness score — populated in about 90 seconds."

---

## Slide 8 — Closing `[6:50 → 7:20]`

> "To wrap up — Fitness AI Agents takes the data you're already generating, breaks it out of its silos, and puts a team of AI sports scientists in your pocket. For free. No new hardware. Works with what you already have.
>
> It is live right now. You can try it after this.
>
> I'm happy to take questions on the architecture, the agent pipeline, or the security model."

**Then stop. Don't add anything. Let them ask.**

---

## Likely Q&A

**"How does the pipeline handle bad data?"**
The Data Cleaner agent flags physiologically impossible values — HR below 30 or above 250 bpm, steps over 100,000 in a day, sleep over 16 hours — and removes or annotates them before any analysis runs. It also reports missing columns and timestamp gaps.

**"What happens when the free model quota runs out?"**
`llm_lite.py` is a custom failover router that reads rate-limit headers and cycles through a pool of free Groq and OpenRouter models. If one hits its limit, it moves to the next. The 200-call-per-user-per-day guardrail also prevents any single account from draining the shared pool.

**"Is this secure? My health data is sensitive."**
Three layers. First, Supabase row-level security is on for every table — even with the correct Supabase URL, the anonymous key returns zero rows. All reads and writes go through the FastAPI backend with the service role key. Second, phone pairing tokens are stored as SHA-256 hashes — a database dump can't forge uploads. Third, there's a one-click GDPR data deletion endpoint at `DELETE /user/data`.

**"Why not use one big LLM call instead of 8 agents?"**
One call loses domain specialization. A single prompt can't simultaneously be a data validator, a sports scientist, a trend detector, and a QA critic. Separating them into agents with defined roles, each passing context forward, produces verifiably more accurate and specific outputs. The QA Critic at the end can catch errors the earlier agents introduced, which a single-call architecture can't do.

**"What's stopping someone from copying this?"**
Nothing, and that's fine — it's MIT licensed. The moat is the data: as users import more sources and the platform learns their baseline, the insights get more personalized. Generic fitness apps can't compete with analysis grounded in your own 6-month history.
