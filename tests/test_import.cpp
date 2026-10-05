#include "db/author_repository.h"
#include "db/book_repository.h"
#include "db/connection.h"
#include "db/migrations.h"
#include "db/series_repository.h"
#include "db/statement.h"
#include "io/csv_importer.h"
#include "io/csv_reader.h"
#include "io/sort_position.h"

#include <QFileInfo>
#include <QTest>

using pinax::db::AuthorRepository;
using pinax::db::BookRepository;
using pinax::db::Connection;
using pinax::db::SeriesRepository;
using pinax::db::Statement;
using pinax::domain::CreditRole;
using pinax::domain::ReadStatus;
using pinax::io::CsvImporter;
using pinax::io::ImportReport;

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

struct Catalogue {
    Connection connection { ":memory:" };
    Catalogue() { pinax::db::migrate(connection); }

    ImportReport import(std::string_view csv) { return CsvImporter(connection).importText(csv); }
    std::int64_t count(std::string_view table)
    {
        return scalar(connection, "SELECT COUNT(*) FROM " + std::string(table));
    }
};

QString failures(const ImportReport& report)
{
    QStringList lines;
    for (const auto& failure : report.failures)
        lines << QStringLiteral("%1: %2").arg(failure.line).arg(QString::fromStdString(failure.message));
    return lines.join(QStringLiteral("; "));
}

} // namespace

class TestImport : public QObject {
    Q_OBJECT

private slots:
    // CSV reading
    void csvHandlesQuotesAndLineBreaks();
    void csvRejectsUnclosedQuote();

    // SPEC.md §1.2
    void sortPositionDerivation_data();
    void sortPositionDerivation();

    // F-003
    void importsRowsReusingAuthorsAndSeries();
    void secondRunChangesNothing();
    void readBooksCarryTheirCount();
    void jointCreditsBecomeSeparateAuthors();
    void creditRoleSuffixIsRecognised();
    void badRowsAreReportedAndTheRestImported();
    void unknownColumnStopsTheRun();
    void fillsAWaitingMissingVolume();
    void changedRowsUpdateInPlace();
    void explicitSortPositionWins();
    void failedRowDoesNotShadowTheNextBook();

    void seedCatalogueImportsInOnePass();
};

void TestImport::csvHandlesQuotesAndLineBreaks()
{
    const auto records = pinax::io::parseCsv(
        "\xEF\xBB\xBFtitle,notes\r\n"
        "\"Gridlinked\",\"first line\nsecond, with a comma\"\r\n"
        "\r\n"
        "\"Say \"\"hello\"\"\",\n");

    QCOMPARE(records.size(), std::size_t(3));
    QCOMPARE(records[0].fields, std::vector<std::string>({"title", "notes"}));
    QCOMPARE(records[1].line, 2);
    QCOMPARE(records[1].fields[1], std::string("first line\nsecond, with a comma"));
    // The quoted line break moves the next record down a line; the blank
    // line is skipped but still counted.
    QCOMPARE(records[2].line, 5);
    QCOMPARE(records[2].fields, std::vector<std::string>({"Say \"hello\"", ""}));
}

void TestImport::csvRejectsUnclosedQuote()
{
    try {
        pinax::io::parseCsv("title\n\"Never closed\n");
        QFAIL("expected CsvError");
    } catch (const pinax::io::CsvError& error) {
        QCOMPARE(error.line(), 2);
    }
}

void TestImport::sortPositionDerivation_data()
{
    QTest::addColumn<QString>("position");
    QTest::addColumn<double>("expected"); // -1 for none

    QTest::newRow("whole") << "6" << 6.0;
    QTest::newRow("two digits") << "18" << 18.0;
    QTest::newRow("half") << "6.5" << 6.5;
    QTest::newRow("word and number") << "Broadcast 6.5" << 6.5;
    QTest::newRow("word and zero") << "Broadcast 0" << 0.0;
    QTest::newRow("range") << "1-4" << 1.0;
    QTest::newRow("later range") << "3-4" << 3.0;
    QTest::newRow("word and range") << "Broadcast 1-2" << 1.0;
    QTest::newRow("letter a") << "3a" << 3.1;
    QTest::newRow("letter b") << "5b" << 5.2;
    QTest::newRow("novellas") << "novellas" << -1.0;
    QTest::newRow("companion") << "companion" << -1.0;
    QTest::newRow("abbreviation") << "Vol." << -1.0;
    QTest::newRow("empty") << "" << -1.0;
    QTest::newRow("trailing words") << "3 of 4" << -1.0;
}

