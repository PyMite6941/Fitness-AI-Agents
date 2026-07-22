"""Web-push subscription management for the PWA.

The browser subscribes to push and POSTs its subscription here; we store it in
`push_subscriptions` keyed by the Clerk user_id. The daily cron (routes/cron.py)
reads these to deliver Readiness/Watchdog notifications. Subscriptions are pruned
automatically when the push service reports them gone (410).
"""
from datetime import datetime, timezone
from urllib.parse import urlparse

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel

from auth import get_user_id
from db import get_db
from push_utils import VAPID_PUBLIC_KEY, push_configured, send_web_push

router = APIRouter()

# Only accept subscription endpoints on real web-push service hosts. Without this
# a signed-in user could store an arbitrary endpoint and the daily job would POST
# to it — a (blind) SSRF vector. The daily cron trusts what's stored, so validate
# here at write time.
_ALLOWED_PUSH_HOSTS_EXACT = {"fcm.googleapis.com"}          # Chrome / Android (FCM)
_ALLOWED_PUSH_SUFFIXES = (
    ".push.apple.com",              # Safari / iOS
    ".push.services.mozilla.com",   # Firefox
    ".notify.windows.com",          # Edge / WNS
)


def _endpoint_allowed(endpoint: str) -> bool:
    try:
        u = urlparse(endpoint)
    except Exception:
        return False
    host = (u.hostname or "").lower()
    if u.scheme != "https" or not host:
        return False
    if host in _ALLOWED_PUSH_HOSTS_EXACT:
        return True
    return any(host.endswith(s) for s in _ALLOWED_PUSH_SUFFIXES)


class SubscribeBody(BaseModel):
    subscription: dict  # the browser PushSubscription.toJSON() ({endpoint, keys:{p256dh,auth}})


class UnsubscribeBody(BaseModel):
    endpoint: str


@router.get("/vapid-public-key")
async def vapid_public_key():
    """Public key the browser needs to subscribe. Not a secret."""
    return {"key": VAPID_PUBLIC_KEY, "configured": push_configured()}


@router.post("/subscribe")
async def subscribe(body: SubscribeBody, user_id: str = Depends(get_user_id)):
    endpoint = (body.subscription or {}).get("endpoint")
    if not endpoint:
        raise HTTPException(status_code=400, detail="subscription.endpoint is required")
    if not _endpoint_allowed(endpoint):
        raise HTTPException(status_code=400, detail="Unsupported push endpoint host.")
    db = await get_db()
    # endpoint is unique — upsert so re-subscribing the same browser is idempotent.
    await db.table("push_subscriptions").upsert({
        "user_id": user_id,
        "endpoint": endpoint,
        "subscription": body.subscription,
        "updated_at": datetime.now(timezone.utc).isoformat(),
    }, on_conflict="endpoint").execute()
    return {"ok": True}


@router.post("/unsubscribe")
async def unsubscribe(body: UnsubscribeBody, user_id: str = Depends(get_user_id)):
    db = await get_db()
    await (db.table("push_subscriptions").delete()
           .eq("user_id", user_id).eq("endpoint", body.endpoint).execute())
    return {"ok": True}


@router.get("/status")
async def status(user_id: str = Depends(get_user_id)):
    db = await get_db()
    res = await (db.table("push_subscriptions").select("endpoint")
                 .eq("user_id", user_id).execute())
    return {"subscribed": bool(res.data), "count": len(res.data or []), "configured": push_configured()}


@router.post("/test")
async def test(user_id: str = Depends(get_user_id)):
    """Send a test notification to all of the caller's subscriptions."""
    if not push_configured():
        raise HTTPException(status_code=503, detail="Push not configured on the server (missing VAPID keys).")
    db = await get_db()
    res = await (db.table("push_subscriptions").select("endpoint,subscription")
                 .eq("user_id", user_id).execute())
    subs = res.data or []
    if not subs:
        raise HTTPException(status_code=404, detail="No push subscriptions for this user.")

    import asyncio
    payload = {
        "title": "FitnessAI ✓ notifications on",
        "body": "You'll get a daily readiness check and health alerts here.",
        "url": "/coach",
    }
    sent = 0
    for s in subs:
        r = await asyncio.to_thread(send_web_push, s["subscription"], payload)
        if r["ok"]:
            sent += 1
        elif r["gone"]:
            await db.table("push_subscriptions").delete().eq("endpoint", s["endpoint"]).execute()
    return {"ok": True, "sent": sent, "of": len(subs)}
