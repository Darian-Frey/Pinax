#include "app/catalogue.h"

#include "db/author_repository.h"
#include "db/book_repository.h"
#include "db/db_error.h"
#include "db/migrations.h"
#include "db/series_repository.h"
#include "db/transaction.h"
#include "domain/sort_title.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
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

std::optional<std::vector<std::int64_t>> Catalogue::bookIds(const domain::BookFilter& filter)
{
    switch (filter.kind) {
    case domain::BookFilter::Kind::ReadState:
        return db::BookRepository(connection_).idsWithReadStatus(filter.readStatus);
    case domain::BookFilter::Kind::Series:
        return db::SeriesRepository(connection_).bookIds(filter.seriesId);
    case domain::BookFilter::Kind::All:
        break;
    }
    return std::nullopt;
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

std::optional<std::string> Catalogue::remove(const std::vector<std::int64_t>& ids)
{
    try {
        db::BookRepository books(connection_);
        db::Transaction transaction(connection_);
        for (const std::int64_t id : ids)
            books.remove(id);
        db::AuthorRepository(connection_).removeUncredited();
        transaction.commit();
    } catch (const db::DbError& error) {
        return std::string("Nothing was deleted: ") + error.what();
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
