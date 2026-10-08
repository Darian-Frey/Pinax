#include "db/backup.h"
#include "db/book_repository.h"
#include "db/dump.h"
#include "db/genre_repository.h"
#include "db/author_repository.h"
#include "db/connection.h"
#include "db/db_error.h"
#include "db/migrations.h"
#include "db/statement.h"

#include <QFile>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QTest>

#include <algorithm>
#include <sstream>

#include <sqlite3.h>

using pinax::db::BookRepository;
using pinax::db::Connection;
using pinax::db::DbError;
using pinax::db::Statement;
using pinax::domain::Binding;
using pinax::domain::Book;
using pinax::domain::MetadataStatus;
using pinax::domain::ReadStatus;
using pinax::domain::Source;

namespace {

std::int64_t scalar(Connection& connection, std::string_view sql)
{
    Statement statement(connection, sql);
    statement.step();
    return statement.columnInt(0);
}

// Every schema object and the version history, as one comparable string.
std::string schemaDump(Connection& connection)
{
    std::string dump;
    Statement objects(connection,
        "SELECT type, name, tbl_name, sql FROM sqlite_master ORDER BY type, name");
    while (objects.step()) {
        for (int column = 0; column < 4; ++column)
            dump += objects.columnText(column) + '\x1f';
        dump += '\n';
    }
    Statement versions(connection, "SELECT version, note FROM schema_version ORDER BY version");
    while (versions.step())
        dump += std::to_string(versions.columnInt(0)) + ' ' + versions.columnText(1) + '\n';
    return dump;
}

std::string scalarText(Connection& connection, std::string_view sql)
{
    Statement statement(connection, sql);
    statement.step();
    return statement.columnText(0);
}

Book titled(const std::string& title)
{
    Book book;
    book.title = title;
    return book;
}

// Expects `action` to throw DbError whose primary result code is `code`.
template <typename Action>
bool throwsWithCode(Action action, int code)
{
    try {
        action();
    } catch (const DbError& error) {
        return (error.code() & 0xff) == code;
    }
    return false;
}

} // namespace

class TestDb : public QObject {
    Q_OBJECT

private slots:
    // Connection and migrations
    void migrateCreatesSchemaOnEmptyDatabase();
    void migrateIsIdempotent();
    void migrateRefusesNewerDatabase();
    void failedMigrationLeavesNothingBehind();
    void foreignKeysEnforcedOnEveryConnection();
    void migratingVersion1MatchesFreshSchema();
    void genreLinksSurviveTheVersion5Rebuild();
    void version6MergesGenresThatDifferOnlyInCase();

    // BookRepository
    void bookRoundTripsEveryField();
    void sortTitleIsDerivedWhenEmpty();
    void missingBookIsReportedNotThrown();
    void importedReadBookKeepsItsTimesRead();
    void finishingABookIncrementsTimesRead();
    void ratingOutsideRangeIsRejected();
    void duplicateIsbn13IsRejected();
    void deletingABookLeavesNoOrphanLinks();

    // v_book_display through BookRepository::summaries
    void authorsJoinInCoverOrder();
    void summariesCarrySeriesSortKeys();
    void severalSeriesJoinInStableOrder();
    void editorsStandInWhenThereIsNoAuthor();

    // F-017
    void filtersCombine();

    // F-020, AV-003
    void aBackupHoldsWhatTheLogHolds();
    void aFailedBackupLeavesTheOldOneAlone();

    // F-021
    void aDumpRestoresExactly();
};

void TestDb::migrateCreatesSchemaOnEmptyDatabase()
{
    Connection connection(":memory:");
    QCOMPARE(pinax::db::schemaVersion(connection), 0);

    QCOMPARE(pinax::db::migrate(connection), pinax::db::latestSchemaVersion);
    QCOMPARE(pinax::db::schemaVersion(connection), pinax::db::latestSchemaVersion);
    QCOMPARE(scalar(connection,
                 "SELECT COUNT(*) FROM sqlite_master WHERE type = 'view' AND name IN "
                 "('v_book_display', 'v_series_status', 'v_missing_entries')"),
        3);
}

