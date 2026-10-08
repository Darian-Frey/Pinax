#include "ui/book_editor.h"

#include "domain/credit_text.h"
#include "domain/dates.h"
#include "domain/isbn.h"
#include "ui/style.h"

#include <QComboBox>
#include <QCompleter>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QToolButton>
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
    heading_ = new QLabel(tr("Editing"), this);
    heading_->setObjectName(QStringLiteral("edit.heading"));
    QFont headingFont = heading_->font();
    headingFont.setPointSizeF(headingFont.pointSizeF() * 1.3);
    headingFont.setBold(true);
    heading_->setFont(headingFont);
    heading->addWidget(heading_);
    heading->addStretch();
    layout->addLayout(heading);

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);

    title_ = makeLine(QStringLiteral("edit.title"), this);
    form->addRow(tr("Title"), title_);
    subtitle_ = makeLine(QStringLiteral("edit.subtitle"), this);
    form->addRow(tr("Subtitle"), subtitle_);

    authors_ = makeLine(QStringLiteral("edit.authors"), this);
    authors_->setPlaceholderText(tr("Larry Niven & Jerry Pournelle"));
    authors_->setToolTip(tr("In cover order, joined by \" & \". Put a role after a name in "
                            "brackets: (editor), (translator), (illustrator)."));
    form->addRow(tr("Authors"), authors_);

    // Series, one row each (BUG-005).
    auto* series = new QWidget(this);
    series->setObjectName(QStringLiteral("edit.series"));
    auto* seriesBox = new QVBoxLayout(series);
    seriesBox->setContentsMargins(0, 0, 0, 0);
    seriesBox->setSpacing(6);
    seriesLayout_ = new QVBoxLayout;
    seriesLayout_->setSpacing(8);
    seriesBox->addLayout(seriesLayout_);
    auto* addSeries = new QPushButton(tr("+ Add to a series"), series);
    addSeries->setObjectName(QStringLiteral("edit.series.add"));
    addSeries->setAutoDefault(false);
    addSeries->setToolTip(tr("Choose one of your series, or type a new name to start one"));
    connect(addSeries, &QPushButton::clicked, this, [this] {
        addSeriesRow();
        seriesRows_.back().name->setFocus();
    });
    seriesBox->addWidget(addSeries, 0, Qt::AlignLeft);
    form->addRow(tr("Series"), series);

    readState_ = new QComboBox(this);
    readState_->setObjectName(QStringLiteral("edit.readState"));
    readState_->addItems({tr("Unread"), tr("Reading"), tr("Read"), tr("Abandoned")});
    form->addRow(tr("Read state"), readState_);
    // When, as precisely as remembered (IMP-010).
    dateStarted_ = makeLine(QStringLiteral("edit.dateStarted"), this);
    dateStarted_->setPlaceholderText(tr("YYYY-MM-DD"));
    dateStarted_->setToolTip(tr("When last started — a day, a month (2019-03) or just a year (2019)."));
    form->addRow(tr("Started"), dateStarted_);
    dateFinished_ = makeLine(QStringLiteral("edit.dateFinished"), this);
    dateFinished_->setPlaceholderText(tr("YYYY-MM-DD"));
    dateFinished_->setToolTip(tr("When last finished — a day, a month (2019-03) or just a year (2019). Left "
                                 "empty, marking a book read records today."));
    form->addRow(tr("Finished"), dateFinished_);

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
    heading_->setText(book.id == 0 ? tr("Adding a book") : tr("Editing"));

    title_->setText(QString::fromStdString(book.title));
    subtitle_->setText(text(book.subtitle));
    authors_->setText(QString::fromStdString(domain::formatCredits(detail.credits)));
    while (!seriesRows_.empty())
        removeSeriesRow(seriesRows_.back().widget);
    for (const auto& membership : detail.series)
        addSeriesRow(&membership);

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
    dateStarted_->setText(text(book.dateStarted));
    dateFinished_->setText(text(book.dateFinished));
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
    auto date = [&errors](QLineEdit* field, const QString& name) -> std::optional<std::string> {
        const auto value = optionalText(field->text());
        if (value && !domain::isPartialIsoDate(*value))
            errors << tr("%1 must be a date such as 2024-03-01, a month such as 2024-03, or a year.").arg(name);
        return value;
    };
    book.dateStarted = date(dateStarted_, tr("Started"));
    book.dateFinished = date(dateFinished_, tr("Finished"));
    book.acquiredDate = date(acquiredDate_, tr("Acquired"));
    book.acquiredNote = optionalText(acquiredNote_->text());

    // A synopsis the owner has touched is theirs: marked manual, so
    // enrichment leaves it alone (AV-001).
    const std::optional<std::string> synopsis = optionalText(synopsis_->toPlainText());
    if (synopsis != original_.synopsis) {
        book.synopsis = synopsis;
        book.synopsisSource = synopsis ? std::optional(domain::Source::Manual) : std::nullopt;
    }
    book.notes = optionalText(notes_->toPlainText());

    std::vector<domain::SeriesPlacement> placements;
    for (const SeriesRow& row : seriesRows_) {
        domain::SeriesPlacement placement;
        const QString name = row.name->currentText().trimmed();
        if (name.isEmpty())
            continue;
        placement.seriesName = name.toStdString();
        // Chosen from the list, unless the text has since been changed.
        if (row.name->currentIndex() >= 0 && row.name->itemText(row.name->currentIndex()) == name)
            placement.seriesId = row.name->currentData().toLongLong();
        placement.position = optionalText(row.position->text());
        if (const auto sort = optionalText(row.sort->text())) {
            bool ok = false;
            const double value = QString::fromStdString(*sort).toDouble(&ok);
            if (ok && value >= 0)
                placement.sortPosition = value;
            else
                errors << tr("The sort number for %1 must be a number, such as 5 or 6.5; or leave it "
                             "blank to work it out from the position.").arg(name);
        }
        placements.push_back(std::move(placement));
    }

    std::vector<domain::NamedCredit> credits;
    try {
        credits = domain::parseCredits(authors_->text().toStdString());
    } catch (const domain::CreditTextError& error) {
        errors << tr("Authors: %1.").arg(QString::fromUtf8(error.what()));
    }

    if (!errors.isEmpty()) {
        showError(errors.join(QLatin1Char('\n')));
        return;
    }
    error_->hide();
    emit saveRequested({book, credits, placements});
}

