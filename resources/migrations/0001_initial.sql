CREATE TABLE app_settings (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL,
    updated_at_utc_ms INTEGER NOT NULL CHECK(updated_at_utc_ms >= 0)
);

CREATE TABLE sites (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL CHECK(length(trim(name)) > 0),
    base_url TEXT NOT NULL CHECK(length(trim(base_url)) > 0),
    adapter_type TEXT NOT NULL CHECK(length(trim(adapter_type)) > 0),
    enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0, 1)),
    created_at_utc_ms INTEGER NOT NULL CHECK(created_at_utc_ms >= 0),
    updated_at_utc_ms INTEGER NOT NULL CHECK(updated_at_utc_ms >= 0)
);

CREATE TABLE secrets (
    id INTEGER PRIMARY KEY,
    kind TEXT NOT NULL CHECK(length(trim(kind)) > 0),
    ciphertext BLOB NOT NULL CHECK(length(ciphertext) > 0),
    format_version INTEGER NOT NULL CHECK(format_version > 0),
    created_at_utc_ms INTEGER NOT NULL CHECK(created_at_utc_ms >= 0),
    rotated_at_utc_ms INTEGER CHECK(rotated_at_utc_ms IS NULL OR rotated_at_utc_ms >= created_at_utc_ms)
);

CREATE TABLE proxies (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL CHECK(length(trim(name)) > 0),
    scheme TEXT NOT NULL CHECK(scheme IN ('http', 'https', 'socks4', 'socks5')),
    host TEXT NOT NULL CHECK(length(trim(host)) > 0),
    port INTEGER NOT NULL CHECK(port BETWEEN 1 AND 65535),
    username_secret_id INTEGER REFERENCES secrets(id) ON DELETE SET NULL,
    password_secret_id INTEGER REFERENCES secrets(id) ON DELETE SET NULL,
    enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0, 1)),
    created_at_utc_ms INTEGER NOT NULL CHECK(created_at_utc_ms >= 0),
    updated_at_utc_ms INTEGER NOT NULL CHECK(updated_at_utc_ms >= 0)
);

CREATE TABLE account_groups (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE CHECK(length(trim(name)) > 0),
    sort_order INTEGER NOT NULL DEFAULT 0,
    created_at_utc_ms INTEGER NOT NULL CHECK(created_at_utc_ms >= 0),
    updated_at_utc_ms INTEGER NOT NULL CHECK(updated_at_utc_ms >= 0)
);

CREATE TABLE credentials (
    id INTEGER PRIMARY KEY,
    site_id INTEGER REFERENCES sites(id) ON DELETE SET NULL,
    group_id INTEGER REFERENCES account_groups(id) ON DELETE SET NULL,
    secret_id INTEGER NOT NULL REFERENCES secrets(id) ON DELETE RESTRICT,
    proxy_id INTEGER REFERENCES proxies(id) ON DELETE SET NULL,
    platform TEXT NOT NULL CHECK(length(trim(platform)) > 0),
    credential_type TEXT NOT NULL CHECK(length(trim(credential_type)) > 0),
    display_name TEXT NOT NULL CHECK(length(trim(display_name)) > 0),
    account_fingerprint TEXT NOT NULL CHECK(length(account_fingerprint) >= 16),
    runtime_status TEXT NOT NULL DEFAULT 'unsupported_runtime' CHECK(runtime_status IN (
        'validating', 'verified', 'schedulable', 'blocked', 'expired', 'unsupported_runtime'
    )),
    enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0, 1)),
    expires_at_utc_ms INTEGER CHECK(expires_at_utc_ms IS NULL OR expires_at_utc_ms >= 0),
    concurrency_limit INTEGER NOT NULL DEFAULT 1 CHECK(concurrency_limit > 0),
    priority INTEGER NOT NULL DEFAULT 0,
    rate_multiplier_micros INTEGER NOT NULL DEFAULT 1000000 CHECK(rate_multiplier_micros >= 0),
    created_at_utc_ms INTEGER NOT NULL CHECK(created_at_utc_ms >= 0),
    updated_at_utc_ms INTEGER NOT NULL CHECK(updated_at_utc_ms >= 0),
    UNIQUE(platform, credential_type, account_fingerprint)
);

