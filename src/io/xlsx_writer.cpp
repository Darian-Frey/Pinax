#include "io/xlsx_writer.h"

#include <xlsxwriter.h>

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <vector>

namespace pinax::io {

namespace {

namespace fs = std::filesystem;

// Roughly how many characters wide a cell's text is, counting UTF-8 code
// points rather than bytes.
std::size_t displayWidth(const std::string& text)
{
    return static_cast<std::size_t>(
        std::count_if(text.begin(), text.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}

} // namespace

std::optional<std::string> writeWorkbook(const domain::Workbook& workbook, const std::string& path)
{
    const fs::path target = fs::absolute(fs::path(path));
    std::error_code error;
    if (!target.has_filename())
        return "a workbook needs a file name, not a folder: " + path;
    fs::create_directories(target.parent_path(), error);
    if (error)
        return "cannot create " + target.parent_path().string() + ": " + error.message();
    const fs::path partial = target.string() + ".partial";
    fs::remove(partial, error);

    lxw_workbook* book = workbook_new(partial.c_str());
    if (!book)
        return "cannot start the workbook at " + partial.string();
    lxw_format* bold = workbook_add_format(book);
    format_set_bold(bold);

    for (const auto& sheet : workbook.sheets) {
        lxw_worksheet* page = workbook_add_worksheet(book, sheet.name.c_str());
        if (!page) {
            workbook_close(book);
            fs::remove(partial, error);
            return "cannot add the sheet " + sheet.name;
        }
        std::vector<std::size_t> widths(sheet.headers.size(), 4);
        for (std::size_t column = 0; column < sheet.headers.size(); ++column) {
            worksheet_write_string(page, 0, static_cast<lxw_col_t>(column), sheet.headers[column].c_str(), bold);
            widths[column] = std::max(widths[column], displayWidth(sheet.headers[column]) + 2);
        }
        for (std::size_t row = 0; row < sheet.rows.size(); ++row) {
            const auto line = static_cast<lxw_row_t>(row + 1);
            for (std::size_t column = 0; column < sheet.rows[row].size(); ++column) {
                const auto col = static_cast<lxw_col_t>(column);
                const auto& cell = sheet.rows[row][column];
                if (const auto* text = std::get_if<std::string>(&cell)) {
                    worksheet_write_string(page, line, col, text->c_str(), nullptr);
                    if (column < widths.size())
                        widths[column] = std::max(widths[column], displayWidth(*text) + 1);
                } else if (const auto* whole = std::get_if<std::int64_t>(&cell)) {
                    worksheet_write_number(page, line, col, static_cast<double>(*whole), nullptr);
                    if (column < widths.size())
                        widths[column] = std::max(widths[column], std::to_string(*whole).size() + 1);
                } else if (const auto* real = std::get_if<double>(&cell)) {
                    worksheet_write_number(page, line, col, *real, nullptr);
                }
            }
        }
        for (std::size_t column = 0; column < widths.size(); ++column) {
            worksheet_set_column(page, static_cast<lxw_col_t>(column), static_cast<lxw_col_t>(column),
                static_cast<double>(std::min<std::size_t>(widths[column], 60)), nullptr);
        }
        worksheet_freeze_panes(page, 1, 0);
        if (!sheet.headers.empty()) {
            worksheet_autofilter(page, 0, 0, static_cast<lxw_row_t>(sheet.rows.size()),
                static_cast<lxw_col_t>(sheet.headers.size() - 1));
        }
    }

    const lxw_error closed = workbook_close(book);
    if (closed != LXW_NO_ERROR) {
        fs::remove(partial, error);
        return std::string("the workbook could not be written: ") + lxw_strerror(closed);
    }
    fs::rename(partial, target, error);
    if (error) {
        fs::remove(partial, error);
        return "the workbook could not be put in place: " + error.message();
    }
    return std::nullopt;
}

} // namespace pinax::io
