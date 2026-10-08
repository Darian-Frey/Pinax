#include "domain/dates.h"

#include <cctype>

namespace pinax::domain {

namespace {

bool digits(std::string_view text)
{
    for (const char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c)))
            return false;
    }
    return !text.empty();
}

int number(std::string_view text)
{
    int value = 0;
    for (const char c : text)
        value = value * 10 + (c - '0');
    return value;
}

} // namespace

bool isPartialIsoDate(std::string_view text)
{
    if (text.size() != 4 && text.size() != 7 && text.size() != 10)
        return false;
    if (!digits(text.substr(0, 4)))
        return false;
    if (text.size() == 4)
        return true;
    if (text[4] != '-' || !digits(text.substr(5, 2)))
        return false;
    const int month = number(text.substr(5, 2));
    if (month < 1 || month > 12)
        return false;
    if (text.size() == 7)
        return true;
    if (text[7] != '-' || !digits(text.substr(8, 2)))
        return false;
    const int day = number(text.substr(8, 2));
    const int year = number(text.substr(0, 4));
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    constexpr int lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const int length = month == 2 && leap ? 29 : lengths[month - 1];
    return day >= 1 && day <= length;
}

} // namespace pinax::domain
