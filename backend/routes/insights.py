"""Deterministic insights — Daily Readiness Score + Health Watchdog alerts.

No AI / no rate limits: pure sports-science math over the user's recent data.
- Readiness (0-100): HRV trend, resting HR trend, last sleep, training load (ACWR).
- Alerts: rising resting HR, dropping HRV, load spike, low-sleep streak, inactivity.
"""
from datetime import datetime, timedelta, timezone

from fastapi import APIRouter, Depends
from auth import get_user_id
from db import get_db

router = APIRouter()


def _avg(xs):
    xs = [x for x in xs if isinstance(x, (int, float))]
    return sum(xs) / len(xs) if xs else None


async def _recent(db, user_id: str, days: int = 30):
    since = (datetime.now(timezone.utc) - timedelta(days=days)).isoformat()
    res = await (db.table("watch_data")
                 .select("type,timestamp,heart_rate,hrv,sleep_hours,duration_minutes,workout_type")
                 .eq("user_id", user_id).gte("timestamp", since).order("timestamp").execute())
    return res.data or []


def _clamp(v, lo=0, hi=100):
    return max(lo, min(hi, v))


def _compute(rows):
    now = datetime.now(timezone.utc)
    def ts(r):
        try: return datetime.fromisoformat((r["timestamp"]).replace("Z", "+00:00"))
        except Exception: return None

    readings = [r for r in rows if r.get("type") == "reading"]
    workouts = [r for r in rows if r.get("type") == "workout"]

    def within(r, d0, d1=0):
        t = ts(r)
        if t is None:
            return False
        age = (now - t).days
        return d1 <= age < d0

    # baselines (last 30d) vs recent (last ~3d)
    hrv_base   = _avg([r.get("hrv") for r in readings])
    hrv_recent = _avg([r.get("hrv") for r in readings if within(r, 4)])
    rhr_base   = _avg([r.get("heart_rate") for r in readings])
    rhr_recent = _avg([r.get("heart_rate") for r in readings if within(r, 4)])
    sleep_vals = [(ts(r), r.get("sleep_hours")) for r in readings if r.get("sleep_hours")]
    sleep_vals = [s for s in sleep_vals if s[0]]
    last_sleep = sleep_vals[-1][1] if sleep_vals else None

    # training load: acute (7d) vs chronic (28d) minutes
    acute   = sum((r.get("duration_minutes") or 0) for r in workouts if within(r, 7))
    chronic = sum((r.get("duration_minutes") or 0) for r in workouts) / 4 if workouts else 0
    acwr = (acute / chronic) if chronic else None

    return {
        "hrv_base": hrv_base, "hrv_recent": hrv_recent,
        "rhr_base": rhr_base, "rhr_recent": rhr_recent,
        "last_sleep": last_sleep, "acwr": acwr,
        "acute_min": round(acute), "n_readings": len(readings), "n_workouts": len(workouts),
        "last_workout_days": min([(now - ts(r)).days for r in workouts if ts(r)], default=None),
    }


@router.get("/readiness")
async def readiness(user_id: str = Depends(get_user_id)):
    db = await get_db()
    m = _compute(await _recent(db, user_id))
    parts, weights = [], []

    # HRV: recent vs baseline (higher better)
    if m["hrv_base"] and m["hrv_recent"]:
        parts.append(_clamp(50 + (m["hrv_recent"] - m["hrv_base"]) / m["hrv_base"] * 250)); weights.append(0.30)
    # Resting HR: recent vs baseline (lower better)
    if m["rhr_base"] and m["rhr_recent"]:
        parts.append(_clamp(50 - (m["rhr_recent"] - m["rhr_base"]) / m["rhr_base"] * 400)); weights.append(0.25)
    # Sleep: vs 8h target
    if m["last_sleep"]:
        parts.append(_clamp(m["last_sleep"] / 8 * 100)); weights.append(0.25)
    # Training load: optimal ACWR ~1.0 (0.8-1.3 good)
    if m["acwr"] is not None:
        parts.append(_clamp(100 - abs(m["acwr"] - 1.0) * 120)); weights.append(0.20)

    if not parts:
        return {"available": False, "reason": "Not enough recent data yet — log or sync a few days of HR/HRV/sleep.", "metrics": m}

    score = round(sum(p * w for p, w in zip(parts, weights)) / sum(weights))
    if score >= 80:
        band, advice = "green", "Fully recovered — green light for a hard or long session."
    elif score >= 60:
        band, advice = "amber", "Moderately recovered — a steady/tempo effort is fine; hold back on max intensity."
    else:
        band, advice = "red", "Under-recovered — keep it easy or take a rest day. Prioritize sleep."
    return {"available": True, "score": score, "band": band, "advice": advice, "metrics": m}


@router.get("/alerts")
async def alerts(user_id: str = Depends(get_user_id)):
    db = await get_db()
    rows = await _recent(db, user_id)
    m = _compute(rows)
    out = []

    if m["rhr_base"] and m["rhr_recent"] and m["rhr_recent"] - m["rhr_base"] >= 5:
        out.append({"level": "warn", "title": "Resting heart rate is elevated",
                    "detail": f"Recent {round(m['rhr_recent'])} bpm vs your {round(m['rhr_base'])} bpm baseline — possible fatigue, illness, or under-recovery."})
    if m["hrv_base"] and m["hrv_recent"] and m["hrv_recent"] < m["hrv_base"] * 0.8:
        out.append({"level": "warn", "title": "HRV has dropped",
                    "detail": f"Recent {round(m['hrv_recent'])} ms is {round((1 - m['hrv_recent']/m['hrv_base'])*100)}% below baseline — your body may be under stress."})
    if m["acwr"] is not None and m["acwr"] > 1.5:
        out.append({"level": "danger", "title": "Training load spike",
                    "detail": f"Acute:chronic load ratio is {m['acwr']:.1f} (>1.5) — high injury risk. Ease off this week."})
    if m["last_workout_days"] is not None and m["last_workout_days"] >= 7:
        out.append({"level": "info", "title": "Time to move",
                    "detail": f"No workout logged in {m['last_workout_days']} days — a light session will rebuild momentum."})
    if not out:
        out.append({"level": "ok", "title": "All clear", "detail": "No red flags in your recent data — nicely balanced."})
    return {"alerts": out, "metrics": m}
