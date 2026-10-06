-- Schema version 3 -> 4.
--
-- v_missing_entries gains entry_id, so the missing-volumes view can act on a
-- row — mark it owned, edit it — through the view rather than a second query
-- that recomputes it (ARCHITECTURE.md §2, AV-011). Views only.
--
-- The CREATE VIEW below must stay byte-identical to the one in db/schema.sql;
-- tests/test_db.cpp compares a migrated file against a fresh one.

DROP VIEW v_missing_entries;

CREATE VIEW v_missing_entries AS
SELECT
    s.id          AS series_id,
    s.name        AS series_name,
    se.position,
    se.sort_position,
    se.title,
    (SELECT COUNT(*) FROM series_entry x
      WHERE x.series_id = s.id AND x.book_id IS NULL)  AS missing_in_series,
    se.id         AS entry_id
FROM series_entry se
JOIN series s ON s.id = se.series_id
WHERE se.book_id IS NULL
ORDER BY missing_in_series ASC, s.name, se.sort_position;

INSERT INTO schema_version (version, note) VALUES (4, 'v_missing_entries: entry_id');
