"""Scheduled jobs, triggered by an external scheduler (GitHub Actions or Vercel
Cron) rather than a logged-in user. Guarded by a shared secret, not Clerk auth.

POST /cron/daily-insights
    For every user with a push subscription: compute today's Readiness + Watchdog
    alerts (the same deterministic math the app shows) and deliver one push
    notification. This is the retention loop — a reason to re-open the app daily.
"""
import asyncio
import os

from fastapi import APIRouter, Header, HTTPException

from db import get_db
from push_utils import push_configured, send_web_push
from routes.insights import readiness_for, alerts_for, build_daily_notification

router = APIRouter()

CRON_SECRET = os.getenv("CRON_SECRET", "")


def _authorize(authorization: str | None, x_cron_secret: str | None):
    if not CRON_SECRET:
        raise HTTPException(status_code=503, detail="CRON_SECRET is not configured on the server.")
    supplied = x_cron_secret or ""
    if not supplied and authorization and authorization.lower().startswith("bearer "):
        supplied = authorization[7:]
    if supplied != CRON_SECRET:
        raise HTTPException(status_code=401, detail="Bad cron secret.")


@router.post("/daily-insights")
async def daily_insights(
    authorization: str | None = Header(default=None),
    x_cron_secret: str | None = Header(default=None),
):
    _authorize(authorization, x_cron_secret)
    if not push_configured():
        raise HTTPException(status_code=503, detail="Push not configured (missing VAPID keys).")

    db = await get_db()
    res = await db.table("push_subscriptions").select("user_id,endpoint,subscription").execute()
    subs = res.data or []

    # Group subscriptions by user so we compute insights once per user.
    by_user: dict[str, list[dict]] = {}
    for s in subs:
        by_user.setdefault(s["user_id"], []).append(s)

    users = sent = skipped = pruned = failed = 0
    for user_id, user_subs in by_user.items():
        users += 1
        try:
            readiness = await readiness_for(db, user_id)
            alerts = await alerts_for(db, user_id)
        except Exception:
            failed += 1
            continue

        payload = build_daily_notification(readiness, alerts)
        if payload is None:
            skipped += 1
            continue

        for s in user_subs:
            r = await asyncio.to_thread(send_web_push, s["subscription"], payload)
            if r["ok"]:
                sent += 1
            elif r["gone"]:
                pruned += 1
                await db.table("push_subscriptions").delete().eq("endpoint", s["endpoint"]).execute()
            else:
                failed += 1

    return {
        "ok": True,
        "users": users,
        "notifications_sent": sent,
        "skipped_no_data": skipped,
        "pruned_dead_subs": pruned,
        "failed": failed,
    }
