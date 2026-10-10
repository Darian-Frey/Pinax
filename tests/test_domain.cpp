#include "domain/credit_text.h"
#include "domain/dates.h"
#include "domain/enrichment.h"
#include "domain/enums.h"
#include "domain/genre_filter.h"
#include "domain/isbn.h"
#include "domain/name_match.h"
#include "domain/placeholder.h"
#include "domain/series_titles.h"
#include "domain/sort_name.h"
#include "domain/sort_title.h"

#include <QTest>

using namespace pinax::domain;

class TestDomain : public QObject {
    Q_OBJECT

private slots:
    void unidentifiedVolumesAreUnownedAndUnnamed();
    void titlesFoundNameTheRightVolumes();
    void numberedTitlesFillUnnumberedSlotsInOrder();
    void sortTitleMovesLeadingArticle_data();
    void sortTitleMovesLeadingArticle();
    void enumsRoundTripThroughSchemaStrings();
    void unknownStringsAreRejected();
    void sortNamePutsSurnameFirst_data();
    void sortNamePutsSurnameFirst();
    void isbnCheckDigits();
    void creditTextRoundTrips();
    void placeholdersAreKnownByTitle();
    void isbn10ConvertsTo13();
    // F-012 to F-015, AV-001, AV-010
    void enrichmentFillsWhatIsEmpty();
    void enrichmentNeverTouchesWhatTheOwnerWrote();
    void aSearchedCandidateGivesNoEditionFacts();
    void aManualStatusStaysManual();
    void typicalFiguresAreNeverWritten();
    // IMP-008
    void lendingAndListTagsAreNotGenres();
    // IMP-010
    void datesAreAsPreciseAsRemembered();
    void filledGapsKeepTheirProvider();
    void titlesAgreeAcrossProviderNoise();
    void isbn13ConvertsTo10WhereItCan();
    void providerNamesMeetTheCataloguesOwn();
};

void TestDomain::sortTitleMovesLeadingArticle_data()
{
    QTest::addColumn<QString>("title");
    QTest::addColumn<QString>("expected");

    QTest::newRow("the") << "The Long Earth" << "Long Earth, The";
    QTest::newRow("a") << "A Fire Upon the Deep" << "Fire Upon the Deep, A";
    QTest::newRow("an") << "An Instance of the Fingerpost" << "Instance of the Fingerpost, An";
    QTest::newRow("no article") << "Excession" << "Excession";
    QTest::newRow("article as prefix of a word") << "Theory of Everything" << "Theory of Everything";
    QTest::newRow("a as prefix of a word") << "Anathem" << "Anathem";
    QTest::newRow("article alone") << "The" << "The";
    QTest::newRow("article in the middle") << "Use of the Weapons" << "Use of the Weapons";
}

void TestDomain::sortTitleMovesLeadingArticle()
{
    QFETCH(QString, title);
    QFETCH(QString, expected);
    QCOMPARE(QString::fromStdString(makeSortTitle(title.toStdString())), expected);
}

void TestDomain::enumsRoundTripThroughSchemaStrings()
{
    for (auto value : {ReadStatus::Unread, ReadStatus::Reading, ReadStatus::Read, ReadStatus::Abandoned})
        QVERIFY(readStatusFromString(toString(value)) == value);
    for (auto value : {Binding::Paperback, Binding::Hardback, Binding::Omnibus, Binding::Boxset, Binding::Other})
        QVERIFY(bindingFromString(toString(value)) == value);
    for (auto value : {MetadataStatus::Unmatched, MetadataStatus::Matched, MetadataStatus::Manual, MetadataStatus::Failed})
        QVERIFY(metadataStatusFromString(toString(value)) == value);
    for (auto value : {Source::GoogleBooks, Source::OpenLibrary, Source::Manual})
        QVERIFY(sourceFromString(toString(value)) == value);

    QCOMPARE(toString(Source::GoogleBooks), std::string_view("google_books"));
}

