#pragma once

#include "domain/series_entry.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace pinax::ui {

// The detail panel's form for one series entry (F-008, F-009): its printed
// position, its sort number, its title and notes. An entry with id 0 is a new
// volume. Emits the edited entry; saving is the caller's.
//
// The sort number is entered, never worked out from the position: only the
// importer reads a number out of a position (D-005, AV-006).
class EntryEditor : public QWidget {
    Q_OBJECT

public:
    explicit EntryEditor(QWidget* parent = nullptr);

    // `ownedBy` names the book on the shelf for this volume, if any.
    void editEntry(const domain::SeriesEntry& entry, const QString& seriesName, const QString& ownedBy);
    void showError(const QString& message);
    void focusFirst();
    void save();

signals:
    void saveRequested(const domain::SeriesEntry& entry);
    void removeRequested(qint64 entryId);
    void cancelled();

private:
    domain::SeriesEntry original_;
    QLabel* heading_;
    QLabel* owned_;
    QLineEdit* position_;
    QLineEdit* sortPosition_;
    QLineEdit* title_;
    QPlainTextEdit* notes_;
    QLabel* error_;
    QPushButton* remove_;
};

} // namespace pinax::ui
