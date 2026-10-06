#pragma once

#include "domain/book_detail.h"
#include "domain/book_edit.h"
#include "domain/series_detail.h"
#include "domain/series_entry.h"
#include "domain/series_row.h"
#include "domain/book_summary.h"
#include "domain/candidate.h"

#include <QList>
#include <QWidget>

#include <functional>
#include <optional>
#include <vector>

class QLabel;
class QPushButton;
class QStackedWidget;

namespace pinax::ui {

class AttachView;
class BookEditor;
class CandidateView;
class BookView;
class EntryEditor;
class SeriesView;

// The right-hand panel: it describes whatever is selected (D-010). Nothing,
// one book in its view state or its edit state (D-011), a new book being
// filled in, several books (the bulk editor arrives later), or a deletion
// waiting to be confirmed. No modal dialogues: confirmation happens here.
class DetailPanel : public QWidget {
    Q_OBJECT

public:
    enum class State { Empty, Viewing, Editing, Several, ConfirmingDelete, ViewingSeries, EditingEntry, Attaching, Fetching };
    Q_ENUM(State)

    // True while the panel holds something the owner must finish — a form, a
    // question, a volume being marked as owned — and the selection must not
    // move (IMP-002).
    bool isBusy() const
    {
        return state_ == State::Editing || state_ == State::ConfirmingDelete
            || state_ == State::EditingEntry || state_ == State::Attaching || state_ == State::Fetching;
    }
    // True while a form is open, or a lookup the owner is waiting on, which
    // nothing may redraw over.
    bool isEditing() const
    {
        return state_ == State::Editing || state_ == State::EditingEntry || state_ == State::Attaching
            || state_ == State::Fetching;
    }

    explicit DetailPanel(QWidget* parent = nullptr);

    void showNothing();
    void showBook(const domain::BookDetail& detail);
    void showSelection(int count);
    void showSeries(const domain::SeriesDetail& detail,
        const std::optional<domain::SeriesRow>& selected = std::nullopt);

    // Switches the book on show into its edit state. Does nothing otherwise.
    void beginEdit();

    // A form for a book not yet in the catalogue (F-001), empty or prefilled.
    void beginNew(const domain::BookDetail& prefill = {});

    // A series entry's form (F-008, F-009); id 0 is a new volume.
    void beginEntryEdit(const domain::SeriesEntry& entry, const QString& seriesName, const QString& ownedBy);
    void showEntryError(const QString& message);

    // "Mark as owned" for a missing volume (AV-007).
    void beginAttach(const domain::SeriesRow& volume, const QString& seriesName,
        const std::vector<domain::BookSummary>& candidates);

    // Asks, in the panel, whether to go ahead; `confirmLabel` names the
    // action ("Delete", "Remove"). Keep is the default and Esc keeps.
    void askToConfirm(const QString& question, const QString& confirmLabel, std::function<void()> onConfirm);

    // Asks, in the panel, whether to delete these books. `question` says what
    // will be lost; Keep is the default and Esc keeps.
    void askToDelete(const QList<qint64>& ids, const QString& question);
    void showSaveError(const QString& message);

    // "Fetch metadata" for the book on show (F-012): the lookup under way,
    // then its candidates to choose from. Cancel returns to the book.
    void beginFetch(const QString& how);
    // The same, for a batch run's find under review (D-023): `place` says
    // where in the queue. Not my book and Skip join Use this.
    void beginReview(const QString& place);
    void offerCandidates(const std::vector<domain::Candidate>& candidates, bool byIsbn);
    void showFetchProblem(const QString& message);
    void setFetchProgress(const QString& text);
    // The book being looked up, while Fetching.
    std::optional<qint64> fetchingBookId() const;

    State state() const { return state_; }
    BookView* view() const { return view_; }
    BookEditor* editor() const { return editor_; }
    SeriesView* seriesView() const { return seriesView_; }
    EntryEditor* entryEditor() const { return entryEditor_; }
    AttachView* attachView() const { return attachView_; }
    CandidateView* candidateView() const { return candidateView_; }

signals:
    void saveRequested(const domain::BookEdit& edit);
    // Delete pressed in the view state, for the book on show.
    void deleteRequested(const QList<qint64>& ids);
    void deleteConfirmed(const QList<qint64>& ids);
    // A question declined, or an entry or attach form cancelled: the panel
    // has nothing to show of its own, and the caller redraws it.
    void dismissed();
    void entrySaveRequested(const domain::SeriesEntry& entry);
    void entryRemoveRequested(qint64 entryId);
    void editEntryRequested(qint64 entryId);
    void markOwnedRequested(qint64 entryId);
    void attachRequested(qint64 entryId, qint64 bookId);
    void createForEntryRequested(qint64 entryId);
    void stateChanged(pinax::ui::DetailPanel::State state);
    // The shown book's rating squares were clicked: 1-10, or 0 to clear.
    void ratingRequested(qint64 bookId, int rating);
    // "Fetch metadata" pressed for the book on show.
    void fetchRequested(qint64 bookId);
    // The owner chose this of the candidates offered.
    void candidateChosen(qint64 bookId, int index);
    // The lookup was cancelled, or its problem acknowledged; the panel is
    // back on the book.
    void fetchCancelled();
    // Reviewing: none of the candidates is this book; or decide later.
    void candidateRejected(qint64 bookId);
    void candidateSkipped(qint64 bookId);

private:
    void setState(State state);

    QStackedWidget* stack_;
    QLabel* empty_;
    BookView* view_;
    BookEditor* editor_;
    QLabel* several_;
    QWidget* confirm_;
    QLabel* question_;
    QPushButton* keep_;
    QPushButton* confirmButton_;
    SeriesView* seriesView_;
    EntryEditor* entryEditor_;
    AttachView* attachView_;
    CandidateView* candidateView_;
    std::function<void()> pendingConfirm_;

    std::optional<domain::BookDetail> shown_;
    State state_ = State::Empty;
};

} // namespace pinax::ui
