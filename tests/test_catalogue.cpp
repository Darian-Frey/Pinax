#include "app/catalogue.h"
#include "db/db_error.h"
#include "db/statement.h"
#include "domain/credit_text.h"
#include "app/main_window.h"
#include "io/csv_importer.h"
#include "io/series_importer.h"
#include "ui/book_editor.h"
#include "ui/book_list_model.h"
#include "ui/book_list_view.h"
#include "ui/book_view.h"
#include "ui/detail_panel.h"
#include "ui/rail_view.h"
#include "ui/series_entry_model.h"
#include "ui/series_page.h"
#include "ui/attach_view.h"
#include "ui/entry_editor.h"
#include "ui/series_view.h"

#include <QAction>
#include <QLabel>
#include <QStatusBar>
#include <QToolButton>
#include <QLineEdit>
#include <QPushButton>
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

QTEST_MAIN(TestCatalogue)
#include "test_catalogue.moc"
