CREATE TABLE channel_state_events (
    id INTEGER PRIMARY KEY,
    channel_id INTEGER NOT NULL REFERENCES channels(id) ON DELETE CASCADE,
    occurred_at_utc_ms INTEGER NOT NULL CHECK(occurred_at_utc_ms >= 0),
    previous_state TEXT NOT NULL,
    next_state TEXT NOT NULL,
    reason_code TEXT NOT NULL,
    error_class TEXT,
    scope TEXT NOT NULL CHECK(scope IN ('credential', 'channel', 'model')),
    source TEXT NOT NULL CHECK(source IN ('manual', 'automatic', 'probe')),
    cooldown_until_utc_ms INTEGER CHECK(cooldown_until_utc_ms IS NULL OR cooldown_until_utc_ms >= occurred_at_utc_ms),
    detail_json TEXT NOT NULL DEFAULT '{}'
);

CREATE TABLE import_jobs (
    id INTEGER PRIMARY KEY,
    stable_id TEXT NOT NULL UNIQUE,
    source_kind TEXT NOT NULL,
    format_name TEXT,
    format_version TEXT,
    state TEXT NOT NULL CHECK(state IN (
        'reading', 'parsed', 'normalized', 'preflighted', 'previewed', 'committing',
        'stored', 'validating', 'completed', 'cancelling', 'cancelled', 'failed'
    )),
    conflict_policy TEXT CHECK(conflict_policy IS NULL OR conflict_policy IN (
        'keep_existing', 'fill_empty', 'use_imported'
    )),
    rollback_policy TEXT NOT NULL DEFAULT 'all_or_nothing' CHECK(rollback_policy IN (
        'all_or_nothing', 'skip_invalid'
    )),
    cancel_requested INTEGER NOT NULL DEFAULT 0 CHECK(cancel_requested IN (0, 1)),
    created_at_utc_ms INTEGER NOT NULL CHECK(created_at_utc_ms >= 0),
    updated_at_utc_ms INTEGER NOT NULL CHECK(updated_at_utc_ms >= 0),
    completed_at_utc_ms INTEGER CHECK(completed_at_utc_ms IS NULL OR completed_at_utc_ms >= created_at_utc_ms),
    summary_json TEXT NOT NULL DEFAULT '{}'
);

CREATE TABLE import_job_items (
    id INTEGER PRIMARY KEY,
    import_job_id INTEGER NOT NULL REFERENCES import_jobs(id) ON DELETE CASCADE,
    stable_item_id TEXT NOT NULL,
    source_index INTEGER NOT NULL CHECK(source_index >= 0),
    import_state TEXT NOT NULL CHECK(import_state IN (
        'parsed', 'normalized', 'preflighted', 'previewed', 'stored', 'rejected'
    )),
    preflight_class TEXT CHECK(preflight_class IS NULL OR preflight_class IN (
        'importable', 'warning', 'existing', 'invalid'
    )),
    selected INTEGER NOT NULL DEFAULT 1 CHECK(selected IN (0, 1)),
    credential_id INTEGER REFERENCES credentials(id) ON DELETE SET NULL,
    masked_label TEXT,
    fingerprint TEXT,
    error_code TEXT,
    detail_json TEXT NOT NULL DEFAULT '{}',
    created_at_utc_ms INTEGER NOT NULL CHECK(created_at_utc_ms >= 0),
    updated_at_utc_ms INTEGER NOT NULL CHECK(updated_at_utc_ms >= 0),
    UNIQUE(import_job_id, stable_item_id)
);

CREATE INDEX idx_channel_state_events_channel_time
    ON channel_state_events(channel_id, occurred_at_utc_ms);
CREATE INDEX idx_import_jobs_state_updated ON import_jobs(state, updated_at_utc_ms);
CREATE INDEX idx_import_job_items_job_state ON import_job_items(import_job_id, import_state);
CREATE INDEX idx_import_job_items_fingerprint ON import_job_items(fingerprint);

