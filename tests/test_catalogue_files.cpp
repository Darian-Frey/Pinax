#include "app/catalogue.h"
#include "app/main_window.h"
#include "app/recent_catalogues.h"
#include "db/backup.h"
#include "db/connection.h"
#include "db/db_error.h"
#include "db/dump.h"
#include "db/migrations.h"
#include "ui/backup_view.h"
#include "ui/book_editor.h"
#include "ui/book_list_view.h"
#include "ui/detail_panel.h"
#include "ui/report_view.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include <deque>

using pinax::app::Catalogue;
using pinax::app::MainWindow;
using pinax::ui::DetailPanel;

namespace {

// A catalogue file with these titles in it.
QString makeCatalogue(const QTemporaryDir& dir, const QString& name, const QStringList& titles)
{
    const QString path = dir.filePath(name);
    Catalogue catalogue(path.toStdString());
    for (const QString& title : titles) {
        pinax::domain::BookEdit edit;
        edit.book.title = title.toStdString();
        catalogue.save(edit);
    }
    return path;
}

// A window with its settings, backups and file choices kept in a scratch
// folder: answers queued in order, as the owner would pick them.
struct Window {
    QTemporaryDir dir;
    QSettings settings {dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat};
    MainWindow window;
    std::deque<QString> answers;
    std::vector<MainWindow::FileRequest> asked;

    Window()
    {
        window.setSettings(&settings);
        window.setBackupFolder(dir.filePath(QStringLiteral("backups")));
        window.setFileChooser([this](const MainWindow::FileRequest& request) {
            asked.push_back(request);
            if (answers.empty())
                return QString();
            const QString answer = answers.front();
            answers.pop_front();
            return answer;
        });
        window.show();
    }
    QString reportHeading()
    {
        return window.detailPanel()->reportView()->findChild<QLabel*>(QStringLiteral("report.heading"))->text();
    }
    QString reportBody()
    {
        return window.detailPanel()->reportView()->findChild<QLabel*>(QStringLiteral("report.body"))->text();
    }
    int shown() { return window.bookList()->shownCount(); }
};

} // namespace

class TestCatalogueFiles : public QObject {
    Q_OBJECT

private slots:
    // db
    void inspectTellsACatalogueFromAnythingElse();
    void restoreFromReplacesTheCatalogue();
    void restoreDumpBuildsANewCatalogue();

    // F-026
    void menusHoldTheCommands();
    void newOpenAndCloseACatalogue();
    void notACatalogueIsNeverOpened();
    void recentCataloguesAreRemembered();
    void quittingWithAFormOpenIsRefused();

    // F-027, F-028
    void importingBooksFromCsv();
    void restoringFromABackup();
    void restoringWithNothingOpen();
    void importingAnSqlDump();
};

void TestCatalogueFiles::inspectTellsACatalogueFromAnythingElse()
{
    QTemporaryDir dir;
    QVERIFY(!pinax::db::inspect(dir.filePath(QStringLiteral("none.db")).toStdString()).exists);
    QFile empty(dir.filePath(QStringLiteral("empty.db")));
    QVERIFY(empty.open(QIODevice::WriteOnly));
    empty.close();
    QVERIFY(pinax::db::inspect(empty.fileName().toStdString()).empty);

    QFile text(dir.filePath(QStringLiteral("notes.db")));
    QVERIFY(text.open(QIODevice::WriteOnly));
    text.write("These are not the droids you are looking for, nor a database.\n");
    text.close();
    const auto notes = pinax::db::inspect(text.fileName().toStdString());
    QVERIFY(!notes.empty && !notes.catalogue);

    {
        pinax::db::Connection other(dir.filePath(QStringLiteral("other.db")).toStdString());
        other.exec("CREATE TABLE recipes (name TEXT)");
    }
    const auto other = pinax::db::inspect(dir.filePath(QStringLiteral("other.db")).toStdString());
    QVERIFY(!other.catalogue);
    QVERIFY(other.problem.find("not a Pinax catalogue") != std::string::npos);

    const auto mine = pinax::db::inspect(makeCatalogue(dir, QStringLiteral("mine.db"), {"Dune", "Emma"}).toStdString());
    QVERIFY(mine.catalogue);
    QCOMPARE(mine.version, pinax::db::latestSchemaVersion);
    QCOMPARE(mine.books, std::int64_t(2));
}

