#include "db/book_repository.h"
#include "db/connection.h"
#include "db/db_error.h"
#include "db/migrations.h"
#include "db/statement.h"

#include <QTemporaryDir>
#include <QTest>

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

    // BookRepository
    void bookRoundTripsEveryField();
    void sortTitleIsDerivedWhenEmpty();
    void missingBookIsReportedNotThrown();
    void importedReadBookKeepsItsTimesRead();
    void finishingABookIncrementsTimesRead();
    void ratingOutsideRangeIsRejected();
    void duplicateIsbn13IsRejected();
    void deletingABookLeavesNoOrphanLinks();
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
    QCOMPARE(scalar(connection, "SELECT COUNT(*) FROM schema_version"), 1);
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
    // The schema keeps the series entry as a known-but-unowned volume
    // (ON DELETE SET NULL, D-006). See BUGS.md BUG-001 on whether F-001
    // intends that.
    QCOMPARE(scalar(connection, "SELECT COUNT(*) FROM series_entry WHERE book_id IS NULL"), 1);
}

QTEST_APPLESS_MAIN(TestDb)
#include "test_db.moc"