void TestDb::migrateIsIdempotent()
{
    Connection connection(":memory:");
    pinax::db::migrate(connection);
    pinax::db::migrate(connection);
    // One row per version, none added by the second run.
    QCOMPARE(scalar(connection, "SELECT COUNT(*) FROM schema_version"),
        pinax::db::latestSchemaVersion);
}

void TestDb::migrateRefusesNewerDatabase()
{
    Connection connection(":memory:");
    pinax::db::migrate(connection);
    connection.exec("INSERT INTO schema_version (version, note) VALUES (99, 'from the future')");
    QVERIFY_THROWS_EXCEPTION(DbError, pinax::db::migrate(connection));
}

void TestDb::failedMigrationLeavesNothingBehind()
{
    // A stray table makes the schema fail part-way through.
    Connection connection(":memory:");
    connection.exec("CREATE TABLE genre (id INTEGER PRIMARY KEY)");

    QVERIFY_THROWS_EXCEPTION(DbError, pinax::db::migrate(connection));
    QCOMPARE(pinax::db::schemaVersion(connection), 0);
    QCOMPARE(scalar(connection, "SELECT COUNT(*) FROM sqlite_master WHERE name = 'book'"), 0);
}

void TestDb::foreignKeysEnforcedOnEveryConnection()
{
    // AV-004: the setting is per connection, so check more than one.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const std::string path = dir.filePath(QStringLiteral("pinax.db")).toStdString();

    {
        Connection first(path);
        QVERIFY(first.foreignKeysEnabled());
        pinax::db::migrate(first);
    }
    Connection second(path);
    QVERIFY(second.foreignKeysEnabled());
    QCOMPARE(scalarText(second, "PRAGMA journal_mode"), std::string("wal"));

    // And it is enforced, not merely reported.
    QVERIFY(throwsWithCode(
        [&] { second.exec("INSERT INTO book_author (book_id, author_id) VALUES (999, 999)"); },
        SQLITE_CONSTRAINT));
}

void TestDb::genreLinksSurviveTheVersion5Rebuild()
{
    QFile fixture(QStringLiteral(PINAX_TEST_FIXTURES "/schema_v1.sql"));
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    Connection connection(":memory:");
    connection.exec(fixture.readAll().toStdString());
    connection.exec("INSERT INTO book (id, title, sort_title) VALUES (1, 'Titan', 'Titan');"
                    "INSERT INTO genre (id, name) VALUES (1, 'Science fiction'), (2, 'Space opera');"
                    "INSERT INTO book_genre (book_id, genre_id, source) VALUES (1, 1, 'open_library'), (1, 2, 'manual');");
    pinax::db::migrate(connection);

    QCOMPARE(scalar(connection, "SELECT COUNT(*) FROM book_genre"), 2);
    QCOMPARE(scalar(connection, "SELECT COUNT(*) FROM book_genre WHERE source = 'manual'"), 1);
    connection.exec("INSERT INTO book_genre (book_id, genre_id, source) VALUES (1, 1, 'british_library') "
                    "ON CONFLICT DO UPDATE SET source = excluded.source");
    QCOMPARE(scalar(connection, "SELECT COUNT(*) FROM book_genre WHERE source = 'british_library'"), 1);
    // Still cascades with its book.
    connection.exec("DELETE FROM book WHERE id = 1");
    QCOMPARE(scalar(connection, "SELECT COUNT(*) FROM book_genre"), 0);
}

