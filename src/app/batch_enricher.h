#pragma once

#include "domain/candidate.h"

#include <QObject>
#include <QString>

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace pinax::app {

class Catalogue;
class Enricher;

// Candidates found for a book in a batch run, waiting for the owner.
struct PendingMatch {
    std::int64_t bookId = 0;
    std::vector<domain::Candidate> candidates;
    bool byIsbn = false;
};

struct BatchProgress {
    int total = 0;    // books to look up in this run
    int done = 0;     // looked up so far
    int matched = 0;  // accepted without asking
    int toReview = 0; // found, waiting for the owner (all runs)
    int notFound = 0; // no provider knew them
};

// Fetch metadata across the catalogue (Phase 3 step 5, F-012, D-023): every
// book whose metadata is still unmatched, one at a time, through the same
// polite queues as a single fetch (AV-009). An ISBN's single answer whose
// title agrees with the book's is written at once (SPEC.md §3.4); any other
// answer waits in the review queue, never written unasked (AV-010); a book
// no provider knows is marked failed.
//
// Resumable by construction: a run takes the books still unmatched, so
// those matched, failed or manual are skipped, and an interrupted run loses
// nothing. If the providers cannot be reached the run stops and the book in
// hand stays unmatched. The review queue lives in memory; books left in it
// at exit are still unmatched and are looked up again by the next run.
class BatchEnricher : public QObject {
    Q_OBJECT

public:
    BatchEnricher(Catalogue& catalogue, Enricher& enricher, QObject* parent = nullptr);

    // Starts a run over the unmatched books not already waiting for review.
    // Does nothing if one is running.
    void start();
    // Stops after nothing more: the book in hand stays unmatched.
    void stop();
    bool running() const { return running_; }

    const BatchProgress& progress() const { return progress_; }
    int pendingCount() const { return static_cast<int>(pending_.size()); }

    // The next match waiting for the owner, removed from the queue.
    std::optional<PendingMatch> takePending();
    // Puts a match back: at the front (review stopped) or the back (skipped).
    void putBack(PendingMatch match, bool atFront);

signals:
    void progressed();
    // A book's row changed: matched, its cover arrived, or not found.
    void bookChanged(qint64 bookId);
    // The run ended: `problem` is empty when it reached the last book.
    void finished(const QString& problem);

private:
    void next();
    void finish(const QString& problem);
    void accept(std::int64_t bookId, const domain::Candidate& candidate);

    Catalogue& catalogue_;
    Enricher& enricher_;
    std::deque<std::int64_t> todo_;
    std::deque<PendingMatch> pending_;
    BatchProgress progress_;
    bool running_ = false;
};

} // namespace pinax::app
