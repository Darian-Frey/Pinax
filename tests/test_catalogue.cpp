#include "app/catalogue.h"
#include "app/main_window.h"
#include "io/csv_importer.h"
#include "ui/book_list_view.h"
#include "ui/book_view.h"
#include "ui/detail_panel.h"

#include <QLabel>
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

    auto book = catalogue.detail(id)->book;
    book.rating = 7;
    emit window.detailPanel()->saveRequested(book);

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

QTEST_MAIN(TestCatalogue)
#include "test_catalogue.moc"
