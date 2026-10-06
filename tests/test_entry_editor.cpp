#include "ui/attach_view.h"
#include "ui/entry_editor.h"
#include "ui/series_view.h"

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

using pinax::domain::BookSummary;
using pinax::domain::SeriesEntry;
using pinax::domain::SeriesRow;
using pinax::ui::AttachView;
using pinax::ui::EntryEditor;
using pinax::ui::SeriesView;

namespace {

template <typename T>
T* child(const QWidget& root, const QString& name)
{
    T* found = root.findChild<T*>(name);
    if (!found)
        qFatal("no child named %s", qPrintable(name));
    return found;
}

BookSummary summary(std::int64_t id, std::string title, std::string authors)
{
    BookSummary book;
    book.id = id;
    book.title = std::move(title);
    book.sortTitle = book.title;
    book.authors = std::move(authors);
    return book;
}

SeriesRow missingVolume()
{
    SeriesRow row;
    row.entryId = 10;
    row.position = "1";
    row.entryTitle = "Consider Phlebas";
    return row;
}

} // namespace

class TestEntryEditor : public QObject {
    Q_OBJECT

private slots:
    void editsAnEntry();
    void sortNumberIsEnteredNotDerived();
    void refusesAnEntryWithNothingToNameIt();
    void aNewEntryCannotBeRemoved();
    void attachFiltersAndAttaches();
    void attachChoosesNothingUntilAsked();
    void attachOffersANewBook();
    void cardActsOnTheSelectedVolume();
};

void TestEntryEditor::editsAnEntry()
{
    SeriesEntry entry;
    entry.id = 10;
    entry.seriesId = 1;
    entry.position = "6";
    entry.sortPosition = 6;
    entry.title = "Inversions";

    EntryEditor editor;
    editor.editEntry(entry, QStringLiteral("The Culture"), QStringLiteral("Inversions"));
    QCOMPARE(child<QLabel>(editor, QStringLiteral("entry.heading"))->text(), QStringLiteral("A volume of The Culture"));
    QCOMPARE(child<QLabel>(editor, QStringLiteral("entry.owned"))->text(), QStringLiteral("On the shelf: Inversions"));

    std::optional<SeriesEntry> saved;
    connect(&editor, &EntryEditor::saveRequested, this, [&](const SeriesEntry& e) { saved = e; });
    child<QLineEdit>(editor, QStringLiteral("entry.position"))->setText(QStringLiteral(" 6.5 "));
    child<QLineEdit>(editor, QStringLiteral("entry.sortPosition"))->setText(QStringLiteral("6.5"));
    editor.save();

    QVERIFY(saved);
    QCOMPARE(saved->id, std::int64_t(10));
    QCOMPARE(saved->position, std::optional<std::string>("6.5"));
    QCOMPARE(saved->sortPosition, std::optional<double>(6.5));
    QCOMPARE(saved->title, std::optional<std::string>("Inversions"));
}

void TestEntryEditor::sortNumberIsEnteredNotDerived()
{
    // AV-006: changing the position leaves the sort number as typed.
    SeriesEntry entry;
    entry.id = 10;
    entry.position = "3";
    entry.sortPosition = 3;

    EntryEditor editor;
    editor.editEntry(entry, QStringLiteral("S"), QString());
    std::optional<SeriesEntry> saved;
    connect(&editor, &EntryEditor::saveRequested, this, [&](const SeriesEntry& e) { saved = e; });

    child<QLineEdit>(editor, QStringLiteral("entry.position"))->setText(QStringLiteral("Broadcast 9"));
    editor.save();
    QCOMPARE(saved->sortPosition, std::optional<double>(3));

    child<QLineEdit>(editor, QStringLiteral("entry.sortPosition"))->clear();
    editor.save();
    QVERIFY(!saved->sortPosition); // sorts last

    saved.reset();
    child<QLineEdit>(editor, QStringLiteral("entry.sortPosition"))->setText(QStringLiteral("nine"));
    editor.save();
    QVERIFY(!saved);
    QVERIFY(child<QLabel>(editor, QStringLiteral("entry.error"))->text().contains(QStringLiteral("number")));
}

void TestEntryEditor::refusesAnEntryWithNothingToNameIt()
{
    EntryEditor editor;
    editor.editEntry(SeriesEntry {}, QStringLiteral("S"), QString());
    QSignalSpy saved(&editor, &EntryEditor::saveRequested);
    editor.save();
    QCOMPARE(saved.count(), 0);
    QVERIFY(child<QLabel>(editor, QStringLiteral("entry.error"))->text().contains(QStringLiteral("position or a title")));
}

