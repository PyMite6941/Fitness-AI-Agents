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