void TestImport::sortPositionDerivation()
{
    QFETCH(QString, position);
    QFETCH(double, expected);

    const std::optional<double> derived = pinax::io::deriveSortPosition(position.toStdString());
    if (expected < 0) {
        QVERIFY(!derived);
    } else {
        QVERIFY(derived);
        QVERIFY(qAbs(*derived - expected) < 1e-9);
    }
}

void TestImport::importsRowsReusingAuthorsAndSeries()
{
    Catalogue catalogue;
    const ImportReport report = catalogue.import(
        "title,authors,series,position,shelf\n"
        "Gridlinked,Neal Asher,Agent Cormac,1,read\n"
        "The Line of Polity,Neal Asher,Agent Cormac,2,read\n"
        "Prador Moon,Neal Asher,,,unread\n");

    QVERIFY2(report.failures.empty(), qPrintable(failures(report)));
    QCOMPARE(report.inserted, 3);
    QCOMPARE(catalogue.count("book"), 3);
    QCOMPARE(catalogue.count("author"), 1);
    QCOMPARE(catalogue.count("series"), 1);
    QCOMPARE(catalogue.count("series_entry"), 2);
    QCOMPARE(scalarText(catalogue.connection, "SELECT sort_name FROM author"),
        std::string("Asher, Neal"));
}

void TestImport::secondRunChangesNothing()
{
    // AV-002.
    const char* csv =
        "title,authors,series,position,shelf,times_read,rating\n"
        "Excession,Iain M. Banks,The Culture,5,read,2,9\n"
        "The Mote in God's Eye,Larry Niven & Jerry Pournelle,Moties,1,read,1,8\n"
        "Tau Zero,Poul Anderson,,,unread,0,\n";

    Catalogue catalogue;
    QCOMPARE(catalogue.import(csv).inserted, 3);
    const std::string stamps = scalarText(catalogue.connection,
        "SELECT group_concat(updated_at) FROM book");
    const auto counts = std::array {catalogue.count("book"), catalogue.count("author"),
        catalogue.count("book_author"), catalogue.count("series"), catalogue.count("series_entry")};

    const ImportReport again = catalogue.import(csv);
    QVERIFY2(again.failures.empty(), qPrintable(failures(again)));
    QCOMPARE(again.inserted, 0);
    QCOMPARE(again.updated, 0);
    QCOMPARE(again.unchanged, 3);
    QCOMPARE((std::array {catalogue.count("book"), catalogue.count("author"),
                 catalogue.count("book_author"), catalogue.count("series"),
                 catalogue.count("series_entry")}),
        counts);
    // Not even touched: no update ran, so no timestamp moved.
    QCOMPARE(scalarText(catalogue.connection, "SELECT group_concat(updated_at) FROM book"), stamps);
}

void TestImport::readBooksCarryTheirCount()
{
    // AV-005: an inserted read book must not be left at zero.
    Catalogue catalogue;
    catalogue.import(
        "title,shelf,times_read\n"
        "Twice Read,read,2\n"
        "Count Left Out,read,\n"
        "Not Yet,unread,\n");

    QCOMPARE(scalar(catalogue.connection, "SELECT times_read FROM book WHERE title = 'Twice Read'"), 2);
    QCOMPARE(scalar(catalogue.connection, "SELECT times_read FROM book WHERE title = 'Count Left Out'"), 1);
    QCOMPARE(scalar(catalogue.connection, "SELECT times_read FROM book WHERE title = 'Not Yet'"), 0);
}

