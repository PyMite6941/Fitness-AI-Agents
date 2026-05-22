from fastapi import APIRouter, Depends
from auth import get_user_id
from db import get_db

router = APIRouter()


@router.get("/me")
async def get_me(user_id: str = Depends(get_user_id)):
    """Return basic stats for the authenticated user."""
    db = await get_db()

    readings = await db.table("watch_data").select("id", count="exact").eq("user_id", user_id).eq("type", "reading").execute()
    workouts = await db.table("watch_data").select("id", count="exact").eq("user_id", user_id).eq("type", "workout").execute()
    analyses = await db.table("analyses").select("id", count="exact").eq("user_id", user_id).execute()

    return {
        "user_id": user_id,
        "readings": readings.count,
        "workouts": workouts.count,
        "analyses": analyses.count,
    }


@router.get("/history")
async def get_history(user_id: str = Depends(get_user_id), limit: int = 20):
    """Return the user's past AI analyses, newest first — consumed by the phone."""
    db = await get_db()
    result = await (
        db.table("analyses")
        .select("*")
        .eq("user_id", user_id)
        .order("created_at", desc=True)
        .limit(limit)
        .execute()
    )
    return {"analyses": result.data}


@router.get("/data")
async def get_watch_data(user_id: str = Depends(get_user_id), limit: int = 100):
    """Return raw watch data for the user, newest first."""
    db = await get_db()
    result = await (
        db.table("watch_data")
        .select("*")
        .eq("user_id", user_id)
        .order("timestamp", desc=True)
        .limit(limit)
        .execute()
    )
    return {"data": result.data}
