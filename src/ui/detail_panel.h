#pragma once

#include "domain/book_detail.h"

#include <QWidget>

#include <optional>

class QLabel;
class QStackedWidget;

namespace pinax::ui {

class BookEditor;
class BookView;

// The right-hand panel: it describes whatever is selected (D-010). Nothing,
// one book in its view state or its edit state (D-011), or several books,
// for which the bulk editor will arrive later. No modal dialogues.
class DetailPanel : public QWidget {
    Q_OBJECT

public:
    enum class State { Empty, Viewing, Editing, Several };

    explicit DetailPanel(QWidget* parent = nullptr);

    void showNothing();
    void showBook(const domain::BookDetail& detail);
    void showSelection(int count);

    // Switches the book on show into its edit state. Does nothing otherwise.
    void beginEdit();
    void showSaveError(const QString& message);

    State state() const { return state_; }
    BookView* view() const { return view_; }
    BookEditor* editor() const { return editor_; }

signals:
    void saveRequested(const domain::Book& book);

private:
    void setState(State state);

    QStackedWidget* stack_;
    QLabel* empty_;
    BookView* view_;
    BookEditor* editor_;
    QLabel* several_;

    std::optional<domain::BookDetail> shown_;
    State state_ = State::Empty;
};

} // namespace pinax::ui