void TestImport::jointCreditsBecomeSeparateAuthors()
{
    // AV-008, D-007.
    Catalogue catalogue;
    catalogue.import(
        "title,authors\n"
        "The Mote in God's Eye,Larry Niven & Jerry Pournelle\n"
        "Ringworld,Larry Niven\n");

    QCOMPARE(catalogue.count("author"), 2);
    QCOMPARE(scalar(catalogue.connection,
                 "SELECT COUNT(*) FROM book_author ba JOIN author a ON a.id = ba.author_id "
                 "WHERE a.name = 'Larry Niven'"),
        2);
    QCOMPARE(scalarText(catalogue.connection,
                 "SELECT authors FROM v_book_display WHERE title = 'The Mote in God''s Eye'"),
        std::string("Larry Niven & Jerry Pournelle"));
}

void TestImport::creditRoleSuffixIsRecognised()
{
    Catalogue catalogue;
    const ImportReport report = catalogue.import(
        "title,authors\n"
        "The Mammoth Book of Extreme SF,Mike Ashley (editor)\n");
    QVERIFY2(report.failures.empty(), qPrintable(failures(report)));

    BookRepository books(catalogue.connection);
    const auto credits = books.credits(1);
    QCOMPARE(credits.size(), std::size_t(1));
    QVERIFY(credits[0].role == CreditRole::Editor);
    QCOMPARE(AuthorRepository(catalogue.connection).find(credits[0].authorId)->name,
        std::string("Mike Ashley"));
}

void TestImport::badRowsAreReportedAndTheRestImported()
{
    Catalogue catalogue;
    const ImportReport report = catalogue.import(
        "title,shelf,rating,isbn13,notes\n"                 // 1
        "Fine,read,7,,\n"                                   // 2
        "Too Good,read,11,,\n"                              // 3: rating out of range
        "Undecided,maybe,,,\n"                              // 4: shelf
        "Misprinted,unread,,9780306406158,\n"               // 5: check digit
        "Long Note,unread,,,\"spans\ntwo lines\"\n"         // 6-7: fine
        ",unread,,,\n"                                      // 8: no title
        "Also Fine,unread,,978-0-306-40615-7,\n"            // 9: fine, hyphens stripped
        "Duplicate ISBN,unread,,9780306406157,\n");         // 10: unique constraint

    QCOMPARE(report.inserted, 3);
    QCOMPARE(catalogue.count("book"), 3);

    std::vector<int> lines;
    for (const auto& failure : report.failures)
        lines.push_back(failure.line);
    QCOMPARE(lines, std::vector<int>({3, 4, 5, 8, 10}));
    QVERIFY(!report.aborted);
    QCOMPARE(scalarText(catalogue.connection, "SELECT isbn13 FROM book WHERE title = 'Also Fine'"),
        std::string("9780306406157"));
}

void TestImport::unknownColumnStopsTheRun()
{
    Catalogue catalogue;
    const ImportReport report = catalogue.import("title,ratign\nExcession,9\n");
    QVERIFY(report.aborted);
    QCOMPARE(report.failures.size(), std::size_t(1));
    QCOMPARE(report.failures[0].line, 1);
    QCOMPARE(catalogue.count("book"), 0);
}

void TestImport::fillsAWaitingMissingVolume()
{
    // AV-007: a volume recorded as missing takes the book.
    Catalogue catalogue;
    catalogue.import(
        "title,authors,series,position\n"
        "Excession,Iain M. Banks,The Culture,5\n");
    catalogue.connection.exec(
        "INSERT INTO series_entry (series_id, book_id, position, sort_position, title) "
        "VALUES (1, NULL, '1', 1, 'Consider Phlebas')");
    QCOMPARE(scalarText(catalogue.connection, "SELECT status FROM v_series_status"),
        std::string("Incomplete"));

    const ImportReport report = catalogue.import(
        "title,authors,series,position\n"
        "Consider Phlebas,Iain M. Banks,The Culture,1\n");
    QCOMPARE(report.inserted, 1);
    QCOMPARE(catalogue.count("series_entry"), 2);
    QCOMPARE(scalarText(catalogue.connection, "SELECT status FROM v_series_status"),
        std::string("Complete"));
}

