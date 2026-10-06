#include "ui/detail_panel.h"

#include "ui/attach_view.h"
#include "ui/book_editor.h"
#include "ui/entry_editor.h"
#include "ui/book_view.h"
#include "ui/series_view.h"
#include "ui/style.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace pinax::ui {

namespace {

QScrollArea* scrolling(QWidget* content, QWidget* parent)
{
    auto* area = new QScrollArea(parent);
    area->setWidget(content);
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return area;
}

QLabel* makeNotice(QWidget* parent)
{
    auto* label = new QLabel(parent);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setMargin(24);
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, muted(label));
    label->setPalette(palette);
    return label;
}

} // namespace

DetailPanel::DetailPanel(QWidget* parent)
    : QWidget(parent)
    , stack_(new QStackedWidget(this))
    , empty_(makeNotice(this))
    , view_(new BookView)
    , editor_(new BookEditor)
    , several_(makeNotice(this))
    , confirm_(new QWidget(this))
    , question_(new QLabel(confirm_))
    , keep_(new QPushButton(tr("Keep"), confirm_))
    , confirmButton_(new QPushButton(tr("Delete"), confirm_))
    , seriesView_(new SeriesView)
    , entryEditor_(new EntryEditor)
    , attachView_(new AttachView)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(stack_);

    empty_->setObjectName(QStringLiteral("detail.empty"));
    empty_->setText(tr("Select a book to see it here."));
    several_->setObjectName(QStringLiteral("detail.several"));

    stack_->addWidget(empty_);
    stack_->addWidget(scrolling(view_, this));
    stack_->addWidget(scrolling(editor_, this));
    stack_->addWidget(several_);

    // Confirming a deletion, in place of a dialogue (D-011).
    auto* confirmLayout = new QVBoxLayout(confirm_);
    confirmLayout->setContentsMargins(14, 24, 14, 14);
    question_->setObjectName(QStringLiteral("confirm.question"));
    question_->setWordWrap(true);
    question_->setTextFormat(Qt::PlainText);
    confirmLayout->addWidget(question_);
    auto* confirmButtons = new QHBoxLayout;
    confirmButton_->setObjectName(QStringLiteral("confirm.delete"));
    confirmButton_->setAutoDefault(false);
    keep_->setObjectName(QStringLiteral("confirm.keep"));
    keep_->setDefault(true);
    confirmButtons->addWidget(keep_);
    confirmButtons->addWidget(confirmButton_);
    confirmButtons->addStretch();
    confirmLayout->addLayout(confirmButtons);
    confirmLayout->addStretch();
    stack_->addWidget(confirm_);
    stack_->addWidget(scrolling(seriesView_, this));
    stack_->addWidget(scrolling(entryEditor_, this));
    stack_->addWidget(attachView_);

    connect(confirmButton_, &QPushButton::clicked, this, [this] {
        auto action = std::move(pendingConfirm_);
        pendingConfirm_ = nullptr;
        if (action)
            action();
    });
    auto keep = [this] {
        pendingConfirm_ = nullptr;
        emit dismissed();
    };
    connect(keep_, &QPushButton::clicked, this, keep);
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), confirm_);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, keep);

    connect(view_, &BookView::editRequested, this, &DetailPanel::beginEdit);
    connect(editor_, &BookEditor::saveRequested, this, &DetailPanel::saveRequested);
    connect(view_, &BookView::ratingChosen, this, [this](int rating) {
        if (shown_)
            emit ratingRequested(shown_->book.id, rating);
    });
    connect(editor_, &BookEditor::cancelled, this, [this] {
        if (shown_ && shown_->book.id != 0) {
            showBook(*shown_);
        } else {
            showNothing();
            emit dismissed();
        }
    });

    connect(seriesView_, &SeriesView::markOwnedRequested, this, &DetailPanel::markOwnedRequested);
    connect(seriesView_, &SeriesView::editEntryRequested, this, &DetailPanel::editEntryRequested);
    connect(entryEditor_, &EntryEditor::saveRequested, this, &DetailPanel::entrySaveRequested);
    connect(entryEditor_, &EntryEditor::removeRequested, this, &DetailPanel::entryRemoveRequested);
    connect(entryEditor_, &EntryEditor::cancelled, this, [this] {
        showNothing();
        emit dismissed();
    });
    connect(attachView_, &AttachView::attachRequested, this, &DetailPanel::attachRequested);
    connect(attachView_, &AttachView::createRequested, this, &DetailPanel::createForEntryRequested);
    connect(attachView_, &AttachView::cancelled, this, [this] {
        showNothing();
        emit dismissed();
    });
    connect(view_, &BookView::deleteRequested, this, [this] {
        if (shown_)
            emit deleteRequested({shown_->book.id});
    });

    // F2 edits from anywhere in the window, the list included.
    auto* edit = new QShortcut(QKeySequence(Qt::Key_F2), this);
    edit->setContext(Qt::WindowShortcut);
    connect(edit, &QShortcut::activated, this, &DetailPanel::beginEdit);

    setState(State::Empty);
}

