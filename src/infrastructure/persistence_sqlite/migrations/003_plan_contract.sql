ALTER TABLE plans ADD COLUMN format_version INTEGER NOT NULL DEFAULT 1;
ALTER TABLE steps ADD COLUMN movement_json TEXT;

INSERT INTO schema_migrations(version, applied_at)
VALUES (3, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
