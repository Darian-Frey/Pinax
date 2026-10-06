#include "ui/entry_editor.h"

#include "ui/style.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

namespace pinax::ui {

namespace {

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

QString number(double value)
{
    return QLocale::c().toString(value, 'g', 10);
}

} // namespace

EntryEditor::EntryEditor(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    heading_ = new QLabel(this);
    heading_->setObjectName(QStringLiteral("entry.heading"));
    heading_->setWordWrap(true);
    QFont font = heading_->font();
    font.setPointSizeF(font.pointSizeF() * 1.3);
    font.setBold(true);
    heading_->setFont(font);
    layout->addWidget(heading_);

    owned_ = new QLabel(this);
    owned_->setObjectName(QStringLiteral("entry.owned"));
    owned_->setWordWrap(true);
    layout->addWidget(owned_);

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    position_ = new QLineEdit(this);
    position_->setObjectName(QStringLiteral("entry.position"));
    position_->setPlaceholderText(tr("as printed: 5, 6.5, 1-4, novella"));
    form->addRow(tr("Position"), position_);
    sortPosition_ = new QLineEdit(this);
    sortPosition_->setObjectName(QStringLiteral("entry.sortPosition"));
    sortPosition_->setPlaceholderText(tr("orders the series; empty sorts last"));
    sortPosition_->setToolTip(tr("A number that places the volume: 6.5 files between 6 and 7, an "
                                 "omnibus of 1-4 takes 1."));
    form->addRow(tr("Sort number"), sortPosition_);
    title_ = new QLineEdit(this);
    title_->setObjectName(QStringLiteral("entry.title"));
    form->addRow(tr("Title"), title_);
    notes_ = new QPlainTextEdit(this);
    notes_->setObjectName(QStringLiteral("entry.notes"));
    notes_->setTabChangesFocus(true);
    notes_->setMaximumHeight(90);
    form->addRow(tr("Notes"), notes_);
    layout->addLayout(form);

    error_ = new QLabel(this);
    error_->setObjectName(QStringLiteral("entry.error"));
    error_->setWordWrap(true);
    error_->setStyleSheet(QStringLiteral("QLabel { color: #E06C6C; }"));
    error_->hide();
    layout->addWidget(error_);

    auto* buttons = new QHBoxLayout;
    auto* save = new QPushButton(tr("Save"), this);
    save->setObjectName(QStringLiteral("entry.save"));
    save->setDefault(true);
    auto* cancel = new QPushButton(tr("Cancel"), this);
    cancel->setObjectName(QStringLiteral("entry.cancel"));
    remove_ = new QPushButton(tr("Remove from series"), this);
    remove_->setObjectName(QStringLiteral("entry.remove"));
    buttons->addWidget(save);
    buttons->addWidget(cancel);
    buttons->addStretch();
    layout->addLayout(buttons);
    // Apart from Save, so the one that removes is never the one beside it.
    layout->addSpacing(12);
    layout->addWidget(remove_, 0, Qt::AlignLeft);
    layout->addStretch();

    connect(save, &QPushButton::clicked, this, &EntryEditor::save);
    connect(cancel, &QPushButton::clicked, this, &EntryEditor::cancelled);
    connect(remove_, &QPushButton::clicked, this, [this] { emit removeRequested(original_.id); });
    for (const auto& key : {QKeySequence(Qt::CTRL | Qt::Key_Return), QKeySequence(Qt::CTRL | Qt::Key_Enter)}) {
        auto* shortcut = new QShortcut(key, this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, &EntryEditor::save);
    }
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, &EntryEditor::cancelled);
}

void EntryEditor::editEntry(const domain::SeriesEntry& entry, const QString& seriesName, const QString& ownedBy)
{
    original_ = entry;
    heading_->setText(entry.id == 0 ? tr("New volume in %1").arg(seriesName)
                                    : tr("A volume of %1").arg(seriesName));
    owned_->setText(ownedBy.isEmpty() ? tr("Not on the shelf.") : tr("On the shelf: %1").arg(ownedBy));
    QPalette palette = owned_->palette();
    palette.setColor(QPalette::WindowText, muted(owned_));
    owned_->setPalette(palette);

    position_->setText(text(entry.position));
    sortPosition_->setText(entry.sortPosition ? number(*entry.sortPosition) : QString());
    title_->setText(text(entry.title));
    notes_->setPlainText(text(entry.notes));
    remove_->setVisible(entry.id != 0);
    error_->hide();
}

void EntryEditor::showError(const QString& message)
{
    error_->setText(message);
    error_->show();
}

void EntryEditor::focusFirst()
{
    position_->setFocus();
    position_->selectAll();
}

void EntryEditor::save()
{
    domain::SeriesEntry entry = original_;
    entry.position = optionalText(position_->text());
    entry.title = optionalText(title_->text());
    entry.notes = optionalText(notes_->toPlainText());

    QStringList errors;
    const QString sort = sortPosition_->text().trimmed();
    if (sort.isEmpty()) {
        entry.sortPosition.reset();
    } else {
        bool ok = false;
        const double value = QLocale::c().toDouble(sort, &ok);
        if (ok)
            entry.sortPosition = value;
        else
            errors << tr("The sort number must be a number, such as 6 or 6.5.");
    }
    if (!entry.position && !entry.title)
        errors << tr("A volume needs a position or a title.");

    if (!errors.isEmpty()) {
        showError(errors.join(QLatin1Char('\n')));
        return;
    }
    error_->hide();
    emit saveRequested(entry);
}

} // namespace pinax::ui
