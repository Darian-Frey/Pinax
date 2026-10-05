#pragma once

#include "domain/book.h"

#include <QList>
#include <QMainWindow>

class QSplitter;
class QWidget;

namespace pinax::ui {
class BookListView;
class DetailPanel;
}

namespace pinax::app {

class Catalogue;

// The application shell: filter rail, list and detail panel side by side in a
// splitter (D-010). The list and detail panel are the ui module's; the rail
// is an empty placeholder until it supplies one. Selection in the list drives
// the panel, and the panel's edits are saved through the Catalogue.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Shows this catalogue's books. The catalogue must outlive the window.
    void setCatalogue(Catalogue* catalogue);

    QSplitter* splitter() const { return splitter_; }
    ui::BookListView* bookList() const { return list_; }
    ui::DetailPanel* detailPanel() const { return detail_; }

private:
    void showSelection(const QList<qint64>& ids);
    void saveBook(const domain::Book& book);

    Catalogue* catalogue_ = nullptr;
    QSplitter* splitter_;
    QWidget* rail_;
    ui::BookListView* list_;
    ui::DetailPanel* detail_;
};

} // namespace pinax::app
