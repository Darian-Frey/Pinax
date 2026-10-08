#include "ui/add_by_isbn_view.h"

#include "domain/isbn.h"
#include "ui/style.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace pinax::ui {

using domain::Candidate;
using domain::SeriesProposal;
using domain::Source;

namespace {

constexpr QSize coverSize(96, 144);

enum Stage { Entry, Waiting, Duplicate, Card, Search };

QString text(const std::string& value)
{
    return QString::fromStdString(value);
}

QLabel* makeNote(QWidget* parent, const QString& name)
{
    auto* label = new QLabel(parent);
    label->setObjectName(name);
    label->setWordWrap(true);
    return label;
}

QString providerName(Source source)
{
    switch (source) {
    case Source::GoogleBooks: return AddByIsbnView::tr("Google Books");
    case Source::BritishLibrary: return AddByIsbnView::tr("the British Library");
    case Source::OpenLibrary:
    case Source::Manual: break;
    }
    return AddByIsbnView::tr("Open Library");
}

QString authorsOf(const Candidate& candidate)
{
    QStringList names;
    for (const auto& name : candidate.authors)
        names << text(name);
    return names.join(QStringLiteral(" & "));
}

// The proposal in two short lines, since a radio button does not wrap:
// "Fills “Consider Phlebas”, volume 1" / "The Culture: 10 of 10 held, complete".
QString describe(const SeriesProposal& proposal)
{
    QString held = AddByIsbnView::tr("%1 of %2 held").arg(proposal.heldAfter).arg(proposal.knownAfter);
    if (proposal.heldAfter == proposal.knownAfter)
        held += AddByIsbnView::tr(", complete");
    const QString series = text(proposal.seriesName);
    if (proposal.kind == SeriesProposal::Kind::FillsMissing) {
        QString volume = proposal.entryTitle ? AddByIsbnView::tr("“%1”").arg(text(*proposal.entryTitle))
                                             : AddByIsbnView::tr("the missing volume");
        if (proposal.position)
            volume += AddByIsbnView::tr(", volume %1").arg(text(*proposal.position));
        return AddByIsbnView::tr("Fills %1\n%2: %3").arg(volume, series, held);
    }
    const QString at = proposal.position ? AddByIsbnView::tr(" as volume %1").arg(text(*proposal.position)) : QString();
    return AddByIsbnView::tr("Joins %1%2\n%1: %3").arg(series, at, held);
}

} // namespace

AddByIsbnView::AddByIsbnView(QWidget* parent)
    : QWidget(parent)
    , stages_(new QStackedWidget(this))
    , heading_(new QLabel(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(8);
    layout->addWidget(makeSectionHeading(tr("Add by ISBN"), this));
    stages_->addWidget(makeEntry());
    stages_->addWidget(makeWaiting());
    stages_->addWidget(makeDuplicate());
    stages_->addWidget(makeCard());
    stages_->addWidget(makeSearch());
    layout->addWidget(stages_, 1);

    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, &AddByIsbnView::cancelled);
}

QWidget* AddByIsbnView::makeEntry()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* intro = makeNote(page, QStringLiteral("add.intro"));
    intro->setText(tr("Type or scan the ISBN from the back cover or the copyright page. Nothing "
                      "is added until you have checked what it finds."));
    setMuted(intro);
    layout->addWidget(intro);
    isbn_ = new QLineEdit(page);
    isbn_->setObjectName(QStringLiteral("add.isbn"));
    isbn_->setPlaceholderText(tr("978-0-575-07801-7"));
    layout->addWidget(isbn_);
    isbnError_ = makeNote(page, QStringLiteral("add.isbnError"));
    isbnError_->setStyleSheet(QStringLiteral("color: #d9534f;"));
    layout->addWidget(isbnError_);
    auto* buttons = new QHBoxLayout;
    auto* look = new QPushButton(tr("Look up"), page);
    look->setObjectName(QStringLiteral("add.lookUp"));
    look->setDefault(true);
    auto* cancel = new QPushButton(tr("Cancel"), page);
    cancel->setObjectName(QStringLiteral("add.cancel"));
    cancel->setAutoDefault(false);
    buttons->addWidget(look);
    buttons->addWidget(cancel);
    buttons->addStretch();
    layout->addLayout(buttons);
    layout->addStretch();
    connect(look, &QPushButton::clicked, this, &AddByIsbnView::lookUp);
    connect(isbn_, &QLineEdit::returnPressed, this, &AddByIsbnView::lookUp);
    connect(cancel, &QPushButton::clicked, this, &AddByIsbnView::cancelled);
    return page;
}

