#include "app/batch_enricher.h"
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

#include <QAction>
#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QSignalSpy>
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

    // The batch run (Phase 3 step 5, D-023)
    void aBatchTakesOnlyWhatItMayAndQueuesTheRest();
    void anInterruptedBatchResumesWhereItLeftOff();
    void reviewingThroughTheWindow();

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

namespace {

// A catalogue of four books, each meeting a different answer:
//   Consider Phlebas  ISBN known, title agrees      -> taken
//   Excession         ISBN answered as another book -> review
//   Surface Detail    no ISBN, found by search      -> review
//   Tau Zero          no ISBN, found nowhere        -> not found
struct Batch {
    QTemporaryDir dir;
    Catalogue catalogue {dir.filePath(QStringLiteral("pinax.db")).toStdString()};
    FakeFetcher fetcher;
    Enricher enricher {fetcher, {}, fast()};
    pinax::app::BatchEnricher batch {catalogue, enricher};
    std::map<std::string, std::int64_t> ids;

    Batch()
    {
        add("Consider Phlebas", phlebasIsbn, "Iain M. Banks");
        add("Excession", "9780356521633", "Iain M. Banks");
        add("Surface Detail", std::nullopt, "Iain M. Banks");
        add("Tau Zero", std::nullopt, "Poul Anderson");

        fetcher.script(OpenLibraryClient::booksApiUrl(phlebasIsbn), {ok(fixture("open_library/isbn_9780316005388.json"))});
        fetcher.script(OpenLibraryClient::recordUrl("/books/OL9759601M"), {ok(fixture("open_library/edition_9780316005388.json"))});
        // Constructed: Consider Phlebas's answer under Excession's ISBN.
        QByteArray other = fixture("open_library/isbn_9780316005388.json");
        other.replace(phlebasIsbn.c_str(), "9780356521633");
        fetcher.script(OpenLibraryClient::booksApiUrl("9780356521633"), {ok(other)});
        fetcher.script(OpenLibraryClient::searchUrl("Surface Detail", std::string("Iain M. Banks")),
            {ok(fixture("open_library/search_consider_phlebas.json"))});
        fetcher.script(OpenLibraryClient::searchUrl("Tau Zero", std::string("Poul Anderson")),
            {ok(R"({"numFound":0,"docs":[]})")});
    }
    void add(const std::string& title, const std::optional<std::string>& isbn, const std::string& author)
    {
        pinax::domain::BookEdit edit;
        edit.book.title = title;
        edit.book.isbn13 = isbn;
        edit.credits = {{author, pinax::domain::CreditRole::Author}};
        ids[title] = catalogue.save(edit).id;
    }
    pinax::domain::MetadataStatus status(const std::string& title)
    {
        return catalogue.detail(ids[title])->book.metadataStatus;
    }
    int asked(const QUrl& url) const
    {
        return static_cast<int>(std::count(fetcher.requested.begin(), fetcher.requested.end(), url.toString()));
    }
};

} // namespace

void TestEnricher::aBatchTakesOnlyWhatItMayAndQueuesTheRest()
{
    using pinax::domain::MetadataStatus;
    Batch b;
    QSignalSpy finished(&b.batch, &pinax::app::BatchEnricher::finished);
    b.batch.start();
    QVERIFY(b.batch.running());
    QCOMPARE(b.batch.progress().total, 4);
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(finished.front().front().toString().isEmpty()); // reached the end

    const auto& progress = b.batch.progress();
    QCOMPARE(progress.done, 4);
    QCOMPARE(progress.matched, 1);
    QCOMPARE(progress.toReview, 2);
    QCOMPARE(progress.notFound, 1);
    QVERIFY(b.status("Consider Phlebas") == MetadataStatus::Matched);
    QVERIFY(b.catalogue.detail(b.ids["Consider Phlebas"])->book.synopsis);
    // Found but not taken: nothing written until the owner decides (AV-010).
    QVERIFY(b.status("Excession") == MetadataStatus::Unmatched);
    QVERIFY(!b.catalogue.detail(b.ids["Excession"])->book.synopsis);
    QVERIFY(b.status("Surface Detail") == MetadataStatus::Unmatched);
    QVERIFY(b.status("Tau Zero") == MetadataStatus::Failed);

    QCOMPARE(b.batch.pendingCount(), 2);
    auto first = b.batch.takePending();
    QVERIFY(first && !first->candidates.empty());
    b.batch.putBack(std::move(*first), false);
    QCOMPARE(b.batch.pendingCount(), 2);
}

