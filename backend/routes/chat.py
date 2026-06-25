"""Chat with your data — conversational AI grounded in the user's fitness history.

A single chat-completion call (Groq -> OpenRouter failover) with a compact,
server-built data summary as context. Multi-turn: the client sends the history.
"""
from datetime import datetime, timedelta, timezone

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel

from auth import get_user_id
from db import get_db
from llm_lite import complete, LLMUnavailable

router = APIRouter()


class ChatRequest(BaseModel):
    messages: list[dict]  # [{role: 'user'|'assistant', content: str}, ...]


async def _context(db, user_id: str) -> str:
    since = (datetime.now(timezone.utc) - timedelta(days=90)).isoformat()
    res = await (db.table("watch_data")
                 .select("type,timestamp,workout_type,duration_minutes,distance_meters,heart_rate,hrv,sleep_hours,device")
                 .eq("user_id", user_id).gte("timestamp", since).order("timestamp").execute())
    rows = res.data or []
    if not rows:
        return "The user has no logged fitness data yet."

    workouts = [r for r in rows if r.get("type") == "workout"]
    readings = [r for r in rows if r.get("type") == "reading"]
    types, devices = {}, {}
    tmin = tkm = 0.0
    for r in workouts:
        t = r.get("workout_type") or "other"; types[t] = types.get(t, 0) + 1
        tmin += r.get("duration_minutes") or 0
        tkm  += (r.get("distance_meters") or 0) / 1000
    for r in rows:
        d = r.get("device") or "unknown"; devices[d] = devices.get(d, 0) + 1

    def avg(key, src):
        xs = [r.get(key) for r in src if isinstance(r.get(key), (int, float))]
        return round(sum(xs) / len(xs), 1) if xs else None

    weeks = max(1, (datetime.now(timezone.utc) - datetime.fromisoformat(rows[0]["timestamp"].replace("Z", "+00:00"))).days / 7)
    lines = [
        f"Window: last ~{round(weeks)} weeks, {len(rows)} data points.",
        f"Workouts: {len(workouts)} ({', '.join(f'{k} x{v}' for k, v in types.items()) or 'none'}).",
        f"Avg per week: {round(tmin/weeks)} min, {round(tkm/weeks, 1)} km.",
        f"Resting HR avg: {avg('heart_rate', readings)} bpm. HRV avg: {avg('hrv', readings)} ms. Sleep avg: {avg('sleep_hours', readings)} h.",
        f"Sources: {', '.join(f'{k} ({v})' for k, v in devices.items())}.",
    ]
    return "\n".join(lines)


@router.post("/")
async def chat(body: ChatRequest, user_id: str = Depends(get_user_id)):
    if not body.messages:
        raise HTTPException(status_code=400, detail="No message.")
    db = await get_db()
    ctx = await _context(db, user_id)
    system = {
        "role": "system",
        "content": (
            "You are a friendly, expert fitness data analyst for one user. Answer ONLY from the "
            "data summary below; if it doesn't cover the question, say so plainly. Be concise, "
            "specific, and cite numbers. Never invent values.\n\nUSER DATA SUMMARY:\n" + ctx
        ),
    }
    history = [{"role": ("assistant" if m.get("role") == "assistant" else "user"),
                "content": str(m.get("content", ""))[:2000]} for m in body.messages[-12:]]
    try:
        reply, quota = await complete([system] + history, max_tokens=900, temperature=0.5)
    except LLMUnavailable as e:
        raise HTTPException(status_code=503, detail=str(e))
    return {"reply": reply, "quota": quota}
