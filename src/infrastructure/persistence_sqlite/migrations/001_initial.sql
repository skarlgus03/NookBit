PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;
PRAGMA synchronous = FULL;

CREATE TABLE IF NOT EXISTS schema_migrations (
    version INTEGER PRIMARY KEY,
    applied_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS plans (
    id TEXT PRIMARY KEY,
    created_at TEXT NOT NULL,
    rule_set_version TEXT NOT NULL,
    settings_revision TEXT NOT NULL,
    rules_revision TEXT NOT NULL,
    plan_hash TEXT NOT NULL,
    status TEXT NOT NULL,
    warnings_json TEXT NOT NULL DEFAULT '[]',
    approved_at TEXT,
    completed_at TEXT
);

CREATE TABLE IF NOT EXISTS steps (
    id TEXT PRIMARY KEY,
    plan_id TEXT NOT NULL REFERENCES plans(id),
    sequence_no INTEGER NOT NULL,
    source_path TEXT NOT NULL,
    destination_path TEXT NOT NULL,
    fingerprint_json TEXT NOT NULL,
    matched_rule_id TEXT NOT NULL,
    reason TEXT NOT NULL,
    collision_policy TEXT NOT NULL,
    status TEXT NOT NULL,
    error_code TEXT,
    error_detail TEXT,
    UNIQUE(plan_id, sequence_no)
);

CREATE TABLE IF NOT EXISTS events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    plan_id TEXT,
    step_id TEXT,
    occurred_at TEXT NOT NULL,
    event_type TEXT NOT NULL,
    payload_json TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_plans_status ON plans(status);
CREATE INDEX IF NOT EXISTS idx_steps_plan ON steps(plan_id, sequence_no);
CREATE INDEX IF NOT EXISTS idx_events_plan ON events(plan_id, id);

INSERT OR IGNORE INTO schema_migrations(version, applied_at)
VALUES (1, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