void TestDomain::unknownStringsAreRejected()
{
    QVERIFY(!readStatusFromString("Read"));
    QVERIFY(!bindingFromString(""));
    QVERIFY(!sourceFromString("goodreads"));
}

void TestDomain::sortNamePutsSurnameFirst_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("expected");

    QTest::newRow("initial") << "Iain M. Banks" << "Banks, Iain M.";
    QTest::newRow("several initials") << "J. R. R. Tolkien" << "Tolkien, J. R. R.";
    QTest::newRow("particle") << "Jon Del Arroz" << "Del Arroz, Jon";
    QTest::newRow("particle after initial") << "Ursula K. Le Guin" << "Le Guin, Ursula K.";
    QTest::newRow("one word") << "Wong" << "Wong";
    QTest::newRow("particle is not a given name") << "Van Morrison" << "Morrison, Van";
}

void TestDomain::sortNamePutsSurnameFirst()
{
    QFETCH(QString, name);
    QFETCH(QString, expected);
    QCOMPARE(QString::fromStdString(makeSortName(name.toStdString())), expected);
}

void TestDomain::isbnCheckDigits()
{
    QCOMPARE(normaliseIsbn("978-0-306-40615-7"), std::string("9780306406157"));
    QCOMPARE(normaliseIsbn("0-8044-2957-x"), std::string("080442957X"));

    QVERIFY(isValidIsbn13("9780306406157"));
    QVERIFY(isValidIsbn13("9780316005388"));
    QVERIFY(!isValidIsbn13("9780306406158"));
    QVERIFY(!isValidIsbn13("978030640615"));
    QVERIFY(!isValidIsbn13("97803064061X7"));

    QVERIFY(isValidIsbn10("0306406152"));
    QVERIFY(isValidIsbn10("080442957X"));
    QVERIFY(!isValidIsbn10("0306406153"));
    QVERIFY(!isValidIsbn10("X306406152"));
}

void TestDomain::creditTextRoundTrips()
{
    const auto credits = parseCredits(" Larry Niven &  Jerry Pournelle & Mike Ashley ( editor ) ");
    QCOMPARE(credits.size(), std::size_t(3));
    QCOMPARE(credits[1].name, std::string("Jerry Pournelle"));
    QVERIFY(credits[2].role == CreditRole::Editor);
    QCOMPARE(formatCredits(credits),
        std::string("Larry Niven & Jerry Pournelle & Mike Ashley (editor)"));

    QVERIFY(parseCredits("").empty());
    QVERIFY(parseCredits("   ").empty());
    QVERIFY_THROWS_EXCEPTION(CreditTextError, parseCredits("Larry Niven & "));
    QVERIFY_THROWS_EXCEPTION(CreditTextError, parseCredits("Somebody (publisher)"));
}

void TestDomain::placeholdersAreKnownByTitle()
{
    QVERIFY(isPlaceholderTitle(std::string_view("Unidentified volume 3")));
    QVERIFY(isPlaceholderTitle(std::string_view("Later volumes — unidentified")));
    QVERIFY(!isPlaceholderTitle(std::string_view("Consider Phlebas")));
    QVERIFY(!isPlaceholderTitle(std::string_view("Later Volumes of the Saga")));
    QVERIFY(!isPlaceholderTitle(std::optional<std::string>()));
}

void TestDomain::isbn10ConvertsTo13()
{
    QVERIFY(isValidIsbn10("031600538X"));
    QCOMPARE(isbn10To13("031600538X"), std::string("9780316005388"));
    QCOMPARE(isbn10To13("0575049723"), std::string("9780575049727"));
    QVERIFY(isValidIsbn13(isbn10To13("0575049723")));
}

