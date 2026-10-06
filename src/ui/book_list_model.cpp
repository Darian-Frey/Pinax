#include "ui/book_list_model.h"

#include "ui/style.h"

#include <QColor>

#include <utility>

namespace pinax::ui {

using domain::BookSummary;
using domain::ReadStatus;

namespace {

QString text(const std::optional<std::string>& value)
{
    return value ? QString::fromStdString(*value) : QString();
}

QString readDescription(const BookSummary& book)
{
    switch (book.readStatus) {
    case ReadStatus::Read:
        return book.timesRead > 1
            ? BookListModel::tr("Read %1 times").arg(book.timesRead)
            : BookListModel::tr("Read");
    case ReadStatus::Reading: return BookListModel::tr("Reading");
    case ReadStatus::Abandoned: return BookListModel::tr("Abandoned");
    case ReadStatus::Unread: break;
    }
    return BookListModel::tr("Unread");
}

} // namespace

BookListModel::BookListModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

void BookListModel::setBooks(std::vector<BookSummary> books)
{
    beginResetModel();
    books_ = std::move(books);
    endResetModel();
}

const BookSummary& BookListModel::book(int row) const
{
    return books_.at(static_cast<std::size_t>(row));
}

void BookListModel::updateBook(const BookSummary& summary)
{
    const int row = rowOf(summary.id);
    if (row < 0)
        return;
    books_[static_cast<std::size_t>(row)] = summary;
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
}

int BookListModel::rowOf(std::int64_t id) const
{
    for (std::size_t row = 0; row < books_.size(); ++row) {
        if (books_[row].id == id)
            return static_cast<int>(row);
    }
    return -1;
}

int BookListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(books_.size());
}

int BookListModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant BookListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount())
        return {};
    const BookSummary& row = book(index.row());

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case ReadColumn: return readMark(row.readStatus);
        case TitleColumn: return QString::fromStdString(row.title);
        case AuthorColumn: return text(row.authors);
        case SeriesColumn: return text(row.seriesLabel);
        case RatingColumn: return row.rating ? QString::number(*row.rating) : QStringLiteral("–");
        case YearColumn: return row.publishedYear ? QString::number(*row.publishedYear) : QString();
        case TimesReadColumn: return row.timesRead > 0 ? QString::number(row.timesRead) : QString();
        }
        break;

    case Qt::ToolTipRole:
        if (index.column() == ReadColumn || index.column() == TimesReadColumn)
            return readDescription(row);
        break;

    case Qt::ForegroundRole:
        if (index.column() == ReadColumn && row.readStatus == ReadStatus::Read)
            return accent();
        break;

    case Qt::TextAlignmentRole:
        switch (index.column()) {
        case ReadColumn: return int(Qt::AlignCenter);
        case RatingColumn:
        case YearColumn:
        case TimesReadColumn: return int(Qt::AlignRight | Qt::AlignVCenter);
        }
        return int(Qt::AlignLeft | Qt::AlignVCenter);
    }
    return {};
}

QVariant BookListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal)
        return {};

    if (role == Qt::DisplayRole) {
        switch (section) {
        case ReadColumn: return QString();
        case TitleColumn: return tr("Title");
        case AuthorColumn: return tr("Author");
        case SeriesColumn: return tr("Series");
        case RatingColumn: return tr("Rating");
        case YearColumn: return tr("Year");
        case TimesReadColumn: return tr("Reads");
        }
    }
    if (role == Qt::ToolTipRole && section == ReadColumn)
        return tr("Read state");
    if (role == Qt::ToolTipRole && section == TimesReadColumn)
        return tr("Times read");
    if (role == Qt::TextAlignmentRole
        && (section == RatingColumn || section == YearColumn || section == TimesReadColumn))
        return int(Qt::AlignRight | Qt::AlignVCenter);
    return {};
}

} // namespace pinax::ui
