#include "io/series_importer.h"

#include "db/connection.h"
#include "db/db_error.h"
#include "db/savepoint.h"
#include "db/series_repository.h"
#include "db/transaction.h"
#include "io/csv_reader.h"
#include "io/import_support.h"
#include "io/sort_position.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>

namespace pinax::io {

using detail::optionalText;
using detail::parseNumber;
using detail::RowError;
using detail::trim;

namespace {

// SPEC.md §1.6, in its column order.
constexpr std::array<std::string_view, 6> knownColumns{
    "series", "position", "sort_position", "title", "ongoing", "notes"};

struct Row {
    int line = 0;
    std::string series;
    std::optional<std::string> position;
    std::optional<double> sortPosition;
    std::optional<std::string> title;
    std::optional<bool> ongoing;
    bool hasNotes = false;
    std::optional<std::string> notes;

    bool describesVolume() const { return position || title; }
};

Row parseRow(const CsvRecord& record, const std::vector<std::string_view>& header)
{
    if (record.fields.size() != header.size()) {
        throw RowError { "expected " + std::to_string(header.size()) + " fields, found "
            + std::to_string(record.fields.size()) };
    }
    std::map<std::string_view, std::string> cells;
    for (std::size_t i = 0; i < header.size(); ++i)
        cells[header[i]] = trim(record.fields[i]);
    auto cell = [&](std::string_view column) -> std::string {
        const auto found = cells.find(column);
        return found == cells.end() ? std::string() : found->second;
    };

    Row row;
    row.line = record.line;
    row.series = cell("series");
    if (row.series.empty())
        throw RowError { "series is empty" };
    row.position = optionalText(cell("position"));
    row.title = optionalText(cell("title"));
    row.sortPosition = parseNumber<double>(cell("sort_position"), "sort_position");
    if (!row.sortPosition && row.position)
        row.sortPosition = deriveSortPosition(*row.position);

    const std::string ongoing = cell("ongoing");
    if (ongoing == "yes")
        row.ongoing = true;
    else if (ongoing == "no")
        row.ongoing = false;
    else if (!ongoing.empty())
        throw RowError { "ongoing '" + ongoing + "' is neither yes nor no" };

    row.hasNotes = cells.contains("notes");
    row.notes = optionalText(cell("notes"));

    if (!row.describesVolume() && !row.ongoing)
        throw RowError { "nothing to record: give a position, a title, or ongoing" };
    if (!row.describesVolume() && row.notes)
        throw RowError { "notes belong to a volume; give a position or a title" };
    return row;
}

enum class Outcome { Inserted, Updated, Unchanged };

class RowImporter {
public:
    explicit RowImporter(db::Connection& connection)
        : series_(connection)
    {
    }

    Outcome import(const Row& row)
    {
        // One answer per series per file; a contradiction is the file's.
        if (row.ongoing) {
            const auto [earlier, fresh] = ongoingSeen_.try_emplace(row.series, *row.ongoing, row.line);
            if (!fresh && earlier->second.first != *row.ongoing) {
                throw RowError { "ongoing contradicts line " + std::to_string(earlier->second.second) };
            }
        }

        const std::int64_t seriesId = series_.findOrCreate(row.series);
        bool changed = false;
        bool inserted = false;

        if (row.ongoing && series_.ongoing(seriesId) != row.ongoing) {
            series_.setOngoing(seriesId, *row.ongoing);
            changed = true;
        }

        if (row.describesVolume()) {
            std::optional<domain::SeriesEntry> existing = row.position
                ? series_.entryAt(seriesId, *row.position)
                : series_.unpositionedEntryTitled(seriesId, *row.title);

            if (!existing) {
                domain::SeriesEntry entry;
                entry.seriesId = seriesId;
                entry.position = row.position;
                entry.sortPosition = row.sortPosition;
                entry.title = row.title;
                entry.notes = row.notes;
                series_.addEntry(entry);
                inserted = true;
            } else if (!existing->bookId) {
                // Still missing: bring it into line with the file.
                domain::SeriesEntry entry = *existing;
                if (row.title)
                    entry.title = row.title;
                if (row.sortPosition)
                    entry.sortPosition = row.sortPosition;
                if (row.hasNotes)
                    entry.notes = row.notes;
                if (!(entry == *existing)) {
                    series_.updateEntry(entry);
                    changed = true;
                }
            }
            // Owned already: the shelf has it, so the file's claim that it is
            // missing is out of date. Left alone (AV-007).
        }

        if (inserted)
            return Outcome::Inserted;
        return changed ? Outcome::Updated : Outcome::Unchanged;
    }

private:
    db::SeriesRepository series_;
    std::map<std::string, std::pair<bool, int>> ongoingSeen_; // series -> (value, line)
};

} // namespace

SeriesImporter::SeriesImporter(db::Connection& connection)
    : connection_(connection)
{
}

ImportReport SeriesImporter::importFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        ImportReport report;
        report.aborted = true;
        report.failures.push_back({ 0, "cannot read " + path });
        return report;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return importText(contents.str());
}

ImportReport SeriesImporter::importText(std::string_view csv)
{
    ImportReport report;
    auto abort = [&](int line, std::string message) {
        report = ImportReport {};
        report.aborted = true;
        report.failures.push_back({ line, std::move(message) });
        return report;
    };

    std::vector<CsvRecord> records;
    try {
        records = parseCsv(csv);
    } catch (const CsvError& error) {
        return abort(error.line(), error.what());
    }
    if (records.empty())
        return abort(0, "the file is empty");

    std::vector<std::string_view> header;
    for (const std::string& raw : records.front().fields) {
        const std::string name = trim(raw);
        const auto known = std::find(knownColumns.begin(), knownColumns.end(), name);
        if (known == knownColumns.end())
            return abort(records.front().line, "unknown column '" + name + "'");
        if (std::find(header.begin(), header.end(), *known) != header.end())
            return abort(records.front().line, "column '" + name + "' appears twice");
        header.push_back(*known);
    }
    if (std::find(header.begin(), header.end(), "series") == header.end())
        return abort(records.front().line, "no series column");

    try {
        db::Transaction transaction(connection_);
        RowImporter importer(connection_);
        for (std::size_t i = 1; i < records.size(); ++i) {
            const CsvRecord& record = records[i];
            db::Savepoint savepoint(connection_, "import_row");
            try {
                switch (importer.import(parseRow(record, header))) {
                case Outcome::Inserted: ++report.inserted; break;
                case Outcome::Updated: ++report.updated; break;
                case Outcome::Unchanged: ++report.unchanged; break;
                }
                savepoint.release();
            } catch (const RowError& error) {
                report.failures.push_back({ record.line, error.message });
            } catch (const db::DbError& error) {
                if (!error.isConstraintViolation())
                    throw;
                report.failures.push_back({ record.line, error.what() });
            }
        }
        transaction.commit();
    } catch (const db::DbError& error) {
        return abort(0, std::string("database error, nothing imported: ") + error.what());
    }
    return report;
}

} // namespace pinax::io
