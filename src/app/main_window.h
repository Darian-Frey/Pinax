#pragma once

#include "app/batch_enricher.h"
#include "metadata/cover_cache.h"
#include "domain/book_edit.h"
#include "ui/add_by_isbn_view.h"
#include "ui/theme.h"
#include "domain/candidate.h"
#include "domain/book_filter.h"
#include "domain/book_query.h"
#include "domain/missing_row.h"
#include "domain/series_entry.h"

#include <QList>
#include <QMainWindow>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QAction;
class QMenu;
class QSettings;
class QProgressBar;
class QSplitter;
class QStackedWidget;
class QWidget;

namespace pinax::ui {
class BookListView;
class FilterBar;
class DetailPanel;
class MissingPage;
class RailView;
class SeriesPage;
}

namespace pinax::app {

class Catalogue;
class Enricher;

// The application shell: filter rail, list and detail panel side by side in a
// splitter (D-010). The list and detail panel are the ui module's; the rail
// is an empty placeholder until it supplies one. Selection in the list drives
// the panel, and the panel's edits are saved through the Catalogue.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Shows this catalogue's books. The catalogue must outlive the window;
    // the window does not own it (tests). nullptr shows none.
    void setCatalogue(Catalogue* catalogue);

    // Catalogue files (F-026). The window owns what it opens.
    // Opens the catalogue at `path` — creating it if `create` — and shows it;
    // the one open before is closed. Refuses a file that is not a Pinax
    // catalogue, or one from a newer Pinax, saying why in the panel.
    bool openCatalogue(const QString& path, bool create = false);
    // Takes a catalogue already opened, as main opens one for its imports.
    void adoptCatalogue(std::unique_ptr<Catalogue> catalogue);
    void closeCatalogue();
    // The open catalogue's file, or empty.
    QString cataloguePath() const;

    // Choosing a file (D-027): the system's dialogue unless a test supplies
    // its own chooser. An empty answer is "cancelled".
    struct FileRequest {
        enum class Kind { OpenCatalogue, NewCatalogue, ImportCsv, ImportDump, DumpTarget, RestoreBackup, Save };
        Kind kind = Kind::OpenCatalogue;
        QString caption;
        QString start;
        QString filter;
        bool save = false;
    };
    using FileChooser = std::function<QString(const FileRequest&)>;
    void setFileChooser(FileChooser chooser);
    // Where recent catalogues are remembered (D-028); Pinax's own settings
    // file unless a test supplies one.
    void setSettings(QSettings* settings);
    // The theme remembered in the settings (F-029), applied and shown as
    // chosen in View ▸ Theme. Called once at start-up, after setSettings.
    void applySavedTheme();
    // As if chosen from View ▸ Theme: applied, remembered, and the rail and
    // panel redrawn in it.
    void chooseTheme(ui::Theme theme);
    QAction* themeAction(ui::Theme theme) const;
    QMenu* recentMenu() const { return recentMenu_; }
    // Where backups go — the safety copies before an import or restore, and
    // the suggestion for Back up; Documents/Pinax backups unless set.
    void setBackupFolder(const QString& folder);
    // Looks books up for "Fetch metadata". Without one, Fetch says so. The
    // enricher must outlive the window.
    void setEnricher(Enricher* enricher);

