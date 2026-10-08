#include "app/batch_enricher.h"
#include "app/catalogue.h"
#include "app/enricher.h"
#include "app/main_window.h"
#include "app/provider_key.h"
#include "io/csv_importer.h"
#include "io/series_importer.h"
#include "fake_fetcher.h"
#include "metadata/british_library.h"
#include "metadata/cover_cache.h"
#include "metadata/google_books.h"
#include "metadata/open_library.h"
#include "ui/book_editor.h"
#include "ui/book_list_view.h"
#include "ui/book_view.h"
#include "ui/add_by_isbn_view.h"
#include "ui/candidate_view.h"
#include "ui/detail_panel.h"

#include <QAction>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QRadioButton>
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

    // Add by ISBN (Phase 3 step 6, F-024, D-012)
    void addingByIsbnFillsTheGapThroughTheWindow();
    void aBadIsbnIsCaughtAtTheDoor();
    void anUnknownIsbnFallsBackToSearchThenToHand();
    void aHeldCopyIsGivenItsIsbnNotAddedAgain();

    // Through the window
    void fetchingFromThePanelWritesTheChoiceAndItsCover();
    void searchedCandidatesShowTheirCovers();
    void myEditionTakesTheSearchedPageCount();
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

void TestEnricher::searchedCandidatesShowTheirCovers()
{
    Fixture f(std::nullopt);
    const QUrl search = OpenLibraryClient::searchUrl("Consider Phlebas", std::string("Iain M. Banks"));
    f.fetcher.script(search, {ok(fixture("open_library/search_consider_phlebas.json"))});
    const auto candidates = openlibrary::parseSearch(fixture("open_library/search_consider_phlebas.json"));
    QVERIFY(!candidates.empty() && candidates.front().coverUrl);
    // The medium image, politely asked, for each candidate with a cover.
    QStringList thumbnails;
    for (const auto& candidate : candidates) {
        if (!candidate.coverUrl)
            continue;
        const QUrl url = CoverCache::politeUrl(CoverCache::thumbnailUrl(QUrl(QString::fromStdString(*candidate.coverUrl))));
        f.fetcher.script(url, {ok(coverImage())});
        thumbnails << url.toString();
    }

    f.pressFetch();
    auto* list = f.panel()->candidateView()->findChild<QListWidget*>(QStringLiteral("candidates.list"));
    QTRY_COMPARE(list->count(), static_cast<int>(candidates.size()));
    QTRY_VERIFY(list->item(0)->data(Qt::UserRole).toBool());
    for (const auto& url : thumbnails)
        QVERIFY(std::count(f.fetcher.requested.begin(), f.fetcher.requested.end(), url) == 1);
    QVERIFY(std::none_of(f.fetcher.requested.begin(), f.fetcher.requested.end(),
        [](const QString& url) { return url.contains(QStringLiteral("-L.jpg")); }));
    // Covers do not choose: nothing is written yet.
    QVERIFY(!f.catalogue.detail(f.book)->book.synopsis);
}

