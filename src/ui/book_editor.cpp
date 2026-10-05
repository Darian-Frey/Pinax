#include "ui/book_editor.h"

#include "domain/isbn.h"
#include "ui/style.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QVBoxLayout>

#include <algorithm>
#include <array>

namespace pinax::ui {

using domain::Binding;
using domain::Book;
using domain::ReadStatus;

namespace {

constexpr std::array readStates{
    ReadStatus::Unread, ReadStatus::Reading, ReadStatus::Read, ReadStatus::Abandoned};
constexpr std::array bindings{
    Binding::Paperback, Binding::Hardback, Binding::Omnibus, Binding::Boxset, Binding::Other};

QString text(const std::optional<std::string>& value)
{
    return value ? QString::fromStdString(*value) : QString();
}

std::optional<std::string> optionalText(const QString& value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty())
        return std::nullopt;
    return trimmed.toStdString();
}

QLineEdit* makeLine(const QString& name, QWidget* parent)
{
    auto* line = new QLineEdit(parent);
    line->setObjectName(name);
    return line;
}

QPlainTextEdit* makeText(const QString& name, QWidget* parent)
{
    auto* edit = new QPlainTextEdit(parent);
    edit->setObjectName(name);
    edit->setTabChangesFocus(true);
    edit->setMinimumHeight(80);
    return edit;
}

// Parses a whole number within [low, high]; empty is nullopt. Adds an error
// in the owner's terms when it does not parse.
std::optional<int> wholeNumber(const QString& input, int low, int high, const QString& error,
    QStringList& errors)
{
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty())
        return std::nullopt;
    bool ok = false;
    const int value = trimmed.toInt(&ok);
    if (!ok || value < low || value > high) {
        errors << error;
        return std::nullopt;
    }
    return value;
}

} // namespace

