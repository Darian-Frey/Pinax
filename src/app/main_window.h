#pragma once

#include <QMainWindow>

class QSplitter;
class QWidget;

namespace pinax::ui {
class BookListView;
}

namespace pinax::app {

// The application shell: filter rail, list and detail panel side by side in a
// splitter (D-010). The list is the ui module's BookListView; the rail and
// detail panel are empty placeholders until it supplies them.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    QSplitter* splitter() const { return splitter_; }
    ui::BookListView* bookList() const { return list_; }

private:
    QSplitter* splitter_;
    QWidget* rail_;
    ui::BookListView* list_;
    QWidget* detail_;
};

} // namespace pinax::app
