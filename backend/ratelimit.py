"""Per-user daily AI rate limiting (Supabase-backed, atomic).

The AI features share free Groq/OpenRouter keys, so one user must not be able to
burn the whole pool. `bump_ai_usage()` (a Postgres function) atomically increments
a per-user/per-day counter and tells us whether they're still under the limit.
"""
from fastapi import HTTPException

from db import get_db

# Generous on purpose: a real user testing everything won't get near this — it's
# only a guardrail so one account can't burn the shared free model pool. ~200
# AI actions/day = dozens of long chat sessions + many plans.
DAILY_AI_LIMIT = 200  # AI calls (coach + chat) per user per day


async def enforce_ai_limit(user_id: str, action: str = "ai", limit: int = DAILY_AI_LIMIT) -> dict:
    """Raise 429 if the user is over their daily AI budget; otherwise return usage."""
    db = await get_db()
    try:
        res = await db.rpc("bump_ai_usage", {"p_user": user_id, "p_action": action, "p_limit": limit}).execute()
    except Exception:
        return {"used": None, "limit": limit}  # fail-open: never block on a metering hiccup
    data = res.data
    row = data[0] if isinstance(data, list) and data else (data or {})
    if row.get("allowed") is False:
        raise HTTPException(
            status_code=429,
            detail=f"Daily AI limit reached ({row.get('used')}/{row.get('lim', limit)}). Resets at midnight UTC.",
        )
    return {"used": row.get("used"), "limit": row.get("lim", limit)}
