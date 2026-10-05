#include "app/main_window.h"
#include "db/book_repository.h"
#include "db/connection.h"
#include "db/db_error.h"
#include "db/migrations.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QStandardPaths>
#include <QStatusBar>

#include <memory>

namespace {

// ~/.local/share/pinax/pinax.db unless a path is given on the command line.
// Covers are cached beside it (SPEC.md §4).
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
        pinax::db::BookRepository books(*connection);
        const qlonglong count = books.count();
        const QString volumes = count == 1
            ? QObject::tr("1 volume")
            : QObject::tr("%1 volumes").arg(count);
        window.statusBar()->showMessage(volumes + QStringLiteral(" · ") + path);
    } catch (const pinax::db::DbError& error) {
        connection.reset();
        qCritical("pinax.db: %s", error.what());
        window.statusBar()->showMessage(
            QObject::tr("Could not open %1: %2").arg(path, QString::fromUtf8(error.what())));
    }

    window.show();
    return QApplication::exec();
}