QWidget* AddByIsbnView::makeWaiting()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    waiting_ = makeNote(page, QStringLiteral("add.waiting"));
    layout->addWidget(waiting_);
    auto* cancel = new QPushButton(tr("Cancel"), page);
    cancel->setObjectName(QStringLiteral("add.cancelWaiting"));
    connect(cancel, &QPushButton::clicked, this, &AddByIsbnView::cancelled);
    layout->addWidget(cancel, 0, Qt::AlignLeft);
    layout->addStretch();
    return page;
}

QWidget* AddByIsbnView::makeDuplicate()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    duplicate_ = makeNote(page, QStringLiteral("add.duplicate"));
    layout->addWidget(duplicate_);
    auto* buttons = new QHBoxLayout;
    auto* show = new QPushButton(tr("Show it"), page);
    show->setObjectName(QStringLiteral("add.showExisting"));
    show->setDefault(true);
    auto* again = new QPushButton(tr("Another ISBN"), page);
    again->setObjectName(QStringLiteral("add.again"));
    again->setAutoDefault(false);
    buttons->addWidget(show);
    buttons->addWidget(again);
    buttons->addStretch();
    layout->addLayout(buttons);
    layout->addStretch();
    connect(show, &QPushButton::clicked, this, [this] { emit showBookRequested(duplicateId_); });
    connect(again, &QPushButton::clicked, this, &AddByIsbnView::start);
    return page;
}

