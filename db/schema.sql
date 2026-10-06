-- Pinax — physical library catalogue
-- SQLite schema, version 4
--
-- This file is always the latest full schema, applied whole to an empty
-- database. Older files are carried forward by db/migrations/NNN_*.sql, one
-- file per version step.
--
-- Apply with:  sqlite3 pinax.db < db/schema.sql
--
-- Schema objects only: no PRAGMA statements, so the whole file can run inside
-- one transaction and a failed apply leaves nothing behind. Connection
-- settings belong to the connection (src/db/connection.cpp):
--
--   PRAGMA foreign_keys = ON;   per-connection and never persisted; without it
--                               every REFERENCES clause below is advisory only
--                               (AV-004). Issue it on every connection.
--   PRAGMA journal_mode = WAL;  persisted in the file once set; cannot be
--                               changed inside a transaction.
--
-- When applying by hand, issue both before reading this file.


-- ---------------------------------------------------------------------------
-- Migration bookkeeping
-- ---------------------------------------------------------------------------

CREATE TABLE schema_version (
    version     INTEGER PRIMARY KEY,
    applied_at  TEXT    NOT NULL DEFAULT (datetime('now')),
    note        TEXT
);

-- One row per version, so a fresh file and a migrated one record the same
-- history.
INSERT INTO schema_version (version, note) VALUES (1, 'Initial schema');
INSERT INTO schema_version (version, note) VALUES (2, 'v_book_display: sort keys, series label, ordered credits');
INSERT INTO schema_version (version, note) VALUES (3, 'v_book_display: editors stand in when a book has no author');
INSERT INTO schema_version (version, note) VALUES (4, 'v_missing_entries: entry_id');


-- ---------------------------------------------------------------------------
-- People
-- ---------------------------------------------------------------------------

