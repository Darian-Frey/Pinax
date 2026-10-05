#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace pinax::io {

// One CSV record and the physical line it starts on (1-based), so an error
// in a record whose quoted field spans lines is still reported where it
// begins.
struct CsvRecord {
    int line = 0;
    std::vector<std::string> fields;
};

class CsvError : public std::runtime_error {
public:
    CsvError(int line, const std::string& message)
        : std::runtime_error(message)
        , line_(line)
    {
    }

    int line() const { return line_; }

private:
    int line_;
};

// Parses RFC 4180 CSV: comma-separated, fields optionally double-quoted, a
// doubled quote inside quotes standing for one, CRLF or LF line ends, line
// breaks allowed inside quoted fields. A leading UTF-8 byte-order mark is
// skipped and blank lines are ignored. Throws CsvError for a quote left open
// or text after a closing quote.
std::vector<CsvRecord> parseCsv(std::string_view text);

} // namespace pinax::io
