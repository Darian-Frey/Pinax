#pragma once

#include <QColor>
#include <QString>

class QLabel;
class QWidget;

namespace pinax::ui {

// The mock-up's amber (design/): read marks, the rating, series progress.
QColor accent();

// A muted colour for "not recorded" and placeholder text, taken from the
// current palette so it works in light and dark themes.
QColor muted(const QWidget* widget);

// The small, spaced, upper-case headings of the mock-up's detail panel:
// SERIES, SYNOPSIS, EDITION.
QLabel* makeSectionHeading(const QString& text, QWidget* parent);

} // namespace pinax::ui
