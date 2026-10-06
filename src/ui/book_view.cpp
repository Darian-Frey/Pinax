#include "ui/book_view.h"

#include "domain/placeholder.h"

#include "ui/rating_bar.h"
#include "ui/style.h"

#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace pinax::ui {

using domain::Binding;
using domain::BookDetail;
using domain::ReadStatus;
using domain::SeriesMembership;
using domain::Source;

namespace {

constexpr QSize coverSize(96, 144);

QString text(const std::string& value)
{
    return QString::fromStdString(value);
}

QFrame* makeRule(QWidget* parent)
{
    auto* rule = new QFrame(parent);
    rule->setFrameShape(QFrame::HLine);
    rule->setFrameShadow(QFrame::Plain);
    QPalette palette = rule->palette();
    palette.setColor(QPalette::WindowText, muted(rule).darker(250));
    rule->setPalette(palette);
    return rule;
}

QLabel* makeValue(const QString& name, QWidget* parent)
{
    auto* label = new QLabel(parent);
    label->setObjectName(name);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

void setMuted(QLabel* label, bool isMuted)
{
    QPalette palette = label->parentWidget()->palette();
    if (isMuted)
        palette.setColor(QPalette::WindowText, muted(label));
    label->setPalette(palette);
}

QString readStateName(ReadStatus status)
{
    switch (status) {
    case ReadStatus::Read: return BookView::tr("Read");
    case ReadStatus::Reading: return BookView::tr("Reading");
    case ReadStatus::Abandoned: return BookView::tr("Abandoned");
    case ReadStatus::Unread: break;
    }
    return BookView::tr("Unread");
}

QString timesText(int times)
{
    switch (times) {
    case 1: return BookView::tr("once");
    case 2: return BookView::tr("twice");
    }
    return BookView::tr("%1 times").arg(times);
}

// The note beside the read-state pill, as the mock-up's "read once".
QString readCountText(ReadStatus status, int timesRead)
{
    switch (status) {
    case ReadStatus::Read:
        return BookView::tr("read %1").arg(timesText(std::max(timesRead, 1)));
    case ReadStatus::Reading:
        return timesRead > 0 ? BookView::tr("reading again, read %1 before").arg(timesText(timesRead))
                             : BookView::tr("reading now");
    case ReadStatus::Abandoned:
        return BookView::tr("set aside");
    case ReadStatus::Unread:
        break;
    }
    return timesRead > 0 ? BookView::tr("read %1 before").arg(timesText(timesRead))
                         : BookView::tr("not read yet");
}

QString bindingName(Binding binding)
{
    switch (binding) {
    case Binding::Paperback: return BookView::tr("Paperback");
    case Binding::Hardback: return BookView::tr("Hardback");
    case Binding::Omnibus: return BookView::tr("Omnibus");
    case Binding::Boxset: return BookView::tr("Box set");
    case Binding::Other: break;
    }
    return BookView::tr("Other");
}

QString sourceName(const std::optional<Source>& source)
{
    if (!source)
        return BookView::tr("not fetched");
    switch (*source) {
    case Source::GoogleBooks: return BookView::tr("Google Books");
    case Source::OpenLibrary: return BookView::tr("Open Library");
    case Source::Manual: break;
    }
    return BookView::tr("entered by hand");
}

QString volumeName(const domain::MissingVolume& volume)
{
    if (volume.title)
        return text(*volume.title).toHtmlEscaped();
    if (volume.position)
        return BookView::tr("volume %1").arg(text(*volume.position).toHtmlEscaped());
    return BookView::tr("an unnamed volume");
}

// "Book 5 of 10", or the printed position as it stands when it is not a
// number in the run ("companion"). Never parses the position (AV-006).
QString placeText(const SeriesMembership& series)
{
    if (series.position && series.sortPosition)
        return BookView::tr("Book %1 of %2").arg(text(*series.position)).arg(series.known);
    if (series.position)
        return BookView::tr("%1 · %2 in the series").arg(text(*series.position)).arg(series.known);
    return BookView::tr("%1 in the series").arg(series.known);
}

QString missingText(const SeriesMembership& series)
{
    const auto count = series.missing.size();
    if (count == 0) {
        if (series.status == "Complete to date")
            return BookView::tr("Complete to date — the series is still being written");
        if (series.status == "Complete")
            return BookView::tr("Complete");
        return {};
    }

    // Placeholders are counted, not named: their names say nothing (IMP-005).
    QStringList named;
    std::size_t unidentified = 0;
    for (const auto& volume : series.missing) {
        if (domain::isPlaceholderTitle(volume.title))
            ++unidentified;
        else
            named << volumeName(volume);
    }

    if (named.isEmpty()) {
        return unidentified == 1 ? BookView::tr("Missing one volume, not yet identified")
                                 : BookView::tr("Missing %1, not yet identified").arg(unidentified);
    }
    if (count == 1) {
        return BookView::tr("Missing <b>%1</b> — one volume completes this series").arg(named.front());
    }

    QString list = named.mid(0, 3).join(QStringLiteral(", "));
    if (named.size() > 3)
        list += BookView::tr(" and %1 more").arg(named.size() - 3);
    if (unidentified > 0)
        list += BookView::tr(", and %1 not yet identified").arg(unidentified);
    return BookView::tr("Missing %1: %2").arg(count).arg(list);
}

} // namespace

BookView::BookView(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    // Heading: cover beside title, author, read state and rating.
    auto* heading = new QHBoxLayout;
    heading->setSpacing(12);
    cover_ = new QLabel(this);
    cover_->setObjectName(QStringLiteral("cover"));
    cover_->setFixedSize(coverSize);
    cover_->setAlignment(Qt::AlignCenter);
    cover_->setWordWrap(true);
    cover_->setFrameShape(QFrame::Box);
    heading->addWidget(cover_, 0, Qt::AlignTop);

    auto* facts = new QVBoxLayout;
    facts->setSpacing(4);
    title_ = makeValue(QStringLiteral("title"), this);
    QFont titleFont = title_->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.45);
    titleFont.setBold(true);
    title_->setFont(titleFont);
    subtitle_ = makeValue(QStringLiteral("subtitle"), this);
    authors_ = makeValue(QStringLiteral("authors"), this);
    facts->addWidget(title_);
    facts->addWidget(subtitle_);
    facts->addWidget(authors_);