    QSplitter* splitter() const { return splitter_; }
    ui::BookListView* bookList() const { return list_; }
    ui::DetailPanel* detailPanel() const { return detail_; }
    ui::RailView* rail() const { return rail_; }
    ui::SeriesPage* seriesPage() const { return seriesPage_; }
    ui::MissingPage* missingPage() const { return missingPage_; }
    ui::FilterBar* filterBar() const { return filterBar_; }
    const domain::BookQuery& query() const { return query_; }
    BatchEnricher* batch() const { return batch_; }
    QAction* fetchAllAction() const { return fetchAll_; }
    QAction* reviewAction() const { return review_; }
    QAction* addByIsbnAction() const { return addByIsbn_; }
    QAction* backUpAction() const { return backUp_; }
    QAction* exportAction() const { return export_; }
    QAction* newCatalogueAction() const { return newCatalogue_; }
    QAction* openCatalogueAction() const { return openCatalogue_; }
    QAction* closeCatalogueAction() const { return closeCatalogue_; }
    QAction* importCsvAction() const { return importCsv_; }
    QAction* importDumpAction() const { return importDump_; }
    QAction* restoreAction() const { return restore_; }
    QAction* quitAction() const { return quit_; }
    // True while a series is chosen and the middle panel lists its entries.
    bool showingSeries() const;
    // True while the shopping list is in the middle panel (F-010).
    bool showingMissing() const;

protected:
    // Not while a form is open: the edit would be lost unsaid.
    void closeEvent(QCloseEvent* event) override;

private:
    void buildMenus();
    // Every action's enabled state, from whether a catalogue is open and
    // whether the panel is busy.
    void updateActions();
    void updateTitle();
    void rebuildRecentMenu();
    QSettings& settings();
    QString chooseFile(const FileRequest& request);
    QString documentsFolder(const QString& sub) const;
    // A checked copy of the open catalogue before something replaces or
    // merges into it; the path, or empty with `problem` set.
    QString safetyBackup(const QString& why, QString& problem);
    void newCatalogueChosen();
    void openCatalogueChosen();
    void importCsvChosen();
    void importDumpChosen();
    void restoreChosen();

    void showSelection(const QList<qint64>& ids);
    // The series page's selection: owned books and volumes not owned.
    void showSeriesSelection(const QList<qint64>& bookIds, int missing);
    // Shows the series in the middle panel and, unless the panel is busy or
    // something is selected, in the detail panel too.
    void showSeriesPage(std::int64_t seriesId);
    void showMissingPage(bool oneVolumeShort);
    // The shopping list's selection: its series in the panel, with the
    // volume's card.
    void showMissingSelection(const std::optional<domain::MissingRow>& volume);
    // Redraws the panel for whatever the middle panel has selected, unless a
    // form is open.
    void refreshPanel();

    // Series entries (Phase 2 step 4).
    void addEntry();
    // Find titles for the series on show (F-030), and the owner's answer.
    void findTitles();
    void useFoundTitles();
    void stopFindingTitles();
    void editEntry(qint64 entryId);
    void saveEntry(const domain::SeriesEntry& entry);
    void askToRemoveEntry(qint64 entryId);
    void beginMarkOwned(qint64 entryId);
    void attachBook(qint64 entryId, qint64 bookId);
    void createForEntry(qint64 entryId);
    // After anything changes a series: the page, the rail, the book list's
    // series labels, and the panel; then selects the book, if given.
    void afterSeriesChange(std::optional<qint64> selectBook = std::nullopt);
    void saveBook(const domain::BookEdit& edit);
    void addBook();
    void askToDelete(const QList<qint64>& ids);
    void deleteBooks(const QList<qint64>& ids);
    void applyFilter(const domain::BookFilter& filter, const QString& label);
    // Narrows the book list to the combined filters (F-017), and shows the
    // read state they hold in the rail.
    void applyQuery();
    void refreshFilterOptions();
    // Lays the list's group headers again (F-018): genres change as books
    // are fetched.
    void applyGrouping();
    // Recounts the rail after anything that changes its numbers.
    void refreshRail();
    void toggleRead(const QList<qint64>& ids);
    void rate(const QList<qint64>& ids, int rating);

    // Re-reads these books into the list, and into the panel when it is
    // showing one of them and not mid-edit.
    void refreshBooks(const QList<qint64>& ids);
    QString titleOf(qint64 id) const;

    // "Fetch metadata" (F-012): look the book up, offer the candidates, write
    // the one chosen, then fetch its cover.
    void fetchMetadata(qint64 bookId);
    void useCandidate(qint64 bookId, int index, bool myEdition);
    // Thumbnails beside the candidates on offer, as they arrive; any still
    // coming for an earlier offer are withdrawn first.
    void previewCovers(qint64 bookId, const std::vector<domain::Candidate>& candidates);
    void fetchCover(qint64 bookId, const std::string& url, domain::Source source);
    void coverArrived(qint64 bookId, domain::Source source, const metadata::CoverResult& cover);

