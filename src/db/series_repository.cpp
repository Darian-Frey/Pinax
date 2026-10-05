#include "db/series_repository.h"

#include "db/connection.h"
#include "db/statement.h"

#include <sqlite3.h>

#include <utility>

namespace pinax::db {

using domain::SeriesEntry;

namespace {

constexpr std::string_view entryColumns =
    "id, series_id, book_id, position, sort_position, title, notes";

SeriesEntry readEntry(const Statement& row)
{
    SeriesEntry entry;
    entry.id = row.columnInt(0);
    entry.seriesId = row.columnInt(1);
    entry.bookId = row.columnOptionalInt(2);
    entry.position = row.columnOptionalText(3);
    entry.sortPosition = row.columnOptionalDouble(4);
    entry.title = row.columnOptionalText(5);
    entry.notes = row.columnOptionalText(6);
    return entry;
}

void bindEntry(Statement& statement, const SeriesEntry& entry)
{
    statement.bind(":series_id", entry.seriesId);
    statement.bind(":book_id", entry.bookId);
    statement.bind(":position", entry.position);
    statement.bind(":sort_position", entry.sortPosition);
    statement.bind(":title", entry.title);
    statement.bind(":notes", entry.notes);
}

} // namespace

SeriesRepository::SeriesRepository(Connection& connection)
    : connection_(connection)
{
}

std::int64_t SeriesRepository::findOrCreate(const std::string& name)
{
    Statement select(connection_, "SELECT id FROM series WHERE name = :name");
    select.bind(":name", name);
    if (select.step())
        return select.columnInt(0);

    Statement insert(connection_, "INSERT INTO series (name) VALUES (:name)");
    insert.bind(":name", name);
    insert.step();
    return sqlite3_last_insert_rowid(connection_.handle());
}

std::optional<SeriesEntry> SeriesRepository::entryForBook(std::int64_t seriesId,
    std::int64_t bookId)
{
    Statement select(connection_, "SELECT " + std::string(entryColumns)
            + " FROM series_entry WHERE series_id = :series_id AND book_id = :book_id");
    select.bind(":series_id", seriesId);
    select.bind(":book_id", bookId);
    if (!select.step())
        return std::nullopt;
    return readEntry(select);
}

std::optional<SeriesEntry> SeriesRepository::unownedEntryAt(std::int64_t seriesId,
    const std::string& position)
{
    Statement select(connection_, "SELECT " + std::string(entryColumns)
            + " FROM series_entry"
              " WHERE series_id = :series_id AND book_id IS NULL AND position = :position"
              " ORDER BY id LIMIT 1");
    select.bind(":series_id", seriesId);
    select.bind(":position", position);
    if (!select.step())
        return std::nullopt;
    return readEntry(select);
}

std::vector<domain::SeriesMembership> SeriesRepository::membershipsForBook(std::int64_t bookId)
{
    Statement select(connection_, R"(
        SELECT s.id, s.name, s.ongoing, se.position, se.sort_position,
               vs.held, vs.known, vs.held_read, vs.status
          FROM series_entry se
          JOIN series s ON s.id = se.series_id
          JOIN v_series_status vs ON vs.id = s.id
         WHERE se.book_id = :book_id
         ORDER BY s.name, se.id)");
    select.bind(":book_id", bookId);

    std::vector<domain::SeriesMembership> result;
    while (select.step()) {
        domain::SeriesMembership membership;
        membership.seriesId = select.columnInt(0);
        membership.name = select.columnText(1);
        membership.ongoing = select.columnInt(2) != 0;
        membership.position = select.columnOptionalText(3);
        membership.sortPosition = select.columnOptionalDouble(4);
        membership.held = static_cast<int>(select.columnInt(5));
        membership.known = static_cast<int>(select.columnInt(6));
        membership.heldRead = static_cast<int>(select.columnInt(7));
        membership.status = select.columnText(8);
        result.push_back(std::move(membership));
    }

    for (domain::SeriesMembership& membership : result) {
        Statement missing(connection_, R"(
            SELECT position, title
              FROM v_missing_entries
             WHERE series_id = :series_id
             ORDER BY sort_position IS NULL, sort_position, position)");
        missing.bind(":series_id", membership.seriesId);
        while (missing.step())
            membership.missing.push_back({missing.columnOptionalText(0), missing.columnOptionalText(1)});
    }
    return result;
}

std::int64_t SeriesRepository::addEntry(const SeriesEntry& entry)
{
    Statement insert(connection_, R"(
        INSERT INTO series_entry (series_id, book_id, position, sort_position, title, notes)
        VALUES (:series_id, :book_id, :position, :sort_position, :title, :notes))");
    bindEntry(insert, entry);
    insert.step();
    return sqlite3_last_insert_rowid(connection_.handle());
}

bool SeriesRepository::updateEntry(const SeriesEntry& entry)
{
    Statement update(connection_, R"(
        UPDATE series_entry SET
            series_id = :series_id, book_id = :book_id,
            position = :position, sort_position = :sort_position,
            title = :title, notes = :notes
        WHERE id = :id)");
    bindEntry(update, entry);
    update.bind(":id", entry.id);
    update.step();
    return sqlite3_changes(connection_.handle()) == 1;
}

std::int64_t SeriesRepository::count()
{
    Statement select(connection_, "SELECT COUNT(*) FROM series");
    select.step();
    return select.columnInt(0);
}

std::int64_t SeriesRepository::entryCount()
{
    Statement select(connection_, "SELECT COUNT(*) FROM series_entry");
    select.step();
    return select.columnInt(0);
}

} // namespace pinax::db
