#pragma once

#include "domain/book_detail.h"
#include "domain/book_edit.h"
#include "domain/book_query.h"

#include <QWidget>

#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QToolButton;
class QVBoxLayout;

namespace pinax::ui {

// The detail panel's edit state (D-011): a form over one book's own fields,
// checked before anything leaves it. Emits the edited book; saving is the
// caller's, so this issues no SQL (invariant 8).
//
// The read count is shown, not edited: it follows the read state through
// the re-read trigger (F-006). Authors are edited as text in the import
// notation (SPEC.md §1.1, F-002). Series are edited as rows — the series,
// chosen or newly named, its position as printed and a sort number that may
// be left blank (BUG-005). A detail
// whose book has id 0 is a new book (F-001).
// An edited synopsis is marked as entered by hand, so enrichment will never
// overwrite it (AV-001).
class BookEditor : public QWidget {
    Q_OBJECT

public:
    explicit BookEditor(QWidget* parent = nullptr);

    void editBook(const domain::BookDetail& detail);

    // The catalogue's series, to choose from in the series rows.
    void setSeriesChoices(const std::vector<domain::FilterOption>& series);
    void showError(const QString& message);
    void focusTitle();

    // Checks the form and, if it passes, emits saveRequested.
    void save();

signals:
    void saveRequested(const domain::BookEdit& edit);
    void cancelled();

private:
    struct SeriesRow {
        QWidget* widget;
        QComboBox* name;
        QLineEdit* position;
        QLineEdit* sort;
    };
    // A row for one series, optionally filled with a place the book holds.
    void addSeriesRow(const domain::SeriesMembership* membership = nullptr);
    void removeSeriesRow(QWidget* row);

    domain::Book original_;
    std::vector<domain::FilterOption> seriesChoices_;
    std::vector<SeriesRow> seriesRows_;
    QVBoxLayout* seriesLayout_;

    QLabel* heading_;

    QLineEdit* title_;
    QLineEdit* subtitle_;
    QLineEdit* authors_;
    QComboBox* readState_;
    QLabel* timesRead_;
    QComboBox* rating_;
    QLineEdit* publisher_;
    QLineEdit* published_;
    QLineEdit* pages_;
    QComboBox* binding_;
    QLineEdit* isbn13_;
    QLineEdit* isbn10_;
    QLineEdit* editionNote_;
    QLineEdit* conditionNote_;
    QLineEdit* acquiredDate_;
    QLineEdit* acquiredNote_;
    QPlainTextEdit* synopsis_;
    QPlainTextEdit* notes_;
    QLabel* error_;
};

} // namespace pinax::ui
