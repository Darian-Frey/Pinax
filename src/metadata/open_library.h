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

// Open Library, the primary provider (D-019, SPEC.md §3.1). No key needed.
//
// An ISBN lookup is up to three requests: the Books API for the edition's
// facts and authors by name, the edition record for its work, and the work
// for a synopsis when the edition has none. A title-and-author search is one
// request and yields work-level candidates, which are never accepted without
// the owner's confirmation (AV-010).
class OpenLibraryClient {
public:
    explicit OpenLibraryClient(RequestQueue& queue);

    void lookupIsbn(const std::string& isbn13, std::function<void(domain::LookupResult)> done);
    void search(const std::string& title, const std::optional<std::string>& author,
        std::function<void(domain::LookupResult)> done);
    // A work's synopsis, for a candidate found by search.
    void description(const std::string& workKey,
        std::function<void(std::optional<std::string> description, std::optional<std::string> error)> done);

    static QUrl booksApiUrl(const std::string& isbn13);
    static QUrl recordUrl(const std::string& key); // "/books/OL..M" or "/works/OL..W"
    static QUrl searchUrl(const std::string& title, const std::optional<std::string>& author);

private:
    RequestQueue& queue_;
};

namespace openlibrary {

// Pure parsing, tested against recorded responses. Each returns nullopt or
// an empty result for a body that is not what was expected.

// The Books API (jscmd=data) answer for one ISBN; nullopt when not found.
// Sets providerKey to the edition key.
std::optional<domain::Candidate> parseBooksApi(const QByteArray& json, const std::string& isbn13);

struct EditionRecord {
    std::optional<std::string> workKey;
    std::optional<std::string> description;
};
std::optional<EditionRecord> parseEdition(const QByteArray& json);

// A work's synopsis, whichever of Open Library's two shapes it is stored in.
std::optional<std::string> parseWorkDescription(const QByteArray& json);

std::vector<domain::Candidate> parseSearch(const QByteArray& json);

} // namespace openlibrary

} // namespace pinax::metadata
