#include "ui/book_editor.h"
#include "ui/book_view.h"
#include "ui/candidate_view.h"
#include "ui/detail_panel.h"
#include "ui/rating_bar.h"

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QApplication>
#include <QTest>

using pinax::domain::Binding;
using pinax::domain::Book;
using pinax::domain::BookDetail;
using pinax::domain::ReadStatus;
using pinax::domain::SeriesMembership;
using pinax::domain::Source;
using pinax::ui::DetailPanel;

namespace {

BookDetail excession()
{
    BookDetail detail;
    detail.book.id = 5;
    detail.book.title = "Excession";
    detail.book.sortTitle = "Excession";
    detail.book.readStatus = ReadStatus::Read;
    detail.book.timesRead = 2;
    detail.book.rating = 9;
    detail.book.publisher = "Orbit";
    detail.book.publishedYear = 1996;
    detail.book.binding = Binding::Paperback;
    detail.authors = "Iain M. Banks";
    detail.credits = {{"Iain M. Banks", pinax::domain::CreditRole::Author}};

    SeriesMembership culture;
    culture.seriesId = 1;
    culture.name = "The Culture";
    culture.position = "5";
    culture.sortPosition = 5;
    culture.held = 9;
    culture.known = 10;
    culture.status = "Incomplete";
    culture.missing.push_back({std::string("1"), std::string("Consider Phlebas")});
    detail.series.push_back(culture);
    return detail;
}

QString labelText(const QWidget& root, const QString& name)
{
    const auto* label = root.findChild<QLabel*>(name);
    return label ? label->text() : QStringLiteral("<no label %1>").arg(name);
}

template <typename T>
T* child(const QWidget& root, const QString& name)
{
    T* found = root.findChild<T*>(name);
    if (!found)
        qFatal("no child named %s", qPrintable(name));
    return found;
}

} // namespace

class TestDetailPanel : public QObject {
    Q_OBJECT

private slots:
    void startsEmptyAndShowsSeveral();
    void viewShowsTheBookAsTheMockUpDoes();
    void viewSaysWhatIsNotRecorded();
    void editEmitsTheChangedBook();
    void editRejectsABadIsbnWithoutEmitting();
    void escapeCancelsBackToView();
    void editedSynopsisIsMarkedManual();
    void ratingSquaresChooseAndClear();
    // F-012, AV-010
    void fetchAsksForTheBookOnShow();
    void searchedCandidatesWaitToBeChosen();
    void anIsbnAnswerIsOfferedReadyToUse();
    void candidatesShowTheirCoversAsTheyArrive();
    void aHoveredCoverShowsAtTwiceTheSize();
    void authorsAreEditedAsText();
    void aNewBookStartsEmptyAndCancelsToNothing();
    void deletionIsConfirmedInThePanel();
    void placeholdersAreCountedNotNamed_data();
    void placeholdersAreCountedNotNamed();
};

void TestDetailPanel::startsEmptyAndShowsSeveral()
{
    DetailPanel panel;
    QVERIFY(panel.state() == DetailPanel::State::Empty);

    panel.showSelection(27);
    QVERIFY(panel.state() == DetailPanel::State::Several);
    QVERIFY(labelText(panel, QStringLiteral("detail.several")).startsWith(QStringLiteral("27 books selected")));

    panel.beginEdit(); // nothing to edit
    QVERIFY(panel.state() == DetailPanel::State::Several);
}

