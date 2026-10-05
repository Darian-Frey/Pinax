#include "io/csv_reader.h"

#include <utility>

namespace pinax::io {

std::vector<CsvRecord> parseCsv(std::string_view text)
{
    if (text.starts_with("\xEF\xBB\xBF"))
        text.remove_prefix(3);

    std::vector<CsvRecord> records;
    CsvRecord record;
    std::string field;
    int line = 1;
    record.line = line;

    bool inQuotes = false;
    bool afterQuote = false; // just closed a quoted field
    bool recordHasContent = false;

    auto endField = [&] {
        record.fields.push_back(std::move(field));
        field.clear();
        afterQuote = false;
    };
    auto endRecord = [&] {
        endField();
        // A blank line is one empty field; skip it.
        if (recordHasContent || record.fields.size() > 1)
            records.push_back(std::move(record));
        record = CsvRecord{};
        recordHasContent = false;
    };

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];

        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else {
                    inQuotes = false;
                    afterQuote = true;
                }
            } else {
                if (c == '\n')
                    ++line;
                field += c;
            }
            continue;
        }

        switch (c) {
        case ',':
            endField();
            recordHasContent = true;
            break;
        case '\r':
            if (i + 1 < text.size() && text[i + 1] == '\n')
                break; // the \n ends the record
            [[fallthrough]];
        case '\n':
            endRecord();
            ++line;
            record.line = line;
            break;
        case '"':
            if (!field.empty() || afterQuote)
                throw CsvError(line, "unexpected quote inside an unquoted field");
            inQuotes = true;
            recordHasContent = true;
            break;
        default:
            if (afterQuote)
                throw CsvError(line, "text after a closing quote");
            field += c;
            recordHasContent = true;
        }
    }

    if (inQuotes)
        throw CsvError(record.line, "quoted field is never closed");
    if (recordHasContent || !field.empty() || !record.fields.empty())
        endRecord();
    return records;
}

} // namespace pinax::io