    // Add by ISBN (Phase 3 step 6, F-024, D-012): look up, confirm, create.
    void addByIsbn();
    void lookUpForAdding(const QString& isbn13);
    void showAddCandidate(int index);
    void searchForAdding(const QString& title, const QString& author);
    void addConfirmed(const ui::AddByIsbnView::Choice& choice);
    void addByHand();
    // The cover for a book just added or given its ISBN: the one the card
    // showed for this candidate, else fetched.
    void keepCover(qint64 bookId, const std::optional<std::string>& url, domain::Source source, int candidate);
    void stopAdding();

    // Back up (F-020): the form in the panel, then the checked copy.
    void beginBackup();
    void backUp(const QString& path);

    // Export (F-021 to F-023): the form in the panel, then the checked file.
    void beginExport();
    void exportTo(int format, const QString& path);

    // The batch run (Phase 3 step 5, D-023): Fetch all starts or stops it;
    // Review walks the matches waiting for the owner, one book at a time.
    void toggleBatch();
    void showBatchProgress();
    void beginReview();
    void reviewNext();
    void endReview(const QString& message);

    // IMP-002: while the panel holds an edit or a question, the list and Add
    // stand still, so a stray click cannot throw the edit away.
    void lockWhileBusy();

    Catalogue* catalogue_ = nullptr;
    std::unique_ptr<Catalogue> owned_; // what the window opened itself
    FileChooser chooser_;
    QSettings* settings_ = nullptr;
    std::unique_ptr<QSettings> ownSettings_;
    QMenu* recentMenu_ = nullptr;
    QAction* newCatalogue_ = nullptr;
    QAction* openCatalogue_ = nullptr;
    QAction* closeCatalogue_ = nullptr;
    QAction* importCsv_ = nullptr;
    QAction* importDump_ = nullptr;
    QAction* restore_ = nullptr;
    QAction* quit_ = nullptr;
    QAction* find_ = nullptr;
    QAction* about_ = nullptr;
    QList<QAction*> themeActions_; // System, Light, Dark, in Theme's order
    QString backupFolder_;
    Enricher* enricher_ = nullptr;
    // What the last lookup offered, for the candidate chosen.
    struct Offer {
        qint64 bookId = 0;
        std::vector<domain::Candidate> candidates;
        bool byIsbn = false;
    };
    std::optional<Offer> offer_;
    unsigned offerSerial_ = 0; // which offer a thumbnail arriving belongs to
    BatchEnricher* batch_ = nullptr;
    QAction* fetchAll_ = nullptr;
    QAction* addByIsbn_ = nullptr;
    QAction* backUp_ = nullptr;
    QAction* export_ = nullptr;
    QString lastExportFolder_;
    QString lastBackupFolder_; // this session's, offered again
    // What the ISBN lookup (or the search after it) offered, and the cover
    // shown for one of them, kept to store once the book exists.
    struct AddOffer {
        std::vector<domain::Candidate> candidates;
        bool byIsbn = true;
    };
    std::optional<AddOffer> addOffer_;
    std::optional<std::pair<int, QByteArray>> addCover_;
    QAction* review_ = nullptr;
    QProgressBar* batchBar_ = nullptr;
    // The match on show while reviewing, and how many are left this pass
    // (so Skip cannot go round for ever).
    std::optional<PendingMatch> reviewing_;
    int reviewLeft_ = 0;
    int reviewPlace_ = 0;
    int reviewTotal_ = 0;
    QAction* addBook_ = nullptr;
    // A new book being added for this missing volume (Mark as owned).
    std::optional<qint64> pendingAttach_;
    std::int64_t findingTitlesFor_ = 0; // the series whose titles are being found
    QSplitter* splitter_;
    ui::RailView* rail_;
    QStackedWidget* centre_;
    ui::SeriesPage* seriesPage_;
    ui::MissingPage* missingPage_;
    QWidget* listPage_;
    ui::FilterBar* filterBar_;
    ui::BookListView* list_;
    domain::BookQuery query_;
    ui::DetailPanel* detail_;
};

} // namespace pinax::app
