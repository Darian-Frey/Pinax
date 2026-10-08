#include "app/catalogue.h"

#include <QDir>
#include <memory>
#include "db/connection.h"
#include "db/db_error.h"
#include "db/statement.h"
#include "db/genre_repository.h"
#include "domain/credit_text.h"
#include "app/main_window.h"
#include "io/csv_importer.h"
#include "io/series_importer.h"
#include "ui/book_editor.h"
#include "ui/book_list_model.h"
#include "ui/book_list_view.h"
#include "ui/book_view.h"
#include "ui/backup_view.h"
#include "ui/export_view.h"
#include "ui/filter_bar.h"
#include "ui/book_group_proxy.h"
#include "ui/detail_panel.h"
#include "ui/rail_view.h"
#include "ui/series_entry_model.h"
#include "ui/series_page.h"
#include "ui/attach_view.h"
#include "ui/entry_editor.h"
#include "ui/series_view.h"
#include "ui/missing_page.h"

#include <QAction>
#include <QApplication>
#include <QDate>
#include <QDateTime>
#include <QImage>
#include <QLabel>
#include <QTemporaryDir>
#include <QStatusBar>
#include <QToolButton>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QProcess>
#include <QSignalSpy>
#include <QTest>

using pinax::app::Catalogue;
using pinax::app::MainWindow;
using pinax::domain::ReadStatus;
using pinax::ui::DetailPanel;

namespace {

void seed(Catalogue& catalogue)
{
    const auto report = pinax::io::CsvImporter(catalogue.connection()).importText(
        "title,authors,series,position,shelf,isbn13\n"
        "Excession,Iain M. Banks,The Culture,5,read,\n"
        "Surface Detail,Iain M. Banks,The Culture,9,unread,9780316005388\n"
        "Tau Zero,Poul Anderson,,,unread,\n");
    QVERIFY(report.failures.empty());
}

std::int64_t idOf(Catalogue& catalogue, const std::string& title)
{
    for (const auto& summary : catalogue.summaries()) {
        if (summary.title == title)
            return summary.id;
    }
    return 0;
}

} // namespace

class TestCatalogue : public QObject {
    Q_OBJECT

private slots:
    void detailCarriesSeriesCompleteness();
    void saveWritesAndReportsInTheOwnersTerms();
    void movingIntoReadCountsARead();
    void selectingARowShowsTheBook();
    void savingFromThePanelUpdatesTheList();

    // F-005, F-006, D-017
    void toggleMarksAnUnreadBookRead();
    void toggleTwiceLeavesTheCountAlone();
    void toggleOnAReReadBookTakesBackOneRead();
    void toggleWithAnyUnreadMarksAllRead();
    // F-007
    void ratingSetsAndClearsEverySelectedBook();
    void ratingOutsideRangeIsRefused();
    // Through the window
    void pressingRInTheListTogglesAndRefreshes();
    void clickingASquareInThePanelRates();

    // F-001, F-002
    void addingABookWritesItAndItsCredits();
    void editingCreditsReplacesThem();
    void removingLeavesTheSeriesGap();
    void ctrlNAddsABookThroughTheWindow();
    void deleteKeyAsksThenDeletes();
    // F-006
    void readsColumnShowsAndSortsTheCount();
    // IMP-003
    void creditedAwayAuthorsAreRemoved();
    void deletingAnAuthorsLastBookRemovesThem();
    void correctedReimportLeavesNoStragglers();
    // IMP-002
    void listStandsStillWhileEditing();
    void listStandsStillWhileConfirmingADelete();
    // Phase 2 step 2: the rail
    void seriesAreFiledIgnoringALeadingArticle();
    void filtersSelectTheRightBooks();
    void choosingASeriesNarrowsAndOrdersTheList();
    void railCountsFollowChangesButTheListHoldsStill();
    void railStandsStillWhileEditing();
    void addingABookReturnsToAllBooks();
    // Phase 2 step 3: a series' own page
    void seriesRowsRunInSeriesOrder();
    void seriesDetailNamesAuthorsAndTotals();
    void aMissingVolumeSelectedShowsTheSeries();
    void keysInTheSeriesPageReachTheBooks();
    void deletingFromASeriesLeavesItsGap();
    // Phase 2 step 4: entries
    void entriesAreAddedEditedAndRemoved();
    void removingAnOwnedVolumeKeepsTheBook();
    void attachingFillsTheWaitingEntry();
    void attachingIsRefusedWhereItWouldDuplicate();
    void aNewBookForAMissingVolumeTakesItsEntry();
    void addVolumeThroughTheSeriesPage();
    void markOwnedWithABookAlreadyHeld();
    void markOwnedWithANewBook();
    // Phase 2 step 5: the shopping list
    void missingVolumesRunFewestNeededFirst();
    void theShoppingListActsInPlace();
    // Phase 3 step 3: covers
    void aCoverIsRecordedWithItsSource();
    void aHandSetCoverIsNeverReplaced();
    void deletingABookTakesItsCoverFile();
    void thePanelShowsTheCoverFile();

    // F-012, F-014, F-015, AV-001
    void enrichingWritesTheCandidateAndItsGenres();
    void enrichingNeverOverwritesTheOwnersWork();
    void aLookupThatFindsNothingChangesNothing();
    // IMP-008
    void openingTidiesLendingTagsAwayButNotTheOwners();

    // F-024, D-012
    void addingFillsTheMissingVolumeItMatches();

    // F-017
    void filtersCombineWithTheRail();
    // F-018
    void groupingThroughTheWindow();

    // BUG-005
    void aBooksSeriesAreEditedWithTheBook();
    void theFormPlacesABookInASeries();

    // F-019
    void searchNarrowsAsYouType();

    // F-020
    void backingUpFromThePanel();

    // F-021
    void exportingAnSqlDump();

    // F-022, AV-011
    void theWorkbookAgreesWithTheViews();
    void exportingAnExcelWorkbook();
    // F-023
    void exportingTheListAsCsv();
    void aSeriesTheProviderNamesIsProposed();
    void anIsbnHeldIsNotAddedTwice();
    void aHeldCopyWithoutAnIsbnIsRecognised();
};

void TestCatalogue::detailCarriesSeriesCompleteness()
{
    Catalogue catalogue(":memory:");
    seed(catalogue);
    catalogue.connection().exec(
        "INSERT INTO series_entry (series_id, book_id, position, sort_position, title) "
        "VALUES (1, NULL, '1', 1, 'Consider Phlebas')");

    const auto detail = catalogue.detail(idOf(catalogue, "Excession"));
    QVERIFY(detail);
    QCOMPARE(detail->authors, std::optional<std::string>("Iain M. Banks"));
    QCOMPARE(detail->series.size(), std::size_t(1));
    const auto& culture = detail->series.front();
    QCOMPARE(culture.name, std::string("The Culture"));
    QCOMPARE(culture.position, std::optional<std::string>("5"));
    QCOMPARE(culture.held, 2);
    QCOMPARE(culture.known, 3);
    QCOMPARE(culture.heldRead, 1);
    QCOMPARE(culture.status, std::string("Incomplete"));
    QCOMPARE(culture.missing.size(), std::size_t(1));
    QCOMPARE(culture.missing.front().title, std::optional<std::string>("Consider Phlebas"));

    QVERIFY(catalogue.detail(idOf(catalogue, "Tau Zero"))->series.empty());
    QVERIFY(!catalogue.detail(999));
}

void TestCatalogue::saveWritesAndReportsInTheOwnersTerms()
{
    Catalogue catalogue(":memory:");
    seed(catalogue);

    auto book = catalogue.detail(idOf(catalogue, "Excession"))->book;
    book.rating = 9;
    book.publisher = "Orbit";
    QVERIFY(!catalogue.save(book));
    QCOMPARE(catalogue.detail(book.id)->book.rating, std::optional<int>(9));

    book.isbn13 = "9780316005388"; // Surface Detail's
    const auto problem = catalogue.save(book);
    QVERIFY(problem);
    QCOMPARE(*problem, std::string("Another book already has ISBN-13 9780316005388."));

    book.id = 999;
    book.isbn13.reset();
    QCOMPARE(catalogue.save(book), std::optional<std::string>("This book is no longer in the catalogue."));
}

void TestCatalogue::movingIntoReadCountsARead()
{
    // F-006 through the panel's path: the count follows the state.
    Catalogue catalogue(":memory:");
    seed(catalogue);

    auto book = catalogue.detail(idOf(catalogue, "Tau Zero"))->book;
    QCOMPARE(book.timesRead, 0);
    book.readStatus = ReadStatus::Read;
    QVERIFY(!catalogue.save(book));
    QCOMPARE(catalogue.detail(book.id)->book.timesRead, 1);
}

void TestCatalogue::selectingARowShowsTheBook()
{
    Catalogue catalogue(":memory:");
    seed(catalogue);

    MainWindow window;
    window.setCatalogue(&catalogue);
    QVERIFY(window.detailPanel()->state() == DetailPanel::State::Empty);

    window.bookList()->selectBook(idOf(catalogue, "Surface Detail"));
    QVERIFY(window.detailPanel()->state() == DetailPanel::State::Viewing);
    QCOMPARE(window.detailPanel()->view()->findChild<QLabel*>(QStringLiteral("title"))->text(),
        QStringLiteral("Surface Detail"));

    window.bookList()->selectAll();
    QVERIFY(window.detailPanel()->state() == DetailPanel::State::Several);

    window.bookList()->clearSelection();
    QVERIFY(window.detailPanel()->state() == DetailPanel::State::Empty);
}

void TestCatalogue::savingFromThePanelUpdatesTheList()
{
    Catalogue catalogue(":memory:");
    seed(catalogue);

    MainWindow window;
    window.setCatalogue(&catalogue);
    const std::int64_t id = idOf(catalogue, "Tau Zero");
    window.bookList()->selectBook(id);

    const auto detail = catalogue.detail(id);
    auto book = detail->book;
    book.rating = 7;
    emit window.detailPanel()->saveRequested({book, detail->credits});

    QVERIFY(window.detailPanel()->state() == DetailPanel::State::Viewing);
    QCOMPARE(window.detailPanel()->view()->findChild<QLabel*>(QStringLiteral("ratingText"))->text(),
        QStringLiteral("7 / 10"));
    // The list row followed, and is still the one selected.
    QCOMPARE(window.bookList()->selectedBooks(), QList<qint64>({id}));
    const auto* model = window.bookList()->model();
    bool found = false;
    for (int row = 0; row < model->rowCount(); ++row) {
        if (model->index(row, 1).data().toString() == QStringLiteral("Tau Zero")) {
            QCOMPARE(model->index(row, 4).data().toString(), QStringLiteral("7"));
            found = true;
        }
    }
    QVERIFY(found);
}

void TestCatalogue::toggleMarksAnUnreadBookRead()
{
    Catalogue catalogue(":memory:");
    seed(catalogue);
    const std::int64_t id = idOf(catalogue, "Tau Zero");

    QVERIFY(catalogue.toggleRead({id}) == ReadStatus::Read);
    const auto book = catalogue.detail(id)->book;
    QVERIFY(book.readStatus == ReadStatus::Read);
    QCOMPARE(book.timesRead, 1);
    QVERIFY(book.dateFinished);
}

void TestCatalogue::toggleTwiceLeavesTheCountAlone()
{
    // D-017: a slip of the key is not a re-read.
    Catalogue catalogue(":memory:");
    seed(catalogue);
    const std::int64_t id = idOf(catalogue, "Tau Zero");

    catalogue.toggleRead({id});
    QVERIFY(catalogue.toggleRead({id}) == ReadStatus::Unread);
    auto book = catalogue.detail(id)->book;
    QVERIFY(book.readStatus == ReadStatus::Unread);
    QCOMPARE(book.timesRead, 0);
    QVERIFY(!book.dateFinished);

    catalogue.toggleRead({id});
    QCOMPARE(catalogue.detail(id)->book.timesRead, 1);
}

void TestCatalogue::toggleOnAReReadBookTakesBackOneRead()
{
    Catalogue catalogue(":memory:");
    seed(catalogue);
    const std::int64_t id = idOf(catalogue, "Excession");

    // Read once by import; a real re-read is read -> reading -> read.
    auto book = catalogue.detail(id)->book;
    book.readStatus = ReadStatus::Reading;
    catalogue.save(book);
    book = catalogue.detail(id)->book;
    book.readStatus = ReadStatus::Read;
    catalogue.save(book);
    QCOMPARE(catalogue.detail(id)->book.timesRead, 2);

    catalogue.toggleRead({id});
    book = catalogue.detail(id)->book;
    QVERIFY(book.readStatus == ReadStatus::Unread);
    QCOMPARE(book.timesRead, 1);
    QVERIFY(book.dateFinished); // still read once before; that date stands
}

