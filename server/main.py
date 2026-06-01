"""
FormCoach Watch Data Server
Receives real-time telemetry from the ESP32-C3 FormCoach wearable over HTTP.
Stores sessions and per-reading data in Supabase.
Auth: X-Device-Key header (shared secret, set DEVICE_KEY env var).

Deploy to Google Cloud Run (free tier):
  1. Install gcloud CLI and authenticate:
       gcloud auth login
       gcloud config set project YOUR_PROJECT_ID

  2. Build and push the container:
       cd server/
       gcloud builds submit --tag gcr.io/YOUR_PROJECT_ID/formcoach-server

  3. Deploy:
       gcloud run deploy formcoach-server \
         --image gcr.io/YOUR_PROJECT_ID/formcoach-server \
         --platform managed \
         --region us-central1 \
         --allow-unauthenticated \
         --set-env-vars SUPABASE_URL=YOUR_URL,SUPABASE_KEY=YOUR_KEY,DEVICE_KEY=YOUR_SECRET

  4. Copy the printed service URL and set it as SERVER_URL in formcoach.ino.

Required Supabase tables — run the SQL in schema.sql (formcoach_* section).
"""

import os
from datetime import datetime, timezone
from typing import Optional

from dotenv import load_dotenv
from fastapi import Depends, FastAPI, Header, HTTPException
from pydantic import BaseModel
from supabase import create_client, Client

load_dotenv()

SUPABASE_URL = os.environ["SUPABASE_URL"]
SUPABASE_KEY = os.environ["SUPABASE_KEY"]
DEVICE_KEY   = os.environ["DEVICE_KEY"]   # shared secret flashed into the watch

_supabase: Optional[Client] = None

def db() -> Client:
    global _supabase
    if _supabase is None:
        _supabase = create_client(SUPABASE_URL, SUPABASE_KEY)
    return _supabase


app = FastAPI(title="FormCoach Server")


# ─── Auth dependency ─────────────────────────────────────────────────────────

def require_device(x_device_key: str = Header(...)) -> None:
    if x_device_key != DEVICE_KEY:
        raise HTTPException(status_code=403, detail="Invalid device key")


# ─── Models ──────────────────────────────────────────────────────────────────

class WatchPayload(BaseModel):
    session_id: str       # MAC_timestamp, generated in firmware on startWorkout()
    exercise:   str       # e.g. "running", "pushup"
    bpm:        int       = 0
    spo2:       int       = 0
    accel_x:    float     = 0.0
    accel_y:    float     = 0.0
    accel_z:    float     = 0.0
    reps:       int       = 0
    form_score: float     = 100.0
    duration_s: int       = 0
    timestamp:  int       = 0       # Unix seconds from watch (NTP-synced or millis fallback)
    event:      Optional[str] = None  # "stop" when workout ends


# ─── Routes ──────────────────────────────────────────────────────────────────

@app.get("/health")
def health():
    return {"status": "ok", "utc": datetime.now(timezone.utc).isoformat()}


@app.post("/data", status_code=201, dependencies=[Depends(require_device)])
def receive_data(payload: WatchPayload):
    now = datetime.now(timezone.utc).isoformat()

    client = db()

    # Upsert session — insert only if session_id is new
    existing = (
        client.table("formcoach_sessions")
        .select("session_id")
        .eq("session_id", payload.session_id)
        .execute()
    )
    if not existing.data:
        client.table("formcoach_sessions").insert({
            "session_id":   payload.session_id,
            "exercise":     payload.exercise,
            "started_at":   now,
            "peak_bpm":     0,
            "peak_spo2":    0,
            "max_reps":     0,
            "avg_form":     100.0,
            "sample_count": 0,
        }).execute()

    # Insert reading
    client.table("formcoach_readings").insert({
        "session_id": payload.session_id,
        "ts":         now,
        "bpm":        payload.bpm,
        "spo2":       payload.spo2,
        "accel_x":    payload.accel_x,
        "accel_y":    payload.accel_y,
        "accel_z":    payload.accel_z,
        "reps":       payload.reps,
        "form_score": payload.form_score,
        "duration_s": payload.duration_s,
    }).execute()

    # Update session aggregates — Supabase RPC is cleaner but raw update works fine
    session_row = (
        client.table("formcoach_sessions")
        .select("peak_bpm,peak_spo2,max_reps,avg_form,sample_count")
        .eq("session_id", payload.session_id)
        .single()
        .execute()
    ).data

    n = session_row["sample_count"]
    new_avg_form = (session_row["avg_form"] * n + payload.form_score) / (n + 1)

    update: dict = {
        "peak_bpm":     max(session_row["peak_bpm"],  payload.bpm),
        "peak_spo2":    max(session_row["peak_spo2"], payload.spo2),
        "max_reps":     max(session_row["max_reps"],  payload.reps),
        "avg_form":     round(new_avg_form, 1),
        "sample_count": n + 1,
    }
    if payload.event == "stop":
        update["ended_at"] = now

    client.table("formcoach_sessions").update(update).eq("session_id", payload.session_id).execute()

    return {"ok": True, "session_id": payload.session_id}


@app.get("/sessions", dependencies=[Depends(require_device)])
def list_sessions(limit: int = 50):
    result = (
        db().table("formcoach_sessions")
        .select("*")
        .order("started_at", desc=True)
        .limit(limit)
        .execute()
    )
    return {"sessions": result.data}


@app.get("/sessions/{session_id}", dependencies=[Depends(require_device)])
def get_session(session_id: str):
    client = db()
    session = (
        client.table("formcoach_sessions")
        .select("*")
        .eq("session_id", session_id)
        .single()
        .execute()
    )
    if not session.data:
        raise HTTPException(404, "Session not found")
    readings = (
        client.table("formcoach_readings")
        .select("*")
        .eq("session_id", session_id)
        .order("ts")
        .execute()
    )
    return {"session": session.data, "readings": readings.data}
