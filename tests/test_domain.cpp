#include "domain/enums.h"
#include "domain/sort_title.h"

#include <QTest>

using namespace pinax::domain;

class TestDomain : public QObject {
    Q_OBJECT

private slots:
    void sortTitleMovesLeadingArticle_data();
    void sortTitleMovesLeadingArticle();
    void enumsRoundTripThroughSchemaStrings();
    void unknownStringsAreRejected();
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

QTEST_APPLESS_MAIN(TestDomain)
#include "test_domain.moc"
