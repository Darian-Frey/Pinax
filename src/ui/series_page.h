#pragma once

#include "domain/series_row.h"
#include "domain/series_status.h"

#include <QList>
#include <QTableView>
#include <QWidget>

#include <vector>

class QLabel;
class QToolButton;

namespace pinax::ui {

class SeriesEntryModel;

// The table of a series' entries, answering the list's single keys for the
// owned volumes selected (list_keys.h). Missing volumes take no key: they
// are edited and removed in the entry editor.
class SeriesTable : public QTableView {
    Q_OBJECT

public:
    explicit SeriesTable(SeriesEntryModel* model, QWidget* parent = nullptr);

    // Book ids of the owned volumes selected, and how many missing ones are.
    QList<qint64> selectedBooks() const;
    int selectedMissing() const;

signals:
    void toggleReadRequested(const QList<qint64>& ids);
    void ratingRequested(const QList<qint64>& ids, int rating);
    void deleteRequested(const QList<qint64>& ids);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    SeriesEntryModel* model_;
};

// The middle panel while a series is chosen in the rail (D-010, mock-up
// screen 2): a heading with the series' held, known and missing counts, a
// toggle for the volumes not owned, and the series' entries in order.
class SeriesPage : public QWidget {
    Q_OBJECT

public:
    explicit SeriesPage(QWidget* parent = nullptr);

    // Shows a series. A refresh of the series already shown keeps the
    // selection on the same entries.
    void showSeries(const domain::SeriesStatus& series, std::vector<domain::SeriesRow> rows);

    std::int64_t seriesId() const { return seriesId_; }
    SeriesTable* table() const { return table_; }
    SeriesEntryModel* entryModel() const { return model_; }

    QList<qint64> selectedBooks() const { return table_->selectedBooks(); }
    void selectBook(std::int64_t bookId);

signals:
    // The selection moved: the owned books selected, and how many volumes
    // not owned are selected with them.
    void selectionChangedTo(const QList<qint64>& bookIds, int missing);

private:
    QLabel* heading_;
    QToolButton* showMissing_;
    SeriesEntryModel* model_;
    SeriesTable* table_;
    std::int64_t seriesId_ = 0;
};

} // namespace pinax::ui
