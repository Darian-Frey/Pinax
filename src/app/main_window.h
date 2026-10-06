#pragma once

#include "domain/book_edit.h"
#include "domain/book_filter.h"

#include <QList>
#include <QMainWindow>

class QAction;
class QSplitter;
class QStackedWidget;
class QWidget;

namespace pinax::ui {
class BookListView;
class DetailPanel;
class RailView;
class SeriesPage;
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
    ui::RailView* rail() const { return rail_; }
    ui::SeriesPage* seriesPage() const { return seriesPage_; }
    // True while a series is chosen and the middle panel lists its entries.
    bool showingSeries() const;

private:
    void showSelection(const QList<qint64>& ids);
    // The series page's selection: owned books and volumes not owned.
    void showSeriesSelection(const QList<qint64>& bookIds, int missing);
    // Shows the series in the middle panel and, unless the panel is busy or
    // something is selected, in the detail panel too.
    void showSeriesPage(std::int64_t seriesId);
    // Redraws the panel for whatever the middle panel has selected, unless a
    // form is open.
    void refreshPanel();
    void saveBook(const domain::BookEdit& edit);
    void addBook();
    void askToDelete(const QList<qint64>& ids);
    void deleteBooks(const QList<qint64>& ids);
    void applyFilter(const domain::BookFilter& filter, const QString& label);
    // Recounts the rail after anything that changes its numbers.
    void refreshRail();
    void toggleRead(const QList<qint64>& ids);
    void rate(const QList<qint64>& ids, int rating);

    // Re-reads these books into the list, and into the panel when it is
    // showing one of them and not mid-edit.
    void refreshBooks(const QList<qint64>& ids);
    QString titleOf(qint64 id) const;

    // IMP-002: while the panel holds an edit or a question, the list and Add
    // stand still, so a stray click cannot throw the edit away.
    void lockWhileBusy();

    Catalogue* catalogue_ = nullptr;
    QAction* addBook_ = nullptr;
    QSplitter* splitter_;
    ui::RailView* rail_;
    QStackedWidget* centre_;
    ui::SeriesPage* seriesPage_;
    ui::BookListView* list_;
    ui::DetailPanel* detail_;
};

} // namespace pinax::app