void TestDetailPanel::viewShowsTheBookAsTheMockUpDoes()
{
    DetailPanel panel;
    panel.showBook(excession());
    QVERIFY(panel.state() == DetailPanel::State::Viewing);

    const QWidget& view = *panel.view();
    QCOMPARE(labelText(view, QStringLiteral("title")), QStringLiteral("Excession"));
    QCOMPARE(labelText(view, QStringLiteral("authors")), QStringLiteral("Iain M. Banks"));
    QCOMPARE(labelText(view, QStringLiteral("readState")), QStringLiteral("Read"));
    QCOMPARE(labelText(view, QStringLiteral("readCount")), QStringLiteral("read twice"));
    QCOMPARE(labelText(view, QStringLiteral("ratingText")), QStringLiteral("9 / 10"));
    QCOMPARE(child<pinax::ui::RatingBar>(view, QStringLiteral("ratingBar"))->rating(),
        std::optional<int>(9));

    QCOMPARE(labelText(view, QStringLiteral("series.name")), QStringLiteral("The Culture"));
    QCOMPARE(labelText(view, QStringLiteral("series.place")), QStringLiteral("Book 5 of 10"));
    QCOMPARE(labelText(view, QStringLiteral("series.held")), QStringLiteral("9 of 10 held"));
    QCOMPARE(labelText(view, QStringLiteral("series.missing")),
        QStringLiteral("Missing <b>Consider Phlebas</b> — one volume completes this series"));

    QCOMPARE(labelText(view, QStringLiteral("edition.publisher")), QStringLiteral("Orbit"));
    QCOMPARE(labelText(view, QStringLiteral("edition.published")), QStringLiteral("1996"));
    QCOMPARE(labelText(view, QStringLiteral("edition.binding")), QStringLiteral("Paperback"));
    QCOMPARE(labelText(view, QStringLiteral("synopsisSource")), QStringLiteral("not fetched"));
}

void TestDetailPanel::viewSaysWhatIsNotRecorded()
{
    BookDetail bare;
    bare.book.id = 1;
    bare.book.title = "Tau Zero";

    DetailPanel panel;
    panel.showBook(bare);
    const QWidget& view = *panel.view();
    QCOMPARE(labelText(view, QStringLiteral("readCount")), QStringLiteral("not read yet"));
    QCOMPARE(labelText(view, QStringLiteral("ratingText")), QStringLiteral("unrated"));
    QCOMPARE(labelText(view, QStringLiteral("edition.isbn")), QStringLiteral("not recorded"));
    QCOMPARE(labelText(view, QStringLiteral("series.none")), QStringLiteral("Not part of a series"));
    QCOMPARE(labelText(view, QStringLiteral("authors")), QStringLiteral("No author recorded"));
}

void TestDetailPanel::editEmitsTheChangedBook()
{
    DetailPanel panel;
    panel.showBook(excession());
    QTest::mouseClick(child<QPushButton>(*panel.view(), QStringLiteral("edit")), Qt::LeftButton);
    QVERIFY(panel.state() == DetailPanel::State::Editing);

    std::optional<Book> saved;
    connect(&panel, &DetailPanel::saveRequested, this,
        [&](const pinax::domain::BookEdit& edit) { saved = edit.book; });

    const QWidget& editor = *panel.editor();
    child<QLineEdit>(editor, QStringLiteral("edit.title"))->setText(QStringLiteral("  Excession  "));
    child<QComboBox>(editor, QStringLiteral("edit.rating"))->setCurrentIndex(10);
    child<QLineEdit>(editor, QStringLiteral("edit.isbn13"))->setText(QStringLiteral("978-1-85723-457-2"));
    child<QLineEdit>(editor, QStringLiteral("edit.conditionNote"))->setText(QStringLiteral("spine creased"));
    QTest::mouseClick(child<QPushButton>(editor, QStringLiteral("edit.save")), Qt::LeftButton);

    QVERIFY(saved);
    QCOMPARE(saved->id, std::int64_t(5));
    QCOMPARE(saved->title, std::string("Excession"));
    QCOMPARE(saved->rating, std::optional<int>(10));
    QCOMPARE(saved->isbn13, std::optional<std::string>("9781857234572"));
    QCOMPARE(saved->conditionNote, std::optional<std::string>("spine creased"));
    // Untouched fields come through as they were; the count is not the form's.
    QCOMPARE(saved->timesRead, 2);
    QCOMPARE(saved->publisher, std::optional<std::string>("Orbit"));
    QVERIFY(!saved->synopsisSource);
}

