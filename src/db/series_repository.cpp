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

std::optional<SeriesEntry> SeriesRepository::entryAt(std::int64_t seriesId,
    const std::string& position)
{
    Statement select(connection_, "SELECT " + std::string(entryColumns)
            + " FROM series_entry WHERE series_id = :series_id AND position = :position"
              " ORDER BY book_id IS NULL, id LIMIT 1");
    select.bind(":series_id", seriesId);
    select.bind(":position", position);
    if (!select.step())
        return std::nullopt;
    return readEntry(select);
}

std::optional<SeriesEntry> SeriesRepository::unpositionedEntryTitled(std::int64_t seriesId,
    const std::string& title)
{
    Statement select(connection_, "SELECT " + std::string(entryColumns)
            + " FROM series_entry"
              " WHERE series_id = :series_id AND position IS NULL AND lower(title) = lower(:title)"
              " ORDER BY id LIMIT 1");
    select.bind(":series_id", seriesId);
    select.bind(":title", title);
    if (!select.step())
        return std::nullopt;
    return readEntry(select);
}

std::optional<bool> SeriesRepository::ongoing(std::int64_t seriesId)
{
    Statement select(connection_, "SELECT ongoing FROM series WHERE id = :id");
    select.bind(":id", seriesId);
    if (!select.step())
        return std::nullopt;
    return select.columnInt(0) != 0;
}

void SeriesRepository::setOngoing(std::int64_t seriesId, bool ongoing)
{
    Statement update(connection_, "UPDATE series SET ongoing = :ongoing WHERE id = :id");
    update.bind(":ongoing", std::int64_t { ongoing ? 1 : 0 });
    update.bind(":id", seriesId);
    update.step();
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

    for (domain::SeriesMembership& membership : result)
        membership.missing = missing(membership.seriesId);
    return result;
}

std::vector<domain::SeriesRow> SeriesRepository::rows(std::int64_t seriesId)
{
    Statement select(connection_, R"(
        SELECT se.id, se.position, se.sort_position, se.title, se.book_id,
               b.title, b.read_status, b.times_read, b.rating, b.published_year
          FROM series_entry se
          LEFT JOIN book b ON b.id = se.book_id
         WHERE se.series_id = :series_id
         ORDER BY se.sort_position IS NULL, se.sort_position, se.position, se.id)");
    select.bind(":series_id", seriesId);

    std::vector<domain::SeriesRow> result;
    while (select.step()) {
        domain::SeriesRow row;
        row.entryId = select.columnInt(0);
        row.position = select.columnOptionalText(1);
        row.sortPosition = select.columnOptionalDouble(2);
        row.entryTitle = select.columnOptionalText(3);
        row.bookId = select.columnOptionalInt(4);
        if (row.bookId) {
            row.bookTitle = select.columnOptionalText(5);
            row.readStatus = domain::readStatusFromString(select.columnText(6))
                                 .value_or(domain::ReadStatus::Unread);
            row.timesRead = static_cast<int>(select.columnInt(7));
            if (const auto rating = select.columnOptionalInt(8))
                row.rating = static_cast<int>(*rating);
            if (const auto year = select.columnOptionalInt(9))
                row.publishedYear = static_cast<int>(*year);
        }
        result.push_back(std::move(row));
    }
    return result;
}

std::optional<domain::SeriesStatus> SeriesRepository::status(std::int64_t seriesId)
{
    Statement select(connection_, R"(
        SELECT id, name, ongoing, held, known, held_read, status
          FROM v_series_status WHERE id = :id)");
    select.bind(":id", seriesId);
    if (!select.step())
        return std::nullopt;
    domain::SeriesStatus series;
    series.id = select.columnInt(0);
    series.name = select.columnText(1);
    series.ongoing = select.columnInt(2) != 0;
    series.held = static_cast<int>(select.columnInt(3));
    series.known = static_cast<int>(select.columnInt(4));
    series.heldRead = static_cast<int>(select.columnInt(5));
    series.status = select.columnText(6);
    return series;
}

std::vector<domain::MissingVolume> SeriesRepository::missing(std::int64_t seriesId)
{
    Statement select(connection_, R"(
        SELECT position, title
          FROM v_missing_entries
         WHERE series_id = :series_id
         ORDER BY sort_position IS NULL, sort_position, position)");
    select.bind(":series_id", seriesId);
    std::vector<domain::MissingVolume> result;
    while (select.step())
        result.push_back({select.columnOptionalText(0), select.columnOptionalText(1)});
    return result;
}

