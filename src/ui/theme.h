#pragma once

#include <QColor>
#include <QPalette>
#include <QString>

#include <optional>
#include <vector>

namespace pinax::ui {

// How the window is coloured (F-029, D-030). System leaves the desktop's
// style, palette and font alone; the others put Fusion under Pinax's own
// palettes. Dark is taken from the mock-up (design/). Terminal is green
// phosphor on black, in a monospace font. Cassette Futurism is the beige
// plastic, brown and rust of late-1970s machines.
enum class Theme { System, Light, Dark, Terminal, Cassette };

// Every theme, in menu order.
std::vector<Theme> allThemes();

// The settings value: "system", "light", "dark", "terminal", "cassette".
QString themeKey(Theme theme);
std::optional<Theme> themeFromKey(const QString& key);
// What the menu calls it: "Cassette Futurism".
QString themeName(Theme theme);

// Pinax's palette for a theme. For System, the desktop's palette as it was
// before Pinax first changed it.
QPalette themePalette(Theme theme);
// The theme's accent: read marks, ratings, series progress. Amber unless a
// theme says otherwise.
QColor themeAccent(Theme theme);

// The theme now applied.
Theme currentTheme();

// Applies the theme to the whole application, at once: style, palette,
// accent and font. Widgets that colour themselves from palette roles, or
// read accent() when they paint, follow; anything that baked a colour in
// must redo it on QEvent::PaletteChange.
void applyTheme(Theme theme);

} // namespace pinax::ui