void TestDetailPanel::editRejectsABadIsbnWithoutEmitting()
{
    DetailPanel panel;
    panel.showBook(excession());
    panel.beginEdit();

    bool emitted = false;
    connect(&panel, &DetailPanel::saveRequested, this, [&] { emitted = true; });

    const QWidget& editor = *panel.editor();
    child<QLineEdit>(editor, QStringLiteral("edit.isbn13"))->setText(QStringLiteral("9781857234576"));
    child<QLineEdit>(editor, QStringLiteral("edit.published"))->setText(QStringLiteral("96"));
    panel.editor()->save();

    QVERIFY(!emitted);
    QVERIFY(panel.state() == DetailPanel::State::Editing);
    const QString error = labelText(editor, QStringLiteral("edit.error"));
    QVERIFY2(error.contains(QStringLiteral("check digit")), qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("First published")), qPrintable(error));
}

void TestDetailPanel::escapeCancelsBackToView()
{
    DetailPanel panel;
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    panel.showBook(excession());
    panel.beginEdit();

    auto* title = child<QLineEdit>(*panel.editor(), QStringLiteral("edit.title"));
    QTRY_VERIFY(title->hasFocus());
    title->setText(QStringLiteral("Not Saved"));
    QTest::keyClick(title, Qt::Key_Escape);

    QVERIFY(panel.state() == DetailPanel::State::Viewing);
    QCOMPARE(labelText(*panel.view(), QStringLiteral("title")), QStringLiteral("Excession"));
}

void TestDetailPanel::editedSynopsisIsMarkedManual()
{
    // AV-001: what the owner types is never enrichment's to replace.
    BookDetail detail = excession();
    detail.book.synopsis = "Fetched text.";
    detail.book.synopsisSource = Source::GoogleBooks;

    DetailPanel panel;
    panel.showBook(detail);
    panel.beginEdit();

    std::optional<Book> saved;
    connect(&panel, &DetailPanel::saveRequested, this,
        [&](const pinax::domain::BookEdit& edit) { saved = edit.book; });
    child<QPlainTextEdit>(*panel.editor(), QStringLiteral("edit.synopsis"))
        ->setPlainText(QStringLiteral("My own words."));
    panel.editor()->save();

    QVERIFY(saved);
    QCOMPARE(saved->synopsis, std::optional<std::string>("My own words."));
    QVERIFY(saved->synopsisSource == Source::Manual);
}

void TestDetailPanel::ratingSquaresChooseAndClear()
{
    DetailPanel panel;
    panel.showBook(excession()); // rated 9
    QSignalSpy requested(&panel, &DetailPanel::ratingRequested);
    auto* bar = child<pinax::ui::RatingBar>(*panel.view(), QStringLiteral("ratingBar"));

    QCOMPARE(bar->ratingAt(QPoint(4, 5)), 1);
    QCOMPARE(bar->ratingAt(QPoint(9 * 11 + 4, 5)), 10);
    QCOMPARE(bar->ratingAt(QPoint(8 * 11 + 4, 5)), 0); // the current rating: clears
    QCOMPARE(bar->ratingAt(QPoint(4, 30)), -1);

    QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, QPoint(2 * 11 + 4, 5));
    QCOMPARE(requested.count(), 1);
    QCOMPARE(requested.at(0).at(0).toLongLong(), qint64(5));
    QCOMPARE(requested.at(0).at(1).toInt(), 3);
}

void TestDetailPanel::authorsAreEditedAsText()
{
    // F-002: the import notation, in cover order, roles in brackets.
    DetailPanel panel;
    panel.showBook(excession());
    panel.beginEdit();

    auto* authors = child<QLineEdit>(*panel.editor(), QStringLiteral("edit.authors"));
    QCOMPARE(authors->text(), QStringLiteral("Iain M. Banks"));

    std::optional<pinax::domain::BookEdit> saved;
    connect(&panel, &DetailPanel::saveRequested, this,
        [&](const pinax::domain::BookEdit& edit) { saved = edit; });

    authors->setText(QStringLiteral("Larry Niven & Jerry Pournelle & Mike Ashley (editor)"));
    panel.editor()->save();
    QVERIFY(saved);
    QCOMPARE(saved->credits.size(), std::size_t(3));
    QCOMPARE(saved->credits[0].name, std::string("Larry Niven"));
    QVERIFY(saved->credits[2].role == pinax::domain::CreditRole::Editor);

    saved.reset();
    authors->setText(QStringLiteral("Somebody (publisher)"));
    panel.editor()->save();
    QVERIFY(!saved);
    QVERIFY(labelText(*panel.editor(), QStringLiteral("edit.error")).contains(QStringLiteral("publisher")));
}

