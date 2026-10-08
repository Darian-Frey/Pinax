#pragma once

#include <QPalette>
#include <QString>

#include <optional>

namespace pinax::ui {

// How the window is coloured (F-029, D-030). System leaves the desktop's
// style and palette alone; Light and Dark put Fusion under Pinax's own
// palettes, the dark one taken from the mock-up (design/).
enum class Theme { System, Light, Dark };

// The settings value: "system", "light" or "dark".
QString themeKey(Theme theme);
std::optional<Theme> themeFromKey(const QString& key);

// Pinax's palette for Light or Dark. For System, the desktop's palette as it
// was before Pinax first changed it.
QPalette themePalette(Theme theme);

// Applies the theme to the whole application, at once. Widgets that colour
// themselves from palette roles follow; anything that baked a colour in must
// redo it on QEvent::PaletteChange.
void applyTheme(Theme theme);

} // namespace pinax::ui