QWidget* AddByIsbnView::makeCard()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    candidates_ = new QComboBox(page);
    candidates_->setObjectName(QStringLiteral("add.candidates"));
    // Long titles must not widen the panel.
    candidates_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    candidates_->setMinimumContentsLength(12);
    layout->addWidget(candidates_);
    connect(candidates_, &QComboBox::currentIndexChanged, this, [this] {
        const int index = shownCandidate();
        if (index >= 0)
            showCandidate(index);
        updateAddButton();
    });

    auto* top = new QHBoxLayout;
    cover_ = new QLabel(page);
    cover_->setObjectName(QStringLiteral("add.cover"));
    cover_->setFixedSize(coverSize);
    cover_->setAlignment(Qt::AlignCenter);
    cover_->setWordWrap(true);
    cover_->setFrameShape(QFrame::Box);
    top->addWidget(cover_, 0, Qt::AlignTop);
    auto* fields = new QFormLayout;
    fields->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    title_ = new QLineEdit(page);
    title_->setObjectName(QStringLiteral("add.title"));
    authors_ = new QLineEdit(page);
    authors_->setObjectName(QStringLiteral("add.authors"));
    authors_->setToolTip(tr("As on the cover: 'Larry Niven & Jerry Pournelle', 'Mike Ashley (editor)'"));
    fields->addRow(tr("Title"), title_);
    fields->addRow(tr("Authors"), authors_);
    top->addLayout(fields, 1);
    layout->addLayout(top);
    connect(title_, &QLineEdit::textChanged, this, &AddByIsbnView::updateAddButton);

    facts_ = makeNote(page, QStringLiteral("add.facts"));
    setMuted(facts_);
    layout->addWidget(facts_);

    // Already on the shelf? The backlog was imported without ISBNs, so the
    // book being scanned is most often one already held.
    heldBox_ = new QWidget(page);
    heldBox_->setObjectName(QStringLiteral("add.held"));
    auto* heldLayout = new QVBoxLayout(heldBox_);
    heldLayout->setContentsMargins(0, 0, 0, 0);
    heldLayout->addWidget(makeSectionHeading(tr("Already on your shelf?"), heldBox_));
    heldOptions_ = new QVBoxLayout;
    heldLayout->addLayout(heldOptions_);
    heldGroup_ = new QButtonGroup(this);
    layout->addWidget(heldBox_);
    connect(heldGroup_, &QButtonGroup::idToggled, this, [this](int, bool) {
        const bool existing = givingIsbn();
        title_->setEnabled(!existing);
        authors_->setEnabled(!existing);
        for (QWidget* part : {static_cast<QWidget*>(seriesBox_), static_cast<QWidget*>(seriesHeading_),
                 static_cast<QWidget*>(shelfHeading_), static_cast<QWidget*>(unread_), static_cast<QWidget*>(read_)})
            part->setVisible(!existing);
        placement_->setVisible(!existing && seriesGroup_->checkedId() >= 0
            && seriesGroup_->checkedId() < static_cast<int>(proposals_.size())
            && proposals_[static_cast<std::size_t>(seriesGroup_->checkedId())].kind == SeriesProposal::Kind::JoinsSeries);
        add_->setText(existing ? tr("Give it this ISBN") : tr("Add to catalogue"));
    });

    seriesBox_ = new QWidget(page);
    seriesBox_->setObjectName(QStringLiteral("add.series"));
    seriesOptions_ = new QVBoxLayout(seriesBox_);
    seriesOptions_->setContentsMargins(0, 0, 0, 0);
    seriesGroup_ = new QButtonGroup(this);
    seriesHeading_ = makeSectionHeading(tr("Series"), page);
    layout->addWidget(seriesHeading_);
    layout->addWidget(seriesBox_);
    placement_ = new QWidget(page);
    auto* placement = new QFormLayout(placement_);
    placement->setContentsMargins(20, 0, 0, 0);
    position_ = new QLineEdit(placement_);
    position_->setObjectName(QStringLiteral("add.position"));
    sortPosition_ = new QDoubleSpinBox(placement_);
    sortPosition_->setObjectName(QStringLiteral("add.sortPosition"));
    sortPosition_->setRange(0, 100000);
    sortPosition_->setDecimals(2);
    placement->addRow(tr("Position"), position_);
    placement->addRow(tr("Sort number"), sortPosition_);
    layout->addWidget(placement_);
    connect(seriesGroup_, &QButtonGroup::idToggled, this, [this](int, bool) {
        const int chosen = seriesGroup_->checkedId();
        placement_->setVisible(chosen >= 0 && chosen < static_cast<int>(proposals_.size())
            && proposals_[static_cast<std::size_t>(chosen)].kind == SeriesProposal::Kind::JoinsSeries);
    });

    shelfHeading_ = makeSectionHeading(tr("On the shelf"), page);
    layout->addWidget(shelfHeading_);
    auto* shelf = new QHBoxLayout;
    unread_ = new QRadioButton(tr("Unread"), page);
    unread_->setObjectName(QStringLiteral("add.unread"));
    read_ = new QRadioButton(tr("Read"), page);
    read_->setObjectName(QStringLiteral("add.read"));
    unread_->setChecked(true);
    shelf->addWidget(unread_);
    shelf->addWidget(read_);
    shelf->addStretch();
    layout->addLayout(shelf);

    cardError_ = makeNote(page, QStringLiteral("add.error"));
    cardError_->setStyleSheet(QStringLiteral("color: #d9534f;"));
    layout->addWidget(cardError_);

    auto* buttons = new QHBoxLayout;
    add_ = new QPushButton(tr("Add to catalogue"), page);
    add_->setObjectName(QStringLiteral("add.add"));
    add_->setDefault(true);
    auto* notThis = new QPushButton(tr("Not this book"), page);
    notThis->setObjectName(QStringLiteral("add.notThis"));
    notThis->setAutoDefault(false);
    auto* cancel = new QPushButton(tr("Cancel"), page);
    cancel->setObjectName(QStringLiteral("add.cancelCard"));
    cancel->setAutoDefault(false);
    buttons->addWidget(add_);
    buttons->addWidget(notThis);
    buttons->addWidget(cancel);
    buttons->addStretch();
    layout->addLayout(buttons);
    layout->addStretch();
    connect(add_, &QPushButton::clicked, this, &AddByIsbnView::add);
    connect(notThis, &QPushButton::clicked, this, [this] { emit notThisBook(std::max(shownCandidate(), 0)); });
    connect(cancel, &QPushButton::clicked, this, &AddByIsbnView::cancelled);
    return page;
}

