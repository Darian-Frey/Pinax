#include "db/connection.h"
#include "db/migrations.h"
#include "db/statement.h"
#include "io/csv_importer.h"
#include "io/series_importer.h"

#include <QFileInfo>
#include <QTest>

using pinax::db::Connection;
using pinax::db::Statement;
using pinax::io::CsvImporter;
using pinax::io::ImportReport;
using pinax::io::SeriesImporter;

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

QString failures(const ImportReport& report)
{
    QStringList lines;
    for (const auto& failure : report.failures)
        lines << QStringLiteral("%1: %2").arg(failure.line).arg(QString::fromStdString(failure.message));
    return lines.join(QStringLiteral("; "));
}

// The Culture with volumes 5 and 9 on the shelf, and a standalone.
struct Shelf {
    Connection connection { ":memory:" };
    Shelf()
    {
        pinax::db::migrate(connection);
        const auto report = CsvImporter(connection).importText(
            "title,authors,series,position\n"
            "Excession,Iain M. Banks,The Culture,5\n"
            "Surface Detail,Iain M. Banks,The Culture,9\n"
            "A Game of Thrones,George R. R. Martin,A Song of Ice and Fire,1\n");
        if (!report.failures.empty())
            qFatal("seed failed");
    }
    ImportReport series(std::string_view csv) { return SeriesImporter(connection).importText(csv); }
    std::string status(const std::string& name)
    {
        return scalarText(connection, "SELECT status FROM v_series_status WHERE name = '" + name + "'");
    }
};

} // namespace

class TestSeriesImport : public QObject {
    Q_OBJECT

private slots:
    void recordsMissingVolumesAndOngoingSeries();
    void secondRunChangesNothing();
    void aVolumeOnTheShelfIsLeftAlone();
    void unpositionedVolumesMatchOnTitle();
    void aChangedTitleUpdatesTheMissingVolume();
    void seriesWithNothingOnTheShelfIsCreated();
    void badRowsAreReportedAndTheRestImported();
    void headerProblemsStopTheRun();
    void seedSeriesMatchTheSpreadsheet();
};

void TestSeriesImport::recordsMissingVolumesAndOngoingSeries()
{
    Shelf shelf;
    QCOMPARE(shelf.status("The Culture"), std::string("Complete")); // nothing known missing yet

    const ImportReport report = shelf.series(
        "series,position,title,ongoing\n"
        "The Culture,1,Consider Phlebas,\n"
        "The Culture,6.5,,\n"
        "A Song of Ice and Fire,,,yes\n");
    QVERIFY2(report.failures.empty(), qPrintable(failures(report)));
    QCOMPARE(report.inserted, 2);
    QCOMPARE(report.updated, 1);

    QCOMPARE(shelf.status("The Culture"), std::string("Incomplete"));
    QCOMPARE(shelf.status("A Song of Ice and Fire"), std::string("Complete to date"));
    QCOMPARE(scalar(shelf.connection,
                 "SELECT sort_position FROM series_entry WHERE position = '6.5'"), 6);
    QCOMPARE(scalarText(shelf.connection,
                 "SELECT group_concat(title, '|') FROM v_missing_entries WHERE series_name = 'The Culture'"),
        std::string("Consider Phlebas"));
}

void TestSeriesImport::secondRunChangesNothing()
{
    const char* csv =
        "series,position,title,ongoing,notes\n"
        "The Culture,1,Consider Phlebas,,\n"
        "The Culture,,Unidentified volume 1,,From the spreadsheet: later volumes\n"
        "A Song of Ice and Fire,,,yes,\n";
    Shelf shelf;
    shelf.series(csv);
    const auto entries = scalar(shelf.connection, "SELECT COUNT(*) FROM series_entry");

    const ImportReport again = shelf.series(csv);
    QCOMPARE(again.inserted, 0);
    QCOMPARE(again.updated, 0);
    QCOMPARE(again.unchanged, 3);
    QCOMPARE(scalar(shelf.connection, "SELECT COUNT(*) FROM series_entry"), entries);
}

void TestSeriesImport::aVolumeOnTheShelfIsLeftAlone()
{
    // AV-007: the file says 5 is missing, but Excession is on the shelf.
    Shelf shelf;
    const ImportReport report = shelf.series("series,position,title\nThe Culture,5,Excession\n");
    QCOMPARE(report.unchanged, 1);
    QCOMPARE(scalar(shelf.connection, "SELECT COUNT(*) FROM series_entry WHERE position = '5'"), 1);
    QCOMPARE(scalar(shelf.connection,
                 "SELECT COUNT(*) FROM series_entry WHERE position = '5' AND book_id IS NOT NULL"), 1);
    QCOMPARE(shelf.status("The Culture"), std::string("Complete"));
}

