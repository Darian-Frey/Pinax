#include "ui/book_group_proxy.h"

#include "domain/sort_title.h"

#include <QFont>
#include <QPalette>

#include <algorithm>
#include <cctype>

namespace pinax::ui {

namespace {

// Groups sort case-blind, filed as titles; the books in no group come last.
std::string filingKey(const std::string& name)
{
    std::string key = domain::makeSortTitle(name);
    for (char& c : key)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return key;
}

const std::string noGroup = "\x7f"; // after every letter

// The first-billed name from the list's author text: "Larry Niven & Jerry
// Pournelle" -> "Larry Niven"; an editor keeps his "(ed.)".
std::string firstAuthor(const std::string& authors)
{
    const auto joint = authors.find(" & ");
    std::string first = authors.substr(0, joint);
    if (joint != std::string::npos) {
        // "A & B (eds.)": the role belongs to both.
        const auto role = authors.rfind(" (");
        if (role != std::string::npos && role > joint)
            first += authors.substr(role) == " (eds.)" ? " (ed.)" : authors.substr(role);
    }
    return first;
}

} // namespace

BookGroupProxy::BookGroupProxy(QObject* parent)
    : QAbstractProxyModel(parent)
{
}

void BookGroupProxy::setSummaryLookup(std::function<const domain::BookSummary&(int)> lookup)
{
    lookup_ = std::move(lookup);
}

void BookGroupProxy::setGrouping(Grouping grouping, std::map<std::int64_t, std::vector<std::string>> genres)
{
    grouping_ = grouping;
    genres_ = std::move(genres);
    rebuild(true);
}

void BookGroupProxy::setSourceModel(QAbstractItemModel* source)
{
    if (sourceModel())
        disconnect(sourceModel(), nullptr, this, nullptr);
    beginResetModel();
    QAbstractProxyModel::setSourceModel(source);
    rows_ = layOut(headers_);
    endResetModel();
    emit groupsChanged();
    if (!source)
        return;
    auto reset = [this] {
        beginResetModel();
        rows_ = layOut(headers_);
        endResetModel();
        emit groupsChanged();
    };
    connect(source, &QAbstractItemModel::modelReset, this, reset);
    connect(source, &QAbstractItemModel::rowsInserted, this, reset);
    connect(source, &QAbstractItemModel::rowsRemoved, this, reset);
    connect(source, &QAbstractItemModel::layoutChanged, this, [this] { rebuild(true); });
    connect(source, &QAbstractItemModel::dataChanged, this,
        [this](const QModelIndex& topLeft, const QModelIndex& bottomRight) {
            if (grouping_ != Grouping::None) {
                // A changed book may change group.
                rebuild(true);
                return;
            }
            emit dataChanged(index(topLeft.row(), topLeft.column()), index(bottomRight.row(), bottomRight.column()));
        });
    connect(source, &QAbstractItemModel::headerDataChanged, this, &QAbstractItemModel::headerDataChanged);
}

void BookGroupProxy::rebuild(bool keepPersistent)
{
    if (!keepPersistent) {
        beginResetModel();
        rows_ = layOut(headers_);
        endResetModel();
        emit groupsChanged();
        return;
    }
    emit layoutAboutToBeChanged();
    const QModelIndexList before = persistentIndexList();
    QList<QPersistentModelIndex> sources;
    for (const QModelIndex& index : before)
        sources << QPersistentModelIndex(mapToSource(index));
    rows_ = layOut(headers_);
    QModelIndexList after;
    for (int i = 0; i < before.size(); ++i)
        after << (sources[i].isValid() ? mapFromSource(sources[i]) : QModelIndex());
    changePersistentIndexList(before, after);
    emit layoutChanged();
    emit groupsChanged();
}

std::vector<BookGroupProxy::Row> BookGroupProxy::layOut(std::vector<Header>& headers) const
{
    headers.clear();
    std::vector<Row> rows;
    const int sourceRows = sourceModel() ? sourceModel()->rowCount() : 0;
    if (grouping_ == Grouping::None || !lookup_) {
        for (int row = 0; row < sourceRows; ++row)
            rows.push_back({row, -1});
        return rows;
    }

    // Each book's groups, in the source's order, bucketed by filing key.
    struct Bucket {
        std::string label;
        std::vector<int> sourceRows;
    };
    std::map<std::string, Bucket> buckets;
    for (int row = 0; row < sourceRows; ++row) {
        const domain::BookSummary& book = lookup_(row);
        std::vector<std::string> names;
        switch (grouping_) {
        case Grouping::Series:
            if (book.seriesSort)
                names.push_back(*book.seriesSort);
            break;
        case Grouping::Author:
            if (book.authors)
                names.push_back(firstAuthor(*book.authors));
            break;
        case Grouping::Genre:
            if (const auto found = genres_.find(book.id); found != genres_.end())
                names = found->second;
            break;
        case Grouping::None:
            break;
        }
        if (names.empty()) {
            auto& bucket = buckets[noGroup];
            bucket.sourceRows.push_back(row);
            continue;
        }
        for (const auto& name : names) {
            // Authors file by surname, as the Author column sorts.
            const std::string key = grouping_ == Grouping::Author && book.authorSort ? filingKey(*book.authorSort)
                                                                                      : filingKey(name);
            auto& bucket = buckets[key];
            if (bucket.label.empty())
                bucket.label = name;
            bucket.sourceRows.push_back(row);
        }
    }

    for (auto& [key, bucket] : buckets) {
        QString label = QString::fromStdString(bucket.label);
        if (key == noGroup) {
            switch (grouping_) {
            case Grouping::Series: label = tr("Not in a series"); break;
            case Grouping::Author: label = tr("No author"); break;
            case Grouping::Genre: label = tr("No genre"); break;
            case Grouping::None: break;
            }
        }
        const int group = static_cast<int>(headers.size());
        headers.push_back({label, static_cast<int>(bucket.sourceRows.size())});
        rows.push_back({-1, group});
        for (const int row : bucket.sourceRows)
            rows.push_back({row, group});
    }
    return rows;
}

bool BookGroupProxy::isHeader(int row) const
{
    return row >= 0 && row < static_cast<int>(rows_.size()) && rows_[static_cast<std::size_t>(row)].sourceRow < 0;
}

QModelIndex BookGroupProxy::index(int row, int column, const QModelIndex& parent) const
{
    if (parent.isValid() || row < 0 || row >= rowCount() || column < 0 || column >= columnCount())
        return {};
    return createIndex(row, column);
}

QModelIndex BookGroupProxy::parent(const QModelIndex&) const
{
    return {};
}

int BookGroupProxy::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

int BookGroupProxy::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() || !sourceModel() ? 0 : sourceModel()->columnCount();
}

