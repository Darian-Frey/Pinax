#include "app/catalogue.h"
#include "app/enricher.h"
#include "app/main_window.h"
#include "app/provider_key.h"
#include "fake_fetcher.h"
#include "metadata/british_library.h"
#include "metadata/cover_cache.h"
#include "metadata/google_books.h"
#include "metadata/open_library.h"
#include "ui/book_list_view.h"
#include "ui/book_view.h"
#include "ui/candidate_view.h"
#include "ui/detail_panel.h"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTest>

using namespace pinax::metadata;
using pinax::app::Catalogue;
using pinax::app::Enricher;
using pinax::app::FindResult;
using pinax::domain::BookDetail;
using pinax::domain::Candidate;
using pinax::domain::Source;
using pinax::ui::DetailPanel;

namespace {

const std::string phlebasIsbn = "9780316005388";

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
    policy.firstBackoff = std::chrono::milliseconds(5);
    policy.maxBackoff = std::chrono::milliseconds(10);
    policy.maxAttempts = 2;
    return policy;
}

BookDetail book(const std::string& title, const std::optional<std::string>& isbn13 = std::nullopt)
{
    BookDetail detail;
    detail.book.id = 1;
    detail.book.title = title;
    detail.book.isbn13 = isbn13;
    detail.credits = {{"Iain M. Banks", pinax::domain::CreditRole::Author}};
    return detail;
}

// Google's URLs depend on the key, so are made by a client holding it.
QUrl googleIsbnUrl(FakeFetcher& fetcher, const std::string& isbn)
{
    RequestQueue queue(fetcher);
    return GoogleBooksClient(queue, QStringLiteral("test-key")).isbnUrl(isbn);
}

bool askedGoogle(const FakeFetcher& fetcher)
{
    return std::any_of(fetcher.requested.begin(), fetcher.requested.end(),
        [](const QString& url) { return url.contains(QStringLiteral("googleapis")); });
}

// A real PNG of a size no placeholder has (SPEC.md §4).
QByteArray coverImage()
{
    QImage image(120, 180, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x)
            image.setPixel(x, y, QRandomGenerator::global()->generate());
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

} // namespace

class TestEnricher : public QObject {
    Q_OBJECT

private slots:
    void anIsbnKnownIsOfferedAsTheEdition();
    void anIsbnUnknownFallsBackToTitleAndAuthor();
    void googleIsAskedOnlyWithAKey();
    void providersUnreachableIsAProblemNotANotFound();
    void aSearchedCandidateIsCompletedFromItsWork();
    void cancellingDropsTheAnswer();
    void theBritishLibraryFillsOpenLibrarysGaps();
    void theBritishLibraryAnswersAlone();
    void theKeyIsFoundInOrder();

    // Through the window
    void fetchingFromThePanelWritesTheChoiceAndItsCover();
    void cancellingAFetchReturnsToTheBook();
    void nothingFoundMarksTheBookAndSaysSo();
};

void TestEnricher::anIsbnKnownIsOfferedAsTheEdition()
{
    FakeFetcher fetcher;
    Enricher enricher(fetcher, {}, fast());
    fetcher.script(OpenLibraryClient::booksApiUrl(phlebasIsbn), {ok(fixture("open_library/isbn_9780316005388.json"))});
    fetcher.script(OpenLibraryClient::recordUrl("/books/OL9759601M"), {ok(fixture("open_library/edition_9780316005388.json"))});

    std::optional<FindResult> result;
    enricher.find(book("Consider Phlebas", phlebasIsbn), [&](FindResult r) { result = std::move(r); });
    QTRY_VERIFY(result);
    QVERIFY(result->byIsbn);
    QVERIFY(!result->problem);
    QCOMPARE(result->candidates.size(), std::size_t(1));
    QCOMPARE(result->candidates.front().title, std::string("Consider Phlebas"));

    // An ISBN-10 alone is asked as its ISBN-13.
    BookDetail older = book("Consider Phlebas");
    older.book.isbn10 = "031600538X";
    result.reset();
    enricher.find(older, [&](FindResult r) { result = std::move(r); });
    QTRY_VERIFY(result);
    QVERIFY(result->byIsbn);
}

void TestEnricher::anIsbnUnknownFallsBackToTitleAndAuthor()
{
    FakeFetcher fetcher;
    Enricher enricher(fetcher, {}, fast());
    fetcher.script(OpenLibraryClient::booksApiUrl("9790000000001"), {ok("{}")});
    fetcher.script(OpenLibraryClient::searchUrl("Consider Phlebas", std::string("Iain M. Banks")),
        {ok(fixture("open_library/search_consider_phlebas.json"))});

    std::optional<FindResult> result;
    enricher.find(book("Consider Phlebas", "9790000000001"), [&](FindResult r) { result = std::move(r); });
    QTRY_VERIFY(result);
    QVERIFY(!result->byIsbn); // may be another edition (AV-010)
    QVERIFY(!result->candidates.empty());
    QVERIFY(!askedGoogle(fetcher));
}

