import os
import httpx
from fastapi import APIRouter, Depends
from fastapi.responses import RedirectResponse
from auth import get_user_id
from db import get_db
from datetime import datetime, timezone

router = APIRouter()

STRAVA_CLIENT_ID     = os.getenv("STRAVA_CLIENT_ID", "")
STRAVA_CLIENT_SECRET = os.getenv("STRAVA_CLIENT_SECRET", "")
BACKEND_URL          = os.getenv("BACKEND_URL", "http://localhost:8000")
FRONTEND_URL         = os.getenv("FRONTEND_URL", "http://localhost:5173")

STRAVA_AUTH_URL    = "https://www.strava.com/oauth/authorize"
STRAVA_TOKEN_URL   = "https://www.strava.com/oauth/token"
STRAVA_ACTIVITIES  = "https://www.strava.com/api/v3/athlete/activities"

STRAVA_TYPE_MAP = {
    "Run": "running", "VirtualRun": "running",
    "Ride": "cycling", "VirtualRide": "cycling", "EBikeRide": "cycling",
    "Walk": "walking",
    "Hike": "hiking",
    "WeightTraining": "weightlifting",
    "Swim": "swimming",
    "Workout": "other", "Crossfit": "other", "Elliptical": "other",
    "RockClimbing": "hiking", "Yoga": "other", "Pilates": "other",
}


@router.get("/status")
async def integration_status(user_id: str = Depends(get_user_id)):
    """Return which providers are connected for the current user."""
    db = await get_db()
    result = await (
        db.table("user_integrations")
        .select("provider")
        .eq("user_id", user_id)
        .execute()
    )
    return {row["provider"]: True for row in (result.data or [])}


@router.get("/strava/connect")
async def strava_connect(user_id: str = Depends(get_user_id)):
    """Return the Strava OAuth URL for the frontend to redirect to."""
    callback = f"{BACKEND_URL}/integrations/strava/callback"
    url = (
        f"{STRAVA_AUTH_URL}"
        f"?client_id={STRAVA_CLIENT_ID}"
        f"&redirect_uri={callback}"
        f"&response_type=code"
        f"&scope=activity:read_all"
        f"&state={user_id}"
        f"&approval_prompt=auto"
    )
    return {"url": url}


@router.get("/strava/callback")
async def strava_callback(code: str = "", state: str = "", error: str = ""):
    """Handle OAuth callback from Strava, save token, redirect to frontend."""
    if error or not code:
        return RedirectResponse(f"{FRONTEND_URL}/log?error={error or 'access_denied'}")

    user_id = state

    async with httpx.AsyncClient() as client:
        resp = await client.post(STRAVA_TOKEN_URL, data={
            "client_id":     STRAVA_CLIENT_ID,
            "client_secret": STRAVA_CLIENT_SECRET,
            "code":          code,
            "grant_type":    "authorization_code",
        })
        if resp.status_code != 200:
            return RedirectResponse(f"{FRONTEND_URL}/log?error=token_exchange_failed")
        data = resp.json()

    db = await get_db()
    row = {
        "user_id":       user_id,
        "provider":      "strava",
        "access_token":  data["access_token"],
        "refresh_token": data.get("refresh_token"),
        "expires_at":    datetime.fromtimestamp(data["expires_at"], tz=timezone.utc).isoformat(),
        "athlete_id":    str(data.get("athlete", {}).get("id", "")),
        "updated_at":    datetime.now(timezone.utc).isoformat(),
    }
    await (
        db.table("user_integrations")
        .upsert(row, on_conflict="user_id,provider")
        .execute()
    )
    return RedirectResponse(f"{FRONTEND_URL}/log?connected=strava")


@router.post("/strava/sync")
async def strava_sync(user_id: str = Depends(get_user_id)):
    """Fetch latest Strava activities and save them to watch_data."""
    db = await get_db()

    integration = await (
        db.table("user_integrations")
        .select("*")
        .eq("user_id", user_id)
        .eq("provider", "strava")
        .single()
        .execute()
    )
    if not integration.data:
        return {"synced": 0, "error": "Strava not connected"}

    token = await _refresh_strava_token_if_needed(db, integration.data)

    async with httpx.AsyncClient() as client:
        resp = await client.get(
            STRAVA_ACTIVITIES,
            headers={"Authorization": f"Bearer {token}"},
            params={"per_page": 30},
        )
        if resp.status_code != 200:
            return {"synced": 0, "error": "Failed to fetch Strava activities"}
        activities = resp.json()

    rows = []
    for a in activities:
        rows.append({
            "user_id":          user_id,
            "type":             "workout",
            "device":           "strava",
            "timestamp":        a.get("start_date"),
            "workout_type":     STRAVA_TYPE_MAP.get(a.get("type", ""), "other"),
            "duration_minutes": round(a.get("moving_time", 0) / 60, 1),
            "distance_meters":  a.get("distance"),
            "calories_burned":  a.get("calories"),
            "avg_heart_rate":   a.get("average_heartrate"),
            "max_heart_rate":   a.get("max_heartrate"),
            "notes":            a.get("name"),
        })

    if rows:
        await db.table("watch_data").insert(rows).execute()

    return {"synced": len(rows)}


async def _refresh_strava_token_if_needed(db, integration: dict) -> str:
    """Refresh Strava access token if expired, save new token, return valid token."""
    expires_at = datetime.fromisoformat(integration["expires_at"])
    if datetime.now(timezone.utc) < expires_at:
        return integration["access_token"]

    async with httpx.AsyncClient() as client:
        resp = await client.post(STRAVA_TOKEN_URL, data={
            "client_id":     STRAVA_CLIENT_ID,
            "client_secret": STRAVA_CLIENT_SECRET,
            "refresh_token": integration["refresh_token"],
            "grant_type":    "refresh_token",
        })
        data = resp.json()

    await (
        db.table("user_integrations")
        .update({
            "access_token":  data["access_token"],
            "refresh_token": data.get("refresh_token", integration["refresh_token"]),
            "expires_at":    datetime.fromtimestamp(data["expires_at"], tz=timezone.utc).isoformat(),
            "updated_at":    datetime.now(timezone.utc).isoformat(),
        })
        .eq("id", integration["id"])
        .execute()
    )
    return data["access_token"]
