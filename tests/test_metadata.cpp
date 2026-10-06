#include "metadata/british_library.h"
#include "metadata/cover_cache.h"
#include "metadata/google_books.h"
#include "metadata/http.h"
#include "metadata/open_library.h"
#include "metadata/request_queue.h"
#include "domain/isbn.h"
#include "fake_fetcher.h"

#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>
#include <QTimer>

#include <map>
#include <vector>

using namespace pinax::metadata;
using pinax::domain::Candidate;
using pinax::domain::LookupResult;

namespace {

QByteArray fixture(const QString& name)
{
    QFile file(QStringLiteral(PINAX_TEST_FIXTURES "/") + name);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", qPrintable(name));
    return file.readAll();
}

HttpReply ok(const QByteArray& body)
{
    return {200, body, {}, std::nullopt};
}

QueuePolicy fast()
{
    QueuePolicy policy;
    policy.minInterval = std::chrono::milliseconds(0);
    policy.firstBackoff = std::chrono::milliseconds(10);
    policy.maxBackoff = std::chrono::milliseconds(40);
    policy.maxAttempts = 3;
    return policy;
}

} // namespace

class TestMetadata : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { qRegisterMetaType<LookupResult>(); }

    // Open Library parsing, against recorded responses
    void booksApiReadsTheEdition();
    void anUnknownIsbnIsNotFound();
    void descriptionsComeInBothShapes();
    void searchYieldsWorks();

    // Open Library client
    void isbnLookupFollowsEditionToWork();
    void isbnLookupStopsAtTheEditionWhenItHasASynopsis();
    void urlsAreWellFormed();

    // Request queue (AV-009)
    void requestsAreSpacedApart();
    void aQuotaReplyPausesAndRetries();
    void persistentTroubleIsGivenUpOn();
    void aMissingRecordIsNotRetried();
    void cancellingDropsWhatIsQueued();

    // Google Books (D-019)
    void googleVolumesParse();
    void googleWithoutAKeySendsNothing();

    // Cover cache (F-013)
    void aCoverIsDownloadedOnceAndKept();
    void openLibraryIsAskedForAPlain404();
    void aMissingCoverIsReportedNotWritten();
    void anythingButAnImageIsRefused();
    void forgettingRemovesTheFile();

    // British Library (D-022)
    void britishLibraryReadsAReprint();
    void britishLibraryMatchesAnIsbn10Record();
    void britishLibraryDropsRelatedEditions();
    void britishLibraryLookupGoesThroughTheQueue();
};

void TestMetadata::booksApiReadsTheEdition()
{
    const auto candidate = openlibrary::parseBooksApi(fixture("open_library/isbn_9780316005388.json"), "9780316005388");
    QVERIFY(candidate);
    QCOMPARE(candidate->title, std::string("Consider Phlebas"));
    QCOMPARE(candidate->authors, std::vector<std::string>({"Iain Banks"}));
    QCOMPARE(candidate->publisher, std::optional<std::string>("Orbit"));
    QCOMPARE(candidate->publishedYear, std::optional<int>(2008)); // from "March 26, 2008"
    QCOMPARE(candidate->pageCount, std::optional<int>(480));
    QCOMPARE(candidate->isbn13, std::optional<std::string>("9780316005388"));
    QCOMPARE(candidate->isbn10, std::optional<std::string>("031600538X"));
    QCOMPARE(candidate->providerKey, std::string("/books/OL9759601M"));
    QCOMPARE(candidate->coverUrl, std::optional<std::string>("https://covers.openlibrary.org/b/id/1174792-L.jpg"));
    QVERIFY(std::find(candidate->categories.begin(), candidate->categories.end(), "Science Fiction")
        != candidate->categories.end());
}

void TestMetadata::anUnknownIsbnIsNotFound()
{
    QVERIFY(!openlibrary::parseBooksApi(fixture("open_library/isbn_unknown.json"), "9790000000001"));
    QVERIFY(!openlibrary::parseBooksApi("not json", "9790000000001"));
}

void TestMetadata::descriptionsComeInBothShapes()
{
    const auto edition = openlibrary::parseEdition(fixture("open_library/edition_9780316005388.json"));
    QVERIFY(edition);
    QCOMPARE(edition->workKey, std::optional<std::string>("/works/OL8368432W"));
    QVERIFY(edition->description && edition->description->rfind("Overview: The war raged", 0) == 0);

    const auto work = openlibrary::parseWorkDescription(fixture("open_library/work_OL8368432W.json"));
    QVERIFY(work && work->rfind("Consider Phlebas is perhaps", 0) == 0);
}