    auto* state = new QHBoxLayout;
    readState_ = new QLabel(this);
    readState_->setObjectName(QStringLiteral("readState"));
    readState_->setAlignment(Qt::AlignCenter);
    readCount_ = makeValue(QStringLiteral("readCount"), this);
    readCount_->setWordWrap(false);
    state->addWidget(readState_);
    state->addWidget(readCount_);
    state->addStretch();
    facts->addSpacing(4);
    facts->addLayout(state);

    facts->addSpacing(4);
    facts->addWidget(makeSectionHeading(tr("Rating"), this));
    auto* ratingRow = new QHBoxLayout;
    rating_ = new RatingBar(this);
    rating_->setObjectName(QStringLiteral("ratingBar"));
    rating_->setInteractive(true);
    connect(rating_, &RatingBar::ratingChosen, this, &BookView::ratingChosen);
    ratingText_ = makeValue(QStringLiteral("ratingText"), this);
    ratingText_->setWordWrap(false);
    ratingRow->addWidget(rating_);
    ratingRow->addWidget(ratingText_);
    ratingRow->addStretch();
    facts->addLayout(ratingRow);

    auto* edit = new QPushButton(tr("Edit"), this);
    edit->setObjectName(QStringLiteral("edit"));
    edit->setToolTip(tr("Edit this book (F2)"));
    connect(edit, &QPushButton::clicked, this, &BookView::editRequested);
    auto* remove = new QPushButton(tr("Delete"), this);
    remove->setObjectName(QStringLiteral("delete"));
    remove->setToolTip(tr("Delete this book from the catalogue (Delete)"));
    connect(remove, &QPushButton::clicked, this, &BookView::deleteRequested);
    auto* actions = new QHBoxLayout;
    actions->addWidget(edit);
    actions->addWidget(remove);
    actions->addStretch();
    facts->addSpacing(6);
    facts->addLayout(actions);
    facts->addStretch();
    heading->addLayout(facts, 1);
    layout->addLayout(heading);

    // Series: one block per series the book belongs to.
    seriesSection_ = new QWidget(this);
    seriesSection_->setObjectName(QStringLiteral("series"));
    seriesList_ = new QVBoxLayout(seriesSection_);
    seriesList_->setContentsMargins(0, 0, 0, 0);
    seriesList_->setSpacing(8);
    layout->addWidget(makeRule(this));
    layout->addWidget(seriesSection_);

