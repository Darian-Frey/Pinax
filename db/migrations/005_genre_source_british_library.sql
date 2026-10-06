-- Schema version 4 -> 5.
--
-- book_genre.source accepts 'british_library' (D-022). SQLite cannot alter a
-- CHECK constraint, so the table is rebuilt: the old one is set aside, the
-- new one created under the final name — so its stored SQL is exactly
-- db/schema.sql's — and the rows copied across. Nothing references
-- book_genre, so setting it aside disturbs no other table, view or trigger.
--
-- The CREATE statements below must stay byte-identical to those in
-- db/schema.sql; tests/test_db.cpp compares a migrated file against a fresh
-- one.

ALTER TABLE book_genre RENAME TO book_genre_v4;

CREATE TABLE book_genre (
    book_id     INTEGER NOT NULL REFERENCES book(id)  ON DELETE CASCADE,
    genre_id    INTEGER NOT NULL REFERENCES genre(id) ON DELETE CASCADE,
    source      TEXT    NOT NULL DEFAULT 'google_books'
                  CHECK (source IN ('google_books','open_library','british_library','manual')),
    PRIMARY KEY (book_id, genre_id)
);

INSERT INTO book_genre (book_id, genre_id, source)
SELECT book_id, genre_id, source FROM book_genre_v4;

DROP TABLE book_genre_v4;

CREATE INDEX idx_book_genre_genre ON book_genre (genre_id);

INSERT INTO schema_version (version, note) VALUES (5, 'book_genre.source: british_library');