QWidget* AddByIsbnView::makeSearch()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    searchMessage_ = makeNote(page, QStringLiteral("add.searchMessage"));
    layout->addWidget(searchMessage_);
    auto* fields = new QFormLayout;
    searchTitle_ = new QLineEdit(page);
    searchTitle_->setObjectName(QStringLiteral("add.searchTitle"));
    searchAuthor_ = new QLineEdit(page);
    searchAuthor_->setObjectName(QStringLiteral("add.searchAuthor"));
    fields->addRow(tr("Title"), searchTitle_);
    fields->addRow(tr("Author"), searchAuthor_);
    layout->addLayout(fields);
    auto* buttons = new QHBoxLayout;
    auto* search = new QPushButton(tr("Search"), page);
    search->setObjectName(QStringLiteral("add.search"));
    search->setDefault(true);
    auto* manual = new QPushButton(tr("Enter by hand"), page);
    manual->setObjectName(QStringLiteral("add.manual"));
    manual->setAutoDefault(false);
    auto* cancel = new QPushButton(tr("Cancel"), page);
    cancel->setObjectName(QStringLiteral("add.cancelSearch"));
    cancel->setAutoDefault(false);
    buttons->addWidget(search);
    buttons->addWidget(manual);
    buttons->addWidget(cancel);
    buttons->addStretch();
    layout->addLayout(buttons);
    layout->addStretch();
    connect(search, &QPushButton::clicked, this, [this] {
        if (!searchTitle_->text().trimmed().isEmpty())
            emit searchRequested(searchTitle_->text().trimmed(), searchAuthor_->text().trimmed());
    });
    connect(manual, &QPushButton::clicked, this, &AddByIsbnView::manualRequested);
    connect(cancel, &QPushButton::clicked, this, &AddByIsbnView::cancelled);
    return page;
}

void AddByIsbnView::start()
{
    isbn_->clear();
    isbnError_->clear();
    isbn13_.clear();
    isbn10_.reset();
    shown_.clear();
    stages_->setCurrentIndex(Entry);
    isbn_->setFocus();
}

void AddByIsbnView::focusIsbn()
{
    isbn_->setFocus();
}

void AddByIsbnView::lookUp()
{
    const std::string typed = domain::normaliseIsbn(isbn_->text().trimmed().toUpper().toStdString());
    if (domain::isValidIsbn13(typed)) {
        isbn13_ = typed;
        isbn10_ = domain::isbn13To10(typed);
    } else if (domain::isValidIsbn10(typed)) {
        isbn13_ = domain::isbn10To13(typed);
        isbn10_ = typed;
    } else {
        isbnError_->setText(typed.size() == 13 || typed.size() == 10
                ? tr("That is not a valid ISBN: its check digit does not match. A digit may be "
                     "mistyped or misread.")
                : tr("An ISBN has 13 digits, or 10 on older books."));
        return;
    }
    isbnError_->clear();
    emit lookupRequested(text(isbn13_));
}

void AddByIsbnView::showWaiting(const QString& message)
{
    waiting_->setText(message);
    stages_->setCurrentIndex(Waiting);
}

void AddByIsbnView::showDuplicate(qint64 bookId, const QString& title)
{
    duplicateId_ = bookId;
    duplicate_->setText(tr("ISBN %1 is already in your catalogue, as “%2”. It is not added twice.")
                            .arg(text(isbn13_), title));
    stages_->setCurrentIndex(Duplicate);
}

