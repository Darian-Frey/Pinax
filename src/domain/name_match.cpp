#include "domain/name_match.h"

#include <cctype>

namespace pinax::domain {

namespace {

// Lower-case words, letters only: "Iain M. Banks" -> {iain, m, banks}.
std::vector<std::string> words(const std::string& name)
{
    std::vector<std::string> result;
    std::string word;
    for (const unsigned char c : name) {
        if (std::isalpha(c) || c >= 0x80 || c == '\'') {
            word.push_back(static_cast<char>(std::tolower(c)));
        } else if (!word.empty()) {
            result.push_back(word);
            word.clear();
        }
    }
    if (!word.empty())
        result.push_back(word);
    return result;
}

bool looselyEqual(const std::string& a, const std::string& b)
{
    const auto x = words(a);
    const auto y = words(b);
    if (x.size() < 2 || y.size() < 2)
        return !x.empty() && x == y;
    if (x.back() != y.back())
        return false;
    // First names: equal when both are written out; an initial matches any
    // name with that letter ("J. Pournelle", "Jerry Pournelle").
    const std::string& first = x.front();
    const std::string& other = y.front();
    if (first.size() > 1 && other.size() > 1)
        return first == other;
    return first.front() == other.front();
}

} // namespace

std::optional<std::string> knownAuthor(const std::string& providerName, const std::vector<std::string>& known)
{
    for (const auto& name : known) {
        if (name == providerName)
            return name;
    }
    for (const auto& name : known) {
        if (looselyEqual(name, providerName))
            return name;
    }
    return std::nullopt;
}

bool shareAnAuthor(const std::vector<std::string>& some, const std::vector<std::string>& others)
{
    for (const auto& a : some) {
        for (const auto& b : others) {
            if (looselyEqual(a, b))
                return true;
        }
    }
    return false;
}

} // namespace pinax::domain