void TestMetadata::searchYieldsWorks()
{
    const auto candidates = openlibrary::parseSearch(fixture("open_library/search_consider_phlebas.json"));
    QCOMPARE(candidates.size(), std::size_t(1));
    const Candidate& work = candidates.front();
    QCOMPARE(work.workKey, std::optional<std::string>("/works/OL8368432W"));
    QCOMPARE(work.firstPublishedYear, std::optional<int>(1987));
    QCOMPARE(work.pageCount, std::optional<int>(471));
    QCOMPARE(work.coverUrl, std::optional<std::string>("https://covers.openlibrary.org/b/id/1009644-L.jpg"));
    QVERIFY(!work.description); // search never carries one
}

void TestMetadata::isbnLookupFollowsEditionToWork()
{
    // An edition with no synopsis: the client goes on to the work.
    FakeFetcher fetcher;
    fetcher.clock.start();
    RequestQueue queue(fetcher, fast());
    OpenLibraryClient client(queue);
    fetcher.script(OpenLibraryClient::booksApiUrl("9780316005388"), {ok(fixture("open_library/isbn_9780316005388.json"))});
    fetcher.script(OpenLibraryClient::recordUrl("/books/OL9759601M"), {ok(R"({"works":[{"key":"/works/OL8368432W"}]})")});
    fetcher.script(OpenLibraryClient::recordUrl("/works/OL8368432W"), {ok(fixture("open_library/work_OL8368432W.json"))});

    std::optional<LookupResult> result;
    client.lookupIsbn("9780316005388", [&](LookupResult r) { result = std::move(r); });
    QTRY_VERIFY(result);

    QCOMPARE(fetcher.requested.size(), std::size_t(3));
    QVERIFY(!result->error);
    QCOMPARE(result->candidates.size(), std::size_t(1));
    QCOMPARE(result->candidates.front().workKey, std::optional<std::string>("/works/OL8368432W"));
    QVERIFY(result->candidates.front().description->rfind("Consider Phlebas is perhaps", 0) == 0);
}

void TestMetadata::isbnLookupStopsAtTheEditionWhenItHasASynopsis()
{
    FakeFetcher fetcher;
    RequestQueue queue(fetcher, fast());
    OpenLibraryClient client(queue);
    fetcher.script(OpenLibraryClient::booksApiUrl("9780316005388"), {ok(fixture("open_library/isbn_9780316005388.json"))});
    fetcher.script(OpenLibraryClient::recordUrl("/books/OL9759601M"), {ok(fixture("open_library/edition_9780316005388.json"))});

    std::optional<LookupResult> result;
    client.lookupIsbn("9780316005388", [&](LookupResult r) { result = std::move(r); });
    QTRY_VERIFY(result);
    QCOMPARE(fetcher.requested.size(), std::size_t(2));
    QVERIFY(result->candidates.front().description->rfind("Overview:", 0) == 0);

    // Not found is an empty answer, not an error.
    std::optional<LookupResult> none;
    fetcher.script(OpenLibraryClient::booksApiUrl("9790000000001"), {ok("{}")});
    client.lookupIsbn("9790000000001", [&](LookupResult r) { none = std::move(r); });
    QTRY_VERIFY(none);
    QVERIFY(none->candidates.empty());
    QVERIFY(!none->error);
}

void TestMetadata::urlsAreWellFormed()
{
    QCOMPARE(OpenLibraryClient::booksApiUrl("9780316005388").toString(),
        QStringLiteral("https://openlibrary.org/api/books?bibkeys=ISBN:9780316005388&format=json&jscmd=data"));
    QCOMPARE(OpenLibraryClient::recordUrl("/works/OL8368432W").toString(),
        QStringLiteral("https://openlibrary.org/works/OL8368432W.json"));
    const QString search = OpenLibraryClient::searchUrl("The Mote in God's Eye", std::string("Niven")).toString(QUrl::FullyEncoded);
    QVERIFY2(search.startsWith(QStringLiteral("https://openlibrary.org/search.json?title=The%20Mote%20in%20God's%20Eye&author=Niven&limit=5")),
        qPrintable(search));
}

void TestMetadata::requestsAreSpacedApart()
{
    FakeFetcher fetcher;
    fetcher.clock.start();
    QueuePolicy policy = fast();
    policy.minInterval = std::chrono::milliseconds(60);
    RequestQueue queue(fetcher, policy);

    int answered = 0;
    for (int i = 0; i < 3; ++i)
        queue.enqueue(QUrl(QStringLiteral("https://example.invalid/%1").arg(i)), [&](const HttpReply&) { ++answered; });
    QTRY_COMPARE(answered, 3);
    QCOMPARE(fetcher.times.size(), std::size_t(3));
    QVERIFY2(fetcher.times[1] - fetcher.times[0] >= 55, qPrintable(QString::number(fetcher.times[1] - fetcher.times[0])));
    QVERIFY(fetcher.times[2] - fetcher.times[1] >= 55);
}

