#include "ui/theme.h"

#include "ui/style.h"

#include <QApplication>
#include <QEvent>
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
};

const SystemLook& systemLook()
{
    static const SystemLook look {QApplication::style()->name(), QApplication::palette()};
    return look;
}

QPalette makePalette(QColor window, QColor base, QColor alternate, QColor text, QColor muted, QColor button,
    QColor highlight, QColor highlightedText, QColor light, QColor mid, QColor dark, QColor disabled)
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
    p.setColor(QPalette::Link, accent());
    p.setColor(QPalette::LinkVisited, accent());
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

} // namespace

QString themeKey(Theme theme)
{
    switch (theme) {
    case Theme::Light: return QStringLiteral("light");
    case Theme::Dark: return QStringLiteral("dark");
    case Theme::System: break;
    }
    return QStringLiteral("system");
}

std::optional<Theme> themeFromKey(const QString& key)
{
    for (Theme theme : {Theme::System, Theme::Light, Theme::Dark})
        if (key == themeKey(theme))
            return theme;
    return std::nullopt;
}

QPalette themePalette(Theme theme)
{
    switch (theme) {
    case Theme::Light:
        // Warm paper, so the amber accent sits as it does in the dark.
        return makePalette(QColor(0xF3, 0xF0, 0xE9), QColor(0xFC, 0xFB, 0xF8), QColor(0xF0, 0xEC, 0xE3),
            QColor(0x1F, 0x1B, 0x16), QColor(0x7A, 0x72, 0x66), QColor(0xE9, 0xE4, 0xDA),
            QColor(0xE8, 0xC2, 0x77), QColor(0x1A, 0x17, 0x14), QColor(0xFF, 0xFF, 0xFF),
            QColor(0xC9, 0xC2, 0xB5), QColor(0xA8, 0xA0, 0x92), QColor(0xA8, 0xA0, 0x92));
    case Theme::Dark:
        // The mock-up's near-black browns and parchment text (design/).
        return makePalette(QColor(0x1C, 0x1A, 0x17), QColor(0x15, 0x13, 0x10), QColor(0x1F, 0x1C, 0x18),
            QColor(0xE8, 0xE2, 0xD6), QColor(0x8C, 0x85, 0x7A), QColor(0x2A, 0x26, 0x22),
            QColor(0x6B, 0x54, 0x28), QColor(0xFF, 0xF8, 0xE8), QColor(0x3A, 0x35, 0x2F),
            QColor(0x3A, 0x35, 0x2F), QColor(0x0F, 0x0E, 0x0C), QColor(0x6B, 0x65, 0x5C));
    case Theme::System: break;
    }
    return systemLook().palette;
}

void applyTheme(Theme theme)
{
    const SystemLook& system = systemLook(); // kept before anything changes
    const QString style = theme == Theme::System ? system.style : QStringLiteral("Fusion");
    if (QApplication::style()->name().compare(style, Qt::CaseInsensitive) != 0)
        if (QStyle* replacement = QStyleFactory::create(style))
            QApplication::setStyle(replacement);
    QApplication::setPalette(themePalette(theme));
    // Qt tells existing widgets only once its event loop runs; the theme
    // saved is applied before that, at start-up. Telling them again is
    // harmless: a widget whose palette is already right sees no change.
    QEvent change(QEvent::ApplicationPaletteChange);
    for (QWidget* widget : QApplication::allWidgets())
        QApplication::sendEvent(widget, &change);
}

} // namespace pinax::ui
