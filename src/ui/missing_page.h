#pragma once

#include "domain/missing_row.h"

#include <QAbstractTableModel>
#include <QWidget>

#include <optional>
#include <vector>

class QLabel;
class QTableView;

namespace pinax::ui {

// The shopping list as a table (F-010): series, printed position, title, and
// how many the series still needs. Placeholders show in italic; their titles
// say only that the volume is unnamed (D-018). Fed by the caller.
class MissingModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { SeriesColumn, PositionColumn, TitleColumn, NeedsColumn, ColumnCount };

    explicit MissingModel(QObject* parent = nullptr);

    void setRows(std::vector<domain::MissingRow> rows);
    const domain::MissingRow& row(int index) const;
    int rowOfEntry(std::int64_t entryId) const;

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    std::vector<domain::MissingRow> rows_;
};

// The middle panel while NEEDS ATTENTION is chosen in the rail: every volume
// series lack, fewest needed first — or only the series one volume short.
// Selecting a volume describes its series in the panel; activating it opens
// the series' own page.
class MissingPage : public QWidget {
    Q_OBJECT

public:
    explicit MissingPage(QWidget* parent = nullptr);

    // `oneVolumeShort` says which list this is, for the heading. A refresh
    // keeps the selected volume selected if it is still missing.
    void showRows(std::vector<domain::MissingRow> rows, bool oneVolumeShort);
    bool oneVolumeShort() const { return oneVolumeShort_; }

    std::optional<domain::MissingRow> selectedVolume() const;
    QTableView* table() const { return table_; }
    MissingModel* missingModel() const { return model_; }

signals:
    void selectionChangedTo(const std::optional<domain::MissingRow>& volume);
    void openSeriesRequested(qint64 seriesId);

private:
    QLabel* heading_;
    MissingModel* model_;
    QTableView* table_;
    bool oneVolumeShort_ = false;
};

} // namespace pinax::ui
