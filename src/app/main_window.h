#pragma once

#include <QMainWindow>

class QSplitter;
class QWidget;

namespace pinax::app {

// The application shell: filter rail, list and detail panel side by side in a
// splitter (D-010). The three panels are empty placeholders until the ui
// module supplies them.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    QSplitter* splitter() const { return splitter_; }

private:
    QSplitter* splitter_;
    QWidget* rail_;
    QWidget* list_;
    QWidget* detail_;
};

} // namespace pinax::app
