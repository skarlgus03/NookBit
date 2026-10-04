ALTER TABLE transfer_events ADD COLUMN recovery_removal_intent INTEGER NOT NULL DEFAULT 0 CHECK(recovery_removal_intent IN (0,1));
INSERT INTO schema_migrations(version, applied_at)
VALUES (6, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
