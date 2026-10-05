#include "domain/isbn.h"

#include <algorithm>
#include <cctype>

namespace pinax::domain {

namespace {

bool isDigit(char c)
{
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

} // namespace

std::string normaliseIsbn(std::string_view isbn)
{
    std::string result;
    for (char c : isbn) {
        if (c == '-' || c == ' ')
            continue;
        result += c == 'x' ? 'X' : c;
    }
    return result;
}

bool isValidIsbn13(std::string_view isbn)
{
    if (isbn.size() != 13 || !std::all_of(isbn.begin(), isbn.end(), isDigit))
        return false;

    int sum = 0;
    for (std::size_t i = 0; i < 12; ++i)
        sum += (isbn[i] - '0') * (i % 2 == 0 ? 1 : 3);
    const int check = (10 - sum % 10) % 10;
    return check == isbn[12] - '0';
}

bool isValidIsbn10(std::string_view isbn)
{
    if (isbn.size() != 10 || !std::all_of(isbn.begin(), isbn.begin() + 9, isDigit))
        return false;

    int sum = 0;
    for (std::size_t i = 0; i < 9; ++i)
        sum += (isbn[i] - '0') * static_cast<int>(10 - i);
    const int check = (11 - sum % 11) % 11;
    const char last = isbn[9];
    if (last == 'X')
        return check == 10;
    return isDigit(last) && check == last - '0';
}

} // namespace pinax::domain
