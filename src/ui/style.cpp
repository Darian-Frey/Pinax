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
    QColor colour = widget->palette().color(QPalette::WindowText);
    colour.setAlphaF(0.55f);
    return colour;
}

QLabel* makeSectionHeading(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text.toUpper(), parent);
    QFont font = label->font();
    font.setPointSizeF(font.pointSizeF() * 0.8);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
    font.setBold(true);
    label->setFont(font);

    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, muted(label));
    label->setPalette(palette);
    return label;
}

} // namespace pinax::ui
