import csv
import os
import json
from datetime import datetime, timezone
from fastapi import APIRouter, Depends, HTTPException
from auth import get_user_id
from db import get_db

router = APIRouter()

_CSV_PATH = os.path.join(os.path.dirname(__file__), "..", "test_data.csv")

_SOURCES = ["strava", "apple_health", "garmin", "manual"]


@router.post("/seed")
async def seed_demo_data(user_id: str = Depends(get_user_id)):
    db = await get_db()

    existing = await db.table("watch_data").select("id", count="exact").eq("user_id", user_id).execute()
    count = existing.count if hasattr(existing, 'count') else len(existing.data or [])
    if count and count > 0:
        raise HTTPException(status_code=409, detail="User already has data. Use /demo/reset to clear first.")

    if not os.path.exists(_CSV_PATH):
        raise HTTPException(status_code=500, detail="Seed data file not found")

    data_rows = []
    with open(_CSV_PATH, newline="") as f:
        reader = csv.DictReader(f)
        for idx, row in enumerate(reader):
            date = row["date"]
            wt = row.get("workout_type", "").strip()
            is_workout = wt and wt.lower() != "rest"
            ts = f"{date}T12:00:00+00:00"
            source = _SOURCES[idx % len(_SOURCES)]

            if is_workout:
                data_rows.append({
                    "user_id": user_id, "type": "workout",
                    "timestamp": ts, "device": source,
                    "workout_type": wt,
                    "duration_minutes": float(row.get("duration_minutes", 0) or 0),
                    "calories_burned": float(row.get("calories_burned", 0) or 0),
                    "avg_heart_rate": float(row.get("heart_rate_avg", 0) or 0),
                    "max_heart_rate": float(row.get("heart_rate_max", 0) or 0),
                })

            data_rows.append({
                "user_id": user_id, "type": "reading",
                "timestamp": ts, "device": source,
                "steps": int(float(row.get("steps", 0) or 0)),
                "sleep_hours": float(row.get("sleep_hours")) if row.get("sleep_hours") else None,
                "heart_rate": float(row.get("heart_rate_avg", 0) or 0),
            })

    for i in range(0, len(data_rows), 50):
        batch = data_rows[i:i + 50]
        await db.table("watch_data").insert(batch).execute()

    now = datetime.now(timezone.utc).isoformat()

    # ── Pre-canned analysis 1: metrics with source attribution ──
    sample_analysis = {
        "user_id": user_id,
        "context": "What does my January training tell me?",
        "summary": (
            "Your January training shows clear progress across data from Strava, Apple Health, Garmin, and manual logs: "
            "weekly running volume increased 30%, heart rate trends declined at similar effort levels "
            "suggesting cardiovascular adaptation, and rest days correlated with stronger subsequent performances. "
            "Strava recorded the highest-intensity sessions, while Apple Health contributed consistent step and sleep data."
        ),
        "key_findings": [
            "Weekly running volume increased from 35 min to 60 min sessions (+71%) — predominantly from Strava",
            "Resting heart rate from Apple Health readings trended down 5 bpm over the month",
            "Recovery days (yoga/rest) consistently preceded personal bests logged via Garmin",
            "Cycling workouts from Strava maintained higher calorie burn per minute than running",
        ],
        "anomalies": ["Jan 17 short run (20 min) followed worst sleep night (5.8 hrs) — captured by Apple Health + Garmin"],
        "recommendations": [
            "Add a recovery week every 4th week to avoid fatigue accumulation",
            "Try interval training 1x/week to improve VO2 max",
            "Increase protein intake on weightlifting days",
        ],
        "output_type": "metrics",
        "metrics": [
            {"label": "Total Workouts", "value": "18", "unit": "sessions", "change": "+28%", "trend": "up"},
            {"label": "Avg Heart Rate", "value": "132", "unit": "bpm", "change": "-4%", "trend": "down"},
            {"label": "Total Calories", "value": "5,840", "unit": "kcal", "change": "+15%", "trend": "up"},
            {"label": "Best Run", "value": "55", "unit": "min", "change": "Jan 19", "trend": "up"},
        ],
        "quality_score": 9,
        "quality_verdict": "Excellent — data-driven with clear trends and actionable insights",
        "created_at": now,
    }
    await db.table("analyses").insert(sample_analysis).execute()

    # ── Pre-canned analysis 2: chart with source-aware insights ──
    chart_analysis = {
        "user_id": user_id,
        "context": "Show me my heart rate trends across different workout types — broken down by source",
        "summary": (
            "Heart rate data aggregated from Strava, Garmin, and Apple Health reveals that running sessions "
            "consistently achieve higher average BPM than cycling or weightlifting, with peak HR hitting 195 bpm "
            "on the longest run day. Recovery heart rate improved 4% over the month. "
            "Garmin data shows slightly higher avg HR readings than Strava for equivalent efforts."
        ),
        "key_findings": [
            "Running avg HR: 147 bpm across 10 sessions (Strava + Garmin)",
            "Cycling avg HR: 138 bpm — lower than running but longer sustained zones",
            "Weightlifting sessions averaged 128 bpm with sharp peaks during heavy sets",
            "Morning resting heart rate from Apple Health declined 5 bpm from week 1 to week 4",
        ],
        "anomalies": [],
        "recommendations": [
            "Zone 2 heart rate training could improve aerobic base",
            "Consider a heart rate variability (HRV) routine for recovery tracking",
        ],
        "output_type": "chart",
        "chart_type": "bar",
        "chart_title": "Avg Heart Rate by Workout Type",
        "data_points": [
            {"label": "Running", "value": 147},
            {"label": "Cycling", "value": 139},
            {"label": "Weightlifting", "value": 128},
            {"label": "Yoga", "value": 86},
        ],
        "quality_score": 8,
        "quality_verdict": "Good — clear comparison across activity types",
        "created_at": now,
    }
    await db.table("analyses").insert(chart_analysis).execute()

    # ── Pre-canned analysis 3: source comparison ──
    comparison_analysis = {
        "user_id": user_id,
        "context": "Compare my Strava and Garmin heart rate data — are they consistent?",
        "summary": (
            "Comparing Strava and Garmin heart rate readings across similar running workouts reveals "
            "strong consistency between both sources. Strava average HR (147 bpm) aligns closely with "
            "Garmin (145 bpm) for equivalent efforts. However, Garmin tends to report slightly higher "
            "peak HR values (+3 bpm on average), suggesting different sensor sampling rates."
        ),
        "key_findings": [
            "Strava and Garmin HR averages within 2 bpm of each other for running — excellent cross-device consistency",
            "Garmin captures higher peak HR values, likely due to continuous wrist-based monitoring vs Strava's periodic GPS watch polling",
            "Both sources agree on recovery day HR patterns (yoga: 86 bpm avg from both)",
            "Combining both sources gives 2x the data density for trend analysis",
        ],
        "anomalies": ["Jan 14 shows a 5 bpm discrepancy between Strava (152) and Garmin (157) for the same running workout type"],
        "recommendations": [
            "Use Garmin for interval/peak HR tracking and Strava for GPS route accuracy",
            "Consider wearing both devices during the same workout for a cross-validation study",
        ],
        "output_type": "comparison",
        "raw_output": {
            "comparison_a_label": "Strava",
            "comparison_b_label": "Garmin",
            "comparison_rows": [
                {"metric": "Avg Heart Rate (running)", "value_a": "147 bpm", "value_b": "145 bpm", "winner": "a"},
                {"metric": "Peak Heart Rate",          "value_a": "188 bpm", "value_b": "195 bpm", "winner": "b"},
                {"metric": "Total Workouts",           "value_a": "8",       "value_b": "6",       "winner": "a"},
                {"metric": "Avg Duration",             "value_a": "41 min",  "value_b": "45 min",  "winner": "tie"},
                {"metric": "Calorie Accuracy",         "value_a": "High",    "value_b": "High",    "winner": "tie"},
            ],
        },
        "quality_score": 9,
        "quality_verdict": "Excellent — meaningful cross-device comparison with actionable insight",
        "created_at": now,
    }
    await db.table("analyses").insert(comparison_analysis).execute()

    source_counts = {}
    for s in _SOURCES:
        c = sum(1 for r in data_rows if r.get("device") == s)
        if c:
            source_counts[s] = c

    return {
        "status": "ok",
        "workouts": sum(1 for r in data_rows if r["type"] == "workout"),
        "readings": sum(1 for r in data_rows if r["type"] == "reading"),
        "analyses": 3,
        "sources": source_counts,
    }


@router.post("/reset")
async def reset_demo_data(user_id: str = Depends(get_user_id)):
    db = await get_db()
    await db.table("watch_data").delete().eq("user_id", user_id).execute()
    await db.table("analyses").delete().eq("user_id", user_id).execute()
    return {"status": "ok"}
