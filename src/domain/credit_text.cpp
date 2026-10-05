#include "domain/credit_text.h"

#include <cctype>

namespace pinax::domain {

namespace {

std::string trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return std::string(text);
}

} // namespace

std::vector<NamedCredit> parseCredits(std::string_view text)
{
    std::vector<NamedCredit> credits;
    if (trim(text).empty())
        return credits;

    std::size_t start = 0;
    while (true) {
        std::size_t end = text.find(" & ", start);
        const bool last = end == std::string_view::npos;
        if (last)
            end = text.size();
        std::string part = trim(text.substr(start, end - start));

        NamedCredit credit;
        if (part.ends_with(')')) {
            const std::size_t open = part.rfind('(');
            if (open != std::string::npos) {
                const std::string role = trim(std::string_view(part).substr(open + 1,
                    part.size() - open - 2));
                const auto parsed = creditRoleFromString(role);
                if (!parsed)
                    throw CreditTextError("unknown credit role '" + role + "'");
                credit.role = *parsed;
                part = trim(std::string_view(part).substr(0, open));
            }
        }
        if (part.empty())
            throw CreditTextError("empty name in '" + std::string(text) + "'");
        credit.name = std::move(part);
        credits.push_back(std::move(credit));

        if (last)
            break;
        start = end + 3;
    }
    return credits;
}

std::string formatCredits(const std::vector<NamedCredit>& credits)
{
    std::string text;
    for (const NamedCredit& credit : credits) {
        if (!text.empty())
            text += " & ";
        text += credit.name;
        if (credit.role != CreditRole::Author) {
            text += " (";
            text += toString(credit.role);
            text += ')';
        }
    }
    return text;
}

} // namespace pinax::domain