void AddByIsbnView::showCandidates(const std::vector<Candidate>& candidates, bool byIsbn)
{
    shown_ = candidates;
    byIsbn_ = byIsbn;
    cardError_->clear();
    unread_->setChecked(true);
    {
        const QSignalBlocker blocker(candidates_);
        candidates_->clear();
        if (!byIsbn)
            candidates_->addItem(tr("Found by title and author — choose the one that is your book"));
        for (const Candidate& candidate : candidates) {
            QString item = text(candidate.title);
            if (!candidate.authors.empty())
                item += QStringLiteral(" — ") + authorsOf(candidate);
            if (const auto year = candidate.publishedYear ? candidate.publishedYear : candidate.firstPublishedYear)
                item += QStringLiteral(", %1").arg(*year);
            candidates_->addItem(item);
        }
        candidates_->setCurrentIndex(0);
    }
    candidates_->setVisible(!byIsbn || candidates.size() > 1);
    stages_->setCurrentIndex(Card);
    if (byIsbn) {
        showCandidate(0);
        title_->setFocus();
    } else {
        // Nothing chosen: the card stays empty until the owner picks.
        title_->clear();
        authors_->clear();
        facts_->setText(tr("Searching by title and author finds works, not your edition, so the "
                           "ISBN you typed is kept and only the synopsis, first-published year, "
                           "genres and cover are taken."));
        cover_->setPixmap({});
        cover_->setText(QString());
        setCardDetails({}, {});
        candidates_->setFocus();
    }
    updateAddButton();
}

int AddByIsbnView::shownCandidate() const
{
    const int index = candidates_->currentIndex() - (byIsbn_ ? 0 : 1);
    return index >= 0 && index < static_cast<int>(shown_.size()) ? index : -1;
}

QString AddByIsbnView::searchedTitle() const
{
    return searchTitle_->text().trimmed();
}

QString AddByIsbnView::searchedAuthor() const
{
    return searchAuthor_->text().trimmed();
}

void AddByIsbnView::showCandidate(int index)
{
    const Candidate& candidate = shown_[static_cast<std::size_t>(index)];
    title_->setText(text(candidate.title));
    authors_->setText(authorsOf(candidate));

    QStringList facts;
    if (candidate.subtitle)
        facts << text(*candidate.subtitle);
    if (candidate.publisher)
        facts << text(*candidate.publisher);
    if (candidate.publishedYear)
        facts << tr("this edition %1").arg(*candidate.publishedYear);
    if (candidate.firstPublishedYear)
        facts << tr("first published %1").arg(*candidate.firstPublishedYear);
    if (candidate.pageCount)
        facts << tr("%1 pages").arg(*candidate.pageCount);
    if (candidate.seriesName) {
        facts << (candidate.seriesNumber ? tr("series: %1, %2").arg(text(*candidate.seriesName), text(*candidate.seriesNumber))
                                         : tr("series: %1").arg(text(*candidate.seriesName)));
    }
    QString from = tr("from %1").arg(providerName(candidate.source));
    if (candidate.filledFrom)
        from += tr(" and %1").arg(providerName(*candidate.filledFrom));
    facts << from;
    QString line = facts.join(QStringLiteral(" · "));
    if (!byIsbn_)
        line += QStringLiteral("\n") + tr("Found by title and author: your ISBN is kept, and only the "
                                          "synopsis, first-published year, genres and cover are taken.");
    facts_->setText(line);

    cover_->setPixmap({});
    cover_->setText(candidate.coverUrl ? tr("cover…") : tr("NO COVER"));
    emit candidateShown(index);
}

