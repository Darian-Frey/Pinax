#include "ui/candidate_view.h"

#include "ui/style.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

namespace pinax::ui {

using domain::Candidate;
using domain::Source;

namespace {

QString text(const std::string& value)
{
    return QString::fromStdString(value);
}

QString providerName(Source source)
{
    switch (source) {
    case Source::GoogleBooks: return CandidateView::tr("Google Books");
    case Source::BritishLibrary: return CandidateView::tr("British Library");
    case Source::OpenLibrary:
    case Source::Manual: break;
    }
    return CandidateView::tr("Open Library");
}

// Three lines: title, who and when, and what else is known.
QString describe(const Candidate& candidate)
{
    QString title = text(candidate.title);
    if (candidate.subtitle)
        title += QStringLiteral(": ") + text(*candidate.subtitle);

    QStringList who;
    if (!candidate.authors.empty()) {
        QStringList authors;
        for (const auto& author : candidate.authors)
            authors << text(author);
        who << authors.join(QStringLiteral(" & "));
    }
    if (const auto year = candidate.publishedYear ? candidate.publishedYear : candidate.firstPublishedYear)
        who << QString::number(*year);
    if (candidate.publisher)
        who << text(*candidate.publisher);

    QStringList more;
    if (candidate.pageCount)
        more << CandidateView::tr("%1 pages").arg(*candidate.pageCount);
    if (candidate.isbn13)
        more << CandidateView::tr("ISBN %1").arg(text(*candidate.isbn13));
    if (candidate.description)
        more << CandidateView::tr("synopsis");
    if (candidate.coverUrl)
        more << CandidateView::tr("cover");
    QString from = providerName(candidate.source);
    if (candidate.filledFrom)
        from += QStringLiteral(" + ") + providerName(*candidate.filledFrom);
    more << from;

    QString line2 = who.join(QStringLiteral(" · "));
    if (line2.isEmpty())
        line2 = CandidateView::tr("no author or date given");
    return title + QLatin1Char('\n') + line2 + QLatin1Char('\n') + more.join(QStringLiteral(" · "));
}

} // namespace

CandidateView::CandidateView(QWidget* parent)
    : QWidget(parent)
    , heading_(new QLabel(this))
    , status_(new QLabel(this))
    , list_(new QListWidget(this))
    , use_(new QPushButton(tr("Use this"), this))
    , cancel_(new QPushButton(tr("Cancel"), this))
    , reject_(new QPushButton(tr("Not my book"), this))
    , skip_(new QPushButton(tr("Skip"), this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(8);

    layout->addWidget(makeSectionHeading(tr("Fetch metadata"), this));
    heading_->setObjectName(QStringLiteral("candidates.heading"));
    heading_->setWordWrap(true);
    QFont headingFont = heading_->font();
    headingFont.setBold(true);
    heading_->setFont(headingFont);
    layout->addWidget(heading_);

    status_->setObjectName(QStringLiteral("candidates.status"));
    status_->setWordWrap(true);
    QPalette palette = status_->palette();
    palette.setColor(QPalette::WindowText, muted(status_));
    status_->setPalette(palette);
    layout->addWidget(status_);

    list_->setObjectName(QStringLiteral("candidates.list"));
    list_->setWordWrap(true);
    list_->setSpacing(4);
    layout->addWidget(list_, 1);

    auto* buttons = new QHBoxLayout;
    use_->setObjectName(QStringLiteral("candidates.use"));
    use_->setDefault(true);
    cancel_->setObjectName(QStringLiteral("candidates.cancel"));
    cancel_->setAutoDefault(false);
    reject_->setObjectName(QStringLiteral("candidates.reject"));
    reject_->setAutoDefault(false);
    reject_->setToolTip(tr("None of these is this book. It is marked as not found and later runs "
                           "leave it alone; Fetch metadata can still try it."));
    skip_->setObjectName(QStringLiteral("candidates.skip"));
    skip_->setAutoDefault(false);
    skip_->setToolTip(tr("Decide later: it goes to the end of the queue."));
    buttons->addWidget(use_);
    buttons->addWidget(reject_);
    buttons->addWidget(skip_);
    buttons->addStretch();
    layout->addLayout(buttons);
    auto* leave = new QHBoxLayout;
    leave->addWidget(cancel_);
    leave->addStretch();
    layout->addLayout(leave);
    reject_->hide();
    skip_->hide();
    connect(reject_, &QPushButton::clicked, this, &CandidateView::rejected);
    connect(skip_, &QPushButton::clicked, this, &CandidateView::skipped);

    connect(use_, &QPushButton::clicked, this, &CandidateView::choose);
    connect(list_, &QListWidget::itemActivated, this, &CandidateView::choose);
    // Use this follows the selection, not the current row: focus alone makes
    // a list's first row current, which must not count as a choice (BUG-004,
    // AV-010).
    connect(list_, &QListWidget::itemSelectionChanged, this,
        [this] { use_->setEnabled(!list_->selectedItems().isEmpty()); });
    connect(cancel_, &QPushButton::clicked, this, &CandidateView::cancelled);
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, &CandidateView::cancelled);
}

void CandidateView::begin(const QString& title, const QString& how)
{
    reviewing_ = false;
    place_.clear();
    heading_->setText(title);
    status_->setText(how);
    list_->clear();
    list_->hide();
    use_->hide();
    reject_->hide();
    skip_->hide();
    cancel_->setText(tr("Cancel"));
    cancel_->setFocus();
}

void CandidateView::beginReview(const QString& title, const QString& place)
{
    begin(title, place);
    reviewing_ = true;
    place_ = place;
}

void CandidateView::setProgress(const QString& text)
{
    status_->setText(text);
}

void CandidateView::offer(const std::vector<Candidate>& candidates, bool byIsbn)
{
    const QString place = reviewing_ ? place_ + QStringLiteral(". ") : QString();
    if (byIsbn) {
        status_->setText(place + (candidates.size() == 1
                ? tr("The ISBN is known as this. Check it is your book before using it.")
                : tr("The ISBN is known as these. Choose the one that is your book.")));
    } else {
        status_->setText(place + (reviewing_ ? tr("Found by title and author, so any of these may be another edition: "
                                         "only the synopsis, first-published year, genres and cover are "
                                         "taken. Choose the one that is your book.")
                                    : tr("Found by title and author, so any of these may be another edition: "
                                         "only the synopsis, first-published year, genres and cover are "
                                         "taken. Choose the one that is your book, or cancel.")));
    }
    list_->clear();
    for (const Candidate& candidate : candidates)
        list_->addItem(describe(candidate));
    list_->show();
    use_->show();
    reject_->setVisible(reviewing_);
    skip_->setVisible(reviewing_);
    cancel_->setText(reviewing_ ? tr("Stop reviewing") : tr("Cancel"));
    // An ISBN's answer is offered ready to accept; a search's never is, and
    // focus goes elsewhere so the keyboard cannot choose one by accident.
    list_->clearSelection();
    list_->setCurrentRow(byIsbn ? 0 : -1);
    use_->setEnabled(byIsbn);
    if (byIsbn)
        list_->setFocus();
    else if (reviewing_)
        skip_->setFocus();
    else
        cancel_->setFocus();
}

void CandidateView::showProblem(const QString& message)
{
    status_->setText(message);
    list_->clear();
    list_->hide();
    use_->hide();
    reject_->hide();
    skip_->hide();
    cancel_->setText(tr("Back"));
    cancel_->setFocus();
}

void CandidateView::focusList()
{
    list_->setFocus();
}

void CandidateView::choose()
{
    const auto selected = list_->selectedItems();
    if (selected.isEmpty() || list_->isHidden())
        return;
    emit chosen(list_->row(selected.front()));
}

} // namespace pinax::ui