void TestEnricher::anInterruptedBatchResumesWhereItLeftOff()
{
    // AV-009: a provider out of reach stops the run without blaming the
    // book; the next run skips what is done and asks only what is not.
    using pinax::domain::MetadataStatus;
    Batch b;
    const HttpReply down {0, {}, QStringLiteral("Host not found"), std::nullopt};
    const QUrl tauZero = OpenLibraryClient::searchUrl("Tau Zero", std::string("Poul Anderson"));
    b.fetcher.script(tauZero, {down, down, ok(R"({"numFound":0,"docs":[]})")});

    QSignalSpy finished(&b.batch, &pinax::app::BatchEnricher::finished);
    b.batch.start();
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(finished.front().front().toString().contains(QStringLiteral("could not be reached")));
    QVERIFY(!b.batch.running());
    QVERIFY(b.status("Tau Zero") == MetadataStatus::Unmatched);

    const int phlebasAsked = b.asked(OpenLibraryClient::booksApiUrl(phlebasIsbn));
    const int queued = b.batch.pendingCount();
    b.batch.start();
    QTRY_COMPARE(finished.count(), 2);
    QVERIFY(finished.back().front().toString().isEmpty());
    QVERIFY(b.status("Tau Zero") == MetadataStatus::Failed);
    // Nothing done before is asked again, and nothing waiting is queued twice.
    QCOMPARE(b.asked(OpenLibraryClient::booksApiUrl(phlebasIsbn)), phlebasAsked);
    QCOMPARE(b.batch.pendingCount(), 2);
    QVERIFY(queued <= 2);

    // Stopping: the run ends at once and says how to carry on.
    b.catalogue.markLookupFailed(b.ids["Tau Zero"]);
    pinax::domain::BookEdit another;
    another.book.title = "Matter";
    b.catalogue.save(another);
    b.batch.start();
    b.batch.stop();
    QVERIFY(!b.batch.running());
    QVERIFY(finished.back().front().toString().contains(QStringLiteral("carry on")));
}

void TestEnricher::reviewingThroughTheWindow()
{
    using pinax::domain::MetadataStatus;
    Batch b;
    pinax::app::MainWindow window;
    window.setCatalogue(&b.catalogue);
    window.setEnricher(&b.enricher);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QVERIFY(window.fetchAllAction()->isEnabled());
    QVERIFY(!window.reviewAction()->isEnabled());
    window.fetchAllAction()->trigger();
    QCOMPARE(window.fetchAllAction()->text(), QStringLiteral("Stop fetching"));
    QTRY_VERIFY(!window.batch()->running());
    QCOMPARE(window.fetchAllAction()->text(), QStringLiteral("Fetch all metadata"));
    QCOMPARE(window.reviewAction()->text(), QStringLiteral("Review matches (2)"));
    QVERIFY(window.reviewAction()->isEnabled());

    auto* panel = window.detailPanel();
    auto* view = panel->candidateView();
    auto* list = view->findChild<QListWidget*>(QStringLiteral("candidates.list"));
    window.reviewAction()->trigger();
    QCOMPARE(panel->state(), DetailPanel::State::Fetching);
    QVERIFY(!window.bookList()->isEnabled());
    QVERIFY(view->findChild<QPushButton*>(QStringLiteral("candidates.reject"))->isVisible());

    // The first: take a candidate. The book on show is the one selected.
    const qint64 first = *panel->fetchingBookId();
    QCOMPARE(window.bookList()->selectedBooks(), QList<qint64>({first}));
    if (list->currentRow() < 0)
        list->setCurrentRow(0);
    QTest::mouseClick(view->findChild<QPushButton*>(QStringLiteral("candidates.use")), Qt::LeftButton);
    QTRY_VERIFY(panel->fetchingBookId() && *panel->fetchingBookId() != first);
    QVERIFY(b.catalogue.detail(first)->book.metadataStatus == MetadataStatus::Matched);

    // The second: none of these.
    const qint64 second = *panel->fetchingBookId();
    QTest::mouseClick(view->findChild<QPushButton*>(QStringLiteral("candidates.reject")), Qt::LeftButton);
    QTRY_VERIFY(panel->state() != DetailPanel::State::Fetching);
    QVERIFY(b.catalogue.detail(second)->book.metadataStatus == MetadataStatus::Failed);
    QVERIFY(window.bookList()->isEnabled());
    QVERIFY(!window.reviewAction()->isEnabled());
    QCOMPARE(window.batch()->pendingCount(), 0);
}


QTEST_MAIN(TestEnricher)
#include "test_enricher.moc"
