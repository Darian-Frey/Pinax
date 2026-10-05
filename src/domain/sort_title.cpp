#include "domain/sort_title.h"

#include <array>

namespace pinax::domain {

std::string makeSortTitle(std::string_view title)
{
    constexpr std::array<std::string_view, 3> articles{"The", "An", "A"};

    for (std::string_view article : articles) {
        // The article must be a whole word followed by more title.
        if (title.size() > article.size() + 1 && title.starts_with(article)
            && title[article.size()] == ' ') {
            std::string result(title.substr(article.size() + 1));
            result += ", ";
            result += article;
            return result;
        }
    }
    return std::string(title);
}

} // namespace pinax::domain
