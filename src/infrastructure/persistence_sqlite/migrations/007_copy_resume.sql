ALTER TABLE transfer_events ADD COLUMN copy_resume INTEGER NOT NULL DEFAULT 0 CHECK(copy_resume IN (0,1,2));
INSERT INTO schema_migrations(version, applied_at)
VALUES (7, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
