#pragma once

class QKeyEvent;

namespace pinax::ui {

// The single keys a list acts on for its selection (F-005, F-007, F-001), so
// the book list and a series' list answer them alike. Anything with Ctrl, Alt
// or Meta held is not one of them.
struct ListKeyAction {
    enum class Kind { None, ToggleRead, Rate, Delete };
    Kind kind = Kind::None;
    int rating = 0; // for Rate: 1-10, or 0 to clear
};

ListKeyAction listKeyAction(const QKeyEvent* event);

} // namespace pinax::ui
