#include "db/book_repository.h"

#include "db/connection.h"
#include "db/db_error.h"
#include "db/statement.h"
#include "domain/sort_title.h"

#include <sqlite3.h>

#include <string>

namespace pinax::db {

using domain::Book;

namespace {

// Column order shared by every SELECT here and by readBook().
constexpr std::string_view selectColumns = R"(
    id, title, sort_title, subtitle,
    read_status, times_read, date_started, date_finished, rating,
    synopsis, synopsis_source, cover_path, cover_source,
    isbn13, isbn10, publisher, published_year, page_count, language, binding,
    edition_note, condition_note, acquired_date, acquired_note,
    metadata_status, metadata_fetched_at,
    notes, created_at, updated_at)";

template <typename Enum>
std::optional<std::string_view> optionalName(const std::optional<Enum>& value)
{
    if (!value)
        return std::nullopt;
    return domain::toString(*value);
}

std::optional<std::int64_t> widen(const std::optional<int>& value)
{
    if (!value)
        return std::nullopt;
    return *value;
}

std::optional<int> narrow(const std::optional<std::int64_t>& value)
{
    if (!value)
        return std::nullopt;
    return static_cast<int>(*value);
}

template <typename Enum, typename Parse>
Enum parseRequired(const std::string& text, Parse parse, const char* column)
{
    const std::optional<Enum> value = parse(text);
    if (!value)
        throw DbError(std::string("unrecognised ") + column + " '" + text + "'", SQLITE_MISMATCH);
    return *value;
}

template <typename Enum, typename Parse>
std::optional<Enum> parseOptional(const std::optional<std::string>& text, Parse parse,
    const char* column)
{
    if (!text)
        return std::nullopt;
    return parseRequired<Enum>(*text, parse, column);
}

// Binds every writable column. `id` is bound separately where needed.
void bindBook(Statement& statement, const Book& book)
{
    statement.bind(":title", book.title);
    statement.bind(":sort_title",
        book.sortTitle.empty() ? domain::makeSortTitle(book.title) : book.sortTitle);
    statement.bind(":subtitle", book.subtitle);

    statement.bind(":read_status", domain::toString(book.readStatus));
    statement.bind(":times_read", std::int64_t { book.timesRead });
    statement.bind(":date_started", book.dateStarted);
    statement.bind(":date_finished", book.dateFinished);
    statement.bind(":rating", widen(book.rating));

    statement.bind(":synopsis", book.synopsis);
    statement.bind(":synopsis_source", optionalName(book.synopsisSource));
    statement.bind(":cover_path", book.coverPath);
    statement.bind(":cover_source", optionalName(book.coverSource));

    statement.bind(":isbn13", book.isbn13);
    statement.bind(":isbn10", book.isbn10);
    statement.bind(":publisher", book.publisher);
    statement.bind(":published_year", widen(book.publishedYear));
    statement.bind(":page_count", widen(book.pageCount));
    statement.bind(":language", book.language);
    statement.bind(":binding", optionalName(book.binding));

    statement.bind(":edition_note", book.editionNote);
    statement.bind(":condition_note", book.conditionNote);
    statement.bind(":acquired_date", book.acquiredDate);
    statement.bind(":acquired_note", book.acquiredNote);

    statement.bind(":metadata_status", domain::toString(book.metadataStatus));
    statement.bind(":metadata_fetched_at", book.metadataFetchedAt);

    statement.bind(":notes", book.notes);
}

Book readBook(const Statement& row)
{
    int column = 0;
    Book book;
    book.id = row.columnInt(column++);
    book.title = row.columnText(column++);
    book.sortTitle = row.columnText(column++);
    book.subtitle = row.columnOptionalText(column++);

    book.readStatus = parseRequired<domain::ReadStatus>(
        row.columnText(column++), domain::readStatusFromString, "read_status");
    book.timesRead = static_cast<int>(row.columnInt(column++));
    book.dateStarted = row.columnOptionalText(column++);
    book.dateFinished = row.columnOptionalText(column++);
    book.rating = narrow(row.columnOptionalInt(column++));

    book.synopsis = row.columnOptionalText(column++);
    book.synopsisSource = parseOptional<domain::Source>(
        row.columnOptionalText(column++), domain::sourceFromString, "synopsis_source");
    book.coverPath = row.columnOptionalText(column++);
    book.coverSource = parseOptional<domain::Source>(
        row.columnOptionalText(column++), domain::sourceFromString, "cover_source");

    book.isbn13 = row.columnOptionalText(column++);
    book.isbn10 = row.columnOptionalText(column++);
    book.publisher = row.columnOptionalText(column++);
    book.publishedYear = narrow(row.columnOptionalInt(column++));
    book.pageCount = narrow(row.columnOptionalInt(column++));
    book.language = row.columnText(column++);
    book.binding = parseOptional<domain::Binding>(
        row.columnOptionalText(column++), domain::bindingFromString, "binding");

    book.editionNote = row.columnOptionalText(column++);
    book.conditionNote = row.columnOptionalText(column++);
    book.acquiredDate = row.columnOptionalText(column++);
    book.acquiredNote = row.columnOptionalText(column++);

    book.metadataStatus = parseRequired<domain::MetadataStatus>(
        row.columnText(column++), domain::metadataStatusFromString, "metadata_status");
    book.metadataFetchedAt = row.columnOptionalText(column++);

    book.notes = row.columnOptionalText(column++);
    book.createdAt = row.columnText(column++);
    book.updatedAt = row.columnText(column++);
    return book;
}

} // namespace