void TestDb::version6MergesGenresThatDifferOnlyInCase()
{
    // IMP-009: three spellings of one genre, stored before version 6.
    QFile fixture(QStringLiteral(PINAX_TEST_FIXTURES "/schema_v1.sql"));
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    Connection connection(":memory:");
    connection.exec(fixture.readAll().toStdString());
    connection.exec("INSERT INTO book (id, title, sort_title) VALUES (1, 'Titan', 'Titan'), (2, 'Voyage', 'Voyage');"
                    "INSERT INTO genre (id, name) VALUES (1, 'Science fiction'), (2, 'Science Fiction'),"
                    " (3, 'SCIENCE FICTION'), (4, 'Space opera');"
                    "INSERT INTO book_genre (book_id, genre_id, source) VALUES"
                    " (1, 1, 'open_library'), (1, 2, 'manual'),"   // both on one book; one the owner's
                    " (2, 3, 'google_books'), (2, 4, 'open_library');");
    pinax::db::migrate(connection);

    // The first spelling stands; every link moved to it, once per book.
    QCOMPARE(scalarText(connection, "SELECT group_concat(name, '|') FROM (SELECT name FROM genre ORDER BY id)"),
        std::string("Science fiction|Space opera"));
    QCOMPARE(scalar(connection, "SELECT COUNT(*) FROM book_genre WHERE book_id = 1"), 1);
    QCOMPARE(scalarText(connection, "SELECT source FROM book_genre WHERE book_id = 1"), std::string("manual"));
    QCOMPARE(scalar(connection, "SELECT genre_id FROM book_genre WHERE book_id = 2 AND source = 'google_books'"), 1);

    // And they stay merged: another spelling finds the same genre, and a
    // raw insert of one is refused.
    QCOMPARE(pinax::db::GenreRepository(connection).findOrCreate("science FICTION"), std::int64_t(1));
    QVERIFY_THROWS_EXCEPTION(DbError, connection.exec("INSERT INTO genre (name) VALUES ('Space Opera')"));
}

void TestDb::migratingVersion1MatchesFreshSchema()
{
    QFile fixture(QStringLiteral(PINAX_TEST_FIXTURES "/schema_v1.sql"));
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    const std::string version1 = fixture.readAll().toStdString();

    Connection migrated(":memory:");
    migrated.exec(version1);
    QCOMPARE(pinax::db::schemaVersion(migrated), 1);
    QCOMPARE(pinax::db::migrate(migrated), pinax::db::latestSchemaVersion);

    Connection fresh(":memory:");
    pinax::db::migrate(fresh);

    QCOMPARE(QString::fromStdString(schemaDump(migrated)),
        QString::fromStdString(schemaDump(fresh)));
}

void TestDb::bookRoundTripsEveryField()
{
    Connection connection(":memory:");
    pinax::db::migrate(connection);
    BookRepository books(connection);

    Book book;
    book.title = "Playing with Infinity";
    book.sortTitle = "Playing with Infinity";
    book.subtitle = "Mathematical Explorations and Excursions";
    book.readStatus = ReadStatus::Reading;
    book.timesRead = 2;
    book.dateStarted = "2026-09-01";
    book.dateFinished = "2025-03-14";
    book.rating = 9;
    book.synopsis = "Entered by hand.";
    book.synopsisSource = Source::Manual;
    book.coverPath = "covers/1.jpg";
    book.coverSource = Source::OpenLibrary;
    book.isbn13 = "9780486232652";
    book.isbn10 = "0486232654";
    book.publisher = "G. Bell";
    book.publishedYear = 1961;
    book.pageCount = 268;
    book.language = "en";
    book.binding = Binding::Hardback;
    book.editionNote = "1961 first English edition";
    book.conditionNote = "near fine in unclipped jacket";
    book.acquiredDate = "2020-05-02";
    book.acquiredNote = "second-hand";
    book.metadataStatus = MetadataStatus::Manual;
    book.metadataFetchedAt = "2026-10-05T12:00:00";
    book.notes = "Shelved with the non-fiction.";

    book.id = books.create(book);
    QVERIFY(book.id > 0);

    const std::optional<Book> stored = books.find(book.id);
    QVERIFY(stored);
    QVERIFY(!stored->createdAt.empty());
    QVERIFY(!stored->updatedAt.empty());

    Book expected = book;
    expected.createdAt = stored->createdAt;
    expected.updatedAt = stored->updatedAt;
    QVERIFY(*stored == expected);
}

void TestDb::sortTitleIsDerivedWhenEmpty()
{
    Connection connection(":memory:");
    pinax::db::migrate(connection);
    BookRepository books(connection);

    const std::int64_t id = books.create(titled("The Hydrogen Sonata"));
    QCOMPARE(books.find(id)->sortTitle, std::string("Hydrogen Sonata, The"));
}

