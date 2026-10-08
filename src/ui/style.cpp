#include "ui/style.h"

#include <QLabel>
#include <QPalette>

namespace pinax::ui {

QColor accent()
{
    return QColor(0xD9, 0xA4, 0x41);
}

QColor muted(const QWidget* widget)
{
    return widget->palette().color(QPalette::PlaceholderText);
}

void setMuted(QWidget* widget, bool isMuted)
{
    widget->setForegroundRole(isMuted ? QPalette::PlaceholderText : QPalette::WindowText);
}

QString progressStyle(const QWidget* widget)
{
    return QStringLiteral("QProgressBar { border: none; background: %1; border-radius: 2px; }"
                          "QProgressBar::chunk { background: %2; border-radius: 2px; }")
        .arg(widget->palette().color(QPalette::Mid).name(), accent().name());
}

QString readMark(domain::ReadStatus status)
{
    switch (status) {
    case domain::ReadStatus::Read: return QStringLiteral("●");
    case domain::ReadStatus::Reading: return QStringLiteral("◐");
    case domain::ReadStatus::Abandoned: return QStringLiteral("×");
    case domain::ReadStatus::Unread: break;
    }
    return QStringLiteral("○");
}

QString missingMark()
{
    return QStringLiteral("◌");
}

QLabel* makeSectionHeading(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text.toUpper(), parent);
    QFont font = label->font();
    font.setPointSizeF(font.pointSizeF() * 0.8);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
    font.setBold(true);
    label->setFont(font);

    setMuted(label);
    return label;
}

} // namespace pinax::ui
