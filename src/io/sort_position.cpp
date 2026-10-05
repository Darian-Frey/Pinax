#include "io/sort_position.h"

#include <cctype>
#include <charconv>

namespace pinax::io {

namespace {

bool isDigit(char c)
{
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

bool isLetter(char c)
{
    return std::isalpha(static_cast<unsigned char>(c)) != 0;
}

std::string_view trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return text;
}

// Reads digits with an optional fractional part from the front of `text`.
// On success, advances `text` past them.
std::optional<double> leadingNumber(std::string_view& text)
{
    std::size_t length = 0;
    while (length < text.size() && isDigit(text[length]))
        ++length;
    if (length == 0)
        return std::nullopt;
    if (length + 1 < text.size() && text[length] == '.' && isDigit(text[length + 1])) {
        ++length;
        while (length < text.size() && isDigit(text[length]))
            ++length;
    }

    double value = 0;
    const auto result = std::from_chars(text.data(), text.data() + length, value);
    if (result.ec != std::errc())
        return std::nullopt;
    text.remove_prefix(length);
    return value;
}

} // namespace

std::optional<double> deriveSortPosition(std::string_view position)
{
    std::string_view rest = trim(position);

    // Rule 1: a single word may precede the number ('Broadcast 6.5').
    if (!rest.empty() && isLetter(rest.front())) {
        const std::size_t space = rest.find(' ');
        if (space == std::string_view::npos)
            return std::nullopt; // a word alone: rule 4
        rest = trim(rest.substr(space + 1));
    }

    const std::optional<double> number = leadingNumber(rest);
    if (!number)
        return std::nullopt;
    if (rest.empty())
        return number;

    // Rule 3: one letter straight after a whole number ('3a').
    if (rest.size() == 1 && isLetter(rest.front()) && *number == static_cast<int>(*number)) {
        const int letter = std::tolower(static_cast<unsigned char>(rest.front())) - 'a' + 1;
        return *number + 0.1 * letter;
    }

    // Rule 2: a range takes its first number ('1-4').
    if (rest.front() == '-')
        return number;

    return std::nullopt;
}

} // namespace pinax::io