void TestMetadata::aQuotaReplyPausesAndRetries()
{
    FakeFetcher fetcher;
    RequestQueue queue(fetcher, fast());
    QSignalSpy paused(&queue, &RequestQueue::paused);
    QSignalSpy resumed(&queue, &RequestQueue::resumed);
    const QUrl url(QStringLiteral("https://example.invalid/limited"));
    fetcher.script(url, {{429, fixture("google_books/quota_exceeded_keyless.json"), {}, std::nullopt}, ok("{}")});

    std::optional<HttpReply> reply;
    queue.enqueue(url, [&](const HttpReply& r) { reply = r; });
    QTRY_VERIFY(reply);
    QCOMPARE(reply->status, 200);
    QCOMPARE(fetcher.requested.size(), std::size_t(2));
    QCOMPARE(paused.count(), 1);
    QCOMPARE(resumed.count(), 1);
}

void TestMetadata::persistentTroubleIsGivenUpOn()
{
    FakeFetcher fetcher;
    RequestQueue queue(fetcher, fast());
    const QUrl url(QStringLiteral("https://example.invalid/down"));
    fetcher.script(url, {{503, {}, {}, std::nullopt}});

    std::optional<HttpReply> reply;
    queue.enqueue(url, [&](const HttpReply& r) { reply = r; });
    QTRY_VERIFY(reply);
    QCOMPARE(reply->status, 503);
    QCOMPARE(fetcher.requested.size(), std::size_t(3)); // maxAttempts
}

void TestMetadata::aMissingRecordIsNotRetried()
{
    FakeFetcher fetcher;
    RequestQueue queue(fetcher, fast());
    std::optional<HttpReply> reply;
    queue.enqueue(QUrl(QStringLiteral("https://example.invalid/nothing")), [&](const HttpReply& r) { reply = r; });
    QTRY_VERIFY(reply);
    QCOMPARE(reply->status, 404);
    QCOMPARE(fetcher.requested.size(), std::size_t(1));
}

void TestMetadata::cancellingDropsWhatIsQueued()
{
    FakeFetcher fetcher;
    QueuePolicy policy = fast();
    policy.minInterval = std::chrono::milliseconds(50);
    RequestQueue queue(fetcher, policy);
    int answered = 0;
    for (int i = 0; i < 3; ++i)
        queue.enqueue(QUrl(QStringLiteral("https://example.invalid/%1").arg(i)), [&](const HttpReply&) { ++answered; });
    queue.cancelAll();
    QTest::qWait(150);
    QCOMPARE(answered, 0);
    QCOMPARE(queue.pending(), 0);
    QVERIFY(fetcher.requested.size() <= 1);
}

void TestMetadata::googleVolumesParse()
{
    // Recorded with a key: two editions of one book, and a book about it.
    const auto candidates = googlebooks::parseVolumes(fixture("google_books/freetext_consider_phlebas.json"));
    QCOMPARE(candidates.size(), std::size_t(3));
    const Candidate& volume = candidates.front();
    QVERIFY(volume.source == pinax::domain::Source::GoogleBooks);
    QCOMPARE(volume.title, std::string("Consider Phlebas"));
    QCOMPARE(volume.authors, std::vector<std::string>({"Iain M. Banks"}));
    QCOMPARE(volume.publishedYear, std::optional<int>(2009));
    QCOMPARE(volume.pageCount, std::optional<int>(516));
    QVERIFY(volume.isbn13 && pinax::domain::isValidIsbn13(*volume.isbn13));
    QCOMPARE(volume.categories, std::vector<std::string>({"Fiction"}));
    QVERIFY(volume.coverUrl->rfind("https://", 0) == 0); // asked for over https
    QCOMPARE(candidates[1].publishedYear, std::optional<int>(1987)); // the other edition (AV-010)
    QVERIFY(!candidates[1].coverUrl);

    QVERIFY(googlebooks::parseVolumes(fixture("google_books/isbn_no_match.json")).empty());

    const auto quota = googlebooks::parseError(fixture("google_books/quota_exceeded_keyless.json"));
    QVERIFY(quota && quota->find("Quota exceeded") != std::string::npos);
}

