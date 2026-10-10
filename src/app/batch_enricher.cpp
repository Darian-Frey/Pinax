#include "app/batch_enricher.h"

#include "app/catalogue.h"
#include "app/enricher.h"
#include "domain/enrichment.h"

#include <QPointer>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace pinax::app {

BatchEnricher::BatchEnricher(Catalogue& catalogue, Enricher& enricher, QObject* parent)
    : QObject(parent)
    , catalogue_(catalogue)
    , enricher_(enricher)
{
}

void BatchEnricher::start()
{
    if (running_)
        return;
    todo_.clear();
    for (const auto& summary : catalogue_.summaries()) {
        const bool waiting = std::any_of(pending_.begin(), pending_.end(),
            [&](const PendingMatch& match) { return match.bookId == summary.id; });
        if (!waiting)
            todo_.push_back(summary.id);
    }
    // Only the unmatched count: the rest are skipped as they come (resume).
    const int toReview = progress_.toReview;
    progress_ = {};
    progress_.toReview = toReview;
    for (const auto id : todo_) {
        const auto detail = catalogue_.detail(id);
        if (detail && detail->book.metadataStatus == domain::MetadataStatus::Unmatched)
            ++progress_.total;
    }
    setAside_.clear();
    patient_ = false;
    running_ = true;
    emit progressed();
    QTimer::singleShot(0, this, &BatchEnricher::next);
}

void BatchEnricher::stop()
{
    if (!running_)
        return;
    enricher_.cancel(Enricher::Channel::Batch);
    todo_.clear();
    setAside_.clear();
    finish(tr("Stopped. Fetch all again to carry on where it left off."));
}

void BatchEnricher::finish(const QString& problem)
{
    running_ = false;
    emit progressed();
    emit finished(problem);
}

void BatchEnricher::next()
{
    if (!running_)
        return;
    // The first pass done: the books set aside, asked again in full.
    if (todo_.empty() && !setAside_.empty()) {
        todo_ = std::exchange(setAside_, {});
        patient_ = true;
    }
    // Skip what is no longer unmatched: matched since, failed, manual, gone.
    std::optional<domain::BookDetail> detail;
    while (!todo_.empty() && !detail) {
        const auto id = todo_.front();
        todo_.pop_front();
        auto found = catalogue_.detail(id);
        if (found && found->book.metadataStatus == domain::MetadataStatus::Unmatched)
            detail = std::move(found);
    }
    if (!detail) {
        progress_.deferred = 0;
        finish({});
        return;
    }

    const std::int64_t bookId = detail->book.id;
    const std::string title = detail->book.title;
    if (patient_) // being asked again: this one and those after it
        progress_.deferred = static_cast<int>(todo_.size()) + 1;
    enricher_.find(*detail, [this, bookId, title](FindResult result) {
        if (result.deferred) {
            // Google is waiting out a refusal: on to the next (IMP-011).
            setAside_.push_back(bookId);
            ++progress_.deferred;
            emit progressed();
            QTimer::singleShot(0, this, &BatchEnricher::next);
            return;
        }
        if (result.problem) {
            // Not the book's fault: it stays unmatched for the next run.
            finish(QString::fromStdString(*result.problem));
            return;
        }
        ++progress_.done;
        if (result.candidates.empty()) {
            catalogue_.markLookupFailed(bookId);
            ++progress_.notFound;
            emit bookChanged(bookId);
        } else if (result.byIsbn && result.candidates.size() == 1
            && domain::titlesAgree(title, result.candidates.front().title)) {
            accept(bookId, result.candidates.front());
            return; // accept() carries on
        } else {
            pending_.push_back({bookId, std::move(result.candidates), result.byIsbn});
            ++progress_.toReview;
        }
        emit progressed();
        QTimer::singleShot(0, this, &BatchEnricher::next);
    }, Enricher::Channel::Batch, !patient_);
}

void BatchEnricher::accept(std::int64_t bookId, const domain::Candidate& candidate)
{
    enricher_.complete(candidate, [this, bookId](domain::Candidate filled) {
        const auto written = catalogue_.enrich(bookId, filled, true);
        if (!written.problem) {
            ++progress_.matched;
            emit bookChanged(bookId);
            const auto directory = catalogue_.dataDirectory();
            if (written.coverUrl && directory) {
                enricher_.fetchCover(bookId, *written.coverUrl, *directory,
                    [self = QPointer(this), bookId, source = filled.source](metadata::CoverResult cover) {
                        // Covers outlive a stopped run; not a deleted one.
                        if (!self)
                            return;
                        if (cover.relativePath && !self->catalogue_.setCover(bookId, *cover.relativePath, source))
                            emit self->bookChanged(bookId);
                    });
            }
        }
        emit progressed();
        QTimer::singleShot(0, this, &BatchEnricher::next);
    }, Enricher::Channel::Batch);
}

std::optional<PendingMatch> BatchEnricher::takePending()
{
    if (pending_.empty())
        return std::nullopt;
    PendingMatch match = std::move(pending_.front());
    pending_.pop_front();
    progress_.toReview = static_cast<int>(pending_.size());
    emit progressed();
    return match;
}

void BatchEnricher::putBack(PendingMatch match, bool atFront)
{
    if (atFront)
        pending_.push_front(std::move(match));
    else
        pending_.push_back(std::move(match));
    progress_.toReview = static_cast<int>(pending_.size());
    emit progressed();
}

} // namespace pinax::app
