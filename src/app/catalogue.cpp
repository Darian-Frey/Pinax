#include "app/catalogue.h"

#include "db/author_repository.h"
#include "db/book_repository.h"
#include "db/db_error.h"
#include "db/genre_repository.h"
#include "db/migrations.h"
#include "db/series_repository.h"
#include "db/transaction.h"
#include "domain/enrichment.h"
#include "domain/isbn.h"
#include "domain/name_match.h"
#include "domain/placeholder.h"
#include "io/sort_position.h"
#include "domain/sort_title.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <map>

namespace pinax::app {

Catalogue::Catalogue(const std::string& path)
    : path_(path)
    , connection_(path)
{
    db::migrate(connection_);
}

std::int64_t Catalogue::count()
{
    return db::BookRepository(connection_).count();
}

std::vector<domain::BookSummary> Catalogue::summaries()
{
    return db::BookRepository(connection_).summaries();
}

std::optional<domain::BookSummary> Catalogue::summary(std::int64_t id)
{
    return db::BookRepository(connection_).summary(id);
}

std::optional<domain::BookDetail> Catalogue::detail(std::int64_t id)
{
    db::BookRepository books(connection_);
    std::optional<domain::Book> book = books.find(id);
    if (!book)
        return std::nullopt;

    domain::BookDetail detail;
    detail.book = std::move(*book);
    if (const auto summary = books.summary(id))
        detail.authors = summary->authors;
    db::AuthorRepository authors(connection_);
    for (const domain::Credit& credit : books.credits(id)) {
        if (const auto author = authors.find(credit.authorId))
            detail.credits.push_back({author->name, credit.role});
    }
    detail.series = db::SeriesRepository(connection_).membershipsForBook(id);
    for (const auto& genre : db::GenreRepository(connection_).forBook(id))
        detail.genres.push_back(genre.name);

    // Covers live beside the database (SPEC.md §4); a missing file shows the
    // placeholder rather than failing.
    if (detail.book.coverPath && path_ != ":memory:") {
        const std::filesystem::path cover
            = std::filesystem::path(path_).parent_path() / *detail.book.coverPath;
        std::error_code error;
        if (std::filesystem::is_regular_file(cover, error))
            detail.coverFile = std::filesystem::absolute(cover, error).string();
    }
    return detail;
}

std::int64_t Catalogue::countWithReadStatus(domain::ReadStatus status)
{
    return db::BookRepository(connection_).countWithReadStatus(status);
}

std::vector<domain::SeriesStatus> Catalogue::seriesStatuses()
{
    // Filed as titles are, so The Culture sits under C (F-016).
    auto statuses = db::SeriesRepository(connection_).statuses();
    auto key = [](const domain::SeriesStatus& series) {
        std::string filed = domain::makeSortTitle(series.name);
        for (char& c : filed)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return filed;
    };
    std::stable_sort(statuses.begin(), statuses.end(),
        [&](const auto& a, const auto& b) { return key(a) < key(b); });
    return statuses;
}

std::vector<domain::SeriesRow> Catalogue::seriesRows(std::int64_t seriesId)
{
    return db::SeriesRepository(connection_).rows(seriesId);
}

std::optional<domain::SeriesDetail> Catalogue::seriesDetail(std::int64_t seriesId)
{
    db::SeriesRepository series(connection_);
    const auto status = series.status(seriesId);
    if (!status)
        return std::nullopt;

    domain::SeriesDetail detail;
    detail.series = *status;
    detail.missing = series.missing(seriesId);
    detail.library = series.libraryTotals();

    // The series' authors are its volumes' authors, most frequent first:
    // "Iain M. Banks"; for a shared world, the two who wrote most of it.
    db::BookRepository books(connection_);
    std::map<std::string, int> counts;
    std::vector<std::string> order;
    for (const std::int64_t id : series.bookIds(seriesId)) {
        const auto summary = books.summary(id);
        if (!summary || !summary->authors)
            continue;
        if (counts[*summary->authors]++ == 0)
            order.push_back(*summary->authors);
    }
    std::stable_sort(order.begin(), order.end(),
        [&](const std::string& a, const std::string& b) { return counts[a] > counts[b]; });
    if (!order.empty()) {
        std::string authors = order.front();
        if (order.size() == 2)
            authors += " and " + order[1];
        else if (order.size() > 2)
            authors += " and others";
        detail.authors = authors;
    }
    return detail;
}

std::vector<domain::MissingRow> Catalogue::missingVolumes(bool oneVolumeShortOnly)
{
    auto rows = db::SeriesRepository(connection_).missingEverywhere();
    if (oneVolumeShortOnly) {
        rows.erase(std::remove_if(rows.begin(), rows.end(),
                       [](const domain::MissingRow& row) { return row.missingInSeries != 1; }),
            rows.end());
    }
    // Within each need, series filed as the rail files them (F-016); the
    // view's order is kept inside a series.
    auto filed = [](const std::string& name) {
        std::string key = domain::makeSortTitle(name);
        for (char& c : key)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return key;
    };
    std::stable_sort(rows.begin(), rows.end(), [&](const auto& a, const auto& b) {
        if (a.missingInSeries != b.missingInSeries)
            return a.missingInSeries < b.missingInSeries;
        return filed(a.seriesName) < filed(b.seriesName);
    });
    return rows;
}

domain::LibrarySeriesTotals Catalogue::libraryTotals()
{
    return db::SeriesRepository(connection_).libraryTotals();
}

std::optional<std::vector<std::int64_t>> Catalogue::bookIds(const domain::BookFilter& filter)
{
    switch (filter.kind) {
    case domain::BookFilter::Kind::ReadState:
        return db::BookRepository(connection_).idsWithReadStatus(filter.readStatus);
    case domain::BookFilter::Kind::Series:
        return db::SeriesRepository(connection_).bookIds(filter.seriesId);
    case domain::BookFilter::Kind::All:
    case domain::BookFilter::Kind::MissingVolumes:
    case domain::BookFilter::Kind::OneVolumeShort:
        break;
    }
    return std::nullopt;
}

std::optional<std::vector<std::int64_t>> Catalogue::matchingIds(const domain::BookQuery& query)
{
    if (query.empty())
        return std::nullopt;
    return db::BookRepository(connection_).idsMatching(query);
}

std::vector<domain::FilterOption> Catalogue::genreOptions()
{
    return db::GenreRepository(connection_).withCounts();
}

std::vector<domain::FilterOption> Catalogue::authorOptions()
{
    return db::AuthorRepository(connection_).withCounts();
}

std::vector<domain::FilterOption> Catalogue::seriesOptions()
{
    // Filed as the rail files them, counted by books held.
    std::vector<domain::FilterOption> options;
    for (const auto& status : seriesStatuses()) {
        if (status.held > 0)
            options.push_back({status.id, status.name, status.held});
    }
    return options;
}

domain::ReadStatus Catalogue::toggleRead(const std::vector<std::int64_t>& ids)
{
    using domain::ReadStatus;

    db::BookRepository books(connection_);
    std::vector<domain::Book> found;
    for (const std::int64_t id : ids) {
        if (auto book = books.find(id))
            found.push_back(std::move(*book));
    }

    const bool allRead = !found.empty()
        && std::all_of(found.begin(), found.end(),
            [](const domain::Book& book) { return book.readStatus == ReadStatus::Read; });
    const ReadStatus target = allRead ? ReadStatus::Unread : ReadStatus::Read;

    db::Transaction transaction(connection_);
    for (domain::Book& book : found) {
        if (book.readStatus == target)
            continue; // already there: no update, so no count
        if (target == ReadStatus::Unread) {
            // D-017: unmarking takes back the read that marking counted.
            book.timesRead = std::max(book.timesRead - 1, 0);
            if (book.timesRead == 0)
                book.dateFinished.reset();
        }
        book.readStatus = target; // into Read, the trigger counts and dates it
        books.update(book);
    }
    transaction.commit();
    return target;
}

void Catalogue::setRating(const std::vector<std::int64_t>& ids, std::optional<int> rating)
{
    db::BookRepository books(connection_);
    db::Transaction transaction(connection_);
    for (const std::int64_t id : ids) {
        auto book = books.find(id);
        if (!book || book->rating == rating)
            continue;
        book->rating = rating;
        books.update(*book);
    }
    transaction.commit();
}

namespace {

std::string describe(const db::DbError& error, const domain::Book& book)
{
    const std::string what = error.what();
    if (error.isConstraintViolation() && what.find("isbn13") != std::string::npos)
        return "Another book already has ISBN-13 " + book.isbn13.value_or("") + ".";
    return "The change was not saved: " + what;
}

} // namespace

Catalogue::SaveResult Catalogue::save(const domain::BookEdit& edit, std::optional<std::int64_t> attachTo)
{
    db::BookRepository books(connection_);
    db::AuthorRepository authors(connection_);
    domain::Book book = edit.book;

    try {
        db::Transaction transaction(connection_);
        if (book.id == 0) {
            // Inserted straight into read, the trigger never fires (AV-005).
            if (book.readStatus == domain::ReadStatus::Read && book.timesRead == 0)
                book.timesRead = 1;
            book.id = books.create(book);
        } else if (!books.update(book)) {
            return {0, "This book is no longer in the catalogue."};
        }

        std::vector<domain::Credit> credits;
        for (std::size_t i = 0; i < edit.credits.size(); ++i) {
            domain::Credit credit;
            credit.authorId = authors.findOrCreate(edit.credits[i].name);
            credit.role = edit.credits[i].role;
            credit.ordinal = static_cast<int>(i);
            credits.push_back(credit);
        }
        if (credits != books.credits(book.id)) {
            books.setCredits(book.id, credits);
            authors.removeUncredited();
        }

        if (attachTo) {
            db::SeriesRepository series(connection_);
            auto entry = series.findEntry(*attachTo);
            if (!entry)
                return {0, "That volume is no longer in the series."};
            if (entry->bookId)
                return {0, "That volume is already owned."};
            entry->bookId = book.id;
            series.updateEntry(*entry);
        }

        transaction.commit();
    } catch (const db::DbError& error) {
        return {0, describe(error, book)};
    }
    return {book.id, std::nullopt};
}

std::optional<domain::SeriesEntry> Catalogue::entry(std::int64_t entryId)
{
    return db::SeriesRepository(connection_).findEntry(entryId);
}

std::optional<std::string> Catalogue::seriesName(std::int64_t seriesId)
{
    return db::SeriesRepository(connection_).name(seriesId);
}

double Catalogue::nextSortPosition(std::int64_t seriesId)
{
    const auto last = db::SeriesRepository(connection_).lastSortPosition(seriesId);
    return last ? std::floor(*last) + 1 : 1;
}

std::optional<std::string> Catalogue::saveEntry(const domain::SeriesEntry& entry)
{
    if (!entry.position && !entry.title)
        return "A volume needs a position or a title.";
    try {
        db::SeriesRepository series(connection_);
        if (entry.id == 0)
            series.addEntry(entry);
        else if (!series.updateEntry(entry))
            return "That volume is no longer in the series.";
    } catch (const db::DbError& error) {
        return std::string("The volume was not saved: ") + error.what();
    }
    return std::nullopt;
}

std::optional<std::string> Catalogue::removeEntry(std::int64_t entryId)
{
    try {
        if (!db::SeriesRepository(connection_).removeEntry(entryId))
            return "That volume is no longer in the series.";
    } catch (const db::DbError& error) {
        return std::string("The volume was not removed: ") + error.what();
    }
    return std::nullopt;
}

std::optional<std::string> Catalogue::attach(std::int64_t entryId, std::int64_t bookId)
{
    try {
        db::SeriesRepository series(connection_);
        auto entry = series.findEntry(entryId);
        if (!entry)
            return "That volume is no longer in the series.";
        if (entry->bookId)
            return "That volume is already owned.";
        if (series.entryForBook(entry->seriesId, bookId))
            return "That book is already in this series.";
        entry->bookId = bookId;
        series.updateEntry(*entry);
    } catch (const db::DbError& error) {
        return std::string("The volume was not marked as owned: ") + error.what();
    }
    return std::nullopt;
}

std::vector<domain::NamedCredit> Catalogue::seriesCredits(std::int64_t seriesId)
{
    db::BookRepository books(connection_);
    std::map<std::string, std::pair<int, std::int64_t>> byAuthors; // authors -> (count, a book)
    for (const std::int64_t id : db::SeriesRepository(connection_).bookIds(seriesId)) {
        const auto summary = books.summary(id);
        if (!summary || !summary->authors)
            continue;
        auto& [count, book] = byAuthors[*summary->authors];
        ++count;
        book = id;
    }
    if (byAuthors.empty())
        return {};
    const auto best = std::max_element(byAuthors.begin(), byAuthors.end(),
        [](const auto& a, const auto& b) { return a.second.first < b.second.first; });
    std::vector<domain::NamedCredit> credits;
    db::AuthorRepository authors(connection_);
    for (const domain::Credit& credit : books.credits(best->second.second)) {
        if (const auto author = authors.find(credit.authorId))
            credits.push_back({author->name, credit.role});
    }
    return credits;
}

namespace {

// ISO 8601, UTC, to the second: 2026-10-06T21:04:00Z.
std::string nowIso()
{
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc {};
    gmtime_r(&now, &utc);
    char text[32];
    std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%SZ", &utc);
    return text;
}

} // namespace

Catalogue::EnrichResult Catalogue::enrich(std::int64_t bookId, const domain::Candidate& candidate,
    bool fromIsbnLookup)
{
    try {
        db::BookRepository books(connection_);
        const auto book = books.find(bookId);
        if (!book)
            return {std::nullopt, "This book is no longer in the catalogue."};
        const auto plan = domain::planEnrichment(*book, candidate, fromIsbnLookup, nowIso());

        db::Transaction transaction(connection_);
        books.update(plan.book);
        db::GenreRepository genres(connection_);
        for (const auto& genre : plan.genres)
            genres.addToBook(bookId, genre.name, genre.source);
        transaction.commit();
        return {plan.coverUrl, std::nullopt};
    } catch (const db::DbError& error) {
        return {std::nullopt, std::string("The details were not saved: ") + error.what()};
    }
}

void Catalogue::markLookupFailed(std::int64_t bookId)
{
    db::BookRepository books(connection_);
    if (auto book = books.find(bookId); book && book->metadataStatus != domain::MetadataStatus::Manual) {
        book->metadataStatus = domain::MetadataStatus::Failed;
        book->metadataFetchedAt = nowIso();
        books.update(*book);
    }
}

std::optional<domain::BookSummary> Catalogue::bookWithIsbn(const std::string& isbn13)
{
    db::BookRepository books(connection_);
    auto book = books.findByIsbn13(isbn13);
    if (!book) {
        if (const auto isbn10 = domain::isbn13To10(isbn13))
            book = books.findByIsbn10(*isbn10);
    }
    if (!book)
        return std::nullopt;
    return books.summary(book->id);
}

std::vector<domain::NamedCredit> Catalogue::creditsFor(const std::vector<std::string>& providerAuthors)
{
    const auto known = db::AuthorRepository(connection_).names();
    std::vector<domain::NamedCredit> credits;
    for (const auto& name : providerAuthors)
        credits.push_back({domain::knownAuthor(name, known).value_or(name), domain::CreditRole::Author});
    return credits;
}

std::vector<domain::SeriesProposal> Catalogue::seriesProposals(const std::string& title,
    const std::vector<domain::NamedCredit>& credits, const domain::Candidate& candidate)
{
    std::vector<std::string> authors;
    for (const auto& credit : credits)
        authors.push_back(credit.name);

    db::SeriesRepository repository(connection_);
    const auto statuses = repository.statuses();
    auto statusOf = [&](std::int64_t seriesId) -> const domain::SeriesStatus* {
        for (const auto& status : statuses) {
            if (status.id == seriesId)
                return &status;
        }
        return nullptr;
    };
    auto seriesAuthors = [&](std::int64_t seriesId) {
        std::vector<std::string> names;
        for (const auto& credit : seriesCredits(seriesId))
            names.push_back(credit.name);
        return names;
    };
    const bool statementNamesSeries = candidate.seriesName.has_value();
    auto statementIs = [&](const std::string& seriesName) {
        return statementNamesSeries && domain::titlesAgree(seriesName, *candidate.seriesName);
    };

    std::vector<domain::SeriesProposal> proposals;
    for (const auto& missing : repository.missingEverywhere()) {
        if (domain::isPlaceholderTitle(missing.title))
            continue;
        const bool byTitle = missing.title && domain::titlesAgree(*missing.title, title);
        // An untitled gap, filled by the provider's own series and number.
        const bool byPlace = !missing.title && missing.position && candidate.seriesNumber
            && statementIs(missing.seriesName) && *missing.position == *candidate.seriesNumber;
        if (!byTitle && !byPlace)
            continue;
        if (!domain::shareAnAuthor(authors, seriesAuthors(missing.seriesId)))
            continue;
        domain::SeriesProposal proposal;
        proposal.kind = domain::SeriesProposal::Kind::FillsMissing;
        proposal.seriesId = missing.seriesId;
        proposal.seriesName = missing.seriesName;
        proposal.entryId = missing.entryId;
        proposal.position = missing.position;
        proposal.sortPosition = missing.sortPosition;
        proposal.entryTitle = missing.title;
        if (const auto* status = statusOf(missing.seriesId)) {
            proposal.heldAfter = status->held + 1;
            proposal.knownAfter = status->known;
        }
        proposals.push_back(proposal);
    }

    if (statementNamesSeries) {
        for (const auto& status : statuses) {
            if (!statementIs(status.name))
                continue;
            const bool alreadyProposed = std::any_of(proposals.begin(), proposals.end(),
                [&](const domain::SeriesProposal& p) { return p.seriesId == status.id; });
            if (alreadyProposed || !domain::shareAnAuthor(authors, seriesAuthors(status.id)))
                continue;
            // Never a second place for a title the series already has.
            const auto rows = repository.rows(status.id);
            const bool present = std::any_of(rows.begin(), rows.end(), [&](const domain::SeriesRow& row) {
                const auto named = row.title();
                return named && domain::titlesAgree(*named, title);
            });
            if (present)
                continue;
            domain::SeriesProposal proposal;
            proposal.kind = domain::SeriesProposal::Kind::JoinsSeries;
            proposal.seriesId = status.id;
            proposal.seriesName = status.name;
            proposal.position = candidate.seriesNumber;
            // The importer's rule is the only reader of a position (AV-006).
            proposal.sortPosition = candidate.seriesNumber ? io::deriveSortPosition(*candidate.seriesNumber)
                                                           : std::nullopt;
            if (!proposal.sortPosition)
                proposal.sortPosition = nextSortPosition(status.id);
            proposal.heldAfter = status.held + 1;
            proposal.knownAfter = status.known + 1;
            proposals.push_back(proposal);
        }
    }
    return proposals;
}

std::vector<domain::BookSummary> Catalogue::booksLike(const std::string& title,
    const std::vector<domain::NamedCredit>& credits)
{
    std::vector<std::string> wanted;
    for (const auto& credit : credits)
        wanted.push_back(credit.name);
    db::BookRepository books(connection_);
    std::vector<domain::BookSummary> like;
    for (auto& summary : books.summaries()) {
        if (!domain::titlesAgree(summary.title, title))
            continue;
        // The list's author text: "A & B", or editors marked "(ed.)".
        std::vector<std::string> theirs;
        if (summary.authors) {
            std::string names = *summary.authors;
            for (std::size_t at; (at = names.find(" & ")) != std::string::npos; names.erase(0, at + 3))
                theirs.push_back(names.substr(0, at));
            theirs.push_back(names);
            for (auto& name : theirs)
                name = name.substr(0, name.find(" ("));
        }
        if (!wanted.empty() && !domain::shareAnAuthor(wanted, theirs))
            continue;
        const auto book = books.find(summary.id);
        if (book && !book->isbn13 && !book->isbn10)
            like.push_back(std::move(summary));
    }
    return like;
}

Catalogue::EnrichResult Catalogue::giveIsbn(std::int64_t bookId, const std::string& isbn13,
    const std::optional<std::string>& isbn10, const domain::Candidate& candidate, bool byIsbn)
{
    try {
        db::BookRepository books(connection_);
        auto book = books.find(bookId);
        if (!book)
            return {std::nullopt, "This book is no longer in the catalogue."};
        if (book->isbn13 && *book->isbn13 != isbn13)
            return {std::nullopt, "This book already has another ISBN, " + *book->isbn13 + "."};
        if (const auto held = bookWithIsbn(isbn13); held && held->id != bookId)
            return {std::nullopt, "This ISBN is already in the catalogue: “" + held->title + "”."};
        book->isbn13 = isbn13;
        if (!book->isbn10)
            book->isbn10 = isbn10;
        books.update(*book);
    } catch (const db::DbError& error) {
        return {std::nullopt, std::string("The ISBN was not saved: ") + error.what()};
    }
    return enrich(bookId, candidate, byIsbn);
}

Catalogue::AddResult Catalogue::addBook(const NewBook& book)
{
    if (book.edit.book.id != 0)
        return {0, std::nullopt, "This book is already in the catalogue."};
    if (book.edit.book.isbn13) {
        if (const auto held = bookWithIsbn(*book.edit.book.isbn13))
            return {0, std::nullopt, "This ISBN is already in the catalogue: “" + held->title + "”."};
    }
    std::optional<std::int64_t> attachTo;
    if (book.series && book.series->kind == domain::SeriesProposal::Kind::FillsMissing)
        attachTo = book.series->entryId;

    const auto saved = save(book.edit, attachTo);
    if (saved.problem)
        return {0, std::nullopt, saved.problem};

    const auto enriched = enrich(saved.id, book.candidate, book.byIsbn);
    if (enriched.problem)
        return {saved.id, std::nullopt, "Added, but its details were not: " + *enriched.problem};

    if (book.series && book.series->kind == domain::SeriesProposal::Kind::JoinsSeries) {
        domain::SeriesEntry entry;
        entry.seriesId = book.series->seriesId;
        entry.bookId = saved.id;
        entry.position = book.series->position;
        entry.sortPosition = book.series->sortPosition;
        entry.title = book.edit.book.title;
        if (const auto problem = saveEntry(entry))
            return {saved.id, enriched.coverUrl, "Added, but not to " + book.series->seriesName + ": " + *problem};
    }
    return {saved.id, enriched.coverUrl, std::nullopt};
}

std::optional<std::string> Catalogue::dataDirectory() const
{
    if (path_ == ":memory:" || path_.empty())
        return std::nullopt;
    return std::filesystem::absolute(std::filesystem::path(path_)).parent_path().string();
}

std::optional<std::string> Catalogue::setCover(std::int64_t bookId, const std::string& relativePath,
    domain::Source source)
{
    try {
        db::BookRepository books(connection_);
        auto book = books.find(bookId);
        if (!book)
            return "This book is no longer in the catalogue.";
        if (book->coverSource == domain::Source::Manual && source != domain::Source::Manual)
            return "The cover was set by hand and is kept.";
        if (book->coverPath == relativePath && book->coverSource == source)
            return std::nullopt;
        book->coverPath = relativePath;
        book->coverSource = source;
        books.update(*book);
    } catch (const db::DbError& error) {
        return std::string("The cover was not recorded: ") + error.what();
    }
    return std::nullopt;
}

std::optional<std::string> Catalogue::remove(const std::vector<std::int64_t>& ids)
{
    std::vector<std::string> covers;
    try {
        db::BookRepository books(connection_);
        db::Transaction transaction(connection_);
        for (const std::int64_t id : ids) {
            if (const auto book = books.find(id); book && book->coverPath)
                covers.push_back(*book->coverPath);
            books.remove(id);
        }
        db::AuthorRepository(connection_).removeUncredited();
        transaction.commit();
    } catch (const db::DbError& error) {
        return std::string("Nothing was deleted: ") + error.what();
    }
    // Only once the rows are gone: a cover outliving its book would be handed
    // to the next book given the same id.
    if (const auto directory = dataDirectory()) {
        for (const auto& cover : covers) {
            std::error_code ignored;
            std::filesystem::remove(std::filesystem::path(*directory) / cover, ignored);
        }
    }
    return std::nullopt;
}

std::optional<std::string> Catalogue::save(const domain::Book& book)
{
    try {
        if (!db::BookRepository(connection_).update(book))
            return "This book is no longer in the catalogue.";
    } catch (const db::DbError& error) {
        return describe(error, book);
    }
    return std::nullopt;
}

} // namespace pinax::app
