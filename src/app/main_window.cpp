#include "app/main_window.h"

#include "app/batch_enricher.h"
#include "app/catalogue.h"
#include "app/recent_catalogues.h"
#include "db/backup.h"
#include "db/connection.h"
#include "db/dump.h"
#include "db/migrations.h"
#include "io/csv_importer.h"
#include "app/enricher.h"
#include "ui/book_list_model.h"
#include "ui/book_list_view.h"
#include "db/db_error.h"
#include "domain/placeholder.h"
#include "ui/book_editor.h"
#include "ui/detail_panel.h"
#include "ui/filter_bar.h"
#include "ui/book_group_proxy.h"
#include "ui/add_by_isbn_view.h"
#include "ui/backup_view.h"
#include "ui/export_view.h"
#include "ui/report_view.h"
#include "ui/rail_view.h"
#include "ui/missing_page.h"
#include "ui/series_page.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QProgressBar>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>

namespace pinax::app {

namespace {

// Panel widths taken from the mock-up in design/: a narrow rail, a list that
// takes the slack, and a detail panel wide enough for the edition fields.
constexpr int railWidth = 190;
constexpr int listWidth = 690;
constexpr int detailWidth = 300;

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , splitter_(new QSplitter(Qt::Horizontal, this))
    , rail_(new ui::RailView(splitter_))
    , centre_(new QStackedWidget(splitter_))
    , seriesPage_(new ui::SeriesPage(centre_))
    , missingPage_(new ui::MissingPage(centre_))
    , listPage_(new QWidget(centre_))
    , filterBar_(new ui::FilterBar(listPage_))
    , list_(new ui::BookListView(listPage_))
    , detail_(new ui::DetailPanel(splitter_))
{
    setWindowTitle(QStringLiteral("Pinax"));
    rail_->setObjectName(QStringLiteral("rail"));
    centre_->setObjectName(QStringLiteral("centre"));
    list_->setObjectName(QStringLiteral("list"));
    seriesPage_->setObjectName(QStringLiteral("seriesPage"));
    // The book list under its filters (F-017).
    filterBar_->setObjectName(QStringLiteral("filterBar"));
    auto* listLayout = new QVBoxLayout(listPage_);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);
    listLayout->addWidget(filterBar_);
    listLayout->addWidget(list_, 1);
    connect(filterBar_, &ui::FilterBar::queryChanged, this, [this](const domain::BookQuery& query) {
        query_ = query;
        applyQuery();
    });
    connect(filterBar_, &ui::FilterBar::groupingChanged, this, &MainWindow::applyGrouping);
    centre_->addWidget(listPage_);
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
            enricher_->cancelPreviews();
            batch_->putBack(std::move(*reviewing_), true);
            reviewing_.reset();
            offer_.reset();
            endReview(tr("Review stopped"));
            return;
        }
        if (enricher_) {
            enricher_->cancel();
            enricher_->cancelPreviews();
        }
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
    backUp_ = toolbar->addAction(tr("Back up"), this, &MainWindow::beginBackup);
    backUp_->setObjectName(QStringLiteral("backUp"));
    backUp_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    backUp_->setToolTip(tr("A checked copy of the catalogue, wherever you choose (Ctrl+B)"));
    connect(detail_->backupView(), &ui::BackupView::backupRequested, this, &MainWindow::backUp);
    connect(detail_->backupView(), &ui::BackupView::closed, this, [this] {
        detail_->showNothing();
        refreshPanel();
    });
    export_ = toolbar->addAction(tr("Export"), this, &MainWindow::beginExport);
    export_->setObjectName(QStringLiteral("export"));
    export_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    export_->setToolTip(tr("The catalogue as a file for other programs (Ctrl+E)"));
    detail_->exportView()->setFormats({
        {tr("SQL dump"), QStringLiteral("sql"),
            tr("Plain SQL that recreates the whole catalogue on an empty database: readable, "
               "fit for version control, and needing nothing of Pinax. Restore with "
               "sqlite3 restored.db < file.sql. Checked by restoring it before it is kept.")},
        {tr("Excel workbook"), QStringLiteral("xlsx"),
            tr("Three sheets — Books, Series status, Authors — with the figures Pinax shows: series "
               "status and missing volumes as computed values, not formulas. Opens in Excel and "
               "LibreOffice.")},
        {tr("CSV of the book list"), QStringLiteral("csv"),
            tr("The books the list shows now — filters, search and sort as they are — in Pinax's "
               "import format, so the file can be imported again; checked by doing so before it is "
               "kept. It carries what that format carries: not synopses, genres or covers, which the "
               "SQL dump keeps.")},
    });
    connect(detail_->exportView(), &ui::ExportView::exportRequested, this, &MainWindow::exportTo);
    // Choose…: the save dialogue fills the path (D-027).
    connect(detail_->backupView(), &ui::BackupView::chooseRequested, this, [this] {
        const QString path = chooseFile({FileRequest::Kind::Save, tr("Back Up To"), detail_->backupView()->path(),
            tr("Pinax catalogues (*.db)"), true});
        if (!path.isEmpty())
            detail_->backupView()->setPath(path);
    });
    connect(detail_->exportView(), &ui::ExportView::chooseRequested, this, [this] {
        const QString path = chooseFile({FileRequest::Kind::Save, tr("Export To"), detail_->exportView()->path(),
            QString(), true});
        if (!path.isEmpty())
            detail_->exportView()->setPath(path);
    });
    connect(detail_->reportView(), &ui::ReportView::closed, this, [this] {
        detail_->showNothing();
        refreshPanel();
    });
    connect(detail_->exportView(), &ui::ExportView::closed, this, [this] {
        detail_->showNothing();
        refreshPanel();
    });
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

