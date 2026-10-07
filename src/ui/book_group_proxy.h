#pragma once

#include "domain/book_summary.h"

#include <QAbstractProxyModel>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace pinax::ui {

enum class Grouping { None, Series, Author, Genre };

// Groups the sorted, filtered book list under header rows — "Discworld · 27"
// — by series, first-billed author or genre (F-018). Sits above the sort
// proxy: within a group, books keep its order; groups run by name, filed as
// titles are, with the books in no group last. A book with several genres
// appears under each. Header rows cannot be selected and map to no book.
// With no grouping it passes every row straight through.
class BookGroupProxy : public QAbstractProxyModel {
    Q_OBJECT

public:
    explicit BookGroupProxy(QObject* parent = nullptr);

    // How to reach a book from a row of the source.
    void setSummaryLookup(std::function<const domain::BookSummary&(int sourceRow)> lookup);

    // `genres` maps a book to its genres, for Grouping::Genre.
    void setGrouping(Grouping grouping, std::map<std::int64_t, std::vector<std::string>> genres = {});
    Grouping grouping() const { return grouping_; }

    bool isHeader(int row) const;
    int groupCount() const { return static_cast<int>(headers_.size()); }

    void setSourceModel(QAbstractItemModel* source) override;
    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QModelIndex mapToSource(const QModelIndex& proxyIndex) const override;
    QModelIndex mapFromSource(const QModelIndex& sourceIndex) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    // Straight from the source: the base class finds a column's title
    // through row 0, which may be a header row.
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

signals:
    // The header rows moved or changed: spans must be laid again.
    void groupsChanged();

private:
    struct Row {
        int sourceRow = -1; // -1: a header
        int group = -1;
    };
    struct Header {
        QString label;
        int count = 0;
    };

    // Recomputes the rows; `keepPersistent` carries selections across.
    void rebuild(bool keepPersistent);
    std::vector<Row> layOut(std::vector<Header>& headers) const;

    std::function<const domain::BookSummary&(int)> lookup_;
    Grouping grouping_ = Grouping::None;
    std::map<std::int64_t, std::vector<std::string>> genres_;
    std::vector<Row> rows_;
    std::vector<Header> headers_;
};

} // namespace pinax::ui
