#pragma once

#include "domain/candidate.h"

#include <QByteArray>
#include <QUrl>

#include <functional>
#include <string>
#include <vector>

namespace pinax::metadata {

class RequestQueue;

// The British Library's catalogue (D-022, SPEC.md §3.6), through the open
// SRU interface of its Alma system: one request per ISBN, MARC 21 records
// back. No key. Strong on UK editions — imprint, page count, the original
// year behind a reprint, subject headings — and carries no synopsis or
// cover, so it fills the gaps in another provider's answer rather than
// replacing it. Asked by ISBN only.
class BritishLibraryClient {
public:
    explicit BritishLibraryClient(RequestQueue& queue);

    void lookupIsbn(const std::string& isbn13, std::function<void(domain::LookupResult)> done);

    static QUrl isbnUrl(const std::string& isbn13);

private:
    RequestQueue& queue_;
};

namespace britishlibrary {

// The records in an SRU searchRetrieve response whose own ISBN — 13 digits,
// or 10 converted — is `isbn13`. The index also returns related editions
// (an ebook beside a paperback), and those are dropped. Subject headings
// (650 and 655, subfield a) become categories with MARC's closing full stop
// removed; nothing else about them is changed (D-009).
std::vector<domain::Candidate> parseSru(const QByteArray& xml, const std::string& isbn13);

// The diagnostic message of an SRU error response, if it is one.
std::optional<std::string> parseDiagnostic(const QByteArray& xml);

} // namespace britishlibrary

} // namespace pinax::metadata
