#include "app/main_window.h"

#include "app/catalogue.h"
#include "ui/book_list_view.h"
#include "ui/detail_panel.h"

#include <QSplitter>
#include <QStatusBar>

namespace pinax::app {

namespace {

// Panel widths taken from the mock-up in design/: a narrow rail, a list that
// takes the slack, and a detail panel wide enough for the edition fields.
constexpr int railWidth = 190;
constexpr int listWidth = 600;
constexpr int detailWidth = 300;

QWidget* makePanel(const QString& name, QWidget* parent)
{
    auto* panel = new QWidget(parent);
    panel->setObjectName(name);
    return panel;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , splitter_(new QSplitter(Qt::Horizontal, this))
    , rail_(makePanel(QStringLiteral("rail"), splitter_))
    , list_(new ui::BookListView(splitter_))
    , detail_(new ui::DetailPanel(splitter_))
{
    setWindowTitle(QStringLiteral("Pinax"));
    list_->setObjectName(QStringLiteral("list"));
    detail_->setObjectName(QStringLiteral("detail"));

    connect(list_, &ui::BookListView::selectionChangedTo, this, &MainWindow::showSelection);
    connect(detail_, &ui::DetailPanel::saveRequested, this, &MainWindow::saveBook);

    splitter_->setChildrenCollapsible(false);
    splitter_->setStretchFactor(0, 0);
    splitter_->setStretchFactor(1, 1);
    splitter_->setStretchFactor(2, 0);
    splitter_->setSizes({railWidth, listWidth, detailWidth});
    setCentralWidget(splitter_);

    statusBar()->showMessage(tr("No catalogue open"));

    resize(railWidth + listWidth + detailWidth, 700);
}

void MainWindow::setCatalogue(Catalogue* catalogue)
{
    catalogue_ = catalogue;
    list_->setBooks(catalogue_ ? catalogue_->summaries() : std::vector<domain::BookSummary> {});
}

void MainWindow::showSelection(const QList<qint64>& ids)
{
    if (!catalogue_ || ids.isEmpty()) {
        detail_->showNothing();
        return;
    }
    if (ids.size() > 1) {
        detail_->showSelection(static_cast<int>(ids.size()));
        return;
    }
    if (const auto detail = catalogue_->detail(ids.first()))
        detail_->showBook(*detail);
    else
        detail_->showNothing();
}

void MainWindow::saveBook(const domain::Book& book)
{
    if (!catalogue_)
        return;
    if (const auto problem = catalogue_->save(book)) {
        detail_->showSaveError(QString::fromStdString(*problem));
        return;
    }
    if (const auto summary = catalogue_->summary(book.id))
        list_->updateBook(*summary);
    if (const auto detail = catalogue_->detail(book.id))
        detail_->showBook(*detail);
    list_->setFocus();
    statusBar()->showMessage(tr("Saved “%1”").arg(QString::fromStdString(book.title)), 4000);
}

} // namespace pinax::app
