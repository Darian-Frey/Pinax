#include "app/main_window.h"
#include "db/book_repository.h"
#include "db/connection.h"
#include "db/db_error.h"
#include "db/migrations.h"
#include "io/csv_importer.h"
#include "ui/book_list_view.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QStandardPaths>
#include <QStatusBar>

#include <cstdio>
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

QString databasePath(const QCommandLineParser& parser)
{
    const QStringList arguments = parser.positionalArguments();
    if (!arguments.isEmpty())
        return arguments.first();

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
    parser.addPositionalArgument(QStringLiteral("database"),
        QStringLiteral("Catalogue file to open. Default: ~/.local/share/pinax/pinax.db"),
        QStringLiteral("[database]"));
    parser.process(app);

    pinax::app::MainWindow window;

    const QString path = databasePath(parser);
    std::unique_ptr<pinax::db::Connection> connection;
    try {
        connection = std::make_unique<pinax::db::Connection>(path.toStdString());
        pinax::db::migrate(*connection);
        QString importSummary;
        if (parser.isSet(importOption)) {
            const QString csvPath = parser.value(importOption);
            pinax::io::CsvImporter importer(*connection);
            importSummary = reportImport(csvPath, importer.importFile(csvPath.toStdString()));
        }

        pinax::db::BookRepository books(*connection);
        window.bookList()->setBooks(books.summaries());
        const qlonglong count = books.count();
        const QString volumes = count == 1
            ? QObject::tr("1 volume")
            : QObject::tr("%1 volumes").arg(count);
        window.statusBar()->showMessage(importSummary.isEmpty()
                ? volumes + QStringLiteral(" · ") + path
                : volumes + QStringLiteral(" · ") + importSummary);
    } catch (const pinax::db::DbError& error) {
        connection.reset();
        qCritical("pinax.db: %s", error.what());
        window.statusBar()->showMessage(
            QObject::tr("Could not open %1: %2").arg(path, QString::fromUtf8(error.what())));
    }

    window.show();
    return QApplication::exec();
}