void TestCatalogueFiles::restoreFromReplacesTheCatalogue()
{
    QTemporaryDir dir;
    const QString target = makeCatalogue(dir, QStringLiteral("live.db"), {"Dune"});
    const QString source = makeCatalogue(dir, QStringLiteral("backup.db"), {"Emma", "Middlemarch"});
    const auto report = pinax::db::restoreFrom(source.toStdString(), target.toStdString());
    QCOMPARE(report.books, std::int64_t(2));
    QCOMPARE(Catalogue(target.toStdString()).count(), 2);
    QVERIFY(!QFile::exists(target + QStringLiteral(".partial")));

    // Not a catalogue: refused, the target as it was.
    QFile text(dir.filePath(QStringLiteral("notes.db")));
    QVERIFY(text.open(QIODevice::WriteOnly));
    text.write("plain text");
    text.close();
    QVERIFY_THROWS_EXCEPTION(pinax::db::DbError, pinax::db::restoreFrom(text.fileName().toStdString(), target.toStdString()));
    QCOMPARE(Catalogue(target.toStdString()).count(), 2);
}

void TestCatalogueFiles::restoreDumpBuildsANewCatalogue()
{
    QTemporaryDir dir;
    const QString source = makeCatalogue(dir, QStringLiteral("source.db"), {"Dune", "Emma", "Ubik"});
    const QString dump = dir.filePath(QStringLiteral("source.sql"));
    {
        Catalogue catalogue(source.toStdString());
        QVERIFY(!catalogue.dumpTo(dump.toStdString()).problem);
    }
    const QString target = dir.filePath(QStringLiteral("rebuilt.db"));
    const auto report = pinax::db::restoreDump(dump.toStdString(), target.toStdString());
    QCOMPARE(report.books, std::int64_t(3));
    QCOMPARE(Catalogue(target.toStdString()).count(), 3);

    // Never over a file that is there, nor from a dump of something else.
    QVERIFY_THROWS_EXCEPTION(pinax::db::DbError, pinax::db::restoreDump(dump.toStdString(), target.toStdString()));
    QFile other(dir.filePath(QStringLiteral("other.sql")));
    QVERIFY(other.open(QIODevice::WriteOnly));
    other.write("CREATE TABLE recipes (name TEXT); INSERT INTO recipes VALUES ('soup');");
    other.close();
    const QString nowhere = dir.filePath(QStringLiteral("nowhere.db"));
    QVERIFY_THROWS_EXCEPTION(pinax::db::DbError, pinax::db::restoreDump(other.fileName().toStdString(), nowhere.toStdString()));
    QVERIFY(!QFile::exists(nowhere));
    QVERIFY(!QFile::exists(nowhere + QStringLiteral(".partial")));
}

void TestCatalogueFiles::menusHoldTheCommands()
{
    Window w;
    QStringList menus;
    for (QAction* action : w.window.menuBar()->actions())
        menus << action->text().remove(QLatin1Char('&'));
    QCOMPARE(menus, QStringList({"File", "Books", "View", "Help"}));
    QCOMPARE(w.window.openCatalogueAction()->shortcut(), QKeySequence(QKeySequence::Open));
    QCOMPARE(w.window.closeCatalogueAction()->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_W));
    QCOMPARE(w.window.quitAction()->shortcut(), QKeySequence(QKeySequence::Quit));
    // No catalogue yet: only what makes or opens one.
    QVERIFY(w.window.newCatalogueAction()->isEnabled());
    QVERIFY(w.window.openCatalogueAction()->isEnabled());
    QVERIFY(!w.window.closeCatalogueAction()->isEnabled());
    QVERIFY(!w.window.importCsvAction()->isEnabled());
    QVERIFY(!w.window.backUpAction()->isEnabled());
}