CREATE TABLE tags (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE CHECK(length(trim(name)) > 0),
    created_at_utc_ms INTEGER NOT NULL CHECK(created_at_utc_ms >= 0)
);

CREATE TABLE credential_tags (
    credential_id INTEGER NOT NULL REFERENCES credentials(id) ON DELETE CASCADE,
    tag_id INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,
    PRIMARY KEY(credential_id, tag_id)
);

CREATE TABLE channels (
    id INTEGER PRIMARY KEY,
    site_id INTEGER NOT NULL REFERENCES sites(id) ON DELETE RESTRICT,
    credential_id INTEGER NOT NULL REFERENCES credentials(id) ON DELETE RESTRICT,
    proxy_id INTEGER REFERENCES proxies(id) ON DELETE SET NULL,
    name TEXT NOT NULL CHECK(length(trim(name)) > 0),
    enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0, 1)),
    auto_disable_enabled INTEGER NOT NULL DEFAULT 1 CHECK(auto_disable_enabled IN (0, 1)),
    auto_enable_enabled INTEGER NOT NULL DEFAULT 1 CHECK(auto_enable_enabled IN (0, 1)),
    priority INTEGER NOT NULL DEFAULT 0,
    weight INTEGER NOT NULL DEFAULT 1 CHECK(weight > 0),
    concurrency_limit INTEGER NOT NULL DEFAULT 1 CHECK(concurrency_limit > 0),
    created_at_utc_ms INTEGER NOT NULL CHECK(created_at_utc_ms >= 0),
    updated_at_utc_ms INTEGER NOT NULL CHECK(updated_at_utc_ms >= 0)
);

CREATE TABLE logical_models (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE CHECK(length(trim(name)) > 0),
    enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0, 1)),
    created_at_utc_ms INTEGER NOT NULL CHECK(created_at_utc_ms >= 0),
    updated_at_utc_ms INTEGER NOT NULL CHECK(updated_at_utc_ms >= 0)
);

CREATE TABLE channel_models (
    id INTEGER PRIMARY KEY,
    channel_id INTEGER NOT NULL REFERENCES channels(id) ON DELETE CASCADE,
    logical_model_id INTEGER NOT NULL REFERENCES logical_models(id) ON DELETE CASCADE,
    upstream_model TEXT NOT NULL CHECK(length(trim(upstream_model)) > 0),
    enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0, 1)),
    created_at_utc_ms INTEGER NOT NULL CHECK(created_at_utc_ms >= 0),
    updated_at_utc_ms INTEGER NOT NULL CHECK(updated_at_utc_ms >= 0),
    UNIQUE(channel_id, logical_model_id)
);

CREATE TABLE routes (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE CHECK(length(trim(name)) > 0),
    strategy TEXT NOT NULL DEFAULT 'strict_round_robin' CHECK(strategy IN (
        'primary_backup', 'strict_round_robin', 'smooth_weighted_round_robin', 'least_loaded'
    )),
    enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0, 1)),
    created_at_utc_ms INTEGER NOT NULL CHECK(created_at_utc_ms >= 0),
    updated_at_utc_ms INTEGER NOT NULL CHECK(updated_at_utc_ms >= 0)
);

CREATE TABLE route_members (
    route_id INTEGER NOT NULL REFERENCES routes(id) ON DELETE CASCADE,
    logical_model_id INTEGER NOT NULL REFERENCES logical_models(id) ON DELETE CASCADE,
    channel_id INTEGER NOT NULL REFERENCES channels(id) ON DELETE CASCADE,
    priority INTEGER NOT NULL DEFAULT 0,
    weight INTEGER NOT NULL DEFAULT 1 CHECK(weight > 0),
    enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0, 1)),
    PRIMARY KEY(route_id, logical_model_id, channel_id)
);

