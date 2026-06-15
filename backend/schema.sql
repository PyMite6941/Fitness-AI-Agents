CREATE TABLE watch_data (
    id          UUID        DEFAULT gen_random_uuid() PRIMARY KEY,
    user_id     TEXT        NOT NULL,
    type        TEXT        NOT NULL,  -- 'reading' or 'workout'
    timestamp   TIMESTAMPTZ NOT NULL,
    device      TEXT,
    -- reading fields
    heart_rate        FLOAT,
    steps             INTEGER,
    calories_burned   FLOAT,
    active_calories   FLOAT,
    distance_meters   FLOAT,
    sleep_hours       FLOAT,
    spo2              FLOAT,
    hrv               FLOAT,
    -- workout fields
    workout_type        TEXT,
    duration_minutes    FLOAT,
    avg_heart_rate      FLOAT,
    max_heart_rate      FLOAT,
    ending_heart_rate   FLOAT,
    notes               TEXT,
    created_at  TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX idx_watch_data_user_time ON watch_data (user_id, timestamp DESC);
CREATE INDEX idx_watch_data_type      ON watch_data (user_id, type);

CREATE TABLE analyses (
    id               UUID    DEFAULT gen_random_uuid() PRIMARY KEY,
    user_id          TEXT    NOT NULL,
    context          TEXT    NOT NULL,
    summary          TEXT,
    key_findings     JSONB   DEFAULT '[]',
    anomalies        JSONB   DEFAULT '[]',
    recommendations  JSONB   DEFAULT '[]',
    -- AI output rendering fields
    output_type      TEXT,
    chart_type       TEXT,
    chart_title      TEXT,
    data_points      JSONB,
    metrics          JSONB,
    table_headers    JSONB,
    table_rows       JSONB,
    quality_score    INTEGER,
    quality_verdict  TEXT,
    raw_output       JSONB,   -- full FormattedOutput — enables comparison/heatmap/code rendering
    created_at       TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX idx_analyses_user_time ON analyses (user_id, created_at DESC);

-- Migration for existing databases (skip if creating fresh):
-- ALTER TABLE analyses ADD COLUMN IF NOT EXISTS output_type    TEXT;
-- ALTER TABLE analyses ADD COLUMN IF NOT EXISTS chart_type     TEXT;
-- ALTER TABLE analyses ADD COLUMN IF NOT EXISTS chart_title    TEXT;
-- ALTER TABLE analyses ADD COLUMN IF NOT EXISTS data_points    JSONB;
-- ALTER TABLE analyses ADD COLUMN IF NOT EXISTS metrics        JSONB;
-- ALTER TABLE analyses ADD COLUMN IF NOT EXISTS table_headers  JSONB;
-- ALTER TABLE analyses ADD COLUMN IF NOT EXISTS table_rows     JSONB;
-- ALTER TABLE analyses ADD COLUMN IF NOT EXISTS quality_score  INTEGER;
-- ALTER TABLE analyses ADD COLUMN IF NOT EXISTS quality_verdict TEXT;
-- ALTER TABLE analyses ADD COLUMN IF NOT EXISTS raw_output     JSONB;

CREATE TABLE routes (
    id               UUID        DEFAULT gen_random_uuid() PRIMARY KEY,
    user_id          TEXT        NOT NULL,
    workout_type     TEXT        NOT NULL,
    coordinates      JSONB       NOT NULL DEFAULT '[]',
    distance_meters  FLOAT,
    duration_seconds INTEGER,
    pace             TEXT,
    calories_burned  FLOAT,
    started_at       TIMESTAMPTZ NOT NULL,
    ended_at         TIMESTAMPTZ NOT NULL,
    notes            TEXT,
    created_at       TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX idx_routes_user_time ON routes (user_id, started_at DESC);

-- Migration: run if routes table already exists
ALTER TABLE routes ADD COLUMN IF NOT EXISTS calories_burned FLOAT;

-- Migration: run this if watch_data table already exists (ending_heart_rate was added to CREATE TABLE above)
ALTER TABLE watch_data ADD COLUMN IF NOT EXISTS ending_heart_rate FLOAT;

CREATE TABLE user_integrations (
    id             UUID        DEFAULT gen_random_uuid() PRIMARY KEY,
    user_id        TEXT        NOT NULL,
    provider       TEXT        NOT NULL,
    access_token   TEXT        NOT NULL,
    refresh_token  TEXT,
    expires_at     TIMESTAMPTZ,
    athlete_id     TEXT,
    created_at     TIMESTAMPTZ DEFAULT NOW(),
    updated_at     TIMESTAMPTZ DEFAULT NOW(),
    UNIQUE(user_id, provider)
);

CREATE INDEX idx_integrations_user ON user_integrations (user_id);

-- ── FormCoach wearable tables ─────────────────────────────────────────────────
-- No user_id — device is authenticated by X-Device-Key header on the server.

CREATE TABLE formcoach_sessions (
    session_id   TEXT        PRIMARY KEY,  -- MAC_timestamp from firmware
    exercise     TEXT        NOT NULL,
    started_at   TIMESTAMPTZ NOT NULL,
    ended_at     TIMESTAMPTZ,
    peak_bpm     INTEGER     DEFAULT 0,
    peak_spo2    INTEGER     DEFAULT 0,
    max_reps     INTEGER     DEFAULT 0,
    avg_form     REAL        DEFAULT 100,
    sample_count INTEGER     DEFAULT 0
);

CREATE TABLE formcoach_readings (
    id          BIGSERIAL   PRIMARY KEY,
    session_id  TEXT        NOT NULL REFERENCES formcoach_sessions(session_id),
    ts          TIMESTAMPTZ NOT NULL,
    bpm         INTEGER,
    spo2        INTEGER,
    accel_x     REAL,
    accel_y     REAL,
    accel_z     REAL,
    reps        INTEGER,
    form_score  REAL,
    duration_s  INTEGER
);

CREATE INDEX idx_fc_readings_session ON formcoach_readings (session_id, ts);