std::vector<domain::MissingRow> SeriesRepository::missingEverywhere()
{
    Statement select(connection_, R"(
        SELECT entry_id, series_id, series_name, position, sort_position, title, missing_in_series
          FROM v_missing_entries
         ORDER BY missing_in_series, series_name COLLATE NOCASE, series_id,
                  sort_position IS NULL, sort_position, position, entry_id)");
    std::vector<domain::MissingRow> result;
    while (select.step()) {
        domain::MissingRow row;
        row.entryId = select.columnInt(0);
        row.seriesId = select.columnInt(1);
        row.seriesName = select.columnText(2);
        row.position = select.columnOptionalText(3);
        row.sortPosition = select.columnOptionalDouble(4);
        row.title = select.columnOptionalText(5);
        row.missingInSeries = static_cast<int>(select.columnInt(6));
        result.push_back(std::move(row));
    }
    return result;
}

domain::LibrarySeriesTotals SeriesRepository::libraryTotals()
{
    Statement select(connection_, R"(
        SELECT SUM(status = 'Incomplete' AND known - held = 1),
               SUM(status IN ('Complete', 'Complete to date')),
               SUM(status = 'Incomplete'),
               (SELECT COUNT(*) FROM series_entry WHERE book_id IS NULL)
          FROM v_series_status)");
    select.step();
    domain::LibrarySeriesTotals totals;
    totals.oneVolumeShort = static_cast<int>(select.columnInt(0));
    totals.complete = static_cast<int>(select.columnInt(1));
    totals.withGaps = static_cast<int>(select.columnInt(2));
    totals.volumesNotOwned = static_cast<int>(select.columnInt(3));
    return totals;
}

std::vector<domain::SeriesStatus> SeriesRepository::statuses()
{
    Statement select(connection_, R"(
        SELECT id, name, ongoing, held, known, held_read, status
          FROM v_series_status
         ORDER BY name COLLATE NOCASE, id)");
    std::vector<domain::SeriesStatus> result;
    while (select.step()) {
        domain::SeriesStatus series;
        series.id = select.columnInt(0);
        series.name = select.columnText(1);
        series.ongoing = select.columnInt(2) != 0;
        series.held = static_cast<int>(select.columnInt(3));
        series.known = static_cast<int>(select.columnInt(4));
        series.heldRead = static_cast<int>(select.columnInt(5));
        series.status = select.columnText(6);
        result.push_back(std::move(series));
    }
    return result;
}

std::vector<std::int64_t> SeriesRepository::bookIds(std::int64_t seriesId)
{
    Statement select(connection_,
        "SELECT book_id FROM series_entry WHERE series_id = :series_id AND book_id IS NOT NULL");
    select.bind(":series_id", seriesId);
    std::vector<std::int64_t> result;
    while (select.step())
        result.push_back(select.columnInt(0));
    return result;
}

std::optional<SeriesEntry> SeriesRepository::findEntry(std::int64_t entryId)
{
    Statement select(connection_,
        "SELECT " + std::string(entryColumns) + " FROM series_entry WHERE id = :id");
    select.bind(":id", entryId);
    if (!select.step())
        return std::nullopt;
    return readEntry(select);
}

std::optional<std::string> SeriesRepository::name(std::int64_t seriesId)
{
    Statement select(connection_, "SELECT name FROM series WHERE id = :id");
    select.bind(":id", seriesId);
    if (!select.step())
        return std::nullopt;
    return select.columnText(0);
}

std::optional<double> SeriesRepository::lastSortPosition(std::int64_t seriesId)
{
    Statement select(connection_,
        "SELECT MAX(sort_position) FROM series_entry WHERE series_id = :series_id");
    select.bind(":series_id", seriesId);
    select.step();
    return select.columnOptionalDouble(0);
}

bool SeriesRepository::removeEntry(std::int64_t entryId)
{
    Statement remove(connection_, "DELETE FROM series_entry WHERE id = :id");
    remove.bind(":id", entryId);
    remove.step();
    return sqlite3_changes(connection_.handle()) == 1;
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
