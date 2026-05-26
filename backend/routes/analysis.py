import os
import csv
import json
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
        raw_json = bots.create_crew(data=tmp_path)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Analysis failed: {str(e)}")
    finally:
        if os.path.exists(tmp_path):
            os.unlink(tmp_path)

    # Parse the structured FormattedOutput returned by bots.create_crew()
    parsed: dict = {}
    try:
        parsed = json.loads(raw_json)
    except Exception:
        parsed = {}

    record = {
        "user_id": user_id,
        "context": request.context,
        "summary": parsed.get("summary") or raw_json,
        "key_findings": parsed.get("findings") or [],
        "recommendations": parsed.get("recommendations") or [],
        "output_type": parsed.get("output_type"),
        "chart_type": parsed.get("chart_type"),
        "chart_title": parsed.get("chart_title"),
        "data_points": parsed.get("data_points"),
        "metrics": parsed.get("metrics"),
        "table_headers": parsed.get("table_headers"),
        "table_rows": parsed.get("table_rows"),
        "quality_score": parsed.get("quality_score"),
        "quality_verdict": parsed.get("quality_verdict"),
        "created_at": datetime.now(timezone.utc).isoformat(),
    }

    # Strip None values so Supabase doesn't reject unknown columns
    record = {k: v for k, v in record.items() if v is not None}

    await db.table("analyses").insert(record).execute()
    return record
