#pragma once

#include "domain/enums.h"

#include <QColor>
#include <QString>

class QLabel;
class QWidget;

namespace pinax::ui {

// The mock-up's amber (design/): read marks, the rating, series progress.
QColor accent();

// A muted colour for "not recorded" and placeholder text, taken from the
// current palette so it works in light and dark themes. For painting and
// stylesheets; a widget's text is muted with setMuted, which follows a
// change of theme by itself.
QColor muted(const QWidget* widget);

// Draws the widget's text in the palette's muted colour (PlaceholderText),
// or in its ordinary colour again. A role, not a colour, so a change of
// theme reaches it (F-029).
void setMuted(QWidget* widget, bool isMuted = true);

// The thin series progress bar's stylesheet: a track in the palette's Mid,
// filled in the accent. Baked from the palette, so set it again on
// QEvent::PaletteChange.
QString progressStyle(const QWidget* widget);

// The read-state mark of the list's first column: ● read, ◐ reading,
// ○ unread, × abandoned.
QString readMark(domain::ReadStatus status);

// A volume the series contains but the shelf does not: a dashed circle.
QString missingMark();

// The small, spaced, upper-case headings of the mock-up's detail panel:
// SERIES, SYNOPSIS, EDITION.
QLabel* makeSectionHeading(const QString& text, QWidget* parent);

} // namespace pinax::ui