void TestDb::missingBookIsReportedNotThrown()
{
    Connection connection(":memory:");
    pinax::db::migrate(connection);
    BookRepository books(connection);

    QVERIFY(!books.find(42));
    Book ghost = titled("Consider Phlebas");
    ghost.id = 42;
    QVERIFY(!books.update(ghost));
    QVERIFY(!books.remove(42));
}

void TestDb::importedReadBookKeepsItsTimesRead()
{
    // AV-005: the re-read trigger fires on UPDATE only, so an insert must
    // carry the count itself.
    Connection connection(":memory:");
    pinax::db::migrate(connection);
    BookRepository books(connection);

    Book book = titled("Excession");
    book.readStatus = ReadStatus::Read;
    book.timesRead = 1;
    const std::int64_t id = books.create(book);

    QCOMPARE(books.find(id)->timesRead, 1);
}

void TestDb::finishingABookIncrementsTimesRead()
{
    // F-006: a re-read is read -> reading -> read.
    Connection connection(":memory:");
    pinax::db::migrate(connection);
    BookRepository books(connection);

    Book book = titled("Surface Detail");
    book.id = books.create(book);

    book.readStatus = ReadStatus::Read;
    QVERIFY(books.update(book));
    book = *books.find(book.id);
    QCOMPARE(book.timesRead, 1);
    QVERIFY(book.dateFinished);

    book.readStatus = ReadStatus::Reading;
    QVERIFY(books.update(book));
    book.readStatus = ReadStatus::Read;
    QVERIFY(books.update(book));
    QCOMPARE(books.find(book.id)->timesRead, 2);

    // Saving an already-read book again is not a re-read.
    QVERIFY(books.update(*books.find(book.id)));
    QCOMPARE(books.find(book.id)->timesRead, 2);
}

void TestDb::ratingOutsideRangeIsRejected()
{
    // F-007: rejected, not clamped.
    Connection connection(":memory:");
    pinax::db::migrate(connection);
    BookRepository books(connection);

    for (int rating : {0, 11}) {
        Book book = titled("Matter");
        book.rating = rating;
        QVERIFY(throwsWithCode([&] { books.create(book); }, SQLITE_CONSTRAINT));
    }
    QCOMPARE(books.count(), 0);

    Book book = titled("Matter");
    book.rating = 10;
    QCOMPARE(books.find(books.create(book))->rating, std::optional<int>(10));
}

void TestDb::duplicateIsbn13IsRejected()
{
    Connection connection(":memory:");
    pinax::db::migrate(connection);
    BookRepository books(connection);

    Book book = titled("Consider Phlebas");
    book.isbn13 = "9780316005388";
    books.create(book);
    QVERIFY(throwsWithCode([&] { books.create(book); }, SQLITE_CONSTRAINT));
    QCOMPARE(books.count(), 1);
}

void TestDb::deletingABookLeavesNoOrphanLinks()
{
    // AV-004 detection: with foreign keys on, credits and genre links
    // cascade away with the book.
    Connection connection(":memory:");
    pinax::db::migrate(connection);
    BookRepository books(connection);

    const std::int64_t id = books.create(titled("Use of Weapons"));
    const std::string bookId = std::to_string(id);
    connection.exec(
        "INSERT INTO author (id, name, sort_name) VALUES (1, 'Iain M. Banks', 'Banks, Iain M.');"
        "INSERT INTO book_author (book_id, author_id) VALUES (" + bookId + ", 1);"
        "INSERT INTO genre (id, name) VALUES (1, 'Fiction / Science Fiction / Space Opera');"
        "INSERT INTO book_genre (book_id, genre_id) VALUES (" + bookId + ", 1);"
        "INSERT INTO series (id, name) VALUES (1, 'The Culture');"
        "INSERT INTO series_entry (series_id, book_id, position, sort_position) "
        "VALUES (1, " + bookId + ", '3', 3);");

    QVERIFY(books.remove(id));

    QCOMPARE(books.count(), 0);
    QCOMPARE(scalar(connection, "SELECT COUNT(*) FROM book_author"), 0);
    QCOMPARE(scalar(connection, "SELECT COUNT(*) FROM book_genre"), 0);
    // The series entry stays as a known-but-unowned volume, so the series
    // shows the gap (F-001, D-006; settled in BUG-001).
    QCOMPARE(scalar(connection, "SELECT COUNT(*) FROM series_entry WHERE book_id IS NULL"), 1);
}