-- One row per person. Joint credits ("Niven & Pournelle") are two rows here
-- plus two rows in book_author, never a single combined author record.
CREATE TABLE author (
    id          INTEGER PRIMARY KEY,
    name        TEXT    NOT NULL UNIQUE,   -- display form: 'Alastair Reynolds'
    sort_name   TEXT    NOT NULL,          -- filing form:  'Reynolds, Alastair'
    notes       TEXT,
    created_at  TEXT    NOT NULL DEFAULT (datetime('now')),
    updated_at  TEXT    NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX idx_author_sort ON author (sort_name);


-- ---------------------------------------------------------------------------
-- Books
-- ---------------------------------------------------------------------------

CREATE TABLE book (
    id              INTEGER PRIMARY KEY,

    title           TEXT    NOT NULL,
    sort_title      TEXT    NOT NULL,      -- 'Long Earth, The'
    subtitle        TEXT,

    -- Reading state
    read_status     TEXT    NOT NULL DEFAULT 'unread'
                      CHECK (read_status IN ('unread','reading','read','abandoned')),
    times_read      INTEGER NOT NULL DEFAULT 0 CHECK (times_read >= 0),
    date_started    TEXT,                  -- ISO 8601; most recent start
    date_finished   TEXT,                  -- ISO 8601; most recent finish
    rating          INTEGER CHECK (rating BETWEEN 1 AND 10),  -- NULL = unrated

    -- Fetched descriptive metadata
    synopsis            TEXT,
    synopsis_source     TEXT CHECK (synopsis_source IN ('google_books','open_library','manual')),
    cover_path          TEXT,              -- relative to the cover cache directory
    cover_source        TEXT CHECK (cover_source IN ('google_books','open_library','manual')),

    -- Identifiers and edition facts
    isbn13          TEXT UNIQUE,
    isbn10          TEXT,
    publisher       TEXT,
    published_year  INTEGER CHECK (published_year BETWEEN 1400 AND 2200),
    page_count      INTEGER CHECK (page_count > 0),
    language        TEXT    NOT NULL DEFAULT 'en',
    binding         TEXT CHECK (binding IN ('paperback','hardback','omnibus','boxset','other')),

    -- Copy-specific notes. One physical copy per book record; see FEATURES.md F-004.
    edition_note    TEXT,                  -- '1961 G. Bell first English edition'
    condition_note  TEXT,                  -- 'near fine in unclipped 25/- jacket'
    acquired_date   TEXT,
    acquired_note   TEXT,

    -- Enrichment bookkeeping
    metadata_status TEXT    NOT NULL DEFAULT 'unmatched'
                      CHECK (metadata_status IN ('unmatched','matched','manual','failed')),
    metadata_fetched_at TEXT,

    notes           TEXT,
    created_at      TEXT    NOT NULL DEFAULT (datetime('now')),
    updated_at      TEXT    NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX idx_book_sort_title  ON book (sort_title);
CREATE INDEX idx_book_read_status ON book (read_status);
CREATE INDEX idx_book_rating      ON book (rating);
CREATE INDEX idx_book_meta_status ON book (metadata_status);


-- Credits. `ordinal` preserves cover order (0 = first-billed).
CREATE TABLE book_author (
    book_id     INTEGER NOT NULL REFERENCES book(id)   ON DELETE CASCADE,
    author_id   INTEGER NOT NULL REFERENCES author(id) ON DELETE CASCADE,
    ordinal     INTEGER NOT NULL DEFAULT 0,
    role        TEXT    NOT NULL DEFAULT 'author'
                  CHECK (role IN ('author','editor','translator','illustrator')),
    PRIMARY KEY (book_id, author_id, role)
);

CREATE INDEX idx_book_author_author ON book_author (author_id);


-- ---------------------------------------------------------------------------
-- Series
-- ---------------------------------------------------------------------------

CREATE TABLE series (
    id          INTEGER PRIMARY KEY,
    name        TEXT    NOT NULL UNIQUE,
    -- 1 when the series is still being written, so a full shelf is only
    -- ever "complete to date" (A Song of Ice and Fire, Red Space).
    ongoing     INTEGER NOT NULL DEFAULT 0 CHECK (ongoing IN (0,1)),
    notes       TEXT,
    created_at  TEXT    NOT NULL DEFAULT (datetime('now')),
    updated_at  TEXT    NOT NULL DEFAULT (datetime('now'))
);


-- Every entry a series is known to contain, whether or not it is owned.
--
--   book_id NOT NULL  -> an owned volume
--   book_id NULL      -> a known entry still missing from the shelf
--
-- Completeness and the "still missing" list are therefore derived from this
-- table rather than stored, and cannot go stale. A book may appear in more
-- than one series (Chasm City sits in Revelation Space; omnibuses span
-- several positions) — that is simply several rows.
CREATE TABLE series_entry (
    id              INTEGER PRIMARY KEY,
    series_id       INTEGER NOT NULL REFERENCES series(id) ON DELETE CASCADE,
    book_id         INTEGER          REFERENCES book(id)   ON DELETE SET NULL,

    -- As printed on the volume: '1', '6.5', '1-4', 'Broadcast 5', '3a',
    -- 'novellas', 'companion'. Free text by necessity.
    position        TEXT,
    -- Numeric key for ordering only. 6.5 files between 6 and 7; an omnibus
    -- spanning 1-4 takes 1.0. NULL sorts last.
    sort_position   REAL,

    -- Title of the entry. For an unowned entry this is the only description
    -- there is; for an owned one it may differ from book.title (reissues).
    title           TEXT,
    notes           TEXT
);

CREATE INDEX idx_series_entry_series ON series_entry (series_id, sort_position);
CREATE INDEX idx_series_entry_book   ON series_entry (book_id);

-- A given book may appear only once per series.
CREATE UNIQUE INDEX idx_series_entry_unique_book
    ON series_entry (series_id, book_id) WHERE book_id IS NOT NULL;


-- ---------------------------------------------------------------------------
-- Genre
-- ---------------------------------------------------------------------------

-- Provider categories are stored verbatim, as returned:
--   'Fiction / Science Fiction / Space Opera'
-- No taxonomy is imposed on them at this version. See FEATURES.md, Out of scope.
CREATE TABLE genre (
    id      INTEGER PRIMARY KEY,
    name    TEXT    NOT NULL UNIQUE
);

CREATE TABLE book_genre (
    book_id     INTEGER NOT NULL REFERENCES book(id)  ON DELETE CASCADE,
    genre_id    INTEGER NOT NULL REFERENCES genre(id) ON DELETE CASCADE,
    source      TEXT    NOT NULL DEFAULT 'google_books'
                  CHECK (source IN ('google_books','open_library','manual')),
    PRIMARY KEY (book_id, genre_id)
);

CREATE INDEX idx_book_genre_genre ON book_genre (genre_id);


-- ---------------------------------------------------------------------------
-- Derived views
-- ---------------------------------------------------------------------------

-- One row per book, flattened for the list view.
--
--   authors               credited authors in cover order, joined ' & ';
--                         for a book with no author, its editors, marked
--                         'Mike Ashley (ed.)' or 'A & B (eds.)' (IMP-004)
--   author_sort           filing form of the first-billed author, so
--                         'Iain M. Banks' sorts under B; else of the first
--                         editor
--
-- These two columns are for display and sorting only. Anything that counts
-- books per author must count role 'author' credits in book_author, never
-- this view, or an editor would be credited with writing an anthology.
--   series, positions     every series the book is in, by series name, with
--                         positions in step ('' where none is printed)
--   series_label          'The Culture · 5', joined '; ' for several series
--   series_sort,          the first of those series and the book's
--   series_sort_position  sort_position in it: the series column's sort key
--                         (never parsed from position, D-005)
--
-- Multi-valued columns are joined from an ordered inner subquery. An ORDER BY
-- beside group_concat in the same query does not fix the order of what it
-- joins (BUG-002).
CREATE VIEW v_book_display AS
SELECT
    b.id,
    b.title,
    b.sort_title,
    COALESCE(
        (SELECT group_concat(name, ' & ') FROM (
            SELECT a.name
              FROM book_author ba
              JOIN author a ON a.id = ba.author_id
             WHERE ba.book_id = b.id AND ba.role = 'author'
             ORDER BY ba.ordinal, a.sort_name)),
        (SELECT group_concat(name, ' & ')
                || CASE WHEN COUNT(*) = 1 THEN ' (ed.)' ELSE ' (eds.)' END
           FROM (
            SELECT a.name
              FROM book_author ba
              JOIN author a ON a.id = ba.author_id
             WHERE ba.book_id = b.id AND ba.role = 'editor'
             ORDER BY ba.ordinal, a.sort_name)
         HAVING COUNT(*) > 0))                              AS authors,
    COALESCE(
        (SELECT a.sort_name
           FROM book_author ba
           JOIN author a ON a.id = ba.author_id
          WHERE ba.book_id = b.id AND ba.role = 'author'
          ORDER BY ba.ordinal, a.sort_name
          LIMIT 1),
        (SELECT a.sort_name
           FROM book_author ba
           JOIN author a ON a.id = ba.author_id
          WHERE ba.book_id = b.id AND ba.role = 'editor'
          ORDER BY ba.ordinal, a.sort_name
          LIMIT 1))                                         AS author_sort,
    (SELECT group_concat(name, '; ') FROM (
        SELECT s.name
          FROM series_entry se
          JOIN series s ON s.id = se.series_id
         WHERE se.book_id = b.id
         ORDER BY s.name, se.id))                           AS series,
    (SELECT group_concat(position, '; ') FROM (
        SELECT COALESCE(se.position, '') AS position
          FROM series_entry se
          JOIN series s ON s.id = se.series_id
         WHERE se.book_id = b.id
         ORDER BY s.name, se.id))                           AS positions,
    (SELECT group_concat(label, '; ') FROM (
        SELECT s.name || COALESCE(' · ' || se.position, '') AS label
          FROM series_entry se
          JOIN series s ON s.id = se.series_id
         WHERE se.book_id = b.id
         ORDER BY s.name, se.id))                           AS series_label,
    (SELECT s.name
       FROM series_entry se
       JOIN series s ON s.id = se.series_id
      WHERE se.book_id = b.id
      ORDER BY s.name, se.id
      LIMIT 1)                                              AS series_sort,
    (SELECT se.sort_position
       FROM series_entry se
       JOIN series s ON s.id = se.series_id
      WHERE se.book_id = b.id
      ORDER BY s.name, se.id
      LIMIT 1)                                              AS series_sort_position,
    b.read_status,
    b.times_read,
    b.date_finished,
    b.rating,
    b.published_year,
    b.cover_path,
    b.metadata_status
FROM book b;


-- Series completeness, derived rather than stored.
--   held     = entries on the shelf
--   known    = entries the series is known to contain
--   status   = Complete | Complete to date | Incomplete | Unknown
CREATE VIEW v_series_status AS
SELECT
    s.id,
    s.name,
    s.ongoing,
    SUM(CASE WHEN se.book_id IS NOT NULL THEN 1 ELSE 0 END)  AS held,
    COUNT(se.id)                                             AS known,
    SUM(CASE WHEN se.book_id IS NOT NULL
              AND b.read_status = 'read' THEN 1 ELSE 0 END)  AS held_read,
    CASE
        WHEN COUNT(se.id) = 0 THEN 'Unknown'
        WHEN SUM(CASE WHEN se.book_id IS NULL THEN 1 ELSE 0 END) > 0 THEN 'Incomplete'
        WHEN s.ongoing = 1 THEN 'Complete to date'
        ELSE 'Complete'
    END                                                      AS status
FROM series s
LEFT JOIN series_entry se ON se.series_id = s.id
LEFT JOIN book b          ON b.id = se.book_id
GROUP BY s.id, s.name, s.ongoing;


-- The shopping list: known entries not on the shelf. entry_id identifies the
-- volume for anything that acts on it, such as marking it owned (version 4).
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


-- ---------------------------------------------------------------------------
-- Triggers
-- ---------------------------------------------------------------------------

CREATE TRIGGER trg_book_updated
AFTER UPDATE ON book FOR EACH ROW
BEGIN
    UPDATE book SET updated_at = datetime('now') WHERE id = NEW.id;
END;

CREATE TRIGGER trg_author_updated
AFTER UPDATE ON author FOR EACH ROW
BEGIN
    UPDATE author SET updated_at = datetime('now') WHERE id = NEW.id;
END;

CREATE TRIGGER trg_series_updated
AFTER UPDATE ON series FOR EACH ROW
BEGIN
    UPDATE series SET updated_at = datetime('now') WHERE id = NEW.id;
END;

-- Finishing a book increments the re-read counter and stamps the date.
-- Re-reads are therefore counted by moving the status back to 'reading'
-- and forward to 'read' again, not by editing the counter by hand.
CREATE TRIGGER trg_book_finished
AFTER UPDATE OF read_status ON book FOR EACH ROW
WHEN NEW.read_status = 'read' AND OLD.read_status <> 'read'
BEGIN
    UPDATE book
       SET times_read    = OLD.times_read + 1,
           date_finished = date('now')
     WHERE id = NEW.id;
END;