namespace {

Candidate everything()
{
    Candidate candidate;
    candidate.source = Source::OpenLibrary;
    candidate.providerKey = "/books/OL9759601M";
    candidate.workKey = "/works/OL8368432W";
    candidate.title = "Consider Phlebas (Fetched)";
    candidate.authors = {"Iain Banks"};
    candidate.publisher = "Orbit";
    candidate.publishedYear = 2008;
    candidate.firstPublishedYear = 1987;
    candidate.pageCount = 480;
    candidate.isbn13 = "9780316005388";
    candidate.isbn10 = "031600538X";
    candidate.description = "The war raged across the galaxy.";
    candidate.categories = {"Science Fiction", "Space opera"};
    candidate.coverUrl = "https://covers.openlibrary.org/b/id/1174792-L.jpg";
    return candidate;
}

} // namespace

void TestDomain::enrichmentFillsWhatIsEmpty()
{
    Book book;
    book.id = 7;
    book.title = "Consider Phlebas";

    const auto plan = planEnrichment(book, everything(), true, "2026-10-06T21:00:00Z");
    QVERIFY(plan.book.synopsis == std::optional<std::string>("The war raged across the galaxy."));
    QVERIFY(plan.book.synopsisSource == Source::OpenLibrary);
    QVERIFY(plan.book.publisher == std::optional<std::string>("Orbit"));
    QVERIFY(plan.book.publishedYear == 1987); // first published, as the panel labels it
    QVERIFY(plan.book.pageCount == 480);
    QVERIFY(plan.book.metadataStatus == MetadataStatus::Matched);
    QVERIFY(plan.book.metadataFetchedAt == std::optional<std::string>("2026-10-06T21:00:00Z"));
    QVERIFY(plan.genres
        == (std::vector<EnrichmentPlan::Genre> {{"Science Fiction", Source::OpenLibrary}, {"Space opera", Source::OpenLibrary}}));
    QVERIFY(plan.coverUrl == everything().coverUrl);

    // Never the title, never the ISBN, even onto a book without one (AV-010).
    QCOMPARE(plan.book.title, std::string("Consider Phlebas"));
    QVERIFY(!plan.book.isbn13 && !plan.book.isbn10);

    // A fetched synopsis replaces an earlier fetched one.
    book.synopsis = "Old words.";
    book.synopsisSource = Source::GoogleBooks;
    QVERIFY(planEnrichment(book, everything(), true, "t").book.synopsisSource == Source::OpenLibrary);
}

void TestDomain::enrichmentNeverTouchesWhatTheOwnerWrote()
{
    // AV-001.
    Book book;
    book.id = 7;
    book.title = "Consider Phlebas";
    book.synopsis = "My own words.";
    book.synopsisSource = Source::Manual;
    book.coverPath = "covers/mine.png";
    book.coverSource = Source::Manual;
    book.publisher = "Macmillan";
    book.publishedYear = 1987;
    book.pageCount = 471;
    book.isbn13 = "9780333447055";
    book.editionNote = "First edition";
    book.conditionNote = "Foxed";
    book.notes = "Signed.";
    book.rating = 9;
    book.readStatus = ReadStatus::Read;
    book.timesRead = 2;

    const auto plan = planEnrichment(book, everything(), true, "2026-10-06T21:00:00Z");
    Book expected = book;
    expected.metadataStatus = MetadataStatus::Matched;
    expected.metadataFetchedAt = "2026-10-06T21:00:00Z";
    QVERIFY(plan.book == expected);
    QVERIFY(!plan.coverUrl);
    // Genres are added beside the owner's, never in place of them.
    QCOMPARE(plan.genres.size(), std::size_t(2));
}

void TestDomain::aSearchedCandidateGivesNoEditionFacts()
{
    // A title search may describe another edition: its publisher and page
    // count are not this copy's (AV-010). The work's facts still apply.
    Book book;
    book.title = "Consider Phlebas";
    const auto plan = planEnrichment(book, everything(), false, "t");
    QVERIFY(!plan.book.publisher);
    QVERIFY(!plan.book.pageCount);
    QVERIFY(plan.book.publishedYear == 1987);
    QVERIFY(plan.book.synopsis);
    QVERIFY(plan.coverUrl);
}