    buildMenus();
    updateActions();
    updateTitle();
    resize(railWidth + listWidth + detailWidth, 700);
}

MainWindow::~MainWindow()
{
    // The batch run refers to the catalogue: stopped and gone before it is.
    delete batch_;
    batch_ = nullptr;
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
    filterBar_->setEnabled(!busy);
    updateActions();
    if (detail_->state() == ui::DetailPanel::State::Adding)
        statusBar()->showMessage(tr("Adding by ISBN — Esc cancels"));
    else if (detail_->state() == ui::DetailPanel::State::BackingUp)
        statusBar()->showMessage(tr("Backing up — Esc closes"));
    else if (detail_->state() == ui::DetailPanel::State::Exporting)
        statusBar()->showMessage(tr("Exporting — Esc closes"));
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
    if (owned_ && owned_.get() != catalogue)
        owned_.reset(); // one the window opened, now replaced by the caller's
    query_ = {};
    filterBar_->setQuery(query_);
    list_->setBooks(catalogue_ ? catalogue_->summaries() : std::vector<domain::BookSummary> {});
    refreshRail();
    // The batch run belongs to a catalogue; make it afresh for this one.
    setEnricher(enricher_);
    detail_->showNothing();
    updateActions();
    updateTitle();
    if (!catalogue_)
        statusBar()->showMessage(tr("No catalogue open — File ▸ Open or New"));
}

// ---------------------------------------------------------------------------
// Catalogue files and menus (F-026 to F-028, D-027, D-028)