void TestEnricher::googleIsAskedOnlyWithAKey()
{
    FakeFetcher fetcher;
    Enricher enricher(fetcher, QStringLiteral("test-key"), fast());
    QVERIFY(enricher.googleAvailable());
    QVERIFY(!Enricher(fetcher, {}, fast()).googleAvailable());

    fetcher.script(OpenLibraryClient::booksApiUrl(phlebasIsbn), {ok("{}")});
    fetcher.script(googleIsbnUrl(fetcher, phlebasIsbn), {ok(fixture("google_books/freetext_consider_phlebas.json"))});

    std::optional<FindResult> result;
    enricher.find(book("Consider Phlebas", phlebasIsbn), [&](FindResult r) { result = std::move(r); });
    QTRY_VERIFY(result);
    QVERIFY(result->byIsbn);
    QVERIFY(!result->candidates.empty());
    QVERIFY(result->candidates.front().source == Source::GoogleBooks);
}

void TestEnricher::providersUnreachableIsAProblemNotANotFound()
{
    FakeFetcher fetcher;
    Enricher enricher(fetcher, {}, fast());
    const HttpReply down {0, {}, QStringLiteral("Host not found"), std::nullopt};
    fetcher.script(OpenLibraryClient::booksApiUrl(phlebasIsbn), {down});
    fetcher.script(OpenLibraryClient::searchUrl("Consider Phlebas", std::string("Iain M. Banks")), {down});

    std::optional<FindResult> result;
    enricher.find(book("Consider Phlebas", phlebasIsbn), [&](FindResult r) { result = std::move(r); });
    QTRY_VERIFY(result);
    QVERIFY(result->candidates.empty());
    QVERIFY(result->problem);
    QVERIFY(QString::fromStdString(*result->problem).contains(QStringLiteral("could not be reached")));

    // One provider answering "none" is an answer, though another failed.
    fetcher.script(OpenLibraryClient::booksApiUrl(phlebasIsbn), {ok("{}")});
    result.reset();
    enricher.find(book("Consider Phlebas", phlebasIsbn), [&](FindResult r) { result = std::move(r); });
    QTRY_VERIFY(result);
    QVERIFY(!result->problem);
}

void TestEnricher::aSearchedCandidateIsCompletedFromItsWork()
{
    FakeFetcher fetcher;
    Enricher enricher(fetcher, {}, fast());
    fetcher.script(OpenLibraryClient::recordUrl("/works/OL8368432W"), {ok(fixture("open_library/work_OL8368432W.json"))});

    Candidate candidate;
    candidate.title = "Consider Phlebas";
    candidate.workKey = "/works/OL8368432W";
    std::optional<Candidate> filled;
    enricher.complete(candidate, [&](Candidate c) { filled = std::move(c); });
    QTRY_VERIFY(filled);
    QVERIFY(filled->description->rfind("Consider Phlebas is perhaps", 0) == 0);

    // One with a synopsis already is handed straight back, unasked.
    const auto asked = fetcher.requested.size();
    candidate.description = "Already here.";
    filled.reset();
    enricher.complete(candidate, [&](Candidate c) { filled = std::move(c); });
    QVERIFY(filled);
    QVERIFY(filled->description == std::optional<std::string>("Already here."));
    QCOMPARE(fetcher.requested.size(), asked);
}

void TestEnricher::theBritishLibraryFillsOpenLibrarysGaps()
{
    FakeFetcher fetcher;
    Enricher enricher(fetcher, {}, fast());
    fetcher.script(OpenLibraryClient::booksApiUrl(phlebasIsbn), {ok(fixture("open_library/isbn_9780316005388.json"))});
    fetcher.script(OpenLibraryClient::recordUrl("/books/OL9759601M"), {ok(fixture("open_library/edition_9780316005388.json"))});
    // Constructed: the Orbit 2023 record, given the US edition's ISBN so the
    // two providers answer the same one.
    QByteArray record = fixture("british_library/isbn_9780356521633.xml");
    record.replace("9780356521633", phlebasIsbn.c_str());
    fetcher.script(BritishLibraryClient::isbnUrl(phlebasIsbn), {ok(record)});

    std::optional<FindResult> result;
    enricher.find(book("Consider Phlebas", phlebasIsbn), [&](FindResult r) { result = std::move(r); });
    QTRY_VERIFY(result);
    QCOMPARE(result->candidates.size(), std::size_t(1));
    const Candidate& candidate = result->candidates.front();
    QVERIFY(candidate.source == Source::OpenLibrary);         // Open Library's answer stands
    QVERIFY(candidate.publisher == std::optional<std::string>("Orbit"));
    QVERIFY(candidate.publishedYear == 2008);                  // Open Library had it; kept
    QVERIFY(candidate.firstPublishedYear == 1987);             // a gap, filled
    QVERIFY(candidate.description);                            // Open Library's synopsis
    QVERIFY(candidate.filledFrom == Source::BritishLibrary);
    QCOMPARE(candidate.filledCategories, std::vector<std::string>({"Science fiction"}));
}