void TestDomain::typicalFiguresAreNeverWritten()
{
    // D-029: a work's median page count is no edition's, even confirmed.
    Book book;
    book.title = "Consider Phlebas";
    Candidate candidate = everything();
    candidate.editionFactsTypical = true;
    const auto plan = planEnrichment(book, candidate, true, "t");
    QVERIFY(!plan.book.pageCount);
    QVERIFY(!plan.book.publisher);
    QVERIFY(plan.book.synopsis); // the rest as ever
}

void TestDomain::lendingAndListTagsAreNotGenres()
{
    for (const char* tag : {"Accessible book", "Protected DAISY", "OverDrive", "In library", "Large type books",
             "Long Now Manual for Civilization", "long now manual for civilization", "New York Times bestseller",
             "nyt:trade-fiction-paperback=2013-03-31", "award:hugo_award=2006", "award:hugo_award=novel",
             "Large type books."})
        QVERIFY2(isServiceSubject(tag), tag);
    // Real subjects, however odd, stay verbatim (D-009).
    for (const char* subject : {"Fiction", "Science fiction.", "Fiction, science fiction, general", "Time travel",
             "Hugo Award Winner", "Captain Frey (Fictitious character)", "Star Wars: The Clone Wars", "Roman",
             "Library science"})
        QVERIFY2(!isServiceSubject(subject), subject);

    Book book;
    book.title = "Consider Phlebas";
    Candidate candidate = everything();
    candidate.categories = {"Fiction", "Accessible book", "nyt:combined-print-and-e-book-fiction=2012-10-14"};
    candidate.filledFrom = Source::BritishLibrary;
    candidate.filledCategories = {"Science fiction", "Protected DAISY"};
    QVERIFY(planEnrichment(book, candidate, true, "t").genres
        == (std::vector<EnrichmentPlan::Genre> {{"Fiction", Source::OpenLibrary},
            {"Science fiction", Source::BritishLibrary}}));
}

void TestDomain::datesAreAsPreciseAsRemembered()
{
    for (const char* date : {"2019", "2019-03", "2019-03-14", "2024-02-29", "1999-12-31"})
        QVERIFY2(isPartialIsoDate(date), date);
    for (const char* date : {"", "19", "2019-3", "2019-13", "2019-00", "2019-03-32", "2023-02-29", "2019/03/14",
             "14-03-2019", "2019-03-14T10:00", "last spring"})
        QVERIFY2(!isPartialIsoDate(date), date);
}

void TestDomain::aManualStatusStaysManual()
{
    Book book;
    book.title = "Consider Phlebas";
    book.metadataStatus = MetadataStatus::Manual;
    QVERIFY(planEnrichment(book, everything(), true, "t").book.metadataStatus == MetadataStatus::Manual);
}

void TestDomain::filledGapsKeepTheirProvider()
{
    Book book;
    book.title = "Titan";
    Candidate candidate = everything();
    candidate.categories = {"Fiction"};
    candidate.filledFrom = Source::BritishLibrary;
    candidate.filledCategories = {"Science fiction"};
    QVERIFY(planEnrichment(book, candidate, true, "t").genres
        == (std::vector<EnrichmentPlan::Genre> {{"Fiction", Source::OpenLibrary},
            {"Science fiction", Source::BritishLibrary}}));
}

void TestDomain::titlesAgreeAcrossProviderNoise()
{
    QVERIFY(titlesAgree("Titan", "Titan (NASA Trilogy)"));
    QVERIFY(titlesAgree("Sunstorm", "Sunstorm (Gollancz)"));
    QVERIFY(titlesAgree("The Long Earth", "Long Earth"));
    QVERIFY(titlesAgree("Consider Phlebas", "CONSIDER PHLEBAS"));
    QVERIFY(titlesAgree("Foundation and Empire: Book 2", "Foundation and Empire"));
    QVERIFY(!titlesAgree("Excession", "Consider Phlebas"));
    QVERIFY(!titlesAgree("Titan", "Titanic"));          // a whole word, not a prefix
    QVERIFY(!titlesAgree("Dune", "The Dune Encyclopedia"));
    QVERIFY(!titlesAgree("", "Dune"));
}

