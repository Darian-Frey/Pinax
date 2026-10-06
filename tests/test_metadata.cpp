#include "metadata/google_books.h"
#include "metadata/http.h"
#include "metadata/open_library.h"
#include "metadata/request_queue.h"

#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTest>
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

// Serves scripted replies per URL, asynchronously as the network would, and
// records what was asked for. Never touches the network (D-020).
class FakeFetcher : public Fetcher {
public:
    void script(const QUrl& url, std::vector<HttpReply> replies) { scripts_[url.toString()] = std::move(replies); }

    void get(const QUrl& url, std::function<void(const HttpReply&)> done) override
    {
        requested.push_back(url.toString());
        times.push_back(clock.elapsed());
        HttpReply reply {404, {}, {}, std::nullopt};
        auto& replies = scripts_[url.toString()];
        if (!replies.empty()) {
            reply = replies.front();
            if (replies.size() > 1)
                replies.erase(replies.begin());
        }
        QTimer::singleShot(0, [reply, done] { done(reply); });
    }

    std::vector<QString> requested;
    std::vector<qint64> times;
    QElapsedTimer clock;

private:
    std::map<QString, std::vector<HttpReply>> scripts_;
};

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
    const auto candidates = googlebooks::parseVolumes(fixture("google_books/volumes_documented_shape.json"));
    QCOMPARE(candidates.size(), std::size_t(1));
    const Candidate& volume = candidates.front();
    QVERIFY(volume.source == pinax::domain::Source::GoogleBooks);
    QCOMPARE(volume.subtitle, std::optional<std::string>("A Culture Novel"));
    QCOMPARE(volume.publishedYear, std::optional<int>(2008));
    QCOMPARE(volume.isbn13, std::optional<std::string>("9780316005388"));
    QCOMPARE(volume.categories, std::vector<std::string>({"Fiction / Science Fiction / Space Opera"}));
    QVERIFY(volume.coverUrl->rfind("https://", 0) == 0);

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

QTEST_MAIN(TestMetadata)
#include "test_metadata.moc"
