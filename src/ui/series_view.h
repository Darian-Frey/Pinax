#pragma once

#include "domain/series_detail.h"
#include "domain/series_row.h"

#include <QWidget>

#include <optional>

class QLabel;
class QPushButton;
class QProgressBar;
class QVBoxLayout;

namespace pinax::ui {

// The detail panel describing a series (D-010, mock-up screen 2): its name,
// authors and length, held against known, what is read, what is missing, and
// how the library stands across every series. Everything shown is derived
// (D-004). Read-only; entries are edited in the entry editor.
class SeriesView : public QWidget {
    Q_OBJECT

public:
    explicit SeriesView(QWidget* parent = nullptr);

    // `selected` is a volume chosen on the series page, shown as a card with
    // its own actions.
    void showSeries(const domain::SeriesDetail& detail,
        const std::optional<domain::SeriesRow>& selected = std::nullopt);

    std::optional<qint64> selectedEntry() const { return selectedEntry_; }

signals:
    void markOwnedRequested(qint64 entryId);
    void editEntryRequested(qint64 entryId);

protected:
    // The progress bar's colours are baked; a change of theme redoes them.
    void changeEvent(QEvent* event) override;

private:
    QWidget* card_;
    QLabel* cardTitle_;
    QPushButton* markOwned_;
    std::optional<qint64> selectedEntry_;

    QLabel* name_;
    QLabel* byline_;
    QProgressBar* progress_;
    QLabel* held_;
    QLabel* legend_;
    QLabel* missingHeading_;
    QLabel* missing_;
    QLabel* oneShort_;
    QLabel* complete_;
    QLabel* withGaps_;
    QLabel* notOwned_;
};

} // namespace pinax::ui
