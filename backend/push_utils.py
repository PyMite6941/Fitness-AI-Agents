"""Web Push (VAPID) helper.

Sends browser push notifications to a stored subscription. Keys come from env:
  VAPID_PUBLIC_KEY   — application-server public key (base64url), also handed to
                       the browser so it can subscribe.
  VAPID_PRIVATE_KEY  — application-server private key (base64url) — SECRET.
  VAPID_SUBJECT      — a mailto: or https: contact, e.g. mailto:you@example.com

If the keys aren't set the module degrades gracefully (send returns "unconfigured")
so the rest of the API keeps working — push is an optional feature.

pywebpush is a light dependency (py-vapid + http-ece + cryptography); it does NOT
pull in the heavy AI stack, so it's safe in the Vercel backend.
"""
import json
import os

try:
    from pywebpush import webpush, WebPushException
    _HAVE_PYWEBPUSH = True
except Exception:  # pragma: no cover - only if dep missing
    _HAVE_PYWEBPUSH = False


VAPID_PUBLIC_KEY = os.getenv("VAPID_PUBLIC_KEY", "")
VAPID_PRIVATE_KEY = os.getenv("VAPID_PRIVATE_KEY", "")
VAPID_SUBJECT = os.getenv("VAPID_SUBJECT", "mailto:admin@example.com")


def push_configured() -> bool:
    return bool(_HAVE_PYWEBPUSH and VAPID_PUBLIC_KEY and VAPID_PRIVATE_KEY)


def send_web_push(subscription_info: dict, payload: dict) -> dict:
    """Send one push. Returns {"ok": bool, "status": int, "gone": bool}.

    `gone` is True when the subscription is dead (404/410) and should be pruned.
    Runs the (synchronous) pywebpush call; callers on the event loop should wrap
    this in asyncio.to_thread.
    """
    if not push_configured():
        return {"ok": False, "status": 0, "gone": False, "reason": "unconfigured"}
    try:
        webpush(
            subscription_info=subscription_info,
            data=json.dumps(payload),
            vapid_private_key=VAPID_PRIVATE_KEY,
            vapid_claims={"sub": VAPID_SUBJECT},
            ttl=12 * 3600,  # let the push service hold it up to 12h if device is offline
        )
        return {"ok": True, "status": 201, "gone": False}
    except WebPushException as e:
        status = getattr(getattr(e, "response", None), "status_code", 0) or 0
        return {"ok": False, "status": status, "gone": status in (404, 410)}
    except Exception:
        return {"ok": False, "status": 0, "gone": False}
