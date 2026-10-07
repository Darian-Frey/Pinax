#include "app/main_window.h"

#include "app/batch_enricher.h"
#include "app/catalogue.h"
#include "app/enricher.h"
#include "ui/book_list_model.h"
#include "ui/book_list_view.h"
#include "db/db_error.h"
#include "domain/placeholder.h"
#include "ui/detail_panel.h"
#include "ui/add_by_isbn_view.h"
#include "ui/rail_view.h"
#include "ui/missing_page.h"
#include "ui/series_page.h"

#include <QAction>
#include <QLabel>
#include <QPointer>
#include <QProgressBar>
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
    connect(detail_, &ui::DetailPanel::fetchRequested, this, &MainWindow::fetchMetadata);
    connect(detail_, &ui::DetailPanel::candidateChosen, this, &MainWindow::useCandidate);
    connect(detail_, &ui::DetailPanel::fetchCancelled, this, [this] {
        if (reviewing_) {
            batch_->putBack(std::move(*reviewing_), true);
            reviewing_.reset();
            offer_.reset();
            endReview(tr("Review stopped"));
            return;
        }
        if (enricher_)
            enricher_->cancel();
        offer_.reset();
        refreshPanel();
    });
    connect(detail_, &ui::DetailPanel::candidateRejected, this, [this](qint64 bookId) {
        if (!reviewing_ || reviewing_->bookId != bookId)
            return;
        catalogue_->markLookupFailed(bookId);
        reviewing_.reset();
        offer_.reset();
        refreshBooks({bookId});
        reviewNext();
    });
    connect(detail_, &ui::DetailPanel::candidateSkipped, this, [this](qint64 bookId) {
        if (!reviewing_ || reviewing_->bookId != bookId)
            return;
        batch_->putBack(std::move(*reviewing_), false);
        reviewing_.reset();
        offer_.reset();
        reviewNext();
    });

    auto* toolbar = addToolBar(tr("Catalogue"));
    toolbar->setObjectName(QStringLiteral("toolbar"));
    toolbar->setMovable(false);
    addBook_ = toolbar->addAction(tr("+ Add a book"), this, &MainWindow::addBook);
    addBook_->setObjectName(QStringLiteral("addBook"));
    addBook_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_N));
    addBook_->setToolTip(tr("Add a book by hand (Ctrl+N)"));
    addByIsbn_ = toolbar->addAction(tr("Add by ISBN"), this, &MainWindow::addByIsbn);
    addByIsbn_->setObjectName(QStringLiteral("addByIsbn"));
    addByIsbn_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    addByIsbn_->setToolTip(tr("Look a book up by its ISBN, check it, then add it (Ctrl+I)"));
    addByIsbn_->setEnabled(false);
    auto* adding = detail_->addView();
    connect(adding, &ui::AddByIsbnView::lookupRequested, this, &MainWindow::lookUpForAdding);
    connect(adding, &ui::AddByIsbnView::candidateShown, this, &MainWindow::showAddCandidate);
    connect(adding, &ui::AddByIsbnView::searchRequested, this, &MainWindow::searchForAdding);
    connect(adding, &ui::AddByIsbnView::addRequested, this, &MainWindow::addConfirmed);
    connect(adding, &ui::AddByIsbnView::manualRequested, this, &MainWindow::addByHand);
    connect(adding, &ui::AddByIsbnView::cancelled, this, &MainWindow::stopAdding);
    connect(adding, &ui::AddByIsbnView::notThisBook, this, [this](int index) {
        QString title;
        QString author;
        if (addOffer_ && index < static_cast<int>(addOffer_->candidates.size())) {
            const auto& candidate = addOffer_->candidates[static_cast<std::size_t>(index)];
            title = QString::fromStdString(candidate.title);
            if (!candidate.authors.empty())
                author = QString::fromStdString(candidate.authors.front());
        }
        detail_->addView()->showSearch(tr("Search by title and author instead — correct them first if "
                                          "the ISBN's answer had them wrong — or enter the book by hand."),
            title, author);
    });
    connect(adding, &ui::AddByIsbnView::showBookRequested, this, [this](qint64 bookId) {
        stopAdding();
        rail_->chooseFilter({});
        list_->selectBook(bookId);
    });
    toolbar->addSeparator();
    fetchAll_ = toolbar->addAction(tr("Fetch all metadata"), this, &MainWindow::toggleBatch);
    fetchAll_->setObjectName(QStringLiteral("fetchAll"));
    fetchAll_->setToolTip(tr("Look up every book not yet looked up. An ISBN's single, agreeing "
                             "answer is taken; everything else waits for you under Review."));
    fetchAll_->setEnabled(false);
    review_ = toolbar->addAction(tr("Review matches"), this, &MainWindow::beginReview);
    review_->setObjectName(QStringLiteral("reviewMatches"));
    review_->setToolTip(tr("Go through what Fetch all found, one book at a time"));
    review_->setEnabled(false);
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
    batchBar_ = new QProgressBar(this);
    batchBar_->setObjectName(QStringLiteral("batchProgress"));
    batchBar_->setMaximumWidth(160);
    batchBar_->setTextVisible(false);
    batchBar_->hide();
    statusBar()->addPermanentWidget(batchBar_);
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
    addByIsbn_->setEnabled(!busy && enricher_ && catalogue_);
    if (fetchAll_)
        showBatchProgress();
    if (detail_->state() == ui::DetailPanel::State::Adding)
        statusBar()->showMessage(tr("Adding by ISBN — Esc cancels"));
    else if (detail_->state() == ui::DetailPanel::State::Fetching && reviewLeft_ > 0)
        statusBar()->showMessage(tr("Reviewing matches — Esc stops"));
    else if (detail_->state() == ui::DetailPanel::State::Fetching)
        statusBar()->showMessage(tr("Fetching metadata — Esc cancels"));
    else if (detail_->isEditing())
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
    // The batch run belongs to a catalogue; make it afresh for this one.
    setEnricher(enricher_);
}