void TestDb::authorsJoinInCoverOrder()
{
    // BUG-002: the first-billed author was not reliably joined first.
    Connection connection(":memory:");
    pinax::db::migrate(connection);
    BookRepository books(connection);

    const std::string id = std::to_string(books.create(titled("The Mote in God's Eye")));
    // Pournelle is inserted first and has the lower id; Niven is billed first.
    connection.exec(
        "INSERT INTO author (id, name, sort_name) VALUES "
        "(1, 'Jerry Pournelle', 'Pournelle, Jerry'), (2, 'Larry Niven', 'Niven, Larry');"
        "INSERT INTO book_author (book_id, author_id, ordinal) VALUES (" + id + ", 1, 1);"
        "INSERT INTO book_author (book_id, author_id, ordinal) VALUES (" + id + ", 2, 0);");

    const auto rows = books.summaries();
    QCOMPARE(rows.size(), std::size_t(1));
    QCOMPARE(rows[0].authors, std::optional<std::string>("Larry Niven & Jerry Pournelle"));
    QCOMPARE(rows[0].authorSort, std::optional<std::string>("Niven, Larry"));
}

void TestDb::summariesCarrySeriesSortKeys()
{
    Connection connection(":memory:");
    pinax::db::migrate(connection);
    BookRepository books(connection);

    Book excession = titled("Excession");
    excession.readStatus = ReadStatus::Read;
    excession.timesRead = 1;
    excession.dateFinished = "2024-02-11";
    excession.rating = 9;
    excession.publishedYear = 1996;
    const std::string id = std::to_string(books.create(excession));
    books.create(titled("Tau Zero"));
    connection.exec(
        "INSERT INTO series (id, name) VALUES (1, 'The Culture');"
        "INSERT INTO series_entry (series_id, book_id, position, sort_position) "
        "VALUES (1, " + id + ", '5', 5);");

    const auto rows = books.summaries();
    QCOMPARE(rows.size(), std::size_t(2));
    const auto& culture = rows[0].title == "Excession" ? rows[0] : rows[1];
    const auto& standalone = rows[0].title == "Excession" ? rows[1] : rows[0];

    QCOMPARE(culture.seriesLabel, std::optional<std::string>("The Culture · 5"));
    QCOMPARE(culture.seriesSort, std::optional<std::string>("The Culture"));
    QCOMPARE(culture.seriesSortPosition, std::optional<double>(5.0));
    QCOMPARE(culture.dateFinished, std::optional<std::string>("2024-02-11"));
    QCOMPARE(culture.rating, std::optional<int>(9));
    QCOMPARE(culture.publishedYear, std::optional<int>(1996));
    QVERIFY(culture.readStatus == ReadStatus::Read);

    QVERIFY(!standalone.seriesLabel);
    QVERIFY(!standalone.seriesSortPosition);
    QVERIFY(!standalone.authors);
}

void TestDb::severalSeriesJoinInStableOrder()
{
    // Series by name, with positions in step even where one is missing.
    Connection connection(":memory:");
    pinax::db::migrate(connection);
    BookRepository books(connection);

    const std::string id = std::to_string(books.create(titled("Chasm City")));
    connection.exec(
        "INSERT INTO series (id, name) VALUES (1, 'Revelation Space'), (2, 'Chasm City Sequence');"
        "INSERT INTO series_entry (series_id, book_id, position, sort_position) "
        "VALUES (1, " + id + ", 'companion', NULL);"
        "INSERT INTO series_entry (series_id, book_id, position, sort_position) "
        "VALUES (2, " + id + ", NULL, NULL);");

    const auto rows = books.summaries();
    QCOMPARE(rows[0].seriesLabel,
        std::optional<std::string>("Chasm City Sequence; Revelation Space · companion"));
    QCOMPARE(rows[0].seriesSort, std::optional<std::string>("Chasm City Sequence"));
    QCOMPARE(scalarText(connection, "SELECT positions FROM v_book_display"),
        std::string("; companion"));
}

