#pragma once

#include "app/batch_enricher.h"
#include "metadata/cover_cache.h"
#include "domain/book_edit.h"
#include "ui/add_by_isbn_view.h"
#include "domain/candidate.h"
#include "domain/book_filter.h"
#include "domain/missing_row.h"
#include "domain/series_entry.h"

#include <QList>
#include <QMainWindow>

#include <optional>
#include <vector>

class QAction;
class QProgressBar;
class QSplitter;
class QStackedWidget;
class QWidget;

namespace pinax::ui {
class BookListView;
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

    // Shows this catalogue's books. The catalogue must outlive the window.
    void setCatalogue(Catalogue* catalogue);
    // Looks books up for "Fetch metadata". Without one, Fetch says so. The
    // enricher must outlive the window.
    void setEnricher(Enricher* enricher);

    QSplitter* splitter() const { return splitter_; }
    ui::BookListView* bookList() const { return list_; }
    ui::DetailPanel* detailPanel() const { return detail_; }
    ui::RailView* rail() const { return rail_; }
    ui::SeriesPage* seriesPage() const { return seriesPage_; }
    ui::MissingPage* missingPage() const { return missingPage_; }
    BatchEnricher* batch() const { return batch_; }
    QAction* fetchAllAction() const { return fetchAll_; }
    QAction* reviewAction() const { return review_; }
    QAction* addByIsbnAction() const { return addByIsbn_; }
    // True while a series is chosen and the middle panel lists its entries.
    bool showingSeries() const;
    // True while the shopping list is in the middle panel (F-010).
    bool showingMissing() const;

private:
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
    void useCandidate(qint64 bookId, int index);
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
    QSplitter* splitter_;
    ui::RailView* rail_;
    QStackedWidget* centre_;
    ui::SeriesPage* seriesPage_;
    ui::MissingPage* missingPage_;
    ui::BookListView* list_;
    ui::DetailPanel* detail_;
};

} // namespace pinax::app