BookRepository::BookRepository(Connection& connection)
    : connection_(connection)
{
}

std::int64_t BookRepository::create(const Book& book)
{
    Statement insert(connection_, R"(
        INSERT INTO book (
            title, sort_title, subtitle,
            read_status, times_read, date_started, date_finished, rating,
            synopsis, synopsis_source, cover_path, cover_source,
            isbn13, isbn10, publisher, published_year, page_count, language, binding,
            edition_note, condition_note, acquired_date, acquired_note,
            metadata_status, metadata_fetched_at,
            notes)
        VALUES (
            :title, :sort_title, :subtitle,
            :read_status, :times_read, :date_started, :date_finished, :rating,
            :synopsis, :synopsis_source, :cover_path, :cover_source,
            :isbn13, :isbn10, :publisher, :published_year, :page_count, :language, :binding,
            :edition_note, :condition_note, :acquired_date, :acquired_note,
            :metadata_status, :metadata_fetched_at,
            :notes))");
    bindBook(insert, book);
    insert.step();
    return sqlite3_last_insert_rowid(connection_.handle());
}

std::optional<Book> BookRepository::find(std::int64_t id)
{
    Statement select(connection_,
        "SELECT " + std::string(selectColumns) + " FROM book WHERE id = :id");
    select.bind(":id", id);
    if (!select.step())
        return std::nullopt;
    return readBook(select);
}

bool BookRepository::update(const Book& book)
{
    Statement statement(connection_, R"(
        UPDATE book SET
            title = :title, sort_title = :sort_title, subtitle = :subtitle,
            read_status = :read_status, times_read = :times_read,
            date_started = :date_started, date_finished = :date_finished,
            rating = :rating,
            synopsis = :synopsis, synopsis_source = :synopsis_source,
            cover_path = :cover_path, cover_source = :cover_source,
            isbn13 = :isbn13, isbn10 = :isbn10, publisher = :publisher,
            published_year = :published_year, page_count = :page_count,
            language = :language, binding = :binding,
            edition_note = :edition_note, condition_note = :condition_note,
            acquired_date = :acquired_date, acquired_note = :acquired_note,
            metadata_status = :metadata_status,
            metadata_fetched_at = :metadata_fetched_at,
            notes = :notes
        WHERE id = :id)");
    bindBook(statement, book);
    statement.bind(":id", book.id);
    statement.step();
    // Counts the row this statement changed, not the triggers' own updates.
    return sqlite3_changes(connection_.handle()) == 1;
}

bool BookRepository::remove(std::int64_t id)
{
    Statement statement(connection_, "DELETE FROM book WHERE id = :id");
    statement.bind(":id", id);
    statement.step();
    return sqlite3_changes(connection_.handle()) == 1;
}

std::int64_t BookRepository::count()
{
    Statement statement(connection_, "SELECT COUNT(*) FROM book");
    statement.step();
    return statement.columnInt(0);
}

} // namespace pinax::db