void TestDb::editorsStandInWhenThereIsNoAuthor()
{
    // IMP-004: an anthology shows and files under its editor; a book with an
    // author never shows its editor there.
    Connection connection(":memory:");
    pinax::db::migrate(connection);
    BookRepository books(connection);

    const std::string mars = std::to_string(books.create(titled("Lost Mars")));
    const std::string pair = std::to_string(books.create(titled("Engineering Infinity")));
    const std::string both = std::to_string(books.create(titled("Excession")));
    connection.exec(
        "INSERT INTO author (id, name, sort_name) VALUES (1, 'Mike Ashley', 'Ashley, Mike'),"
        " (2, 'Jonathan Strahan', 'Strahan, Jonathan'), (3, 'Iain M. Banks', 'Banks, Iain M.');"
        "INSERT INTO book_author (book_id, author_id, ordinal, role) VALUES"
        " (" + mars + ", 1, 0, 'editor'),"
        " (" + pair + ", 2, 0, 'editor'), (" + pair + ", 1, 1, 'editor'),"
        " (" + both + ", 3, 0, 'author'), (" + both + ", 1, 1, 'editor');");

    auto row = [&](const std::string& id) {
        return scalarText(connection, "SELECT authors || ' / ' || author_sort FROM v_book_display WHERE id = " + id);
    };
    QCOMPARE(row(mars), std::string("Mike Ashley (ed.) / Ashley, Mike"));
    QCOMPARE(row(pair), std::string("Jonathan Strahan & Mike Ashley (eds.) / Strahan, Jonathan"));
    QCOMPARE(row(both), std::string("Iain M. Banks / Banks, Iain M."));

    // No credit of any kind stays empty.
    const std::string none = std::to_string(books.create(titled("Practical Algebra")));
    QCOMPARE(scalar(connection, "SELECT authors IS NULL AND author_sort IS NULL FROM v_book_display WHERE id = " + none), 1);
}

void TestDb::filtersCombine()
{
    Connection connection(":memory:");
    pinax::db::migrate(connection);
    connection.exec(
        "INSERT INTO book (id, title, sort_title, read_status, times_read, rating) VALUES"
        " (1, 'Excession', 'Excession', 'read', 1, 9),"
        " (2, 'Matter', 'Matter', 'unread', 0, NULL),"
        " (3, 'Surface Detail', 'Surface Detail', 'read', 1, 6),"
        " (4, 'Lost Mars', 'Lost Mars', 'unread', 0, 8);"
        "INSERT INTO author (id, name, sort_name) VALUES (1, 'Iain M. Banks', 'Banks, Iain M.'),"
        " (2, 'Mike Ashley', 'Ashley, Mike');"
        "INSERT INTO book_author (book_id, author_id, ordinal, role) VALUES"
        " (1, 1, 0, 'author'), (2, 1, 0, 'author'), (3, 1, 0, 'author'), (4, 2, 0, 'editor');"
        "INSERT INTO genre (id, name) VALUES (1, 'Space opera'), (2, 'Science fiction');"
        "INSERT INTO book_genre (book_id, genre_id, source) VALUES"
        " (1, 1, 'manual'), (3, 1, 'open_library'), (4, 2, 'british_library');"
        "INSERT INTO series (id, name) VALUES (1, 'The Culture');"
        "INSERT INTO series_entry (series_id, book_id, position, sort_position) VALUES"
        " (1, 1, '5', 5), (1, 3, '9', 9), (1, 2, '8', 8);");
    BookRepository books(connection);
    auto ids = [&](const pinax::domain::BookQuery& query) {
        auto found = books.idsMatching(query);
        std::sort(found.begin(), found.end());
        return found;
    };
    using Ids = std::vector<std::int64_t>;
    pinax::domain::BookQuery query;
    QCOMPARE(ids(query), (Ids {1, 2, 3, 4}));

    query.readStatus = ReadStatus::Read;
    QCOMPARE(ids(query), (Ids {1, 3}));
    query.ratingFrom = 8;
    query.ratingTo = 10;
    QCOMPARE(ids(query), (Ids {1}));                // read, and rated 8-10

    pinax::domain::BookQuery unrated;
    unrated.unratedOnly = true;
    QCOMPARE(ids(unrated), (Ids {2}));

    pinax::domain::BookQuery genre;
    genre.genreId = 1;
    QCOMPARE(ids(genre), (Ids {1, 3}));
    genre.ratingTo = 7;
    QCOMPARE(ids(genre), (Ids {3}));                // space opera rated at most 7

    // An editor is not an author (IMP-004): Mike Ashley wrote none of these.
    pinax::domain::BookQuery author;
    author.authorId = 2;
    QVERIFY(ids(author).empty());
    author.authorId = 1;
    author.readStatus = ReadStatus::Unread;
    QCOMPARE(ids(author), (Ids {2}));

    pinax::domain::BookQuery series;
    series.seriesId = 1;
    series.unratedOnly = true;
    QCOMPARE(ids(series), (Ids {2}));               // unrated Culture books

    // Search reads every name, any role, and every series (F-019).
    const auto texts = books.searchTexts();
    QVERIFY(texts.at(4).find("Mike Ashley") != std::string::npos);  // an editor
    QVERIFY(texts.at(2).find("The Culture") != std::string::npos);
    QVERIFY(texts.at(1).find("Excession") != std::string::npos);

    // The choices, with counts; editors are not offered as authors.
    const auto authors = pinax::db::AuthorRepository(connection).withCounts();
    QCOMPARE(authors.size(), std::size_t(1));
    QCOMPARE(authors.front().name, std::string("Iain M. Banks"));
    QCOMPARE(authors.front().count, 3);
    const auto genres = pinax::db::GenreRepository(connection).withCounts();
    QCOMPARE(genres.size(), std::size_t(2));
    QCOMPARE(genres.front().name, std::string("Science fiction"));
    QCOMPARE(genres.back().count, 2);
}

