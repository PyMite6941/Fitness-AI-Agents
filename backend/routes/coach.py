"""AI Coach — goal-based adaptive training plans.

Plan generation is a SINGLE structured LLM call (Groq -> OpenRouter failover via
httpx), so it runs fine on the light Vercel backend (no crewai). Progress is
computed deterministically by matching the user's real workouts to planned days.
"""
import json
import re
from datetime import date, datetime, timedelta, timezone

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel

from auth import get_user_id
from db import get_db
from llm_lite import complete, LLMUnavailable

router = APIRouter()


class PlanRequest(BaseModel):
    goal: str
    weeks: int = 8


def _extract_json(text: str) -> dict:
    text = (text or "").strip()
    text = re.sub(r"^```(?:json)?\s*", "", text)
    text = re.sub(r"\s*```$", "", text)
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        s, e = text.find("{"), text.rfind("}")
        if s != -1 and e != -1:
            return json.loads(text[s:e + 1])
        raise


async def _llm_json(system: str, user: str) -> tuple[dict, dict]:
    """One structured call across the resilient model pool. Returns (plan, quota)."""
    msgs = [{"role": "system", "content": system}, {"role": "user", "content": user}]
    try:
        content, quota = await complete(msgs, json_mode=True, max_tokens=4000)
    except LLMUnavailable as e:
        raise HTTPException(status_code=503, detail=str(e))
    try:
        return _extract_json(content), quota
    except Exception:
        raise HTTPException(status_code=502, detail="The AI returned an unparseable plan — please try again.")


async def _recent_summary(db, user_id: str) -> str:
    since = (datetime.now(timezone.utc) - timedelta(days=28)).isoformat()
    res = await (db.table("watch_data").select("workout_type,duration_minutes,distance_meters")
                 .eq("user_id", user_id).eq("type", "workout").gte("timestamp", since).execute())
    rows = res.data or []
    if not rows:
        return "No recent workout history — assume a beginner-to-intermediate base."
    types: dict = {}
    total_min = total_km = 0.0
    for r in rows:
        t = r.get("workout_type") or "other"
        types[t] = types.get(t, 0) + 1
        total_min += r.get("duration_minutes") or 0
        total_km  += (r.get("distance_meters") or 0) / 1000
    mix = ", ".join(f"{k} x{v}" for k, v in types.items())
    return (f"Last 4 weeks: {len(rows)} workouts ({mix}); "
            f"~{round(total_min/4)} min/week, ~{round(total_km/4, 1)} km/week.")


def _build_plan_prompt(goal: str, weeks: int, summary: str, adherence: str = "") -> tuple[str, str]:
    system = ("You are an elite endurance and strength coach. You design safe, progressive, "
              "personalized training plans. Output ONLY valid JSON, no prose.")
    user = (
        f'Create a {weeks}-week training plan for this goal: "{goal}".\n'
        f"Athlete's recent data: {summary}\n"
        f"{adherence}\n"
        "Return JSON with this exact shape:\n"
        '{"title": "short plan name", "weeks": [{"week": 1, "focus": "phase focus", '
        '"days": [{"day": "Mon", "type": "easy_run|tempo|intervals|long_run|strength|cross|rest", '
        '"title": "short", "detail": "1 specific sentence", '
        '"target_distance_km": number_or_null, "target_minutes": number_or_null}]}]}\n'
        f"Exactly {weeks} week objects; each week has exactly 7 day objects Mon..Sun. "
        "Apply progressive overload, ~1-2 rest days/week, and taper the final week. Be specific but concise."
    )
    return system, user


