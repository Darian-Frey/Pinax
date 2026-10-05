#include "app/main_window.h"

#include "app/catalogue.h"
#include "ui/book_list_view.h"
#include "db/db_error.h"
#include "ui/detail_panel.h"

#include <QAction>
#include <QLabel>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>

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
    connect(list_, &ui::BookListView::toggleReadRequested, this, &MainWindow::toggleRead);
    connect(list_, &ui::BookListView::ratingRequested, this, &MainWindow::rate);
    connect(detail_, &ui::DetailPanel::ratingRequested, this,
        [this](qint64 id, int rating) { rate({id}, rating); });
    connect(list_, &ui::BookListView::deleteRequested, this, &MainWindow::askToDelete);
    connect(detail_, &ui::DetailPanel::deleteRequested, this, &MainWindow::askToDelete);
    connect(detail_, &ui::DetailPanel::deleteConfirmed, this, &MainWindow::deleteBooks);
    connect(detail_, &ui::DetailPanel::deleteCancelled, this,
        [this] { showSelection(list_->selectedBooks()); });

    auto* toolbar = addToolBar(tr("Catalogue"));
    toolbar->setObjectName(QStringLiteral("toolbar"));
    toolbar->setMovable(false);
    addBook_ = toolbar->addAction(tr("+ Add a book"), this, &MainWindow::addBook);
    addBook_->setObjectName(QStringLiteral("addBook"));
    addBook_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_N));
    addBook_->setToolTip(tr("Add a book by hand (Ctrl+N)"));
    connect(detail_, &ui::DetailPanel::stateChanged, this, &MainWindow::lockWhileBusy);

    splitter_->setChildrenCollapsible(false);
    splitter_->setStretchFactor(0, 0);
    splitter_->setStretchFactor(1, 1);
    splitter_->setStretchFactor(2, 0);
    splitter_->setSizes({railWidth, listWidth, detailWidth});
    setCentralWidget(splitter_);

    statusBar()->showMessage(tr("No catalogue open"));
    auto* keys = new QLabel(tr("R toggles read · 1–9, 0 rate · F2 edits · Del deletes"), this);
    keys->setObjectName(QStringLiteral("keys"));
    keys->setEnabled(false);
    statusBar()->addPermanentWidget(keys);

    resize(railWidth + listWidth + detailWidth, 700);
}

