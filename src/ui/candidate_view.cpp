#include "ui/candidate_view.h"

#include "ui/style.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QVBoxLayout>

#include <algorithm>

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
    if (candidate.pageCount) {
        more << (candidate.editionFactsTypical ? CandidateView::tr("about %1 pages").arg(*candidate.pageCount)
                                               : CandidateView::tr("%1 pages").arg(*candidate.pageCount));
    }
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

constexpr QSize thumbnailSize(48, 72);
constexpr QSize previewSize = thumbnailSize * 2;

// A frame where a cover will go: "…" while one is on its way, a dash when
// the provider has none.
QPixmap placeholder(const QWidget* widget, bool coming)
{
    QPixmap pixmap(thumbnailSize);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setPen(muted(widget));
    painter.drawRect(QRect(QPoint(0, 0), thumbnailSize - QSize(1, 1)));
    painter.drawText(pixmap.rect(), Qt::AlignCenter, coming ? QStringLiteral("…") : QStringLiteral("—"));
    return pixmap;
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
    setMuted(status_);
    layout->addWidget(status_);

    list_->setObjectName(QStringLiteral("candidates.list"));
    list_->setWordWrap(true);
    list_->setIconSize(thumbnailSize);
    list_->setMouseTracking(true);
    list_->viewport()->installEventFilter(this);

    // The enlarged cover: a borderless window that follows the pointer and
    // never takes focus.
    preview_ = new QLabel(this, Qt::ToolTip | Qt::FramelessWindowHint);
    preview_->setObjectName(QStringLiteral("candidates.preview"));
    preview_->setAttribute(Qt::WA_ShowWithoutActivating);
    preview_->setFrameShape(QFrame::Box);
    preview_->setAlignment(Qt::AlignCenter);
    preview_->hide();
    list_->setSpacing(4);
    layout->addWidget(list_, 1);

    // A searched candidate may be another edition; the owner can say it is
    // theirs, and its publisher and page count are then taken (D-029).
    myEdition_ = new QCheckBox(tr("This is my edition — take its publisher and page count too"), this);
    myEdition_->setObjectName(QStringLiteral("candidates.myEdition"));
    myEdition_->setToolTip(tr("Only where your book has none recorded. The cover beside each candidate, "
                              "enlarged on hover, helps tell editions apart. Its ISBN is never taken."));
    myEdition_->hide();
    layout->addWidget(myEdition_);

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
    hidePreview();
    reviewing_ = false;
    place_.clear();
    heading_->setText(title);
    status_->setText(how);
    list_->clear();
    list_->hide();
    use_->hide();
    reject_->hide();
    skip_->hide();
    myEdition_->hide();
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
                                         "the synopsis, first-published year, genres and cover are taken, "
                                         "and its publisher and page count only if you say it is your "
                                         "edition. Choose the one that is your book.")
                                    : tr("Found by title and author, so any of these may be another edition: "
                                         "the synopsis, first-published year, genres and cover are taken, "
                                         "and its publisher and page count only if you say it is your "
                                         "edition. Choose the one that is your book, or cancel.")));
    }
    byIsbn_ = byIsbn;
    // Unticked for every new offer: never carried from one book to the next.
    myEdition_->setChecked(false);
    myEdition_->setVisible(!byIsbn);
    list_->clear();
    hidePreview();
    covers_.assign(candidates.size(), QPixmap());
    for (const Candidate& candidate : candidates) {
        // Each row as tall as its text or its thumbnail, whichever is more.
        new QListWidgetItem(QIcon(placeholder(this, candidate.coverUrl.has_value())), describe(candidate), list_);
    }
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

void CandidateView::setCover(int index, const QPixmap& cover)
{
    QListWidgetItem* item = list_->item(index);
    if (!item || cover.isNull())
        return;
    // Centred in a frame of the thumbnail's size, so every row lines up.
    QPixmap framed(thumbnailSize);
    framed.fill(Qt::transparent);
    const QPixmap scaled = cover.scaled(thumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPainter painter(&framed);
    painter.drawPixmap((thumbnailSize.width() - scaled.width()) / 2, (thumbnailSize.height() - scaled.height()) / 2,
        scaled);
    painter.end();
    item->setIcon(QIcon(framed));
    item->setData(Qt::UserRole, true); // a cover is shown
    if (index >= 0 && index < static_cast<int>(covers_.size()))
        covers_[static_cast<std::size_t>(index)] = cover;
}

const QLabel* CandidateView::coverPreview() const
{
    return preview_->isVisible() ? preview_ : nullptr;
}

bool CandidateView::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == list_->viewport()) {
        if (event->type() == QEvent::MouseMove) {
            const auto* move = static_cast<QMouseEvent*>(event);
            const QPoint at = move->position().toPoint();
            const QModelIndex index = list_->indexAt(at);
            // Over the thumbnail itself, not the text beside it.
            const QRect row = index.isValid() ? list_->visualRect(index) : QRect();
            const bool onThumbnail = index.isValid() && at.x() <= row.left() + thumbnailSize.width() + 8;
            if (onThumbnail)
                previewCover(index.row(), move->globalPosition().toPoint());
            else
                hidePreview();
        } else if (event->type() == QEvent::Leave || event->type() == QEvent::Hide
            || event->type() == QEvent::MouseButtonPress || event->type() == QEvent::Wheel) {
            hidePreview();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CandidateView::previewCover(int row, const QPoint& globalPosition)
{
    if (row < 0 || row >= static_cast<int>(covers_.size()) || covers_[static_cast<std::size_t>(row)].isNull()) {
        hidePreview();
        return;
    }
    if (row != previewRow_) {
        previewRow_ = row;
        preview_->setPixmap(covers_[static_cast<std::size_t>(row)].scaled(previewSize, Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
        preview_->adjustSize();
    }
    // To the left of the pointer, which sits at the panel's right-hand edge;
    // kept on the screen.
    QPoint where(globalPosition.x() - preview_->width() - 16, globalPosition.y() - preview_->height() / 2);
    if (const QScreen* screen = this->screen()) {
        const QRect bounds = screen->availableGeometry();
        where.setX(std::clamp(where.x(), bounds.left(), std::max(bounds.left(), bounds.right() - preview_->width())));
        where.setY(std::clamp(where.y(), bounds.top(), std::max(bounds.top(), bounds.bottom() - preview_->height())));
    }
    preview_->move(where);
    preview_->show();
}

void CandidateView::hideEvent(QHideEvent* event)
{
    // The preview is a window of its own: it goes when the panel moves on.
    hidePreview();
    QWidget::hideEvent(event);
}

void CandidateView::hidePreview()
{
    previewRow_ = -1;
    preview_->hide();
}

void CandidateView::showProblem(const QString& message)
{
    hidePreview();
    status_->setText(message);
    list_->clear();
    list_->hide();
    use_->hide();
    reject_->hide();
    skip_->hide();
    myEdition_->hide();
    cancel_->setText(tr("Back"));
    cancel_->setFocus();
}

void CandidateView::focusList()
{
    list_->setFocus();
}

void CandidateView::choose()
{
    hidePreview();
    const auto selected = list_->selectedItems();
    if (selected.isEmpty() || list_->isHidden())
        return;
    emit chosen(list_->row(selected.front()), byIsbn_ || myEdition_->isChecked());
}

} // namespace pinax::ui
