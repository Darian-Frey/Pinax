#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace pinax::domain {

// A spreadsheet as plain data (F-022, SPEC.md §5.1): sheets of headed rows,
// each cell empty, text or a number. Filled from the database's views, so
// its figures are the application's own (AV-011); written by io.
struct Workbook {
    using Cell = std::variant<std::monostate, std::string, std::int64_t, double>;

    struct Sheet {
        std::string name;
        std::vector<std::string> headers;
        std::vector<std::vector<Cell>> rows;
    };

    std::vector<Sheet> sheets;

    const Sheet* sheet(const std::string& name) const
    {
        for (const auto& candidate : sheets) {
            if (candidate.name == name)
                return &candidate;
        }
        return nullptr;
    }
};

} // namespace pinax::domain