void MainWindow::lockWhileBusy()
{
    const bool busy = detail_->isBusy();
    if (list_->isEnabled() == !busy)
        return;
    list_->setEnabled(!busy);
    addBook_->setEnabled(!busy);
    if (detail_->state() == ui::DetailPanel::State::Editing)
        statusBar()->showMessage(tr("Editing — Ctrl+Enter saves, Esc cancels"));
    else if (detail_->state() == ui::DetailPanel::State::ConfirmingDelete)
        statusBar()->showMessage(tr("Delete or keep? Esc keeps"));
    else
        statusBar()->clearMessage();
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

void MainWindow::saveBook(const domain::BookEdit& edit)
{
    if (!catalogue_)
        return;
    const auto result = catalogue_->save(edit);
    if (result.problem) {
        detail_->showSaveError(QString::fromStdString(*result.problem));
        return;
    }

    const QString title = QString::fromStdString(edit.book.title);
    if (edit.book.id == 0) {
        // A new row: reload, then select it, which shows it in the panel.
        list_->setBooks(catalogue_->summaries());
        list_->selectBook(result.id);
        statusBar()->showMessage(tr("Added “%1”").arg(title), 4000);
    } else {
        if (const auto summary = catalogue_->summary(result.id))
            list_->updateBook(*summary);
        if (const auto detail = catalogue_->detail(result.id))
            detail_->showBook(*detail);
        statusBar()->showMessage(tr("Saved “%1”").arg(title), 4000);
    }
    list_->setFocus();
}

void MainWindow::addBook()
{
    if (!catalogue_)
        return;
    list_->clearSelection();
    detail_->beginNew();
}

void MainWindow::askToDelete(const QList<qint64>& ids)
{
    if (!catalogue_ || ids.isEmpty())
        return;

    QString question;
    if (ids.size() == 1) {
        const auto detail = catalogue_->detail(ids.first());
        if (!detail)
            return;
        question = tr("Delete “%1”?").arg(QString::fromStdString(detail->book.title));
        question += QStringLiteral("\n\n") + tr("It leaves the catalogue with its credits and notes.");
        for (const auto& series : detail->series) {
            question += QLatin1Char(' ')
                + tr("Its place in %1 stays, as a missing volume.").arg(QString::fromStdString(series.name));
        }
    } else {
        question = tr("Delete %1 books?").arg(ids.size());
        question += QStringLiteral("\n\n")
            + tr("They leave the catalogue with their credits and notes. Any series places they "
                 "hold stay, as missing volumes.");
    }
    question += QStringLiteral("\n\n") + tr("This cannot be undone.");
    detail_->askToDelete(ids, question);
}

void MainWindow::deleteBooks(const QList<qint64>& ids)
{
    if (!catalogue_ || ids.isEmpty())
        return;
    const QString subject = ids.size() == 1 ? tr("“%1”").arg(titleOf(ids.first()))
                                            : tr("%1 books").arg(ids.size());
    if (const auto problem = catalogue_->remove(std::vector<std::int64_t>(ids.begin(), ids.end()))) {
        statusBar()->showMessage(QString::fromStdString(*problem));
        showSelection(list_->selectedBooks());
        return;
    }
    list_->setBooks(catalogue_->summaries());
    detail_->showNothing();
    list_->setFocus();
    statusBar()->showMessage(tr("Deleted %1").arg(subject), 4000);
}

void MainWindow::toggleRead(const QList<qint64>& ids)
{
    if (!catalogue_ || ids.isEmpty())
        return;
    domain::ReadStatus state;
    try {
        state = catalogue_->toggleRead(std::vector<std::int64_t>(ids.begin(), ids.end()));
    } catch (const db::DbError& error) {
        statusBar()->showMessage(tr("Read state not changed: %1").arg(QString::fromUtf8(error.what())));
        return;
    }
    refreshBooks(ids);

    const bool read = state == domain::ReadStatus::Read;
    if (ids.size() == 1) {
        statusBar()->showMessage(read ? tr("“%1” marked read").arg(titleOf(ids.first()))
                                      : tr("“%1” marked unread").arg(titleOf(ids.first())),
            4000);
    } else {
        statusBar()->showMessage(read ? tr("%1 books marked read").arg(ids.size())
                                      : tr("%1 books marked unread").arg(ids.size()),
            4000);
    }
}

void MainWindow::rate(const QList<qint64>& ids, int rating)
{
    if (!catalogue_ || ids.isEmpty())
        return;
    const std::optional<int> value = rating == 0 ? std::nullopt : std::optional(rating);
    try {
        catalogue_->setRating(std::vector<std::int64_t>(ids.begin(), ids.end()), value);
    } catch (const db::DbError& error) {
        statusBar()->showMessage(tr("Rating not changed: %1").arg(QString::fromUtf8(error.what())));
        return;
    }
    refreshBooks(ids);

    const QString subject = ids.size() == 1 ? tr("“%1”").arg(titleOf(ids.first()))
                                            : tr("%1 books").arg(ids.size());
    statusBar()->showMessage(value ? tr("%1 rated %2 / 10").arg(subject).arg(*value)
                                   : tr("%1 unrated").arg(subject),
        4000);
}

void MainWindow::refreshBooks(const QList<qint64>& ids)
{
    for (const qint64 id : ids) {
        if (const auto summary = catalogue_->summary(id))
            list_->updateBook(*summary);
    }
    // Never redraw over a form in progress.
    if (detail_->state() == ui::DetailPanel::State::Viewing)
        showSelection(list_->selectedBooks());
}

QString MainWindow::titleOf(qint64 id) const
{
    const auto summary = catalogue_ ? catalogue_->summary(id) : std::nullopt;
    return summary ? QString::fromStdString(summary->title) : QString();
}

} // namespace pinax::app
