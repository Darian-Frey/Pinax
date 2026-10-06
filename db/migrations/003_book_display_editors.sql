-- Schema version 2 -> 3.
--
-- v_book_display's authors and author_sort fall back to a book's editors when
-- it credits no author, so an anthology shows 'Mike Ashley (ed.)' and files
-- under Ashley (IMP-004). Views only; no table changes.
--
-- The CREATE VIEW below must stay byte-identical to the one in db/schema.sql;
-- tests/test_db.cpp compares a migrated file against a fresh one.

DROP VIEW v_book_display;

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

INSERT INTO schema_version (version, note) VALUES (3, 'v_book_display: editors stand in when a book has no author');
