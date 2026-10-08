#include "domain/genre_filter.h"

#include <array>
#include <cctype>
#include <string_view>

namespace pinax::domain {

namespace {

// Lending, format and list labels, lower case.
constexpr std::array<std::string_view, 12> serviceSubjects {
    "accessible book",
    "protected daisy",
    "in library",
    "lending library",
    "overdrive",
    "large type books",
    "large print books",
    "long now manual for civilization",
    "new york times bestseller",
    "new york times reviewed",
    "internet archive wishlist",
    "open library staff picks",
};

std::string lowered(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (const unsigned char c : text)
        out.push_back(static_cast<char>(std::tolower(c)));
    // Trimmed, and a closing full stop dropped, as MARC headings carry one.
    while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
        out.pop_back();
    while (!out.empty() && out.front() == ' ')
        out.erase(out.begin());
    return out;
}

// "nyt:trade-fiction-paperback=2013-03-31", "award:hugo_award=2006": a
// lower-case word, a colon, then a value with an equals sign and no spaces.
bool isMachineTag(const std::string& subject)
{
    const auto colon = subject.find(':');
    if (colon == std::string::npos || colon == 0)
        return false;
    for (std::size_t i = 0; i < colon; ++i) {
        const auto c = static_cast<unsigned char>(subject[i]);
        if (!std::islower(c) && !std::isdigit(c) && c != '_')
            return false;
    }
    const std::string_view value(subject.data() + colon + 1, subject.size() - colon - 1);
    return value.find('=') != std::string_view::npos && value.find(' ') == std::string_view::npos;
}

} // namespace

bool isServiceSubject(const std::string& subject)
{
    if (isMachineTag(subject))
        return true;
    const std::string key = lowered(subject);
    for (const auto& service : serviceSubjects) {
        if (key == service)
            return true;
    }
    return false;
}

} // namespace pinax::domain
