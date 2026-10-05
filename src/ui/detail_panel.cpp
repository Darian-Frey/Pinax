#include "ui/detail_panel.h"

#include "ui/book_editor.h"
#include "ui/book_view.h"
#include "ui/style.h"

#include <QLabel>
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

    connect(view_, &BookView::editRequested, this, &DetailPanel::beginEdit);
    connect(editor_, &BookEditor::saveRequested, this, &DetailPanel::saveRequested);
    connect(view_, &BookView::ratingChosen, this, [this](int rating) {
        if (shown_)
            emit ratingRequested(shown_->book.id, rating);
    });
    connect(editor_, &BookEditor::cancelled, this, [this] {
        if (shown_)
            showBook(*shown_);
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

void DetailPanel::beginEdit()
{
    if (state_ != State::Viewing || !shown_)
        return;
    editor_->editBook(*shown_);
    setState(State::Editing);
    editor_->focusTitle();
}

void DetailPanel::showSaveError(const QString& message)
{
    editor_->showError(message);
}

void DetailPanel::setState(State state)
{
    state_ = state;
    stack_->setCurrentIndex(static_cast<int>(state));
}

} // namespace pinax::ui
