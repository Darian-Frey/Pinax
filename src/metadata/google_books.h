#pragma once

#include "domain/candidate.h"

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <functional>
#include <optional>
#include <string>

namespace pinax::metadata {

class RequestQueue;

// Google Books, the second opinion (D-019, SPEC.md §3.2). Google gives no
// keyless quota, so the client is usable only with an API key; without one,
// available() is false and nothing is sent.
//
// Google's field-qualified queries (`isbn:`, `intitle:`, `inauthor:`) have
// answered nothing since at least 2026-10-06 while free text works
// (IMP-007). So each lookup asks the qualified way first and, only if that
// finds nothing, asks again in free text — keeping, for an ISBN, only the
// volumes that carry it, and for a title, only those whose title agrees and
// that share an author.
class GoogleBooksClient {
public:
    GoogleBooksClient(RequestQueue& queue, QString apiKey);

    bool available() const { return !apiKey_.isEmpty(); }

    // `tag` marks every request a lookup makes, so that a caller may
    // withdraw them from the queue (RequestQueue::cancelTagged; IMP-011).
    void lookupIsbn(const std::string& isbn13, std::function<void(domain::LookupResult)> done, int tag = 0);
    void search(const std::string& title, const std::optional<std::string>& author,
        std::function<void(domain::LookupResult)> done, int tag = 0);

    QUrl isbnUrl(const std::string& isbn13) const;
    QUrl searchUrl(const std::string& title, const std::optional<std::string>& author) const;
    // The fallback: the words alone, unqualified.
    QUrl freeTextUrl(const std::string& words) const;

private:
    void get(const QUrl& url, std::function<void(domain::LookupResult)> done, int tag);

    RequestQueue& queue_;
    QString apiKey_;
};

namespace googlebooks {

// The volumes answer as candidates, in the provider's order. Categories are
// kept verbatim (D-009).
std::vector<domain::Candidate> parseVolumes(const QByteArray& json);

// The provider's own complaint, if the body is an error: "Quota exceeded…".
std::optional<std::string> parseError(const QByteArray& json);

} // namespace googlebooks

} // namespace pinax::metadata
