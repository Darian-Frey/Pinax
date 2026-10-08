#include "ui/export_view.h"

#include "ui/style.h"

#include <QComboBox>
#include <QCompleter>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

namespace pinax::ui {

namespace {

QLabel* makeNote(const QString& name, QWidget* parent)
{
    auto* label = new QLabel(parent);
    label->setObjectName(name);
    label->setWordWrap(true);
    return label;
}

} // namespace

ExportView::ExportView(QWidget* parent)
    : QWidget(parent)
    , format_(new QComboBox(this))
    , description_(makeNote(QStringLiteral("export.description"), this))
    , path_(new QLineEdit(this))
    , choose_(new QPushButton(tr("Choose…"), this))
    , warning_(makeNote(QStringLiteral("export.warning"), this))
    , outcome_(makeNote(QStringLiteral("export.outcome"), this))
    , export_(new QPushButton(tr("Export"), this))
    , close_(new QPushButton(tr("Close"), this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(8);
    layout->addWidget(makeSectionHeading(tr("Export the catalogue"), this));

    format_->setObjectName(QStringLiteral("export.format"));
    layout->addWidget(format_);
    setMuted(description_);
    layout->addWidget(description_);

    layout->addWidget(new QLabel(tr("Export to"), this));
    path_->setObjectName(QStringLiteral("export.path"));
    auto* files = new QFileSystemModel(this);
    files->setRootPath(QString());
    auto* completer = new QCompleter(files, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    path_->setCompleter(completer);
    auto* where = new QHBoxLayout;
    where->addWidget(path_, 1);
    choose_->setObjectName(QStringLiteral("export.choose"));
    choose_->setAutoDefault(false);
    where->addWidget(choose_);
    layout->addLayout(where);
    connect(choose_, &QPushButton::clicked, this, &ExportView::chooseRequested);
    warning_->setStyleSheet(QStringLiteral("color: %1;").arg(accent().name()));
    layout->addWidget(warning_);

    auto* buttons = new QHBoxLayout;
    export_->setObjectName(QStringLiteral("export.go"));
    export_->setDefault(true);
    close_->setObjectName(QStringLiteral("export.close"));
    close_->setAutoDefault(false);
    buttons->addWidget(export_);
    buttons->addWidget(close_);
    buttons->addStretch();
    layout->addLayout(buttons);
    layout->addWidget(outcome_);
    layout->addStretch();

    connect(format_, &QComboBox::currentIndexChanged, this, &ExportView::formatChosen);
    connect(path_, &QLineEdit::textChanged, this, &ExportView::updateWarning);
    connect(path_, &QLineEdit::returnPressed, export_, &QPushButton::click);
    connect(export_, &QPushButton::clicked, this, [this] {
        if (!path().isEmpty())
            emit exportRequested(format(), path());
    });
    connect(close_, &QPushButton::clicked, this, &ExportView::closed);
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, &ExportView::closed);
}

void ExportView::setFormats(std::vector<Format> formats)
{
    formats_ = std::move(formats);
    const QSignalBlocker blocker(format_);
    format_->clear();
    for (const auto& format : formats_)
        format_->addItem(format.name);
}

void ExportView::start(const QString& folder, const QString& stem)
{
    outcome_->clear();
    const int index = std::max(format_->currentIndex(), 0);
    const QString extension = index < static_cast<int>(formats_.size()) ? formats_[static_cast<std::size_t>(index)].extension
                                                                        : QString();
    path_->setText(QStringLiteral("%1/%2.%3").arg(folder, stem, extension));
    formatChosen();
}

void ExportView::formatChosen()
{
    const int index = format();
    if (index < 0 || index >= static_cast<int>(formats_.size()))
        return;
    const Format& chosen = formats_[static_cast<std::size_t>(index)];
    description_->setText(chosen.description);
    // The file name follows the format: pinax.sql becomes pinax.xlsx.
    QFileInfo current(path());
    if (!path().isEmpty() && !current.isDir()) {
        const QString stem = current.completeBaseName();
        path_->setText(current.dir().filePath(stem + QLatin1Char('.') + chosen.extension));
    }
    updateWarning();
}

void ExportView::showDone(const QString& message)
{
    outcome_->setStyleSheet(QString());
    outcome_->setText(message);
    warning_->clear(); // the file there now is the one just written
    close_->setFocus();
}

void ExportView::showProblem(const QString& message)
{
    outcome_->setStyleSheet(QStringLiteral("color: #d9534f;"));
    outcome_->setText(message);
}

void ExportView::setPath(const QString& path)
{
    path_->setText(path);
}

QString ExportView::path() const
{
    return path_->text().trimmed();
}

int ExportView::format() const
{
    return format_->currentIndex();
}

void ExportView::focusPath()
{
    path_->setFocus();
    const QString text = path_->text();
    const int name = text.lastIndexOf(QLatin1Char('/')) + 1;
    const int dot = text.lastIndexOf(QLatin1Char('.'));
    path_->setSelection(name, (dot > name ? dot : text.size()) - name);
}

void ExportView::updateWarning()
{
    const QFileInfo target(path());
    if (path().isEmpty())
        warning_->setText(tr("Where should the file go?"));
    else if (target.isDir())
        warning_->setText(tr("That is a folder: add a file name."));
    else if (target.exists())
        warning_->setText(tr("A file of that name is already there; it will be replaced."));
    else
        warning_->clear();
    export_->setEnabled(!path().isEmpty() && !target.isDir());
}

} // namespace pinax::ui