void TestImport::changedRowsUpdateInPlace()
{
    Catalogue catalogue;
    catalogue.import(
        "title,authors,shelf,times_read,rating\n"
        "Surface Detail,Iain M. Banks,unread,0,\n");

    const ImportReport report = catalogue.import(
        "title,authors,shelf,times_read,rating\n"
        "surface detail,Iain M. Banks,read,3,8\n"); // matched without regard to case
    QVERIFY2(report.failures.empty(), qPrintable(failures(report)));
    QCOMPARE(report.updated, 1);
    QCOMPARE(catalogue.count("book"), 1);

    const auto book = BookRepository(catalogue.connection).find(1);
    QVERIFY(book->readStatus == ReadStatus::Read);
    // The re-read trigger would have made this 1; the file's count wins.
    QCOMPARE(book->timesRead, 3);
    QCOMPARE(book->rating, std::optional<int>(8));
}

void TestImport::explicitSortPositionWins()
{
    Catalogue catalogue;
    catalogue.import(
        "title,series,position,sort_position\n"
        "Fracture,Spinward Fringe,Broadcast 5,\n"
        "Freedom,Spinward Fringe,Broadcast 6.5,6.75\n");

    QCOMPARE(scalarText(catalogue.connection,
                 "SELECT group_concat(sort_position, ' ') FROM "
                 "(SELECT sort_position FROM series_entry ORDER BY id)"),
        std::string("5.0 6.75"));
}

void TestImport::failedRowDoesNotShadowTheNextBook()
{
    // Line 2 inserts its book, then breaks book_author's primary key with a
    // repeated credit and is rolled back; SQLite reuses the id for line 3's
    // book. Line 4 repeats line 3 and must be reported against line 3, not
    // against the row that no longer exists.
    Catalogue catalogue;
    const ImportReport report = catalogue.import(
        "title,authors\n"
        "Rolled Back,Somebody & Somebody\n"
        "Kept,Somebody Else\n"
        "Kept,Somebody Else\n");

    QCOMPARE(report.inserted, 1);
    QCOMPARE(catalogue.count("book"), 1);
    QCOMPARE(catalogue.count("book_author"), 1);
    QCOMPARE(report.failures.size(), std::size_t(2));
    QCOMPARE(report.failures[0].line, 2);
    QCOMPARE(report.failures[1].line, 4);
    QVERIFY2(report.failures[1].message.find("line 3") != std::string::npos,
        report.failures[1].message.c_str());
}

void TestImport::seedCatalogueImportsInOnePass()
{
    // Phase 1 acceptance, against the owner's real catalogue. seed/ is never
    // committed, so elsewhere this skips.
    const QString seed = QStringLiteral(PINAX_SEED_CSV);
    if (!QFileInfo::exists(seed))
        QSKIP("seed/library.csv not present");

    Catalogue catalogue;
    CsvImporter importer(catalogue.connection);
    const ImportReport first = importer.importFile(seed.toStdString());
    QVERIFY2(first.failures.empty(), qPrintable(failures(first)));
    QCOMPARE(first.inserted, 443);
    QCOMPARE(scalar(catalogue.connection, "SELECT COUNT(*) FROM book WHERE read_status = 'read'"), 176);
    QCOMPARE(scalar(catalogue.connection,
                 "SELECT COUNT(*) FROM book WHERE read_status = 'read' AND times_read < 1"),
        0);
    QCOMPARE(catalogue.count("series"), 144);

    const ImportReport second = importer.importFile(seed.toStdString());
    QCOMPARE(second.unchanged, 443);
    QCOMPARE(catalogue.count("book"), 443);
}

QTEST_APPLESS_MAIN(TestImport)
#include "test_import.moc"
