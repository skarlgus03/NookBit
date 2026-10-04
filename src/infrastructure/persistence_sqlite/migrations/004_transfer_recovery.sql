ALTER TABLE transfer_events ADD COLUMN recovery_completed INTEGER NOT NULL DEFAULT 0 CHECK(recovery_completed IN (0,1));

INSERT INTO schema_migrations(version, applied_at)
VALUES (4, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