BookEditor::BookEditor(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    auto* heading = new QHBoxLayout;
    auto* label = new QLabel(tr("Editing"), this);
    QFont headingFont = label->font();
    headingFont.setPointSizeF(headingFont.pointSizeF() * 1.3);
    headingFont.setBold(true);
    label->setFont(headingFont);
    heading->addWidget(label);
    heading->addStretch();
    layout->addLayout(heading);

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);

    title_ = makeLine(QStringLiteral("edit.title"), this);
    form->addRow(tr("Title"), title_);
    subtitle_ = makeLine(QStringLiteral("edit.subtitle"), this);
    form->addRow(tr("Subtitle"), subtitle_);

    authors_ = new QLabel(this);
    authors_->setObjectName(QStringLiteral("edit.authors"));
    authors_->setWordWrap(true);
    authors_->setToolTip(tr("Authors and series are not editable here yet"));
    form->addRow(tr("Authors"), authors_);

    readState_ = new QComboBox(this);
    readState_->setObjectName(QStringLiteral("edit.readState"));
    readState_->addItems({tr("Unread"), tr("Reading"), tr("Read"), tr("Abandoned")});
    form->addRow(tr("Read state"), readState_);

    timesRead_ = new QLabel(this);
    timesRead_->setObjectName(QStringLiteral("edit.timesRead"));
    timesRead_->setToolTip(tr("Counted each time the book moves into Read"));
    form->addRow(tr("Times read"), timesRead_);

    rating_ = new QComboBox(this);
    rating_->setObjectName(QStringLiteral("edit.rating"));
    rating_->addItem(tr("Unrated"));
    for (int i = 1; i <= 10; ++i)
        rating_->addItem(QString::number(i));
    form->addRow(tr("Rating"), rating_);

    publisher_ = makeLine(QStringLiteral("edit.publisher"), this);
    form->addRow(tr("Publisher"), publisher_);
    published_ = makeLine(QStringLiteral("edit.published"), this);
    published_->setPlaceholderText(tr("year"));
    form->addRow(tr("First published"), published_);
    pages_ = makeLine(QStringLiteral("edit.pages"), this);
    form->addRow(tr("Pages"), pages_);

    binding_ = new QComboBox(this);
    binding_->setObjectName(QStringLiteral("edit.binding"));
    binding_->addItems(
        {tr("Not recorded"), tr("Paperback"), tr("Hardback"), tr("Omnibus"), tr("Box set"), tr("Other")});
    form->addRow(tr("Binding"), binding_);

    isbn13_ = makeLine(QStringLiteral("edit.isbn13"), this);
    form->addRow(tr("ISBN-13"), isbn13_);
    isbn10_ = makeLine(QStringLiteral("edit.isbn10"), this);
    form->addRow(tr("ISBN-10"), isbn10_);
    editionNote_ = makeLine(QStringLiteral("edit.editionNote"), this);
    form->addRow(tr("Edition"), editionNote_);
    conditionNote_ = makeLine(QStringLiteral("edit.conditionNote"), this);
    form->addRow(tr("Condition"), conditionNote_);
    acquiredDate_ = makeLine(QStringLiteral("edit.acquiredDate"), this);
    acquiredDate_->setPlaceholderText(tr("YYYY-MM-DD"));
    form->addRow(tr("Acquired"), acquiredDate_);
    acquiredNote_ = makeLine(QStringLiteral("edit.acquiredNote"), this);
    form->addRow(tr("How acquired"), acquiredNote_);
    synopsis_ = makeText(QStringLiteral("edit.synopsis"), this);
    form->addRow(tr("Synopsis"), synopsis_);
    notes_ = makeText(QStringLiteral("edit.notes"), this);
    form->addRow(tr("Notes"), notes_);
    layout->addLayout(form);

    error_ = new QLabel(this);
    error_->setObjectName(QStringLiteral("edit.error"));
    error_->setWordWrap(true);
    error_->setStyleSheet(QStringLiteral("QLabel { color: #E06C6C; }"));
    error_->hide();
    layout->addWidget(error_);

    auto* buttons = new QHBoxLayout;
    auto* save = new QPushButton(tr("Save"), this);
    save->setObjectName(QStringLiteral("edit.save"));
    save->setToolTip(tr("Save (Ctrl+Enter)"));
    save->setDefault(true);
    auto* cancel = new QPushButton(tr("Cancel"), this);
    cancel->setObjectName(QStringLiteral("edit.cancel"));
    cancel->setToolTip(tr("Cancel (Esc)"));
    buttons->addWidget(save);
    buttons->addWidget(cancel);
    buttons->addStretch();
    layout->addLayout(buttons);
    layout->addStretch();

    connect(save, &QPushButton::clicked, this, &BookEditor::save);
    connect(cancel, &QPushButton::clicked, this, &BookEditor::cancelled);
    for (const auto& key : {QKeySequence(Qt::CTRL | Qt::Key_Return), QKeySequence(Qt::CTRL | Qt::Key_Enter)}) {
        auto* shortcut = new QShortcut(key, this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, &BookEditor::save);
    }
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, &BookEditor::cancelled);
}

void BookEditor::editBook(const domain::BookDetail& detail)
{
    original_ = detail.book;
    const Book& book = detail.book;

    title_->setText(QString::fromStdString(book.title));
    subtitle_->setText(text(book.subtitle));
    authors_->setText(detail.authors ? QString::fromStdString(*detail.authors) : tr("None recorded"));

    const auto state = std::find(readStates.begin(), readStates.end(), book.readStatus);
    readState_->setCurrentIndex(static_cast<int>(state - readStates.begin()));
    timesRead_->setText(QString::number(book.timesRead));
    rating_->setCurrentIndex(book.rating.value_or(0));

    publisher_->setText(text(book.publisher));
    published_->setText(book.publishedYear ? QString::number(*book.publishedYear) : QString());
    pages_->setText(book.pageCount ? QString::number(*book.pageCount) : QString());
    const auto binding = book.binding
        ? std::find(bindings.begin(), bindings.end(), *book.binding) - bindings.begin() + 1
        : 0;
    binding_->setCurrentIndex(static_cast<int>(binding));

    isbn13_->setText(text(book.isbn13));
    isbn10_->setText(text(book.isbn10));
    editionNote_->setText(text(book.editionNote));
    conditionNote_->setText(text(book.conditionNote));
    acquiredDate_->setText(text(book.acquiredDate));
    acquiredNote_->setText(text(book.acquiredNote));
    synopsis_->setPlainText(text(book.synopsis));
    notes_->setPlainText(text(book.notes));

    error_->hide();
}