void TestCatalogue::toggleWithAnyUnreadMarksAllRead()
{
    Catalogue catalogue(":memory:");
    seed(catalogue);
    const std::int64_t read = idOf(catalogue, "Excession");
    const std::int64_t unread = idOf(catalogue, "Surface Detail");

    QVERIFY(catalogue.toggleRead({read, unread}) == ReadStatus::Read);
    // The one already read was left alone, not counted again.
    QCOMPARE(catalogue.detail(read)->book.timesRead, 1);
    QCOMPARE(catalogue.detail(unread)->book.timesRead, 1);

    QVERIFY(catalogue.toggleRead({read, unread}) == ReadStatus::Unread);
    QVERIFY(catalogue.detail(read)->book.readStatus == ReadStatus::Unread);
    QVERIFY(catalogue.detail(unread)->book.readStatus == ReadStatus::Unread);
}

void TestCatalogue::ratingSetsAndClearsEverySelectedBook()
{
    Catalogue catalogue(":memory:");
    seed(catalogue);
    const std::int64_t a = idOf(catalogue, "Excession");
    const std::int64_t b = idOf(catalogue, "Tau Zero");

    catalogue.setRating({a, b}, 8);
    QCOMPARE(catalogue.detail(a)->book.rating, std::optional<int>(8));
    QCOMPARE(catalogue.detail(b)->book.rating, std::optional<int>(8));

    catalogue.setRating({b}, std::nullopt);
    QCOMPARE(catalogue.detail(a)->book.rating, std::optional<int>(8));
    QVERIFY(!catalogue.detail(b)->book.rating);
}

void TestCatalogue::ratingOutsideRangeIsRefused()
{
    // F-007: rejected, not clamped; the whole call rolls back.
    Catalogue catalogue(":memory:");
    seed(catalogue);
    const std::int64_t a = idOf(catalogue, "Excession");
    const std::int64_t b = idOf(catalogue, "Tau Zero");

    QVERIFY_THROWS_EXCEPTION(pinax::db::DbError, catalogue.setRating({a, b}, 11));
    QVERIFY(!catalogue.detail(a)->book.rating);
    QVERIFY(!catalogue.detail(b)->book.rating);
}

void TestCatalogue::pressingRInTheListTogglesAndRefreshes()
{
    Catalogue catalogue(":memory:");
    seed(catalogue);

    MainWindow window;
    window.setCatalogue(&catalogue);
    const std::int64_t id = idOf(catalogue, "Tau Zero");
    window.bookList()->selectBook(id);
    auto* readCount = window.detailPanel()->view()->findChild<QLabel*>(QStringLiteral("readCount"));
    QCOMPARE(readCount->text(), QStringLiteral("not read yet"));

    QTest::keyClick(window.bookList(), Qt::Key_R);

    QVERIFY(catalogue.detail(id)->book.readStatus == ReadStatus::Read);
    QCOMPARE(readCount->text(), QStringLiteral("read once"));
    QCOMPARE(window.bookList()->selectedBooks(), QList<qint64>({id}));

    QTest::keyClick(window.bookList(), Qt::Key_7);
    QCOMPARE(catalogue.detail(id)->book.rating, std::optional<int>(7));
    QTest::keyClick(window.bookList(), Qt::Key_Backspace);
    QVERIFY(!catalogue.detail(id)->book.rating);
}

void TestCatalogue::clickingASquareInThePanelRates()
{
    Catalogue catalogue(":memory:");
    seed(catalogue);

    MainWindow window;
    window.setCatalogue(&catalogue);
    const std::int64_t id = idOf(catalogue, "Excession");
    window.bookList()->selectBook(id);

    auto* bar = window.detailPanel()->view()->findChild<QWidget*>(QStringLiteral("ratingBar"));
    // The sixth square: five squares and five gaps of 11px in, plus a little.
    QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, QPoint(5 * 11 + 4, 5));
    QCOMPARE(catalogue.detail(id)->book.rating, std::optional<int>(6));

    // The same square again clears it.
    QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, QPoint(5 * 11 + 4, 5));
    QVERIFY(!catalogue.detail(id)->book.rating);
}

void TestCatalogue::addingABookWritesItAndItsCredits()
{
    Catalogue catalogue(":memory:");
    seed(catalogue);

    pinax::domain::BookEdit edit;
    edit.book.title = "The Mote in God's Eye";
    edit.book.readStatus = ReadStatus::Read;
    edit.credits = {{"Larry Niven", pinax::domain::CreditRole::Author},
        {"Jerry Pournelle", pinax::domain::CreditRole::Author}};
    const auto result = catalogue.save(edit);
    QVERIFY(!result.problem);
    QVERIFY(result.id > 0);

    const auto detail = catalogue.detail(result.id);
    QCOMPARE(detail->book.sortTitle, std::string("Mote in God's Eye, The"));
    // AV-005: created straight into read, so the count is set, not triggered.
    QCOMPARE(detail->book.timesRead, 1);
    QCOMPARE(detail->authors, std::optional<std::string>("Larry Niven & Jerry Pournelle"));
    QCOMPARE(catalogue.count(), 4);
}

void TestCatalogue::editingCreditsReplacesThem()
{
    Catalogue catalogue(":memory:");
    seed(catalogue);
    const std::int64_t id = idOf(catalogue, "Excession");

    auto detail = catalogue.detail(id);
    pinax::domain::BookEdit edit {detail->book,
        pinax::domain::parseCredits("Iain M. Banks & Ken MacLeod (editor)")};
    QVERIFY(!catalogue.save(edit).problem);

    detail = catalogue.detail(id);
    QCOMPARE(detail->credits.size(), std::size_t(2));
    QCOMPARE(pinax::domain::formatCredits(detail->credits),
        std::string("Iain M. Banks & Ken MacLeod (editor)"));
    // Banks is reused, not duplicated; editors are not listed as authors.
    QCOMPARE(detail->authors, std::optional<std::string>("Iain M. Banks"));
    QCOMPARE(catalogue.detail(idOf(catalogue, "Surface Detail"))->credits.front().name,
        std::string("Iain M. Banks"));
}

void TestCatalogue::removingLeavesTheSeriesGap()
{
    Catalogue catalogue(":memory:");
    seed(catalogue);
    const std::int64_t id = idOf(catalogue, "Excession");

    QVERIFY(!catalogue.remove({id}));
    QVERIFY(!catalogue.detail(id));
    QCOMPARE(catalogue.count(), 2);

    // The Culture now knows a volume 5 it does not hold (F-001, D-006).
    const auto culture = catalogue.detail(idOf(catalogue, "Surface Detail"))->series.front();
    QCOMPARE(culture.held, 1);
    QCOMPARE(culture.known, 2);
    QCOMPARE(culture.missing.front().position, std::optional<std::string>("5"));
}

void TestCatalogue::ctrlNAddsABookThroughTheWindow()
{
    Catalogue catalogue(":memory:");
    seed(catalogue);

    MainWindow window;
    window.setCatalogue(&catalogue);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.bookList()->selectBook(idOf(catalogue, "Tau Zero"));

    QTest::keyClick(&window, Qt::Key_N, Qt::ControlModifier);
    QVERIFY(window.detailPanel()->state() == DetailPanel::State::Editing);
    QVERIFY(window.bookList()->selectedBooks().isEmpty());

    auto* editor = window.detailPanel()->editor();
    editor->findChild<QLineEdit*>(QStringLiteral("edit.title"))->setText(QStringLiteral("Ringworld"));
    editor->findChild<QLineEdit*>(QStringLiteral("edit.authors"))->setText(QStringLiteral("Larry Niven"));
    editor->save();

    const std::int64_t id = idOf(catalogue, "Ringworld");
    QVERIFY(id > 0);
    QCOMPARE(window.bookList()->selectedBooks(), QList<qint64>({id}));
    QVERIFY(window.detailPanel()->state() == DetailPanel::State::Viewing);
    QCOMPARE(window.bookList()->model()->rowCount(), 4);
}

void TestCatalogue::deleteKeyAsksThenDeletes()
{
    Catalogue catalogue(":memory:");
    seed(catalogue);

    MainWindow window;
    window.setCatalogue(&catalogue);
    // Shown, so the confirmation's buttons have a size to click.
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    const std::int64_t id = idOf(catalogue, "Excession");
    window.bookList()->selectBook(id);

    QTest::keyClick(window.bookList(), Qt::Key_Delete);
    QVERIFY(window.detailPanel()->state() == DetailPanel::State::ConfirmingDelete);
    const QString question
        = window.detailPanel()->findChild<QLabel*>(QStringLiteral("confirm.question"))->text();
    QVERIFY2(question.contains(QStringLiteral("The Culture stays, as a missing volume")), qPrintable(question));
    QCOMPARE(catalogue.count(), 3); // nothing yet

    QTest::mouseClick(window.detailPanel()->findChild<QPushButton*>(QStringLiteral("confirm.delete")),
        Qt::LeftButton);
    QCOMPARE(catalogue.count(), 2);
    QVERIFY(!catalogue.detail(id));
    QCOMPARE(window.bookList()->model()->rowCount(), 2);
    QVERIFY(window.detailPanel()->state() == DetailPanel::State::Empty);
}

void TestCatalogue::readsColumnShowsAndSortsTheCount()
{
    Catalogue catalogue(":memory:");
    seed(catalogue); // Excession read once, the others not at all
    const std::int64_t twice = idOf(catalogue, "Surface Detail");
    auto book = catalogue.detail(twice)->book;
    book.readStatus = ReadStatus::Read;
    catalogue.save(book);
    book = catalogue.detail(twice)->book;
    book.readStatus = ReadStatus::Reading;
    catalogue.save(book);
    book = catalogue.detail(twice)->book;
    book.readStatus = ReadStatus::Read;
    catalogue.save(book);

    MainWindow window;
    window.setCatalogue(&catalogue);
    auto* view = window.bookList();
    view->sortByColumn(pinax::ui::BookListModel::TimesReadColumn, Qt::DescendingOrder);

    const auto* model = view->model();
    auto cell = [&](int row, int column) { return model->index(row, column).data().toString(); };
    QCOMPARE(cell(0, pinax::ui::BookListModel::TitleColumn), QStringLiteral("Surface Detail"));
    QCOMPARE(cell(0, pinax::ui::BookListModel::TimesReadColumn), QStringLiteral("2"));
    QCOMPARE(cell(1, pinax::ui::BookListModel::TimesReadColumn), QStringLiteral("1"));
    QCOMPARE(cell(2, pinax::ui::BookListModel::TimesReadColumn), QString()); // never read

    // F-016: marking it read recorded the day (UTC, as SQLite's date('now')),
    // and the Finished column sorts it ahead of the books never finished.
    view->sortByColumn(pinax::ui::BookListModel::FinishedColumn, Qt::DescendingOrder);
    QCOMPARE(cell(0, pinax::ui::BookListModel::TitleColumn), QStringLiteral("Surface Detail"));
    QCOMPARE(cell(0, pinax::ui::BookListModel::FinishedColumn),
        QDateTime::currentDateTimeUtc().date().toString(Qt::ISODate));
    QVERIFY(cell(2, pinax::ui::BookListModel::FinishedColumn).isEmpty());
}

namespace {

std::int64_t authorCount(Catalogue& catalogue, const std::string& name)
{
    pinax::db::Statement count(catalogue.connection(), "SELECT COUNT(*) FROM author WHERE name = :name");
    count.bind(":name", name);
    count.step();
    return count.columnInt(0);
}

} // namespace

void TestCatalogue::creditedAwayAuthorsAreRemoved()
{
    Catalogue catalogue(":memory:");
    seed(catalogue);
    catalogue.connection().exec("INSERT INTO author (name, sort_name, notes) "
                                "VALUES ('Kept On Purpose', 'Purpose, Kept On', 'a note')");

    const std::int64_t id = idOf(catalogue, "Tau Zero");
    const auto detail = catalogue.detail(id);
    QVERIFY(!catalogue.save({detail->book, pinax::domain::parseCredits("Poul William Anderson")}).problem);

    QCOMPARE(authorCount(catalogue, "Poul Anderson"), 0);       // credited by nothing now
    QCOMPARE(authorCount(catalogue, "Poul William Anderson"), 1);
    QCOMPARE(authorCount(catalogue, "Kept On Purpose"), 1);     // has notes
    QCOMPARE(authorCount(catalogue, "Iain M. Banks"), 1);       // untouched
}

