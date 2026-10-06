#pragma once

#include "domain/book_detail.h"
#include "domain/candidate.h"
#include "metadata/cover_cache.h"
#include "metadata/google_books.h"
#include "metadata/open_library.h"
#include "metadata/request_queue.h"

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
// By ISBN first, Open Library then Google; if neither knows the ISBN, by
// title and first author the same way. Google is asked only with a key
// (D-019). One request queue per host keeps each provider at its own pace
// (AV-009).
class Enricher : public QObject {
    Q_OBJECT

public:
    Enricher(metadata::Fetcher& fetcher, const QString& googleKey, metadata::QueuePolicy policy = {},
        QObject* parent = nullptr);
    ~Enricher() override;

    bool googleAvailable() const { return google_.available(); }

    void find(const domain::BookDetail& detail, std::function<void(FindResult)> done);

    // A candidate found by search carries no synopsis; its work may. Hands
    // the candidate back, filled where it could be.
    void complete(const domain::Candidate& candidate, std::function<void(domain::Candidate)> done);

    // The cover, into `dataDirectory`/covers (SPEC.md §4).
    void fetchCover(std::int64_t bookId, const std::string& url, const std::string& dataDirectory,
        std::function<void(metadata::CoverResult)> done);

    // Forgets every request not yet answered; their callbacks never run.
    void cancel();

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

    void findByIsbn(const std::string& isbn13, const domain::BookDetail& detail,
        std::function<void(FindResult)> done);
    void findByTitle(const domain::BookDetail& detail, Asked asked, std::function<void(FindResult)> done);
    // Wraps a callback so it is dropped if cancel() runs first.
    template <typename T>
    std::function<void(T)> guarded(std::function<void(T)> done);

    metadata::RequestQueue openLibraryQueue_;
    metadata::RequestQueue googleQueue_;
    metadata::RequestQueue coverQueue_;
    metadata::OpenLibraryClient openLibrary_;
    metadata::GoogleBooksClient google_;
    unsigned generation_ = 0;
};

} // namespace pinax::app
