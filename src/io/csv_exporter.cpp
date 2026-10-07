#include "io/csv_exporter.h"

#include "db/author_repository.h"
#include "db/book_repository.h"
#include "db/connection.h"
#include "db/db_error.h"
#include "db/migrations.h"
#include "db/series_repository.h"
#include "domain/credit_text.h"
#include "io/csv_importer.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace pinax::io {

namespace {

namespace fs = std::filesystem;

// One field, quoted only where it must be (RFC 4180).
std::string field(const std::string& text)
{
    if (text.find_first_of(",\"\r\n") == std::string::npos
        && (text.empty() || (text.front() != ' ' && text.back() != ' ')))
        return text;
    std::string out = "\"";
    for (const char c : text)
        out += c == '"' ? std::string("\"\"") : std::string(1, c);
    return out + "\"";
}

std::string field(const std::optional<std::string>& text)
{
    return text ? field(*text) : std::string();
}

std::string field(const std::optional<int>& number)
{
    return number ? std::to_string(*number) : std::string();
}

// The fewest digits that give the same double back.
std::string field(const std::optional<double>& number)
{
    if (!number)
        return {};
    char buffer[40];
    for (int digits = 15; digits <= 17; ++digits) {
        std::snprintf(buffer, sizeof buffer, "%.*g", digits, *number);
        if (std::strtod(buffer, nullptr) == *number)
            break;
    }
    return buffer;
}

const char* const header = "title,subtitle,authors,series,position,sort_position,shelf,times_read,rating,"
                           "isbn13,publisher,published_year,binding,edition_note,condition_note,notes";

} // namespace

void writeBooksCsv(db::Connection& connection, const std::vector<std::int64_t>& bookIds, std::ostream& out)
{
    db::BookRepository books(connection);
    db::AuthorRepository authors(connection);
    db::SeriesRepository series(connection);
    out << header << '\n';
    for (const std::int64_t id : bookIds) {
        const auto book = books.find(id);
        if (!book)
            continue;
        std::vector<domain::NamedCredit> credits;
        for (const auto& credit : books.credits(id)) {
            if (const auto author = authors.find(credit.authorId))
                credits.push_back({author->name, credit.role});
        }
        const std::string common = field(book->title) + ',' + field(book->subtitle) + ','
            + field(domain::formatCredits(credits));
        const std::string rest = std::string(domain::toString(book->readStatus)) + ','
            + std::to_string(book->timesRead) + ',' + field(book->rating) + ',' + field(book->isbn13) + ','
            + field(book->publisher) + ',' + field(book->publishedYear) + ','
            + (book->binding ? std::string(domain::toString(*book->binding)) : std::string()) + ','
            + field(book->editionNote) + ',' + field(book->conditionNote) + ',' + field(book->notes);

        const auto memberships = series.membershipsForBook(id);
        if (memberships.empty()) {
            out << common << ",,,," << rest << '\n';
            continue;
        }
        for (const auto& membership : memberships) {
            out << common << ',' << field(membership.name) << ',' << field(membership.position) << ','
                << field(membership.sortPosition) << ',' << rest << '\n';
        }
    }
}

std::optional<std::string> exportBooksCsv(db::Connection& connection, const std::vector<std::int64_t>& bookIds,
    const std::string& path, CsvExportReport& report)
{
    const fs::path target = fs::absolute(fs::path(path));
    std::error_code error;
    if (!target.has_filename())
        return "a CSV file needs a name, not a folder: " + path;
    fs::create_directories(target.parent_path(), error);
    if (error)
        return "cannot create " + target.parent_path().string() + ": " + error.message();
    const fs::path partial = target.string() + ".partial";

    try {
        std::ostringstream text;
        writeBooksCsv(connection, bookIds, text);
        const std::string csv = text.str();
        {
            std::ofstream file(partial, std::ios::binary | std::ios::trunc);
            file << csv;
            file.close();
            if (!file)
                return "cannot write " + partial.string();
        }

        // Imported into an empty catalogue and exported again, it must come
        // back the same: nothing lost, nothing merged.
        db::Connection empty(":memory:");
        db::migrate(empty);
        const auto imported = CsvImporter(empty).importText(csv);
        if (imported.aborted || !imported.failures.empty()) {
            fs::remove(partial, error);
            return "the file would not import cleanly"
                + (imported.failures.empty() ? std::string() : ": line " + std::to_string(imported.failures.front().line)
                        + ", " + imported.failures.front().message);
        }
        std::vector<std::int64_t> importedIds;
        for (const auto& summary : db::BookRepository(empty).summaries())
            importedIds.push_back(summary.id);
        std::sort(importedIds.begin(), importedIds.end()); // inserted in the file's order
        std::ostringstream again;
        writeBooksCsv(empty, importedIds, again);
        if (again.str() != csv) {
            fs::remove(partial, error);
            return "the file would not re-import without loss — two books may share a title and first "
                   "author and no ISBN, and would be merged";
        }

        fs::rename(partial, target, error);
        if (error) {
            fs::remove(partial, error);
            return "the file could not be put in place: " + error.message();
        }
        report.books = static_cast<int>(importedIds.size());
        report.rows = imported.inserted + imported.updated + imported.unchanged;
        report.path = target.string();
        return std::nullopt;
    } catch (const db::DbError& failure) {
        fs::remove(partial, error);
        return failure.what();
    }
}

} // namespace pinax::io
