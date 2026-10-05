#include "app/main_window.h"

#include "ui/book_list_view.h"

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
    , detail_(makePanel(QStringLiteral("detail"), splitter_))
{
    setWindowTitle(QStringLiteral("Pinax"));
    list_->setObjectName(QStringLiteral("list"));

    splitter_->setChildrenCollapsible(false);
    splitter_->setStretchFactor(0, 0);
    splitter_->setStretchFactor(1, 1);
    splitter_->setStretchFactor(2, 0);
    splitter_->setSizes({railWidth, listWidth, detailWidth});
    setCentralWidget(splitter_);

    statusBar()->showMessage(tr("No catalogue open"));

    resize(railWidth + listWidth + detailWidth, 700);
}

} // namespace pinax::app
