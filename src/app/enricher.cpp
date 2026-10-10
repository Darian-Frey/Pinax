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
    , britishLibraryQueue_(fetcher, policy)
    , coverQueue_(fetcher, policy)
    , wikidataQueue_(fetcher, policy)
    , openLibrary_(openLibraryQueue_)
    , google_(googleQueue_, googleKey)
    , britishLibrary_(britishLibraryQueue_)
    , wikidata_(wikidataQueue_)
{
    connect(&wikidataQueue_, &metadata::RequestQueue::paused, this,
        [this](int seconds) { emit waiting(QStringLiteral("Wikidata"), seconds); });
    connect(&britishLibraryQueue_, &metadata::RequestQueue::paused, this,
        [this](int seconds) { emit waiting(QStringLiteral("The British Library"), seconds); });
    connect(&openLibraryQueue_, &metadata::RequestQueue::paused, this,
        [this](int seconds) { emit waiting(QStringLiteral("Open Library"), seconds); });
    connect(&googleQueue_, &metadata::RequestQueue::paused, this,
        [this](int seconds) { emit waiting(QStringLiteral("Google Books"), seconds); });
    connect(&coverQueue_, &metadata::RequestQueue::paused, this,
        [this](int seconds) { emit waiting(QStringLiteral("the cover server"), seconds); });
}

Enricher::~Enricher() = default;

template <typename T>
std::function<void(T)> Enricher::guarded(Channel channel, std::function<void(T)> done)
{
    const auto index = static_cast<std::size_t>(channel);
    return [this, index, generation = generations_[index], done = std::move(done)](T value) {
        if (generation == generations_[index])
            done(std::move(value));
    };
}

void Enricher::cancel(Channel channel)
{
    ++generations_[static_cast<std::size_t>(channel)];
}

void Enricher::find(const BookDetail& detail, std::function<void(FindResult)> done, Channel channel)
{
    done = guarded(channel, std::move(done));
    if (const auto isbn = isbn13Of(detail.book))
        findByIsbn(*isbn, detail, true, std::move(done));
    else
        findByTitle(detail, {}, std::move(done));
}

void Enricher::findSeries(const std::string& name, const std::vector<std::string>& credits,
    std::function<void(domain::SeriesFind)> done, Channel channel)
{
    done = guarded(channel, std::move(done));
    wikidata_.findSeries(name, credits, [this, name, credits, done](domain::SeriesFind wikidata) {
        if (!wikidata.volumes.empty()) {
            done(std::move(wikidata));
            return;
        }
        // Not a series Wikidata knows: titles that carry its name.
        const auto author = credits.empty() ? std::nullopt : std::optional(credits.front());
        openLibrary_.searchSeries(name, author, [wikidata, done](domain::SeriesFind openLibrary) {
            // Nothing found: say which provider could not be asked, if any.
            if (openLibrary.volumes.empty() && wikidata.error)
                openLibrary.error = openLibrary.error ? *wikidata.error + "; " + *openLibrary.error : wikidata.error;
            done(std::move(openLibrary));
        });
    });
}

void Enricher::lookupIsbn(const std::string& isbn13, std::function<void(FindResult)> done, Channel channel)
{
    findByIsbn(isbn13, {}, false, guarded(channel, std::move(done)));
}

namespace {
constexpr int previewTag = 1; // cover-queue requests cancelPreviews may withdraw
}

void Enricher::cancelPreviews()
{
    coverQueue_.cancelTagged(previewTag);
    cancel(Channel::Previews);
}

void Enricher::fetchImage(const std::string& url,
    std::function<void(std::optional<QByteArray> bytes, std::optional<std::string> error)> done, bool thumbnail)
{
    // No data directory: the image is only downloaded and checked.
    auto cache = std::make_shared<metadata::CoverCache>(coverQueue_, QString());
    auto guardedDone = guarded<std::pair<std::optional<QByteArray>, std::optional<std::string>>>(Channel::Previews,
        [done](std::pair<std::optional<QByteArray>, std::optional<std::string>> result) {
            done(std::move(result.first), std::move(result.second));
        });
    QUrl address(QString::fromStdString(url));
    if (thumbnail)
        address = metadata::CoverCache::thumbnailUrl(address);
    cache->download(address,
        [cache, guardedDone](std::optional<QByteArray> bytes, std::optional<std::string> error) {
            guardedDone({std::move(bytes), std::move(error)});
        },
        previewTag);
}