void BookEditor::setSeriesChoices(const std::vector<domain::FilterOption>& series)
{
    seriesChoices_ = series;
}

void BookEditor::addSeriesRow(const domain::SeriesMembership* membership)
{
    auto* row = new QWidget(this);
    row->setObjectName(QStringLiteral("edit.series.row"));
    auto* layout = new QVBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    auto* name = new QComboBox(row);
    name->setObjectName(QStringLiteral("edit.series.name"));
    name->setEditable(true);
    name->setInsertPolicy(QComboBox::NoInsert);
    name->completer()->setFilterMode(Qt::MatchContains);
    name->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    name->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    name->setMinimumContentsLength(10);
    for (const auto& choice : seriesChoices_)
        name->addItem(QString::fromStdString(choice.name), QVariant::fromValue<qlonglong>(choice.id));
    name->setCurrentIndex(-1);
    name->lineEdit()->setPlaceholderText(tr("Series, or a new name"));
    layout->addWidget(name);

    auto* place = new QHBoxLayout;
    auto* position = makeLine(QStringLiteral("edit.series.position"), row);
    position->setPlaceholderText(tr("as printed"));
    position->setToolTip(tr("The position as the book prints it: 5, 6.5, 1-4, Companion"));
    auto* sort = makeLine(QStringLiteral("edit.series.sort"), row);
    sort->setPlaceholderText(tr("auto"));
    sort->setToolTip(tr("Orders the series. Left blank, it is worked out from the position."));
    sort->setMaximumWidth(56);
    auto* remove = new QToolButton(row);
    remove->setObjectName(QStringLiteral("edit.series.remove"));
    remove->setText(QStringLiteral("×"));
    remove->setToolTip(tr("Not in this series"));
    place->addWidget(new QLabel(tr("No."), row));
    place->addWidget(position, 1);
    place->addWidget(new QLabel(tr("sort"), row));
    place->addWidget(sort);
    place->addWidget(remove);
    layout->addLayout(place);

    if (membership) {
        const int index = name->findData(QVariant::fromValue<qlonglong>(membership->seriesId));
        if (index >= 0)
            name->setCurrentIndex(index);
        else
            name->setEditText(QString::fromStdString(membership->name));
        position->setText(membership->position ? QString::fromStdString(*membership->position) : QString());
        if (membership->sortPosition)
            sort->setText(QString::number(*membership->sortPosition));
    }

    connect(remove, &QToolButton::clicked, this, [this, row] { removeSeriesRow(row); });
    seriesLayout_->addWidget(row);
    seriesRows_.push_back({row, name, position, sort});
}

void BookEditor::removeSeriesRow(QWidget* row)
{
    seriesRows_.erase(std::remove_if(seriesRows_.begin(), seriesRows_.end(),
                          [row](const SeriesRow& candidate) { return candidate.widget == row; }),
        seriesRows_.end());
    seriesLayout_->removeWidget(row);
    // Out of the form at once; deleted once its own × has finished.
    row->hide();
    row->setParent(nullptr);
    row->deleteLater();
}

} // namespace pinax::ui
