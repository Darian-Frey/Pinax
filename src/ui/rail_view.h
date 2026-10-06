#pragma once

#include "domain/book_filter.h"
#include "domain/series_status.h"

#include <QTreeView>

#include <cstdint>
#include <vector>

class QStandardItem;
class QStandardItemModel;

namespace pinax::ui {

// What the rail shows: counts for the library section and every series with
// its completeness. Supplied by the caller; the rail issues no SQL.
struct RailContents {
    std::int64_t all = 0;
    std::int64_t unread = 0;
    std::int64_t reading = 0;
    std::int64_t read = 0;
    std::vector<domain::SeriesStatus> series;
    int oneVolumeShort = 0; // series lacking exactly one volume
    int missingVolumes = 0; // volumes lacking across every series
};

// The left-hand filter rail of the mock-up (D-010): LIBRARY, then
// SERIES · n with held/known counts, then NEEDS ATTENTION with the shopping
// list (F-010). Choosing an entry emits the filter;
// the list narrows in response. Section headings cannot be chosen.
class RailView : public QTreeView {
    Q_OBJECT

public:
    explicit RailView(QWidget* parent = nullptr);

    // Rebuilds the rail. The filter chosen before stays chosen, silently.
    void setContents(const RailContents& contents);

    // Chooses a filter as if clicked, emitting filterChosen.
    void chooseFilter(const domain::BookFilter& filter);

    domain::BookFilter currentFilter() const { return current_; }

signals:
    // `label` names the choice for the owner: "Unread", "The Culture".
    void filterChosen(const domain::BookFilter& filter, const QString& label);

private:
    QStandardItem* itemFor(const domain::BookFilter& filter) const;
    void selectItem(QStandardItem* item);

    QStandardItemModel* model_;
    domain::BookFilter current_;
    bool rebuilding_ = false;
};

} // namespace pinax::ui
