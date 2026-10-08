#include "app/catalogue.h"
#include "app/enricher.h"
#include "app/main_window.h"
#include "app/provider_key.h"
#include "app/recent_catalogues.h"
#include "db/backup.h"
#include "db/migrations.h"
#include "db/db_error.h"
#include "io/csv_importer.h"
#include "io/series_importer.h"
#include "metadata/http.h"
#include "ui/detail_panel.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>

#include <cstdio>
#include <iostream>
#include <memory>

namespace {

// ~/.local/share/pinax/pinax.db unless a path is given on the command line.
// Covers are cached beside it (SPEC.md §4).
// Prints the report the way a compiler prints errors, and returns the line
// for the status bar.
QString reportImport(const QString& csvPath, const pinax::io::ImportReport& report)
{
    const QByteArray file = csvPath.toLocal8Bit();
    for (const auto& failure : report.failures) {
        if (failure.line > 0)
            std::fprintf(stderr, "%s:%d: %s\n", file.constData(), failure.line, failure.message.c_str());
        else
            std::fprintf(stderr, "%s: %s\n", file.constData(), failure.message.c_str());
    }

    QString summary;
    if (report.aborted) {
        summary = QObject::tr("Import of %1 failed; nothing was written").arg(csvPath);
    } else {
        summary = QObject::tr("Imported %1: %2 new, %3 updated, %4 unchanged, %5 failed")
                      .arg(csvPath)
                      .arg(report.inserted)
                      .arg(report.updated)
                      .arg(report.unchanged)
                      .arg(report.failures.size());
    }
    std::printf("%s\n", summary.toLocal8Bit().constData());
    std::fflush(stdout);
    return summary;
}

// The catalogue named on the command line; else the one last open in the
// window (F-026, D-028); else ~/.local/share/pinax/pinax.db.
QString databasePath(const QCommandLineParser& parser)
{
    const QStringList arguments = parser.positionalArguments();
    if (!arguments.isEmpty())
        return arguments.first();
    QSettings settings;
    if (const QString last = pinax::app::RecentCatalogues(settings).last(); !last.isEmpty())
        return last;

    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/pinax");
    QDir().mkpath(dataDir);
    return dataDir + QStringLiteral("/pinax.db");
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Pinax"));
    QApplication::setOrganizationName(QStringLiteral("Pinax"));
    QApplication::setApplicationVersion(QStringLiteral(PINAX_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Catalogue for a personal physical library"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption importOption(QStringLiteral("import"),
        QStringLiteral("Import a CSV file (SPEC.md §1) before opening. Safe to repeat."),
        QStringLiteral("file.csv"));
    parser.addOption(importOption);
    const QCommandLineOption importSeriesOption(QStringLiteral("import-series"),
        QStringLiteral("Import known volumes and ongoing flags (SPEC.md §1.6) before opening, "
                       "after --import. Safe to repeat."),
        QStringLiteral("series.csv"));
    parser.addOption(importSeriesOption);
    const QCommandLineOption backupOption(QStringLiteral("backup"),
        QStringLiteral("Write a checked copy of the catalogue to this file (F-020), then quit "
                       "without opening the window. Exit status 0 on success."),
        QStringLiteral("file.db"));
    parser.addOption(backupOption);
    const QCommandLineOption dumpOption(QStringLiteral("dump"),
        QStringLiteral("Write the catalogue as plain SQL (F-021) to this file, checked by restoring "
                       "it, or to standard output for -; then quit without opening the window."),
        QStringLiteral("file.sql"));
    parser.addOption(dumpOption);
    const QCommandLineOption xlsxOption(QStringLiteral("xlsx"),
        QStringLiteral("Write the catalogue as an Excel workbook (F-022) to this file, then quit "
                       "without opening the window."),
        QStringLiteral("file.xlsx"));
    parser.addOption(xlsxOption);
    const QCommandLineOption csvOption(QStringLiteral("csv"),
        QStringLiteral("Write every book as CSV in the import format (F-023), checked by importing it, "
                       "then quit without opening the window."),
        QStringLiteral("file.csv"));
    parser.addOption(csvOption);
    parser.addPositionalArgument(QStringLiteral("database"),
        QStringLiteral("Catalogue file to open. Default: ~/.local/share/pinax/pinax.db"),
        QStringLiteral("[database]"));
    parser.process(app);

    const QString path = databasePath(parser);

    // Opened here for the command-line tasks; the window then takes it.
    std::unique_ptr<pinax::app::Catalogue> catalogue;
    QString status;
    try {
        // Never migrate a database that is not a catalogue into one, nor
        // one from a newer Pinax (F-026).
        const auto found = pinax::db::inspect(path.toStdString());
        if (!found.empty && !found.catalogue)
            throw pinax::db::DbError("this is not a Pinax catalogue: " + found.problem, 26);
        if (found.catalogue && found.version > pinax::db::latestSchemaVersion)
            throw pinax::db::DbError("it was made by a newer Pinax", 26);
        catalogue = std::make_unique<pinax::app::Catalogue>(path.toStdString());

        QString importSummary;
        if (parser.isSet(importOption)) {
            const QString csvPath = parser.value(importOption);
            pinax::io::CsvImporter importer(catalogue->connection());
            importSummary = reportImport(csvPath, importer.importFile(csvPath.toStdString()));
        }
        if (parser.isSet(importSeriesOption)) {
            const QString csvPath = parser.value(importSeriesOption);
            pinax::io::SeriesImporter importer(catalogue->connection());
            importSummary = reportImport(csvPath, importer.importFile(csvPath.toStdString()));
        }

        if (parser.isSet(backupOption)) {
            // For a scheduled job: no window, a line saying what happened.
            const auto result = catalogue->backupTo(parser.value(backupOption).toStdString());
            if (result.problem) {
                std::fprintf(stderr, "%s: not backed up: %s\n", result.path.c_str(), result.problem->c_str());
                return 1;
            }
            std::printf("Backed up %lld books to %s (checked)\n", static_cast<long long>(result.books),
                result.path.c_str());
            return 0;
        }

        if (parser.isSet(csvOption)) {
            const auto result = catalogue->exportCsv(catalogue->booksInListOrder(), parser.value(csvOption).toStdString());
            if (result.problem) {
                std::fprintf(stderr, "%s: not written: %s\n", result.path.c_str(), result.problem->c_str());
                return 1;
            }
            std::printf("Wrote %lld books as CSV to %s (re-imported and checked)\n",
                static_cast<long long>(result.books), result.path.c_str());
            return 0;
        }
        if (parser.isSet(xlsxOption)) {
            const auto result = catalogue->exportWorkbook(parser.value(xlsxOption).toStdString());
            if (result.problem) {
                std::fprintf(stderr, "%s: not written: %s\n", result.path.c_str(), result.problem->c_str());
                return 1;
            }
            std::printf("Wrote %lld books as an Excel workbook to %s\n", static_cast<long long>(result.books),
                result.path.c_str());
            return 0;
        }
        if (parser.isSet(dumpOption)) {
            const QString target = parser.value(dumpOption);
            if (target == QStringLiteral("-")) {
                catalogue->writeDump(std::cout);
                return std::cout ? 0 : 1;
            }
            const auto result = catalogue->dumpTo(target.toStdString());
            if (result.problem) {
                std::fprintf(stderr, "%s: not written: %s\n", result.path.c_str(), result.problem->c_str());
                return 1;
            }
            std::printf("Wrote %lld books as SQL to %s (restored and checked)\n",
                static_cast<long long>(result.books), result.path.c_str());
            return 0;
        }

        const qlonglong count = catalogue->count();
        const QString volumes = count == 1
            ? QObject::tr("1 volume")
            : QObject::tr("%1 volumes").arg(count);
        status = volumes + QStringLiteral(" · ") + (importSummary.isEmpty() ? path : importSummary);
    } catch (const pinax::db::DbError& error) {
        catalogue.reset();
        // A task for the command line has nothing to do without its
        // catalogue: say so and stop, rather than open a window.
        for (const auto* task : {&backupOption, &dumpOption, &xlsxOption, &csvOption, &importOption, &importSeriesOption}) {
            if (parser.isSet(*task)) {
                std::fprintf(stderr, "%s: cannot open: %s\n", path.toLocal8Bit().constData(), error.what());
                return 1;
            }
        }
        qCritical("pinax.db: %s", error.what());
        status = QObject::tr("Could not open %1: %2").arg(path, QString::fromUtf8(error.what()));
    }

    // The Google Books key, if the owner has one (D-021): beside the
    // catalogue, or in the folder Pinax was started from.
    const QString googleKey = pinax::app::findGoogleBooksKey(
        {QFileInfo(path).absolutePath(), QDir::currentPath()});
    QNetworkAccessManager network;
    pinax::metadata::NetworkFetcher fetcher(network);
    pinax::app::Enricher enricher(fetcher, googleKey);

    pinax::app::MainWindow window;
    window.setEnricher(&enricher);
    if (catalogue) {
        window.adoptCatalogue(std::move(catalogue));
    } else {
        window.detailPanel()->showReport(QObject::tr("No catalogue open"),
            status + QStringLiteral("\n\n") + QObject::tr("File ▸ Open Catalogue or New Catalogue."), true);
    }
    window.statusBar()->showMessage(status);

    window.show();
    return QApplication::exec();
}