void TestMetadata::googleWithoutAKeySendsNothing()
{
    FakeFetcher fetcher;
    RequestQueue queue(fetcher, fast());
    GoogleBooksClient client(queue, QString());
    QVERIFY(!client.available());

    std::optional<LookupResult> result;
    client.lookupIsbn("9780316005388", [&](LookupResult r) { result = std::move(r); });
    QVERIFY(result);
    QVERIFY(result->error);
    QVERIFY(fetcher.requested.empty());

    GoogleBooksClient keyed(queue, QStringLiteral("test-key"));
    QVERIFY(keyed.isbnUrl("9780316005388").toString().contains(QStringLiteral("q=isbn:9780316005388&key=test-key")));
}

namespace {

// Synthetic images: the right signature, padded past the placeholder size.
// No real cover art is kept in the repository.
QByteArray jpeg(int size = 4096)
{
    QByteArray body("\xFF\xD8\xFF\xE0", 4);
    body.append(QByteArray(size - body.size(), 'j'));
    return body;
}

QByteArray png(int size = 4096)
{
    QByteArray body("\x89PNG\r\n\x1A\n", 8);
    body.append(QByteArray(size - body.size(), 'p'));
    return body;
}

const QUrl coverUrl(QStringLiteral("https://covers.openlibrary.org/b/id/1009644-L.jpg"));

} // namespace

void TestMetadata::aCoverIsDownloadedOnceAndKept()
{
    QTemporaryDir dir;
    FakeFetcher fetcher;
    RequestQueue queue(fetcher, fast());
    CoverCache cache(queue, dir.path());
    fetcher.script(CoverCache::politeUrl(coverUrl), {ok(jpeg())});

    std::optional<CoverResult> first;
    cache.fetch(12, coverUrl, [&](CoverResult r) { first = r; });
    QTRY_VERIFY(first);
    QVERIFY2(!first->error, first->error.value_or("").c_str());
    QCOMPARE(first->relativePath, std::optional<std::string>("covers/12.jpg"));
    QVERIFY(first->downloaded);
    QVERIFY(QFile::exists(dir.filePath(QStringLiteral("covers/12.jpg"))));

    // Asked again: from disk, not the network (F-013).
    std::optional<CoverResult> second;
    cache.fetch(12, coverUrl, [&](CoverResult r) { second = r; });
    QVERIFY(second);
    QVERIFY(!second->downloaded);
    QCOMPARE(fetcher.requested.size(), std::size_t(1));
    QCOMPARE(cache.cached(12), std::optional<std::string>("covers/12.jpg"));

    // A PNG keeps its own extension.
    fetcher.script(CoverCache::politeUrl(QUrl(QStringLiteral("https://covers.openlibrary.org/b/id/2-L.jpg"))), {ok(png())});
    std::optional<CoverResult> third;
    cache.fetch(13, QUrl(QStringLiteral("https://covers.openlibrary.org/b/id/2-L.jpg")), [&](CoverResult r) { third = r; });
    QTRY_VERIFY(third);
    QCOMPARE(third->relativePath, std::optional<std::string>("covers/13.png"));
}

void TestMetadata::openLibraryIsAskedForAPlain404()
{
    QCOMPARE(CoverCache::politeUrl(coverUrl).toString(),
        QStringLiteral("https://covers.openlibrary.org/b/id/1009644-L.jpg?default=false"));
    const QUrl google(QStringLiteral("https://books.google.com/books/content?id=x&img=1"));
    QCOMPARE(CoverCache::politeUrl(google), google);
}

void TestMetadata::aMissingCoverIsReportedNotWritten()
{
    QTemporaryDir dir;
    FakeFetcher fetcher; // unscripted: 404
    RequestQueue queue(fetcher, fast());
    CoverCache cache(queue, dir.path());

    std::optional<CoverResult> result;
    cache.fetch(7, coverUrl, [&](CoverResult r) { result = r; });
    QTRY_VERIFY(result);
    QCOMPARE(result->error, std::optional<std::string>("no cover"));
    QVERIFY(!result->relativePath);
    QVERIFY(!cache.cached(7));
}

void TestMetadata::anythingButAnImageIsRefused()
{
    QCOMPARE(CoverCache::imageType(jpeg()), std::optional<QString>(QStringLiteral("jpg")));
    QVERIFY(!CoverCache::imageType(jpeg(43)));                        // a 1×1 placeholder
    QVERIFY(!CoverCache::imageType(QByteArray(4096, '<')));           // an HTML error page

    QTemporaryDir dir;
    FakeFetcher fetcher;
    RequestQueue queue(fetcher, fast());
    CoverCache cache(queue, dir.path());
    fetcher.script(CoverCache::politeUrl(coverUrl), {ok(QByteArray(4096, '<'))});
    std::optional<CoverResult> result;
    cache.fetch(8, coverUrl, [&](CoverResult r) { result = r; });
    QTRY_VERIFY(result);
    QVERIFY(result->error);
    QVERIFY(!cache.cached(8));
}