void TestDetailPanel::aNewBookStartsEmptyAndCancelsToNothing()
{
    DetailPanel panel;
    panel.beginNew();
    QVERIFY(panel.state() == DetailPanel::State::Editing);
    QCOMPARE(labelText(*panel.editor(), QStringLiteral("edit.heading")), QStringLiteral("Adding a book"));
    QCOMPARE(child<QLineEdit>(*panel.editor(), QStringLiteral("edit.title"))->text(), QString());

    std::optional<pinax::domain::BookEdit> saved;
    connect(&panel, &DetailPanel::saveRequested, this,
        [&](const pinax::domain::BookEdit& edit) { saved = edit; });
    panel.editor()->save(); // no title
    QVERIFY(!saved);

    child<QLineEdit>(*panel.editor(), QStringLiteral("edit.title"))->setText(QStringLiteral("Ringworld"));
    panel.editor()->save();
    QVERIFY(saved);
    QCOMPARE(saved->book.id, std::int64_t(0));
    QCOMPARE(saved->book.title, std::string("Ringworld"));

    QTest::mouseClick(child<QPushButton>(*panel.editor(), QStringLiteral("edit.cancel")), Qt::LeftButton);
    QVERIFY(panel.state() == DetailPanel::State::Empty);
}

void TestDetailPanel::deletionIsConfirmedInThePanel()
{
    DetailPanel panel;
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    panel.showBook(excession());

    QSignalSpy requested(&panel, &DetailPanel::deleteRequested);
    QTest::mouseClick(child<QPushButton>(*panel.view(), QStringLiteral("delete")), Qt::LeftButton);
    QCOMPARE(requested.count(), 1);
    QCOMPARE(requested.at(0).at(0).value<QList<qint64>>(), QList<qint64>({5}));

    QSignalSpy confirmed(&panel, &DetailPanel::deleteConfirmed);
    QSignalSpy cancelled(&panel, &DetailPanel::dismissed);

    panel.askToDelete({5}, QStringLiteral("Delete “Excession”?"));
    QVERIFY(panel.state() == DetailPanel::State::ConfirmingDelete);
    QVERIFY(panel.isBusy());
    auto* keep = child<QPushButton>(panel, QStringLiteral("confirm.keep"));
    QTRY_VERIFY(keep->hasFocus()); // Enter keeps
    QTest::keyClick(keep, Qt::Key_Escape);
    QCOMPARE(cancelled.count(), 1);
    QCOMPARE(confirmed.count(), 0);

    panel.askToDelete({5, 6}, QStringLiteral("Delete 2 books?"));
    QTest::mouseClick(child<QPushButton>(panel, QStringLiteral("confirm.delete")), Qt::LeftButton);
    QCOMPARE(confirmed.count(), 1);
    QCOMPARE(confirmed.at(0).at(0).value<QList<qint64>>(), QList<qint64>({5, 6}));
}

void TestDetailPanel::placeholdersAreCountedNotNamed_data()
{
    // IMP-005: missing titles given, then the expected line in the panel.
    QTest::addColumn<QStringList>("missing");
    QTest::addColumn<QString>("expected");

    QTest::newRow("all unidentified")
        << QStringList({"Unidentified volume 1", "Unidentified volume 2", "Unidentified volume 3"})
        << "Missing 3, not yet identified";
    QTest::newRow("one unidentified")
        << QStringList({"Later volumes — unidentified"}) << "Missing one volume, not yet identified";
    QTest::newRow("named and unidentified")
        << QStringList({"A Talent for War", "Later volumes — unidentified"})
        << "Missing 2: A Talent for War, and 1 not yet identified";
    QTest::newRow("one named")
        << QStringList({"Consider Phlebas"})
        << "Missing <b>Consider Phlebas</b> — one volume completes this series";
}