void MainWindow::buildMenus()
{
    QMenu* file = menuBar()->addMenu(tr("&File"));
    newCatalogue_ = file->addAction(tr("&New Catalogue…"), this, &MainWindow::newCatalogueChosen);
    newCatalogue_->setObjectName(QStringLiteral("newCatalogue"));
    newCatalogue_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
    openCatalogue_ = file->addAction(tr("&Open Catalogue…"), this, &MainWindow::openCatalogueChosen);
    openCatalogue_->setObjectName(QStringLiteral("openCatalogue"));
    openCatalogue_->setShortcut(QKeySequence::Open);
    recentMenu_ = file->addMenu(tr("Open &Recent"));
    recentMenu_->setObjectName(QStringLiteral("recentMenu"));
    connect(recentMenu_, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);
    closeCatalogue_ = file->addAction(tr("&Close Catalogue"), this, &MainWindow::closeCatalogue);
    closeCatalogue_->setObjectName(QStringLiteral("closeCatalogue"));
    closeCatalogue_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
    file->addSeparator();
    QMenu* import = file->addMenu(tr("&Import"));
    importCsv_ = import->addAction(tr("Books from &CSV…"), this, &MainWindow::importCsvChosen);
    importCsv_->setObjectName(QStringLiteral("importCsv"));
    importCsv_->setToolTip(tr("Merge books from a CSV file into this catalogue (F-028)"));
    importDump_ = import->addAction(tr("Catalogue from &SQL Dump…"), this, &MainWindow::importDumpChosen);
    importDump_->setObjectName(QStringLiteral("importDump"));
    restore_ = file->addAction(tr("&Restore from Backup…"), this, &MainWindow::restoreChosen);
    restore_->setObjectName(QStringLiteral("restore"));
    file->addAction(export_);
    file->addAction(backUp_);
    file->addSeparator();
    quit_ = file->addAction(tr("&Quit"), this, &QWidget::close);
    quit_->setObjectName(QStringLiteral("quit"));
    quit_->setShortcut(QKeySequence::Quit);

    QMenu* books = menuBar()->addMenu(tr("&Books"));
    books->addAction(addBook_);
    books->addAction(addByIsbn_);
    find_ = books->addAction(tr("&Find…"), this, [this] {
        // To the search, from anywhere in the window (F-019).
        if (!catalogue_ || detail_->isBusy())
            return;
        if (centre_->currentWidget() != listPage_)
            applyQuery(); // back to the list, filters as they were
        filterBar_->focusSearch();
    });
    find_->setObjectName(QStringLiteral("find"));
    find_->setShortcut(QKeySequence::Find);
    books->addSeparator();
    books->addAction(fetchAll_);
    books->addAction(review_);

    QMenu* view = menuBar()->addMenu(tr("&View"));
    QMenu* theme = view->addMenu(tr("&Theme"));
    theme->setObjectName(QStringLiteral("themeMenu"));
    auto* themes = new QActionGroup(this);
    for (const auto& [choice, name] : {std::pair {ui::Theme::System, tr("&System")},
             std::pair {ui::Theme::Light, tr("&Light")}, std::pair {ui::Theme::Dark, tr("&Dark")}}) {
        QAction* action = theme->addAction(name, this, [this, choice = choice] { chooseTheme(choice); });
        action->setObjectName(QStringLiteral("theme.") + ui::themeKey(choice));
        action->setCheckable(true);
        themes->addAction(action);
        themeActions_.append(action);
    }
    themeActions_.front()->setChecked(true);

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    about_ = help->addAction(tr("&About Pinax"), this, [this] {
        detail_->showReport(tr("Pinax %1").arg(QCoreApplication::applicationVersion()),
            tr("A catalogue for a personal physical library: what is on the shelf, what has been read, "
               "and what each series still lacks.\n\nNamed for the Pinakes, Callimachus's catalogue of "
               "the Library of Alexandria."));
    });
}

void MainWindow::updateActions()
{
    const bool busy = detail_->isBusy();
    const bool open = catalogue_ != nullptr;
    for (QAction* action : {newCatalogue_, openCatalogue_, importDump_, restore_})
        if (action)
            action->setEnabled(!busy);
    if (recentMenu_)
        recentMenu_->setEnabled(!busy);
    for (QAction* action : {closeCatalogue_, importCsv_, find_, addBook_, backUp_, export_})
        if (action)
            action->setEnabled(!busy && open);
    if (addByIsbn_)
        addByIsbn_->setEnabled(!busy && open && enricher_);
    if (fetchAll_)
        showBatchProgress();
}

void MainWindow::updateTitle()
{
    const QString path = cataloguePath();
    setWindowTitle(path.isEmpty() ? QStringLiteral("Pinax")
                                  : QStringLiteral("%1 — Pinax").arg(QFileInfo(path).fileName()));
    setWindowFilePath(path);
}

QString MainWindow::cataloguePath() const
{
    if (!catalogue_ || catalogue_->path() == ":memory:")
        return {};
    return QFileInfo(QString::fromStdString(catalogue_->path())).absoluteFilePath();
}

void MainWindow::setFileChooser(FileChooser chooser)
{
    chooser_ = std::move(chooser);
}

void MainWindow::setBackupFolder(const QString& folder)
{
    backupFolder_ = folder;
}

void MainWindow::setSettings(QSettings* settings)
{
    settings_ = settings;
}

// ---------------------------------------------------------------------------
// Theme (F-029, D-030)

namespace {
const QString themeSetting = QStringLiteral("appearance/theme");
}

void MainWindow::applySavedTheme()
{
    const auto theme = ui::themeFromKey(settings().value(themeSetting).toString()).value_or(ui::Theme::System);
    ui::applyTheme(theme);
    themeAction(theme)->setChecked(true);
}