void AddByIsbnView::setCardDetails(const std::vector<domain::NamedCredit>& credits,
    const std::vector<SeriesProposal>& proposals, const std::vector<HeldBook>& held)
{
    held_ = held;
    for (auto* button : heldGroup_->buttons()) {
        heldGroup_->removeButton(button);
        delete button;
    }
    while (QLayoutItem* item = heldOptions_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (std::size_t i = 0; i < held.size(); ++i) {
        auto* option = new QRadioButton(tr("Yes — give my copy this ISBN:\n%1").arg(held[i].description), heldBox_);
        option->setObjectName(QStringLiteral("add.held.%1").arg(i));
        heldGroup_->addButton(option, static_cast<int>(i));
        heldOptions_->addWidget(option);
    }
    if (!held.empty()) {
        auto* another = new QRadioButton(tr("No — add it as another book"), heldBox_);
        another->setObjectName(QStringLiteral("add.held.none"));
        heldGroup_->addButton(another, -2);
        heldOptions_->addWidget(another);
    }
    heldBox_->setVisible(!held.empty());

    if (!credits.empty())
        authors_->setText(text(domain::formatCredits(credits)));

    proposals_ = proposals;
    for (auto* button : seriesGroup_->buttons()) {
        seriesGroup_->removeButton(button);
        delete button;
    }
    while (QLayoutItem* item = seriesOptions_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (std::size_t i = 0; i < proposals.size(); ++i) {
        auto* option = new QRadioButton(describe(proposals[i]), seriesBox_);
        option->setObjectName(QStringLiteral("add.series.%1").arg(i));
        seriesGroup_->addButton(option, static_cast<int>(i));
        seriesOptions_->addWidget(option);
    }
    auto* none = new QRadioButton(proposals.empty() ? tr("No series of yours matches") : tr("Not in a series"),
        seriesBox_);
    none->setObjectName(QStringLiteral("add.series.none"));
    seriesGroup_->addButton(none, -2);
    seriesOptions_->addWidget(none);
    // Proposed, not typed: the first proposal is chosen, and can be declined.
    (proposals.empty() ? none : seriesGroup_->button(0))->setChecked(true);
    none->setEnabled(true);

    const SeriesProposal* joining = nullptr;
    for (const auto& proposal : proposals) {
        if (proposal.kind == SeriesProposal::Kind::JoinsSeries) {
            joining = &proposal;
            break;
        }
    }
    position_->setText(joining && joining->position ? text(*joining->position) : QString());
    sortPosition_->setValue(joining && joining->sortPosition ? *joining->sortPosition : 0);
    placement_->setVisible(!proposals.empty() && proposals.front().kind == SeriesProposal::Kind::JoinsSeries);
    // Most likely the copy already held: that answer is chosen first.
    if (!held.empty())
        heldGroup_->button(0)->setChecked(true);
    else
        add_->setText(tr("Add to catalogue"));
    for (QWidget* part : {static_cast<QWidget*>(title_), static_cast<QWidget*>(authors_)})
        part->setEnabled(!givingIsbn());
}

bool AddByIsbnView::givingIsbn() const
{
    const int chosen = heldGroup_->checkedId();
    return chosen >= 0 && chosen < static_cast<int>(held_.size());
}

void AddByIsbnView::setCover(const QPixmap& cover)
{
    cover_->setPixmap(cover.scaled(coverSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void AddByIsbnView::showSearch(const QString& message, const QString& title, const QString& author)
{
    searchMessage_->setText(message);
    searchTitle_->setText(title);
    searchAuthor_->setText(author);
    stages_->setCurrentIndex(Search);
    searchTitle_->setFocus();
}

void AddByIsbnView::showError(const QString& message)
{
    cardError_->setText(message);
}

void AddByIsbnView::updateAddButton()
{
    add_->setEnabled(shownCandidate() >= 0 && !title_->text().trimmed().isEmpty());
}

void AddByIsbnView::add()
{
    const int index = shownCandidate();
    if (index < 0)
        return;
    Choice choice;
    choice.candidate = index;
    if (givingIsbn()) {
        choice.existingBook = held_[static_cast<std::size_t>(heldGroup_->checkedId())].id;
        emit addRequested(choice);
        return;
    }
    choice.title = title_->text().trimmed().toStdString();
    choice.subtitle = shown_[static_cast<std::size_t>(index)].subtitle;
    try {
        choice.credits = domain::parseCredits(authors_->text().trimmed().toStdString());
    } catch (const domain::CreditTextError& error) {
        cardError_->setText(tr("Authors: %1").arg(QString::fromUtf8(error.what())));
        return;
    }
    choice.readStatus = read_->isChecked() ? domain::ReadStatus::Read : domain::ReadStatus::Unread;
    const int chosen = seriesGroup_->checkedId();
    if (chosen >= 0 && chosen < static_cast<int>(proposals_.size())) {
        SeriesProposal proposal = proposals_[static_cast<std::size_t>(chosen)];
        if (proposal.kind == SeriesProposal::Kind::JoinsSeries) {
            const QString position = position_->text().trimmed();
            if (position.isEmpty()) {
                cardError_->setText(tr("A new volume of %1 needs a position.").arg(text(proposal.seriesName)));
                return;
            }
            proposal.position = position.toStdString();
            proposal.sortPosition = sortPosition_->value();
        }
        choice.series = proposal;
    }
    emit addRequested(choice);
}

} // namespace pinax::ui
