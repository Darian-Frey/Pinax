#include "ui/rail_view.h"

#include <QStandardItemModel>
#include <QTest>

using pinax::domain::BookFilter;
using pinax::domain::ReadStatus;
using pinax::domain::SeriesStatus;
using pinax::ui::RailContents;
using pinax::ui::RailView;

namespace {

SeriesStatus series(std::int64_t id, std::string name, int held, int known, std::string status)
{
    SeriesStatus s;
    s.id = id;
    s.name = std::move(name);
    s.held = held;
    s.known = known;
    s.status = std::move(status);
    return s;
}

RailContents contents()
{
    RailContents c;
    c.all = 443;
    c.unread = 267;
    c.reading = 0;
    c.read = 176;
    c.series = {series(7, "The Culture", 9, 10, "Incomplete"),
        series(3, "Agent Cormac", 5, 5, "Complete"),
        series(4, "A Song of Ice and Fire", 7, 7, "Complete to date")};
    return c;
}

QString cell(const RailView& rail, int section, int row, int column)
{
    const QAbstractItemModel* model = rail.model();
    const QModelIndex heading = model->index(section, 0);
    return model->index(row, column, heading).data().toString();
}

struct Chosen {
    QList<BookFilter> filters;
    QStringList labels;
};

void record(RailView& rail, Chosen& chosen)
{
    QObject::connect(&rail, &RailView::filterChosen, &rail,
        [&chosen](const BookFilter& filter, const QString& label) {
            chosen.filters << filter;
            chosen.labels << label;
        });
}

} // namespace

class TestRail : public QObject {
    Q_OBJECT

private slots:
    void showsLibraryAndSeriesWithCounts();
    void headingsCannotBeChosen();
    void choosingAnEntryEmitsItsFilter();
    void rebuildingKeepsTheChoiceSilently();
    void aVanishedChoiceFallsBackToAllBooks();
};

void TestRail::showsLibraryAndSeriesWithCounts()
{
    RailView rail;
    rail.setContents(contents());
    const QAbstractItemModel* model = rail.model();

    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(model->index(0, 0).data().toString(), QStringLiteral("LIBRARY"));
    QCOMPARE(cell(rail, 0, 0, 0), QStringLiteral("All books"));
    QCOMPARE(cell(rail, 0, 0, 1), QStringLiteral("443"));
    QCOMPARE(cell(rail, 0, 1, 0), QStringLiteral("Unread"));
    QCOMPARE(cell(rail, 0, 2, 1), QStringLiteral("0"));
    QCOMPARE(cell(rail, 0, 3, 1), QStringLiteral("176"));

    QCOMPARE(model->index(1, 0).data().toString(), QStringLiteral("SERIES · 3"));
    // In the order given; filing order is the caller's.
    QCOMPARE(cell(rail, 1, 0, 0), QStringLiteral("The Culture"));
    QCOMPARE(cell(rail, 1, 0, 1), QStringLiteral("9/10"));
    QCOMPARE(model->index(0, 1, model->index(1, 0)).data(Qt::ToolTipRole).toString(),
        QStringLiteral("Incomplete — one volume missing"));
    QCOMPARE(model->index(2, 1, model->index(1, 0)).data(Qt::ToolTipRole).toString(),
        QStringLiteral("Complete to date — still being written"));
}

void TestRail::headingsCannotBeChosen()
{
    RailView rail;
    rail.setContents(contents());
    QVERIFY(!(rail.model()->index(1, 0).flags() & Qt::ItemIsSelectable));
}

void TestRail::choosingAnEntryEmitsItsFilter()
{
    RailView rail;
    rail.setContents(contents());
    QVERIFY(rail.currentFilter() == BookFilter {}); // All books to start

    Chosen chosen;
    record(rail, chosen);
    rail.chooseFilter({BookFilter::Kind::Series, ReadStatus::Unread, 7});
    QCOMPARE(chosen.filters.size(), 1);
    QVERIFY(chosen.filters[0].kind == BookFilter::Kind::Series);
    QCOMPARE(chosen.filters[0].seriesId, std::int64_t(7));
    QCOMPARE(chosen.labels[0], QStringLiteral("The Culture"));

    rail.chooseFilter({BookFilter::Kind::ReadState, ReadStatus::Read, 0});
    QCOMPARE(chosen.filters.size(), 2);
    QVERIFY(chosen.filters[1].readStatus == ReadStatus::Read);
    QCOMPARE(chosen.labels[1], QStringLiteral("Read"));

    // Arrow keys pass over the SERIES heading to the first series.
    QTest::keyClick(&rail, Qt::Key_Down);
    QCOMPARE(chosen.labels.last(), QStringLiteral("The Culture"));
}

void TestRail::rebuildingKeepsTheChoiceSilently()
{
    RailView rail;
    rail.setContents(contents());
    rail.chooseFilter({BookFilter::Kind::Series, ReadStatus::Unread, 3});

    Chosen chosen;
    record(rail, chosen);
    RailContents updated = contents();
    updated.read = 177;
    rail.setContents(updated);

    QCOMPARE(chosen.filters.size(), 0);
    QCOMPARE(rail.currentFilter().seriesId, std::int64_t(3));
    QCOMPARE(rail.currentIndex().data().toString(), QStringLiteral("Agent Cormac"));
    QCOMPARE(cell(rail, 0, 3, 1), QStringLiteral("177"));
}

void TestRail::aVanishedChoiceFallsBackToAllBooks()
{
    RailView rail;
    rail.setContents(contents());
    rail.chooseFilter({BookFilter::Kind::Series, ReadStatus::Unread, 4});

    RailContents fewer = contents();
    fewer.series.pop_back(); // series 4 gone
    rail.setContents(fewer);
    QVERIFY(rail.currentFilter().kind == BookFilter::Kind::All);
    QCOMPARE(rail.currentIndex().data().toString(), QStringLiteral("All books"));
}

QTEST_MAIN(TestRail)
#include "test_rail.moc"