    // Synopsis, with where it came from.
    layout->addWidget(makeRule(this));
    auto* synopsisHeading = new QHBoxLayout;
    synopsisHeading->addWidget(makeSectionHeading(tr("Synopsis"), this));
    synopsisHeading->addStretch();
    synopsisSource_ = makeValue(QStringLiteral("synopsisSource"), this);
    synopsisSource_->setWordWrap(false);
    synopsisHeading->addWidget(synopsisSource_);
    layout->addLayout(synopsisHeading);
    synopsis_ = makeValue(QStringLiteral("synopsis"), this);
    layout->addWidget(synopsis_);
    genres_ = makeValue(QStringLiteral("genres"), this);
    layout->addWidget(genres_);
    fetch_ = new QPushButton(tr("Fetch metadata"), this);
    fetch_->setObjectName(QStringLiteral("fetch"));
    fetch_->setToolTip(tr("Look this book up by its ISBN, or by title and author, and choose what to "
                          "take. Nothing entered by hand is replaced."));
    connect(fetch_, &QPushButton::clicked, this, &BookView::fetchRequested);
    layout->addWidget(fetch_, 0, Qt::AlignLeft);

    // Edition: the facts of this copy.
    layout->addWidget(makeRule(this));
    layout->addWidget(makeSectionHeading(tr("Edition"), this));
    auto* edition = new QGridLayout;
    edition->setHorizontalSpacing(12);
    edition->setVerticalSpacing(4);
    edition->setColumnStretch(1, 1);
    auto addRow = [&](const QString& label, const QString& name) {
        const int row = edition->rowCount();
        auto* caption = new QLabel(label, this);
        setMuted(caption, true);
        edition->addWidget(caption, row, 0, Qt::AlignTop);
        QLabel* value = makeValue(name, this);
        edition->addWidget(value, row, 1);
        return value;
    };
    publisher_ = addRow(tr("Publisher"), QStringLiteral("edition.publisher"));
    published_ = addRow(tr("First published"), QStringLiteral("edition.published"));
    pages_ = addRow(tr("Pages"), QStringLiteral("edition.pages"));
    binding_ = addRow(tr("Binding"), QStringLiteral("edition.binding"));
    isbn_ = addRow(tr("ISBN"), QStringLiteral("edition.isbn"));
    edition_ = addRow(tr("Edition"), QStringLiteral("edition.note"));
    condition_ = addRow(tr("Condition"), QStringLiteral("edition.condition"));
    acquired_ = addRow(tr("Acquired"), QStringLiteral("edition.acquired"));
    layout->addLayout(edition);

    // Notes, only when there are some.
    notesSection_ = new QWidget(this);
    auto* notesLayout = new QVBoxLayout(notesSection_);
    notesLayout->setContentsMargins(0, 0, 0, 0);
    notesLayout->addWidget(makeRule(notesSection_));
    notesLayout->addWidget(makeSectionHeading(tr("Notes"), notesSection_));
    notes_ = makeValue(QStringLiteral("notes"), notesSection_);
    notesLayout->addWidget(notes_);
    layout->addWidget(notesSection_);

    layout->addStretch();
}

