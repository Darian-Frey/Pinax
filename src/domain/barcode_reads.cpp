#include "domain/barcode_reads.h"

#include "domain/isbn.h"

#include <algorithm>
#include <cctype>

namespace pinax::domain {

namespace {

bool allDigits(std::string_view text)
{
    return !text.empty()
        && std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c); });
}

} // namespace

std::optional<std::string> isbnFromBarcode(std::string_view decoded)
{
    // The main code, then perhaps an add-on of two or five digits.
    std::string_view main = decoded.substr(0, std::min<std::size_t>(13, decoded.size()));
    std::string_view rest = decoded.size() > 13 ? decoded.substr(13) : std::string_view {};
    if (!rest.empty() && rest.front() == ' ')
        rest.remove_prefix(1);
    if (!rest.empty() && !(allDigits(rest) && (rest.size() == 2 || rest.size() == 5)))
        return std::nullopt;
    if (main.size() != 13 || !allDigits(main))
        return std::nullopt;
    if (!main.starts_with("978") && !main.starts_with("979"))
        return std::nullopt; // an EAN-13, but not a book's
    if (!isValidIsbn13(main))
        return std::nullopt;
    return std::string(main);
}

std::optional<std::string> BarcodeReads::frame(const std::vector<std::string>& decoded, Clock::time_point at)
{
    std::optional<std::string> seen;
    for (const auto& text : decoded) {
        const auto isbn = isbnFromBarcode(text);
        if (!isbn)
            continue;
        if (seen && *seen != *isbn) {
            seen.reset(); // two books in view: neither is read
            break;
        }
        seen = isbn;
    }

    if (!seen) {
        candidate_.clear();
        agreeing_ = 0;
        return std::nullopt;
    }
    if (*seen == candidate_) {
        ++agreeing_;
    } else {
        candidate_ = *seen;
        agreeing_ = 1;
    }
    if (agreeing_ < framesToAgree)
        return std::nullopt;

    // Read. The same book still in view is not read again for a while.
    candidate_.clear();
    agreeing_ = 0;
    if (*seen == lastRead_ && lastReadAt_ && at - *lastReadAt_ < quietAfterRead) {
        lastReadAt_ = at; // still in view: quiet until it has been away a while
        return std::nullopt;
    }
    lastRead_ = *seen;
    lastReadAt_ = at;
    return seen;
}

void BarcodeReads::reset()
{
    candidate_.clear();
    agreeing_ = 0;
    lastRead_.clear();
    lastReadAt_.reset();
}

} // namespace pinax::domain
