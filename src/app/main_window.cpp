#include "app/main_window.h"

#include "app/catalogue.h"
#include "ui/book_list_model.h"
#include "ui/book_list_view.h"
#include "db/db_error.h"
#include "domain/placeholder.h"
#include "ui/detail_panel.h"
#include "ui/rail_view.h"
#include "ui/missing_page.h"
#include "ui/series_page.h"

#include <QAction>
#include <QLabel>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolBar>

#include <algorithm>

namespace pinax::app {

namespace {

// Panel widths taken from the mock-up in design/: a narrow rail, a list that
// takes the slack, and a detail panel wide enough for the edition fields.
constexpr int railWidth = 190;
constexpr int listWidth = 600;
constexpr int detailWidth = 300;

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , splitter_(new QSplitter(Qt::Horizontal, this))
    , rail_(new ui::RailView(splitter_))
    , centre_(new QStackedWidget(splitter_))
    , seriesPage_(new ui::SeriesPage(centre_))
    , missingPage_(new ui::MissingPage(centre_))
    , list_(new ui::BookListView(centre_))
    , detail_(new ui::DetailPanel(splitter_))
{
    setWindowTitle(QStringLiteral("Pinax"));
    rail_->setObjectName(QStringLiteral("rail"));
    centre_->setObjectName(QStringLiteral("centre"));
    list_->setObjectName(QStringLiteral("list"));
    seriesPage_->setObjectName(QStringLiteral("seriesPage"));
    centre_->addWidget(list_);
    centre_->addWidget(seriesPage_);
    missingPage_->setObjectName(QStringLiteral("missingPage"));
    centre_->addWidget(missingPage_);
    connect(missingPage_, &ui::MissingPage::selectionChangedTo, this, &MainWindow::showMissingSelection);
    connect(missingPage_, &ui::MissingPage::openSeriesRequested, this, [this](qint64 seriesId) {
        rail_->chooseFilter({domain::BookFilter::Kind::Series, domain::ReadStatus::Unread, seriesId});
    });

    // A series' list answers the same keys, through the same handlers.
    connect(seriesPage_, &ui::SeriesPage::selectionChangedTo, this, &MainWindow::showSeriesSelection);
    connect(seriesPage_->table(), &ui::SeriesTable::toggleReadRequested, this, &MainWindow::toggleRead);
    connect(seriesPage_->table(), &ui::SeriesTable::ratingRequested, this, &MainWindow::rate);
    connect(seriesPage_->table(), &ui::SeriesTable::deleteRequested, this, &MainWindow::askToDelete);
    connect(seriesPage_, &ui::SeriesPage::addEntryRequested, this, &MainWindow::addEntry);
    connect(seriesPage_, &ui::SeriesPage::editEntryRequested, this, &MainWindow::editEntry);
    detail_->setObjectName(QStringLiteral("detail"));

    connect(list_, &ui::BookListView::selectionChangedTo, this, &MainWindow::showSelection);
    connect(rail_, &ui::RailView::filterChosen, this, &MainWindow::applyFilter);
    connect(detail_, &ui::DetailPanel::saveRequested, this, &MainWindow::saveBook);
    connect(list_, &ui::BookListView::toggleReadRequested, this, &MainWindow::toggleRead);
    connect(list_, &ui::BookListView::ratingRequested, this, &MainWindow::rate);
    connect(detail_, &ui::DetailPanel::ratingRequested, this,
        [this](qint64 id, int rating) { rate({id}, rating); });
    connect(list_, &ui::BookListView::deleteRequested, this, &MainWindow::askToDelete);
    connect(detail_, &ui::DetailPanel::deleteRequested, this, &MainWindow::askToDelete);
    connect(detail_, &ui::DetailPanel::deleteConfirmed, this, &MainWindow::deleteBooks);
    connect(detail_, &ui::DetailPanel::dismissed, this, [this] {
        pendingAttach_.reset();
        refreshPanel();
    });
    connect(detail_, &ui::DetailPanel::editEntryRequested, this, &MainWindow::editEntry);
    connect(detail_, &ui::DetailPanel::entrySaveRequested, this, &MainWindow::saveEntry);
    connect(detail_, &ui::DetailPanel::entryRemoveRequested, this, &MainWindow::askToRemoveEntry);
    connect(detail_, &ui::DetailPanel::markOwnedRequested, this, &MainWindow::beginMarkOwned);
    connect(detail_, &ui::DetailPanel::attachRequested, this, &MainWindow::attachBook);
    connect(detail_, &ui::DetailPanel::createForEntryRequested, this, &MainWindow::createForEntry);

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
    seriesPage_->setEnabled(!busy);
    missingPage_->setEnabled(!busy);
    rail_->setEnabled(!busy);
    addBook_->setEnabled(!busy);
    if (detail_->isEditing())
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
    refreshRail();
}

void MainWindow::refreshRail()
{
    ui::RailContents contents;
    if (catalogue_) {
        contents.all = catalogue_->count();
        contents.unread = catalogue_->countWithReadStatus(domain::ReadStatus::Unread);
        contents.reading = catalogue_->countWithReadStatus(domain::ReadStatus::Reading);
        contents.read = catalogue_->countWithReadStatus(domain::ReadStatus::Read);
        contents.series = catalogue_->seriesStatuses();
        const auto totals = catalogue_->libraryTotals();
        contents.oneVolumeShort = totals.oneVolumeShort;
        contents.missingVolumes = totals.volumesNotOwned;
    }
    rail_->setContents(contents);
}

void MainWindow::applyFilter(const domain::BookFilter& filter, const QString& label)
{
    if (!catalogue_)
        return;
    if (filter.kind == domain::BookFilter::Kind::MissingVolumes
        || filter.kind == domain::BookFilter::Kind::OneVolumeShort) {
        // The shopping list (F-010).
        centre_->setCurrentWidget(missingPage_);
        showMissingPage(filter.kind == domain::BookFilter::Kind::OneVolumeShort);
        missingPage_->table()->clearSelection();
        missingPage_->table()->setCurrentIndex({});
        detail_->showNothing();
        missingPage_->table()->setFocus();
        statusBar()->showMessage(label);
        return;
    }
    if (filter.kind == domain::BookFilter::Kind::Series) {
        // A series has a page of its own: its entries in order, the volumes
        // not owned among them (D-006, D-010).
        centre_->setCurrentWidget(seriesPage_);
        showSeriesPage(filter.seriesId);
        seriesPage_->table()->clearSelection();
        seriesPage_->table()->setCurrentIndex({});
        refreshPanel();
        seriesPage_->table()->setFocus();
        statusBar()->showMessage(label);
        return;
    }
    centre_->setCurrentWidget(list_);
    const auto ids = catalogue_->bookIds(filter);
    list_->showOnly(ids ? std::optional(QList<qint64>(ids->begin(), ids->end())) : std::nullopt);
    list_->clearSelection();
    statusBar()->showMessage(tr("%1 · %2 of %3 shown")
                                 .arg(label)
                                 .arg(list_->shownCount())
                                 .arg(catalogue_->count()));
}

bool MainWindow::showingSeries() const
{
    return centre_->currentWidget() == seriesPage_;
}

bool MainWindow::showingMissing() const
{
    return centre_->currentWidget() == missingPage_;
}

void MainWindow::showMissingPage(bool oneVolumeShort)
{
    missingPage_->showRows(catalogue_->missingVolumes(oneVolumeShort), oneVolumeShort);
}

void MainWindow::showMissingSelection(const std::optional<domain::MissingRow>& volume)
{
    if (!catalogue_ || !showingMissing())
        return;
    if (!volume) {
        detail_->showNothing();
        return;
    }
    domain::SeriesRow row;
    row.entryId = volume->entryId;
    row.position = volume->position;
    row.sortPosition = volume->sortPosition;
    row.entryTitle = volume->title;
    if (const auto detail = catalogue_->seriesDetail(volume->seriesId))
        detail_->showSeries(*detail, row);
}

void MainWindow::showSeriesSelection(const QList<qint64>& bookIds, int missing)
{
    if (!catalogue_ || !showingSeries())
        return;
    if (bookIds.isEmpty()) {
        // Nothing owned selected: the panel describes the series itself, with
        // a card for the one missing volume selected, if that is what it is.
        const auto entries = seriesPage_->selectedEntries();
        std::optional<domain::SeriesRow> selected;
        if (entries.size() == 1)
            selected = entries.front();
        if (const auto detail = catalogue_->seriesDetail(seriesPage_->seriesId()))
            detail_->showSeries(*detail, selected);
        return;
    }
    if (bookIds.size() == 1 && missing == 0) {
        showSelection(bookIds);
        return;
    }
    detail_->showSelection(static_cast<int>(bookIds.size()) + missing);
}

void MainWindow::showSeriesPage(std::int64_t seriesId)
{
    const auto detail = catalogue_->seriesDetail(seriesId);
    if (!detail)
        return;
    seriesPage_->showSeries(detail->series, catalogue_->seriesRows(seriesId));
}

void MainWindow::refreshPanel()
{
    // Never redraw over a form in progress. A pending question is redrawn:
    // that is how Keep returns to what was shown.
    if (detail_->isEditing())
        return;
    if (showingSeries())
        showSeriesSelection(seriesPage_->selectedBooks(), seriesPage_->table()->selectedMissing());
    else if (showingMissing())
        showMissingSelection(missingPage_->selectedVolume());
    else
        showSelection(list_->selectedBooks());
}

void MainWindow::addEntry()
{
    if (!catalogue_ || !showingSeries())
        return;
    domain::SeriesEntry entry;
    entry.seriesId = seriesPage_->seriesId();
    entry.sortPosition = catalogue_->nextSortPosition(entry.seriesId);
    detail_->beginEntryEdit(entry,
        QString::fromStdString(catalogue_->seriesName(entry.seriesId).value_or("")), QString());
}

void MainWindow::editEntry(qint64 entryId)
{
    if (!catalogue_)
        return;
    const auto entry = catalogue_->entry(entryId);
    if (!entry)
        return;
    QString ownedBy;
    if (entry->bookId)
        ownedBy = titleOf(*entry->bookId);
    detail_->beginEntryEdit(*entry,
        QString::fromStdString(catalogue_->seriesName(entry->seriesId).value_or("")), ownedBy);
}

void MainWindow::saveEntry(const domain::SeriesEntry& entry)
{
    if (!catalogue_)
        return;
    if (const auto problem = catalogue_->saveEntry(entry)) {
        detail_->showEntryError(QString::fromStdString(*problem));
        return;
    }
    detail_->showNothing();
    afterSeriesChange();
    statusBar()->showMessage(entry.id == 0 ? tr("Volume added") : tr("Volume saved"), 4000);
}

void MainWindow::askToRemoveEntry(qint64 entryId)
{
    if (!catalogue_)
        return;
    const auto entry = catalogue_->entry(entryId);
    if (!entry)
        return;
    const QString series = QString::fromStdString(catalogue_->seriesName(entry->seriesId).value_or(""));
    QString name = entry->title ? QString::fromStdString(*entry->title)
                                : tr("volume %1").arg(QString::fromStdString(entry->position.value_or("?")));
    QString question;
    if (entry->bookId) {
        name = titleOf(*entry->bookId);
        question = tr("Take “%1” out of %2?").arg(name, series) + QStringLiteral("\n\n")
            + tr("The book stays in the catalogue; the series will no longer count it.");
    } else {
        question = tr("Remove “%1” from %2?").arg(name, series) + QStringLiteral("\n\n")
            + tr("The series will no longer count it as missing.");
    }
    detail_->askToConfirm(question, tr("Remove"), [this, entryId, name] {
        if (const auto problem = catalogue_->removeEntry(entryId)) {
            statusBar()->showMessage(QString::fromStdString(*problem));
            refreshPanel();
            return;
        }
        detail_->showNothing();
        afterSeriesChange();
        statusBar()->showMessage(tr("Removed “%1” from the series").arg(name), 4000);
    });
}

void MainWindow::beginMarkOwned(qint64 entryId)
{
    if (!catalogue_)
        return;
    // From the series' page or the shopping list alike: the volume knows its
    // series.
    const auto entry = catalogue_->entry(entryId);
    if (!entry)
        return;
    const auto rows = catalogue_->seriesRows(entry->seriesId);
    const auto volume = std::find_if(rows.begin(), rows.end(),
        [entryId](const domain::SeriesRow& row) { return row.entryId == entryId; });
    if (volume == rows.end() || volume->owned())
        return;

    // Any book not already in this series may take the volume.
    std::vector<domain::BookSummary> candidates;
    for (auto& book : catalogue_->summaries()) {
        const bool inSeries = std::any_of(rows.begin(), rows.end(),
            [&](const domain::SeriesRow& row) { return row.bookId == book.id; });
        if (!inSeries)
            candidates.push_back(std::move(book));
    }
    detail_->beginAttach(*volume,
        QString::fromStdString(catalogue_->seriesName(entry->seriesId).value_or("")), candidates);
}

void MainWindow::attachBook(qint64 entryId, qint64 bookId)
{
    if (!catalogue_)
        return;
    if (const auto problem = catalogue_->attach(entryId, bookId)) {
        statusBar()->showMessage(QString::fromStdString(*problem));
        return;
    }
    detail_->showNothing();
    afterSeriesChange(bookId);
    statusBar()->showMessage(tr("“%1” now owned").arg(titleOf(bookId)), 4000);
}

void MainWindow::createForEntry(qint64 entryId)
{
    if (!catalogue_)
        return;
    const auto entry = catalogue_->entry(entryId);
    if (!entry || entry->bookId)
        return;
    // The form starts with what the series already knows: the volume's title
    // (unless it is a placeholder) and the authors its other volumes carry.
    domain::BookDetail prefill;
    if (entry->title && !domain::isPlaceholderTitle(entry->title))
        prefill.book.title = *entry->title;
    prefill.credits = catalogue_->seriesCredits(entry->seriesId);
    pendingAttach_ = entryId;
    detail_->beginNew(prefill);
}

void MainWindow::afterSeriesChange(std::optional<qint64> selectBook)
{
    list_->setBooks(catalogue_->summaries());
    refreshRail();
    if (showingSeries()) {
        showSeriesPage(seriesPage_->seriesId());
        if (selectBook)
            seriesPage_->selectBook(*selectBook);
        seriesPage_->table()->setFocus();
    }
    if (showingMissing()) {
        // A volume marked owned drops off the list; the list stays put.
        showMissingPage(missingPage_->oneVolumeShort());
        missingPage_->table()->setFocus();
    }
    refreshPanel();
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
    const auto attachTo = edit.book.id == 0 ? pendingAttach_ : std::nullopt;
    const auto result = catalogue_->save(edit, attachTo);
    if (result.problem) {
        detail_->showSaveError(QString::fromStdString(*result.problem));
        return;
    }

    const QString title = QString::fromStdString(edit.book.title);
    if (attachTo) {
        // Added for a missing volume: it takes the waiting entry (AV-007), and
        // the owner stays on the series' page.
        pendingAttach_.reset();
        detail_->showNothing();
        afterSeriesChange(result.id);
        statusBar()->showMessage(tr("Added “%1”, now owned").arg(title), 4000);
        return;
    }
    refreshRail();
    if (edit.book.id == 0) {
        // A new row: back to every book, since a filter chosen before it
        // existed cannot include it; reload, then select it.
        rail_->chooseFilter({});
        list_->setBooks(catalogue_->summaries());
        list_->selectBook(result.id);
        statusBar()->showMessage(tr("Added “%1”").arg(title), 4000);
    } else {
        if (const auto summary = catalogue_->summary(result.id))
            list_->updateBook(*summary);
        if (showingSeries())
            showSeriesPage(seriesPage_->seriesId());
        if (const auto detail = catalogue_->detail(result.id))
            detail_->showBook(*detail);
        statusBar()->showMessage(tr("Saved “%1”").arg(title), 4000);
    }
    if (showingSeries())
        seriesPage_->table()->setFocus();
    else
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
        refreshPanel();
        return;
    }
    list_->setBooks(catalogue_->summaries());
    refreshRail();
    if (showingSeries()) {
        // The deleted volume stays in the series as a missing one (F-001).
        showSeriesPage(seriesPage_->seriesId());
        refreshPanel();
        seriesPage_->table()->setFocus();
    } else {
        detail_->showNothing();
        list_->setFocus();
    }
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
    refreshRail();

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
    if (showingSeries())
        showSeriesPage(seriesPage_->seriesId());
    // Never redraw over a form in progress.
    refreshPanel();
}

QString MainWindow::titleOf(qint64 id) const
{
    const auto summary = catalogue_ ? catalogue_->summary(id) : std::nullopt;
    return summary ? QString::fromStdString(summary->title) : QString();
}

} // namespace pinax::app
