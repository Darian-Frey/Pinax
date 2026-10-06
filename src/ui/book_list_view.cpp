#include "ui/book_list_view.h"

#include "ui/book_list_model.h"
#include "ui/book_sort_proxy.h"
#include "ui/list_keys.h"

#include <QHeaderView>
#include <QKeyEvent>
#include <QItemSelectionModel>

#include <utility>

namespace pinax::ui {

namespace {

// Row height and fixed column widths from the mock-up in design/.
constexpr int defaultRowHeight = 34;
constexpr int defaultReadWidth = 34;
constexpr int defaultAuthorWidth = 130;
constexpr int defaultSeriesWidth = 140;
constexpr int defaultRatingWidth = 56;
constexpr int defaultYearWidth = 48;
constexpr int defaultReadsWidth = 52;

} // namespace

BookListView::BookListView(QWidget* parent)
    : QTableView(parent)
    , model_(new BookListModel(this))
    , proxy_(new BookSortProxy(this))
{
    proxy_->setSourceModel(model_);
    setModel(proxy_);

    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setShowGrid(false);
    setWordWrap(false);
    setTextElideMode(Qt::ElideRight);
    setEditTriggers(QAbstractItemView::NoEditTriggers);

    verticalHeader()->hide();
    verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    verticalHeader()->setDefaultSectionSize(defaultRowHeight);

    QHeaderView* header = horizontalHeader();
    header->setHighlightSections(false);
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setSectionResizeMode(BookListModel::ReadColumn, QHeaderView::Fixed);
    header->setSectionResizeMode(BookListModel::TitleColumn, QHeaderView::Stretch);
    header->resizeSection(BookListModel::ReadColumn, defaultReadWidth);
    header->resizeSection(BookListModel::AuthorColumn, defaultAuthorWidth);
    header->resizeSection(BookListModel::SeriesColumn, defaultSeriesWidth);
    header->resizeSection(BookListModel::RatingColumn, defaultRatingWidth);
    header->resizeSection(BookListModel::YearColumn, defaultYearWidth);
    header->resizeSection(BookListModel::TimesReadColumn, defaultReadsWidth);

    setSortingEnabled(true);
    sortByColumn(BookListModel::AuthorColumn, Qt::AscendingOrder);

    connect(selectionModel(), &QItemSelectionModel::selectionChanged, this,
        [this] { emit selectionChangedTo(selectedBooks()); });
    // A reset clears the selection without saying so.
    connect(model_, &QAbstractItemModel::modelReset, this,
        [this] { emit selectionChangedTo(selectedBooks()); });
}

void BookListView::setBooks(std::vector<domain::BookSummary> books)
{
    model_->setBooks(std::move(books));
}

void BookListView::updateBook(const domain::BookSummary& summary)
{
    model_->updateBook(summary);
}

void BookListView::selectBook(std::int64_t id)
{
    const int row = model_->rowOf(id);
    if (row < 0)
        return;
    const QModelIndex index = proxy_->mapFromSource(model_->index(row, BookListModel::TitleColumn));
    selectionModel()->setCurrentIndex(index,
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    scrollTo(index);
}

void BookListView::keyPressEvent(QKeyEvent* event)
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
    QTableView::keyPressEvent(event);
}

void BookListView::showOnly(const std::optional<QList<qint64>>& ids)
{
    if (ids)
        proxy_->showOnly(QSet<qint64>(ids->begin(), ids->end()));
    else
        proxy_->showOnly(std::nullopt);
}

int BookListView::shownCount() const
{
    return proxy_->rowCount();
}

QList<qint64> BookListView::selectedBooks() const
{
    QList<qint64> ids;
    const QModelIndexList rows = selectionModel()->selectedRows();
    for (const QModelIndex& index : rows)
        ids << model_->book(proxy_->mapToSource(index).row()).id;
    return ids;
}

} // namespace pinax::ui