void BookEditor::focusTitle()
{
    title_->setFocus();
    title_->selectAll();
}

void BookEditor::showError(const QString& message)
{
    error_->setText(message);
    error_->show();
}

void BookEditor::save()
{
    QStringList errors;
    Book book = original_;

    const std::optional<std::string> title = optionalText(title_->text());
    if (title)
        book.title = *title;
    else
        errors << tr("A title is needed.");
    // A changed title gets a fresh sort title from the repository.
    if (book.title != original_.title)
        book.sortTitle.clear();

    book.subtitle = optionalText(subtitle_->text());
    book.readStatus = readStates[static_cast<std::size_t>(readState_->currentIndex())];
    book.rating = rating_->currentIndex() == 0 ? std::nullopt : std::optional(rating_->currentIndex());
    book.publisher = optionalText(publisher_->text());
    book.publishedYear = wholeNumber(published_->text(), 1400, 2200,
        tr("First published must be a year between 1400 and 2200."), errors);
    book.pageCount = wholeNumber(pages_->text(), 1, 100000,
        tr("Pages must be a whole number above nought."), errors);
    book.binding = binding_->currentIndex() == 0
        ? std::nullopt
        : std::optional(bindings[static_cast<std::size_t>(binding_->currentIndex() - 1)]);

    book.isbn13 = std::nullopt;
    if (const auto isbn = optionalText(isbn13_->text())) {
        const std::string normalised = domain::normaliseIsbn(*isbn);
        if (domain::isValidIsbn13(normalised))
            book.isbn13 = normalised;
        else
            errors << tr("ISBN-13 %1 fails its check digit.").arg(QString::fromStdString(*isbn));
    }
    book.isbn10 = std::nullopt;
    if (const auto isbn = optionalText(isbn10_->text())) {
        const std::string normalised = domain::normaliseIsbn(*isbn);
        if (domain::isValidIsbn10(normalised))
            book.isbn10 = normalised;
        else
            errors << tr("ISBN-10 %1 fails its check digit.").arg(QString::fromStdString(*isbn));
    }

    book.editionNote = optionalText(editionNote_->text());
    book.conditionNote = optionalText(conditionNote_->text());
    book.acquiredDate = optionalText(acquiredDate_->text());
    static const QRegularExpression isoDate(QStringLiteral("^\\d{4}(-\\d{2}(-\\d{2})?)?$"));
    if (book.acquiredDate && !isoDate.match(QString::fromStdString(*book.acquiredDate)).hasMatch())
        errors << tr("Acquired must be a date such as 2024-03-01, or just a year.");
    book.acquiredNote = optionalText(acquiredNote_->text());

    // A synopsis the owner has touched is theirs: marked manual, so
    // enrichment leaves it alone (AV-001).
    const std::optional<std::string> synopsis = optionalText(synopsis_->toPlainText());
    if (synopsis != original_.synopsis) {
        book.synopsis = synopsis;
        book.synopsisSource = synopsis ? std::optional(domain::Source::Manual) : std::nullopt;
    }
    book.notes = optionalText(notes_->toPlainText());

    if (!errors.isEmpty()) {
        showError(errors.join(QLatin1Char('\n')));
        return;
    }
    error_->hide();
    emit saveRequested(book);
}

} // namespace pinax::ui
