#include "metadata/british_library.h"

#include "domain/isbn.h"
#include "metadata/request_queue.h"

#include <QRegularExpression>
#include <QUrlQuery>
#include <QXmlStreamReader>

#include <algorithm>
#include <map>

namespace pinax::metadata {

using domain::Candidate;
using domain::LookupResult;

namespace {

const QString sruBase = QStringLiteral("https://bl.alma.exlibrisgroup.com/view/sru/44BL_MAIN");

// One MARC data field: its tag, second indicator and subfields in order.
struct Field {
    QString tag;
    QChar ind2;
    std::vector<std::pair<QChar, QString>> subfields;

    QString first(QChar code) const
    {
        for (const auto& [subfieldCode, value] : subfields) {
            if (subfieldCode == code)
                return value;
        }
        return {};
    }
};

struct Record {
    std::map<QString, QString> control; // tag -> value
    std::vector<Field> fields;

    std::vector<const Field*> all(const QString& tag) const
    {
        std::vector<const Field*> found;
        for (const auto& field : fields) {
            if (field.tag == tag)
                found.push_back(&field);
        }
        return found;
    }
    const Field* first(const QString& tag) const
    {
        const auto found = all(tag);
        return found.empty() ? nullptr : found.front();
    }
};

std::vector<Record> readRecords(const QByteArray& xml)
{
    std::vector<Record> records;
    QXmlStreamReader reader(xml);
    Record* record = nullptr;
    Field* field = nullptr;
    QString controlTag;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const auto name = reader.name();
            const auto attributes = reader.attributes();
            if (name == QLatin1String("record")
                && reader.namespaceUri() == QLatin1String("http://www.loc.gov/MARC21/slim")) {
                records.emplace_back();
                record = &records.back();
            } else if (record && name == QLatin1String("controlfield")) {
                record->control[attributes.value(QLatin1String("tag")).toString()] = reader.readElementText();
            } else if (record && name == QLatin1String("datafield")) {
                const QString ind2 = attributes.value(QLatin1String("ind2")).toString();
                record->fields.push_back({attributes.value(QLatin1String("tag")).toString(),
                    ind2.isEmpty() ? QChar(' ') : ind2.front(), {}});
                field = &record->fields.back();
            } else if (field && name == QLatin1String("subfield")) {
                const QString code = attributes.value(QLatin1String("code")).toString();
                const QString value = reader.readElementText();
                if (!code.isEmpty())
                    field->subfields.emplace_back(code.front(), value);
            }
        } else if (reader.isEndElement()) {
            if (reader.name() == QLatin1String("datafield"))
                field = nullptr;
            else if (reader.name() == QLatin1String("record")
                && reader.namespaceUri() == QLatin1String("http://www.loc.gov/MARC21/slim"))
                record = nullptr;
        }
    }
    return records;
}

// MARC ends a subfield with the punctuation that would join it to the next:
// "Titan /", "Harper Voyager,", "Science fiction.". Removes it and spaces.
QString trimmed(QString value)
{
    static const QRegularExpression trailing(QStringLiteral("[\\s/:;,=]+$"));
    value = value.trimmed();
    value.remove(trailing);
    return value.trimmed();
}

// As trimmed, and a closing full stop too, unless it ends an initial or an
// abbreviation ("Iain M.", "Co."): kept when the word before it is short.
QString withoutFullStop(QString value)
{
    value = trimmed(value);
    static const QRegularExpression shortLast(QStringLiteral("(^|\\s)\\S{1,3}\\.$"));
    if (value.endsWith(QLatin1Char('.')) && !shortLast.match(value).hasMatch())
        value.chop(1);
    return value.trimmed();
}

std::optional<std::string> text(const QString& value)
{
    if (value.isEmpty())
        return std::nullopt;
    return value.toStdString();
}

// "Baxter, Stephen," -> "Stephen Baxter"; a name without a comma stays.
std::string directOrder(const QString& heading)
{
    const QString name = trimmed(heading);
    const auto comma = name.indexOf(QLatin1Char(','));
    if (comma < 0)
        return name.toStdString();
    return (name.mid(comma + 1).trimmed() + QLatin1Char(' ') + name.left(comma).trimmed()).toStdString();
}

// The ISBNs a record gives in 020 $a, as ISBN-13: "0575078014 (pbk.) :".
std::vector<std::string> isbnsOf(const Record& record)
{
    std::vector<std::string> isbns;
    static const QRegularExpression leading(QStringLiteral("^[0-9Xx-]+"));
    for (const Field* field : record.all(QStringLiteral("020"))) {
        const auto match = leading.match(field->first(QLatin1Char('a')).trimmed());
        if (!match.hasMatch())
            continue;
        const std::string isbn = domain::normaliseIsbn(match.captured(0).toUpper().toStdString());
        if (domain::isValidIsbn13(isbn))
            isbns.push_back(isbn);
        else if (domain::isValidIsbn10(isbn))
            isbns.push_back(domain::isbn10To13(isbn));
    }
    return isbns;
}

std::optional<int> yearAt(const QString& fixed, int from)
{
    if (fixed.size() < from + 4)
        return std::nullopt;
    bool ok = false;
    const int year = fixed.mid(from, 4).toInt(&ok);
    return ok && year >= 1450 && year <= 2199 ? std::optional(year) : std::nullopt;
}

