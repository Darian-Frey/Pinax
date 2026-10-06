#include "ui/list_keys.h"

#include <QKeyEvent>

namespace pinax::ui {

ListKeyAction listKeyAction(const QKeyEvent* event)
{
    using Kind = ListKeyAction::Kind;
    if ((event->modifiers() & ~Qt::KeypadModifier) != Qt::NoModifier)
        return {};

    const int key = event->key();
    if (key == Qt::Key_R)
        return {Kind::ToggleRead, 0};
    if (key >= Qt::Key_1 && key <= Qt::Key_9)
        return {Kind::Rate, key - Qt::Key_0};
    if (key == Qt::Key_0)
        return {Kind::Rate, 10};
    if (key == Qt::Key_Backspace || key == Qt::Key_Minus)
        return {Kind::Rate, 0};
    if (key == Qt::Key_Delete)
        return {Kind::Delete, 0};
    return {};
}

} // namespace pinax::ui
