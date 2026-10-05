#pragma once

#include "domain/book_detail.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;

namespace pinax::ui {

// The detail panel's edit state (D-011): a form over one book's own fields,
// checked before anything leaves it. Emits the edited book; saving is the
// caller's, so this issues no SQL (invariant 8).
//
// The read count is shown, not edited: it follows the read state through
// the re-read trigger (F-006). Authors and series are not edited here yet.
// An edited synopsis is marked as entered by hand, so enrichment will never
// overwrite it (AV-001).
class BookEditor : public QWidget {
    Q_OBJECT

public:
    explicit BookEditor(QWidget* parent = nullptr);

    void editBook(const domain::BookDetail& detail);
    void showError(const QString& message);
    void focusTitle();

    // Checks the form and, if it passes, emits saveRequested.
    void save();

signals:
    void saveRequested(const domain::Book& book);
    void cancelled();

private:
    domain::Book original_;

    QLineEdit* title_;
    QLineEdit* subtitle_;
    QLabel* authors_;
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