void TestCatalogue::deletingAnAuthorsLastBookRemovesThem()
{
    Catalogue catalogue(":memory:");
    seed(catalogue);

    QVERIFY(!catalogue.remove({idOf(catalogue, "Excession")}));
    QCOMPARE(authorCount(catalogue, "Iain M. Banks"), 1); // Surface Detail still credits him

    QVERIFY(!catalogue.remove({idOf(catalogue, "Surface Detail"), idOf(catalogue, "Tau Zero")}));
    QCOMPARE(authorCount(catalogue, "Iain M. Banks"), 0);
    QCOMPARE(authorCount(catalogue, "Poul Anderson"), 0);
}

void TestCatalogue::correctedReimportLeavesNoStragglers()
{
    Catalogue catalogue(":memory:");
    pinax::io::CsvImporter importer(catalogue.connection());
    importer.importText("title,authors\nLost Mars,ed. Mike Ashley\n");
    // The same book with its credit corrected by hand, then a corrected file.
    const std::int64_t id = idOf(catalogue, "Lost Mars");
    QVERIFY(!catalogue.save({catalogue.detail(id)->book,
        pinax::domain::parseCredits("Mike Ashley (editor)")}).problem);
    const auto report = importer.importText("title,authors\nLost Mars,Mike Ashley (editor)\n");
    QCOMPARE(report.unchanged, 1);
    QCOMPARE(authorCount(catalogue, "ed. Mike Ashley"), 0);
    QCOMPARE(authorCount(catalogue, "Mike Ashley"), 1);
}

void TestCatalogue::listStandsStillWhileEditing()
{
    Catalogue catalogue(":memory:");
    seed(catalogue);

    MainWindow window;
    window.setCatalogue(&catalogue);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto* list = window.bookList();
    auto* add = window.findChild<QAction*>(QStringLiteral("addBook"));
    const std::int64_t id = idOf(catalogue, "Excession");
    list->selectBook(id);
    QVERIFY(list->isEnabled() && add->isEnabled());

    QTest::keyClick(&window, Qt::Key_F2);
    QVERIFY(window.detailPanel()->state() == DetailPanel::State::Editing);
    QVERIFY(!list->isEnabled());
    QVERIFY(!add->isEnabled());

    // A click on another row, and the toggle key, reach nothing.
    const QModelIndex other = list->model()->index(0, 1);
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
        list->visualRect(other).center());
    QTest::keyClick(list, Qt::Key_R);
    QCOMPARE(list->selectedBooks(), QList<qint64>({id}));
    QVERIFY(window.detailPanel()->state() == DetailPanel::State::Editing);
    QVERIFY(catalogue.detail(id)->book.readStatus == ReadStatus::Read);

    auto* title = window.detailPanel()->editor()->findChild<QLineEdit*>(QStringLiteral("edit.title"));
    QTest::keyClick(title, Qt::Key_Escape);
    QVERIFY(window.detailPanel()->state() == DetailPanel::State::Viewing);
    QVERIFY(list->isEnabled() && add->isEnabled());

    // Saving releases it too.
    QTest::keyClick(&window, Qt::Key_F2);
    QVERIFY(!list->isEnabled());
    window.detailPanel()->editor()->save();
    QVERIFY(list->isEnabled());
}

void TestCatalogue::listStandsStillWhileConfirmingADelete()
{
    Catalogue catalogue(":memory:");
    seed(catalogue);

    MainWindow window;
    window.setCatalogue(&catalogue);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.bookList()->selectBook(idOf(catalogue, "Tau Zero"));

    QTest::keyClick(window.bookList(), Qt::Key_Delete);
    QVERIFY(!window.bookList()->isEnabled());
    QTest::mouseClick(window.detailPanel()->findChild<QPushButton*>(QStringLiteral("confirm.keep")),
        Qt::LeftButton);
    QVERIFY(window.bookList()->isEnabled());
    QCOMPARE(catalogue.count(), 3);
}

namespace {

std::int64_t seriesId(Catalogue& catalogue, const std::string& name)
{
    for (const auto& series : catalogue.seriesStatuses()) {
        if (series.name == name)
            return series.id;
    }
    return 0;
}

pinax::domain::BookFilter seriesFilter(std::int64_t id)
{
    return {pinax::domain::BookFilter::Kind::Series, ReadStatus::Unread, id};
}

QStringList seriesTitles(const MainWindow& window)
{
    QStringList titles;
    const auto* model = window.seriesPage()->entryModel();
    for (int row = 0; row < model->rowCount(); ++row)
        titles << model->index(row, pinax::ui::SeriesEntryModel::TitleColumn).data().toString();
    return titles;
}

} // namespace

void TestCatalogue::seriesAreFiledIgnoringALeadingArticle()
{
    Catalogue catalogue(":memory:");
    pinax::io::CsvImporter(catalogue.connection()).importText(
        "title,series,position\nA,The Culture,1\nB,Agent Cormac,1\nC,Dune,1\n");

    std::vector<std::string> names;
    for (const auto& series : catalogue.seriesStatuses())
        names.push_back(series.name);
    QCOMPARE(names, std::vector<std::string>({"Agent Cormac", "The Culture", "Dune"}));
}

void TestCatalogue::filtersSelectTheRightBooks()
{
    Catalogue catalogue(":memory:");
    seed(catalogue);

    QVERIFY(!catalogue.bookIds({}));
    const auto read = catalogue.bookIds({pinax::domain::BookFilter::Kind::ReadState, ReadStatus::Read, 0});
    QCOMPARE(*read, std::vector<std::int64_t>({idOf(catalogue, "Excession")}));
    auto culture = *catalogue.bookIds(seriesFilter(seriesId(catalogue, "The Culture")));
    std::sort(culture.begin(), culture.end());
    std::vector<std::int64_t> expected {idOf(catalogue, "Excession"), idOf(catalogue, "Surface Detail")};
    std::sort(expected.begin(), expected.end());
    QCOMPARE(culture, expected);
    QCOMPARE(catalogue.countWithReadStatus(ReadStatus::Unread), 2);
}

void TestCatalogue::choosingASeriesNarrowsAndOrdersTheList()
{
    Catalogue catalogue(":memory:");
    pinax::io::CsvImporter(catalogue.connection()).importText(
        "title,authors,series,position\n"
        "Surface Detail,Iain M. Banks,The Culture,9\n"
        "Consider Phlebas,Iain M. Banks,The Culture,1\n"
        "Excession,Iain M. Banks,The Culture,5\n"
        "Tau Zero,Poul Anderson,,\n");

    MainWindow window;
    window.setCatalogue(&catalogue);
    window.rail()->chooseFilter(seriesFilter(seriesId(catalogue, "The Culture")));

    // A series has its own page, in series order.
    QVERIFY(window.showingSeries());
    QCOMPARE(seriesTitles(window), QStringList({"Consider Phlebas", "Excession", "Surface Detail"}));
    QCOMPARE(window.statusBar()->currentMessage(), QStringLiteral("The Culture"));

    window.rail()->chooseFilter({});
    QVERIFY(!window.showingSeries());
    QCOMPARE(window.bookList()->shownCount(), 4);
}

void TestCatalogue::railCountsFollowChangesButTheListHoldsStill()
{
    Catalogue catalogue(":memory:");
    seed(catalogue); // one read, two unread

    MainWindow window;
    window.setCatalogue(&catalogue);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    const auto unread = pinax::domain::BookFilter {pinax::domain::BookFilter::Kind::ReadState, ReadStatus::Unread, 0};
    window.rail()->chooseFilter(unread);
    QCOMPARE(window.bookList()->shownCount(), 2);

    const std::int64_t id = idOf(catalogue, "Tau Zero");
    window.bookList()->selectBook(id);
    QTest::keyClick(window.bookList(), Qt::Key_R);

    // Counted at once in the rail...
    const QAbstractItemModel* rail = window.rail()->model();
    const QModelIndex library = rail->index(0, 0);
    QCOMPARE(rail->index(1, 1, library).data().toString(), QStringLiteral("1")); // Unread
    QCOMPARE(rail->index(3, 1, library).data().toString(), QStringLiteral("2")); // Read
    // ...but the book stays where the owner is working, still selected.
    QCOMPARE(window.bookList()->shownCount(), 2);
    QCOMPARE(window.bookList()->selectedBooks(), QList<qint64>({id}));
    QVERIFY(window.rail()->currentFilter() == unread);
}

void TestCatalogue::railStandsStillWhileEditing()
{
    Catalogue catalogue(":memory:");
    seed(catalogue);

    MainWindow window;
    window.setCatalogue(&catalogue);
    window.bookList()->selectBook(idOf(catalogue, "Excession"));
    window.detailPanel()->beginEdit();
    QVERIFY(!window.rail()->isEnabled());
    emit window.detailPanel()->editor()->cancelled();
    QVERIFY(window.rail()->isEnabled());
}

void TestCatalogue::addingABookReturnsToAllBooks()
{
    Catalogue catalogue(":memory:");
    seed(catalogue);

    MainWindow window;
    window.setCatalogue(&catalogue);
    window.rail()->chooseFilter(seriesFilter(seriesId(catalogue, "The Culture")));
    QVERIFY(window.showingSeries());

    pinax::domain::BookEdit edit;
    edit.book.title = "Ringworld";
    emit window.detailPanel()->saveRequested(edit);

    QVERIFY(window.rail()->currentFilter().kind == pinax::domain::BookFilter::Kind::All);
    QVERIFY(!window.showingSeries());
    QCOMPARE(window.bookList()->shownCount(), 4);
    QCOMPARE(window.bookList()->selectedBooks(), QList<qint64>({idOf(catalogue, "Ringworld")}));
    const QModelIndex library = window.rail()->model()->index(0, 0);
    QCOMPARE(window.rail()->model()->index(0, 1, library).data().toString(), QStringLiteral("4"));
}

namespace {

// The Culture: 1 missing, 5 and 9 held, a placeholder with no position; and
// a standalone.
void seedCulture(Catalogue& catalogue)
{
    pinax::io::CsvImporter(catalogue.connection()).importText(
        "title,authors,series,position,shelf\n"
        "Surface Detail,Iain M. Banks,The Culture,9,unread\n"
        "Excession,Iain M. Banks,The Culture,5,read\n"
        "Tau Zero,Poul Anderson,,,unread\n");
    pinax::io::SeriesImporter(catalogue.connection()).importText(
        "series,position,title\n"
        "The Culture,1,Consider Phlebas\n"
        "The Culture,,Unidentified volume 1\n");
}

} // namespace

void TestCatalogue::seriesRowsRunInSeriesOrder()
{
    Catalogue catalogue(":memory:");
    seedCulture(catalogue);

    const auto rows = catalogue.seriesRows(seriesId(catalogue, "The Culture"));
    QCOMPARE(rows.size(), std::size_t(4));
    QCOMPARE(*rows[0].title(), std::string("Consider Phlebas"));
    QVERIFY(!rows[0].owned());
    QCOMPARE(*rows[1].title(), std::string("Excession"));
    QVERIFY(rows[1].readStatus == ReadStatus::Read);
    QCOMPARE(*rows[2].title(), std::string("Surface Detail"));
    QCOMPARE(*rows[3].title(), std::string("Unidentified volume 1")); // no position: last
}

void TestCatalogue::seriesDetailNamesAuthorsAndTotals()
{
    Catalogue catalogue(":memory:");
    seedCulture(catalogue);

    const auto detail = catalogue.seriesDetail(seriesId(catalogue, "The Culture"));
    QVERIFY(detail);
    QCOMPARE(detail->authors, std::optional<std::string>("Iain M. Banks"));
    QCOMPARE(detail->series.held, 2);
    QCOMPARE(detail->series.known, 4);
    QCOMPARE(detail->series.heldRead, 1);
    QCOMPARE(detail->missing.size(), std::size_t(2));
    QCOMPARE(detail->library.withGaps, 1);
    QCOMPARE(detail->library.oneVolumeShort, 0);
    QCOMPARE(detail->library.volumesNotOwned, 2);
    QVERIFY(!catalogue.seriesDetail(999));
}

