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
class GoogleBooksClient {
public:
    GoogleBooksClient(RequestQueue& queue, QString apiKey);

    bool available() const { return !apiKey_.isEmpty(); }

    void lookupIsbn(const std::string& isbn13, std::function<void(domain::LookupResult)> done);
    void search(const std::string& title, const std::optional<std::string>& author,
        std::function<void(domain::LookupResult)> done);

    QUrl isbnUrl(const std::string& isbn13) const;
    QUrl searchUrl(const std::string& title, const std::optional<std::string>& author) const;

private:
    void get(const QUrl& url, std::function<void(domain::LookupResult)> done);

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