void TestMetadata::forgettingRemovesTheFile()
{
    QTemporaryDir dir;
    FakeFetcher fetcher;
    RequestQueue queue(fetcher, fast());
    CoverCache cache(queue, dir.path());
    fetcher.script(CoverCache::politeUrl(coverUrl), {ok(jpeg())});
    std::optional<CoverResult> result;
    cache.fetch(12, coverUrl, [&](CoverResult r) { result = r; });
    QTRY_VERIFY(result);

    cache.forget(12);
    QVERIFY(!cache.cached(12));
}

void TestMetadata::britishLibraryReadsAReprint()
{
    const auto candidates = britishlibrary::parseSru(fixture("british_library/isbn_9780356521633.xml"), "9780356521633");
    QCOMPARE(candidates.size(), std::size_t(1));
    const Candidate& book = candidates.front();
    QVERIFY(book.source == pinax::domain::Source::BritishLibrary);
    QCOMPARE(book.title, std::string("Consider Phlebas"));
    QCOMPARE(book.authors, std::vector<std::string>({"Iain Banks"}));
    QVERIFY(book.publisher == std::optional<std::string>("Orbit"));
    QVERIFY(book.publishedYear == 2023);
    QVERIFY(book.firstPublishedYear == 1987); // the reprint's original (008 type r)
    QVERIFY(book.pageCount == 480);
    QVERIFY(std::find(book.categories.begin(), book.categories.end(), "Science fiction") != book.categories.end());
    // Never a synopsis or a cover: the book's source columns could not hold them.
    QVERIFY(!book.description);
    QVERIFY(!book.coverUrl);
}

void TestMetadata::britishLibraryMatchesAnIsbn10Record()
{
    // The 1988 Futura paperback records only its ISBN-10; asked as ISBN-13.
    const auto candidates = britishlibrary::parseSru(fixture("british_library/isbn_9780708837078.xml"), "9780708837078");
    QCOMPARE(candidates.size(), std::size_t(1));
    QVERIFY(candidates.front().publishedYear == 1988);
    QVERIFY(candidates.front().firstPublishedYear == 1987);
    QVERIFY(candidates.front().publisher);
}

void TestMetadata::britishLibraryDropsRelatedEditions()
{
    // The index returns related editions too; only the ISBN asked for counts.
    QVERIFY(britishlibrary::parseSru(fixture("british_library/isbn_9780356521633.xml"), "9780708837078").empty());
    QVERIFY(britishlibrary::parseSru(fixture("british_library/isbn_9780316005388.xml"), "9780316005388").empty());
    QVERIFY(!britishlibrary::parseDiagnostic(fixture("british_library/isbn_9780316005388.xml")));
    QVERIFY(britishlibrary::parseSru("not xml", "9780316005388").empty());
}

void TestMetadata::britishLibraryLookupGoesThroughTheQueue()
{
    FakeFetcher fetcher;
    RequestQueue queue(fetcher, fast());
    BritishLibraryClient client(queue);
    const QUrl url = BritishLibraryClient::isbnUrl("9780356521633");
    QCOMPARE(url.host(), QStringLiteral("bl.alma.exlibrisgroup.com"));
    QCOMPARE(QUrlQuery(url).queryItemValue(QStringLiteral("query"), QUrl::FullyDecoded),
        QStringLiteral("alma.isbn=9780356521633"));
    fetcher.script(url, {ok(fixture("british_library/isbn_9780356521633.xml"))});

    std::optional<LookupResult> result;
    client.lookupIsbn("9780356521633", [&](LookupResult r) { result = std::move(r); });
    QTRY_VERIFY(result);
    QCOMPARE(result->candidates.size(), std::size_t(1));

    // A miss is an empty answer; a failure is an error.
    std::optional<LookupResult> miss;
    fetcher.script(BritishLibraryClient::isbnUrl("9780316005388"), {ok(fixture("british_library/isbn_9780316005388.xml"))});
    client.lookupIsbn("9780316005388", [&](LookupResult r) { miss = std::move(r); });
    QTRY_VERIFY(miss);
    QVERIFY(miss->candidates.empty() && !miss->error);
}

QTEST_MAIN(TestMetadata)
#include "test_metadata.moc"