QModelIndex BookGroupProxy::mapToSource(const QModelIndex& proxyIndex) const
{
    if (!proxyIndex.isValid() || !sourceModel() || isHeader(proxyIndex.row())
        || proxyIndex.row() >= static_cast<int>(rows_.size()))
        return {};
    return sourceModel()->index(rows_[static_cast<std::size_t>(proxyIndex.row())].sourceRow, proxyIndex.column());
}

QModelIndex BookGroupProxy::mapFromSource(const QModelIndex& sourceIndex) const
{
    if (!sourceIndex.isValid())
        return {};
    // A book under several genres: its first appearance.
    for (std::size_t row = 0; row < rows_.size(); ++row) {
        if (rows_[row].sourceRow == sourceIndex.row())
            return index(static_cast<int>(row), sourceIndex.column());
    }
    return {};
}

QVariant BookGroupProxy::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};
    if (!isHeader(index.row()))
        return QAbstractProxyModel::data(index, role);
    const Header& header = headers_[static_cast<std::size_t>(rows_[static_cast<std::size_t>(index.row())].group)];
    switch (role) {
    case Qt::DisplayRole:
        return index.column() == 0 ? QStringLiteral("%1 · %2").arg(header.label).arg(header.count) : QVariant();
    case Qt::FontRole: {
        QFont font;
        font.setBold(true);
        return font;
    }
    case Qt::BackgroundRole:
        return QPalette().alternateBase();
    case Qt::TextAlignmentRole:
        return int(Qt::AlignLeft | Qt::AlignVCenter);
    default:
        return {};
    }
}

Qt::ItemFlags BookGroupProxy::flags(const QModelIndex& index) const
{
    if (index.isValid() && isHeader(index.row()))
        return Qt::ItemIsEnabled; // seen, never selected
    return QAbstractProxyModel::flags(index);
}

QVariant BookGroupProxy::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (!sourceModel() || orientation != Qt::Horizontal)
        return {};
    return sourceModel()->headerData(section, orientation, role);
}

void BookGroupProxy::sort(int column, Qt::SortOrder order)
{
    if (sourceModel())
        sourceModel()->sort(column, order);
}

} // namespace pinax::ui