void TestCatalogue::aMissingVolumeSelectedShowsTheSeries()
{
    Catalogue catalogue(":memory:");
    seedCulture(catalogue);

    MainWindow window;
    window.setCatalogue(&catalogue);
    window.rail()->chooseFilter(seriesFilter(seriesId(catalogue, "The Culture")));
    QVERIFY(window.detailPanel()->state() == DetailPanel::State::ViewingSeries);

    window.seriesPage()->selectBook(idOf(catalogue, "Excession"));
    QVERIFY(window.detailPanel()->state() == DetailPanel::State::Viewing);

    window.seriesPage()->table()->selectRow(0); // Consider Phlebas, not owned
    QVERIFY(window.detailPanel()->state() == DetailPanel::State::ViewingSeries);
}

void TestCatalogue::keysInTheSeriesPageReachTheBooks()
{
    Catalogue catalogue(":memory:");
    seedCulture(catalogue);

    MainWindow window;
    window.setCatalogue(&catalogue);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.rail()->chooseFilter(seriesFilter(seriesId(catalogue, "The Culture")));
    const std::int64_t id = idOf(catalogue, "Surface Detail");
    window.seriesPage()->selectBook(id);

    QTest::keyClick(window.seriesPage()->table(), Qt::Key_R);
    QTest::keyClick(window.seriesPage()->table(), Qt::Key_8);

    const auto book = catalogue.detail(id)->book;
    QVERIFY(book.readStatus == ReadStatus::Read);
    QCOMPARE(book.rating, std::optional<int>(8));
    // The page redrew in place, still on the same volume.
    QVERIFY(window.showingSeries());
    QCOMPARE(window.seriesPage()->selectedBooks(), QList<qint64>({id}));
    QCOMPARE(window.seriesPage()->entryModel()->index(2, pinax::ui::SeriesEntryModel::StateColumn)
                 .data().toString(),
        QStringLiteral("read"));
}

void TestCatalogue::deletingFromASeriesLeavesItsGap()
{
    Catalogue catalogue(":memory:");
    seedCulture(catalogue);

    MainWindow window;
    window.setCatalogue(&catalogue);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.rail()->chooseFilter(seriesFilter(seriesId(catalogue, "The Culture")));
    window.seriesPage()->selectBook(idOf(catalogue, "Excession"));

    QTest::keyClick(window.seriesPage()->table(), Qt::Key_Delete);
    QTest::mouseClick(window.detailPanel()->findChild<QPushButton*>(QStringLiteral("confirm.delete")),
        Qt::LeftButton);

    // Still four entries; Excession's is now a gap at 5 (F-001, D-006).
    const auto* model = window.seriesPage()->entryModel();
    QCOMPARE(model->rowCount(), 4);
    QCOMPARE(model->index(1, pinax::ui::SeriesEntryModel::StateColumn).data().toString(),
        QStringLiteral("not owned"));
    QVERIFY(window.detailPanel()->state() == DetailPanel::State::ViewingSeries);
}

namespace {

std::int64_t entryIdAt(Catalogue& catalogue, std::int64_t series, const std::string& position)
{
    for (const auto& row : catalogue.seriesRows(series)) {
        if (row.position == position)
            return row.entryId;
    }
    return 0;
}

} // namespace

void TestCatalogue::entriesAreAddedEditedAndRemoved()
{
    Catalogue catalogue(":memory:");
    seedCulture(catalogue);
    const std::int64_t culture = seriesId(catalogue, "The Culture");
    QCOMPARE(catalogue.nextSortPosition(culture), 10.0); // after Surface Detail's 9

    pinax::domain::SeriesEntry entry;
    entry.seriesId = culture;
    QCOMPARE(catalogue.saveEntry(entry), std::optional<std::string>("A volume needs a position or a title."));

    entry.position = "10";
    entry.sortPosition = 10;
    entry.title = "The Hydrogen Sonata";
    QVERIFY(!catalogue.saveEntry(entry));
    QCOMPARE(catalogue.seriesDetail(culture)->series.known, 5);

    auto stored = *catalogue.entry(entryIdAt(catalogue, culture, "10"));
    stored.title = "The Hydrogen Sonata (2012)";
    QVERIFY(!catalogue.saveEntry(stored));
    QCOMPARE(catalogue.entry(stored.id)->title, std::optional<std::string>("The Hydrogen Sonata (2012)"));

    QVERIFY(!catalogue.removeEntry(stored.id));
    QVERIFY(!catalogue.entry(stored.id));
    QCOMPARE(catalogue.removeEntry(stored.id), std::optional<std::string>("That volume is no longer in the series."));
}

void TestCatalogue::removingAnOwnedVolumeKeepsTheBook()
{
    Catalogue catalogue(":memory:");
    seedCulture(catalogue);
    const std::int64_t culture = seriesId(catalogue, "The Culture");

    QVERIFY(!catalogue.removeEntry(entryIdAt(catalogue, culture, "5")));
    const auto excession = catalogue.detail(idOf(catalogue, "Excession"));
    QVERIFY(excession);                  // still in the catalogue
    QVERIFY(excession->series.empty()); // out of the series
    QCOMPARE(catalogue.seriesDetail(culture)->series.known, 3);
}

void TestCatalogue::attachingFillsTheWaitingEntry()
{
    // AV-007: Consider Phlebas bought, already catalogued by hand, then
    // attached to the waiting volume 1.
    Catalogue catalogue(":memory:");
    seedCulture(catalogue);
    const std::int64_t culture = seriesId(catalogue, "The Culture");
    pinax::domain::BookEdit edit;
    edit.book.title = "Consider Phlebas";
    const std::int64_t book = catalogue.save(edit).id;

    const auto entriesBefore = catalogue.seriesRows(culture).size();
    QVERIFY(!catalogue.attach(entryIdAt(catalogue, culture, "1"), book));
    QCOMPARE(catalogue.seriesRows(culture).size(), entriesBefore);
    QCOMPARE(catalogue.seriesDetail(culture)->series.held, 3);
    QCOMPARE(catalogue.detail(book)->series.front().position, std::optional<std::string>("1"));
}

void TestCatalogue::attachingIsRefusedWhereItWouldDuplicate()
{
    Catalogue catalogue(":memory:");
    seedCulture(catalogue);
    const std::int64_t culture = seriesId(catalogue, "The Culture");
    const std::int64_t waiting = entryIdAt(catalogue, culture, "1");

    QCOMPARE(catalogue.attach(entryIdAt(catalogue, culture, "5"), idOf(catalogue, "Tau Zero")),
        std::optional<std::string>("That volume is already owned."));
    QCOMPARE(catalogue.attach(waiting, idOf(catalogue, "Excession")),
        std::optional<std::string>("That book is already in this series."));
    QVERIFY(!catalogue.entry(waiting)->bookId);
}

void TestCatalogue::aNewBookForAMissingVolumeTakesItsEntry()
{
    Catalogue catalogue(":memory:");
    seedCulture(catalogue);
    const std::int64_t culture = seriesId(catalogue, "The Culture");
    QCOMPARE(pinax::domain::formatCredits(catalogue.seriesCredits(culture)), std::string("Iain M. Banks"));

    pinax::domain::BookEdit edit;
    edit.book.title = "Consider Phlebas";
    edit.credits = catalogue.seriesCredits(culture);
    const auto entriesBefore = catalogue.seriesRows(culture).size();
    const auto result = catalogue.save(edit, entryIdAt(catalogue, culture, "1"));
    QVERIFY(!result.problem);

    QCOMPARE(catalogue.seriesRows(culture).size(), entriesBefore);
    QCOMPARE(catalogue.entry(entryIdAt(catalogue, culture, "1"))->bookId, std::optional<std::int64_t>(result.id));

    // An entry already owned refuses, and the new book is not left behind.
    const auto count = catalogue.count();
    edit.book.title = "Excession again";
    const auto refused = catalogue.save(edit, entryIdAt(catalogue, culture, "5"));
    QCOMPARE(refused.problem, std::optional<std::string>("That volume is already owned."));
    QCOMPARE(catalogue.count(), count);
}

void TestCatalogue::addVolumeThroughTheSeriesPage()
{
    Catalogue catalogue(":memory:");
    seedCulture(catalogue);

    MainWindow window;
    window.setCatalogue(&catalogue);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.rail()->chooseFilter(seriesFilter(seriesId(catalogue, "The Culture")));

    window.seriesPage()->findChild<QToolButton*>(QStringLiteral("series.addEntry"))->click();
    QVERIFY(window.detailPanel()->state() == DetailPanel::State::EditingEntry);
    QVERIFY(!window.seriesPage()->isEnabled()); // IMP-002
    auto* editor = window.detailPanel()->entryEditor();
    QCOMPARE(editor->findChild<QLineEdit*>(QStringLiteral("entry.sortPosition"))->text(), QStringLiteral("10"));
    editor->findChild<QLineEdit*>(QStringLiteral("entry.position"))->setText(QStringLiteral("10"));
    editor->findChild<QLineEdit*>(QStringLiteral("entry.title"))->setText(QStringLiteral("The Hydrogen Sonata"));
    editor->save();

    QVERIFY(window.seriesPage()->isEnabled());
    QCOMPARE(window.seriesPage()->entryModel()->rowCount(), 5);
    QCOMPARE(seriesTitles(window).at(3), QStringLiteral("The Hydrogen Sonata"));
}

void TestCatalogue::markOwnedWithABookAlreadyHeld()
{
    Catalogue catalogue(":memory:");
    seedCulture(catalogue);
    pinax::domain::BookEdit edit;
    edit.book.title = "Consider Phlebas";
    const std::int64_t book = catalogue.save(edit).id;

    MainWindow window;
    window.setCatalogue(&catalogue);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.rail()->chooseFilter(seriesFilter(seriesId(catalogue, "The Culture")));
    window.seriesPage()->table()->selectRow(0); // Consider Phlebas, missing

    window.detailPanel()->seriesView()->findChild<QPushButton*>(QStringLiteral("seriesView.markOwned"))->click();
    QVERIFY(window.detailPanel()->state() == DetailPanel::State::Attaching);
    window.detailPanel()->attachView()->findChild<QPushButton*>(QStringLiteral("attach.attach"))->click();

    QCOMPARE(catalogue.entry(entryIdAt(catalogue, seriesId(catalogue, "The Culture"), "1"))->bookId,
        std::optional<std::int64_t>(book));
    QCOMPARE(window.seriesPage()->selectedBooks(), QList<qint64>({book}));
    QVERIFY(window.detailPanel()->state() == DetailPanel::State::Viewing);
}

void TestCatalogue::markOwnedWithANewBook()
{
    Catalogue catalogue(":memory:");
    seedCulture(catalogue);
    const std::int64_t culture = seriesId(catalogue, "The Culture");

    MainWindow window;
    window.setCatalogue(&catalogue);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.rail()->chooseFilter(seriesFilter(culture));
    window.seriesPage()->table()->selectRow(0);
    window.detailPanel()->seriesView()->findChild<QPushButton*>(QStringLiteral("seriesView.markOwned"))->click();
    window.detailPanel()->attachView()->findChild<QPushButton*>(QStringLiteral("attach.create"))->click();

    // The book form, prefilled from the series.
    QVERIFY(window.detailPanel()->state() == DetailPanel::State::Editing);
    auto* editor = window.detailPanel()->editor();
    QCOMPARE(editor->findChild<QLineEdit*>(QStringLiteral("edit.title"))->text(), QStringLiteral("Consider Phlebas"));
    QCOMPARE(editor->findChild<QLineEdit*>(QStringLiteral("edit.authors"))->text(), QStringLiteral("Iain M. Banks"));
    editor->save();

    const std::int64_t book = idOf(catalogue, "Consider Phlebas");
    QVERIFY(book > 0);
    QCOMPARE(catalogue.entry(entryIdAt(catalogue, culture, "1"))->bookId, std::optional<std::int64_t>(book));
    QCOMPARE(catalogue.seriesRows(culture).size(), std::size_t(4)); // no second entry
    QVERIFY(window.showingSeries());
    QCOMPARE(window.seriesPage()->selectedBooks(), QList<qint64>({book}));
}