void TestDomain::isbn13ConvertsTo10WhereItCan()
{
    QVERIFY(isbn13To10("9780316005388") == std::optional<std::string>("031600538X"));
    QVERIFY(isbn13To10("9780575078017") == std::optional<std::string>("0575078014"));
    QVERIFY(!isbn13To10("9798655608320")); // 979 has no ISBN-10
}

void TestDomain::providerNamesMeetTheCataloguesOwn()
{
    const std::vector<std::string> known {"Iain M. Banks", "Arthur C. Clarke", "Stephen Baxter"};
    QVERIFY(knownAuthor("Iain Banks", known) == std::optional<std::string>("Iain M. Banks"));
    QVERIFY(knownAuthor("ARTHUR C CLARKE", known) == std::optional<std::string>("Arthur C. Clarke"));
    QVERIFY(knownAuthor("Stephen Baxter", known) == std::optional<std::string>("Stephen Baxter"));
    QVERIFY(!knownAuthor("Kali Wallace", known));
    QVERIFY(!knownAuthor("Gregory Benford", known));
    QVERIFY(!knownAuthor("Sarah Baxter", known)); // a surname is not enough
    QVERIFY(shareAnAuthor({"Iain Banks"}, {"Iain M. Banks"}));
    QVERIFY(!shareAnAuthor({"Poul Anderson"}, {"Iain M. Banks", "Stephen Baxter"}));
}

namespace {

SeriesRow entryRow(std::int64_t id, std::optional<std::string> position, std::optional<std::string> title,
    bool owned = false)
{
    SeriesRow row;
    row.entryId = id;
    row.position = std::move(position);
    row.entryTitle = title;
    if (owned) {
        row.bookId = id * 100;
        row.bookTitle = title;
    }
    return row;
}

FoundVolume found(const std::string& title, std::optional<std::string> ordinal = std::nullopt)
{
    return {title, std::move(ordinal), std::nullopt, {}};
}

} // namespace

void TestDomain::unidentifiedVolumesAreUnownedAndUnnamed()
{
    QVERIFY(isUnidentified(entryRow(1, "2", std::nullopt)));
    QVERIFY(isUnidentified(entryRow(1, "2", std::string("  "))));
    QVERIFY(isUnidentified(entryRow(1, "3", std::string("Unidentified volume 3"))));
    QVERIFY(isUnidentified(entryRow(1, std::nullopt, std::string("Later volumes — unidentified"))));
    QVERIFY(!isUnidentified(entryRow(1, "4", std::string("God Emperor of Dune"))));
    QVERIFY(!isUnidentified(entryRow(1, "1", std::nullopt, true))); // owned: it has its book

    SeriesEntry entry;
    QVERIFY(isUnidentified(entry));
    entry.title = "Unidentified volume 2";
    QVERIFY(isUnidentified(entry));
    entry.bookId = 7;
    QVERIFY(!isUnidentified(entry));
}

