#include "ui/series_entry_model.h"
#include "ui/series_page.h"
#include "ui/series_view.h"
#include "ui/missing_page.h"

#include <QLabel>
#include <QSignalSpy>
#include <QTest>
#include <QTableView>
#include <QToolButton>

using pinax::domain::MissingVolume;
using pinax::domain::ReadStatus;
using pinax::domain::SeriesDetail;
using pinax::domain::SeriesRow;
using pinax::domain::SeriesStatus;
using pinax::ui::SeriesEntryModel;
using pinax::ui::SeriesPage;
using pinax::ui::SeriesView;

namespace {

SeriesRow owned(std::int64_t entry, std::string position, std::int64_t book, std::string title,
    ReadStatus status = ReadStatus::Unread)
{
    SeriesRow row;
    row.entryId = entry;
    row.position = std::move(position);
    row.bookId = book;
    row.bookTitle = std::move(title);
    row.readStatus = status;
    return row;
}

SeriesRow missing(std::int64_t entry, std::optional<std::string> position, std::optional<std::string> title)
{
    SeriesRow row;
    row.entryId = entry;
    row.position = std::move(position);
    row.entryTitle = std::move(title);
    return row;
}

SeriesStatus culture()
{
    SeriesStatus s;
    s.id = 1;
    s.name = "The Culture";
    s.held = 2;
    s.known = 3;
    s.heldRead = 1;
    s.status = "Incomplete";
    return s;
}

std::vector<SeriesRow> cultureRows()
{
    return {missing(10, "1", "Consider Phlebas"), owned(11, "5", 105, "Excession", ReadStatus::Read),
        owned(12, "9", 109, "Surface Detail")};
}

QString cell(const SeriesEntryModel& model, int row, int column, int role = Qt::DisplayRole)
{
    return model.data(model.index(row, column), role).toString();
}

QString labelText(const QWidget& root, const QString& name)
{
    const auto* label = root.findChild<QLabel*>(name);
    return label ? label->text() : QStringLiteral("<no %1>").arg(name);
}

} // namespace

class TestSeriesPage : public QObject {
    Q_OBJECT

private slots:
    void missingVolumesSitInPlaceMarked();
    void theToggleHidesWhatIsNotOwned();
    void headingCountsHeldAndMissing();
    void selectionReportsOwnedAndMissing();
    void keysActOnlyOnOwnedVolumes();
    void refreshKeepsTheSelection();
    void panelDescribesOneVolumeShort();
    void panelListsSeveralMissing();
    void panelNeverClaimsConcluded();
    void panelCountsPlaceholders();
    void missingPageListsAndOpens();
};

void TestSeriesPage::missingVolumesSitInPlaceMarked()
{
    SeriesEntryModel model;
    model.setRows(cultureRows());
    QCOMPARE(model.rowCount(), 3);

    QCOMPARE(cell(model, 0, SeriesEntryModel::TitleColumn), QStringLiteral("Consider Phlebas"));
    QCOMPARE(cell(model, 0, SeriesEntryModel::StateColumn), QStringLiteral("not owned"));
    QCOMPARE(cell(model, 0, SeriesEntryModel::ReadColumn), QStringLiteral("◌"));
    QVERIFY(model.data(model.index(0, SeriesEntryModel::TitleColumn), Qt::FontRole).value<QFont>().italic());
    QCOMPARE(cell(model, 0, SeriesEntryModel::RatingColumn), QString());

    QCOMPARE(cell(model, 1, SeriesEntryModel::PositionColumn), QStringLiteral("5"));
    QCOMPARE(cell(model, 1, SeriesEntryModel::StateColumn), QStringLiteral("read"));
    QCOMPARE(cell(model, 1, SeriesEntryModel::ReadColumn), QStringLiteral("●"));
    QVERIFY(!model.data(model.index(1, SeriesEntryModel::TitleColumn), Qt::FontRole).isValid());
}

void TestSeriesPage::theToggleHidesWhatIsNotOwned()
{
    SeriesPage page;
    page.showSeries(culture(), cultureRows());
    QCOMPARE(page.entryModel()->rowCount(), 3);

    auto* toggle = page.findChild<QToolButton*>(QStringLiteral("series.showMissing"));
    toggle->click();
    QCOMPARE(page.entryModel()->rowCount(), 2);
    QCOMPARE(toggle->text(), QStringLiteral("Hiding volumes you don't own"));
    toggle->click();
    QCOMPARE(page.entryModel()->rowCount(), 3);
}

