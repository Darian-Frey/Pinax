#include "ui/book_sort_proxy.h"

#include "ui/book_list_model.h"

#include <optional>
#include <string>

namespace pinax::ui {

using domain::BookSummary;
using domain::ReadStatus;

namespace {

int compareText(const std::string& a, const std::string& b)
{
    return QString::fromStdString(a).compare(QString::fromStdString(b), Qt::CaseInsensitive);
}

template <typename T>
int compareValue(const T& a, const T& b)
{
    return a < b ? -1 : (b < a ? 1 : 0);
}

// Qt sorts descending by asking lessThan(right, left), so a missing value
// must count as largest when ascending and smallest when descending to stay
// at the bottom either way.
template <typename T, typename Compare>
int compareOptional(const std::optional<T>& a, const std::optional<T>& b, bool descending,
    Compare compare)
{
    if (a && b)
        return compare(*a, *b);
    if (!a && !b)
        return 0;
    const int missingLast = descending ? -1 : 1;
    return a ? -missingLast : missingLast;
}

template <typename T>
int compareOptional(const std::optional<T>& a, const std::optional<T>& b, bool descending)
{
    return compareOptional(a, b, descending, compareValue<T>);
}

int readRank(ReadStatus status)
{
    switch (status) {
    case ReadStatus::Unread: return 0;
    case ReadStatus::Reading: return 1;
    case ReadStatus::Read: return 2;
    case ReadStatus::Abandoned: return 3;
    }
    return 0;
}

// Series name, then sort_position within it; missing either sorts last.
int compareSeries(const BookSummary& a, const BookSummary& b, bool descending)
{
    const int order = compareOptional(a.seriesSort, b.seriesSort, descending, compareText);
    if (order != 0)
        return order;
    return compareOptional(a.seriesSortPosition, b.seriesSortPosition, descending);
}

int compareBooks(int column, const BookSummary& a, const BookSummary& b, bool descending)
{
    int order = 0;
    switch (column) {
    case BookListModel::ReadColumn:
        order = compareValue(readRank(a.readStatus), readRank(b.readStatus));
        break;
    case BookListModel::AuthorColumn:
        // Within one author, their series stay together in order, and
        // standalones follow (IMP-001).
        order = compareOptional(a.authorSort, b.authorSort, descending, compareText);
        if (order == 0)
            order = compareSeries(a, b, descending);
        break;
    case BookListModel::SeriesColumn:
        order = compareSeries(a, b, descending);
        break;
    case BookListModel::RatingColumn:
        order = compareOptional(a.rating, b.rating, descending);
        break;
    case BookListModel::YearColumn:
        order = compareOptional(a.publishedYear, b.publishedYear, descending);
        break;
    case BookListModel::TimesReadColumn:
        order = compareValue(a.timesRead, b.timesRead);
        break;
    case BookListModel::TitleColumn:
        break;
    }
    if (order == 0)
        order = compareText(a.sortTitle, b.sortTitle);
    if (order == 0)
        order = compareValue(a.id, b.id);
    return order;
}

} // namespace

bool BookSortProxy::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
    const auto* books = qobject_cast<const BookListModel*>(sourceModel());
    if (!books)
        return QSortFilterProxyModel::lessThan(left, right);

    const bool descending = sortOrder() == Qt::DescendingOrder;
    return compareBooks(left.column(), books->book(left.row()), books->book(right.row()),
               descending)
        < 0;
}

} // namespace pinax::ui
