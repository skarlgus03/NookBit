ALTER TABLE transfer_events ADD COLUMN staging_cleanup INTEGER NOT NULL DEFAULT 0 CHECK(staging_cleanup IN (0,1,2));
INSERT INTO schema_migrations(version, applied_at)
VALUES (5, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
