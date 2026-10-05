#include "ui/book_list_model.h"
#include "ui/book_list_view.h"
#include "ui/book_sort_proxy.h"

#include <QHeaderView>
#include <QTest>

using pinax::domain::BookSummary;
using pinax::domain::ReadStatus;
using pinax::ui::BookListModel;
using pinax::ui::BookListView;

namespace {

BookSummary book(std::int64_t id, std::string title, std::string sortTitle = {})
{
    BookSummary row;
    row.id = id;
    row.sortTitle = sortTitle.empty() ? title : std::move(sortTitle);
    row.title = std::move(title);
    return row;
}

BookSummary byAuthor(std::int64_t id, std::string title, std::string authors, std::string authorSort)
{
    BookSummary row = book(id, std::move(title));
    row.authors = std::move(authors);
    row.authorSort = std::move(authorSort);
    return row;
}

BookSummary inSeries(std::int64_t id, std::string title, std::string label,
    std::optional<double> sortPosition)
{
    BookSummary row = book(id, std::move(title));
    row.seriesLabel = std::move(label);
    row.seriesSort = "The Culture";
    row.seriesSortPosition = sortPosition;
    return row;
}

BookSummary rated(std::int64_t id, std::string title, std::optional<int> rating)
{
    BookSummary row = book(id, std::move(title));
    row.rating = rating;
    return row;
}

// Titles in the order the view currently shows them.
QStringList shownTitles(const BookListView& view)
{
    QStringList titles;
    const QAbstractItemModel* model = view.model();
    for (int row = 0; row < model->rowCount(); ++row)
        titles << model->index(row, BookListModel::TitleColumn).data().toString();
    return titles;
}

} // namespace

class TestBookList : public QObject {
    Q_OBJECT

private slots:
    void showsOneRowPerBookWithItsColumns();
    void opensSortedByAuthor();
    void titleSortIgnoresLeadingArticle();
    void authorSortsByFilingName();
    void seriesSortsBySortPositionNotPrintedPosition();
    void missingValuesSortLastInBothDirections();
};

void TestBookList::showsOneRowPerBookWithItsColumns()
{
    BookSummary excession = inSeries(1, "Excession", "The Culture · 5", 5);
    excession.authors = "Iain M. Banks";
    excession.readStatus = ReadStatus::Read;
    excession.timesRead = 2;
    excession.rating = 9;
    excession.publishedYear = 1996;

    BookListModel model;
    model.setBooks({excession, book(2, "Tau Zero")});

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.columnCount(), int(BookListModel::ColumnCount));

    auto cell = [&](int row, int column, int role = Qt::DisplayRole) {
        return model.data(model.index(row, column), role);
    };
    QCOMPARE(cell(0, BookListModel::TitleColumn).toString(), QStringLiteral("Excession"));
    QCOMPARE(cell(0, BookListModel::AuthorColumn).toString(), QStringLiteral("Iain M. Banks"));
    QCOMPARE(cell(0, BookListModel::SeriesColumn).toString(), QStringLiteral("The Culture · 5"));
    QCOMPARE(cell(0, BookListModel::RatingColumn).toString(), QStringLiteral("9"));
    QCOMPARE(cell(0, BookListModel::YearColumn).toString(), QStringLiteral("1996"));
    QCOMPARE(cell(0, BookListModel::ReadColumn, Qt::ToolTipRole).toString(),
        QStringLiteral("Read 2 times"));

    // Absent values: unrated shows a dash, no year and no series show nothing.
    QCOMPARE(cell(1, BookListModel::RatingColumn).toString(), QStringLiteral("–"));
    QCOMPARE(cell(1, BookListModel::YearColumn).toString(), QString());
    QCOMPARE(cell(1, BookListModel::SeriesColumn).toString(), QString());
    QCOMPARE(cell(1, BookListModel::ReadColumn, Qt::ToolTipRole).toString(),
        QStringLiteral("Unread"));
}

void TestBookList::opensSortedByAuthor()
{
    BookListView view;
    QCOMPARE(view.horizontalHeader()->sortIndicatorSection(), int(BookListModel::AuthorColumn));
    QCOMPARE(view.horizontalHeader()->sortIndicatorOrder(), Qt::AscendingOrder);
}

void TestBookList::titleSortIgnoresLeadingArticle()
{
    BookListView view;
    view.setBooks({
        book(1, "The Hydrogen Sonata", "Hydrogen Sonata, The"),
        book(2, "Matter"),
        book(3, "Excession"),
    });
    view.sortByColumn(BookListModel::TitleColumn, Qt::AscendingOrder);
    QCOMPARE(shownTitles(view),
        QStringList({"Excession", "The Hydrogen Sonata", "Matter"}));
}

void TestBookList::authorSortsByFilingName()
{
    BookListView view;
    view.setBooks({
        byAuthor(1, "Excession", "Iain M. Banks", "Banks, Iain M."),
        byAuthor(2, "Gridlinked", "Neal Asher", "Asher, Neal"),
        byAuthor(3, "Mostly Harmless", "Douglas Adams", "Adams, Douglas"),
        book(4, "Practical Algebra"), // no author: last
    });
    view.sortByColumn(BookListModel::AuthorColumn, Qt::AscendingOrder);
    QCOMPARE(shownTitles(view),
        QStringList({"Mostly Harmless", "Gridlinked", "Excession", "Practical Algebra"}));
}

void TestBookList::seriesSortsBySortPositionNotPrintedPosition()
{
    // AV-006: '10' after '2', '6.5' between 6 and 7, and a position with no
    // sort key after every numbered one.
    BookListView view;
    view.setBooks({
        inSeries(1, "The Hydrogen Sonata", "The Culture · 10", 10),
        inSeries(2, "The Player of Games", "The Culture · 2", 2),
        inSeries(3, "Look to Windward", "The Culture · 7", 7),
        inSeries(4, "An Interlude", "The Culture · 6.5", 6.5),
        inSeries(5, "Inversions", "The Culture · 6", 6),
        inSeries(6, "The State of the Art", "The Culture · companion", std::nullopt),
        book(7, "Tau Zero"),
    });
    view.sortByColumn(BookListModel::SeriesColumn, Qt::AscendingOrder);
    QCOMPARE(shownTitles(view),
        QStringList({"The Player of Games", "Inversions", "An Interlude", "Look to Windward",
            "The Hydrogen Sonata", "The State of the Art", "Tau Zero"}));
}

void TestBookList::missingValuesSortLastInBothDirections()
{
    BookListView view;
    view.setBooks({
        rated(1, "Nine", 9),
        rated(2, "Unrated", std::nullopt),
        rated(3, "Seven", 7),
    });

    view.sortByColumn(BookListModel::RatingColumn, Qt::AscendingOrder);
    QCOMPARE(shownTitles(view), QStringList({"Seven", "Nine", "Unrated"}));

    view.sortByColumn(BookListModel::RatingColumn, Qt::DescendingOrder);
    QCOMPARE(shownTitles(view), QStringList({"Nine", "Seven", "Unrated"}));
}

QTEST_MAIN(TestBookList)
#include "test_book_list.moc"
