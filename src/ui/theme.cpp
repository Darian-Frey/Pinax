#include "ui/theme.h"

#include <QApplication>
#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <QStyle>
#include <QStyleFactory>
#include <QWidget>

namespace pinax::ui {

namespace {

// The desktop's look, kept from before Pinax first changed it, so that
// System can put it back.
struct SystemLook {
    QString style;
    QPalette palette;
    QFont font;
};

const SystemLook& systemLook()
{
    static const SystemLook look {QApplication::style()->name(), QApplication::palette(), QApplication::font()};
    return look;
}

const QColor amber(0xD9, 0xA4, 0x41); // the mock-up's accent (design/)

Theme current = Theme::System;

QPalette makePalette(QColor window, QColor base, QColor alternate, QColor text, QColor muted, QColor button,
    QColor highlight, QColor highlightedText, QColor light, QColor mid, QColor dark, QColor disabled, QColor link)
{
    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, alternate);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::PlaceholderText, muted);
    p.setColor(QPalette::Button, button);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, highlight);
    p.setColor(QPalette::HighlightedText, highlightedText);
    p.setColor(QPalette::ToolTipBase, button);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Link, link);
    p.setColor(QPalette::LinkVisited, link);
    p.setColor(QPalette::Light, light);
    p.setColor(QPalette::Midlight, light);
    p.setColor(QPalette::Mid, mid);
    p.setColor(QPalette::Dark, dark);
    p.setColor(QPalette::Shadow, Qt::black);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::PlaceholderText})
        p.setColor(QPalette::Disabled, role, disabled);
    p.setColor(QPalette::Disabled, QPalette::Highlight, mid);
    return p;
}

// A font from the system's fixed-pitch family, at the desktop's size.
QFont monospaceFont()
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    const QFont& desktop = systemLook().font;
    if (desktop.pointSizeF() > 0)
        font.setPointSizeF(desktop.pointSizeF());
    return font;
}

} // namespace

std::vector<Theme> allThemes()
{
    return {Theme::System, Theme::Light, Theme::Dark, Theme::Terminal, Theme::Cassette};
}

QString themeKey(Theme theme)
{
    switch (theme) {
    case Theme::Light: return QStringLiteral("light");
    case Theme::Dark: return QStringLiteral("dark");
    case Theme::Terminal: return QStringLiteral("terminal");
    case Theme::Cassette: return QStringLiteral("cassette");
    case Theme::System: break;
    }
    return QStringLiteral("system");
}

std::optional<Theme> themeFromKey(const QString& key)
{
    for (Theme theme : allThemes())
        if (key == themeKey(theme))
            return theme;
    return std::nullopt;
}

QString themeName(Theme theme)
{
    switch (theme) {
    case Theme::Light: return QApplication::translate("Theme", "&Light");
    case Theme::Dark: return QApplication::translate("Theme", "&Dark");
    case Theme::Terminal: return QApplication::translate("Theme", "&Terminal");
    case Theme::Cassette: return QApplication::translate("Theme", "&Cassette Futurism");
    case Theme::System: break;
    }
    return QApplication::translate("Theme", "&System");
}

QColor themeAccent(Theme theme)
{
    switch (theme) {
    case Theme::Terminal: return QColor(0x4D, 0xFF, 0x7A); // phosphor at full brightness
    case Theme::Cassette: return QColor(0xC8, 0x50, 0x1E); // rust, as on a tape deck's stripes
    case Theme::System:
    case Theme::Light:
    case Theme::Dark: break;
    }
    return amber;
}