void MainWindow::setEnricher(Enricher* enricher)
{
    if (enricher_)
        disconnect(enricher_, nullptr, this, nullptr);
    enricher_ = enricher;
    addByIsbn_->setEnabled(enricher_ && catalogue_ && !detail_->isBusy());
    if (batch_)
        batch_->stop();
    delete batch_;
    batch_ = nullptr;
    showBatchProgress();
    if (!enricher_ || !catalogue_)
        return;
    batch_ = new BatchEnricher(*catalogue_, *enricher_, this);
    connect(batch_, &BatchEnricher::progressed, this, &MainWindow::showBatchProgress);
    connect(batch_, &BatchEnricher::bookChanged, this, [this](qint64 id) { refreshBooks({id}); });
    connect(batch_, &BatchEnricher::finished, this, [this](const QString& problem) {
        const auto& progress = batch_->progress();
        const QString summary = tr("Fetch all: %1 looked up, %2 taken, %3 to review, %4 not found")
                                    .arg(progress.done)
                                    .arg(progress.matched)
                                    .arg(progress.toReview)
                                    .arg(progress.notFound);
        statusBar()->showMessage(problem.isEmpty() ? summary : summary + QStringLiteral(" — ") + problem);
    });
    showBatchProgress();
    connect(enricher_, &Enricher::waiting, this, [this](const QString& provider, int seconds) {
        const QString text = tr("%1 asked us to wait; trying again in about %2 s.").arg(provider).arg(seconds);
        if (detail_->state() == ui::DetailPanel::State::Fetching)
            detail_->setFetchProgress(text);
        statusBar()->showMessage(text);
    });
}