void TestCatalogueFiles::newOpenAndCloseACatalogue()
{
    Window w;
    const QString fresh = w.dir.filePath(QStringLiteral("shelves/new-library"));
    w.answers.push_back(fresh); // the .db is added
    w.window.newCatalogueAction()->trigger();
    QCOMPARE(w.window.cataloguePath(), QFileInfo(fresh + QStringLiteral(".db")).absoluteFilePath());
    QCOMPARE(w.window.windowTitle(), QStringLiteral("new-library.db — Pinax"));
    QCOMPARE(w.shown(), 0);
    QVERIFY(w.window.closeCatalogueAction()->isEnabled());
    QVERIFY(w.asked.back().save);

    // New over a file that is there: refused.
    w.answers.push_back(fresh + QStringLiteral(".db"));
    w.window.newCatalogueAction()->trigger();
    QCOMPARE(w.reportHeading(), QStringLiteral("Not opened"));

    // Open another: its books, its name.
    const QString other = makeCatalogue(w.dir, QStringLiteral("other.db"), {"Dune", "Emma"});
    w.answers.push_back(other);
    w.window.openCatalogueAction()->trigger();
    QCOMPARE(w.window.windowTitle(), QStringLiteral("other.db — Pinax"));
    QCOMPARE(w.shown(), 2);

    // Close: nothing open, and the commands that need a catalogue wait.
    w.window.closeCatalogueAction()->trigger();
    QVERIFY(w.window.cataloguePath().isEmpty());
    QCOMPARE(w.window.windowTitle(), QStringLiteral("Pinax"));
    QCOMPARE(w.shown(), 0);
    QVERIFY(!w.window.closeCatalogueAction()->isEnabled());
    QVERIFY(!w.window.exportAction()->isEnabled());
    QVERIFY(w.window.openCatalogueAction()->isEnabled());

    // Cancelling the dialogue does nothing.
    w.window.openCatalogueAction()->trigger();
    QVERIFY(w.window.cataloguePath().isEmpty());
}

void TestCatalogueFiles::notACatalogueIsNeverOpened()
{
    Window w;
    const QString mine = makeCatalogue(w.dir, QStringLiteral("mine.db"), {"Dune"});
    QVERIFY(w.window.openCatalogue(mine));

    {
        pinax::db::Connection other(w.dir.filePath(QStringLiteral("recipes.db")).toStdString());
        other.exec("CREATE TABLE recipes (name TEXT)");
    }
    QVERIFY(!w.window.openCatalogue(w.dir.filePath(QStringLiteral("recipes.db"))));
    QCOMPARE(w.reportHeading(), QStringLiteral("Not opened"));
    QVERIFY(w.reportBody().contains(QStringLiteral("not a Pinax catalogue")));
    QCOMPARE(QFileInfo(w.window.cataloguePath()).fileName(), QStringLiteral("mine.db")); // still open
    // And the other database was not turned into a catalogue.
    QVERIFY(!pinax::db::inspect(w.dir.filePath(QStringLiteral("recipes.db")).toStdString()).catalogue);

    // From a newer Pinax: refused, untouched.
    const QString newer = makeCatalogue(w.dir, QStringLiteral("newer.db"), {"Dune"});
    {
        pinax::db::Connection connection(newer.toStdString());
        connection.exec("INSERT INTO schema_version (version, note) VALUES (99, 'from the future')");
    }
    QVERIFY(!w.window.openCatalogue(newer));
    QVERIFY(w.reportBody().contains(QStringLiteral("newer Pinax")));
    QCOMPARE(pinax::db::inspect(newer.toStdString()).version, 99);
}

void TestCatalogueFiles::recentCataloguesAreRemembered()
{
    Window w;
    const QString first = makeCatalogue(w.dir, QStringLiteral("first.db"), {});
    const QString second = makeCatalogue(w.dir, QStringLiteral("second.db"), {});
    w.window.openCatalogue(first);
    w.window.openCatalogue(second);
    pinax::app::RecentCatalogues recent(w.settings);
    QCOMPARE(recent.last(), QFileInfo(second).canonicalFilePath());
    QCOMPARE(recent.list().size(), 2);

    // Open Recent lists them, the open one greyed.
    emit w.window.recentMenu()->aboutToShow();
    const auto actions = w.window.recentMenu()->actions();
    QCOMPARE(actions.size(), 2);
    QCOMPARE(actions.at(0)->text(), QStringLiteral("second.db"));
    QVERIFY(!actions.at(0)->isEnabled());
    actions.at(1)->trigger();
    QCOMPARE(QFileInfo(w.window.cataloguePath()).fileName(), QStringLiteral("first.db"));
    QCOMPARE(recent.last(), QFileInfo(first).canonicalFilePath());

    // A catalogue since deleted drops off the list.
    w.window.closeCatalogue();
    QFile::remove(second);
    QCOMPARE(recent.list(), QStringList({QFileInfo(first).canonicalFilePath()}));
}