void TestSeriesPage::headingCountsHeldAndMissing()
{
    SeriesPage page;
    page.showSeries(culture(), cultureRows());
    QCOMPARE(labelText(page, QStringLiteral("series.heading")),
        QStringLiteral("<b>The Culture</b>&nbsp;&nbsp;3 entries · 2 held · 1 missing"));
}

void TestSeriesPage::selectionReportsOwnedAndMissing()
{
    SeriesPage page;
    page.showSeries(culture(), cultureRows());
    QList<QList<qint64>> books;
    QList<int> missingCounts;
    connect(&page, &SeriesPage::selectionChangedTo, this, [&](const QList<qint64>& ids, int missing) {
        books << ids;
        missingCounts << missing;
    });

    page.table()->selectRow(0);
    QCOMPARE(books.last(), QList<qint64>());
    QCOMPARE(missingCounts.last(), 1);

    page.selectBook(105);
    QCOMPARE(books.last(), QList<qint64>({105}));
    QCOMPARE(missingCounts.last(), 0);
}

void TestSeriesPage::keysActOnlyOnOwnedVolumes()
{
    SeriesPage page;
    page.showSeries(culture(), cultureRows());
    QSignalSpy toggled(page.table(), &pinax::ui::SeriesTable::toggleReadRequested);
    QSignalSpy rated(page.table(), &pinax::ui::SeriesTable::ratingRequested);

    page.table()->selectRow(0); // the missing volume
    QTest::keyClick(page.table(), Qt::Key_R);
    QTest::keyClick(page.table(), Qt::Key_7);
    QCOMPARE(toggled.count(), 0);
    QCOMPARE(rated.count(), 0);

    page.selectBook(109);
    QTest::keyClick(page.table(), Qt::Key_R);
    QCOMPARE(toggled.count(), 1);
    QCOMPARE(toggled.at(0).at(0).value<QList<qint64>>(), QList<qint64>({109}));

    page.table()->selectAll(); // owned and missing: the keys reach the owned
    QTest::keyClick(page.table(), Qt::Key_8);
    QCOMPARE(rated.count(), 1);
    QCOMPARE(rated.at(0).at(0).value<QList<qint64>>().size(), 2);
}

void TestSeriesPage::refreshKeepsTheSelection()
{
    SeriesPage page;
    page.showSeries(culture(), cultureRows());
    page.selectBook(109);

    auto rows = cultureRows();
    rows[2].readStatus = ReadStatus::Read; // Surface Detail toggled
    page.showSeries(culture(), rows);
    QCOMPARE(page.selectedBooks(), QList<qint64>({109}));
    QCOMPARE(cell(*page.entryModel(), 2, SeriesEntryModel::StateColumn), QStringLiteral("read"));

    // A different series starts with nothing selected.
    SeriesStatus other = culture();
    other.id = 2;
    page.showSeries(other, rows);
    QVERIFY(page.selectedBooks().isEmpty());
}

void TestSeriesPage::panelDescribesOneVolumeShort()
{
    SeriesDetail detail;
    detail.series = culture();
    detail.authors = "Iain M. Banks";
    detail.missing = {MissingVolume {std::string("1"), std::string("Consider Phlebas")}};
    detail.library = {28, 51, 93, 287};

    SeriesView view;
    view.showSeries(detail);
    QCOMPARE(labelText(view, QStringLiteral("seriesView.name")), QStringLiteral("The Culture"));
    QCOMPARE(labelText(view, QStringLiteral("seriesView.byline")), QStringLiteral("Iain M. Banks · 3 volumes"));
    QCOMPARE(labelText(view, QStringLiteral("seriesView.held")), QStringLiteral("2 of 3"));
    QCOMPARE(labelText(view, QStringLiteral("seriesView.legend")), QStringLiteral("1 read · 1 unread · 1 missing"));
    QCOMPARE(labelText(view, QStringLiteral("seriesView.missingHeading")), QStringLiteral("ONE VOLUME SHORT"));
    QVERIFY(labelText(view, QStringLiteral("seriesView.missing")).contains(QStringLiteral("Consider Phlebas")));
    QCOMPARE(labelText(view, QStringLiteral("seriesView.oneShort")), QStringLiteral("28"));
    QCOMPARE(labelText(view, QStringLiteral("seriesView.notOwned")), QStringLiteral("287"));
}

