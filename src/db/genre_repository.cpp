#include "db/genre_repository.h"

#include "db/connection.h"
#include "db/statement.h"

#include <sqlite3.h>

namespace pinax::db {

GenreRepository::GenreRepository(Connection& connection)
    : connection_(connection)
{
}

std::int64_t GenreRepository::findOrCreate(const std::string& name)
{
    Statement select(connection_, "SELECT id FROM genre WHERE name = :name");
    select.bind(":name", name);
    if (select.step())
        return select.columnInt(0);
    Statement insert(connection_, "INSERT INTO genre (name) VALUES (:name)");
    insert.bind(":name", name);
    insert.step();
    return sqlite3_last_insert_rowid(connection_.handle());
}

void GenreRepository::addToBook(std::int64_t bookId, const std::string& name, domain::Source source)
{
    const std::int64_t genreId = findOrCreate(name);
    Statement insert(connection_, R"(
        INSERT OR IGNORE INTO book_genre (book_id, genre_id, source)
        VALUES (:book_id, :genre_id, :source))");
    insert.bind(":book_id", bookId);
    insert.bind(":genre_id", genreId);
    insert.bind(":source", domain::toString(source));
    insert.step();
}

std::vector<GenreLink> GenreRepository::forBook(std::int64_t bookId)
{
    Statement select(connection_, R"(
        SELECT g.name, bg.source
          FROM book_genre bg
          JOIN genre g ON g.id = bg.genre_id
         WHERE bg.book_id = :book_id
         ORDER BY g.name COLLATE NOCASE)");
    select.bind(":book_id", bookId);
    std::vector<GenreLink> result;
    while (select.step()) {
        result.push_back({select.columnText(0),
            domain::sourceFromString(select.columnText(1)).value_or(domain::Source::Manual)});
    }
    return result;
}

} // namespace pinax::db