CREATE TABLE local_api_keys (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL CHECK(length(trim(name)) > 0),
    key_prefix TEXT NOT NULL CHECK(length(key_prefix) > 0),
    salt BLOB NOT NULL CHECK(length(salt) >= 16),
    key_digest BLOB NOT NULL CHECK(length(key_digest) >= 32),
    created_at_utc_ms INTEGER NOT NULL CHECK(created_at_utc_ms >= 0),
    last_used_at_utc_ms INTEGER CHECK(last_used_at_utc_ms IS NULL OR last_used_at_utc_ms >= 0),
    revoked_at_utc_ms INTEGER CHECK(revoked_at_utc_ms IS NULL OR revoked_at_utc_ms >= created_at_utc_ms)
);

CREATE TABLE health_checks (
    id INTEGER PRIMARY KEY,
    channel_id INTEGER REFERENCES channels(id) ON DELETE SET NULL,
    logical_model_id INTEGER REFERENCES logical_models(id) ON DELETE SET NULL,
    started_at_utc_ms INTEGER NOT NULL CHECK(started_at_utc_ms >= 0),
    finished_at_utc_ms INTEGER CHECK(finished_at_utc_ms IS NULL OR finished_at_utc_ms >= started_at_utc_ms),
    outcome TEXT NOT NULL,
    error_class TEXT,
    http_status INTEGER,
    ttfb_ms INTEGER CHECK(ttfb_ms IS NULL OR ttfb_ms >= 0),
    ttft_ms INTEGER CHECK(ttft_ms IS NULL OR ttft_ms >= 0),
    total_ms INTEGER CHECK(total_ms IS NULL OR total_ms >= 0)
);

CREATE TABLE request_records (
    id INTEGER PRIMARY KEY,
    request_id TEXT NOT NULL UNIQUE,
    route_id INTEGER REFERENCES routes(id) ON DELETE SET NULL,
    logical_model_id INTEGER REFERENCES logical_models(id) ON DELETE SET NULL,
    channel_id INTEGER REFERENCES channels(id) ON DELETE SET NULL,
    started_at_utc_ms INTEGER NOT NULL CHECK(started_at_utc_ms >= 0),
    finished_at_utc_ms INTEGER CHECK(finished_at_utc_ms IS NULL OR finished_at_utc_ms >= started_at_utc_ms),
    outcome TEXT NOT NULL,
    error_class TEXT,
    ttfb_ms INTEGER CHECK(ttfb_ms IS NULL OR ttfb_ms >= 0),
    ttft_ms INTEGER CHECK(ttft_ms IS NULL OR ttft_ms >= 0),
    total_ms INTEGER CHECK(total_ms IS NULL OR total_ms >= 0),
    input_tokens INTEGER CHECK(input_tokens IS NULL OR input_tokens >= 0),
    cache_read_tokens INTEGER CHECK(cache_read_tokens IS NULL OR cache_read_tokens >= 0),
    cache_write_tokens INTEGER CHECK(cache_write_tokens IS NULL OR cache_write_tokens >= 0),
    output_tokens INTEGER CHECK(output_tokens IS NULL OR output_tokens >= 0),
    reasoning_tokens INTEGER CHECK(reasoning_tokens IS NULL OR reasoning_tokens >= 0),
    cost_microusd INTEGER CHECK(cost_microusd IS NULL OR cost_microusd >= 0),
    evidence_grade TEXT CHECK(evidence_grade IS NULL OR evidence_grade IN ('A', 'B', 'C', 'U'))
);

