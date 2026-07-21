# FitnessAI — Positioning & Go‑To‑Market (one‑pager)

## One‑liner
**The one place that unifies every fitness data source you already own — and turns it into coach‑grade guidance no single‑device app can give.**

## The problem
Athletes' data is scattered across Strava, Garmin, Apple Health, Oura, Whoop, Fitbit, Zwift, Peloton and a dozen apps. Every ecosystem analyzes **only its own data** and quietly locks you in. Nobody reads *across* sources — so the most useful insight ("your HRV dropped three days after you added Zwift intervals") lives in the gaps between apps, where no vendor is looking.

## The wedge — why we win
**Aggregation × AI.** We already ingest **13+ sources**, including universal `.fit` / `.tcx` / `.gpx` (COROS, Suunto, Wahoo, Polar, Zwift, Peloton, Garmin…). On top of that *unified* history we run four AI/analytics layers nobody else can, because nobody else has the combined data:
- 8‑agent deep analysis (`/analyze`)
- Adaptive **AI Coach** (goal → weekly plan that re‑plans against real workouts)
- Deterministic **daily Readiness** score (HRV/RHR/sleep/training‑load)
- **Health Watchdog** alerts (rising RHR, dropping HRV, load spikes, inactivity)

The moat is the **union of sources × AI reasoning**: the more devices a user owns, the more valuable we are — and the harder we are to leave.

## ICP (start with ONE)
- **Primary — multi‑device endurance athletes** (runners / cyclists / triathletes) who own 2+ of {Garmin, Strava, Whoop, Oura, Zwift} and already export `.fit` files. They feel the fragmentation daily, already pay for tools (TrainingPeaks ~$20/mo, Whoop ~$30/mo), and gather in findable places: r/running, r/Garmin, r/triathlon, r/Whoop, TrainerRoad forums.
- **Secondary** — Whoop/Oura owners who want cross‑device context their band alone can't provide.

## Value prop, by how the buyer hears it
- "See *all* your training in one timeline, finally."
- "A coach that reads every source — not just one brand."
- "Every morning: am I recovered? One number, and *why*."

## Pricing (land above COGS, anchor below incumbents)
| Tier | Price | What's in it |
|---|---|---|
| **Free** | $0 | Connect all sources + dashboard + daily Readiness + 3 AI analyses/mo |
| **Pro** | **$9/mo** or **$79/yr** | Unlimited AI Coach/Chat/Analysis, all sources, daily push, Watchdog, priority models |

- Wire tiers to the existing `ai_usage` / `ratelimit.py` hooks (Free = capped, Pro = high cap).
- Anchor: cheaper than TrainingPeaks/Whoop, and it's the **layer above** them, not a replacement.
- **COGS:** metered AI is the only variable cost — a funded OpenRouter/Groq key + response caching keeps Pro margins healthy. Retiring the free‑model crutch is a prerequisite to charging.

## Moat / defensibility
1. **Integration breadth** — each new source compounds; painful for a single‑device incumbent to match.
2. **Cross‑source AI memory** — longer you're in, the better the guidance → real switching cost.
3. **Portability as trust** — import everything, export/delete anytime (`DELETE /user/data` already ships) → wins privacy‑conscious buyers.

## Retention loop (shipping now)
**Daily Readiness + Watchdog push** every morning → open app → see *why* → act. That's the habit that makes a subscription stick. *(Built in the accompanying PR.)*

## Launch plan — 0 → first 100 paying
1. **Pre‑launch (2 wks):** landing page on the "unify everything" promise + waitlist; lead with the zero‑setup `/demo` dashboard so people see value instantly.
2. **Reddit‑first** (use the `product-research` playbook — genuine value, no spam): "I built a tool that reads across *all* your fitness apps" in r/running, r/Garmin, r/triathlon, r/Whoop.
3. **Product Hunt** once billing + the push loop are live (assets: the demo + the daily‑insight hook).
4. **SEO/content:** own the category term — "how to combine Garmin + Strava + Whoop data," "multi‑source fitness AI."
5. **App stores:** Google Play closed → open → prod to unlock mobile acquisition + native Android push.

## Gates before charging money
- Funded, reliable inference (retire the free‑pool rotation).
- Real billing — **Polar.sh** (lower fees than Stripe; already researched in `Marketplace-Setup-Guide.md`).
- Terms + Privacy + **production OAuth approval** (Strava / Fitbit / Google redirect URIs).

## 30 / 60 / 90
- **30:** billing live (Polar) · funded AI key + caching · push loop (this PR) · Terms/Privacy.
- **60:** Play Store listing · Reddit + PH launch · PostHog funnel · Sentry.
- **90:** 100 signups / 10 paying; iterate pricing + onboarding on funnel data.

## Metrics that matter
- **Activation:** connected ≥1 source *and* saw first Readiness.
- **Retention:** D7 / D30 app opens (push‑driven).
- **Conversion:** Free → Pro.
- **North star:** weekly active users who opened a daily insight.