QPalette themePalette(Theme theme)
{
    switch (theme) {
    case Theme::Light:
        // Warm paper, so the amber accent sits as it does in the dark.
        return makePalette(QColor(0xF3, 0xF0, 0xE9), QColor(0xFC, 0xFB, 0xF8), QColor(0xF0, 0xEC, 0xE3),
            QColor(0x1F, 0x1B, 0x16), QColor(0x7A, 0x72, 0x66), QColor(0xE9, 0xE4, 0xDA),
            QColor(0xE8, 0xC2, 0x77), QColor(0x1A, 0x17, 0x14), QColor(0xFF, 0xFF, 0xFF),
            QColor(0xC9, 0xC2, 0xB5), QColor(0xA8, 0xA0, 0x92), QColor(0xA8, 0xA0, 0x92), amber);
    case Theme::Dark:
        // The mock-up's near-black browns and parchment text (design/).
        return makePalette(QColor(0x1C, 0x1A, 0x17), QColor(0x15, 0x13, 0x10), QColor(0x1F, 0x1C, 0x18),
            QColor(0xE8, 0xE2, 0xD6), QColor(0x8C, 0x85, 0x7A), QColor(0x2A, 0x26, 0x22),
            QColor(0x6B, 0x54, 0x28), QColor(0xFF, 0xF8, 0xE8), QColor(0x3A, 0x35, 0x2F),
            QColor(0x3A, 0x35, 0x2F), QColor(0x0F, 0x0E, 0x0C), QColor(0x6B, 0x65, 0x5C), amber);
    case Theme::Terminal:
        // Green phosphor on a black tube; the selection a brighter band.
        return makePalette(QColor(0x0B, 0x11, 0x0C), QColor(0x07, 0x0B, 0x08), QColor(0x0D, 0x15, 0x0F),
            QColor(0x8C, 0xF5, 0xA1), QColor(0x4E, 0x8F, 0x5C), QColor(0x12, 0x20, 0x16),
            QColor(0x1E, 0x5C, 0x30), QColor(0xD4, 0xFF, 0xDD), QColor(0x1A, 0x2E, 0x1F),
            QColor(0x24, 0x40, 0x2B), QColor(0x03, 0x05, 0x03), QColor(0x3A, 0x5E, 0x42),
            themeAccent(Theme::Terminal));
    case Theme::Cassette:
        // Beige plastic, brown lettering, a mustard selection, rust accents.
        return makePalette(QColor(0xE6, 0xDC, 0xC6), QColor(0xF2, 0xEA, 0xDA), QColor(0xEA, 0xE0, 0xCB),
            QColor(0x3A, 0x2C, 0x20), QColor(0x7E, 0x6A, 0x55), QColor(0xD9, 0xCB, 0xB0),
            QColor(0xE9, 0xA2, 0x3B), QColor(0x24, 0x18, 0x0E), QColor(0xFB, 0xF6, 0xEC),
            QColor(0xBF, 0xAE, 0x90), QColor(0x8A, 0x78, 0x60), QColor(0xA8, 0x9A, 0x84),
            themeAccent(Theme::Cassette));
    case Theme::System: break;
    }
    return systemLook().palette;
}

Theme currentTheme()
{
    return current;
}

void applyTheme(Theme theme)
{
    const SystemLook& system = systemLook(); // kept before anything changes
    current = theme; // first, so accent() answers for the new theme as widgets redraw
    const QString style = theme == Theme::System ? system.style : QStringLiteral("Fusion");
    if (QApplication::style()->name().compare(style, Qt::CaseInsensitive) != 0)
        if (QStyle* replacement = QStyleFactory::create(style))
            QApplication::setStyle(replacement);
    QApplication::setFont(theme == Theme::Terminal ? monospaceFont() : system.font);
    QApplication::setPalette(themePalette(theme));
    // Qt tells existing widgets only once its event loop runs; the theme
    // saved is applied before that, at start-up. Telling them again is
    // harmless: a widget already right sees no change.
    QEvent fontChange(QEvent::ApplicationFontChange);
    QEvent paletteChange(QEvent::ApplicationPaletteChange);
    for (QWidget* widget : QApplication::allWidgets()) {
        QApplication::sendEvent(widget, &fontChange);
        QApplication::sendEvent(widget, &paletteChange);
    }
}

} // namespace pinax::ui