CREATE TABLE daily_usage (
    day_utc TEXT NOT NULL,
    logical_model_id INTEGER REFERENCES logical_models(id) ON DELETE SET NULL,
    channel_id INTEGER REFERENCES channels(id) ON DELETE SET NULL,
    request_count INTEGER NOT NULL DEFAULT 0 CHECK(request_count >= 0),
    success_count INTEGER NOT NULL DEFAULT 0 CHECK(success_count >= 0),
    input_tokens INTEGER NOT NULL DEFAULT 0 CHECK(input_tokens >= 0),
    cache_read_tokens INTEGER NOT NULL DEFAULT 0 CHECK(cache_read_tokens >= 0),
    cache_write_tokens INTEGER NOT NULL DEFAULT 0 CHECK(cache_write_tokens >= 0),
    output_tokens INTEGER NOT NULL DEFAULT 0 CHECK(output_tokens >= 0),
    cost_microusd INTEGER NOT NULL DEFAULT 0 CHECK(cost_microusd >= 0),
    PRIMARY KEY(day_utc, logical_model_id, channel_id)
);

CREATE TABLE route_cursors (
    route_id INTEGER NOT NULL REFERENCES routes(id) ON DELETE CASCADE,
    logical_model_id INTEGER NOT NULL REFERENCES logical_models(id) ON DELETE CASCADE,
    cursor_value INTEGER NOT NULL DEFAULT 0 CHECK(cursor_value >= 0),
    updated_at_utc_ms INTEGER NOT NULL CHECK(updated_at_utc_ms >= 0),
    PRIMARY KEY(route_id, logical_model_id)
);

CREATE TABLE fingerprint_runs (
    id INTEGER PRIMARY KEY,
    channel_id INTEGER REFERENCES channels(id) ON DELETE SET NULL,
    logical_model_id INTEGER REFERENCES logical_models(id) ON DELETE SET NULL,
    test_pack_version TEXT NOT NULL,
    verdict TEXT NOT NULL,
    evidence_json TEXT NOT NULL,
    started_at_utc_ms INTEGER NOT NULL CHECK(started_at_utc_ms >= 0),
    finished_at_utc_ms INTEGER CHECK(finished_at_utc_ms IS NULL OR finished_at_utc_ms >= started_at_utc_ms)
);

CREATE TABLE pricing_tables (
    id INTEGER PRIMARY KEY,
    version TEXT NOT NULL UNIQUE,
    currency TEXT NOT NULL,
    prices_json TEXT NOT NULL,
    created_at_utc_ms INTEGER NOT NULL CHECK(created_at_utc_ms >= 0)
);

CREATE TABLE cost_experiments (
    id INTEGER PRIMARY KEY,
    credential_id INTEGER REFERENCES credentials(id) ON DELETE SET NULL,
    pricing_table_id INTEGER REFERENCES pricing_tables(id) ON DELETE SET NULL,
    evidence_grade TEXT NOT NULL CHECK(evidence_grade IN ('A', 'B', 'C', 'U')),
    baseline_cost_microusd INTEGER CHECK(baseline_cost_microusd IS NULL OR baseline_cost_microusd >= 0),
    observed_cost_microusd INTEGER CHECK(observed_cost_microusd IS NULL OR observed_cost_microusd >= 0),
    result_json TEXT NOT NULL,
    started_at_utc_ms INTEGER NOT NULL CHECK(started_at_utc_ms >= 0),
    finished_at_utc_ms INTEGER CHECK(finished_at_utc_ms IS NULL OR finished_at_utc_ms >= started_at_utc_ms)
);

CREATE INDEX idx_credentials_site ON credentials(site_id);
CREATE INDEX idx_credentials_runtime ON credentials(runtime_status, enabled);
CREATE INDEX idx_channels_site ON channels(site_id);
CREATE INDEX idx_channels_credential ON channels(credential_id);
CREATE INDEX idx_channel_models_logical ON channel_models(logical_model_id, enabled);
CREATE INDEX idx_request_records_started ON request_records(started_at_utc_ms);
CREATE INDEX idx_health_checks_channel_started ON health_checks(channel_id, started_at_utc_ms);

