#include "app/enricher.h"

#include "domain/isbn.h"

namespace pinax::app {

using domain::BookDetail;
using domain::Candidate;
using domain::LookupResult;

namespace {

std::optional<std::string> isbn13Of(const domain::Book& book)
{
    if (book.isbn13)
        return book.isbn13;
    if (book.isbn10 && domain::isValidIsbn10(*book.isbn10))
        return domain::isbn10To13(*book.isbn10);
    return std::nullopt;
}

std::optional<std::string> firstAuthor(const BookDetail& detail)
{
    for (const auto& credit : detail.credits) {
        if (credit.role == domain::CreditRole::Author)
            return credit.name;
    }
    return std::nullopt;
}

} // namespace

Enricher::Enricher(metadata::Fetcher& fetcher, const QString& googleKey, metadata::QueuePolicy policy,
    QObject* parent)
    : QObject(parent)
    , openLibraryQueue_(fetcher, policy)
    , googleQueue_(fetcher, policy)
    , coverQueue_(fetcher, policy)
    , openLibrary_(openLibraryQueue_)
    , google_(googleQueue_, googleKey)
{
    connect(&openLibraryQueue_, &metadata::RequestQueue::paused, this,
        [this](int seconds) { emit waiting(QStringLiteral("Open Library"), seconds); });
    connect(&googleQueue_, &metadata::RequestQueue::paused, this,
        [this](int seconds) { emit waiting(QStringLiteral("Google Books"), seconds); });
    connect(&coverQueue_, &metadata::RequestQueue::paused, this,
        [this](int seconds) { emit waiting(QStringLiteral("the cover server"), seconds); });
}

Enricher::~Enricher() = default;

template <typename T>
std::function<void(T)> Enricher::guarded(std::function<void(T)> done)
{
    return [this, generation = generation_, done = std::move(done)](T value) {
        if (generation == generation_)
            done(std::move(value));
    };
}

void Enricher::cancel()
{
    ++generation_;
    openLibraryQueue_.cancelAll();
    googleQueue_.cancelAll();
    coverQueue_.cancelAll();
}

void Enricher::find(const BookDetail& detail, std::function<void(FindResult)> done)
{
    done = guarded(std::move(done));
    if (const auto isbn = isbn13Of(detail.book))
        findByIsbn(*isbn, detail, std::move(done));
    else
        findByTitle(detail, {}, std::move(done));
}

void Enricher::findByIsbn(const std::string& isbn13, const BookDetail& detail,
    std::function<void(FindResult)> done)
{
    openLibrary_.lookupIsbn(isbn13, [this, isbn13, detail, done](LookupResult openLibrary) {
        if (!openLibrary.candidates.empty()) {
            done({std::move(openLibrary.candidates), true, std::nullopt});
            return;
        }
        Asked asked;
        asked.note(openLibrary);
        if (!google_.available()) {
            findByTitle(detail, std::move(asked), done);
            return;
        }
        google_.lookupIsbn(isbn13, [this, detail, done, asked](LookupResult google) mutable {
            if (!google.candidates.empty()) {
                done({std::move(google.candidates), true, std::nullopt});
                return;
            }
            asked.note(google);
            findByTitle(detail, std::move(asked), done);
        });
    });
}

void Enricher::findByTitle(const BookDetail& detail, Asked asked, std::function<void(FindResult)> done)
{
    const std::string title = detail.book.title;
    const auto author = firstAuthor(detail);
    openLibrary_.search(title, author, [this, title, author, asked, done](LookupResult openLibrary) mutable {
        if (!openLibrary.candidates.empty()) {
            done({std::move(openLibrary.candidates), false, std::nullopt});
            return;
        }
        asked.note(openLibrary);
        if (!google_.available()) {
            done({{}, false, asked.problem()});
            return;
        }
        google_.search(title, author, [asked, done](LookupResult google) mutable {
            if (!google.candidates.empty()) {
                done({std::move(google.candidates), false, std::nullopt});
                return;
            }
            asked.note(google);
            done({{}, false, asked.problem()});
        });
    });
}

void Enricher::complete(const Candidate& candidate, std::function<void(Candidate)> done)
{
    done = guarded(std::move(done));
    if (candidate.description || !candidate.workKey || candidate.source != domain::Source::OpenLibrary) {
        done(candidate);
        return;
    }
    openLibrary_.description(*candidate.workKey,
        [candidate, done](std::optional<std::string> description, std::optional<std::string>) {
            Candidate filled = candidate;
            filled.description = description;
            done(filled);
        });
}

void Enricher::fetchCover(std::int64_t bookId, const std::string& url, const std::string& dataDirectory,
    std::function<void(metadata::CoverResult)> done)
{
    // The cache is a view onto the directory; it holds nothing between
    // fetches.
    auto cache = std::make_shared<metadata::CoverCache>(coverQueue_, QString::fromStdString(dataDirectory));
    cache->fetch(bookId, QUrl(QString::fromStdString(url)),
        [cache, done = guarded(std::move(done))](metadata::CoverResult result) { done(std::move(result)); });
}

} // namespace pinax::app
