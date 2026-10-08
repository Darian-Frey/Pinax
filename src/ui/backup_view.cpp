#include "ui/backup_view.h"

#include "ui/style.h"

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

BackupView::BackupView(QWidget* parent)
    : QWidget(parent)
    , path_(new QLineEdit(this))
    , choose_(new QPushButton(tr("Choose…"), this))
    , warning_(makeNote(QStringLiteral("backup.warning"), this))
    , outcome_(makeNote(QStringLiteral("backup.outcome"), this))
    , backUp_(new QPushButton(tr("Back up"), this))
    , close_(new QPushButton(tr("Close"), this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(8);
    layout->addWidget(makeSectionHeading(tr("Back up the catalogue"), this));

    auto* intro = makeNote(QStringLiteral("backup.intro"), this);
    intro->setText(tr("A complete copy of the catalogue, taken while Pinax stays open, then checked "
                      "before it is kept. Covers are not included: they can be fetched again."));
    QPalette palette = intro->palette();
    palette.setColor(QPalette::WindowText, muted(intro));
    intro->setPalette(palette);
    layout->addWidget(intro);

    layout->addWidget(new QLabel(tr("Back up to"), this));
    path_->setObjectName(QStringLiteral("backup.path"));
    // Folders and files offered as the path is typed: no dialogue (D-011).
    auto* files = new QFileSystemModel(this);
    files->setRootPath(QString());
    auto* completer = new QCompleter(files, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    path_->setCompleter(completer);
    auto* where = new QHBoxLayout;
    where->addWidget(path_, 1);
    choose_->setObjectName(QStringLiteral("backup.choose"));
    choose_->setAutoDefault(false);
    where->addWidget(choose_);
    layout->addLayout(where);
    connect(choose_, &QPushButton::clicked, this, &BackupView::chooseRequested);
    warning_->setStyleSheet(QStringLiteral("color: %1;").arg(accent().name()));
    layout->addWidget(warning_);

    auto* buttons = new QHBoxLayout;
    backUp_->setObjectName(QStringLiteral("backup.go"));
    backUp_->setDefault(true);
    close_->setObjectName(QStringLiteral("backup.close"));
    close_->setAutoDefault(false);
    buttons->addWidget(backUp_);
    buttons->addWidget(close_);
    buttons->addStretch();
    layout->addLayout(buttons);
    layout->addWidget(outcome_);
    layout->addStretch();

    connect(path_, &QLineEdit::textChanged, this, &BackupView::updateWarning);
    connect(path_, &QLineEdit::returnPressed, backUp_, &QPushButton::click);
    connect(backUp_, &QPushButton::clicked, this, [this] {
        if (!path().isEmpty())
            emit backupRequested(path());
    });
    connect(close_, &QPushButton::clicked, this, &BackupView::closed);
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, &BackupView::closed);
}

void BackupView::start(const QString& suggestedPath)
{
    path_->setText(suggestedPath);
    outcome_->clear();
    backUp_->setEnabled(true);
    updateWarning();
}

void BackupView::showDone(const QString& message)
{
    outcome_->setStyleSheet(QString());
    outcome_->setText(message);
    close_->setFocus();
    // The file there now is the one just written: nothing to warn about
    // until the path changes.
    warning_->clear();
}

void BackupView::showProblem(const QString& message)
{
    outcome_->setStyleSheet(QStringLiteral("color: #d9534f;"));
    outcome_->setText(message);
}

void BackupView::setPath(const QString& path)
{
    path_->setText(path);
}

QString BackupView::path() const
{
    return path_->text().trimmed();
}

void BackupView::focusPath()
{
    path_->setFocus();
    // The name, ready to change; the folder kept.
    const QString text = path_->text();
    const int name = text.lastIndexOf(QLatin1Char('/')) + 1;
    path_->setSelection(name, text.size() - name);
}

void BackupView::updateWarning()
{
    const QFileInfo target(path());
    if (path().isEmpty())
        warning_->setText(tr("Where should the copy go?"));
    else if (target.isDir())
        warning_->setText(tr("That is a folder: add a file name, such as pinax.db."));
    else if (target.exists())
        warning_->setText(tr("A file of that name is already there; it will be replaced."));
    else
        warning_->clear();
    backUp_->setEnabled(!path().isEmpty() && !target.isDir());
}

} // namespace pinax::ui