void DetailPanel::showNothing()
{
    shown_.reset();
    setState(State::Empty);
}

void DetailPanel::showBook(const domain::BookDetail& detail)
{
    shown_ = detail;
    view_->showBook(detail);
    setState(State::Viewing);
}

void DetailPanel::showSelection(int count)
{
    shown_.reset();
    several_->setText(tr("%1 books selected.\n\nEditing several at once arrives with the bulk "
                         "editor.")
                          .arg(count));
    setState(State::Several);
}

void DetailPanel::showSeries(const domain::SeriesDetail& detail,
    const std::optional<domain::SeriesRow>& selected)
{
    shown_.reset();
    seriesView_->showSeries(detail, selected);
    setState(State::ViewingSeries);
}

void DetailPanel::beginEdit()
{
    // F2 on a selected volume of a series edits that volume's entry.
    if (state_ == State::ViewingSeries) {
        if (const auto entry = seriesView_->selectedEntry())
            emit editEntryRequested(*entry);
        return;
    }
    if (state_ != State::Viewing || !shown_)
        return;
    editor_->editBook(*shown_);
    setState(State::Editing);
    editor_->focusTitle();
}

void DetailPanel::beginNew(const domain::BookDetail& prefill)
{
    shown_ = prefill;
    shown_->book.id = 0;
    editor_->editBook(*shown_);
    setState(State::Editing);
    editor_->focusTitle();
}

void DetailPanel::askToDelete(const QList<qint64>& ids, const QString& question)
{
    if (ids.isEmpty())
        return;
    askToConfirm(question, tr("Delete"), [this, ids] { emit deleteConfirmed(ids); });
}

void DetailPanel::askToConfirm(const QString& question, const QString& confirmLabel,
    std::function<void()> onConfirm)
{
    pendingConfirm_ = std::move(onConfirm);
    question_->setText(question);
    confirmButton_->setText(confirmLabel);
    setState(State::ConfirmingDelete);
    keep_->setFocus();
}

void DetailPanel::beginEntryEdit(const domain::SeriesEntry& entry, const QString& seriesName,
    const QString& ownedBy)
{
    shown_.reset();
    entryEditor_->editEntry(entry, seriesName, ownedBy);
    setState(State::EditingEntry);
    entryEditor_->focusFirst();
}

void DetailPanel::showEntryError(const QString& message)
{
    entryEditor_->showError(message);
}

void DetailPanel::beginAttach(const domain::SeriesRow& volume, const QString& seriesName,
    const std::vector<domain::BookSummary>& candidates)
{
    shown_.reset();
    attachView_->offer(volume, seriesName, candidates);
    setState(State::Attaching);
    attachView_->focusSearch();
}

void DetailPanel::showSaveError(const QString& message)
{
    editor_->showError(message);
}

void DetailPanel::setState(State state)
{
    const bool changed = state != state_;
    state_ = state;
    stack_->setCurrentIndex(static_cast<int>(state));
    if (changed)
        emit stateChanged(state);
}

} // namespace pinax::ui
