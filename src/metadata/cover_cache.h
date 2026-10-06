#pragma once

#include <QString>
#include <QUrl>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace pinax::metadata {

class RequestQueue;

struct CoverResult {
    std::optional<std::string> relativePath; // "covers/12.jpg", relative to the data directory
    std::optional<std::string> error;        // why there is no cover
    bool downloaded = false;                 // false when it was already cached
};

// Covers on disk beside the database (SPEC.md §4, F-013): one file per book,
// `covers/<book id>.jpg` or `.png`. The database stores the relative path,
// never the image (ARCHITECTURE.md invariant 7). A cover already on disk is
// never fetched again; a download is checked to be a real image before it is
// kept, and written atomically.
class CoverCache {
public:
    CoverCache(RequestQueue& queue, QString dataDirectory);

    // The cached cover's relative path, if there is one.
    std::optional<std::string> cached(std::int64_t bookId) const;

    void fetch(std::int64_t bookId, const QUrl& url, std::function<void(CoverResult)> done);

    // Deletes a book's cover file, if any. Ids can be reused once a book is
    // deleted, and a new book must not inherit an old cover.
    void forget(std::int64_t bookId) const;

    // Open Library serves a blank 1-pixel image for a missing cover unless
    // asked not to; with `default=false` a miss is a plain 404 (SPEC.md §3.1).
    static QUrl politeUrl(const QUrl& url);

    // "jpg" or "png" for a body that is plainly one, of a size no placeholder
    // has; nullopt otherwise.
    static std::optional<QString> imageType(const QByteArray& body);

private:
    QString path(std::int64_t bookId, const QString& extension) const;

    RequestQueue& queue_;
    QString dataDirectory_;
};

} // namespace pinax::metadata