void TestCatalogueFiles::quittingWithAFormOpenIsRefused()
{
    Window w;
    w.window.openCatalogue(makeCatalogue(w.dir, QStringLiteral("mine.db"), {"Dune"}));
    w.window.detailPanel()->beginNew();
    QCloseEvent close;
    QApplication::sendEvent(&w.window, &close);
    QVERIFY(!close.isAccepted());
    QVERIFY(w.window.isVisible());
    // Cancelled, the window may go.
    QTest::mouseClick(w.window.detailPanel()->editor()->findChild<QPushButton*>(QStringLiteral("edit.cancel")),
        Qt::LeftButton);
    QCloseEvent again;
    QApplication::sendEvent(&w.window, &again);
    QVERIFY(again.isAccepted());
}

void TestCatalogueFiles::importingBooksFromCsv()
{
    Window w;
    w.window.openCatalogue(makeCatalogue(w.dir, QStringLiteral("mine.db"), {"Dune"}));
    const QString csv = w.dir.filePath(QStringLiteral("more.csv"));
    QFile file(csv);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("title,authors,shelf\nEmma,Jane Austen,read\nUbik,Philip K. Dick,unread\n,No Title,read\n");
    file.close();
    w.answers.push_back(csv);
    w.window.importCsvAction()->trigger();

    QCOMPARE(w.window.detailPanel()->state(), DetailPanel::State::Reporting);
    QCOMPARE(w.reportHeading(), QStringLiteral("Imported more.csv"));
    QVERIFY(w.reportBody().contains(QStringLiteral("2 new, 0 updated, 0 unchanged, 1 failed")));
    QVERIFY(w.reportBody().contains(QStringLiteral("Line 4:")));
    QCOMPARE(w.shown(), 3);
    // Backed up first, before anything was merged.
    const QStringList safety = QDir(w.dir.filePath(QStringLiteral("backups"))).entryList({QStringLiteral("pinax-before-import-*.db")});
    QCOMPARE(safety.size(), 1);
    QCOMPARE(Catalogue(QDir(w.dir.filePath(QStringLiteral("backups"))).filePath(safety.front()).toStdString()).count(), 1);
    // A report is not a form: the list stays free.
    QVERIFY(w.window.bookList()->isEnabled());
}

void TestCatalogueFiles::restoringFromABackup()
{
    Window w;
    const QString live = makeCatalogue(w.dir, QStringLiteral("live.db"), {"Dune"});
    const QString backup = makeCatalogue(w.dir, QStringLiteral("older.db"), {"Emma", "Middlemarch", "Ubik"});
    w.window.openCatalogue(live);
    w.answers.push_back(backup);
    w.window.restoreAction()->trigger();

    QCOMPARE(w.reportHeading(), QStringLiteral("Catalogue restored"));
    QCOMPARE(QFileInfo(w.window.cataloguePath()).fileName(), QStringLiteral("live.db")); // the same file
    QCOMPARE(w.shown(), 3);
    const QStringList safety = QDir(w.dir.filePath(QStringLiteral("backups"))).entryList({QStringLiteral("pinax-before-restore-*.db")});
    QCOMPARE(safety.size(), 1);
    QCOMPARE(Catalogue(QDir(w.dir.filePath(QStringLiteral("backups"))).filePath(safety.front()).toStdString()).count(), 1);

    // Not a backup: refused, nothing replaced.
    QFile text(w.dir.filePath(QStringLiteral("notes.db")));
    QVERIFY(text.open(QIODevice::WriteOnly));
    text.write("plain text");
    text.close();
    w.answers.push_back(text.fileName());
    w.window.restoreAction()->trigger();
    QCOMPARE(w.reportHeading(), QStringLiteral("Not restored"));
    QCOMPARE(w.shown(), 3);
}