void MainWindow::fetchMetadata(qint64 bookId)
{
    if (!catalogue_)
        return;
    if (!enricher_) {
        statusBar()->showMessage(tr("Metadata cannot be fetched here: no network access was set up."));
        return;
    }
    const auto detail = catalogue_->detail(bookId);
    if (!detail)
        return;

    const auto& book = detail->book;
    const QString providers = enricher_->googleAvailable() ? tr("Open Library, then Google Books")
                                                           : tr("Open Library");
    const auto isbn = book.isbn13 ? book.isbn13 : book.isbn10;
    const QString how = isbn ? tr("Looking up ISBN %1 on %2…").arg(QString::fromStdString(*isbn), providers)
                             : tr("No ISBN, so searching %1 by title and author…").arg(providers);
    offer_.reset();
    detail_->beginFetch(how);

    enricher_->find(*detail, [this, bookId](FindResult result) {
        if (detail_->fetchingBookId() != bookId)
            return;
        if (result.problem) {
            detail_->showFetchProblem(tr("Nothing could be fetched: %1").arg(QString::fromStdString(*result.problem)));
            return;
        }
        if (result.candidates.empty()) {
            catalogue_->markLookupFailed(bookId);
            detail_->showFetchProblem(tr("Neither its ISBN nor its title and author are known to the "
                                         "providers asked. The book is marked as not found; its details "
                                         "are unchanged."));
            if (const auto summary = catalogue_->summary(bookId))
                list_->updateBook(*summary);
            return;
        }
        offer_ = Offer {bookId, result.candidates, result.byIsbn};
        detail_->offerCandidates(result.candidates, result.byIsbn);
    });
}

void MainWindow::useCandidate(qint64 bookId, int index)
{
    if (!catalogue_ || !enricher_ || !offer_ || offer_->bookId != bookId || index < 0
        || index >= static_cast<int>(offer_->candidates.size()))
        return;
    const auto candidate = offer_->candidates[static_cast<std::size_t>(index)];
    const bool byIsbn = offer_->byIsbn;
    detail_->setFetchProgress(tr("Fetching the synopsis…"));

    enricher_->complete(candidate, [this, bookId, byIsbn](domain::Candidate filled) {
        if (detail_->fetchingBookId() != bookId)
            return;
        offer_.reset();
        const auto result = catalogue_->enrich(bookId, filled, byIsbn);
        if (result.problem) {
            detail_->showFetchProblem(QString::fromStdString(*result.problem));
            return;
        }
        detail_->showNothing(); // out of Fetching, so the refresh may redraw
        if (result.coverUrl)
            fetchCover(bookId, *result.coverUrl, filled.source);
        if (reviewing_) {
            reviewing_.reset();
            refreshBooks({bookId});
            reviewNext();
            return;
        }
        refreshBooks({bookId});
        refreshRail();
        statusBar()->showMessage(tr("Details fetched for “%1”").arg(titleOf(bookId)), 4000);
    });
}

void MainWindow::fetchCover(qint64 bookId, const std::string& url, domain::Source source)
{
    const auto directory = catalogue_->dataDirectory();
    if (!directory)
        return;
    enricher_->fetchCover(bookId, url, *directory, [self = QPointer(this), bookId, source](metadata::CoverResult cover) {
        // Covers are never cancelled, so the window may be gone.
        if (!self)
            return;
        self->coverArrived(bookId, source, cover);
    });
}

void MainWindow::coverArrived(qint64 bookId, domain::Source source, const metadata::CoverResult& cover)
{
    // While reviewing, the panel has moved on; say nothing over it.
    if (!cover.relativePath) {
        // A missing cover is common and harmless; say so and move on.
        if (!reviewing_) {
            statusBar()->showMessage(tr("No cover for “%1”: %2")
                                         .arg(titleOf(bookId), QString::fromStdString(cover.error.value_or(""))),
                6000);
        }
        return;
    }
    if (const auto problem = catalogue_->setCover(bookId, *cover.relativePath, source)) {
        if (!reviewing_)
            statusBar()->showMessage(QString::fromStdString(*problem), 6000);
        return;
    }
    refreshBooks({bookId});
    if (!reviewing_)
        statusBar()->showMessage(tr("Cover saved for “%1”").arg(titleOf(bookId)), 4000);
}

