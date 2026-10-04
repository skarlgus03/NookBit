ALTER TABLE transfer_events ADD COLUMN staging_commit INTEGER NOT NULL DEFAULT 0 CHECK(staging_commit IN (0,1,2));
INSERT INTO schema_migrations(version, applied_at)
VALUES (8, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
