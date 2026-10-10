#pragma once

#include "domain/book_detail.h"
#include "domain/candidate.h"
#include "metadata/british_library.h"
#include "metadata/cover_cache.h"
#include "metadata/google_books.h"
#include "metadata/open_library.h"
#include "metadata/request_queue.h"
#include "metadata/wikidata.h"

#include <QObject>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pinax::app {

// What the providers said about one book.
struct FindResult {
    std::vector<domain::Candidate> candidates;
    // True when they answer the book's ISBN, and so describe its edition;
    // false for a search by title and author, which may describe another
    // (AV-010).
    bool byIsbn = false;
    // Every provider asked failed; nothing is known either way.
    std::optional<std::string> problem;
};

// Asks the providers about a book for "Fetch metadata" (F-012, SPEC.md
// §3.4). Writes nothing: the owner chooses a candidate and the Catalogue
// writes it.
//
// By ISBN first: Open Library and the British Library together, the
// British Library filling the gaps in Open Library's answer or answering
// alone (D-022); then Google. If none knows the ISBN, by title and first
// author, Open Library then Google. Google is asked only with a key (D-019).
// One request queue per host keeps each provider at its own pace (AV-009).
class Enricher : public QObject {
    Q_OBJECT

public:
    // Who is asking, so one can be cancelled without the other: a fetch in
    // the panel, the batch run, and covers, which are never cancelled once
    // their book is written; and cover previews shown before a choice, which
    // are withdrawn from the queue as soon as they are not wanted. All share
    // the same polite queues.
    enum class Channel { Interactive, Batch, Covers, Previews };

    Enricher(metadata::Fetcher& fetcher, const QString& googleKey, metadata::QueuePolicy policy = {},
        QObject* parent = nullptr);
    ~Enricher() override;

    bool googleAvailable() const { return google_.available(); }

    void find(const domain::BookDetail& detail, std::function<void(FindResult)> done,
        Channel channel = Channel::Interactive);

    // The ISBN alone, for adding a book by it (F-024): the same providers as
    // find's first step, but no title search after — the owner chooses that.
    void lookupIsbn(const std::string& isbn13, std::function<void(FindResult)> done,
        Channel channel = Channel::Interactive);

    // A cover image, downloaded and checked but not kept, to show before a
    // choice: the card's cover in Add by ISBN (F-024), or with `thumbnail`
    // a smaller one beside each candidate. On the Previews channel.
    void fetchImage(const std::string& url,
        std::function<void(std::optional<QByteArray> bytes, std::optional<std::string> error)> done,
        bool thumbnail = false);

    // Withdraws the previews not yet downloaded and drops those under way:
    // the choice they were for has been made or abandoned.
    void cancelPreviews();

    // A candidate found by search carries no synopsis; its work may. Hands
    // the candidate back, filled where it could be.
    // With `ownersEdition`, a searched candidate's typical figures are
    // replaced by the edition behind its cover — its publisher, pages and
    // year — or dropped if there is none to fetch (D-029).
    void complete(const domain::Candidate& candidate, std::function<void(domain::Candidate)> done,
        Channel channel = Channel::Interactive, bool ownersEdition = false);

    // The volumes of a series, for naming the ones the owner has not
    // (F-030, D-031): Wikidata, and Open Library when Wikidata knows no such
    // series. `credits` are the series' authors. Writes nothing.
    void findSeries(const std::string& name, const std::vector<std::string>& credits,
        std::function<void(domain::SeriesFind)> done, Channel channel = Channel::Interactive);

    // The cover, into `dataDirectory`/covers (SPEC.md §4).
    void fetchCover(std::int64_t bookId, const std::string& url, const std::string& dataDirectory,
        std::function<void(metadata::CoverResult)> done);

    // Keeps image bytes already downloaded as a book's cover (SPEC.md §4).
    metadata::CoverResult storeCover(std::int64_t bookId, const QByteArray& bytes, const std::string& dataDirectory);

    // Forgets the channel's answers still to come; their callbacks never run.
    // Requests already queued are still sent — at most a few — and their
    // answers dropped.
    void cancel(Channel channel = Channel::Interactive);

signals:
    // A provider asked us to wait (AV-009); requests resume in about
    // `seconds`.
    void waiting(const QString& provider, int seconds);

private:
    // What the providers asked so far said. A provider that answered "none"
    // has answered; only if none answered at all is there a problem to report.
    struct Asked {
        std::optional<std::string> firstProblem;
        bool answered = false;

        void note(const domain::LookupResult& result)
        {
            if (!result.error)
                answered = true;
            else if (!firstProblem)
                firstProblem = result.error;
        }
        std::optional<std::string> problem() const { return answered ? std::nullopt : firstProblem; }
    };

    void findByIsbn(const std::string& isbn13, const domain::BookDetail& detail, bool thenByTitle,
        std::function<void(FindResult)> done);
    void askGoogleByIsbn(const std::string& isbn13, const domain::BookDetail& detail, bool thenByTitle,
        Asked asked, std::function<void(FindResult)> done);
    void findByTitle(const domain::BookDetail& detail, Asked asked, std::function<void(FindResult)> done);

public:
    // Fills each primary candidate's empty publisher, pages, years and
    // subtitle from the first secondary one, which answered the same ISBN,
    // and carries its categories (D-022). With no primary candidates, the
    // secondary ones answer alone.
    static std::vector<domain::Candidate> fillGaps(std::vector<domain::Candidate> primary,
        const std::vector<domain::Candidate>& secondary);
    // Wraps a callback so it is dropped if its channel is cancelled first.
    template <typename T>
    std::function<void(T)> guarded(Channel channel, std::function<void(T)> done);

    metadata::RequestQueue openLibraryQueue_;
    metadata::RequestQueue googleQueue_;
    metadata::RequestQueue britishLibraryQueue_;
    metadata::RequestQueue coverQueue_;
    metadata::RequestQueue wikidataQueue_;
    metadata::OpenLibraryClient openLibrary_;
    metadata::GoogleBooksClient google_;
    metadata::BritishLibraryClient britishLibrary_;
    metadata::WikidataClient wikidata_;
    unsigned generations_[4] = {0, 0, 0, 0}; // by Channel
};

} // namespace pinax::app
