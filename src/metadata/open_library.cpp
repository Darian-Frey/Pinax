#include "metadata/open_library.h"

#include "domain/isbn.h"
#include "metadata/request_queue.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrlQuery>

namespace pinax::metadata {

using domain::Candidate;
using domain::LookupResult;

namespace {

const QString base = QStringLiteral("https://openlibrary.org");

std::optional<std::string> text(const QJsonValue& value)
{
    const QString string = value.toString().trimmed();
    if (string.isEmpty())
        return std::nullopt;
    return string.toStdString();
}

// Open Library stores text either as a string or as {"type", "value"}.
std::optional<std::string> textOrValue(const QJsonValue& value)
{
    if (value.isObject())
        return text(value.toObject().value(QStringLiteral("value")));
    return text(value);
}

// The first four-digit year in a free-form date: "March 26, 2008" -> 2008.
std::optional<int> yearIn(const QString& date)
{
    static const QRegularExpression year(QStringLiteral("\\b(1[4-9]\\d\\d|2[01]\\d\\d)\\b"));
    const auto match = year.match(date);
    if (!match.hasMatch())
        return std::nullopt;
    return match.captured(1).toInt();
}

std::optional<std::string> firstString(const QJsonValue& array)
{
    const QJsonArray values = array.toArray();
    for (const auto& value : values) {
        if (auto string = text(value))
            return string;
    }
    return std::nullopt;
}

std::optional<QJsonObject> objectOf(const QByteArray& json)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;
    return document.object();
}

QString httpProblem(const HttpReply& reply)
{
    if (reply.status == 0)
        return QStringLiteral("Open Library could not be reached: %1").arg(reply.error);
    return QStringLiteral("Open Library answered %1").arg(reply.status);
}

} // namespace

namespace openlibrary {

std::optional<Candidate> parseBooksApi(const QByteArray& json, const std::string& isbn13)
{
    const auto root = objectOf(json);
    if (!root)
        return std::nullopt;
    const QJsonValue entry = root->value(QStringLiteral("ISBN:") + QString::fromStdString(isbn13));
    if (!entry.isObject())
        return std::nullopt;
    const QJsonObject book = entry.toObject();

    Candidate candidate;
    candidate.source = domain::Source::OpenLibrary;
    candidate.providerKey = text(book.value(QStringLiteral("key"))).value_or("");
    candidate.title = text(book.value(QStringLiteral("title"))).value_or("");
    candidate.subtitle = text(book.value(QStringLiteral("subtitle")));
    for (const auto& author : book.value(QStringLiteral("authors")).toArray()) {
        if (auto name = text(author.toObject().value(QStringLiteral("name"))))
            candidate.authors.push_back(*name);
    }
    const QJsonArray publishers = book.value(QStringLiteral("publishers")).toArray();
    if (!publishers.isEmpty())
        candidate.publisher = text(publishers.first().toObject().value(QStringLiteral("name")));
    candidate.publishedYear = yearIn(book.value(QStringLiteral("publish_date")).toString());
    if (const int pages = book.value(QStringLiteral("number_of_pages")).toInt(); pages > 0)
        candidate.pageCount = pages;

    const QJsonObject ids = book.value(QStringLiteral("identifiers")).toObject();
    if (auto isbn = firstString(ids.value(QStringLiteral("isbn_13"))); isbn && domain::isValidIsbn13(*isbn))
        candidate.isbn13 = isbn;
    else
        candidate.isbn13 = isbn13;
    if (auto isbn = firstString(ids.value(QStringLiteral("isbn_10"))); isbn && domain::isValidIsbn10(*isbn))
        candidate.isbn10 = isbn;

    for (const auto& subject : book.value(QStringLiteral("subjects")).toArray()) {
        if (auto name = text(subject.toObject().value(QStringLiteral("name"))))
            candidate.categories.push_back(*name);
    }
    candidate.coverUrl = text(book.value(QStringLiteral("cover")).toObject().value(QStringLiteral("large")));
    if (candidate.title.empty())
        return std::nullopt;
    return candidate;
}

std::optional<EditionRecord> parseEdition(const QByteArray& json)
{
    const auto root = objectOf(json);
    if (!root)
        return std::nullopt;
    EditionRecord record;
    const QJsonArray works = root->value(QStringLiteral("works")).toArray();
    if (!works.isEmpty())
        record.workKey = text(works.first().toObject().value(QStringLiteral("key")));
    record.description = textOrValue(root->value(QStringLiteral("description")));
    return record;
}

std::optional<std::string> parseWorkDescription(const QByteArray& json)
{
    const auto root = objectOf(json);
    if (!root)
        return std::nullopt;
    return textOrValue(root->value(QStringLiteral("description")));
}

std::vector<Candidate> parseSearch(const QByteArray& json)
{
    std::vector<Candidate> candidates;
    const auto root = objectOf(json);
    if (!root)
        return candidates;
    for (const auto& value : root->value(QStringLiteral("docs")).toArray()) {
        const QJsonObject doc = value.toObject();
        Candidate candidate;
        candidate.source = domain::Source::OpenLibrary;
        candidate.providerKey = text(doc.value(QStringLiteral("key"))).value_or("");
        candidate.workKey = text(doc.value(QStringLiteral("key")));
        candidate.title = text(doc.value(QStringLiteral("title"))).value_or("");
        for (const auto& author : doc.value(QStringLiteral("author_name")).toArray()) {
            if (auto name = text(author))
                candidate.authors.push_back(*name);
        }
        candidate.publisher = firstString(doc.value(QStringLiteral("publisher")));
        if (const int year = doc.value(QStringLiteral("first_publish_year")).toInt(); year > 0)
            candidate.firstPublishedYear = year;
        if (const int pages = doc.value(QStringLiteral("number_of_pages_median")).toInt(); pages > 0)
            candidate.pageCount = pages;
        for (const auto& subject : doc.value(QStringLiteral("subject")).toArray()) {
            if (auto name = text(subject))
                candidate.categories.push_back(*name);
        }
        if (const int cover = doc.value(QStringLiteral("cover_i")).toInt(); cover > 0)
            candidate.coverUrl = QStringLiteral("https://covers.openlibrary.org/b/id/%1-L.jpg").arg(cover).toStdString();
        if (!candidate.title.empty())
            candidates.push_back(std::move(candidate));
    }
    return candidates;
}

} // namespace openlibrary