void TestEnricher::theBritishLibraryAnswersAlone()
{
    FakeFetcher fetcher;
    Enricher enricher(fetcher, {}, fast());
    fetcher.script(OpenLibraryClient::booksApiUrl("9780356521633"), {ok("{}")});
    fetcher.script(BritishLibraryClient::isbnUrl("9780356521633"), {ok(fixture("british_library/isbn_9780356521633.xml"))});

    std::optional<FindResult> result;
    enricher.find(book("Consider Phlebas", "9780356521633"), [&](FindResult r) { result = std::move(r); });
    QTRY_VERIFY(result);
    QVERIFY(result->byIsbn);
    QCOMPARE(result->candidates.size(), std::size_t(1));
    QVERIFY(result->candidates.front().source == Source::BritishLibrary);
    QVERIFY(!result->candidates.front().filledFrom);
}

void TestEnricher::cancellingDropsTheAnswer()
{
    FakeFetcher fetcher;
    Enricher enricher(fetcher, {}, fast());
    fetcher.script(OpenLibraryClient::booksApiUrl(phlebasIsbn), {ok(fixture("open_library/isbn_9780316005388.json"))});

    bool answered = false;
    enricher.find(book("Consider Phlebas", phlebasIsbn), [&](FindResult) { answered = true; });
    enricher.cancel();
    QTest::qWait(50);
    QVERIFY(!answered);
}

void TestEnricher::theKeyIsFoundInOrder()
{
    qunsetenv("PINAX_GOOGLE_BOOKS_KEY");
    QTemporaryDir first;
    QTemporaryDir second;
    QVERIFY(pinax::app::findGoogleBooksKey({first.path(), second.path()}).isEmpty());

    QFile secondKey(second.filePath(QStringLiteral("google-books.key")));
    QVERIFY(secondKey.open(QIODevice::WriteOnly));
    secondKey.write("second-key\n");
    secondKey.close();
    QCOMPARE(pinax::app::findGoogleBooksKey({first.path(), second.path()}), QStringLiteral("second-key"));

    QFile firstKey(first.filePath(QStringLiteral("google-books.key")));
    QVERIFY(firstKey.open(QIODevice::WriteOnly));
    firstKey.write("  first-key  ");
    firstKey.close();
    QCOMPARE(pinax::app::findGoogleBooksKey({first.path(), second.path()}), QStringLiteral("first-key"));

    qputenv("PINAX_GOOGLE_BOOKS_KEY", "from-environment");
    QCOMPARE(pinax::app::findGoogleBooksKey({first.path(), second.path()}), QStringLiteral("from-environment"));
    qunsetenv("PINAX_GOOGLE_BOOKS_KEY");
}

namespace {

// A window over a catalogue on disk, one book in it, and an enricher whose
// network is scripted.
struct Fixture {
    QTemporaryDir dir;
    Catalogue catalogue {dir.filePath(QStringLiteral("pinax.db")).toStdString()};
    FakeFetcher fetcher;
    Enricher enricher {fetcher, {}, fast()};
    pinax::app::MainWindow window;
    std::int64_t book = 0;

    explicit Fixture(const std::optional<std::string>& isbn)
    {
        pinax::domain::BookEdit edit;
        edit.book.title = "Consider Phlebas";
        edit.book.isbn13 = isbn;
        edit.credits = {{"Iain M. Banks", pinax::domain::CreditRole::Author}};
        book = catalogue.save(edit).id;
        window.setCatalogue(&catalogue);
        window.setEnricher(&enricher);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.bookList()->selectBook(book);
    }
    DetailPanel* panel() { return window.detailPanel(); }
    void pressFetch()
    {
        QTest::mouseClick(panel()->view()->findChild<QPushButton*>(QStringLiteral("fetch")), Qt::LeftButton);
    }
};

} // namespace