void TestCatalogueFiles::restoringWithNothingOpen()
{
    // BUG-006, as the owner did it: back up, close, then restore.
    Window w;
    const QString live = makeCatalogue(w.dir, QStringLiteral("live.db"), {"Dune", "Emma"});
    QVERIFY(w.window.openCatalogue(live));
    const QString backup = w.dir.filePath(QStringLiteral("saved.db"));
    w.window.backUpAction()->trigger();
    w.window.detailPanel()->backupView()->setPath(backup);
    QTest::mouseClick(w.window.detailPanel()->backupView()->findChild<QPushButton*>(QStringLiteral("backup.go")),
        Qt::LeftButton);
    QVERIFY(QFile::exists(backup));
    QTest::mouseClick(w.window.detailPanel()->backupView()->findChild<QPushButton*>(QStringLiteral("backup.close")),
        Qt::LeftButton);
    w.window.closeCatalogueAction()->trigger();
    QVERIFY(w.window.cataloguePath().isEmpty());
    // A book lost since, as it might be.
    {
        Catalogue changed(live.toStdString());
        changed.remove({changed.summaries().front().id});
    }

    QVERIFY(w.window.restoreAction()->isEnabled()); // nothing open, still offered
    w.answers.push_back(backup);
    w.answers.push_back(live); // into the catalogue last open, as suggested
    w.window.restoreAction()->trigger();
    QCOMPARE(w.asked.back().start, QFileInfo(live).canonicalFilePath());
    QCOMPARE(w.reportHeading(), QStringLiteral("Catalogue restored"));
    QCOMPARE(QFileInfo(w.window.cataloguePath()).fileName(), QStringLiteral("live.db"));
    QCOMPARE(w.shown(), 2);
    // The catalogue as it was — one book — was kept first.
    const QStringList safety = QDir(w.dir.filePath(QStringLiteral("backups"))).entryList({QStringLiteral("pinax-before-restore-*.db")});
    QCOMPARE(safety.size(), 1);
    QCOMPARE(Catalogue(QDir(w.dir.filePath(QStringLiteral("backups"))).filePath(safety.front()).toStdString()).count(), 1);

    // Into a new file: nothing to keep first, and it opens.
    w.window.closeCatalogue();
    w.answers.push_back(backup);
    w.answers.push_back(w.dir.filePath(QStringLiteral("fresh/copy")));
    w.window.restoreAction()->trigger();
    QCOMPARE(QFileInfo(w.window.cataloguePath()).fileName(), QStringLiteral("copy.db"));
    QCOMPARE(w.shown(), 2);

    // Never over a file that is not a catalogue.
    w.window.closeCatalogue();
    QFile text(w.dir.filePath(QStringLiteral("notes.db")));
    QVERIFY(text.open(QIODevice::WriteOnly));
    text.write("plain text");
    text.close();
    w.answers.push_back(backup);
    w.answers.push_back(text.fileName());
    w.window.restoreAction()->trigger();
    QCOMPARE(w.reportHeading(), QStringLiteral("Not restored"));
    QVERIFY(text.open(QIODevice::ReadOnly));
    QCOMPARE(text.readAll(), QByteArray("plain text"));
}

void TestCatalogueFiles::importingAnSqlDump()
{
    Window w;
    const QString source = makeCatalogue(w.dir, QStringLiteral("source.db"), {"Dune", "Emma"});
    const QString dump = w.dir.filePath(QStringLiteral("library.sql"));
    {
        Catalogue catalogue(source.toStdString());
        QVERIFY(!catalogue.dumpTo(dump.toStdString()).problem);
    }
    w.answers.push_back(dump);
    w.answers.push_back(w.dir.filePath(QStringLiteral("rebuilt")));
    w.window.importDumpAction()->trigger();
    QCOMPARE(w.asked.back().start, QFileInfo(dump).absoluteDir().filePath(QStringLiteral("library.db")));
    QCOMPARE(w.reportHeading(), QStringLiteral("Catalogue restored"));
    QCOMPARE(QFileInfo(w.window.cataloguePath()).fileName(), QStringLiteral("rebuilt.db"));
    QCOMPARE(w.shown(), 2);
}

QTEST_MAIN(TestCatalogueFiles)
#include "test_catalogue_files.moc"
