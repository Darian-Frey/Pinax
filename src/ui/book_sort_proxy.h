#pragma once

#include <QSet>
#include <QSortFilterProxyModel>

#include <optional>

namespace pinax::ui {

// Sorts a BookListModel by the keys the database supplies rather than by the
// text on screen (F-016):
//
//   Title   sort_title, so a leading article is ignored
//   Author  first-billed author's filing name, so Banks files under B;
//           within an author, by series and position, standalones last
//   Series  series name, then sort_position — never the printed position
//   Rating, Year, Reads, Read state  their values
//
// A missing value (unrated, no series, no year) sorts after every present
// one in both directions. Ties fall back to sort title.
class BookSortProxy : public QSortFilterProxyModel {
    Q_OBJECT

public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    // Shows only these books; nullopt shows every book (the rail's filter).
    void showOnly(std::optional<QSet<qint64>> ids);

protected:
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    std::optional<QSet<qint64>> shown_;
};

} // namespace pinax::ui
