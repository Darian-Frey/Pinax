#include "ui/attach_view.h"

#include "domain/placeholder.h"

#include "ui/style.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

namespace pinax::ui {

AttachView::AttachView(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    layout->addWidget(makeSectionHeading(tr("Mark as owned"), this));
    volume_ = new QLabel(this);
    volume_->setObjectName(QStringLiteral("attach.volume"));
    volume_->setWordWrap(true);
    volume_->setTextFormat(Qt::RichText);
    layout->addWidget(volume_);

    auto* create = new QPushButton(tr("Add it as a new book"), this);
    create->setObjectName(QStringLiteral("attach.create"));
    create->setToolTip(tr("Opens the book form with the title and the series' authors filled in"));
    layout->addWidget(create, 0, Qt::AlignLeft);

    auto* or_ = new QLabel(tr("Or attach a book already in the catalogue:"), this);
    or_->setWordWrap(true);
    layout->addWidget(or_);
    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("attach.search"));
    search_->setPlaceholderText(tr("Search title or author"));
    search_->setClearButtonEnabled(true);
    layout->addWidget(search_);
    candidates_ = new QListWidget(this);
    candidates_->setObjectName(QStringLiteral("attach.candidates"));
    layout->addWidget(candidates_, 1);

    auto* buttons = new QHBoxLayout;
    attach_ = new QPushButton(tr("Attach"), this);
    attach_->setObjectName(QStringLiteral("attach.attach"));
    attach_->setEnabled(false);
    auto* cancel = new QPushButton(tr("Cancel"), this);
    cancel->setObjectName(QStringLiteral("attach.cancel"));
    buttons->addWidget(attach_);
    buttons->addWidget(cancel);
    buttons->addStretch();
    layout->addLayout(buttons);

    connect(create, &QPushButton::clicked, this, [this] { emit createRequested(entryId_); });
    connect(search_, &QLineEdit::textChanged, this, &AttachView::filter);
    connect(candidates_, &QListWidget::currentItemChanged, this,
        [this](QListWidgetItem* item) { attach_->setEnabled(item && !item->isHidden()); });
    connect(candidates_, &QListWidget::itemActivated, this, &AttachView::attachSelected);
    connect(attach_, &QPushButton::clicked, this, &AttachView::attachSelected);
    connect(cancel, &QPushButton::clicked, this, &AttachView::cancelled);
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, &AttachView::cancelled);
}

void AttachView::offer(const domain::SeriesRow& volume, const QString& seriesName,
    const std::vector<domain::BookSummary>& candidates)
{
    entryId_ = volume.entryId;
    const auto title = volume.title();
    QString text = QStringLiteral("<b>%1</b>")
                       .arg(title ? QString::fromStdString(*title).toHtmlEscaped() : tr("Untitled volume"));
    text += QStringLiteral("<br>") + seriesName.toHtmlEscaped();
    if (volume.position)
        text += QStringLiteral(" · ") + QString::fromStdString(*volume.position).toHtmlEscaped();
    volume_->setText(text);

    candidates_->clear();
    for (const auto& book : candidates) {
        QString label = QString::fromStdString(book.title);
        if (book.authors)
            label += QStringLiteral(" — ") + QString::fromStdString(*book.authors);
        auto* item = new QListWidgetItem(label, candidates_);
        item->setData(Qt::UserRole, QVariant::fromValue<qint64>(book.id));
    }
    candidates_->sortItems();
    // Start with the volume's own title, which is what is most likely held —
    // unless nothing matches it, when every candidate is the better start.
    QString start;
    if (title && !domain::isPlaceholderTitle(title))
        start = QString::fromStdString(*title);
    search_->setText(start);
    filter(start);
    if (!start.isEmpty() && !candidates_->currentItem()) {
        search_->clear();
        filter(QString());
    }
}

void AttachView::focusSearch()
{
    search_->setFocus();
    search_->selectAll();
}

void AttachView::filter(const QString& text)
{
    QListWidgetItem* first = nullptr;
    for (int i = 0; i < candidates_->count(); ++i) {
        QListWidgetItem* item = candidates_->item(i);
        const bool match = text.trimmed().isEmpty() || item->text().contains(text.trimmed(), Qt::CaseInsensitive);
        item->setHidden(!match);
        if (match && !first)
            first = item;
    }
    // With nothing typed, nothing is chosen: Enter must not attach whatever
    // book happens to sort first.
    QListWidgetItem* chosen = text.trimmed().isEmpty() ? nullptr : first;
    candidates_->setCurrentItem(chosen);
    if (!chosen)
        candidates_->clearSelection();
    attach_->setEnabled(chosen != nullptr);
}

void AttachView::attachSelected()
{
    const QListWidgetItem* item = candidates_->currentItem();
    if (!item || item->isHidden())
        return;
    emit attachRequested(entryId_, item->data(Qt::UserRole).toLongLong());
}

} // namespace pinax::ui