void TestSeriesPage::panelListsSeveralMissing()
{
    SeriesDetail detail;
    detail.series = culture();
    detail.series.known = 12;
    for (int i = 1; i <= 10; ++i)
        detail.missing.push_back({std::to_string(i), "Volume " + std::to_string(i)});

    SeriesView view;
    view.showSeries(detail);
    QCOMPARE(labelText(view, QStringLiteral("seriesView.missingHeading")), QStringLiteral("MISSING · 10"));
    const QString missing = labelText(view, QStringLiteral("seriesView.missing"));
    QVERIFY(missing.contains(QStringLiteral("Volume 8")));
    QVERIFY(!missing.contains(QStringLiteral("Volume 9")));
    QVERIFY(missing.endsWith(QStringLiteral("and 2 more")));
}

void TestSeriesPage::panelNeverClaimsConcluded()
{
    SeriesDetail detail;
    detail.series = culture();
    detail.series.held = detail.series.known = 3;
    detail.series.status = "Complete";

    SeriesView view;
    view.showSeries(detail);
    QCOMPARE(labelText(view, QStringLiteral("seriesView.byline")), QStringLiteral("3 volumes"));
    QCOMPARE(labelText(view, QStringLiteral("seriesView.missing")), QStringLiteral("Nothing. The series is complete."));

    detail.series.ongoing = true;
    detail.series.status = "Complete to date";
    view.showSeries(detail);
    QCOMPARE(labelText(view, QStringLiteral("seriesView.byline")), QStringLiteral("3 volumes · still being written"));
}

void TestSeriesPage::panelCountsPlaceholders()
{
    // IMP-005: Discworld's 14 unnamed gaps are one line, not eight names.
    SeriesDetail detail;
    detail.series = culture();
    for (int i = 1; i <= 14; ++i)
        detail.missing.push_back({std::nullopt, "Unidentified volume " + std::to_string(i)});

    SeriesView view;
    view.showSeries(detail);
    QCOMPARE(labelText(view, QStringLiteral("seriesView.missing")), QStringLiteral("14 volumes, not yet identified"));

    detail.missing.insert(detail.missing.begin(), MissingVolume {std::string("1"), std::string("Consider Phlebas")});
    view.showSeries(detail);
    const QString text = labelText(view, QStringLiteral("seriesView.missing"));
    QVERIFY2(text.startsWith(QStringLiteral("Consider Phlebas")), qPrintable(text));
    QVERIFY2(text.endsWith(QStringLiteral("<br>and 14 volumes not yet identified")), qPrintable(text));
}

void TestSeriesPage::missingPageListsAndOpens()
{
    auto volume = [](std::int64_t entry, std::int64_t series, std::string name, std::optional<std::string> title,
                      int needs) {
        pinax::domain::MissingRow row;
        row.entryId = entry;
        row.seriesId = series;
        row.seriesName = std::move(name);
        row.position = std::string("1");
        row.title = std::move(title);
        row.missingInSeries = needs;
        return row;
    };
    pinax::ui::MissingPage page;
    page.showRows({volume(10, 1, "The Culture", "Consider Phlebas", 1),
                      volume(20, 2, "Discworld", "Unidentified volume 1", 2),
                      volume(21, 2, "Discworld", "Unidentified volume 2", 2)},
        false);

    QCOMPARE(labelText(page, QStringLiteral("missing.heading")),
        QStringLiteral("<b>Missing volumes</b>&nbsp;&nbsp;3 across 2 series, fewest needed first"));
    const auto* model = page.missingModel();
    QCOMPARE(model->index(0, pinax::ui::MissingModel::TitleColumn).data().toString(), QStringLiteral("Consider Phlebas"));
    QCOMPARE(model->index(1, pinax::ui::MissingModel::NeedsColumn).data().toString(), QStringLiteral("2"));

    std::optional<pinax::domain::MissingRow> selected;
    connect(&page, &pinax::ui::MissingPage::selectionChangedTo, this,
        [&](const std::optional<pinax::domain::MissingRow>& row) { selected = row; });
    page.table()->selectRow(0);
    QVERIFY(selected);
    QCOMPARE(selected->entryId, std::int64_t(10));

    QSignalSpy open(&page, &pinax::ui::MissingPage::openSeriesRequested);
    emit page.table()->activated(model->index(1, 0));
    QCOMPARE(open.at(0).at(0).toLongLong(), qint64(2));

    // A refresh keeps the volume selected while it is still missing.
    page.showRows({volume(10, 1, "The Culture", "Consider Phlebas", 1)}, true);
    QCOMPARE(page.selectedVolume()->entryId, std::int64_t(10));
    QCOMPARE(labelText(page, QStringLiteral("missing.heading")),
        QStringLiteral("<b>One volume short</b>&nbsp;&nbsp;1 series, each needing one"));
}

QTEST_MAIN(TestSeriesPage)
#include "test_series_page.moc"