void MainWindow::chooseTheme(ui::Theme theme)
{
    ui::applyTheme(theme);
    themeAction(theme)->setChecked(true);
    settings().setValue(themeSetting, ui::themeKey(theme));
    // The rail's counts and the panel's pills and bars bake their colours;
    // drawn again, they take the new palette. An open form is left alone.
    refreshRail();
    refreshPanel();
}

QAction* MainWindow::themeAction(ui::Theme theme) const
{
    return themeActions_.at(static_cast<int>(theme));
}

QSettings& MainWindow::settings()
{
    if (settings_)
        return *settings_;
    if (!ownSettings_)
        ownSettings_ = std::make_unique<QSettings>(); // ~/.config/Pinax/Pinax.conf
    return *ownSettings_;
}

QString MainWindow::chooseFile(const FileRequest& request)
{
    if (chooser_)
        return chooser_(request);
    // The one kind of dialogue Pinax shows (D-027).
    return request.save ? QFileDialog::getSaveFileName(this, request.caption, request.start, request.filter)
                        : QFileDialog::getOpenFileName(this, request.caption, request.start, request.filter);
}

QString MainWindow::documentsFolder(const QString& sub) const
{
    QString folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (folder.isEmpty())
        folder = QDir::homePath();
    return folder + QLatin1Char('/') + sub;
}

void MainWindow::rebuildRecentMenu()
{
    recentMenu_->clear();
    const QStringList recent = RecentCatalogues(settings()).list();
    for (const QString& path : recent) {
        QAction* action = recentMenu_->addAction(QFileInfo(path).fileName(), this, [this, path] {
            if (!detail_->isBusy())
                openCatalogue(path);
        });
        action->setToolTip(path);
        action->setData(path);
        action->setEnabled(path != cataloguePath());
    }
    if (recent.isEmpty())
        recentMenu_->addAction(tr("No catalogues yet"))->setEnabled(false);
}

bool MainWindow::openCatalogue(const QString& path, bool create)
{
    const QString file = QFileInfo(path).absoluteFilePath();
    const auto found = db::inspect(file.toStdString());
    auto refuse = [this, &file](const QString& why) {
        detail_->showReport(tr("Not opened"), tr("%1\n\n%2").arg(file, why), true);
        statusBar()->showMessage(tr("Not opened: %1").arg(why), 6000);
        return false;
    };
    if (create && !found.empty)
        return refuse(tr("A file is already there. Open it, or choose another name for the new catalogue."));
    if (!create && !found.catalogue) {
        return refuse(found.exists ? tr("This is not a Pinax catalogue: %1.").arg(QString::fromStdString(found.problem))
                                   : tr("There is no such file."));
    }
    if (found.catalogue && found.version > db::latestSchemaVersion) {
        return refuse(tr("It was made by a newer Pinax (schema version %1; this one knows %2). It has not been "
                         "touched.")
                          .arg(found.version)
                          .arg(db::latestSchemaVersion));
    }
    std::unique_ptr<Catalogue> opened;
    try {
        QDir().mkpath(QFileInfo(file).absolutePath());
        opened = std::make_unique<Catalogue>(file.toStdString());
    } catch (const db::DbError& error) {
        return refuse(QString::fromUtf8(error.what()));
    }
    adoptCatalogue(std::move(opened));
    statusBar()->showMessage(create ? tr("New catalogue %1").arg(QFileInfo(file).fileName())
                                    : tr("%1 · %2 books").arg(QFileInfo(file).fileName()).arg(catalogue_->count()),
        6000);
    return true;
}

void MainWindow::adoptCatalogue(std::unique_ptr<Catalogue> catalogue)
{
    // The old one stays alive until the window has let go of it.
    std::unique_ptr<Catalogue> previous = std::move(owned_);
    owned_ = std::move(catalogue);
    setCatalogue(owned_.get());
    previous.reset();
    if (owned_ && owned_->path() != ":memory:")
        RecentCatalogues(settings()).remember(cataloguePath());
}

void MainWindow::closeCatalogue()
{
    if (detail_->isBusy())
        return;
    setCatalogue(nullptr);
    owned_.reset();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (detail_->isEditing()) {
        statusBar()->showMessage(tr("A form is open — save or cancel it (Esc) before quitting"), 6000);
        event->ignore();
        return;
    }
    if (batch_)
        batch_->stop();
    QMainWindow::closeEvent(event);
}

