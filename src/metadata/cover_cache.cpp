#include "metadata/cover_cache.h"

#include "metadata/request_queue.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QUrlQuery>

namespace pinax::metadata {

namespace {

const QString folder = QStringLiteral("covers");
constexpr qsizetype smallestCover = 1024; // a 1×1 placeholder is a few dozen bytes

} // namespace

CoverCache::CoverCache(RequestQueue& queue, QString dataDirectory)
    : queue_(queue)
    , dataDirectory_(std::move(dataDirectory))
{
}

QString CoverCache::path(std::int64_t bookId, const QString& extension) const
{
    return dataDirectory_ + QLatin1Char('/') + folder + QLatin1Char('/') + QString::number(bookId)
        + QLatin1Char('.') + extension;
}

std::optional<std::string> CoverCache::cached(std::int64_t bookId) const
{
    for (const QString& extension : {QStringLiteral("jpg"), QStringLiteral("png")}) {
        if (QFile::exists(path(bookId, extension)))
            return (folder + QLatin1Char('/') + QString::number(bookId) + QLatin1Char('.') + extension).toStdString();
    }
    return std::nullopt;
}

void CoverCache::forget(std::int64_t bookId) const
{
    for (const QString& extension : {QStringLiteral("jpg"), QStringLiteral("png")})
        QFile::remove(path(bookId, extension));
}

QUrl CoverCache::politeUrl(const QUrl& url)
{
    if (url.host() != QStringLiteral("covers.openlibrary.org"))
        return url;
    QUrl polite = url;
    QUrlQuery query(polite);
    if (!query.hasQueryItem(QStringLiteral("default")))
        query.addQueryItem(QStringLiteral("default"), QStringLiteral("false"));
    polite.setQuery(query);
    return polite;
}

std::optional<QString> CoverCache::imageType(const QByteArray& body)
{
    if (body.size() < smallestCover)
        return std::nullopt;
    if (body.startsWith("\xFF\xD8\xFF"))
        return QStringLiteral("jpg");
    if (body.startsWith("\x89PNG\r\n\x1A\n"))
        return QStringLiteral("png");
    return std::nullopt;
}

void CoverCache::fetch(std::int64_t bookId, const QUrl& url, std::function<void(CoverResult)> done)
{
    if (const auto existing = cached(bookId)) {
        done({existing, std::nullopt, false});
        return;
    }
    queue_.enqueue(politeUrl(url), [this, bookId, done](const HttpReply& reply) {
        if (reply.status == 404) {
            done({std::nullopt, "no cover", false});
            return;
        }
        if (reply.status != 200) {
            done({std::nullopt,
                reply.status == 0 ? "cover not downloaded: " + reply.error.toStdString()
                                  : "cover not downloaded: HTTP " + std::to_string(reply.status),
                false});
            return;
        }
        const auto type = imageType(reply.body);
        if (!type) {
            done({std::nullopt, "the download was not a cover image", false});
            return;
        }
        if (!QDir(dataDirectory_).mkpath(folder)) {
            done({std::nullopt, "cannot create the covers folder", false});
            return;
        }
        QSaveFile file(path(bookId, *type));
        if (!file.open(QIODevice::WriteOnly) || file.write(reply.body) != reply.body.size() || !file.commit()) {
            done({std::nullopt, "cannot write the cover: " + file.errorString().toStdString(), false});
            return;
        }
        done({cached(bookId), std::nullopt, true});
    });
}

} // namespace pinax::metadata
