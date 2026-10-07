#include "db/author_repository.h"

#include "db/connection.h"
#include "db/statement.h"
#include "domain/sort_name.h"

#include <sqlite3.h>

namespace pinax::db {

using domain::Author;

namespace {

Author readAuthor(const Statement& row)
{
    Author author;
    author.id = row.columnInt(0);
    author.name = row.columnText(1);
    author.sortName = row.columnText(2);
    return author;
}

} // namespace

AuthorRepository::AuthorRepository(Connection& connection)
    : connection_(connection)
{
}

std::optional<Author> AuthorRepository::find(std::int64_t id)
{
    Statement select(connection_, "SELECT id, name, sort_name FROM author WHERE id = :id");
    select.bind(":id", id);
    if (!select.step())
        return std::nullopt;
    return readAuthor(select);
}

std::optional<Author> AuthorRepository::findByName(const std::string& name)
{
    Statement select(connection_, "SELECT id, name, sort_name FROM author WHERE name = :name");
    select.bind(":name", name);
    if (!select.step())
        return std::nullopt;
    return readAuthor(select);
}

std::int64_t AuthorRepository::findOrCreate(const std::string& name)
{
    if (const auto existing = findByName(name))
        return existing->id;

    Statement insert(connection_,
        "INSERT INTO author (name, sort_name) VALUES (:name, :sort_name)");
    insert.bind(":name", name);
    insert.bind(":sort_name", domain::makeSortName(name));
    insert.step();
    return sqlite3_last_insert_rowid(connection_.handle());
}

int AuthorRepository::removeUncredited()
{
    Statement remove(connection_, R"(
        DELETE FROM author
         WHERE notes IS NULL
           AND NOT EXISTS (SELECT 1 FROM book_author ba WHERE ba.author_id = author.id))");
    remove.step();
    return sqlite3_changes(connection_.handle());
}

std::int64_t AuthorRepository::count()
{
    Statement select(connection_, "SELECT COUNT(*) FROM author");
    select.step();
    return select.columnInt(0);
}

std::vector<std::string> AuthorRepository::names()
{
    Statement select(connection_, "SELECT name FROM author ORDER BY sort_name");
    std::vector<std::string> result;
    while (select.step())
        result.push_back(select.columnText(0));
    return result;
}

std::vector<domain::FilterOption> AuthorRepository::withCounts()
{
    Statement select(connection_, R"(
        SELECT a.id, a.name, COUNT(DISTINCT ba.book_id)
          FROM author a
          JOIN book_author ba ON ba.author_id = a.id AND ba.role = 'author'
         GROUP BY a.id
         ORDER BY a.sort_name COLLATE NOCASE)");
    std::vector<domain::FilterOption> result;
    while (select.step())
        result.push_back({select.columnInt(0), select.columnText(1), static_cast<int>(select.columnInt(2))});
    return result;
}

} // namespace pinax::db