void TestCatalogue::missingVolumesRunFewestNeededFirst()
{
    // The Culture needs 2 (Consider Phlebas, a placeholder); Agent Cormac 1.
    Catalogue catalogue(":memory:");
    seedCulture(catalogue);
    pinax::io::CsvImporter(catalogue.connection()).importText(
        "title,series,position\nGridlinked,Agent Cormac,1\n");
    pinax::io::SeriesImporter(catalogue.connection()).importText(
        "series,position,title\nAgent Cormac,2,The Line of Polity\n");

    const auto all = catalogue.missingVolumes();
    QCOMPARE(all.size(), std::size_t(3));
    QCOMPARE(*all[0].title, std::string("The Line of Polity"));
    QCOMPARE(all[0].missingInSeries, 1);
    QCOMPARE(*all[1].title, std::string("Consider Phlebas"));   // numbered first
    QCOMPARE(*all[2].title, std::string("Unidentified volume 1")); // unnumbered last
    QVERIFY(all[1].entryId > 0);

    const auto one = catalogue.missingVolumes(true);
    QCOMPARE(one.size(), std::size_t(1));
    QCOMPARE(one.front().seriesName, std::string("Agent Cormac"));
    QCOMPARE(catalogue.libraryTotals().oneVolumeShort, 1);
}

void TestCatalogue::theShoppingListActsInPlace()
{
    Catalogue catalogue(":memory:");
    seedCulture(catalogue);
    pinax::domain::BookEdit edit;
    edit.book.title = "Consider Phlebas";
    const std::int64_t book = catalogue.save(edit).id;

    MainWindow window;
    window.setCatalogue(&catalogue);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.rail()->chooseFilter({pinax::domain::BookFilter::Kind::MissingVolumes, ReadStatus::Unread, 0});
    QVERIFY(window.showingMissing());
    QCOMPARE(window.missingPage()->missingModel()->rowCount(), 2);

    // Selecting a volume shows its series with the volume's card.
    window.missingPage()->table()->selectRow(0);
    QVERIFY(window.detailPanel()->state() == DetailPanel::State::ViewingSeries);
    window.detailPanel()->seriesView()->findChild<QPushButton*>(QStringLiteral("seriesView.markOwned"))->click();
    QVERIFY(window.detailPanel()->state() == DetailPanel::State::Attaching);
    window.detailPanel()->attachView()->findChild<QPushButton*>(QStringLiteral("attach.attach"))->click();

    // Owned now: off the list, the list still showing, the rail recounted.
    QVERIFY(window.showingMissing());
    QCOMPARE(window.missingPage()->missingModel()->rowCount(), 1);
    QCOMPARE(catalogue.detail(book)->series.size(), std::size_t(1));
    const QModelIndex attention = window.rail()->model()->index(2, 0);
    QCOMPARE(window.rail()->model()->index(1, 1, attention).data().toString(), QStringLiteral("1"));

    // Opening the remaining volume's series goes to its own page.
    emit window.missingPage()->table()->activated(window.missingPage()->missingModel()->index(0, 0));
    QVERIFY(window.showingSeries());
    QCOMPARE(window.rail()->currentFilter().seriesId, seriesId(catalogue, "The Culture"));
}

namespace {

// A catalogue in a temporary folder, so covers have somewhere to live, with a
// small real image written as book 1's cover.
struct OnDisk {
    QTemporaryDir dir;
    std::unique_ptr<Catalogue> catalogue;
    std::int64_t book = 0;

    OnDisk()
    {
        catalogue = std::make_unique<Catalogue>(dir.filePath(QStringLiteral("pinax.db")).toStdString());
        pinax::domain::BookEdit edit;
        edit.book.title = "Consider Phlebas";
        book = catalogue->save(edit).id;
        QDir(dir.path()).mkpath(QStringLiteral("covers"));
        QImage image(60, 90, QImage::Format_RGB32);
        image.fill(QColor(0xD9, 0xA4, 0x41));
        image.save(dir.filePath(QStringLiteral("covers/%1.png").arg(book)));
    }
    QString coverPath() const { return QStringLiteral("covers/%1.png").arg(book); }
};

} // namespace

void TestCatalogue::aCoverIsRecordedWithItsSource()
{
    OnDisk disk;
    Catalogue& catalogue = *disk.catalogue;
    QCOMPARE(catalogue.dataDirectory(), std::optional<std::string>(QDir(disk.dir.path()).absolutePath().toStdString()));
    QVERIFY(!Catalogue(":memory:").dataDirectory());

    QVERIFY(!catalogue.setCover(disk.book, disk.coverPath().toStdString(), pinax::domain::Source::OpenLibrary));
    const auto detail = catalogue.detail(disk.book);
    QCOMPARE(detail->book.coverPath, std::optional<std::string>(disk.coverPath().toStdString()));
    QVERIFY(detail->book.coverSource == pinax::domain::Source::OpenLibrary);
    QVERIFY(detail->coverFile); // resolved beside the database (SPEC.md §4)
}

void TestCatalogue::aHandSetCoverIsNeverReplaced()
{
    // AV-001.
    OnDisk disk;
    Catalogue& catalogue = *disk.catalogue;
    QVERIFY(!catalogue.setCover(disk.book, "covers/mine.png", pinax::domain::Source::Manual));
    QCOMPARE(catalogue.setCover(disk.book, disk.coverPath().toStdString(), pinax::domain::Source::OpenLibrary),
        std::optional<std::string>("The cover was set by hand and is kept."));
    QCOMPARE(catalogue.detail(disk.book)->book.coverPath, std::optional<std::string>("covers/mine.png"));
}

void TestCatalogue::deletingABookTakesItsCoverFile()
{
    OnDisk disk;
    Catalogue& catalogue = *disk.catalogue;
    catalogue.setCover(disk.book, disk.coverPath().toStdString(), pinax::domain::Source::OpenLibrary);
    QVERIFY(QFile::exists(disk.dir.filePath(disk.coverPath())));

    QVERIFY(!catalogue.remove({disk.book}));
    QVERIFY(!QFile::exists(disk.dir.filePath(disk.coverPath())));
}

void TestCatalogue::thePanelShowsTheCoverFile()
{
    OnDisk disk;
    Catalogue& catalogue = *disk.catalogue;
    catalogue.setCover(disk.book, disk.coverPath().toStdString(), pinax::domain::Source::OpenLibrary);

    MainWindow window;
    window.setCatalogue(&catalogue);
    window.bookList()->selectBook(disk.book);
    const auto* cover = window.detailPanel()->view()->findChild<QLabel*>(QStringLiteral("cover"));
    QVERIFY(!cover->pixmap().isNull());
    QVERIFY(cover->text().isEmpty()); // no "NO COVER YET"

    // A cover file gone missing falls back to the placeholder (F-013).
    QFile::remove(disk.dir.filePath(disk.coverPath()));
    window.bookList()->clearSelection();
    window.bookList()->selectBook(disk.book);
    QVERIFY(cover->pixmap().isNull());
    QVERIFY(cover->text().contains(QStringLiteral("NO COVER YET")));
}

namespace {

pinax::domain::Candidate phlebas()
{
    pinax::domain::Candidate candidate;
    candidate.source = pinax::domain::Source::OpenLibrary;
    candidate.providerKey = "/books/OL9759601M";
    candidate.title = "Consider Phlebas";
    candidate.publisher = "Orbit";
    candidate.firstPublishedYear = 1987;
    candidate.pageCount = 480;
    candidate.description = "The war raged across the galaxy.";
    candidate.categories = {"Science Fiction", "Space opera"};
    candidate.coverUrl = "https://covers.openlibrary.org/b/id/1174792-L.jpg";
    return candidate;
}

} // namespace

void TestCatalogue::enrichingWritesTheCandidateAndItsGenres()
{
    Catalogue catalogue(":memory:");
    pinax::domain::BookEdit edit;
    edit.book.title = "Consider Phlebas";
    const auto id = catalogue.save(edit).id;

    const auto result = catalogue.enrich(id, phlebas(), true);
    QVERIFY(!result.problem);
    QVERIFY(result.coverUrl == phlebas().coverUrl);

    const auto detail = catalogue.detail(id);
    QVERIFY(detail->book.synopsis == phlebas().description);
    QVERIFY(detail->book.synopsisSource == pinax::domain::Source::OpenLibrary);
    QVERIFY(detail->book.publisher == std::optional<std::string>("Orbit"));
    QVERIFY(detail->book.pageCount == 480);
    QVERIFY(detail->book.metadataStatus == pinax::domain::MetadataStatus::Matched);
    QVERIFY(detail->book.metadataFetchedAt);
    QCOMPARE(detail->genres, (std::vector<std::string> {"Science Fiction", "Space opera"}));

    pinax::db::GenreRepository genres(catalogue.connection());
    QVERIFY(genres.forBook(id).front().source == pinax::domain::Source::OpenLibrary);

    // A genre from the provider that filled the gaps is recorded as its own
    // (schema version 5) — and "Science fiction" is the "Science Fiction"
    // already there, whatever its capitals (IMP-009): one link, as it was.
    auto filled = phlebas();
    filled.filledFrom = pinax::domain::Source::BritishLibrary;
    filled.filledCategories = {"Science fiction", "Space colonies"};
    QVERIFY(!catalogue.enrich(id, filled, true).problem);
    const auto links = genres.forBook(id);
    QCOMPARE(links.size(), std::size_t(3));
    QVERIFY(std::find(links.begin(), links.end(),
                pinax::db::GenreLink {"Science Fiction", pinax::domain::Source::OpenLibrary})
        != links.end());
    QVERIFY(std::find(links.begin(), links.end(),
                pinax::db::GenreLink {"Space colonies", pinax::domain::Source::BritishLibrary})
        != links.end());

    // Fetching again duplicates nothing.
    QVERIFY(!catalogue.enrich(id, phlebas(), true).problem);
    QCOMPARE(catalogue.detail(id)->genres.size(), std::size_t(3));
}

void TestCatalogue::enrichingNeverOverwritesTheOwnersWork()
{
    // AV-001, end to end: everything the owner entered survives a fetch with
    // an answer for every field, and their genre stays theirs.
    OnDisk disk;
    Catalogue& catalogue = *disk.catalogue;
    auto book = catalogue.detail(disk.book)->book;
    book.synopsis = "My own words.";
    book.synopsisSource = pinax::domain::Source::Manual;
    book.publisher = "Macmillan";
    book.publishedYear = 1987;
    book.pageCount = 471;
    book.isbn13 = "9780333447055";
    book.editionNote = "First edition";
    book.notes = "Signed.";
    QVERIFY(!catalogue.save(book));
    QVERIFY(!catalogue.setCover(disk.book, "covers/mine.png", pinax::domain::Source::Manual));
    pinax::db::GenreRepository genres(catalogue.connection());
    genres.addToBook(disk.book, "Space opera", pinax::domain::Source::Manual);
    const auto before = catalogue.detail(disk.book)->book;

    const auto result = catalogue.enrich(disk.book, phlebas(), true);
    QVERIFY(!result.problem);
    QVERIFY(!result.coverUrl); // the owner's cover is not even asked for

    auto after = catalogue.detail(disk.book)->book;
    QVERIFY(after.metadataStatus == pinax::domain::MetadataStatus::Matched);
    after.metadataStatus = before.metadataStatus;
    after.metadataFetchedAt = before.metadataFetchedAt;
    after.updatedAt = before.updatedAt;
    QVERIFY(after == before);

    // And a cover fetched regardless is refused.
    QVERIFY(catalogue.setCover(disk.book, disk.coverPath().toStdString(), pinax::domain::Source::OpenLibrary));

    QCOMPARE(genres.forBook(disk.book),
        (std::vector<pinax::db::GenreLink> {{"Science Fiction", pinax::domain::Source::OpenLibrary},
            {"Space opera", pinax::domain::Source::Manual}}));
}

void TestCatalogue::aLookupThatFindsNothingChangesNothing()
{
    Catalogue catalogue(":memory:");
    seed(catalogue);
    const auto id = idOf(catalogue, "Tau Zero");
    auto before = catalogue.detail(id)->book;

    catalogue.markLookupFailed(id);
    auto after = catalogue.detail(id)->book;
    QVERIFY(after.metadataStatus == pinax::domain::MetadataStatus::Failed);
    QVERIFY(after.metadataFetchedAt);
    after.metadataStatus = before.metadataStatus;
    after.metadataFetchedAt = before.metadataFetchedAt;
    after.updatedAt = before.updatedAt;
    QVERIFY(after == before);
}

