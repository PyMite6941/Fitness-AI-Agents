"""Device pairing — issue/list/revoke tokens the Android tracker uses to ingest.

Flow:
  1. Web app (Clerk-authed) calls POST /device/pair → gets a `fit_...` token.
  2. User pastes the token into the phone app (or scans it as a QR).
  3. The phone posts readings to /ingest with `Authorization: Bearer fit_...`.
  4. Web app can list paired devices and revoke any of them.
"""
import secrets
from datetime import datetime, timezone

from fastapi import APIRouter, Depends
from pydantic import BaseModel

from auth import get_user_id, DEVICE_TOKEN_PREFIX
from db import get_db

router = APIRouter()


class PairRequest(BaseModel):
    device_name: str | None = None


class RevokeRequest(BaseModel):
    id: int


@router.post("/pair")
async def pair_device(body: PairRequest, user_id: str = Depends(get_user_id)):
    """Issue a new device token for the signed-in user. Show this to the phone once."""
    token = DEVICE_TOKEN_PREFIX + secrets.token_hex(20)
    db = await get_db()
    await db.table("device_tokens").insert({
        "token": token,
        "user_id": user_id,
        "device_name": (body.device_name or "Android phone")[:64],
        "created_at": datetime.now(timezone.utc).isoformat(),
    }).execute()
    return {"token": token}


@router.get("/list")
async def list_devices(user_id: str = Depends(get_user_id)):
    """Paired devices for this user. Token is masked — never returned in full."""
    db = await get_db()
    res = (
        await db.table("device_tokens")
        .select("id, token, device_name, created_at, last_used_at, revoked")
        .eq("user_id", user_id)
        .order("created_at", desc=True)
        .execute()
    )
    devices = []
    for r in res.data or []:
        t = r.pop("token", "")
        r["token_hint"] = f"{t[:8]}…{t[-4:]}" if len(t) > 12 else "…"
        devices.append(r)
    return {"devices": devices}


@router.post("/revoke")
async def revoke_device(body: RevokeRequest, user_id: str = Depends(get_user_id)):
    """Revoke one of the caller's device tokens by id. Scoped to the user."""
    db = await get_db()
    await db.table("device_tokens").update({"revoked": True}) \
        .eq("id", body.id).eq("user_id", user_id).execute()
    return {"revoked": True}
