#pragma once

#include "domain/book_detail.h"

#include <QWidget>

class QGridLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace pinax::ui {

class RatingBar;

// The detail panel's view state for one book, laid out as the mock-up's
// first screen: cover and heading, rating, series, synopsis, edition.
// Read-only; editing is BookEditor's (D-011).
class BookView : public QWidget {
    Q_OBJECT

public:
    explicit BookView(QWidget* parent = nullptr);

    void showBook(const domain::BookDetail& detail);

signals:
    void editRequested();
    void deleteRequested();
    // From the rating squares: 1-10, or 0 to clear (F-007).
    void ratingChosen(int rating);
    void fetchRequested();

private:
    void showSeries(const std::vector<domain::SeriesMembership>& series);
    void setEditionValue(QLabel* label, const std::optional<std::string>& value);

    QLabel* cover_;
    QLabel* title_;
    QLabel* subtitle_;
    QLabel* authors_;
    QLabel* readState_;
    QLabel* readCount_;
    RatingBar* rating_;
    QLabel* ratingText_;

    QWidget* seriesSection_;
    QVBoxLayout* seriesList_;

    QLabel* synopsisSource_;
    QLabel* synopsis_;
    QLabel* genres_;
    QPushButton* fetch_;

    QLabel* publisher_;
    QLabel* published_;
    QLabel* pages_;
    QLabel* binding_;
    QLabel* isbn_;
    QLabel* edition_;
    QLabel* condition_;
    QLabel* acquired_;

    QWidget* notesSection_;
    QLabel* notes_;
};

} // namespace pinax::ui
