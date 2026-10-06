#pragma once

#include "domain/book_summary.h"
#include "domain/series_row.h"

#include <QWidget>

#include <vector>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace pinax::ui {

// "Mark as owned" for a volume the series is missing (F-009, AV-007): add it
// as a new book, or attach a book already in the catalogue. Either way the
// book takes the waiting entry; no second entry is made.
class AttachView : public QWidget {
    Q_OBJECT

public:
    explicit AttachView(QWidget* parent = nullptr);

    // `candidates` are the books that may take the volume: those not already
    // in this series.
    void offer(const domain::SeriesRow& volume, const QString& seriesName,
        const std::vector<domain::BookSummary>& candidates);
    void focusSearch();

signals:
    void createRequested(qint64 entryId);
    void attachRequested(qint64 entryId, qint64 bookId);
    void cancelled();

private:
    void filter(const QString& text);
    void attachSelected();

    qint64 entryId_ = 0;
    QLabel* volume_;
    QLineEdit* search_;
    QListWidget* candidates_;
    QPushButton* attach_;
};

} // namespace pinax::ui
