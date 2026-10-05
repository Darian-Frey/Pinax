#pragma once

#include <cctype>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>

// Row-level helpers shared by the CSV importers. Internal to io.
namespace pinax::io::detail {

// A row that cannot be imported, for a reason the file can fix. Reported
// with the row's line number; the run carries on.
struct RowError {
    std::string message;
};

inline std::string trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return std::string(text);
}

inline std::optional<std::string> optionalText(const std::string& cell)
{
    if (cell.empty())
        return std::nullopt;
    return cell;
}

// Empty is nullopt; anything that is not wholly a number is a RowError.
template <typename Number>
std::optional<Number> parseNumber(const std::string& cell, std::string_view column)
{
    if (cell.empty())
        return std::nullopt;
    Number value {};
    const auto result = std::from_chars(cell.data(), cell.data() + cell.size(), value);
    if (result.ec != std::errc() || result.ptr != cell.data() + cell.size())
        throw RowError { std::string(column) + " '" + cell + "' is not a number" };
    return value;
}

} // namespace pinax::io::detail
