#include "db/book_repository.h"

#include "db/connection.h"
#include "db/db_error.h"
#include "db/statement.h"
#include "domain/sort_title.h"

#include <sqlite3.h>

#include <string>
#include <utility>

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

// Column order shared by summary() and summaries().
constexpr std::string_view summaryColumns = R"(
    id, title, sort_title,
    authors, author_sort,
    series_label, series_sort, series_sort_position,
    read_status, times_read, date_finished, rating, published_year,
    cover_path, metadata_status)";

domain::BookSummary readSummary(const Statement& select)
{
    int column = 0;
    domain::BookSummary row;
    row.id = select.columnInt(column++);
    row.title = select.columnText(column++);
    row.sortTitle = select.columnText(column++);
    row.authors = select.columnOptionalText(column++);
    row.authorSort = select.columnOptionalText(column++);
    row.seriesLabel = select.columnOptionalText(column++);
    row.seriesSort = select.columnOptionalText(column++);
    row.seriesSortPosition = select.columnOptionalDouble(column++);
    row.readStatus = parseRequired<domain::ReadStatus>(
        select.columnText(column++), domain::readStatusFromString, "read_status");
    row.timesRead = static_cast<int>(select.columnInt(column++));
    row.dateFinished = select.columnOptionalText(column++);
    row.rating = narrow(select.columnOptionalInt(column++));
    row.publishedYear = narrow(select.columnOptionalInt(column++));
    row.coverPath = select.columnOptionalText(column++);
    row.metadataStatus = parseRequired<domain::MetadataStatus>(
        select.columnText(column++), domain::metadataStatusFromString, "metadata_status");
    return row;
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

std::optional<Book> BookRepository::findByIsbn13(const std::string& isbn13)
{
    Statement select(connection_,
        "SELECT " + std::string(selectColumns) + " FROM book WHERE isbn13 = :isbn13");
    select.bind(":isbn13", isbn13);
    if (!select.step())
        return std::nullopt;
    return readBook(select);
}

std::optional<Book> BookRepository::findByIsbn10(const std::string& isbn10)
{
    Statement select(connection_,
        "SELECT " + std::string(selectColumns) + " FROM book WHERE isbn10 = :isbn10");
    select.bind(":isbn10", isbn10);
    if (!select.step())
        return std::nullopt;
    return readBook(select);
}

std::optional<Book> BookRepository::findByTitleAndFirstAuthor(const std::string& title,
    const std::optional<std::string>& firstAuthor)
{
    // The first-billed author is the lowest ordinal among 'author' credits,
    // ties broken by filing name as in v_book_display.
    Statement select(connection_, "SELECT " + std::string(selectColumns) + R"(
          FROM book b
         WHERE lower(b.title) = lower(:title)
           AND (SELECT lower(a.name)
                  FROM book_author ba
                  JOIN author a ON a.id = ba.author_id
                 WHERE ba.book_id = b.id AND ba.role = 'author'
                 ORDER BY ba.ordinal, a.sort_name
                 LIMIT 1) IS lower(:author)
         ORDER BY b.id
         LIMIT 1)");
    select.bind(":title", title);
    select.bind(":author", firstAuthor);
    if (!select.step())
        return std::nullopt;
    return readBook(select);
}

std::vector<domain::Credit> BookRepository::credits(std::int64_t bookId)
{
    Statement select(connection_, R"(
        SELECT author_id, role, ordinal
          FROM book_author
         WHERE book_id = :book_id
         ORDER BY ordinal, role, author_id)");
    select.bind(":book_id", bookId);

    std::vector<domain::Credit> result;
    while (select.step()) {
        domain::Credit credit;
        credit.authorId = select.columnInt(0);
        credit.role = parseRequired<domain::CreditRole>(
            select.columnText(1), domain::creditRoleFromString, "role");
        credit.ordinal = static_cast<int>(select.columnInt(2));
        result.push_back(credit);
    }
    return result;
}

void BookRepository::setCredits(std::int64_t bookId, const std::vector<domain::Credit>& credits)
{
    Statement clear(connection_, "DELETE FROM book_author WHERE book_id = :book_id");
    clear.bind(":book_id", bookId);
    clear.step();

    for (const domain::Credit& credit : credits) {
        Statement insert(connection_, R"(
            INSERT INTO book_author (book_id, author_id, ordinal, role)
            VALUES (:book_id, :author_id, :ordinal, :role))");
        insert.bind(":book_id", bookId);
        insert.bind(":author_id", credit.authorId);
        insert.bind(":ordinal", std::int64_t { credit.ordinal });
        insert.bind(":role", domain::toString(credit.role));
        insert.step();
    }
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

std::optional<domain::BookSummary> BookRepository::summary(std::int64_t id)
{
    Statement select(connection_,
        "SELECT " + std::string(summaryColumns) + " FROM v_book_display WHERE id = :id");
    select.bind(":id", id);
    if (!select.step())
        return std::nullopt;
    return readSummary(select);
}

std::vector<domain::BookSummary> BookRepository::summaries()
{
    Statement select(connection_,
        "SELECT " + std::string(summaryColumns) + " FROM v_book_display");

    std::vector<domain::BookSummary> result;
    while (select.step())
        result.push_back(readSummary(select));
    return result;
}

std::int64_t BookRepository::countWithReadStatus(domain::ReadStatus status)
{
    Statement select(connection_, "SELECT COUNT(*) FROM book WHERE read_status = :status");
    select.bind(":status", domain::toString(status));
    select.step();
    return select.columnInt(0);
}

std::vector<std::int64_t> BookRepository::idsWithReadStatus(domain::ReadStatus status)
{
    Statement select(connection_, "SELECT id FROM book WHERE read_status = :status");
    select.bind(":status", domain::toString(status));
    std::vector<std::int64_t> result;
    while (select.step())
        result.push_back(select.columnInt(0));
    return result;
}

std::vector<std::int64_t> BookRepository::idsMatching(const domain::BookQuery& query)
{
    std::string sql = "SELECT b.id FROM book b WHERE 1 = 1";
    if (query.readStatus)
        sql += " AND b.read_status = :read_status";
    if (query.unratedOnly)
        sql += " AND b.rating IS NULL";
    if (query.ratingFrom)
        sql += " AND b.rating >= :rating_from";
    if (query.ratingTo)
        sql += " AND b.rating <= :rating_to";
    if (query.genreId)
        sql += " AND EXISTS (SELECT 1 FROM book_genre bg WHERE bg.book_id = b.id AND bg.genre_id = :genre_id)";
    if (query.authorId) {
        sql += " AND EXISTS (SELECT 1 FROM book_author ba WHERE ba.book_id = b.id"
               " AND ba.author_id = :author_id AND ba.role = 'author')";
    }
    if (query.seriesId) {
        sql += " AND EXISTS (SELECT 1 FROM series_entry se WHERE se.book_id = b.id"
               " AND se.series_id = :series_id)";
    }

    Statement select(connection_, sql);
    if (query.readStatus)
        select.bind(":read_status", domain::toString(*query.readStatus));
    if (query.ratingFrom)
        select.bind(":rating_from", static_cast<std::int64_t>(*query.ratingFrom));
    if (query.ratingTo)
        select.bind(":rating_to", static_cast<std::int64_t>(*query.ratingTo));
    if (query.genreId)
        select.bind(":genre_id", *query.genreId);
    if (query.authorId)
        select.bind(":author_id", *query.authorId);
    if (query.seriesId)
        select.bind(":series_id", *query.seriesId);
    std::vector<std::int64_t> result;
    while (select.step())
        result.push_back(select.columnInt(0));
    return result;
}

std::int64_t BookRepository::count()
{
    Statement statement(connection_, "SELECT COUNT(*) FROM book");
    statement.step();
    return statement.columnInt(0);
}

} // namespace pinax::db