void TestDomain::titlesFoundNameTheRightVolumes()
{
    const std::vector<SeriesRow> rows {
        entryRow(1, "1", std::string("Dune"), true),
        entryRow(2, "2", std::nullopt),
        entryRow(3, "3", std::string("Unidentified volume 3")),
        entryRow(4, "4", std::string("God Emperor of Dune")),
        entryRow(5, std::nullopt, std::string("Later volumes — unidentified")),
        entryRow(6, "Broadcast 10", std::nullopt),
    };
    const std::vector<FoundVolume> volumes {
        found("Dune", "1"),                       // held already
        found("Dune Messiah", "2"),               // names entry 2
        found("Children of Dune", "3"),           // names the placeholder at 3
        found("God Emperor of Dune", "4"),        // listed already
        found("Heretics of Dune", "5"),           // not listed: a new volume
        found("A Different Fourth", "4"),         // 4 has the owner's title: left out
        found("The Dune Encyclopedia"),           // unnumbered: a new volume
        found("Spinward Fringe Broadcast 10"),    // ends with entry 6's position
        found("Spinward Fringe Broadcast 1"),     // not "Broadcast 10": a new volume
        found("Dune Messiah", "2"),               // the same title twice: once
    };
    const auto proposals = planSeriesTitles(rows, volumes);
    QCOMPARE(proposals.size(), std::size_t(6));

    QCOMPARE(proposals[0].volume.title, std::string("Dune Messiah"));
    QVERIFY(proposals[0].entryId == 2);
    QVERIFY(proposals[0].chosen);
    QCOMPARE(proposals[1].volume.title, std::string("Children of Dune"));
    QVERIFY(proposals[1].entryId == 3);
    QVERIFY(proposals[1].chosen);
    QCOMPARE(proposals[2].volume.title, std::string("Heretics of Dune"));
    QVERIFY(!proposals[2].entryId);
    QVERIFY(!proposals[2].chosen); // a new volume is offered, never assumed
    QCOMPARE(proposals[3].volume.title, std::string("The Dune Encyclopedia"));
    QVERIFY(!proposals[3].entryId);
    QCOMPARE(proposals[4].volume.title, std::string("Spinward Fringe Broadcast 10"));
    QVERIFY(proposals[4].entryId == 6);
    QVERIFY(proposals[4].chosen);
    QCOMPARE(proposals[5].volume.title, std::string("Spinward Fringe Broadcast 1"));
    QVERIFY(!proposals[5].entryId);

    // Nothing found, nothing proposed; a series already complete likewise.
    QVERIFY(planSeriesTitles(rows, {}).empty());
    QVERIFY(planSeriesTitles({entryRow(1, "1", std::string("Dune"), true)}, {found("Dune", "1")}).empty());
}

void TestDomain::numberedTitlesFillUnnumberedSlotsInOrder()
{
    // The owner knew two volumes were missing, not which: two slots.
    const std::vector<SeriesRow> rows {
        entryRow(1, std::nullopt, std::string("Unidentified volume 1")),
        entryRow(2, std::nullopt, std::string("Unidentified volume 2")),
        entryRow(3, "1", std::string("The Colour of Magic"), true),
        entryRow(4, "Broadcast 5", std::string("Frontline"), true),
        entryRow(5, std::nullopt, std::string("Later volumes — unidentified")),
    };
    const auto proposals = planSeriesTitles(rows, {
        found("The Colour of Magic", "1"),        // held
        found("The Light Fantastic", "2"),        // the first slot
        found("Equal Rites", "3"),                // the second
        found("Mort", "4"),                       // no slot left: a new volume
        found("Spinward Fringe Broadcast 5"),     // held at "Broadcast 5" under its own title
        found("The Art of Discworld"),            // unnumbered: never put in a slot
    });
    QCOMPARE(proposals.size(), std::size_t(4));
    QCOMPARE(proposals[0].volume.title, std::string("The Light Fantastic"));
    QVERIFY(proposals[0].entryId == 1);
    QVERIFY(proposals[0].chosen);
    QCOMPARE(proposals[1].volume.title, std::string("Equal Rites"));
    QVERIFY(proposals[1].entryId == 2);
    QCOMPARE(proposals[2].volume.title, std::string("Mort"));
    QVERIFY(!proposals[2].entryId); // "Later volumes" stands for any number: not a slot
    QVERIFY(!proposals[2].chosen);
    QCOMPARE(proposals[3].volume.title, std::string("The Art of Discworld"));
    QVERIFY(!proposals[3].entryId);
}

QTEST_APPLESS_MAIN(TestDomain)
#include "test_domain.moc"