def _annotate_progress(plan: dict, start: date, weeks: int, workout_dates: set) -> dict:
    """Attach per-day `date`/`done` and an overall `current_week` + counts (deterministic)."""
    today = date.today()
    cur_week = max(1, min(weeks, (today - start).days // 7 + 1))
    done_total = planned_total = 0
    for wk in plan.get("weeks", []):
        wnum = wk.get("week", 1)
        for i, day in enumerate(wk.get("days", [])[:7]):
            d = start + timedelta(days=(wnum - 1) * 7 + i)
            day["date"] = d.isoformat()
            is_workout = "rest" not in (day.get("type") or "rest").lower()
            day["done"] = is_workout and d.isoformat() in workout_dates
            day["past"] = d < today
            if wnum == cur_week and is_workout:
                planned_total += 1
                if day["done"]:
                    done_total += 1
    plan["current_week"] = cur_week
    plan["week_done"] = done_total
    plan["week_planned"] = planned_total
    return plan


@router.post("/plan")
async def create_plan(body: PlanRequest, user_id: str = Depends(get_user_id)):
    weeks = max(1, min(24, body.weeks))
    goal  = (body.goal or "").strip()[:300]
    if not goal:
        raise HTTPException(status_code=400, detail="Tell the coach your goal.")
    db = await get_db()
    summary = await _recent_summary(db, user_id)
    system, user = _build_plan_prompt(goal, weeks, summary)
    plan, quota = await _llm_json(system, user)

    start = date.today()
    record = {
        "user_id": user_id, "goal": goal, "weeks": weeks,
        "start_date": start.isoformat(), "plan": plan, "status": "active",
        "updated_at": datetime.now(timezone.utc).isoformat(),
    }
    # one active plan at a time
    await db.table("coach_plans").update({"status": "archived"}).eq("user_id", user_id).eq("status", "active").execute()
    res = await db.table("coach_plans").insert(record).execute()
    saved = (res.data or [record])[0]
    _annotate_progress(saved["plan"], start, weeks, set())
    saved["quota"] = quota
    return saved


async def _active(db, user_id: str):
    res = await (db.table("coach_plans").select("*").eq("user_id", user_id)
                 .eq("status", "active").order("created_at", desc=True).limit(1).execute())
    return (res.data or [None])[0]


@router.get("/plan")
async def get_plan(user_id: str = Depends(get_user_id)):
    db = await get_db()
    plan_row = await _active(db, user_id)
    if not plan_row:
        return {"plan": None}
    start = date.fromisoformat(plan_row["start_date"])
    end = start + timedelta(days=plan_row["weeks"] * 7)
    wd = await (db.table("watch_data").select("timestamp").eq("user_id", user_id).eq("type", "workout")
                .gte("timestamp", start.isoformat()).lte("timestamp", end.isoformat()).execute())
    workout_dates = {(r["timestamp"] or "")[:10] for r in (wd.data or [])}
    _annotate_progress(plan_row["plan"], start, plan_row["weeks"], workout_dates)
    return plan_row


@router.post("/adapt")
async def adapt_plan(user_id: str = Depends(get_user_id)):
    db = await get_db()
    plan_row = await _active(db, user_id)
    if not plan_row:
        raise HTTPException(status_code=404, detail="No active plan to adapt.")
    summary = await _recent_summary(db, user_id)
    start = date.fromisoformat(plan_row["start_date"])
    cur_week = max(1, min(plan_row["weeks"], (date.today() - start).days // 7 + 1))
    adherence = (f"They are on week {cur_week} of {plan_row['weeks']}. Adjust the REMAINING weeks "
                 "to their actual recent volume above — easier if they're behind, harder if ahead. "
                 "Keep the same goal and total week count.")
    system, user = _build_plan_prompt(plan_row["goal"], plan_row["weeks"], summary, adherence)
    plan, quota = await _llm_json(system, user)
    await db.table("coach_plans").update({"plan": plan, "updated_at": datetime.now(timezone.utc).isoformat()}) \
        .eq("id", plan_row["id"]).execute()
    out = await get_plan(user_id)
    out["quota"] = quota
    return out


@router.delete("/plan")
async def end_plan(user_id: str = Depends(get_user_id)):
    db = await get_db()
    await db.table("coach_plans").update({"status": "abandoned"}).eq("user_id", user_id).eq("status", "active").execute()
    return {"ok": True}