void TestEnricher::fetchingFromThePanelWritesTheChoiceAndItsCover()
{
    Fixture f(phlebasIsbn);
    f.fetcher.script(OpenLibraryClient::booksApiUrl(phlebasIsbn), {ok(fixture("open_library/isbn_9780316005388.json"))});
    f.fetcher.script(OpenLibraryClient::recordUrl("/books/OL9759601M"), {ok(fixture("open_library/edition_9780316005388.json"))});
    const auto candidate = openlibrary::parseBooksApi(fixture("open_library/isbn_9780316005388.json"), phlebasIsbn);
    QVERIFY(candidate && candidate->coverUrl);
    f.fetcher.script(CoverCache::politeUrl(QUrl(QString::fromStdString(*candidate->coverUrl))), {ok(coverImage())});

    f.pressFetch();
    QCOMPARE(f.panel()->state(), DetailPanel::State::Fetching);
    QVERIFY(!f.window.bookList()->isEnabled()); // the panel is busy (IMP-002)

    // Offered, not taken: nothing is written until the owner chooses (AV-010).
    auto* list = f.panel()->candidateView()->findChild<QListWidget*>(QStringLiteral("candidates.list"));
    QTRY_COMPARE(list->count(), 1);
    QVERIFY(!f.catalogue.detail(f.book)->book.synopsis);

    QTest::mouseClick(f.panel()->candidateView()->findChild<QPushButton*>(QStringLiteral("candidates.use")),
        Qt::LeftButton);
    QTRY_COMPARE(f.panel()->state(), DetailPanel::State::Viewing);
    QVERIFY(f.window.bookList()->isEnabled());

    const auto detail = f.catalogue.detail(f.book);
    QVERIFY(detail->book.synopsis->rfind("Overview:", 0) == 0);
    QVERIFY(detail->book.publisher == std::optional<std::string>("Orbit"));
    QVERIFY(detail->book.metadataStatus == pinax::domain::MetadataStatus::Matched);
    QVERIFY(!detail->genres.empty());
    const auto* view = f.panel()->view();
    QCOMPARE(view->findChild<QLabel*>(QStringLiteral("synopsisSource"))->text(), QStringLiteral("Open Library"));
    QVERIFY(view->findChild<QLabel*>(QStringLiteral("genres"))->text().contains(QStringLiteral("Science Fiction")));

    // The cover follows, and the panel shows it.
    QTRY_VERIFY(f.catalogue.detail(f.book)->book.coverPath);
    QVERIFY(f.catalogue.detail(f.book)->book.coverSource == Source::OpenLibrary);
    QVERIFY(QFile::exists(f.dir.filePath(QStringLiteral("covers/%1.png").arg(f.book))));
    QTRY_VERIFY(!view->findChild<QLabel*>(QStringLiteral("cover"))->pixmap().isNull());
}

void TestEnricher::cancellingAFetchReturnsToTheBook()
{
    Fixture f(phlebasIsbn);
    f.pressFetch();
    QCOMPARE(f.panel()->state(), DetailPanel::State::Fetching);
    QTest::keyClick(f.panel()->candidateView(), Qt::Key_Escape);
    QCOMPARE(f.panel()->state(), DetailPanel::State::Viewing);
    QVERIFY(f.window.bookList()->isEnabled());
    QTest::qWait(50); // the answer, had there been one, arrives to nothing
    QCOMPARE(f.panel()->state(), DetailPanel::State::Viewing);
    QVERIFY(f.catalogue.detail(f.book)->book.metadataStatus == pinax::domain::MetadataStatus::Unmatched);
}

void TestEnricher::nothingFoundMarksTheBookAndSaysSo()
{
    Fixture f(std::nullopt);
    f.fetcher.script(OpenLibraryClient::searchUrl("Consider Phlebas", std::string("Iain M. Banks")),
        {ok(R"({"numFound":0,"docs":[]})")});
    f.pressFetch();
    auto* status = f.panel()->candidateView()->findChild<QLabel*>(QStringLiteral("candidates.status"));
    QTRY_VERIFY(status->text().contains(QStringLiteral("marked as not found")));
    QVERIFY(f.catalogue.detail(f.book)->book.metadataStatus == pinax::domain::MetadataStatus::Failed);

    QTest::mouseClick(f.panel()->candidateView()->findChild<QPushButton*>(QStringLiteral("candidates.cancel")),
        Qt::LeftButton);
    QCOMPARE(f.panel()->state(), DetailPanel::State::Viewing);
}

QTEST_MAIN(TestEnricher)
#include "test_enricher.moc"