void TestDetailPanel::placeholdersAreCountedNotNamed()
{
    QFETCH(QStringList, missing);
    QFETCH(QString, expected);

    BookDetail detail = excession();
    detail.series.front().missing.clear();
    for (const QString& title : missing)
        detail.series.front().missing.push_back({std::nullopt, title.toStdString()});

    DetailPanel panel;
    panel.showBook(detail);
    QCOMPARE(labelText(*panel.view(), QStringLiteral("series.missing")), expected);
}

namespace {

std::vector<pinax::domain::Candidate> twoEditions()
{
    pinax::domain::Candidate first;
    first.title = "Excession";
    first.authors = {"Iain M. Banks"};
    first.publishedYear = 1996;
    first.publisher = "Orbit";
    pinax::domain::Candidate second = first;
    second.publishedYear = 1998;
    second.publisher = "Bantam Spectra";
    second.source = Source::GoogleBooks;
    return {first, second};
}

} // namespace

void TestDetailPanel::fetchAsksForTheBookOnShow()
{
    DetailPanel panel;
    panel.show();
    panel.showBook(excession());
    QSignalSpy asked(&panel, &DetailPanel::fetchRequested);
    auto* fetch = child<QPushButton>(*panel.view(), QStringLiteral("fetch"));
    QVERIFY(fetch->isEnabled());
    QTest::mouseClick(fetch, Qt::LeftButton);
    QCOMPARE(asked.count(), 1);
    QCOMPARE(asked.front().front().toLongLong(), 5);

    panel.beginFetch(QStringLiteral("Looking up…"));
    QCOMPARE(panel.state(), DetailPanel::State::Fetching);
    QVERIFY(panel.isBusy());
    QVERIFY(panel.isEditing()); // nothing redraws over it
    QVERIFY(panel.fetchingBookId() == 5);
    QCOMPARE(labelText(*panel.candidateView(), QStringLiteral("candidates.heading")), QStringLiteral("Excession"));
}

void TestDetailPanel::searchedCandidatesWaitToBeChosen()
{
    DetailPanel panel;
    panel.show();
    // Active, so focus is real: focusing a list makes its first row current.
    panel.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&panel));
    panel.showBook(excession());
    panel.beginFetch(QStringLiteral("Searching…"));
    panel.offerCandidates(twoEditions(), false);
    QApplication::processEvents();

    auto* list = child<QListWidget>(*panel.candidateView(), QStringLiteral("candidates.list"));
    auto* use = child<QPushButton>(*panel.candidateView(), QStringLiteral("candidates.use"));
    QCOMPARE(list->count(), 2);
    QVERIFY(list->item(1)->text().contains(QStringLiteral("Bantam Spectra")));
    QVERIFY(list->item(1)->text().contains(QStringLiteral("Google Books")));
    QVERIFY(labelText(*panel.candidateView(), QStringLiteral("candidates.status")).contains(QStringLiteral("another edition")));
    QCOMPARE(list->currentRow(), -1);
    QVERIFY(list->selectedItems().isEmpty());
    QVERIFY(!use->isEnabled());
    // Focus is not a choice (BUG-004): into the list by keyboard, still none.
    list->setFocus(Qt::TabFocusReason);
    QApplication::processEvents();
    QVERIFY(list->selectedItems().isEmpty());
    QVERIFY(!use->isEnabled());

    QSignalSpy chosen(&panel, &DetailPanel::candidateChosen);
    list->setCurrentRow(1);
    QVERIFY(use->isEnabled());
    QTest::mouseClick(use, Qt::LeftButton);
    QCOMPARE(chosen.count(), 1);
    QCOMPARE(chosen.front().at(0).toLongLong(), 5);
    QCOMPARE(chosen.front().at(1).toInt(), 1);
}