QString MainWindow::safetyBackup(const QString& why, QString& problem)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-ddTHH-mm-ss"));
    const QString folder = backupFolder_.isEmpty() ? documentsFolder(QStringLiteral("Pinax backups")) : backupFolder_;
    const QString path = folder + QStringLiteral("/pinax-before-%1-%2.db").arg(why, stamp);
    const auto result = catalogue_->backupTo(path.toStdString());
    if (result.problem) {
        problem = QString::fromStdString(*result.problem);
        return {};
    }
    return QString::fromStdString(result.path);
}

void MainWindow::newCatalogueChosen()
{
    if (detail_->isBusy())
        return;
    const QString path = chooseFile({FileRequest::Kind::NewCatalogue, tr("New Catalogue"),
        documentsFolder(QStringLiteral("pinax.db")), tr("Pinax catalogues (*.db)"), true});
    if (path.isEmpty())
        return;
    openCatalogue(path.endsWith(QStringLiteral(".db")) ? path : path + QStringLiteral(".db"), true);
}

void MainWindow::openCatalogueChosen()
{
    if (detail_->isBusy())
        return;
    const QString start = cataloguePath().isEmpty() ? documentsFolder(QString()) : QFileInfo(cataloguePath()).absolutePath();
    const QString path = chooseFile({FileRequest::Kind::OpenCatalogue, tr("Open Catalogue"), start,
        tr("Pinax catalogues and backups (*.db);;All files (*)"), false});
    if (!path.isEmpty())
        openCatalogue(path);
}

void MainWindow::importCsvChosen()
{
    if (!catalogue_ || detail_->isBusy())
        return;
    const QString csv = chooseFile({FileRequest::Kind::ImportCsv, tr("Import Books from CSV"), documentsFolder(QString()),
        tr("CSV files (*.csv);;All files (*)"), false});
    if (csv.isEmpty())
        return;
    QString problem;
    const QString safety = safetyBackup(QStringLiteral("import"), problem);
    if (safety.isEmpty()) {
        detail_->showReport(tr("Not imported"), tr("The catalogue could not be backed up first, so nothing was "
                                                   "imported: %1").arg(problem), true);
        return;
    }
    const auto report = io::CsvImporter(catalogue_->connection()).importFile(csv.toStdString());
    QStringList lines;
    if (report.aborted) {
        lines << tr("Nothing was imported: the file could not be read as a whole.");
    } else {
        lines << tr("%1 new, %2 updated, %3 unchanged, %4 failed.")
                     .arg(report.inserted)
                     .arg(report.updated)
                     .arg(report.unchanged)
                     .arg(report.failures.size());
    }
    for (const auto& failure : report.failures) {
        lines << (failure.line > 0 ? tr("Line %1: %2").arg(failure.line).arg(QString::fromStdString(failure.message))
                                   : QString::fromStdString(failure.message));
    }
    lines << QString() << tr("Before importing, the catalogue was backed up to %1.").arg(safety);
    list_->setBooks(catalogue_->summaries());
    refreshRail();
    applyQuery();
    detail_->showReport(tr("Imported %1").arg(QFileInfo(csv).fileName()), lines.join(QLatin1Char('\n')),
        report.aborted || !report.failures.empty());
}

void MainWindow::importDumpChosen()
{
    if (detail_->isBusy())
        return;
    const QString dump = chooseFile({FileRequest::Kind::ImportDump, tr("Import Catalogue from SQL Dump"),
        documentsFolder(QStringLiteral("Pinax exports")), tr("SQL dumps (*.sql);;All files (*)"), false});
    if (dump.isEmpty())
        return;
    const QString suggested = QFileInfo(dump).absoluteDir().filePath(QFileInfo(dump).completeBaseName() + QStringLiteral(".db"));
    QString target = chooseFile({FileRequest::Kind::DumpTarget, tr("Save the Restored Catalogue As"), suggested,
        tr("Pinax catalogues (*.db)"), true});
    if (target.isEmpty())
        return;
    if (!target.endsWith(QStringLiteral(".db")))
        target += QStringLiteral(".db");
    if (QFileInfo(target).absoluteFilePath() == cataloguePath()) {
        detail_->showReport(tr("Not restored"), tr("That is the catalogue open now; choose a new file."), true);
        return;
    }
    try {
        const auto report = db::restoreDump(dump.toStdString(), target.toStdString());
        if (openCatalogue(QString::fromStdString(report.path))) {
            detail_->showReport(tr("Catalogue restored"), tr("%1 books rebuilt from %2 into %3, checked, and opened.")
                                                             .arg(report.books)
                                                             .arg(QFileInfo(dump).fileName(),
                                                                 QString::fromStdString(report.path)));
        }
    } catch (const db::DbError& error) {
        detail_->showReport(tr("Not restored"), QString::fromUtf8(error.what()), true);
    }
}

