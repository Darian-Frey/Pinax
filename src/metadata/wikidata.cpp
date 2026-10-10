#include "metadata/wikidata.h"

#include "domain/name_match.h"
#include "metadata/request_queue.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>

namespace pinax::metadata {

using domain::FoundVolume;
using domain::SeriesFind;

namespace {

const QString endpoint = QStringLiteral("https://query.wikidata.org/sparql");

// Books, by what Wikidata says they are: a literary work, novel, written
// work, book, novella, short story collection or short story. Asked outright,
// since the subclass path is too slow beside the search; TV seasons and films
// "in" a series are left out by it.
const char* const query = R"(SELECT DISTINCT ?series ?seriesLabel ?book ?bookLabel ?ordinal ?authorLabel ?published WHERE {
  SERVICE wikibase:mwapi {
    bd:serviceParam wikibase:endpoint "www.wikidata.org"; wikibase:api "EntitySearch";
                    mwapi:search "%1"; mwapi:language "en"; mwapi:limit "20" .
    ?series wikibase:apiOutputItem mwapi:item .
  }
  ?book p:P179 ?membership . ?membership ps:P179 ?series .
  ?book wdt:P31 ?kind . VALUES ?kind { wd:Q7725634 wd:Q8261 wd:Q47461344 wd:Q571 wd:Q149537 wd:Q1279564 wd:Q49084 }
  OPTIONAL { ?membership pq:P1545 ?ordinal }
  OPTIONAL { ?book wdt:P50 ?author }
  OPTIONAL { ?book wdt:P577 ?published }
  SERVICE wikibase:label { bd:serviceParam wikibase:language "en" . }
})";

QString value(const QJsonObject& binding, const char* name)
{
    return binding.value(QLatin1String(name)).toObject().value(QStringLiteral("value")).toString();
}

// "The Culture", "Culture series" and "culture" are one name.
QString plainName(QString name)
{
    name = name.toLower().trimmed();
    if (name.startsWith(QStringLiteral("the ")))
        name.remove(0, 4);
    if (name.endsWith(QStringLiteral(" series")))
        name.chop(7);
    return name.trimmed();
}

// An item with no English label comes back labelled with its id.
bool isBareId(const QString& label)
{
    static const QRegularExpression id(QStringLiteral("^Q\\d+$"));
    return id.match(label).hasMatch();
}

std::optional<double> ordinalNumber(const std::optional<std::string>& ordinal)
{
    if (!ordinal)
        return std::nullopt;
    bool ok = false;
    const double number = QString::fromStdString(*ordinal).toDouble(&ok);
    return ok && std::isfinite(number) ? std::optional(number) : std::nullopt;
}

struct Found {
    QString label;
    std::map<QString, FoundVolume> books; // by item
};

} // namespace

WikidataClient::WikidataClient(RequestQueue& queue)
    : queue_(queue)
{
}

QUrl WikidataClient::seriesUrl(const std::string& name)
{
    QString search = QString::fromStdString(name);
    search.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));
    QUrl url(endpoint);
    url.setQuery(QStringLiteral("query=") + QString::fromLatin1(QUrl::toPercentEncoding(QString::fromUtf8(query).arg(search)))
            + QStringLiteral("&format=json"),
        QUrl::StrictMode);
    return url;
}

void WikidataClient::findSeries(const std::string& name, const std::vector<std::string>& credits,
    std::function<void(SeriesFind)> done)
{
    ask(name, name, credits, std::move(done));
}

void WikidataClient::ask(const std::string& name, const std::string& asked, const std::vector<std::string>& credits,
    std::function<void(SeriesFind)> done)
{
    queue_.enqueue(seriesUrl(asked), [this, name, asked, credits, done](const HttpReply& reply) {
        SeriesFind result;
        if (reply.status == 200) {
            result = wikidata::parseSeries(reply.body, asked, credits);
            const auto colon = asked.find(": ");
            if (result.volumes.empty() && colon != std::string::npos) {
                ask(name, asked.substr(colon + 2), credits, done);
                return;
            }
        } else if (reply.status == 0) {
            result.error = "Wikidata could not be reached: " + reply.error.toStdString();
        } else {
            result.error = "Wikidata answered " + std::to_string(reply.status);
        }
        result.provider = "Wikidata";
        done(std::move(result));
    });
}

namespace wikidata {

SeriesFind parseSeries(const QByteArray& json, const std::string& name, const std::vector<std::string>& credits)
{
    SeriesFind result;
    result.provider = "Wikidata";
    const QJsonArray bindings = QJsonDocument::fromJson(json)
                                    .object()
                                    .value(QStringLiteral("results"))
                                    .toObject()
                                    .value(QStringLiteral("bindings"))
                                    .toArray();

    std::map<QString, Found> found; // by series item
    for (const auto& entry : bindings) {
        const QJsonObject binding = entry.toObject();
        const QString title = value(binding, "bookLabel");
        if (title.isEmpty() || isBareId(title))
            continue;
        Found& series = found[value(binding, "series")];
        series.label = value(binding, "seriesLabel");
        FoundVolume& volume = series.books[value(binding, "book")];
        volume.title = title.toStdString();
        if (const QString ordinal = value(binding, "ordinal"); !ordinal.isEmpty())
            volume.ordinal = ordinal.trimmed().toStdString();
        if (const QString author = value(binding, "authorLabel"); !author.isEmpty() && !isBareId(author)) {
            const std::string text = author.toStdString();
            if (std::find(volume.authors.begin(), volume.authors.end(), text) == volume.authors.end())
                volume.authors.push_back(text);
        }
        // The earliest date given is first publication.
        const int year = value(binding, "published").left(4).toInt();
        if (year > 0 && (!volume.year || year < *volume.year))
            volume.year = year;
    }

    const QString wanted = plainName(QString::fromStdString(name));
    const Found* best = nullptr;
    std::tuple<bool, bool, std::size_t> bestScore {};
    for (const auto& [item, series] : found) {
        std::vector<std::string> authors;
        for (const auto& [book, volume] : series.books)
            authors.insert(authors.end(), volume.authors.begin(), volume.authors.end());
        const bool sharesAuthor = domain::shareAnAuthor(credits, authors);
        if (!credits.empty() && !sharesAuthor)
            continue; // another series of the same name
        const std::tuple<bool, bool, std::size_t> score {
            sharesAuthor, plainName(series.label) == wanted, series.books.size()};
        if (!best || score > bestScore) {
            best = &series;
            bestScore = score;
        }
    }
    if (!best)
        return result;

    result.seriesName = best->label.toStdString();
    for (const auto& [book, volume] : best->books)
        result.volumes.push_back(volume);
    std::stable_sort(result.volumes.begin(), result.volumes.end(), [](const FoundVolume& a, const FoundVolume& b) {
        const auto na = ordinalNumber(a.ordinal);
        const auto nb = ordinalNumber(b.ordinal);
        if (na.has_value() != nb.has_value())
            return na.has_value();
        if (na && *na != *nb)
            return *na < *nb;
        return a.year.value_or(9999) < b.year.value_or(9999);
    });
    return result;
}

} // namespace wikidata

} // namespace pinax::metadata