void MainWindow::addByIsbn()
{
    if (!catalogue_ || !enricher_ || detail_->isBusy())
        return;
    list_->clearSelection();
    addOffer_.reset();
    addCover_.reset();
    detail_->beginAddByIsbn();
}

void MainWindow::lookUpForAdding(const QString& isbn13)
{
    auto* view = detail_->addView();
    const std::string isbn = isbn13.toStdString();
    // A book already held is not looked up, let alone added twice.
    if (const auto held = catalogue_->bookWithIsbn(isbn)) {
        view->showDuplicate(held->id, QString::fromStdString(held->title));
        return;
    }
    addOffer_.reset();
    addCover_.reset();
    view->showWaiting(tr("Looking up ISBN %1 on %2…")
                          .arg(isbn13, enricher_->googleAvailable()
                                  ? tr("Open Library, the British Library and Google Books")
                                  : tr("Open Library and the British Library")));
    enricher_->lookupIsbn(isbn, [this](FindResult result) {
        if (detail_->state() != ui::DetailPanel::State::Adding)
            return;
        auto* view = detail_->addView();
        if (result.problem || result.candidates.empty()) {
            const QString why = result.problem
                ? tr("Nothing could be looked up: %1").arg(QString::fromStdString(*result.problem))
                : tr("No provider knows this ISBN yet — a new or self-published book often isn't.");
            view->showSearch(why + QStringLiteral(" ") + tr("Search by title and author, or enter it by hand."),
                QString(), QString());
            return;
        }
        addOffer_ = AddOffer {result.candidates, true};
        view->showCandidates(result.candidates, true);
    });
}

void MainWindow::showAddCandidate(int index)
{
    if (!addOffer_ || index < 0 || index >= static_cast<int>(addOffer_->candidates.size()))
        return;
    const auto& candidate = addOffer_->candidates[static_cast<std::size_t>(index)];
    const auto credits = catalogue_->creditsFor(candidate.authors);
    std::vector<ui::AddByIsbnView::HeldBook> held;
    for (const auto& book : catalogue_->booksLike(candidate.title, credits)) {
        QString description = QString::fromStdString(book.title);
        if (book.authors)
            description += QStringLiteral(" — ") + QString::fromStdString(*book.authors);
        if (book.seriesLabel)
            description += QStringLiteral(" · ") + QString::fromStdString(*book.seriesLabel);
        held.push_back({book.id, description});
    }
    detail_->addView()->setCardDetails(credits, catalogue_->seriesProposals(candidate.title, credits, candidate), held);
    if (addCover_ && addCover_->first == index) {
        QPixmap cover;
        cover.loadFromData(addCover_->second);
        detail_->addView()->setCover(cover);
        return;
    }
    if (!candidate.coverUrl)
        return;
    enricher_->fetchImage(*candidate.coverUrl,
        [this, index](std::optional<QByteArray> bytes, std::optional<std::string>) {
            if (detail_->state() != ui::DetailPanel::State::Adding || !bytes
                || detail_->addView()->shownCandidate() != index)
                return;
            addCover_ = std::make_pair(index, *bytes);
            QPixmap cover;
            cover.loadFromData(*bytes);
            detail_->addView()->setCover(cover);
        });
}

void MainWindow::searchForAdding(const QString& title, const QString& author)
{
    auto* view = detail_->addView();
    domain::BookDetail detail;
    detail.book.title = title.toStdString();
    if (!author.isEmpty())
        detail.credits = {{author.toStdString(), domain::CreditRole::Author}};
    addOffer_.reset();
    addCover_.reset();
    view->showWaiting(tr("Searching for “%1”…").arg(title));
    enricher_->find(detail, [this, title, author](FindResult result) {
        if (detail_->state() != ui::DetailPanel::State::Adding)
            return;
        auto* view = detail_->addView();
        if (result.problem || result.candidates.empty()) {
            view->showSearch(result.problem
                    ? tr("Nothing could be looked up: %1").arg(QString::fromStdString(*result.problem))
                    : tr("Nothing found by that title and author. Try other spellings, or enter the "
                         "book by hand."),
                title, author);
            return;
        }
        addOffer_ = AddOffer {result.candidates, false};
        view->showCandidates(result.candidates, false);
    });
}

