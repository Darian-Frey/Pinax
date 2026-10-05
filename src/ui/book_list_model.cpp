#include "ui/book_list_model.h"

#include <QColor>

#include <utility>

namespace pinax::ui {

using domain::BookSummary;
using domain::ReadStatus;

namespace {

// The mock-up's accent, used for the read mark.
const QColor accent(0xD9, 0xA4, 0x41);

QString text(const std::optional<std::string>& value)
{
    return value ? QString::fromStdString(*value) : QString();
}

QString readMark(ReadStatus status)
{
    switch (status) {
    case ReadStatus::Read: return QStringLiteral("●");
    case ReadStatus::Reading: return QStringLiteral("◐");
    case ReadStatus::Abandoned: return QStringLiteral("×");
    case ReadStatus::Unread: break;
    }
    return QStringLiteral("○");
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
        }
        break;

    case Qt::ToolTipRole:
        if (index.column() == ReadColumn)
            return readDescription(row);
        break;

    case Qt::ForegroundRole:
        if (index.column() == ReadColumn && row.readStatus == ReadStatus::Read)
            return accent;
        break;

    case Qt::TextAlignmentRole:
        switch (index.column()) {
        case ReadColumn: return int(Qt::AlignCenter);
        case RatingColumn:
        case YearColumn: return int(Qt::AlignRight | Qt::AlignVCenter);
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
        }
    }
    if (role == Qt::ToolTipRole && section == ReadColumn)
        return tr("Read state");
    if (role == Qt::TextAlignmentRole && (section == RatingColumn || section == YearColumn))
        return int(Qt::AlignRight | Qt::AlignVCenter);
    return {};
}

} // namespace pinax::ui