OpenLibraryClient::OpenLibraryClient(RequestQueue& queue)
    : queue_(queue)
{
}

QUrl OpenLibraryClient::booksApiUrl(const std::string& isbn13)
{
    QUrl url(base + QStringLiteral("/api/books"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("bibkeys"), QStringLiteral("ISBN:") + QString::fromStdString(isbn13));
    query.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    query.addQueryItem(QStringLiteral("jscmd"), QStringLiteral("data"));
    url.setQuery(query);
    return url;
}

QUrl OpenLibraryClient::recordUrl(const std::string& key)
{
    return QUrl(base + QString::fromStdString(key) + QStringLiteral(".json"));
}

QUrl OpenLibraryClient::searchUrl(const std::string& title, const std::optional<std::string>& author)
{
    QUrl url(base + QStringLiteral("/search.json"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("title"), QString::fromStdString(title));
    if (author)
        query.addQueryItem(QStringLiteral("author"), QString::fromStdString(*author));
    query.addQueryItem(QStringLiteral("limit"), QStringLiteral("5"));
    query.addQueryItem(QStringLiteral("fields"),
        QStringLiteral("key,title,author_name,first_publish_year,isbn,cover_i,number_of_pages_median,publisher,subject"));
    url.setQuery(query);
    return url;
}

void OpenLibraryClient::lookupIsbn(const std::string& isbn13, std::function<void(LookupResult)> done)
{
    queue_.enqueue(booksApiUrl(isbn13), [this, isbn13, done](const HttpReply& reply) {
        if (reply.status != 200) {
            done({{}, httpProblem(reply).toStdString()});
            return;
        }
        auto candidate = openlibrary::parseBooksApi(reply.body, isbn13);
        if (!candidate) {
            done({}); // not found
            return;
        }
        // The edition record names the work; the work may hold the synopsis.
        queue_.enqueue(recordUrl(candidate->providerKey), [this, candidate = *candidate, done](const HttpReply& reply) mutable {
            if (reply.status == 200) {
                if (const auto edition = openlibrary::parseEdition(reply.body)) {
                    candidate.workKey = edition->workKey;
                    candidate.description = edition->description;
                }
            }
            if (candidate.description || !candidate.workKey) {
                done({{candidate}, std::nullopt});
                return;
            }
            description(*candidate.workKey, [candidate, done](std::optional<std::string> text, std::optional<std::string>) mutable {
                candidate.description = std::move(text);
                done({{candidate}, std::nullopt});
            });
        });
    });
}

void OpenLibraryClient::search(const std::string& title, const std::optional<std::string>& author,
    std::function<void(LookupResult)> done)
{
    queue_.enqueue(searchUrl(title, author), [done](const HttpReply& reply) {
        if (reply.status != 200) {
            done({{}, httpProblem(reply).toStdString()});
            return;
        }
        done({openlibrary::parseSearch(reply.body), std::nullopt});
    });
}

void OpenLibraryClient::description(const std::string& workKey,
    std::function<void(std::optional<std::string>, std::optional<std::string>)> done)
{
    queue_.enqueue(recordUrl(workKey), [done](const HttpReply& reply) {
        if (reply.status != 200) {
            done(std::nullopt, httpProblem(reply).toStdString());
            return;
        }
        done(openlibrary::parseWorkDescription(reply.body), std::nullopt);
    });
}

} // namespace pinax::metadata