void MainWindow::addConfirmed(const ui::AddByIsbnView::Choice& choice)
{
    if (!addOffer_ || choice.candidate < 0 || choice.candidate >= static_cast<int>(addOffer_->candidates.size()))
        return;
    auto* view = detail_->addView();
    const auto& candidate = addOffer_->candidates[static_cast<std::size_t>(choice.candidate)];

    if (choice.existingBook) {
        // The copy already held takes the ISBN and the details, as a fetch
        // would write them (AV-001).
        const std::int64_t id = *choice.existingBook;
        const auto given = catalogue_->giveIsbn(id, view->isbn13(), view->isbn10(), candidate, addOffer_->byIsbn);
        if (given.problem) {
            view->showError(QString::fromStdString(*given.problem));
            return;
        }
        keepCover(id, given.coverUrl, candidate.source, choice.candidate);
        addOffer_.reset();
        addCover_.reset();
        detail_->showNothing();
        rail_->chooseFilter({});
        refreshBooks({id});
        list_->selectBook(id);
        list_->setFocus();
        statusBar()->showMessage(tr("“%1” now has ISBN %2").arg(titleOf(id), QString::fromStdString(view->isbn13())), 6000);
        return;
    }

    Catalogue::NewBook book;
    book.edit.book.title = choice.title;
    book.edit.book.subtitle = choice.subtitle;
    book.edit.book.isbn13 = view->isbn13();
    book.edit.book.isbn10 = view->isbn10();
    book.edit.book.readStatus = choice.readStatus;
    book.edit.credits = choice.credits;
    book.candidate = candidate;
    book.byIsbn = addOffer_->byIsbn;
    book.series = choice.series;

    const auto result = catalogue_->addBook(book);
    if (result.problem && result.id == 0) {
        view->showError(QString::fromStdString(*result.problem));
        return;
    }
    const std::int64_t id = result.id;
    keepCover(id, result.coverUrl, candidate.source, choice.candidate);
    addOffer_.reset();
    addCover_.reset();
    detail_->showNothing();

    rail_->chooseFilter({});
    list_->setBooks(catalogue_->summaries());
    refreshRail();
    list_->selectBook(id);
    list_->setFocus();
    QString message = tr("Added “%1”").arg(QString::fromStdString(choice.title));
    if (choice.series) {
        message += tr(" to %1").arg(QString::fromStdString(choice.series->seriesName));
        if (choice.series->heldAfter == choice.series->knownAfter)
            message += tr(", now complete");
    }
    if (result.problem)
        message += QStringLiteral(" — ") + QString::fromStdString(*result.problem);
    statusBar()->showMessage(message, 6000);
}

void MainWindow::keepCover(qint64 bookId, const std::optional<std::string>& url, domain::Source source,
    int candidate)
{
    if (!url)
        return; // none offered, or the owner's own cover stands (AV-001)
    // What the card showed is kept, not downloaded again (SPEC.md §4).
    const auto directory = catalogue_->dataDirectory();
    if (directory && addCover_ && addCover_->first == candidate) {
        const auto stored = enricher_->storeCover(bookId, addCover_->second, *directory);
        if (stored.relativePath) {
            catalogue_->setCover(bookId, *stored.relativePath, source);
            return;
        }
    }
    fetchCover(bookId, *url, source);
}