void TestEntryEditor::aNewEntryCannotBeRemoved()
{
    EntryEditor editor;
    editor.show();
    editor.editEntry(SeriesEntry {}, QStringLiteral("The Culture"), QString());
    QCOMPARE(child<QLabel>(editor, QStringLiteral("entry.heading"))->text(), QStringLiteral("New volume in The Culture"));
    QVERIFY(!child<QPushButton>(editor, QStringLiteral("entry.remove"))->isVisible());

    SeriesEntry existing;
    existing.id = 4;
    existing.title = "Matter";
    editor.editEntry(existing, QStringLiteral("The Culture"), QString());
    QVERIFY(child<QPushButton>(editor, QStringLiteral("entry.remove"))->isVisible());
    QSignalSpy remove(&editor, &EntryEditor::removeRequested);
    child<QPushButton>(editor, QStringLiteral("entry.remove"))->click();
    QCOMPARE(remove.at(0).at(0).toLongLong(), qint64(4));
}

void TestEntryEditor::attachFiltersAndAttaches()
{
    AttachView view;
    view.offer(missingVolume(), QStringLiteral("The Culture"),
        {summary(1, "Tau Zero", "Poul Anderson"), summary(2, "Consider Phlebas", "Iain M. Banks")});

    // Starts on the volume's own title, which is held here.
    auto* list = child<QListWidget>(view, QStringLiteral("attach.candidates"));
    QCOMPARE(list->currentItem()->text(), QStringLiteral("Consider Phlebas — Iain M. Banks"));

    QSignalSpy attach(&view, &AttachView::attachRequested);
    child<QPushButton>(view, QStringLiteral("attach.attach"))->click();
    QCOMPARE(attach.count(), 1);
    QCOMPARE(attach.at(0).at(0).toLongLong(), qint64(10));
    QCOMPARE(attach.at(0).at(1).toLongLong(), qint64(2));

    child<QLineEdit>(view, QStringLiteral("attach.search"))->setText(QStringLiteral("anderson"));
    QCOMPARE(list->currentItem()->text(), QStringLiteral("Tau Zero — Poul Anderson"));
    QVERIFY(list->item(0)->isHidden() != list->item(1)->isHidden());
}

void TestEntryEditor::attachChoosesNothingUntilAsked()
{
    // The title matches no book: every candidate shows, none is chosen.
    AttachView view;
    view.offer(missingVolume(), QStringLiteral("The Culture"), {summary(1, "Tau Zero", "Poul Anderson")});
    QCOMPARE(child<QLineEdit>(view, QStringLiteral("attach.search"))->text(), QString());
    QVERIFY(!child<QListWidget>(view, QStringLiteral("attach.candidates"))->item(0)->isHidden());
    QVERIFY(!child<QPushButton>(view, QStringLiteral("attach.attach"))->isEnabled());
}

void TestEntryEditor::attachOffersANewBook()
{
    AttachView view;
    view.offer(missingVolume(), QStringLiteral("The Culture"), {});
    QSignalSpy create(&view, &AttachView::createRequested);
    child<QPushButton>(view, QStringLiteral("attach.create"))->click();
    QCOMPARE(create.at(0).at(0).toLongLong(), qint64(10));
}

void TestEntryEditor::cardActsOnTheSelectedVolume()
{
    pinax::domain::SeriesDetail detail;
    detail.series.name = "The Culture";
    detail.series.known = 10;
    detail.series.held = 9;

    SeriesView view;
    view.show();
    view.showSeries(detail);
    QVERIFY(!child<QWidget>(view, QStringLiteral("seriesView.card"))->isVisible());

    view.showSeries(detail, missingVolume());
    QVERIFY(child<QWidget>(view, QStringLiteral("seriesView.card"))->isVisible());
    QCOMPARE(child<QLabel>(view, QStringLiteral("seriesView.cardTitle"))->text(),
        QStringLiteral("<b>Consider Phlebas</b> · 1 · not owned"));

    QSignalSpy owned(&view, &SeriesView::markOwnedRequested);
    QSignalSpy edit(&view, &SeriesView::editEntryRequested);
    child<QPushButton>(view, QStringLiteral("seriesView.markOwned"))->click();
    child<QPushButton>(view, QStringLiteral("seriesView.editEntry"))->click();
    QCOMPARE(owned.at(0).at(0).toLongLong(), qint64(10));
    QCOMPARE(edit.at(0).at(0).toLongLong(), qint64(10));
}

QTEST_MAIN(TestEntryEditor)
#include "test_entry_editor.moc"
