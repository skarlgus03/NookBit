CREATE TABLE transfer_attempts (
    id TEXT PRIMARY KEY,
    plan_id TEXT NOT NULL REFERENCES plans(id),
    step_id TEXT NOT NULL REFERENCES steps(id),
    direction TEXT NOT NULL CHECK(direction IN ('forward', 'rollback')),
    source_path TEXT NOT NULL,
    destination_path TEXT NOT NULL,
    staging_path TEXT NOT NULL,
    source_fingerprint_json TEXT NOT NULL,
    staging_fingerprint_json TEXT,
    destination_fingerprint_json TEXT,
    phase TEXT NOT NULL,
    error_json TEXT,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

CREATE UNIQUE INDEX idx_transfer_live_step ON transfer_attempts(step_id)
    WHERE phase NOT IN ('completed', 'failed', 'cancelled');

CREATE TABLE transfer_events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    attempt_id TEXT NOT NULL REFERENCES transfer_attempts(id),
    occurred_at TEXT NOT NULL,
    phase TEXT NOT NULL,
    staging_fingerprint_json TEXT,
    destination_fingerprint_json TEXT,
    error_json TEXT
);

CREATE INDEX idx_transfer_step ON transfer_attempts(step_id, created_at);

INSERT INTO schema_migrations(version, applied_at)
VALUES (2, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