void MainWindow::restoreChosen()
{
    if (detail_->isBusy())
        return;
    const QString source = chooseFile({FileRequest::Kind::RestoreBackup, tr("Restore from Backup"),
        documentsFolder(QStringLiteral("Pinax backups")), tr("Pinax backups (*.db);;All files (*)"), false});
    if (source.isEmpty())
        return;
    const auto found = db::inspect(source.toStdString());
    if (!found.catalogue || found.version > db::latestSchemaVersion) {
        detail_->showReport(tr("Not restored"),
            found.catalogue ? tr("The backup was made by a newer Pinax.")
                            : tr("%1 is not a Pinax backup: %2.").arg(source, QString::fromStdString(found.problem)),
            true);
        return;
    }

    // Over the catalogue open now; with none open, wherever the owner says,
    // the catalogue last open suggested (BUG-006).
    QString target = cataloguePath();
    if (target.isEmpty()) {
        QString suggested = RecentCatalogues(settings()).last();
        if (suggested.isEmpty())
            suggested = documentsFolder(QStringLiteral("pinax.db"));
        target = chooseFile({FileRequest::Kind::Save, tr("Restore Into"), suggested, tr("Pinax catalogues (*.db)"), true});
        if (target.isEmpty())
            return;
        if (!target.endsWith(QStringLiteral(".db")))
            target += QStringLiteral(".db");
        target = QFileInfo(target).absoluteFilePath();
    }
    if (QFileInfo(source).absoluteFilePath() == target) {
        detail_->showReport(tr("Not restored"), tr("The backup and the catalogue it would replace are the same file."), true);
        return;
    }
    const auto replacing = db::inspect(target.toStdString());
    if (!replacing.empty && !replacing.catalogue) {
        detail_->showReport(tr("Not restored"),
            tr("%1 is not a Pinax catalogue, so it is not replaced: %2.").arg(target, QString::fromStdString(replacing.problem)),
            true);
        return;
    }

    // Whatever is replaced is kept first.
    QString safety;
    if (replacing.catalogue) {
        QString problem;
        if (catalogue_) {
            safety = safetyBackup(QStringLiteral("restore"), problem);
        } else {
            try {
                db::Connection closed(target.toStdString());
                const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-ddTHH-mm-ss"));
                const QString folder = backupFolder_.isEmpty() ? documentsFolder(QStringLiteral("Pinax backups")) : backupFolder_;
                safety = QString::fromStdString(
                    db::backupTo(closed, (folder + QStringLiteral("/pinax-before-restore-%1.db").arg(stamp)).toStdString()).path);
            } catch (const db::DbError& error) {
                problem = QString::fromUtf8(error.what());
            }
        }
        if (safety.isEmpty()) {
            detail_->showReport(tr("Not restored"), tr("The catalogue could not be backed up first, so nothing was "
                                                       "replaced: %1").arg(problem), true);
            return;
        }
    }

    // Closed, so nothing holds the file while it is replaced.
    closeCatalogue();
    QString outcome;
    bool failed = false;
    try {
        const auto report = db::restoreFrom(source.toStdString(), target.toStdString());
        outcome = tr("%1 books restored from %2 into %3.").arg(report.books).arg(source, target);
    } catch (const db::DbError& error) {
        outcome = tr("The catalogue was not replaced: %1").arg(QString::fromUtf8(error.what()));
        failed = true;
    }
    if (!QFileInfo::exists(target)) {
        detail_->showReport(tr("Not restored"), outcome, true);
        return;
    }
    if (openCatalogue(target)) {
        QString body = outcome;
        if (!safety.isEmpty())
            body += QStringLiteral("\n\n") + tr("The catalogue as it was is kept at %1.").arg(safety);
        detail_->showReport(failed ? tr("Not restored") : tr("Catalogue restored"), body, failed);
    }
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
        previewCovers(bookId, result.candidates);
    });
}

