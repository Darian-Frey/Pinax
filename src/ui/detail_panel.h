#pragma once

#include "domain/book_detail.h"
#include "domain/book_edit.h"

#include <QList>
#include <QWidget>

#include <optional>

class QLabel;
class QPushButton;
class QStackedWidget;

namespace pinax::ui {

class BookEditor;
class BookView;

// The right-hand panel: it describes whatever is selected (D-010). Nothing,
// one book in its view state or its edit state (D-011), a new book being
// filled in, several books (the bulk editor arrives later), or a deletion
// waiting to be confirmed. No modal dialogues: confirmation happens here.
class DetailPanel : public QWidget {
    Q_OBJECT

public:
    enum class State { Empty, Viewing, Editing, Several, ConfirmingDelete };
    Q_ENUM(State)

    // True while the panel holds something the owner must finish — an edit
    // or a deletion to confirm — and the selection must not move (IMP-002).
    bool isBusy() const { return state_ == State::Editing || state_ == State::ConfirmingDelete; }

    explicit DetailPanel(QWidget* parent = nullptr);

    void showNothing();
    void showBook(const domain::BookDetail& detail);
    void showSelection(int count);

    // Switches the book on show into its edit state. Does nothing otherwise.
    void beginEdit();

    // An empty form for a book not yet in the catalogue (F-001).
    void beginNew();

    // Asks, in the panel, whether to delete these books. `question` says what
    // will be lost; Keep is the default and Esc keeps.
    void askToDelete(const QList<qint64>& ids, const QString& question);
    void showSaveError(const QString& message);

    State state() const { return state_; }
    BookView* view() const { return view_; }
    BookEditor* editor() const { return editor_; }

signals:
    void saveRequested(const domain::BookEdit& edit);
    // Delete pressed in the view state, for the book on show.
    void deleteRequested(const QList<qint64>& ids);
    void deleteConfirmed(const QList<qint64>& ids);
    void deleteCancelled();
    void stateChanged(pinax::ui::DetailPanel::State state);
    // The shown book's rating squares were clicked: 1-10, or 0 to clear.
    void ratingRequested(qint64 bookId, int rating);

private:
    void setState(State state);

    QStackedWidget* stack_;
    QLabel* empty_;
    BookView* view_;
    BookEditor* editor_;
    QLabel* several_;
    QWidget* confirm_;
    QLabel* question_;
    QPushButton* keep_;
    QList<qint64> pendingDelete_;

    std::optional<domain::BookDetail> shown_;
    State state_ = State::Empty;
};

} // namespace pinax::ui