void BookView::showBook(const BookDetail& detail)
{
    const domain::Book& book = detail.book;

    if (detail.coverFile) {
        const QPixmap pixmap(QString::fromStdString(*detail.coverFile));
        cover_->setPixmap(pixmap.scaled(coverSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        cover_->setPixmap({});
        cover_->setText(text(book.title).toHtmlEscaped()
            + QStringLiteral("<br><br><small>") + tr("NO COVER YET") + QStringLiteral("</small>"));
    }

    title_->setText(text(book.title));
    subtitle_->setText(book.subtitle ? text(*book.subtitle) : QString());
    subtitle_->setVisible(book.subtitle.has_value());
    authors_->setText(detail.authors ? text(*detail.authors) : tr("No author recorded"));
    setMuted(authors_, !detail.authors);

    readState_->setText(readStateName(book.readStatus));
    const bool read = book.readStatus == ReadStatus::Read;
    readState_->setStyleSheet(read
            ? QStringLiteral("QLabel { background: %1; color: #1A1714; border-radius: 9px; padding: 2px 12px; font-weight: bold; }")
                  .arg(accent().name())
            : QStringLiteral("QLabel { border: 1px solid %1; border-radius: 9px; padding: 1px 11px; }")
                  .arg(muted(this).name(QColor::HexArgb)));
    readCount_->setText(readCountText(book.readStatus, book.timesRead));
    setMuted(readCount_, true);

    rating_->setRating(book.rating);
    ratingText_->setText(book.rating ? tr("%1 / 10").arg(*book.rating) : tr("unrated"));
    setMuted(ratingText_, !book.rating);

    showSeries(detail.series);

    synopsisSource_->setText(sourceName(book.synopsisSource));
    setMuted(synopsisSource_, true);
    if (book.synopsis) {
        synopsis_->setText(text(*book.synopsis));
        setMuted(synopsis_, false);
    } else {
        synopsis_->setText(tr("No synopsis yet. Fetch metadata looks for one, with the cover, on "
                              "Open Library."));
        setMuted(synopsis_, true);
    }

    QStringList genres;
    for (const auto& genre : detail.genres)
        genres << text(genre);
    genres_->setText(tr("Genres: %1").arg(genres.join(QStringLiteral(" · "))));
    genres_->setVisible(!genres.isEmpty());
    setMuted(genres_, true);

    setEditionValue(publisher_, book.publisher);
    setEditionValue(published_,
        book.publishedYear ? std::optional(std::to_string(*book.publishedYear)) : std::nullopt);
    setEditionValue(pages_,
        book.pageCount ? std::optional(std::to_string(*book.pageCount)) : std::nullopt);
    setEditionValue(binding_,
        book.binding ? std::optional(bindingName(*book.binding).toStdString()) : std::nullopt);
    setEditionValue(isbn_, book.isbn13 ? book.isbn13 : book.isbn10);
    setEditionValue(edition_, book.editionNote);
    setEditionValue(condition_, book.conditionNote);
    std::optional<std::string> acquired = book.acquiredDate;
    if (book.acquiredNote)
        acquired = acquired ? *acquired + " — " + *book.acquiredNote : *book.acquiredNote;
    setEditionValue(acquired_, acquired);

    notes_->setText(book.notes ? text(*book.notes) : QString());
    notesSection_->setVisible(book.notes.has_value());
}

void BookView::showSeries(const std::vector<SeriesMembership>& series)
{
    while (QLayoutItem* item = seriesList_->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    seriesList_->addWidget(makeSectionHeading(tr("Series"), seriesSection_));
    if (series.empty()) {
        auto* none = new QLabel(tr("Not part of a series"), seriesSection_);
        none->setObjectName(QStringLiteral("series.none"));
        setMuted(none, true);
        seriesList_->addWidget(none);
        return;
    }

    for (const SeriesMembership& membership : series) {
        auto* block = new QWidget(seriesSection_);
        auto* layout = new QVBoxLayout(block);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);

        auto* nameRow = new QHBoxLayout;
        auto* name = new QLabel(text(membership.name), block);
        name->setObjectName(QStringLiteral("series.name"));
        QPalette namePalette = name->palette();
        namePalette.setColor(QPalette::WindowText, accent());
        name->setPalette(namePalette);
        QFont nameFont = name->font();
        nameFont.setBold(true);
        name->setFont(nameFont);
        auto* place = new QLabel(placeText(membership), block);
        place->setObjectName(QStringLiteral("series.place"));
        nameRow->addWidget(name);
        nameRow->addWidget(place);
        nameRow->addStretch();
        layout->addLayout(nameRow);

        auto* progressRow = new QHBoxLayout;
        auto* progress = new QProgressBar(block);
        progress->setObjectName(QStringLiteral("series.progress"));
        progress->setRange(0, std::max(membership.known, 1));
        progress->setValue(membership.held);
        progress->setTextVisible(false);
        progress->setFixedHeight(5);
        progress->setStyleSheet(QStringLiteral(
            "QProgressBar { border: none; background: %1; border-radius: 2px; }"
            "QProgressBar::chunk { background: %2; border-radius: 2px; }")
                .arg(muted(this).darker(200).name(QColor::HexArgb), accent().name()));
        auto* held = new QLabel(tr("%1 of %2 held").arg(membership.held).arg(membership.known), block);
        held->setObjectName(QStringLiteral("series.held"));
        setMuted(held, true);
        progressRow->addWidget(progress, 1);
        progressRow->addWidget(held);
        layout->addLayout(progressRow);

        const QString missing = missingText(membership);
        if (!missing.isEmpty()) {
            auto* note = new QLabel(missing, block);
            note->setObjectName(QStringLiteral("series.missing"));
            note->setWordWrap(true);
            note->setTextFormat(Qt::RichText);
            if (!membership.missing.empty()) {
                note->setStyleSheet(QStringLiteral(
                    "QLabel { border-left: 3px solid %1; padding: 6px 8px; }")
                        .arg(accent().name()));
            } else {
                setMuted(note, true);
            }
            layout->addWidget(note);
        }
        seriesList_->addWidget(block);
    }
}

void BookView::setEditionValue(QLabel* label, const std::optional<std::string>& value)
{
    label->setText(value ? text(*value) : tr("not recorded"));
    setMuted(label, !value);
}

} // namespace pinax::ui
