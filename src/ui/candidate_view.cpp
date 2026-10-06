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
    return source == Source::GoogleBooks ? CandidateView::tr("Google Books") : CandidateView::tr("Open Library");
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
    more << providerName(candidate.source);

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
    buttons->addWidget(use_);
    buttons->addWidget(cancel_);
    buttons->addStretch();
    layout->addLayout(buttons);

    connect(use_, &QPushButton::clicked, this, &CandidateView::choose);
    connect(list_, &QListWidget::itemActivated, this, &CandidateView::choose);
    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) { use_->setEnabled(row >= 0); });
    connect(cancel_, &QPushButton::clicked, this, &CandidateView::cancelled);
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, &CandidateView::cancelled);
}

void CandidateView::begin(const QString& title, const QString& how)
{
    heading_->setText(title);
    status_->setText(how);
    list_->clear();
    list_->hide();
    use_->hide();
    cancel_->setText(tr("Cancel"));
    cancel_->setFocus();
}

void CandidateView::setProgress(const QString& text)
{
    status_->setText(text);
}

void CandidateView::offer(const std::vector<Candidate>& candidates, bool byIsbn)
{
    if (byIsbn) {
        status_->setText(candidates.size() == 1
                ? tr("The ISBN is known as this. Check it is your book before using it.")
                : tr("The ISBN is known as these. Choose the one that is your book."));
    } else {
        status_->setText(tr("Found by title and author, so any of these may be another edition: "
                            "only the synopsis, first-published year, genres and cover are taken. "
                            "Choose the one that is your book, or cancel."));
    }
    list_->clear();
    for (const Candidate& candidate : candidates)
        list_->addItem(describe(candidate));
    list_->show();
    use_->show();
    cancel_->setText(tr("Cancel"));
    // An ISBN's answer is offered ready to accept; a search's never is.
    list_->setCurrentRow(byIsbn ? 0 : -1);
    use_->setEnabled(byIsbn);
}

void CandidateView::showProblem(const QString& message)
{
    status_->setText(message);
    list_->clear();
    list_->hide();
    use_->hide();
    cancel_->setText(tr("Back"));
    cancel_->setFocus();
}

void CandidateView::focusList()
{
    list_->setFocus();
}

void CandidateView::choose()
{
    const int row = list_->currentRow();
    if (row >= 0 && !list_->isHidden())
        emit chosen(row);
}

} // namespace pinax::ui