void TestDb::aBackupHoldsWhatTheLogHolds()
{
    // AV-003: in WAL mode the newest rows sit in the log, not the file; a
    // plain copy of the file misses them, and the backup must not.
    QTemporaryDir dir;
    const std::string live = dir.filePath(QStringLiteral("pinax.db")).toStdString();
    Connection connection(live);
    pinax::db::migrate(connection);
    QCOMPARE(scalarText(connection, "PRAGMA journal_mode"), std::string("wal"));
    BookRepository books(connection);
    for (int i = 0; i < 40; ++i)
        books.create(titled("Book " + std::to_string(i)));

    const QString naive = dir.filePath(QStringLiteral("naive.db"));
    QFile::copy(QString::fromStdString(live), naive);
    {
        Connection copied(naive.toStdString());
        // The trap: the copy lacks the newest rows — here even the tables,
        // made moments ago and still in the log.
        const bool hasBooks = scalar(copied, "SELECT COUNT(*) FROM sqlite_master WHERE name = 'book'") == 1;
        QVERIFY(!hasBooks || scalar(copied, "SELECT COUNT(*) FROM book") < 40);
    }

    const std::string target = dir.filePath(QStringLiteral("backups/pinax.db")).toStdString();
    const auto report = pinax::db::backupTo(connection, target);
    QCOMPARE(report.books, std::int64_t(40));
    QVERIFY(!QFile::exists(QString::fromStdString(target) + QStringLiteral(".partial")));
    {
        Connection restored(target);
        QCOMPARE(scalar(restored, "SELECT COUNT(*) FROM book"), 40);
        QCOMPARE(scalarText(restored, "PRAGMA integrity_check"), std::string("ok"));
        QCOMPARE(pinax::db::schemaVersion(restored), pinax::db::latestSchemaVersion);
    }

    // The live catalogue carries on, and a second backup replaces the first.
    books.create(titled("One more"));
    QCOMPARE(pinax::db::backupTo(connection, target).books, std::int64_t(41));
}

