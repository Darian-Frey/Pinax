-- Schema version 1 -> 2.
--
-- v_book_display gains the sort keys the list view needs (author_sort,
-- series_sort, series_sort_position, date_finished) and a ready-made
-- series_label, and joins its multi-valued columns in a defined order
-- (BUG-002). Views only; no table changes.
--
-- The CREATE VIEW below must stay byte-identical to the one in db/schema.sql;
-- tests/test_db.cpp compares a migrated file against a fresh one.

DROP VIEW v_book_display;

CREATE VIEW v_book_display AS
SELECT
    b.id,
    b.title,
    b.sort_title,
    (SELECT group_concat(name, ' & ') FROM (
        SELECT a.name
          FROM book_author ba
          JOIN author a ON a.id = ba.author_id
         WHERE ba.book_id = b.id AND ba.role = 'author'
         ORDER BY ba.ordinal, a.sort_name))                 AS authors,
    (SELECT a.sort_name
       FROM book_author ba
       JOIN author a ON a.id = ba.author_id
      WHERE ba.book_id = b.id AND ba.role = 'author'
      ORDER BY ba.ordinal, a.sort_name
      LIMIT 1)                                              AS author_sort,
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

INSERT INTO schema_version (version, note) VALUES (2, 'v_book_display: sort keys, series label, ordered credits');
