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
    workout_type      TEXT,
    duration_minutes  FLOAT,
    avg_heart_rate    FLOAT,
    max_heart_rate    FLOAT,
    notes             TEXT,
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
    created_at       TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX idx_analyses_user_time ON analyses (user_id, created_at DESC);

-- Migration: run if analyses table already exists
-- ALTER TABLE analyses ADD COLUMN IF NOT EXISTS output_type    TEXT;
-- ALTER TABLE analyses ADD COLUMN IF NOT EXISTS chart_type     TEXT;
-- ALTER TABLE analyses ADD COLUMN IF NOT EXISTS chart_title    TEXT;
-- ALTER TABLE analyses ADD COLUMN IF NOT EXISTS data_points    JSONB;
-- ALTER TABLE analyses ADD COLUMN IF NOT EXISTS metrics        JSONB;
-- ALTER TABLE analyses ADD COLUMN IF NOT EXISTS table_headers  JSONB;
-- ALTER TABLE analyses ADD COLUMN IF NOT EXISTS table_rows     JSONB;
-- ALTER TABLE analyses ADD COLUMN IF NOT EXISTS quality_score  INTEGER;
-- ALTER TABLE analyses ADD COLUMN IF NOT EXISTS quality_verdict TEXT;

CREATE TABLE routes (
    id               UUID        DEFAULT gen_random_uuid() PRIMARY KEY,
    user_id          TEXT        NOT NULL,
    workout_type     TEXT        NOT NULL,
    coordinates      JSONB       NOT NULL DEFAULT '[]',
    distance_meters  FLOAT,
    duration_seconds INTEGER,
    pace             TEXT,
    started_at       TIMESTAMPTZ NOT NULL,
    ended_at         TIMESTAMPTZ NOT NULL,
    notes            TEXT,
    created_at       TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX idx_routes_user_time ON routes (user_id, started_at DESC);

-- Migration: run this if watch_data table already exists
-- ALTER TABLE watch_data ADD COLUMN IF NOT EXISTS ending_heart_rate FLOAT;

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
