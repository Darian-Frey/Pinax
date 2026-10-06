#include "ui/series_page.h"

#include "ui/list_keys.h"
#include "ui/series_entry_model.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QSet>
#include <QToolButton>
#include <QVBoxLayout>

#include <utility>

namespace pinax::ui {

namespace {

constexpr int defaultRowHeight = 34;

QString headingText(const domain::SeriesStatus& series)
{
    const int missing = series.known - series.held;
    QString text = QStringLiteral("<b>%1</b>&nbsp;&nbsp;").arg(QString::fromStdString(series.name).toHtmlEscaped());
    text += SeriesPage::tr("%1 entries · %2 held").arg(series.known).arg(series.held);
    if (missing > 0)
        text += SeriesPage::tr(" · %1 missing").arg(missing);
    if (series.ongoing)
        text += SeriesPage::tr(" · still being written");
    return text;
}

} // namespace

SeriesTable::SeriesTable(SeriesEntryModel* model, QWidget* parent)
    : QTableView(parent)
    , model_(model)
{
    setModel(model_);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setShowGrid(false);
    setWordWrap(false);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    verticalHeader()->hide();
    verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    verticalHeader()->setDefaultSectionSize(defaultRowHeight);

    QHeaderView* header = horizontalHeader();
    header->setHighlightSections(false);
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setSectionResizeMode(SeriesEntryModel::TitleColumn, QHeaderView::Stretch);
    header->resizeSection(SeriesEntryModel::ReadColumn, 34);
    header->resizeSection(SeriesEntryModel::PositionColumn, 64);
    header->resizeSection(SeriesEntryModel::StateColumn, 110);
    header->resizeSection(SeriesEntryModel::RatingColumn, 56);
    header->resizeSection(SeriesEntryModel::YearColumn, 48);
}

QList<qint64> SeriesTable::selectedBooks() const
{
    QList<qint64> ids;
    for (const QModelIndex& index : selectionModel()->selectedRows()) {
        const auto& row = model_->row(index.row());
        if (row.bookId)
            ids << *row.bookId;
    }
    return ids;
}

int SeriesTable::selectedMissing() const
{
    int missing = 0;
    for (const QModelIndex& index : selectionModel()->selectedRows()) {
        if (!model_->row(index.row()).owned())
            ++missing;
    }
    return missing;
}

void SeriesTable::keyPressEvent(QKeyEvent* event)
{
    const QList<qint64> ids = selectedBooks();
    const ListKeyAction action = listKeyAction(event);
    if (!ids.isEmpty()) {
        switch (action.kind) {
        case ListKeyAction::Kind::ToggleRead: emit toggleReadRequested(ids); return;
        case ListKeyAction::Kind::Rate: emit ratingRequested(ids, action.rating); return;
        case ListKeyAction::Kind::Delete: emit deleteRequested(ids); return;
        case ListKeyAction::Kind::None: break;
        }
    }
    // A key meant for the selection never falls through to the table's own
    // keyboard search when only missing volumes are selected.
    if (action.kind != ListKeyAction::Kind::None)
        return;
    QTableView::keyPressEvent(event);
}

SeriesPage::SeriesPage(QWidget* parent)
    : QWidget(parent)
    , heading_(new QLabel(this))
    , showMissing_(new QToolButton(this))
    , model_(new SeriesEntryModel(this))
    , table_(new SeriesTable(model_, this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(10, 6, 10, 6);
    heading_->setObjectName(QStringLiteral("series.heading"));
    heading_->setTextFormat(Qt::RichText);
    showMissing_->setObjectName(QStringLiteral("series.showMissing"));
    showMissing_->setCheckable(true);
    showMissing_->setChecked(true);
    showMissing_->setText(tr("Showing volumes you don't own"));
    showMissing_->setToolTip(tr("Show or hide the volumes the series contains but the shelf does not"));
    bar->addWidget(heading_, 1);
    bar->addWidget(showMissing_);
    layout->addLayout(bar);
    layout->addWidget(table_, 1);

    connect(showMissing_, &QToolButton::toggled, this, [this](bool show) {
        showMissing_->setText(show ? tr("Showing volumes you don't own") : tr("Hiding volumes you don't own"));
        model_->setShowMissing(show);
    });
    auto report = [this] { emit selectionChangedTo(table_->selectedBooks(), table_->selectedMissing()); };
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this, report);
    connect(model_, &QAbstractItemModel::modelReset, this, report);
}

void SeriesPage::showSeries(const domain::SeriesStatus& series, std::vector<domain::SeriesRow> rows)
{
    // Remember the selection by entry, to restore it after a refresh.
    QSet<qint64> selected;
    if (series.id == seriesId_) {
        for (const QModelIndex& index : table_->selectionModel()->selectedRows())
            selected.insert(model_->row(index.row()).entryId);
    }
    seriesId_ = series.id;
    heading_->setText(headingText(series));

    const QSignalBlocker block(table_->selectionModel());
    model_->setRows(std::move(rows));
    for (const qint64 entryId : selected) {
        const int row = model_->rowOfEntry(entryId);
        if (row >= 0)
            table_->selectionModel()->select(model_->index(row, 0),
                QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
}

void SeriesPage::selectBook(std::int64_t bookId)
{
    for (int row = 0; row < model_->rowCount(); ++row) {
        if (model_->row(row).bookId == bookId) {
            const QModelIndex index = model_->index(row, SeriesEntryModel::TitleColumn);
            table_->selectionModel()->setCurrentIndex(index,
                QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            table_->scrollTo(index);
            return;
        }
    }
}

} // namespace pinax::ui
