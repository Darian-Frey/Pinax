-- Schema version 5 -> 6.
--
-- Genre names become unique whatever their capitals (IMP-009). Genres that
-- differ only in case are merged first into the one stored first, the
-- lowest id: each book keeps one link to it, marked the owner's if either
-- link was; then the case-blind unique index keeps them merged.
--
-- The CREATE INDEX below must stay byte-identical to the one in
-- db/schema.sql; tests/test_db.cpp compares a migrated file against a fresh
-- one.

CREATE TEMP TABLE genre_keeper AS
SELECT g.id AS id,
       (SELECT MIN(k.id) FROM genre k WHERE k.name = g.name COLLATE NOCASE) AS keeper
FROM genre g;

-- The owner's say-so survives the merge.
UPDATE book_genre SET source = 'manual'
WHERE EXISTS (
    SELECT 1
      FROM book_genre d
      JOIN genre_keeper m ON m.id = d.genre_id
     WHERE m.id <> m.keeper
       AND m.keeper = book_genre.genre_id
       AND d.book_id = book_genre.book_id
       AND d.source = 'manual');

INSERT OR IGNORE INTO book_genre (book_id, genre_id, source)
SELECT d.book_id, m.keeper, d.source
  FROM book_genre d
  JOIN genre_keeper m ON m.id = d.genre_id
 WHERE m.id <> m.keeper;

DELETE FROM book_genre WHERE genre_id IN (SELECT id FROM genre_keeper WHERE id <> keeper);
DELETE FROM genre WHERE id IN (SELECT id FROM genre_keeper WHERE id <> keeper);
DROP TABLE genre_keeper;

-- One genre whatever its capitals: "Science fiction" and "Science Fiction"
-- are the same (IMP-009). ASCII letters only, as SQLite's NOCASE folds them.
CREATE UNIQUE INDEX idx_genre_name_nocase ON genre (name COLLATE NOCASE);

INSERT INTO schema_version (version, note) VALUES (6, 'genre names unique whatever their capitals');
