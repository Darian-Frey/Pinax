#pragma once

#include "domain/series_row.h"

#include <QAbstractTableModel>

#include <vector>

namespace pinax::ui {

// A series' entries as a table, in series order (D-005): the volumes on the
// shelf and, unless hidden, those known but not owned — italic, marked, and
// "not owned" in the state column (D-006, D-010, mock-up screen 2). Fed by
// the caller; issues no SQL.
class SeriesEntryModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { ReadColumn, PositionColumn, TitleColumn, StateColumn, RatingColumn, YearColumn, ColumnCount };

    explicit SeriesEntryModel(QObject* parent = nullptr);

    void setRows(std::vector<domain::SeriesRow> rows);
    void setShowMissing(bool show);
    bool showsMissing() const { return showMissing_; }

    const domain::SeriesRow& row(int index) const;
    // The shown row holding this entry, or -1.
    int rowOfEntry(std::int64_t entryId) const;

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    void rebuildShown();

    std::vector<domain::SeriesRow> rows_;
    std::vector<std::size_t> shown_; // indices into rows_
    bool showMissing_ = true;
};

} // namespace pinax::ui