void Enricher::findByIsbn(const std::string& isbn13, const BookDetail& detail, bool thenByTitle,
    std::function<void(FindResult)> done)
{
    // Both at once, each on its own queue; combined when both have answered.
    struct Answers {
        std::optional<LookupResult> openLibrary;
        std::optional<LookupResult> britishLibrary;
    };
    auto answers = std::make_shared<Answers>();
    auto combine = [this, isbn13, detail, thenByTitle, done, answers] {
        if (!answers->openLibrary || !answers->britishLibrary)
            return;
        auto candidates = fillGaps(std::move(answers->openLibrary->candidates), answers->britishLibrary->candidates);
        if (!candidates.empty()) {
            done({std::move(candidates), true, std::nullopt});
            return;
        }
        Asked asked;
        asked.note(*answers->openLibrary);
        asked.note(*answers->britishLibrary);
        askGoogleByIsbn(isbn13, detail, thenByTitle, std::move(asked), done);
    };
    openLibrary_.lookupIsbn(isbn13, [answers, combine](LookupResult result) {
        answers->openLibrary = std::move(result);
        combine();
    });
    britishLibrary_.lookupIsbn(isbn13, [answers, combine](LookupResult result) {
        answers->britishLibrary = std::move(result);
        combine();
    });
}

std::vector<Candidate> Enricher::fillGaps(std::vector<Candidate> primary, const std::vector<Candidate>& secondary)
{
    if (primary.empty())
        return secondary;
    if (secondary.empty())
        return primary;
    // The same ISBN, so the same edition: the first record stands for it.
    const Candidate& filler = secondary.front();
    for (Candidate& candidate : primary) {
        bool filled = false;
        auto fill = [&filled](auto& field, const auto& value) {
            if (!field && value) {
                field = value;
                filled = true;
            }
        };
        fill(candidate.publisher, filler.publisher);
        fill(candidate.pageCount, filler.pageCount);
        fill(candidate.publishedYear, filler.publishedYear);
        fill(candidate.firstPublishedYear, filler.firstPublishedYear);
        fill(candidate.subtitle, filler.subtitle);
        fill(candidate.seriesName, filler.seriesName);
        fill(candidate.seriesNumber, filler.seriesNumber);
        if (!filler.categories.empty())
            filled = true;
        if (filled) {
            candidate.filledFrom = filler.source;
            candidate.filledCategories = filler.categories;
        }
    }
    return primary;
}

void Enricher::askGoogleByIsbn(const std::string& isbn13, const BookDetail& detail, bool thenByTitle,
    Asked asked, std::function<void(FindResult)> done)
{
    auto nothing = [this, detail, thenByTitle, done](Asked asked) {
        if (thenByTitle)
            findByTitle(detail, std::move(asked), done);
        else
            done({{}, true, asked.problem()});
    };
    if (!google_.available()) {
        nothing(std::move(asked));
        return;
    }
    google_.lookupIsbn(isbn13, [done, asked, nothing](LookupResult google) mutable {
        if (!google.candidates.empty()) {
            done({std::move(google.candidates), true, std::nullopt});
            return;
        }
        asked.note(google);
        nothing(std::move(asked));
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

void Enricher::complete(const Candidate& candidate, std::function<void(Candidate)> done, Channel channel,
    bool ownersEdition)
{
    done = guarded(channel, std::move(done));
    // Second: the owner's edition, behind the cover they saw (D-029).
    auto withEdition = [this, ownersEdition, done](Candidate filled) {
        if (!ownersEdition || !filled.editionFactsTypical) {
            done(std::move(filled));
            return;
        }
        if (!filled.editionKey) {
            done(std::move(filled)); // typical figures stay unwritten
            return;
        }
        const std::string key = *filled.editionKey;
        openLibrary_.edition(key, [filled, done](std::optional<metadata::openlibrary::EditionRecord> record,
                                      std::optional<std::string>) mutable {
            if (record) {
                filled.publisher = record->publisher;
                filled.pageCount = record->pageCount;
                filled.publishedYear = record->publishedYear;
                filled.editionFactsTypical = false;
            }
            done(std::move(filled));
        });
    };
    // First: the work's synopsis, which a search result lacks.
    if (candidate.description || !candidate.workKey || candidate.source != domain::Source::OpenLibrary) {
        withEdition(candidate);
        return;
    }
    openLibrary_.description(*candidate.workKey,
        [candidate, withEdition](std::optional<std::string> description, std::optional<std::string>) {
            Candidate filled = candidate;
            filled.description = description;
            withEdition(std::move(filled));
        });
}

metadata::CoverResult Enricher::storeCover(std::int64_t bookId, const QByteArray& bytes,
    const std::string& dataDirectory)
{
    return metadata::CoverCache(coverQueue_, QString::fromStdString(dataDirectory)).store(bookId, bytes);
}

void Enricher::fetchCover(std::int64_t bookId, const std::string& url, const std::string& dataDirectory,
    std::function<void(metadata::CoverResult)> done)
{
    // The cache is a view onto the directory; it holds nothing between
    // fetches.
    auto cache = std::make_shared<metadata::CoverCache>(coverQueue_, QString::fromStdString(dataDirectory));
    cache->fetch(bookId, QUrl(QString::fromStdString(url)),
        [cache, done = guarded(Channel::Covers, std::move(done))](metadata::CoverResult result) { done(std::move(result)); });
}

} // namespace pinax::app
