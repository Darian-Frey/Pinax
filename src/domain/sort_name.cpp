#include "domain/sort_name.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <vector>

namespace pinax::domain {

namespace {

bool isParticle(std::string_view word)
{
    constexpr std::array<std::string_view, 10> particles{
        "de", "del", "della", "der", "di", "du", "la", "le", "van", "von"};
    std::string lower(word);
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return std::find(particles.begin(), particles.end(), lower) != particles.end();
}

std::vector<std::string_view> words(std::string_view text)
{
    std::vector<std::string_view> result;
    std::size_t start = 0;
    while (start < text.size()) {
        while (start < text.size() && text[start] == ' ')
            ++start;
        std::size_t end = start;
        while (end < text.size() && text[end] != ' ')
            ++end;
        if (end > start)
            result.push_back(text.substr(start, end - start));
        start = end;
    }
    return result;
}

std::string join(std::vector<std::string_view>::const_iterator first,
    std::vector<std::string_view>::const_iterator last)
{
    std::string result;
    for (auto it = first; it != last; ++it) {
        if (!result.empty())
            result += ' ';
        result += *it;
    }
    return result;
}

} // namespace

std::string makeSortName(std::string_view name)
{
    const std::vector<std::string_view> parts = words(name);
    if (parts.size() < 2)
        return std::string(name);

    // The surname is the last word plus any particles directly before it,
    // provided at least one given name is left over.
    auto surnameStart = parts.end() - 1;
    while (surnameStart - 1 > parts.begin() && isParticle(*(surnameStart - 1)))
        --surnameStart;

    return join(surnameStart, parts.end()) + ", " + join(parts.begin(), surnameStart);
}

} // namespace pinax::domain
