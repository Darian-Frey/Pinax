#include "app/catalogue.h"

#include "db/book_repository.h"
#include "db/db_error.h"
#include "db/migrations.h"
#include "db/series_repository.h"
#include "db/transaction.h"

#include <algorithm>
#include <filesystem>

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

std::optional<std::string> Catalogue::save(const domain::Book& book)
{
    try {
        if (!db::BookRepository(connection_).update(book))
            return "This book is no longer in the catalogue.";
    } catch (const db::DbError& error) {
        const std::string what = error.what();
        if (error.isConstraintViolation() && what.find("isbn13") != std::string::npos)
            return "Another book already has ISBN-13 " + book.isbn13.value_or("") + ".";
        return "The change was not saved: " + what;
    }
    return std::nullopt;
}

} // namespace pinax::app