void MainWindow::addByHand()
{
    auto* view = detail_->addView();
    domain::BookDetail prefill;
    prefill.book.isbn13 = view->isbn13().empty() ? std::nullopt : std::optional(view->isbn13());
    prefill.book.isbn10 = view->isbn10();
    if (addOffer_ && view->shownCandidate() >= 0) {
        const auto& candidate = addOffer_->candidates[static_cast<std::size_t>(view->shownCandidate())];
        prefill.book.title = candidate.title;
        prefill.credits = catalogue_->creditsFor(candidate.authors);
    } else {
        prefill.book.title = view->searchedTitle().toStdString();
        if (!view->searchedAuthor().isEmpty())
            prefill.credits = catalogue_->creditsFor({view->searchedAuthor().toStdString()});
    }
    addOffer_.reset();
    addCover_.reset();
    enricher_->cancel();
    detail_->beginNew(prefill);
}

void MainWindow::stopAdding()
{
    if (enricher_)
        enricher_->cancel();
    addOffer_.reset();
    addCover_.reset();
    detail_->showNothing();
    refreshPanel();
}

void MainWindow::toggleBatch()
{
    if (!batch_)
        return;
    if (batch_->running())
        batch_->stop();
    else
        batch_->start();
}

void MainWindow::showBatchProgress()
{
    const bool busy = detail_->isBusy();
    if (!batch_) {
        fetchAll_->setEnabled(false);
        review_->setEnabled(false);
        batchBar_->hide();
        return;
    }
    const auto& progress = batch_->progress();
    fetchAll_->setText(batch_->running() ? tr("Stop fetching") : tr("Fetch all metadata"));
    fetchAll_->setEnabled(batch_->running() || !busy);
    const int waiting = batch_->pendingCount();
    review_->setText(waiting > 0 ? tr("Review matches (%1)").arg(waiting) : tr("Review matches"));
    review_->setEnabled(waiting > 0 && !busy);
    batchBar_->setVisible(batch_->running());
    if (batch_->running()) {
        batchBar_->setRange(0, std::max(progress.total, 1));
        batchBar_->setValue(progress.done);
        if (!busy) {
            statusBar()->showMessage(tr("Fetching metadata: %1 of %2 · %3 taken · %4 to review · %5 not found")
                                         .arg(progress.done)
                                         .arg(progress.total)
                                         .arg(progress.matched)
                                         .arg(waiting)
                                         .arg(progress.notFound));
        }
    }
}

void MainWindow::beginReview()
{
    if (!batch_ || batch_->pendingCount() == 0 || detail_->isBusy())
        return;
    // The book list, so the book under review can be shown selected.
    rail_->chooseFilter({});
    reviewLeft_ = batch_->pendingCount();
    reviewTotal_ = reviewLeft_;
    reviewPlace_ = 0;
    reviewNext();
}

void MainWindow::reviewNext()
{
    while (reviewLeft_ > 0) {
        auto match = batch_->takePending();
        if (!match)
            break;
        --reviewLeft_;
        ++reviewPlace_;
        // Fetched or edited since it was found: nothing to decide.
        const auto detail = catalogue_->detail(match->bookId);
        if (!detail || detail->book.metadataStatus != domain::MetadataStatus::Unmatched)
            continue;

        detail_->showNothing();
        list_->selectBook(match->bookId);
        detail_->showBook(*detail);
        detail_->beginReview(tr("%1 of %2 to review").arg(reviewPlace_).arg(reviewTotal_));
        offer_ = Offer {match->bookId, match->candidates, match->byIsbn};
        detail_->offerCandidates(match->candidates, match->byIsbn);
        reviewing_ = std::move(match);
        return;
    }
    endReview(batch_->pendingCount() > 0 ? tr("Review done; %1 skipped for later").arg(batch_->pendingCount())
                                         : tr("Review done"));
}

void MainWindow::endReview(const QString& message)
{
    reviewLeft_ = 0;
    if (detail_->state() == ui::DetailPanel::State::Fetching)
        detail_->showNothing();
    refreshPanel();
    showBatchProgress();
    statusBar()->showMessage(message, 6000);
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