void MainWindow::previewCovers(qint64 bookId, const std::vector<domain::Candidate>& candidates)
{
    enricher_->cancelPreviews();
    const unsigned serial = ++offerSerial_;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (!candidates[i].coverUrl)
            continue;
        enricher_->fetchImage(*candidates[i].coverUrl,
            [this, bookId, serial, index = static_cast<int>(i)](std::optional<QByteArray> bytes,
                std::optional<std::string>) {
                if (!bytes || serial != offerSerial_ || detail_->fetchingBookId() != bookId)
                    return;
                QPixmap cover;
                if (cover.loadFromData(*bytes))
                    detail_->setCandidateCover(index, cover);
            },
            true);
    }
}

void MainWindow::useCandidate(qint64 bookId, int index, bool myEdition)
{
    // Chosen: the other covers are not needed, and must not hold up this one's.
    if (enricher_)
        enricher_->cancelPreviews();
    if (!catalogue_ || !enricher_ || !offer_ || offer_->bookId != bookId || index < 0
        || index >= static_cast<int>(offer_->candidates.size()))
        return;
    const auto candidate = offer_->candidates[static_cast<std::size_t>(index)];
    // The owner's edition: an ISBN's answer, or one they say is theirs (D-029).
    const bool ownersEdition = offer_->byIsbn || myEdition;
    const bool confirmedSearch = myEdition && !offer_->byIsbn; // the cover's edition is fetched
    detail_->setFetchProgress(tr("Fetching the synopsis…"));

    enricher_->complete(candidate, [this, bookId, ownersEdition](domain::Candidate filled) {
        if (detail_->fetchingBookId() != bookId)
            return;
        offer_.reset();
        const auto result = catalogue_->enrich(bookId, filled, ownersEdition);
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
    }, Enricher::Channel::Interactive, confirmedSearch);
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
    // Only the card on show wants its cover.
    enricher_->cancelPreviews();
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
    enricher_->cancelPreviews();
    detail_->beginNew(prefill);
}

void MainWindow::stopAdding()
{
    if (enricher_) {
        enricher_->cancel();
        enricher_->cancelPreviews();
    }
    addOffer_.reset();
    addCover_.reset();
    detail_->showNothing();
    refreshPanel();
}

void MainWindow::beginBackup()
{
    if (!catalogue_ || detail_->isBusy())
        return;
    QString folder = lastBackupFolder_;
    if (folder.isEmpty())
        folder = backupFolder_.isEmpty() ? documentsFolder(QStringLiteral("Pinax backups")) : backupFolder_;
    const QString name = QStringLiteral("pinax-%1.db").arg(QDate::currentDate().toString(Qt::ISODate));
    list_->clearSelection();
    detail_->beginBackup(QDir(folder).filePath(name));
}

void MainWindow::backUp(const QString& path)
{
    if (!catalogue_)
        return;
    const QString target = QDir::cleanPath(QDir::fromNativeSeparators(path).startsWith(QLatin1Char('~'))
            ? QDir::homePath() + path.mid(1)
            : path);
    const auto result = catalogue_->backupTo(target.toStdString());
    auto* view = detail_->backupView();
    if (result.problem) {
        view->showProblem(tr("Not backed up: %1").arg(QString::fromStdString(*result.problem)));
        return;
    }
    lastBackupFolder_ = QFileInfo(QString::fromStdString(result.path)).absolutePath();
    const QString message = tr("Backed up %1 books to %2. The copy was checked: it opens, passes SQLite's "
                               "integrity check and holds every book.")
                                .arg(result.books)
                                .arg(QString::fromStdString(result.path));
    view->showDone(message);
    statusBar()->showMessage(tr("Backed up %1 books").arg(result.books), 6000);
}

void MainWindow::beginExport()
{
    if (!catalogue_ || detail_->isBusy())
        return;
    QString folder = lastExportFolder_;
    if (folder.isEmpty()) {
        folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        if (folder.isEmpty())
            folder = QDir::homePath();
        folder += QStringLiteral("/Pinax exports");
    }
    list_->clearSelection();
    detail_->beginExport(folder, QStringLiteral("pinax-%1").arg(QDate::currentDate().toString(Qt::ISODate)));
}

