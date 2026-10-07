CREATE TABLE transfer_removal_proofs (
    attempt_id TEXT PRIMARY KEY REFERENCES transfer_attempts(id),
    verified_transfer_event_id INTEGER NOT NULL REFERENCES transfer_events(id),
    audit_event_id INTEGER NOT NULL UNIQUE REFERENCES events(id),
    proof_json TEXT NOT NULL
);
INSERT INTO schema_migrations(version, applied_at) VALUES(9, strftime('%Y-%m-%dT%H:%M:%fZ','now'));
