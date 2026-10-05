#include "app/catalogue.h"

#include "db/author_repository.h"
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

Catalogue::SaveResult Catalogue::save(const domain::BookEdit& edit)
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
        if (credits != books.credits(book.id))
            books.setCredits(book.id, credits);

        transaction.commit();
    } catch (const db::DbError& error) {
        return {0, describe(error, book)};
    }
    return {book.id, std::nullopt};
}

std::optional<std::string> Catalogue::remove(const std::vector<std::int64_t>& ids)
{
    try {
        db::BookRepository books(connection_);
        db::Transaction transaction(connection_);
        for (const std::int64_t id : ids)
            books.remove(id);
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