void TestDetailPanel::anIsbnAnswerIsOfferedReadyToUse()
{
    DetailPanel panel;
    panel.show();
    panel.showBook(excession());
    panel.beginFetch(QStringLiteral("Looking up…"));
    panel.offerCandidates({twoEditions().front()}, true);
    QCOMPARE(child<QListWidget>(*panel.candidateView(), QStringLiteral("candidates.list"))->currentRow(), 0);
    QVERIFY(child<QPushButton>(*panel.candidateView(), QStringLiteral("candidates.use"))->isEnabled());

    QSignalSpy cancelled(&panel, &DetailPanel::fetchCancelled);
    QTest::mouseClick(child<QPushButton>(*panel.candidateView(), QStringLiteral("candidates.cancel")), Qt::LeftButton);
    QCOMPARE(cancelled.count(), 1);
    QCOMPARE(panel.state(), DetailPanel::State::Viewing);
}

void TestDetailPanel::candidatesShowTheirCoversAsTheyArrive()
{
    DetailPanel panel;
    panel.show();
    panel.showBook(excession());
    panel.beginFetch(QStringLiteral("Searching…"));
    auto editions = twoEditions();
    editions.front().coverUrl = "https://covers.openlibrary.org/b/id/1-L.jpg";
    panel.offerCandidates(editions, false);

    auto* list = child<QListWidget>(*panel.candidateView(), QStringLiteral("candidates.list"));
    // A frame for each from the start, so the rows do not jump.
    QVERIFY(!list->item(0)->icon().isNull());
    QVERIFY(!list->item(1)->icon().isNull());
    QVERIFY(!list->item(0)->data(Qt::UserRole).toBool());

    QPixmap cover(60, 90);
    cover.fill(Qt::darkCyan);
    panel.setCandidateCover(0, cover);
    QVERIFY(list->item(0)->data(Qt::UserRole).toBool());
    QVERIFY(!list->item(1)->data(Qt::UserRole).toBool());
    panel.setCandidateCover(7, cover); // no such row: ignored
    // A cover is not a choice (BUG-004).
    QVERIFY(list->selectedItems().isEmpty());
}

void TestDetailPanel::aHoveredCoverShowsAtTwiceTheSize()
{
    DetailPanel panel;
    panel.resize(320, 600);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    panel.showBook(excession());
    panel.beginFetch(QStringLiteral("Searching…"));
    auto editions = twoEditions();
    editions.front().coverUrl = "https://covers.openlibrary.org/b/id/1-L.jpg";
    panel.offerCandidates(editions, false);
    QPixmap cover(180, 270); // Open Library's medium size, near enough
    cover.fill(Qt::darkCyan);
    panel.setCandidateCover(0, cover);

    auto* view = panel.candidateView();
    auto* list = child<QListWidget>(*view, QStringLiteral("candidates.list"));
    auto move = [&](const QPoint& at) {
        QMouseEvent event(QEvent::MouseMove, at, list->viewport()->mapToGlobal(at), Qt::NoButton, Qt::NoButton,
            Qt::NoModifier);
        QApplication::sendEvent(list->viewport(), &event);
    };
    const QRect first = list->visualRect(list->model()->index(0, 0));
    const QRect second = list->visualRect(list->model()->index(1, 0));

    // On the thumbnail: the cover, twice the thumbnail's size.
    move(QPoint(first.left() + 20, first.center().y()));
    QVERIFY(view->coverPreview());
    QCOMPARE(view->coverPreview()->pixmap().size(), QSize(96, 144));
    // On the text beside it, or a row with no cover yet: nothing.
    move(QPoint(first.left() + 200, first.center().y()));
    QVERIFY(!view->coverPreview());
    move(QPoint(second.left() + 20, second.center().y()));
    QVERIFY(!view->coverPreview());
    // Leaving the list, or the panel moving on, takes it away.
    move(QPoint(first.left() + 20, first.center().y()));
    QVERIFY(view->coverPreview());
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(list->viewport(), &leave);
    QVERIFY(!view->coverPreview());
    move(QPoint(first.left() + 20, first.center().y()));
    QTest::mouseClick(child<QPushButton>(*view, QStringLiteral("candidates.cancel")), Qt::LeftButton);
    QVERIFY(!view->coverPreview());
    // A preview is not a choice (BUG-004).
    QVERIFY(list->selectedItems().isEmpty());
}

QTEST_MAIN(TestDetailPanel)
#include "test_detail_panel.moc"
