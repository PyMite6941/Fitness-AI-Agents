import os
import csv
import tempfile
from fastapi import APIRouter, Depends, HTTPException
from datetime import datetime, timezone
from auth import get_user_id
from db import get_db
from models.analysis import AnalysisRequest
from bots import Bots

router = APIRouter()


@router.post("/")
async def run_analysis(request: AnalysisRequest, user_id: str = Depends(get_user_id)):
    """Pull the user's watch data, run the AI crew, store and return the result."""
    db = await get_db()

    query = db.table("watch_data").select("*").eq("user_id", user_id)
    if request.date_from:
        query = query.gte("timestamp", request.date_from.isoformat())
    if request.date_to:
        query = query.lte("timestamp", request.date_to.isoformat())

    result = await query.order("timestamp").execute()
    rows = result.data

    if not rows:
        raise HTTPException(status_code=404, detail="No watch data found for this user")

    for row in rows:
        row.pop("user_id", None)

    with tempfile.NamedTemporaryFile(mode="w", suffix=".csv", delete=False, newline="") as f:
        writer = csv.DictWriter(f, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
        tmp_path = f.name

    try:
        bots = Bots(context=request.context)
        bots.create_agents()
        bots.create_tasks()
        bots.create_crew(data=tmp_path)
        report = bots.crew.tasks[-1].output.raw
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Analysis failed: {str(e)}")
    finally:
        os.unlink(tmp_path)

    record = {
        "user_id": user_id,
        "context": request.context,
        "summary": report,
        "key_findings": [],
        "anomalies": [],
        "recommendations": [],
        "created_at": datetime.now(timezone.utc).isoformat(),
    }

    await db.table("analyses").insert(record).execute()
    return record