void TestDb::aFailedBackupLeavesTheOldOneAlone()
{
    QTemporaryDir dir;
    Connection connection(dir.filePath(QStringLiteral("pinax.db")).toStdString());
    pinax::db::migrate(connection);
    BookRepository(connection).create(titled("Excession"));
    const QString target = dir.filePath(QStringLiteral("pinax-backup.db"));
    pinax::db::backupTo(connection, target.toStdString());
    const auto before = QFileInfo(target).lastModified();

    // A folder for a file name, and a folder that cannot be made.
    QVERIFY_THROWS_EXCEPTION(DbError, pinax::db::backupTo(connection, dir.path().toStdString() + "/"));
    QFile blocker(dir.filePath(QStringLiteral("blocker")));
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    QVERIFY_THROWS_EXCEPTION(DbError,
        pinax::db::backupTo(connection, dir.filePath(QStringLiteral("blocker/inside/pinax.db")).toStdString()));

    QCOMPARE(QFileInfo(target).lastModified(), before);
    QVERIFY(QDir(dir.path()).entryList({QStringLiteral("*.partial")}, QDir::Files).isEmpty());
}

void TestDb::aDumpRestoresExactly()
{
    Connection original(":memory:");
    pinax::db::migrate(original);
    original.exec(
        "INSERT INTO author (id, name, sort_name) VALUES (1, 'Stanisław Lem', 'Lem, Stanisław');"
        "INSERT INTO book (id, title, sort_title, read_status, times_read, date_finished, rating, synopsis,"
        " synopsis_source) VALUES"
        " (1, 'Solaris', 'Solaris', 'read', 2, '2025-03-01', 9,"
        "  'It''s about a planet.' || char(13) || char(10) || 'Second line;' || char(10) || 'Third.', 'manual'),"
        " (2, 'The Cyberiad', 'Cyberiad, The', 'unread', 0, NULL, NULL, NULL, NULL);"
        "INSERT INTO book_author (book_id, author_id, ordinal, role) VALUES (1, 1, 0, 'author'), (2, 1, 0, 'author');"
        "INSERT INTO series (id, name) VALUES (1, 'Ijon Tichy');"
        "INSERT INTO series_entry (series_id, book_id, position, sort_position, title) VALUES"
        " (1, 2, '6.5', 6.5, NULL), (1, NULL, 'Companion', 0.1, 'The Star Diaries');"
        "INSERT INTO genre (id, name) VALUES (1, 'Science fiction');"
        "INSERT INTO book_genre (book_id, genre_id, source) VALUES (1, 1, 'british_library');");

    std::ostringstream first;
    pinax::db::writeDump(original, first);
    const std::string sql = first.str();
    // Readable: a line per row, the carriage return spelt out.
    QVERIFY(sql.find("INSERT INTO book VALUES(2,'The Cyberiad'") != std::string::npos);
    QVERIFY(sql.find("'It''s about a planet.'||char(13)||'") != std::string::npos);
    QVERIFY(sql.find("6.5,") != std::string::npos);
    // Deterministic: the same catalogue, the same bytes.
    std::ostringstream again;
    pinax::db::writeDump(original, again);
    QVERIFY(again.str() == sql);

    // Restored on an empty database: the same schema, the same values —
    // and the trigger that counts re-reads did not fire on the way in.
    Connection restored(":memory:");
    restored.exec(sql);
    QCOMPARE(QString::fromStdString(schemaDump(restored)), QString::fromStdString(schemaDump(original)));
    std::ostringstream roundTrip;
    pinax::db::writeDump(restored, roundTrip);
    QVERIFY(roundTrip.str() == sql);
    QCOMPARE(scalar(restored, "SELECT times_read FROM book WHERE id = 1"), 2);
    QCOMPARE(scalarText(restored, "SELECT synopsis FROM book WHERE id = 1"),
        std::string("It's about a planet.\r\nSecond line;\nThird."));
    QCOMPARE(scalarText(restored, "PRAGMA foreign_key_check"), std::string());

    // To a file, checked; a folder for a name is refused.
    QTemporaryDir dir;
    const auto report = pinax::db::dumpTo(original, dir.filePath(QStringLiteral("out/pinax.sql")).toStdString());
    QCOMPARE(report.books, std::int64_t(2));
    QVERIFY(QFile::exists(dir.filePath(QStringLiteral("out/pinax.sql"))));
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("out/pinax.sql.partial"))));
    QVERIFY_THROWS_EXCEPTION(DbError, pinax::db::dumpTo(original, dir.path().toStdString() + "/"));
}

QTEST_APPLESS_MAIN(TestDb)
#include "test_db.moc"