namespace {

// The Culture with Excession and Surface Detail held and Consider Phlebas
// known but missing: two of three.
void seedCultureWithAGap(Catalogue& catalogue)
{
    pinax::io::CsvImporter(catalogue.connection()).importText(
        "title,authors,series,position,shelf\n"
        "Surface Detail,Iain M. Banks,The Culture,9,unread\n"
        "Excession,Iain M. Banks,The Culture,5,read\n");
    pinax::io::SeriesImporter(catalogue.connection()).importText(
        "series,position,title\n"
        "The Culture,1,Consider Phlebas\n");
}

pinax::domain::Candidate phlebasByIsbn()
{
    auto candidate = phlebas();
    candidate.authors = {"Iain Banks"};
    candidate.isbn13 = "9780316005388";
    return candidate;
}

} // namespace

void TestCatalogue::addingFillsTheMissingVolumeItMatches()
{
    using pinax::domain::SeriesProposal;
    Catalogue catalogue(":memory:");
    seedCultureWithAGap(catalogue);

    // The provider's spelling meets the catalogue's.
    const auto credits = catalogue.creditsFor({"Iain Banks"});
    QCOMPARE(credits.size(), std::size_t(1));
    QCOMPARE(credits.front().name, std::string("Iain M. Banks"));

    const auto proposals = catalogue.seriesProposals("Consider Phlebas", credits, phlebasByIsbn());
    QCOMPARE(proposals.size(), std::size_t(1));
    QVERIFY(proposals.front().kind == SeriesProposal::Kind::FillsMissing);
    QCOMPARE(proposals.front().seriesName, std::string("The Culture"));
    QCOMPARE(proposals.front().heldAfter, 3);
    QCOMPARE(proposals.front().knownAfter, 3); // complete once added
    // Another author's book of the same name fills nothing.
    QVERIFY(catalogue.seriesProposals("Consider Phlebas", catalogue.creditsFor({"Someone Else"}), phlebasByIsbn())
                .empty());

    Catalogue::NewBook book;
    book.edit.book.title = "Consider Phlebas";
    book.edit.book.isbn13 = "9780316005388";
    book.edit.book.readStatus = ReadStatus::Read;
    book.edit.credits = credits;
    book.candidate = phlebasByIsbn();
    book.series = proposals.front();
    const auto result = catalogue.addBook(book);
    QVERIFY(!result.problem);
    QVERIFY(result.coverUrl);

    const auto detail = catalogue.detail(result.id);
    QVERIFY(detail->book.isbn13 == std::optional<std::string>("9780316005388"));
    QCOMPARE(detail->book.timesRead, 1); // shelved as read (AV-005)
    QVERIFY(detail->book.synopsis);
    QVERIFY(detail->book.publisher == std::optional<std::string>("Orbit"));
    QCOMPARE(detail->series.size(), std::size_t(1));
    QCOMPARE(detail->series.front().status, std::string("Complete"));
    QCOMPARE(catalogue.count(), 3); // the waiting entry took it; no fourth
    QVERIFY(catalogue.missingVolumes().empty());
}

void TestCatalogue::aSeriesTheProviderNamesIsProposed()
{
    using pinax::domain::SeriesProposal;
    Catalogue catalogue(":memory:");
    seedCultureWithAGap(catalogue);
    auto candidate = phlebasByIsbn();
    candidate.title = "The Hydrogen Sonata";
    candidate.seriesName = "The culture";
    candidate.seriesNumber = "10";

    const auto credits = catalogue.creditsFor(candidate.authors);
    const auto proposals = catalogue.seriesProposals(candidate.title, credits, candidate);
    QCOMPARE(proposals.size(), std::size_t(1));
    QVERIFY(proposals.front().kind == SeriesProposal::Kind::JoinsSeries);
    QVERIFY(proposals.front().position == std::optional<std::string>("10"));
    QVERIFY(proposals.front().sortPosition == 10.0); // the importer's rule (AV-006)
    QCOMPARE(proposals.front().heldAfter, 3);
    QCOMPARE(proposals.front().knownAfter, 4);

    Catalogue::NewBook book;
    book.edit.book.title = candidate.title;
    book.edit.credits = credits;
    book.candidate = candidate;
    book.series = proposals.front();
    const auto result = catalogue.addBook(book);
    QVERIFY(!result.problem);
    const auto detail = catalogue.detail(result.id);
    QCOMPARE(detail->series.size(), std::size_t(1));
    QVERIFY(detail->series.front().position == std::optional<std::string>("10"));
}

void TestCatalogue::anIsbnHeldIsNotAddedTwice()
{
    Catalogue catalogue(":memory:");
    seed(catalogue); // Surface Detail holds 9780316005388
    QVERIFY(catalogue.bookWithIsbn("9780316005388"));
    QCOMPARE(catalogue.bookWithIsbn("9780316005388")->title, std::string("Surface Detail"));
    QVERIFY(!catalogue.bookWithIsbn("9780575078017"));

    // Held by its ISBN-10 alone, it is still found.
    pinax::domain::BookEdit older;
    older.book.title = "Sunstorm";
    older.book.isbn10 = "0575078014";
    catalogue.save(older);
    QCOMPARE(catalogue.bookWithIsbn("9780575078017")->title, std::string("Sunstorm"));

    Catalogue::NewBook again;
    again.edit.book.title = "Consider Phlebas";
    again.edit.book.isbn13 = "9780316005388";
    again.candidate = phlebasByIsbn();
    const auto count = catalogue.count();
    const auto result = catalogue.addBook(again);
    QVERIFY(result.problem);
    QCOMPARE(result.id, 0);
    QCOMPARE(catalogue.count(), count);
}

void TestCatalogue::aHeldCopyWithoutAnIsbnIsRecognised()
{
    // The backlog was imported without ISBNs: scanning a book already on
    // the shelf finds it, rather than adding it again.
    Catalogue catalogue(":memory:");
    seedCultureWithAGap(catalogue);
    const auto credits = catalogue.creditsFor({"Iain Banks"});
    const auto like = catalogue.booksLike("Excession", credits);
    QCOMPARE(like.size(), std::size_t(1));
    QCOMPARE(like.front().title, std::string("Excession"));
    QVERIFY(catalogue.booksLike("Excession", catalogue.creditsFor({"Someone Else"})).empty());

    // And no series place is proposed for a title the series already has.
    auto candidate = phlebasByIsbn();
    candidate.title = "Excession";
    candidate.seriesName = "The Culture";
    candidate.seriesNumber = "5";
    QVERIFY(catalogue.seriesProposals("Excession", credits, candidate).empty());

    // Given the ISBN: the details follow under the usual rules; the owner's
    // own fields stay (AV-001).
    const auto id = like.front().id;
    auto book = catalogue.detail(id)->book;
    book.synopsis = "My own words.";
    book.synopsisSource = pinax::domain::Source::Manual;
    QVERIFY(!catalogue.save(book));
    const auto count = catalogue.count();
    const auto given = catalogue.giveIsbn(id, "9780316005388", std::string("031600538X"), candidate, true);
    QVERIFY(!given.problem);
    const auto after = catalogue.detail(id);
    QVERIFY(after->book.isbn13 == std::optional<std::string>("9780316005388"));
    QVERIFY(after->book.isbn10 == std::optional<std::string>("031600538X"));
    QVERIFY(after->book.synopsis == std::optional<std::string>("My own words."));
    QVERIFY(after->book.publisher == std::optional<std::string>("Orbit"));
    QCOMPARE(catalogue.count(), count);
    // Now it has one, it is no longer offered as a copy without.
    QVERIFY(catalogue.booksLike("Excession", credits).empty());

    // Another book may not take an ISBN already held, nor this book another.
    const auto other = catalogue.booksLike("Surface Detail", credits).front().id;
    QVERIFY(catalogue.giveIsbn(other, "9780316005388", std::nullopt, candidate, true).problem);
    QVERIFY(catalogue.giveIsbn(id, "9780575078017", std::nullopt, candidate, true).problem);
}

void TestCatalogue::filtersCombineWithTheRail()
{
    Catalogue catalogue(":memory:");
    seed(catalogue); // Excession (read), Surface Detail (unread), both Banks; Tau Zero (unread), Anderson
    catalogue.setRating({idOf(catalogue, "Excession")}, 9);
    MainWindow window;
    window.setCatalogue(&catalogue);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto* bar = window.filterBar();
    auto* author = bar->findChild<QComboBox*>(QStringLiteral("filter.author"));
    auto* rating = bar->findChild<QComboBox*>(QStringLiteral("filter.rating"));
    auto* clear = bar->findChild<QPushButton*>(QStringLiteral("filter.clear"));
    auto* shown = bar->findChild<QLabel*>(QStringLiteral("filter.shown"));
    auto* list = window.bookList();
    QVERIFY(!clear->isEnabled());

    // The choices carry their counts; only author credits count.
    QVERIFY(author->findText(QStringLiteral("Iain M. Banks (2)")) > 0);
    QVERIFY(author->findText(QStringLiteral("Poul Anderson (1)")) > 0);

    author->setCurrentIndex(author->findText(QStringLiteral("Iain M. Banks (2)")));
    QCOMPARE(list->shownCount(), 2);
    QCOMPARE(shown->text(), QStringLiteral("2 of 3 shown"));
    QVERIFY(clear->isEnabled());

    // The rail's read state joins the bar's author, not replacing it.
    window.rail()->chooseFilter({pinax::domain::BookFilter::Kind::ReadState, ReadStatus::Unread, 0});
    QCOMPARE(list->shownCount(), 1);
    QVERIFY(window.query().authorId);
    QVERIFY(window.query().readStatus == ReadStatus::Unread);
    QCOMPARE(bar->findChild<QComboBox*>(QStringLiteral("filter.read"))->currentText(), QStringLiteral("Unread"));

    // A rating range: Banks, any read state, rated 8 to 10 -> Excession.
    bar->findChild<QComboBox*>(QStringLiteral("filter.read"))->setCurrentIndex(0);
    rating->setCurrentIndex(2);
    bar->findChild<QSpinBox*>(QStringLiteral("filter.ratingFrom"))->setValue(8);
    QCOMPARE(list->shownCount(), 1);
    QCOMPARE(list->model()->index(0, pinax::ui::BookListModel::TitleColumn).data().toString(),
        QStringLiteral("Excession"));
    // The rail follows the bar: no read state, so All books.
    QVERIFY(window.rail()->currentFilter() == pinax::domain::BookFilter {});

    // Clear filters: every book, in one action.
    QTest::mouseClick(clear, Qt::LeftButton);
    QCOMPARE(list->shownCount(), 3);
    QVERIFY(window.query().empty());
    QCOMPARE(author->currentIndex(), 0);
    QVERIFY(shown->text().isEmpty());

    // All books in the rail does the same.
    author->setCurrentIndex(author->findText(QStringLiteral("Poul Anderson (1)")));
    QCOMPARE(list->shownCount(), 1);
    window.rail()->chooseFilter({pinax::domain::BookFilter::Kind::ReadState, ReadStatus::Read, 0});
    window.rail()->chooseFilter({});
    QCOMPARE(list->shownCount(), 3);
    QVERIFY(window.query().empty());
}

void TestCatalogue::groupingThroughTheWindow()
{
    Catalogue catalogue(":memory:");
    seed(catalogue); // Excession, Surface Detail (The Culture); Tau Zero
    catalogue.enrich(idOf(catalogue, "Excession"), phlebas(), false); // Science Fiction, Space opera
    MainWindow window;
    window.setCatalogue(&catalogue);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto* bar = window.filterBar();
    auto* groupBy = bar->findChild<QComboBox*>(QStringLiteral("filter.groupBy"));
    auto* list = window.bookList();
    auto headers = [&] {
        QStringList found;
        for (int row = 0; row < list->model()->rowCount(); ++row) {
            if (list->groupProxy()->isHeader(row))
                found << list->model()->index(row, 0).data().toString();
        }
        return found;
    };

    groupBy->setCurrentIndex(groupBy->findText(QStringLiteral("Group by genre")));
    QCOMPARE(headers(), QStringList({"Science Fiction · 1", "Space opera · 1", "No genre · 2"}));

    // With a filter: the groups hold only what is shown.
    bar->findChild<QComboBox*>(QStringLiteral("filter.read"))->setCurrentIndex(1); // Unread
    QCOMPARE(headers(), QStringList({"No genre · 2"}));

    // Clear filters clears filters, not the grouping.
    QTest::mouseClick(bar->findChild<QPushButton*>(QStringLiteral("filter.clear")), Qt::LeftButton);
    QCOMPARE(groupBy->currentText(), QStringLiteral("Group by genre"));
    QCOMPARE(headers().size(), 3);

    // Grouped by series, a book selected under its header reaches the panel.
    groupBy->setCurrentIndex(groupBy->findText(QStringLiteral("Group by series")));
    QCOMPARE(headers(), QStringList({"The Culture · 2", "Not in a series · 1"}));
    list->selectBook(idOf(catalogue, "Surface Detail"));
    QCOMPARE(window.detailPanel()->state(), DetailPanel::State::Viewing);
    QCOMPARE(window.detailPanel()->view()->findChild<QLabel*>(QStringLiteral("title"))->text(),
        QStringLiteral("Surface Detail"));
}

