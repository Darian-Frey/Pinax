#pragma once

#include "domain/series_detail.h"

#include <QWidget>

class QLabel;
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

    void showSeries(const domain::SeriesDetail& detail);

private:
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
