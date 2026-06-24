from fastapi import APIRouter, Depends, HTTPException
from auth import get_user_id_flexible
from db import get_db
from models.watch import WatchSyncPayload
from calories import estimate_calories

router = APIRouter()


@router.post("/")
async def ingest_data(payload: WatchSyncPayload, user_id: str = Depends(get_user_id_flexible)):
    """Generic data ingestion — any wearable, any app, any device. POST readings and workouts here.

    Auth accepts either a Clerk JWT (web) or a paired-device token (the Android tracker).
    """
    db = await get_db()
    rows = []

    for reading in payload.readings:
        rows.append({
            "user_id": user_id,
            "type": "reading",
            "device": payload.device or "unknown",
            **reading.model_dump(),
        })

    for workout in payload.workouts:
        row = {
            "user_id": user_id,
            "type": "workout",
            "device": payload.device or "unknown",
            **workout.model_dump(),
        }
        if row.get("calories_burned") is None:
            row["calories_burned"] = estimate_calories(
                row.get("duration_minutes") or 0, row.get("workout_type"))
        rows.append(row)

    if not rows:
        raise HTTPException(status_code=400, detail="No data provided")

    for row in rows:
        if row.get("timestamp"):
            row["timestamp"] = row["timestamp"].isoformat()

    await db.table("watch_data").insert(rows).execute()
    return {"ingested": len(rows)}