void TestEnricher::myEditionTakesTheSearchedPageCount()
{
    // D-029: as the owner found with Cello's Gate — no ISBN, a search, a
    // page count shown and not saved — unless they say it is their edition.
    for (const bool tick : {false, true}) {
        Fixture f(std::nullopt);
        f.fetcher.script(OpenLibraryClient::searchUrl("Consider Phlebas", std::string("Iain M. Banks")),
            {ok(fixture("open_library/search_consider_phlebas.json"))});
        // The edition behind the search's cover — as recorded, the German one.
        f.fetcher.script(OpenLibraryClient::recordUrl("/books/OL9041460M"), {ok(fixture("open_library/edition_OL9041460M.json"))});
        f.pressFetch();
        auto* view = f.panel()->candidateView();
        auto* list = view->findChild<QListWidget*>(QStringLiteral("candidates.list"));
        QTRY_VERIFY(list->count() > 0);
        view->findChild<QCheckBox*>(QStringLiteral("candidates.myEdition"))->setChecked(tick);
        list->setCurrentRow(0);
        QTest::mouseClick(view->findChild<QPushButton*>(QStringLiteral("candidates.use")), Qt::LeftButton);
        QTRY_COMPARE(f.panel()->state(), DetailPanel::State::Viewing);
        const auto book = f.catalogue.detail(f.book)->book;
        // Unticked: nothing of an edition. Ticked: the cover's edition's own
        // facts — never the work's median of 471 pages (D-029).
        QVERIFY(book.pageCount == (tick ? std::optional(762) : std::nullopt));
        QVERIFY(book.publisher == (tick ? std::optional<std::string>("Heyne") : std::nullopt));
        QVERIFY(!book.isbn13); // never from a search (D-029)
    }
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


namespace {

// The Culture with two volumes held and Consider Phlebas missing, in a
// window whose network is scripted.
struct Adding {
    QTemporaryDir dir;
    Catalogue catalogue {dir.filePath(QStringLiteral("pinax.db")).toStdString()};
    FakeFetcher fetcher;
    Enricher enricher {fetcher, {}, fast()};
    pinax::app::MainWindow window;

    Adding()
    {
        pinax::io::CsvImporter(catalogue.connection()).importText(
            "title,authors,series,position,shelf\n"
            "Surface Detail,Iain M. Banks,The Culture,9,unread\n"
            "Excession,Iain M. Banks,The Culture,5,read\n");
        pinax::io::SeriesImporter(catalogue.connection()).importText(
            "series,position,title\n"
            "The Culture,1,Consider Phlebas\n");
        fetcher.script(OpenLibraryClient::booksApiUrl(phlebasIsbn), {ok(fixture("open_library/isbn_9780316005388.json"))});
        fetcher.script(OpenLibraryClient::recordUrl("/books/OL9759601M"), {ok(fixture("open_library/edition_9780316005388.json"))});
        window.setCatalogue(&catalogue);
        window.setEnricher(&enricher);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
    }
    pinax::ui::AddByIsbnView* view() { return window.detailPanel()->addView(); }
    template <typename T>
    T* find(const char* name) { return view()->findChild<T*>(QString::fromLatin1(name)); }
    void lookUp(const QString& isbn)
    {
        find<QLineEdit>("add.isbn")->setText(isbn);
        QTest::mouseClick(find<QPushButton>("add.lookUp"), Qt::LeftButton);
    }
};

} // namespace

void TestEnricher::addingByIsbnFillsTheGapThroughTheWindow()
{
    Adding a;
    const auto candidate = openlibrary::parseBooksApi(fixture("open_library/isbn_9780316005388.json"), phlebasIsbn);
    const QUrl coverUrl = CoverCache::politeUrl(QUrl(QString::fromStdString(*candidate->coverUrl)));
    a.fetcher.script(coverUrl, {ok(coverImage())});

    QVERIFY(a.window.addByIsbnAction()->isEnabled());
    a.window.addByIsbnAction()->trigger();
    QCOMPARE(a.window.detailPanel()->state(), DetailPanel::State::Adding);
    QVERIFY(!a.window.bookList()->isEnabled());

    a.lookUp(QStringLiteral("978-0-316-00538-8"));
    QTRY_COMPARE(a.find<QLineEdit>("add.title")->text(), QStringLiteral("Consider Phlebas"));
    // Nothing written yet (D-012).
    QCOMPARE(a.catalogue.count(), 2);
    // The provider's "Iain Banks" in the catalogue's spelling.
    QCOMPARE(a.find<QLineEdit>("add.authors")->text(), QStringLiteral("Iain M. Banks"));
    // The gap it fills, and what that does to the series, before confirming.
    auto* fills = a.find<QRadioButton>("add.series.0");
    QVERIFY(fills && fills->isChecked());
    QVERIFY(fills->text().contains(QStringLiteral("Consider Phlebas")));
    QVERIFY(fills->text().contains(QStringLiteral("3 of 3 held, complete")));
    // It fits the panel: nothing wider than the view it sits in.
    QVERIFY(a.view()->minimumSizeHint().width() <= 320);
    QTRY_VERIFY(!a.find<QLabel>("add.cover")->pixmap().isNull());

    QTest::mouseClick(a.find<QRadioButton>("add.read"), Qt::LeftButton);
    QTest::mouseClick(a.find<QPushButton>("add.add"), Qt::LeftButton);
    QVERIFY(a.window.detailPanel()->state() != DetailPanel::State::Adding);
    QCOMPARE(a.catalogue.count(), 3);
    QVERIFY(a.catalogue.missingVolumes().empty());

    const auto held = a.catalogue.bookWithIsbn(phlebasIsbn);
    QVERIFY(held);
    const auto detail = a.catalogue.detail(held->id);
    QVERIFY(detail->book.readStatus == pinax::domain::ReadStatus::Read);
    QVERIFY(detail->book.synopsis);
    QCOMPARE(detail->series.front().status, std::string("Complete"));
    // The cover shown on the card is the one kept: downloaded once.
    QVERIFY(detail->book.coverPath);
    QCOMPARE(static_cast<int>(std::count(a.fetcher.requested.begin(), a.fetcher.requested.end(), coverUrl.toString())), 1);
    QCOMPARE(a.window.bookList()->selectedBooks(), QList<qint64>({held->id}));

    // The same ISBN again: reported, not added, and Show it goes to it.
    a.window.addByIsbnAction()->trigger();
    a.lookUp(QString::fromStdString(phlebasIsbn));
    QVERIFY(a.find<QLabel>("add.duplicate")->text().contains(QStringLiteral("already in your catalogue")));
    QTest::mouseClick(a.find<QPushButton>("add.showExisting"), Qt::LeftButton);
    QCOMPARE(a.window.detailPanel()->state(), DetailPanel::State::Viewing);
    QCOMPARE(a.catalogue.count(), 3);
}

void TestEnricher::aBadIsbnIsCaughtAtTheDoor()
{
    Adding a;
    a.window.addByIsbnAction()->trigger();
    a.lookUp(QStringLiteral("9780316005389")); // last digit wrong
    QVERIFY(a.find<QLabel>("add.isbnError")->text().contains(QStringLiteral("check digit")));
    QTest::qWait(20);
    QVERIFY(a.fetcher.requested.empty());
    QTest::keyClick(a.view(), Qt::Key_Escape);
    QCOMPARE(a.window.detailPanel()->state(), DetailPanel::State::Empty);
    QVERIFY(a.window.bookList()->isEnabled());
}

void TestEnricher::anUnknownIsbnFallsBackToSearchThenToHand()
{
    Adding a;
    const std::string unknown = "9780575078017";
    a.fetcher.script(OpenLibraryClient::booksApiUrl(unknown), {ok("{}")});
    a.fetcher.script(OpenLibraryClient::searchUrl("Consider Phlebas", std::string("Iain M. Banks")),
        {ok(fixture("open_library/search_consider_phlebas.json"))});

    a.window.addByIsbnAction()->trigger();
    a.lookUp(QString::fromStdString(unknown));
    QTRY_VERIFY(a.find<QLabel>("add.searchMessage")->text().contains(QStringLiteral("No provider knows")));
    a.find<QLineEdit>("add.searchTitle")->setText(QStringLiteral("Consider Phlebas"));
    a.find<QLineEdit>("add.searchAuthor")->setText(QStringLiteral("Iain M. Banks"));
    QTest::mouseClick(a.find<QPushButton>("add.search"), Qt::LeftButton);

    // Found by search: nothing chosen until the owner chooses (AV-010).
    auto* candidates = a.find<QComboBox>("add.candidates");
    QTRY_VERIFY(candidates->count() > 1);
    QVERIFY(!a.find<QPushButton>("add.add")->isEnabled());
    candidates->setCurrentIndex(1);
    QVERIFY(a.find<QPushButton>("add.add")->isEnabled());
    QTest::mouseClick(a.find<QPushButton>("add.add"), Qt::LeftButton);

    const auto held = a.catalogue.bookWithIsbn(unknown);
    QVERIFY(held); // the typed ISBN is kept
    const auto detail = a.catalogue.detail(held->id);
    QVERIFY(!detail->book.publisher); // a search's edition facts are not taken
    QVERIFY(!detail->book.pageCount);

    // Nothing found at all: by hand, with what is known filled in.
    a.fetcher.script(OpenLibraryClient::booksApiUrl("9780575083417"), {ok("{}")});
    a.window.addByIsbnAction()->trigger();
    a.lookUp(QStringLiteral("9780575083417"));
    QTRY_VERIFY(a.find<QLineEdit>("add.searchTitle")->isVisible());
    a.find<QLineEdit>("add.searchTitle")->setText(QStringLiteral("Firstborn"));
    QTest::mouseClick(a.find<QPushButton>("add.manual"), Qt::LeftButton);
    QCOMPARE(a.window.detailPanel()->state(), DetailPanel::State::Editing);
    auto* editor = a.window.detailPanel()->editor();
    QCOMPARE(editor->findChild<QLineEdit*>(QStringLiteral("edit.title"))->text(), QStringLiteral("Firstborn"));
    QCOMPARE(editor->findChild<QLineEdit*>(QStringLiteral("edit.isbn13"))->text(), QStringLiteral("9780575083417"));
}

void TestEnricher::aHeldCopyIsGivenItsIsbnNotAddedAgain()
{
    Adding a;
    // Consider Phlebas already on the shelf, outside the series, no ISBN.
    pinax::domain::BookEdit held;
    held.book.title = "Consider Phlebas";
    held.credits = {{"Iain M. Banks", pinax::domain::CreditRole::Author}};
    const auto id = a.catalogue.save(held).id;
    a.window.setCatalogue(&a.catalogue);
    a.window.setEnricher(&a.enricher);

    a.window.addByIsbnAction()->trigger();
    a.lookUp(QString::fromStdString(phlebasIsbn));
    QTRY_VERIFY(a.find<QRadioButton>("add.held.0") != nullptr);
    auto* mine = a.find<QRadioButton>("add.held.0");
    QVERIFY(mine->isChecked()); // the likeliest answer first
    QVERIFY(mine->text().contains(QStringLiteral("Consider Phlebas")));
    QCOMPARE(a.find<QPushButton>("add.add")->text(), QStringLiteral("Give it this ISBN"));
    QVERIFY(!a.find<QLineEdit>("add.title")->isEnabled());

    const auto count = a.catalogue.count();
    QTest::mouseClick(a.find<QPushButton>("add.add"), Qt::LeftButton);
    QCOMPARE(a.catalogue.count(), count); // no second copy
    const auto detail = a.catalogue.detail(id);
    QVERIFY(detail->book.isbn13 == std::optional<std::string>(phlebasIsbn));
    QVERIFY(detail->book.synopsis);
    QCOMPARE(a.window.bookList()->selectedBooks(), QList<qint64>({id}));
}

QTEST_MAIN(TestEnricher)
#include "test_enricher.moc"