Candidate toCandidate(const Record& record, const std::string& isbn13)
{
    Candidate candidate;
    candidate.source = domain::Source::BritishLibrary;
    if (auto id = record.control.find(QStringLiteral("001")); id != record.control.end())
        candidate.providerKey = id->second.toStdString();
    candidate.isbn13 = isbn13;

    if (const Field* title = record.first(QStringLiteral("245"))) {
        candidate.title = trimmed(title->first(QLatin1Char('a'))).toStdString();
        candidate.subtitle = text(trimmed(title->first(QLatin1Char('b'))));
    }

    for (const QString& tag : {QStringLiteral("100"), QStringLiteral("700")}) {
        for (const Field* name : record.all(tag)) {
            const QString role = name->first(QLatin1Char('e'));
            if (role.isEmpty() || role.startsWith(QLatin1String("author")))
                candidate.authors.push_back(directOrder(name->first(QLatin1Char('a'))));
        }
    }

    // Publication: 264 with second indicator 1, else the older 260.
    const Field* publication = nullptr;
    for (const Field* field : record.all(QStringLiteral("264"))) {
        if (field->ind2 == QLatin1Char('1')) {
            publication = field;
            break;
        }
    }
    if (!publication)
        publication = record.first(QStringLiteral("260"));
    if (publication)
        candidate.publisher = text(trimmed(publication->first(QLatin1Char('b'))));

    // 008: date type at 6, first date at 7, second at 11. A reprint ('r')
    // gives the reprint's year and then the original's.
    if (auto fixed = record.control.find(QStringLiteral("008")); fixed != record.control.end()) {
        const QString& value = fixed->second;
        candidate.publishedYear = yearAt(value, 7);
        if (value.size() > 6 && value.at(6) == QLatin1Char('r'))
            candidate.firstPublishedYear = yearAt(value, 11);
    }

    // 300 $a: "580 pages ;", "viii, 591 pages :", "332 p. ;".
    if (const Field* extent = record.first(QStringLiteral("300"))) {
        static const QRegularExpression pages(QStringLiteral("(\\d+)\\s*(pages|p\\.)"));
        const auto match = pages.match(extent->first(QLatin1Char('a')));
        if (match.hasMatch())
            candidate.pageCount = match.captured(1).toInt();
    }

    // 490: the series statement, "A time odyssey ;" with the number in $v.
    if (const Field* series = record.first(QStringLiteral("490"))) {
        candidate.seriesName = text(trimmed(series->first(QLatin1Char('a'))));
        candidate.seriesNumber = text(trimmed(series->first(QLatin1Char('v'))));
    }

    for (const QString& tag : {QStringLiteral("650"), QStringLiteral("655")}) {
        for (const Field* subject : record.all(tag)) {
            const std::string name = withoutFullStop(subject->first(QLatin1Char('a'))).toStdString();
            if (!name.empty()
                && std::find(candidate.categories.begin(), candidate.categories.end(), name)
                    == candidate.categories.end())
                candidate.categories.push_back(name);
        }
    }
    return candidate;
}

} // namespace

namespace britishlibrary {

std::vector<Candidate> parseSru(const QByteArray& xml, const std::string& isbn13)
{
    std::vector<Candidate> candidates;
    for (const Record& record : readRecords(xml)) {
        const auto isbns = isbnsOf(record);
        if (std::find(isbns.begin(), isbns.end(), isbn13) == isbns.end())
            continue;
        candidates.push_back(toCandidate(record, isbn13));
    }
    return candidates;
}

std::optional<std::string> parseDiagnostic(const QByteArray& xml)
{
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QLatin1String("message"))
            return text(reader.readElementText().trimmed());
    }
    return std::nullopt;
}

} // namespace britishlibrary

BritishLibraryClient::BritishLibraryClient(RequestQueue& queue)
    : queue_(queue)
{
}

QUrl BritishLibraryClient::isbnUrl(const std::string& isbn13)
{
    QUrl url(sruBase);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("version"), QStringLiteral("1.2"));
    query.addQueryItem(QStringLiteral("operation"), QStringLiteral("searchRetrieve"));
    query.addQueryItem(QStringLiteral("recordSchema"), QStringLiteral("marcxml"));
    query.addQueryItem(QStringLiteral("maximumRecords"), QStringLiteral("5"));
    query.addQueryItem(QStringLiteral("query"), QStringLiteral("alma.isbn=%1").arg(QString::fromStdString(isbn13)));
    url.setQuery(query);
    return url;
}

void BritishLibraryClient::lookupIsbn(const std::string& isbn13, std::function<void(LookupResult)> done)
{
    queue_.enqueue(isbnUrl(isbn13), [isbn13, done](const HttpReply& reply) {
        LookupResult result;
        if (reply.status == 200) {
            result.candidates = britishlibrary::parseSru(reply.body, isbn13);
            if (result.candidates.empty()) {
                if (auto diagnostic = britishlibrary::parseDiagnostic(reply.body))
                    result.error = "The British Library answered: " + *diagnostic;
            }
        } else if (reply.status == 0) {
            result.error = "The British Library could not be reached: " + reply.error.toStdString();
        } else {
            result.error = "The British Library answered " + std::to_string(reply.status);
        }
        done(std::move(result));
    });
}

} // namespace pinax::metadata