void TestSeriesImport::unpositionedVolumesMatchOnTitle()
{
    Shelf shelf;
    shelf.series("series,title\nThe Culture,The State of the Art\n");
    // Matched without regard to case, so no second volume; the file's
    // spelling is then written, as the file is the authority.
    const ImportReport again = shelf.series("series,title\nThe Culture,the state of the art\n");
    QCOMPARE(again.inserted, 0);
    QCOMPARE(again.updated, 1);
    QCOMPARE(scalar(shelf.connection, "SELECT COUNT(*) FROM series_entry WHERE book_id IS NULL"), 1);
}

void TestSeriesImport::aChangedTitleUpdatesTheMissingVolume()
{
    Shelf shelf;
    shelf.series("series,position,title\nThe Culture,1,Consider Phlebas\n");
    const ImportReport report = shelf.series(
        "series,position,title,notes\nThe Culture,1,Consider Phlebas (1987),first published by Macmillan\n");
    QCOMPARE(report.updated, 1);
    QCOMPARE(scalarText(shelf.connection, "SELECT title || ' / ' || notes FROM series_entry WHERE position = '1' AND book_id IS NULL"),
        std::string("Consider Phlebas (1987) / first published by Macmillan"));
}

void TestSeriesImport::seriesWithNothingOnTheShelfIsCreated()
{
    Shelf shelf;
    shelf.series("series,position,title\nThe Expanse,1,Leviathan Wakes\n");
    QCOMPARE(scalarText(shelf.connection,
                 "SELECT held || '/' || known || ' ' || status FROM v_series_status WHERE name = 'The Expanse'"),
        std::string("0/1 Incomplete"));
}

void TestSeriesImport::badRowsAreReportedAndTheRestImported()
{
    Shelf shelf;
    const ImportReport report = shelf.series(
        "series,position,title,ongoing,notes\n"     // 1
        "The Culture,1,Consider Phlebas,,\n"         // 2 fine
        ",2,The Player of Games,,\n"                 // 3 no series
        "The Culture,,,,\n"                          // 4 nothing to record
        "The Culture,,,maybe,\n"                     // 5 bad ongoing
        "The Culture,,,,a stray note\n"              // 6 note with no volume
        "A Song of Ice and Fire,,,yes,\n"            // 7 fine
        "A Song of Ice and Fire,,,no,\n");           // 8 contradicts 7

    std::vector<int> lines;
    for (const auto& failure : report.failures)
        lines.push_back(failure.line);
    QCOMPARE(lines, std::vector<int>({3, 4, 5, 6, 8}));
    QVERIFY(report.failures.back().message.find("line 7") != std::string::npos);
    QCOMPARE(report.inserted, 1);
    QCOMPARE(shelf.status("A Song of Ice and Fire"), std::string("Complete to date"));
}

void TestSeriesImport::headerProblemsStopTheRun()
{
    Shelf shelf;
    QVERIFY(shelf.series("series,positon\nThe Culture,1\n").aborted);
    QVERIFY(shelf.series("position,title\n1,Consider Phlebas\n").aborted);
    QCOMPARE(scalar(shelf.connection, "SELECT COUNT(*) FROM series_entry WHERE book_id IS NULL"), 0);
}

void TestSeriesImport::seedSeriesMatchTheSpreadsheet()
{
    // Phase 2 step 1 against the owner's real catalogue, where present: the
    // statuses the spreadsheet recorded by hand, now derived (D-004).
    const QString library = QStringLiteral(PINAX_SEED_DIR "/library.csv");
    const QString series = QStringLiteral(PINAX_SEED_DIR "/series.csv");
    if (!QFileInfo::exists(library) || !QFileInfo::exists(series))
        QSKIP("seed/library.csv and seed/series.csv not present");

    Connection connection(":memory:");
    pinax::db::migrate(connection);
    QVERIFY(CsvImporter(connection).importFile(library.toStdString()).failures.empty());
    const ImportReport first = SeriesImporter(connection).importFile(series.toStdString());
    QVERIFY2(first.failures.empty(), qPrintable(failures(first)));

    auto count = [&](const char* status) {
        return scalar(connection,
            std::string("SELECT COUNT(*) FROM v_series_status WHERE status = '") + status + "'");
    };
    QCOMPARE(count("Complete") + count("Complete to date"), 51);
    QCOMPARE(count("Incomplete"), 93);
    QCOMPARE(scalarText(connection,
                 "SELECT held || '/' || known FROM v_series_status WHERE name = 'Discworld'"),
        std::string("27/41"));

    const ImportReport second = SeriesImporter(connection).importFile(series.toStdString());
    QCOMPARE(second.inserted + second.updated, 0);
}

QTEST_APPLESS_MAIN(TestSeriesImport)
#include "test_series_import.moc"