void MainWindow::exportTo(int format, const QString& path)
{
    if (!catalogue_)
        return;
    const QString target = QDir::cleanPath(path.startsWith(QLatin1Char('~')) ? QDir::homePath() + path.mid(1) : path);
    auto* view = detail_->exportView();
    Catalogue::ExportResult result;
    QString what;
    switch (format) {
    case 0:
        result = catalogue_->dumpTo(target.toStdString());
        what = tr("as SQL");
        break;
    case 1:
        result = catalogue_->exportWorkbook(target.toStdString());
        what = tr("as an Excel workbook");
        break;
    case 2: {
        const auto shown = list_->shownBooks();
        result = catalogue_->exportCsv(std::vector<std::int64_t>(shown.begin(), shown.end()), target.toStdString());
        what = tr("as CSV");
        break;
    }
    default:
        return;
    }
    if (result.problem) {
        view->showProblem(tr("Not exported: %1").arg(QString::fromStdString(*result.problem)));
        return;
    }
    lastExportFolder_ = QFileInfo(QString::fromStdString(result.path)).absolutePath();
    QString checked;
    if (format == 0)
        checked = tr(" Checked by restoring it into an empty database: everything came back as it is.");
    else if (format == 2)
        checked = tr(" Checked by importing it into an empty catalogue: it reads back without loss.");
    view->showDone(tr("Wrote %1 books %2 to %3.%4")
                       .arg(result.books)
                       .arg(what, QString::fromStdString(result.path), checked));
    statusBar()->showMessage(tr("Exported %1 books").arg(result.books), 6000);
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
        previewCovers(match->bookId, match->candidates);
        reviewing_ = std::move(match);
        return;
    }
    endReview(batch_->pendingCount() > 0 ? tr("Review done; %1 skipped for later").arg(batch_->pendingCount())
                                         : tr("Review done"));
}

void MainWindow::endReview(const QString& message)
{
    reviewLeft_ = 0;
    enricher_->cancelPreviews();
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
    refreshFilterOptions();
    // What search finds each book by (F-019); the list holds still until
    // the search next changes.
    list_->setSearchTexts(catalogue_ ? catalogue_->searchTexts() : std::map<std::int64_t, std::string> {});
    // The edit form's series to choose from (BUG-005), every one known.
    std::vector<domain::FilterOption> series;
    for (const auto& status : contents.series)
        series.push_back({status.id, status.name, status.held});
    detail_->editor()->setSeriesChoices(series);
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
    // The book list: All books clears every filter; a read state sets that
    // one and keeps the others (F-017).
    if (filter.kind == domain::BookFilter::Kind::All)
        query_ = {};
    else if (filter.kind == domain::BookFilter::Kind::ReadState)
        query_.readStatus = filter.readStatus;
    centre_->setCurrentWidget(listPage_);
    applyQuery();
}

void MainWindow::applyQuery()
{
    if (!catalogue_)
        return;
    centre_->setCurrentWidget(listPage_);
    const auto ids = catalogue_->matchingIds(query_);
    list_->showOnly(ids ? std::optional(QList<qint64>(ids->begin(), ids->end())) : std::nullopt);
    list_->search(QString::fromStdString(query_.text));
    list_->clearSelection();
    filterBar_->setQuery(query_);
    const int total = static_cast<int>(catalogue_->count());
    filterBar_->setShown(list_->shownCount(), total);
    // The rail shows the read state held, or All books.
    rail_->showFilter(query_.readStatus
            ? domain::BookFilter {domain::BookFilter::Kind::ReadState, *query_.readStatus, 0}
            : domain::BookFilter {});
    statusBar()->showMessage(query_.empty() ? tr("All books · %1").arg(total)
                                            : tr("%1 of %2 shown").arg(list_->shownCount()).arg(total));
}

void MainWindow::refreshFilterOptions()
{
    if (!catalogue_)
        return;
    filterBar_->setOptions(catalogue_->genreOptions(), catalogue_->authorOptions(), catalogue_->seriesOptions());
    if (filterBar_->grouping() == ui::Grouping::Genre)
        applyGrouping();
}

void MainWindow::applyGrouping()
{
    if (!catalogue_)
        return;
    const auto grouping = filterBar_->grouping();
    list_->setGrouping(grouping, grouping == ui::Grouping::Genre ? catalogue_->genresByBook()
                                                                 : std::map<std::int64_t, std::vector<std::string>> {});
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
