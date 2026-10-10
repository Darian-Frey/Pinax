#include "metadata/google_books.h"

#include "domain/enrichment.h"
#include "domain/isbn.h"
#include "domain/name_match.h"
#include "metadata/request_queue.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>

namespace pinax::metadata {

using domain::Candidate;
using domain::LookupResult;

namespace {

const QString base = QStringLiteral("https://www.googleapis.com/books/v1/volumes");

std::optional<std::string> text(const QJsonValue& value)
{
    const QString string = value.toString().trimmed();
    if (string.isEmpty())
        return std::nullopt;
    return string.toStdString();
}

std::optional<QJsonObject> objectOf(const QByteArray& json)
{
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isObject())
        return std::nullopt;
    return document.object();
}

} // namespace

namespace googlebooks {

std::vector<Candidate> parseVolumes(const QByteArray& json)
{
    std::vector<Candidate> candidates;
    const auto root = objectOf(json);
    if (!root)
        return candidates;
    for (const auto& value : root->value(QStringLiteral("items")).toArray()) {
        const QJsonObject item = value.toObject();
        const QJsonObject info = item.value(QStringLiteral("volumeInfo")).toObject();
        Candidate candidate;
        candidate.source = domain::Source::GoogleBooks;
        candidate.providerKey = text(item.value(QStringLiteral("id"))).value_or("");
        candidate.title = text(info.value(QStringLiteral("title"))).value_or("");
        candidate.subtitle = text(info.value(QStringLiteral("subtitle")));
        for (const auto& author : info.value(QStringLiteral("authors")).toArray()) {
            if (auto name = text(author))
                candidate.authors.push_back(*name);
        }
        candidate.publisher = text(info.value(QStringLiteral("publisher")));
        const QString date = info.value(QStringLiteral("publishedDate")).toString();
        if (date.size() >= 4) {
            bool ok = false;
            const int year = date.left(4).toInt(&ok);
            if (ok)
                candidate.publishedYear = year;
        }
        if (const int pages = info.value(QStringLiteral("pageCount")).toInt(); pages > 0)
            candidate.pageCount = pages;
        candidate.description = text(info.value(QStringLiteral("description")));
        for (const auto& category : info.value(QStringLiteral("categories")).toArray()) {
            if (auto name = text(category))
                candidate.categories.push_back(*name);
        }
        for (const auto& id : info.value(QStringLiteral("industryIdentifiers")).toArray()) {
            const QJsonObject identifier = id.toObject();
            const auto type = identifier.value(QStringLiteral("type")).toString();
            const auto number = text(identifier.value(QStringLiteral("identifier")));
            if (type == QStringLiteral("ISBN_13") && number && domain::isValidIsbn13(*number))
                candidate.isbn13 = number;
            else if (type == QStringLiteral("ISBN_10") && number && domain::isValidIsbn10(*number))
                candidate.isbn10 = number;
        }
        if (auto thumbnail = text(info.value(QStringLiteral("imageLinks")).toObject().value(QStringLiteral("thumbnail")))) {
            // Google serves these over http; ask for https.
            if (thumbnail->rfind("http://", 0) == 0)
                thumbnail->replace(0, 7, "https://");
            candidate.coverUrl = thumbnail;
        }
        if (!candidate.title.empty())
            candidates.push_back(std::move(candidate));
    }
    return candidates;
}

std::optional<std::string> parseError(const QByteArray& json)
{
    const auto root = objectOf(json);
    if (!root || !root->contains(QStringLiteral("error")))
        return std::nullopt;
    return text(root->value(QStringLiteral("error")).toObject().value(QStringLiteral("message")));
}

} // namespace googlebooks

GoogleBooksClient::GoogleBooksClient(RequestQueue& queue, QString apiKey)
    : queue_(queue)
    , apiKey_(std::move(apiKey))
{
}

QUrl GoogleBooksClient::isbnUrl(const std::string& isbn13) const
{
    QUrl url(base);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), QStringLiteral("isbn:") + QString::fromStdString(isbn13));
    query.addQueryItem(QStringLiteral("key"), apiKey_);
    url.setQuery(query);
    return url;
}

QUrl GoogleBooksClient::searchUrl(const std::string& title, const std::optional<std::string>& author) const
{
    QString q = QStringLiteral("intitle:") + QString::fromStdString(title);
    if (author)
        q += QStringLiteral("+inauthor:") + QString::fromStdString(*author);
    QUrl url(base);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), q);
    query.addQueryItem(QStringLiteral("key"), apiKey_);
    url.setQuery(query);
    return url;
}

QUrl GoogleBooksClient::freeTextUrl(const std::string& words) const
{
    QUrl url(base);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), QString::fromStdString(words));
    query.addQueryItem(QStringLiteral("key"), apiKey_);
    url.setQuery(query);
    return url;
}

void GoogleBooksClient::lookupIsbn(const std::string& isbn13, std::function<void(LookupResult)> done, int tag)
{
    get(isbnUrl(isbn13), [this, isbn13, done, tag](LookupResult qualified) {
        if (qualified.error || !qualified.candidates.empty()) {
            done(std::move(qualified));
            return;
        }
        // Free text finds pages that mention the number — a bibliography,
        // say — so only a volume carrying this very ISBN is kept (IMP-007).
        get(freeTextUrl(isbn13), [isbn13, done](LookupResult loose) {
            std::erase_if(loose.candidates, [&](const Candidate& candidate) {
                const bool by13 = candidate.isbn13 == isbn13;
                const bool by10 = candidate.isbn10 && domain::isValidIsbn10(*candidate.isbn10)
                    && domain::isbn10To13(*candidate.isbn10) == isbn13;
                return !by13 && !by10;
            });
            done(std::move(loose));
        }, tag);
    }, tag);
}

void GoogleBooksClient::search(const std::string& title, const std::optional<std::string>& author,
    std::function<void(LookupResult)> done, int tag)
{
    get(searchUrl(title, author), [this, title, author, done, tag](LookupResult qualified) {
        if (qualified.error || !qualified.candidates.empty()) {
            done(std::move(qualified));
            return;
        }
        // Free text also finds books about the book, and others of the
        // name: the title must agree and an author be shared (IMP-007).
        // The owner still chooses among what is left (AV-010).
        const std::string words = author ? title + " " + *author : title;
        get(freeTextUrl(words), [title, author, done](LookupResult loose) {
            std::erase_if(loose.candidates, [&](const Candidate& candidate) {
                if (!domain::titlesAgree(title, candidate.title))
                    return true;
                return author && !domain::shareAnAuthor({*author}, candidate.authors);
            });
            done(std::move(loose));
        }, tag);
    }, tag);
}

void GoogleBooksClient::get(const QUrl& url, std::function<void(LookupResult)> done, int tag)
{
    if (!available()) {
        done({{}, "Google Books needs an API key, and none is set."});
        return;
    }
    queue_.enqueue(url, [done](const HttpReply& reply) {
        if (reply.status != 200) {
            const auto problem = googlebooks::parseError(reply.body);
            done({{}, problem ? "Google Books: " + *problem
                              : "Google Books answered " + std::to_string(reply.status)});
            return;
        }
        done({googlebooks::parseVolumes(reply.body), std::nullopt});
    }, tag);
}

} // namespace pinax::metadata
