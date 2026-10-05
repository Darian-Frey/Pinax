#include "ui/book_list_view.h"

#include "ui/book_list_model.h"
#include "ui/book_sort_proxy.h"

#include <QHeaderView>

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

    setSortingEnabled(true);
    sortByColumn(BookListModel::AuthorColumn, Qt::AscendingOrder);
}

void BookListView::setBooks(std::vector<domain::BookSummary> books)
{
    model_->setBooks(std::move(books));
}

} // namespace pinax::ui
