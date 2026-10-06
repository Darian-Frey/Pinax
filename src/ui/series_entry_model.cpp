#include "ui/series_entry_model.h"

#include "ui/style.h"

#include <QFont>

#include <utility>

namespace pinax::ui {

using domain::ReadStatus;
using domain::SeriesRow;

namespace {

QString stateText(const SeriesRow& row)
{
    if (!row.owned())
        return SeriesEntryModel::tr("not owned");
    switch (row.readStatus) {
    case ReadStatus::Read: return SeriesEntryModel::tr("read");
    case ReadStatus::Reading: return SeriesEntryModel::tr("reading");
    case ReadStatus::Abandoned: return SeriesEntryModel::tr("abandoned");
    case ReadStatus::Unread: break;
    }
    return SeriesEntryModel::tr("unread");
}

} // namespace

SeriesEntryModel::SeriesEntryModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

void SeriesEntryModel::setRows(std::vector<SeriesRow> rows)
{
    beginResetModel();
    rows_ = std::move(rows);
    rebuildShown();
    endResetModel();
}

void SeriesEntryModel::setShowMissing(bool show)
{
    if (show == showMissing_)
        return;
    beginResetModel();
    showMissing_ = show;
    rebuildShown();
    endResetModel();
}

void SeriesEntryModel::rebuildShown()
{
    shown_.clear();
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (showMissing_ || rows_[i].owned())
            shown_.push_back(i);
    }
}

const SeriesRow& SeriesEntryModel::row(int index) const
{
    return rows_.at(shown_.at(static_cast<std::size_t>(index)));
}

int SeriesEntryModel::rowOfEntry(std::int64_t entryId) const
{
    for (std::size_t i = 0; i < shown_.size(); ++i) {
        if (rows_[shown_[i]].entryId == entryId)
            return static_cast<int>(i);
    }
    return -1;
}

int SeriesEntryModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(shown_.size());
}

int SeriesEntryModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant SeriesEntryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount())
        return {};
    const SeriesRow& entry = row(index.row());
    const bool missing = !entry.owned();

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case ReadColumn: return missing ? missingMark() : readMark(entry.readStatus);
        case PositionColumn: return entry.position ? QString::fromStdString(*entry.position) : QString();
        case TitleColumn: {
            const auto title = entry.title();
            return title ? QString::fromStdString(*title) : tr("Untitled volume");
        }
        case StateColumn: return stateText(entry);
        case RatingColumn:
            if (missing)
                return QString();
            return entry.rating ? QString::number(*entry.rating) : QStringLiteral("–");
        case YearColumn: return entry.publishedYear ? QString::number(*entry.publishedYear) : QString();
        }
        break;

    case Qt::FontRole:
        if (missing && index.column() == TitleColumn) {
            QFont font;
            font.setItalic(true);
            return font;
        }
        if (missing && index.column() == StateColumn) {
            QFont font;
            font.setBold(true);
            font.setCapitalization(QFont::AllUppercase);
            font.setPointSizeF(font.pointSizeF() * 0.8);
            return font;
        }
        break;

    case Qt::ForegroundRole:
        if (missing && (index.column() == StateColumn || index.column() == ReadColumn))
            return accent();
        if (!missing && index.column() == ReadColumn && entry.readStatus == ReadStatus::Read)
            return accent();
        break;

    case Qt::ToolTipRole:
        if (missing)
            return tr("Known to be in the series, not on the shelf");
        break;

    case Qt::TextAlignmentRole:
        switch (index.column()) {
        case ReadColumn: return int(Qt::AlignCenter);
        case PositionColumn:
        case RatingColumn:
        case YearColumn: return int(Qt::AlignRight | Qt::AlignVCenter);
        }
        return int(Qt::AlignLeft | Qt::AlignVCenter);
    }
    return {};
}

QVariant SeriesEntryModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal)
        return {};
    if (role == Qt::DisplayRole) {
        switch (section) {
        case ReadColumn: return QString();
        case PositionColumn: return tr("No.");
        case TitleColumn: return tr("Title");
        case StateColumn: return tr("State");
        case RatingColumn: return tr("Rating");
        case YearColumn: return tr("Year");
        }
    }
    if (role == Qt::TextAlignmentRole
        && (section == PositionColumn || section == RatingColumn || section == YearColumn))
        return int(Qt::AlignRight | Qt::AlignVCenter);
    return {};
}

} // namespace pinax::ui