void TestCatalogue::aBooksSeriesAreEditedWithTheBook()
{
    using pinax::domain::SeriesPlacement;
    Catalogue catalogue(":memory:");
    seedCultureWithAGap(catalogue); // Excession 5, Surface Detail 9 held; Consider Phlebas 1 missing
    pinax::domain::BookEdit phlebas;
    phlebas.book.title = "Consider Phlebas";
    phlebas.credits = {{"Iain M. Banks", pinax::domain::CreditRole::Author}};
    const auto id = catalogue.save(phlebas).id;
    auto statusOf = [&](const std::string& name) {
        for (const auto& status : catalogue.seriesStatuses()) {
            if (status.name == name)
                return std::make_pair(status.held, status.known);
        }
        return std::make_pair(-1, -1);
    };
    QVERIFY(statusOf("The Culture") == std::make_pair(2, 3));

    // Joining by name, case aside, at the missing volume's place: it fills
    // the waiting entry rather than adding one (AV-007).
    auto edit = phlebas;
    edit.book = catalogue.detail(id)->book;
    edit.series = std::vector<SeriesPlacement> {{std::nullopt, "the culture", std::string("1"), std::nullopt}};
    QVERIFY(!catalogue.save(edit).problem);
    QVERIFY(statusOf("The Culture") == std::make_pair(3, 3));
    QVERIFY(catalogue.missingVolumes().empty());
    QVERIFY(catalogue.detail(id)->series.front().sortPosition == 1.0);

    // A second series, new: created, the sort number worked out (AV-006).
    edit.series->push_back({std::nullopt, "Banks Firsts", std::string("1"), std::nullopt});
    QVERIFY(!catalogue.save(edit).problem);
    QVERIFY(statusOf("Banks Firsts") == std::make_pair(1, 1));
    QCOMPARE(catalogue.detail(id)->series.size(), std::size_t(2));

    // Moved within a series: the same entry, its position changed.
    (*edit.series)[0].position = "0";
    (*edit.series)[0].sortPosition = 0.5;
    QVERIFY(!catalogue.save(edit).problem);
    const auto culture = catalogue.detail(id)->series;
    QVERIFY(std::any_of(culture.begin(), culture.end(), [](const auto& m) {
        return m.name == "The Culture" && m.position == std::optional<std::string>("0") && m.sortPosition == 0.5;
    }));
    QVERIFY(statusOf("The Culture") == std::make_pair(3, 3));

    // Leaving a series: it no longer counts the book.
    edit.series->erase(edit.series->begin());
    QVERIFY(!catalogue.save(edit).problem);
    QVERIFY(statusOf("The Culture") == std::make_pair(2, 2));
    QCOMPARE(catalogue.detail(id)->series.size(), std::size_t(1));

    // Listed twice: refused, nothing changed.
    edit.series->push_back({std::nullopt, "banks firsts", std::nullopt, std::nullopt});
    QVERIFY(catalogue.save(edit).problem);
    QCOMPARE(catalogue.detail(id)->series.size(), std::size_t(1));

    // Saved without series rows given (every other path): left alone.
    auto untouched = catalogue.detail(id)->book;
    untouched.notes = "Signed.";
    QVERIFY(!catalogue.save(pinax::domain::BookEdit {untouched, phlebas.credits}).problem);
    QCOMPARE(catalogue.detail(id)->series.size(), std::size_t(1));
}

void TestCatalogue::theFormPlacesABookInASeries()
{
    Catalogue catalogue(":memory:");
    seedCultureWithAGap(catalogue);
    pinax::domain::BookEdit phlebas;
    phlebas.book.title = "Consider Phlebas";
    phlebas.credits = {{"Iain M. Banks", pinax::domain::CreditRole::Author}};
    const auto id = catalogue.save(phlebas).id;
    MainWindow window;
    window.setCatalogue(&catalogue);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.bookList()->selectBook(id);
    QTest::keyClick(window.bookList(), Qt::Key_F2);
    auto* editor = window.detailPanel()->editor();
    QCOMPARE(editor->findChildren<QComboBox*>(QStringLiteral("edit.series.name")).size(), 0);

    QTest::mouseClick(editor->findChild<QPushButton*>(QStringLiteral("edit.series.add")), Qt::LeftButton);
    auto* name = editor->findChild<QComboBox*>(QStringLiteral("edit.series.name"));
    QVERIFY(name);
    QVERIFY(name->findText(QStringLiteral("The Culture")) >= 0); // offered
    name->setCurrentIndex(name->findText(QStringLiteral("The Culture")));
    // No position typed: the title alone finds the waiting volume.
    QTest::mouseClick(editor->findChild<QPushButton*>(QStringLiteral("edit.save")), Qt::LeftButton);
    QCOMPARE(window.detailPanel()->state(), DetailPanel::State::Viewing);
    QVERIFY(catalogue.missingVolumes().empty());
    QVERIFY(catalogue.detail(id)->series.front().position == std::optional<std::string>("1"));
    // The panel shows it in the series now.
    QCOMPARE(window.detailPanel()->view()->findChild<QLabel*>(QStringLiteral("series.name"))->text(),
        QStringLiteral("The Culture"));

    // Back in the form: the row is there, filled; a bad sort number is refused.
    QTest::keyClick(window.bookList(), Qt::Key_F2);
    QCOMPARE(window.detailPanel()->state(), DetailPanel::State::Editing);
    QCOMPARE(editor->findChild<QComboBox*>(QStringLiteral("edit.series.name"))->currentText(), QStringLiteral("The Culture"));
    QCOMPARE(editor->findChild<QLineEdit*>(QStringLiteral("edit.series.position"))->text(), QStringLiteral("1"));
    editor->findChild<QLineEdit*>(QStringLiteral("edit.series.sort"))->setText(QStringLiteral("first"));
    QTest::mouseClick(editor->findChild<QPushButton*>(QStringLiteral("edit.save")), Qt::LeftButton);
    QCOMPARE(window.detailPanel()->state(), DetailPanel::State::Editing);
    QVERIFY(editor->findChild<QLabel*>(QStringLiteral("edit.error"))->text().contains(QStringLiteral("sort number")));

    // Removing the row takes the book out of the series.
    QTest::mouseClick(editor->findChild<QToolButton*>(QStringLiteral("edit.series.remove")), Qt::LeftButton);
    QApplication::processEvents();
    QTest::mouseClick(editor->findChild<QPushButton*>(QStringLiteral("edit.save")), Qt::LeftButton);
    QVERIFY(catalogue.detail(id)->series.empty());
}

void TestCatalogue::searchNarrowsAsYouType()
{
    Catalogue catalogue(":memory:");
    seedCulture(catalogue); // Surface Detail, Excession (The Culture); Tau Zero
    MainWindow window;
    window.setCatalogue(&catalogue);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto* search = window.filterBar()->findChild<QLineEdit*>(QStringLiteral("filter.search"));
    auto* list = window.bookList();

    // No submit: each keystroke narrows.
    search->setFocus();
    QTest::keyClicks(search, QStringLiteral("cult"));
    QCOMPARE(list->shownCount(), 2);
    QTest::keyClicks(search, QStringLiteral("ure exc"));
    QCOMPARE(search->text(), QStringLiteral("culture exc")); // the space typed stays
    QCOMPARE(list->shownCount(), 1);
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("1 of 3")));
    // Esc clears it.
    QTest::keyClick(search, Qt::Key_Escape);
    QVERIFY(search->text().isEmpty());
    QCOMPARE(list->shownCount(), 3);

    // With a filter, and cleared with the filters.
    QTest::keyClicks(search, QStringLiteral("anderson"));
    window.rail()->chooseFilter({pinax::domain::BookFilter::Kind::ReadState, ReadStatus::Unread, 0});
    QCOMPARE(list->shownCount(), 1);
    QTest::mouseClick(window.filterBar()->findChild<QPushButton*>(QStringLiteral("filter.clear")), Qt::LeftButton);
    QVERIFY(search->text().isEmpty());
    QCOMPARE(list->shownCount(), 3);

    // Ctrl+F from a series' page: back to the list, in the search field.
    window.rail()->chooseFilter({pinax::domain::BookFilter::Kind::Series, ReadStatus::Unread,
        catalogue.seriesStatuses().front().id});
    QVERIFY(window.showingSeries());
    QTest::keyClick(&window, Qt::Key_F, Qt::ControlModifier);
    QVERIFY(!window.showingSeries());
    QTRY_VERIFY(search->hasFocus());

    // A book renamed through the window is found by its new name.
    QTest::keyClick(search, Qt::Key_Escape);
    window.bookList()->setFocus();
    window.bookList()->selectBook(idOf(catalogue, "Tau Zero"));
    QTest::keyClick(window.bookList(), Qt::Key_F2);
    auto* title = window.detailPanel()->editor()->findChild<QLineEdit*>(QStringLiteral("edit.title"));
    title->setText(QStringLiteral("Tau Zero Revisited"));
    QTest::mouseClick(window.detailPanel()->editor()->findChild<QPushButton*>(QStringLiteral("edit.save")),
        Qt::LeftButton);
    search->setFocus();
    QTest::keyClicks(search, QStringLiteral("revisited"));
    QCOMPARE(list->shownCount(), 1);
}

void TestCatalogue::backingUpFromThePanel()
{
    OnDisk disk;
    Catalogue& catalogue = *disk.catalogue;
    MainWindow window;
    window.setCatalogue(&catalogue);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QTest::keyClick(&window, Qt::Key_B, Qt::ControlModifier);
    QCOMPARE(window.detailPanel()->state(), DetailPanel::State::BackingUp);
    QVERIFY(!window.bookList()->isEnabled()); // the panel is busy, as any form
    auto* view = window.detailPanel()->backupView();
    auto* path = view->findChild<QLineEdit*>(QStringLiteral("backup.path"));
    QVERIFY(path->text().contains(QStringLiteral("Pinax backups")));
    QVERIFY(path->text().endsWith(QDate::currentDate().toString(Qt::ISODate) + QStringLiteral(".db")));

    const QString target = disk.dir.filePath(QStringLiteral("elsewhere/pinax-copy.db"));
    path->setText(target);
    QVERIFY(view->findChild<QLabel*>(QStringLiteral("backup.warning"))->text().isEmpty());
    QTest::mouseClick(view->findChild<QPushButton*>(QStringLiteral("backup.go")), Qt::LeftButton);
    QVERIFY(view->findChild<QLabel*>(QStringLiteral("backup.outcome"))->text().contains(QStringLiteral("Backed up 1 books")));
    QVERIFY(QFile::exists(target));
    QVERIFY(view->findChild<QLabel*>(QStringLiteral("backup.warning"))->text().isEmpty()); // not "will be replaced"
    QCOMPARE(Catalogue(target.toStdString()).count(), 1);

    // There now: the form says it will be replaced, and replaces it.
    path->setText(QString());
    path->setText(target);
    QVERIFY(view->findChild<QLabel*>(QStringLiteral("backup.warning"))->text().contains(QStringLiteral("replaced")));
    // A folder is not a file name.
    path->setText(disk.dir.path());
    QVERIFY(!view->findChild<QPushButton*>(QStringLiteral("backup.go"))->isEnabled());

    QTest::keyClick(path, Qt::Key_Escape);
    QCOMPARE(window.detailPanel()->state(), DetailPanel::State::Empty);
    QVERIFY(window.bookList()->isEnabled());
    // The folder used is offered next time.
    QTest::keyClick(&window, Qt::Key_B, Qt::ControlModifier);
    QVERIFY(path->text().startsWith(disk.dir.filePath(QStringLiteral("elsewhere"))));
}

