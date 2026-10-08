#include "db/genre_repository.h"

#include "db/connection.h"
#include "db/statement.h"
#include "domain/genre_filter.h"

#include <sqlite3.h>

namespace pinax::db {

GenreRepository::GenreRepository(Connection& connection)
    : connection_(connection)
{
}

std::int64_t GenreRepository::findOrCreate(const std::string& name)
{
    // Whatever its capitals (IMP-009): the spelling stored first stands.
    Statement select(connection_, "SELECT id FROM genre WHERE name = :name COLLATE NOCASE");
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

std::map<std::int64_t, std::vector<std::string>> GenreRepository::byBook()
{
    Statement select(connection_, R"(
        SELECT bg.book_id, g.name
          FROM book_genre bg
          JOIN genre g ON g.id = bg.genre_id
         ORDER BY bg.book_id, g.name COLLATE NOCASE)");
    std::map<std::int64_t, std::vector<std::string>> result;
    while (select.step())
        result[select.columnInt(0)].push_back(select.columnText(1));
    return result;
}

int GenreRepository::removeServiceSubjects()
{
    std::vector<std::int64_t> service;
    {
        Statement select(connection_, R"(
            SELECT DISTINCT g.id, g.name
              FROM genre g
              JOIN book_genre bg ON bg.genre_id = g.id
             WHERE bg.source <> 'manual')");
        while (select.step()) {
            if (domain::isServiceSubject(select.columnText(1)))
                service.push_back(select.columnInt(0));
        }
    }
    int removed = 0;
    for (const auto id : service) {
        Statement remove(connection_, "DELETE FROM book_genre WHERE genre_id = :id AND source <> 'manual'");
        remove.bind(":id", id);
        remove.step();
        removed += sqlite3_changes(connection_.handle());
    }
    Statement orphans(connection_, "DELETE FROM genre WHERE id NOT IN (SELECT genre_id FROM book_genre)");
    orphans.step();
    return removed;
}

std::vector<domain::FilterOption> GenreRepository::withCounts()
{
    Statement select(connection_, R"(
        SELECT g.id, g.name, COUNT(*)
          FROM genre g
          JOIN book_genre bg ON bg.genre_id = g.id
         GROUP BY g.id
         ORDER BY g.name COLLATE NOCASE)");
    std::vector<domain::FilterOption> result;
    while (select.step())
        result.push_back({select.columnInt(0), select.columnText(1), static_cast<int>(select.columnInt(2))});
    return result;
}

} // namespace pinax::db