void TestCatalogue::exportingAnSqlDump()
{
    OnDisk disk;
    Catalogue& catalogue = *disk.catalogue;
    MainWindow window;
    window.setCatalogue(&catalogue);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QTest::keyClick(&window, Qt::Key_E, Qt::ControlModifier);
    QCOMPARE(window.detailPanel()->state(), DetailPanel::State::Exporting);
    auto* view = window.detailPanel()->exportView();
    auto* path = view->findChild<QLineEdit*>(QStringLiteral("export.path"));
    QCOMPARE(view->findChild<QComboBox*>(QStringLiteral("export.format"))->currentText(), QStringLiteral("SQL dump"));
    QVERIFY(path->text().contains(QStringLiteral("Pinax exports")));
    QVERIFY(path->text().endsWith(QStringLiteral(".sql")));

    const QString target = disk.dir.filePath(QStringLiteral("exports/pinax.sql"));
    path->setText(target);
    QTest::mouseClick(view->findChild<QPushButton*>(QStringLiteral("export.go")), Qt::LeftButton);
    QVERIFY2(view->findChild<QLabel*>(QStringLiteral("export.outcome"))->text().contains(QStringLiteral("Wrote 1 books as SQL")),
        qPrintable(view->findChild<QLabel*>(QStringLiteral("export.outcome"))->text()));

    // It needs nothing of Pinax: plain SQL onto an empty database.
    QFile file(target);
    QVERIFY(file.open(QIODevice::ReadOnly));
    pinax::db::Connection restored(":memory:");
    restored.exec(file.readAll().toStdString());
    pinax::db::Statement count(restored, "SELECT title FROM book");
    QVERIFY(count.step());
    QCOMPARE(count.columnText(0), std::string("Consider Phlebas"));

    QTest::keyClick(path, Qt::Key_Escape);
    QCOMPARE(window.detailPanel()->state(), DetailPanel::State::Empty);
}

void TestCatalogue::theWorkbookAgreesWithTheViews()
{
    using Cell = pinax::domain::Workbook::Cell;
    Catalogue catalogue(":memory:");
    seedCulture(catalogue); // Surface Detail (unread), Excession (read) in The Culture; Tau Zero
    // An anthology edited, not written, by someone: not an author (IMP-004).
    pinax::domain::BookEdit anthology;
    anthology.book.title = "Lost Mars";
    anthology.credits = {{"Mike Ashley", pinax::domain::CreditRole::Editor}};
    catalogue.save(anthology);
    catalogue.setRating({idOf(catalogue, "Excession")}, 9);

    const auto workbook = catalogue.workbook();
    QCOMPARE(workbook.sheets.size(), std::size_t(3));
    const auto* books = workbook.sheet("Books");
    const auto* series = workbook.sheet("Series status");
    const auto* authors = workbook.sheet("Authors");
    QVERIFY(books && series && authors);
    QCOMPARE(books->headers.front(), std::string("#"));
    QCOMPARE(books->headers.size(), std::size_t(13));
    QCOMPARE(books->rows.size(), std::size_t(catalogue.count()));

    auto textOf = [](const Cell& cell) { return std::holds_alternative<std::string>(cell) ? std::get<std::string>(cell) : std::string(); };
    auto numberOf = [](const Cell& cell) { return std::holds_alternative<std::int64_t>(cell) ? std::get<std::int64_t>(cell) : -1; };

    // Series: row for row, the view's own figures (AV-011).
    const auto statuses = catalogue.seriesStatuses();
    QCOMPARE(series->rows.size(), statuses.size());
    for (std::size_t i = 0; i < statuses.size(); ++i) {
        const auto& row = series->rows[i];
        QCOMPARE(textOf(row[0]), statuses[i].name);
        QCOMPARE(numberOf(row[2]), std::int64_t(statuses[i].held));
        QCOMPARE(numberOf(row[3]), std::int64_t(statuses[i].heldRead));
        QCOMPARE(numberOf(row[4]), std::int64_t(statuses[i].held - statuses[i].heldRead));
        QCOMPARE(textOf(row[5]), statuses[i].status);
    }
    const auto& culture = series->rows.front();
    QCOMPARE(textOf(culture[1]), std::string("Iain M. Banks"));
    // Named volumes listed, placeholders counted, as the panel says it.
    QCOMPARE(textOf(culture[6]), std::string("Consider Phlebas (1); and 1 not yet identified"));

    // A book row carries its series' status and what it lacks.
    for (const auto& row : books->rows) {
        if (textOf(row[1]) == "Excession") {
            QCOMPARE(textOf(row[3]), std::string("The Culture"));
            QCOMPARE(textOf(row[4]), std::string("5"));
            QCOMPARE(textOf(row[5]), std::string("Read"));
            QCOMPARE(textOf(row[6]), std::string("Incomplete"));
            QCOMPARE(numberOf(row[8]), std::int64_t(9));
            QCOMPARE(numberOf(row[9]), std::int64_t(1));
        }
        if (textOf(row[1]) == "Tau Zero")
            QVERIFY(std::holds_alternative<std::monostate>(row[3])); // no series: an empty cell
    }

    // Authors: author credits only; the editor is not among them.
    QCOMPARE(authors->rows.size(), std::size_t(2));
    QCOMPARE(textOf(authors->rows[0][0]), std::string("Poul Anderson"));
    QCOMPARE(textOf(authors->rows[1][0]), std::string("Iain M. Banks"));
    QCOMPARE(numberOf(authors->rows[1][1]), std::int64_t(2));
    QCOMPARE(numberOf(authors->rows[1][2]), std::int64_t(1));
    QCOMPARE(numberOf(authors->rows[1][3]), std::int64_t(1));
}

void TestCatalogue::exportingAnExcelWorkbook()
{
    OnDisk disk;
    Catalogue& catalogue = *disk.catalogue;
    MainWindow window;
    window.setCatalogue(&catalogue);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QTest::keyClick(&window, Qt::Key_E, Qt::ControlModifier);
    auto* view = window.detailPanel()->exportView();
    auto* format = view->findChild<QComboBox*>(QStringLiteral("export.format"));
    auto* path = view->findChild<QLineEdit*>(QStringLiteral("export.path"));
    format->setCurrentIndex(format->findText(QStringLiteral("Excel workbook")));
    QVERIFY(path->text().endsWith(QStringLiteral(".xlsx"))); // the name follows the format
    const QString target = disk.dir.filePath(QStringLiteral("exports/pinax.xlsx"));
    path->setText(target);
    QTest::mouseClick(view->findChild<QPushButton*>(QStringLiteral("export.go")), Qt::LeftButton);
    QVERIFY2(view->findChild<QLabel*>(QStringLiteral("export.outcome"))->text().contains(QStringLiteral("as an Excel workbook")),
        qPrintable(view->findChild<QLabel*>(QStringLiteral("export.outcome"))->text()));
    QFile file(target);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.read(2), QByteArray("PK")); // an .xlsx is a zip
    QVERIFY(!QFile::exists(target + QStringLiteral(".partial")));
    file.close();

    // Opened as a spreadsheet program would open it, where one is installed.
    const QString office = QStandardPaths::findExecutable(QStringLiteral("soffice"));
    if (office.isEmpty())
        QSKIP("LibreOffice is not installed; the workbook was written but not read back");
    QTemporaryDir profile;
    QProcess convert;
    convert.setWorkingDirectory(disk.dir.filePath(QStringLiteral("exports")));
    convert.start(office, {QStringLiteral("-env:UserInstallation=file://") + profile.path(), QStringLiteral("--headless"),
                              QStringLiteral("--convert-to"),
                              QStringLiteral("csv:Text - txt - csv (StarCalc):44,34,76,1,,0,false,true,false,false,false,-1"),
                              QStringLiteral("pinax.xlsx")});
    QVERIFY(convert.waitForFinished(120000));
    QFile books(disk.dir.filePath(QStringLiteral("exports/pinax-Books.csv")));
    QVERIFY2(books.open(QIODevice::ReadOnly), "LibreOffice did not read the workbook");
    const QList<QByteArray> lines = books.readAll().trimmed().split('\n');
    QCOMPARE(lines.size(), 2); // the header and one book
    QVERIFY(lines.at(0).startsWith("#,Title,Author,Series,Vol.,Shelf,Series status,Still missing"));
    QVERIFY(lines.at(1).startsWith("1,Consider Phlebas,"));
    QVERIFY(QFile::exists(disk.dir.filePath(QStringLiteral("exports/pinax-Series status.csv"))));
    QVERIFY(QFile::exists(disk.dir.filePath(QStringLiteral("exports/pinax-Authors.csv"))));
}

void TestCatalogue::exportingTheListAsCsv()
{
    QTemporaryDir dir;
    Catalogue catalogue(dir.filePath(QStringLiteral("pinax.db")).toStdString());
    seedCulture(catalogue); // Surface Detail (unread), Excession (read); Tau Zero (unread)
    MainWindow window;
    window.setCatalogue(&catalogue);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    // The view as it stands: unread only, sorted by title.
    window.rail()->chooseFilter({pinax::domain::BookFilter::Kind::ReadState, ReadStatus::Unread, 0});
    window.bookList()->sortByColumn(pinax::ui::BookListModel::TitleColumn, Qt::AscendingOrder);
    QTest::keyClick(&window, Qt::Key_E, Qt::ControlModifier);
    auto* view = window.detailPanel()->exportView();
    auto* format = view->findChild<QComboBox*>(QStringLiteral("export.format"));
    format->setCurrentIndex(format->findText(QStringLiteral("CSV of the book list")));
    const QString target = dir.filePath(QStringLiteral("unread.csv"));
    view->findChild<QLineEdit*>(QStringLiteral("export.path"))->setText(target);
    QTest::mouseClick(view->findChild<QPushButton*>(QStringLiteral("export.go")), Qt::LeftButton);
    QVERIFY2(view->findChild<QLabel*>(QStringLiteral("export.outcome"))->text().contains(QStringLiteral("Wrote 2 books as CSV")),
        qPrintable(view->findChild<QLabel*>(QStringLiteral("export.outcome"))->text()));

    QFile file(target);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QList<QByteArray> lines = file.readAll().trimmed().split('\n');
    QCOMPARE(lines.size(), 3);
    QVERIFY(lines.at(1).startsWith("Surface Detail,,Iain M. Banks,The Culture,9,9,unread,0"));
    QVERIFY(lines.at(2).startsWith("Tau Zero,,Poul Anderson,,,,unread,0"));
}

void TestCatalogue::openingTidiesLendingTagsAwayButNotTheOwners()
{
    QTemporaryDir dir;
    const std::string path = dir.filePath(QStringLiteral("pinax.db")).toStdString();
    std::int64_t id = 0;
    {
        Catalogue catalogue(path);
        pinax::domain::BookEdit edit;
        edit.book.title = "Consider Phlebas";
        id = catalogue.save(edit).id;
        // As stored before IMP-008: fetched tags, and one the owner chose.
        pinax::db::GenreRepository genres(catalogue.connection());
        genres.addToBook(id, "Science fiction", pinax::domain::Source::OpenLibrary);
        genres.addToBook(id, "Accessible book", pinax::domain::Source::OpenLibrary);
        genres.addToBook(id, "nyt:trade-fiction-paperback=2013-03-31", pinax::domain::Source::OpenLibrary);
        genres.addToBook(id, "OverDrive", pinax::domain::Source::Manual); // the owner's: kept
    }
    Catalogue reopened(path);
    pinax::db::GenreRepository genres(reopened.connection());
    QCOMPARE(genres.forBook(id),
        (std::vector<pinax::db::GenreLink> {{"OverDrive", pinax::domain::Source::Manual},
            {"Science fiction", pinax::domain::Source::OpenLibrary}}));
    // Genres nothing links to any more are gone too.
    pinax::db::Statement count(reopened.connection(), "SELECT COUNT(*) FROM genre");
    count.step();
    QCOMPARE(count.columnInt(0), std::int64_t(2));
    // Opening again finds nothing more to do.
    QCOMPARE(genres.removeServiceSubjects(), 0);
}

QTEST_MAIN(TestCatalogue)
#include "test_catalogue.moc"
