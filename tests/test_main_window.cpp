#include "app/main_window.h"
#include "ui/series_view.h"
#include "ui/theme.h"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QProgressBar>
#include <QSettings>
#include <QSplitter>
#include <QStyle>
#include <QTemporaryDir>
#include <QTest>

using pinax::ui::Theme;

class TestMainWindow : public QObject {
    Q_OBJECT

private slots:
    void opensWithThreePanels();
    void themeKeysRoundTrip();
    void choosingAThemeAppliesAndRemembersIt();
    void mutedTextFollowsTheTheme();
    void systemPutsTheDesktopBack();
};

void TestMainWindow::opensWithThreePanels()
{
    pinax::app::MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QCOMPARE(window.windowTitle(), QStringLiteral("Pinax"));

    const QSplitter* splitter = window.splitter();
    QVERIFY(splitter != nullptr);
    QCOMPARE(splitter->count(), 3);
    QCOMPARE(splitter->widget(0)->objectName(), QStringLiteral("rail"));
    QCOMPARE(splitter->widget(1)->objectName(), QStringLiteral("centre"));
    QCOMPARE(splitter->widget(2)->objectName(), QStringLiteral("detail"));
}

void TestMainWindow::themeKeysRoundTrip()
{
    for (Theme theme : {Theme::System, Theme::Light, Theme::Dark})
        QVERIFY(pinax::ui::themeFromKey(pinax::ui::themeKey(theme)) == theme);
    QVERIFY(!pinax::ui::themeFromKey(QStringLiteral("sepia")));
    QVERIFY(!pinax::ui::themeFromKey(QString()));
}

void TestMainWindow::choosingAThemeAppliesAndRemembersIt()
{
    QTemporaryDir dir;
    QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    {
        pinax::app::MainWindow window;
        window.setSettings(&settings);
        window.applySavedTheme();
        QVERIFY(window.themeAction(Theme::System)->isChecked()); // nothing saved yet

        window.themeAction(Theme::Dark)->trigger();
        QCOMPARE(QApplication::palette().color(QPalette::Window),
            pinax::ui::themePalette(Theme::Dark).color(QPalette::Window));
        QCOMPARE(QApplication::style()->name().toLower(), QStringLiteral("fusion"));
        QVERIFY(window.themeAction(Theme::Dark)->isChecked());
        QVERIFY(!window.themeAction(Theme::System)->isChecked());
        QCOMPARE(settings.value(QStringLiteral("appearance/theme")).toString(), QStringLiteral("dark"));
    }
    // Next start-up: the same theme, shown as chosen.
    pinax::app::MainWindow again;
    again.setSettings(&settings);
    again.applySavedTheme();
    QVERIFY(again.themeAction(Theme::Dark)->isChecked());
    again.chooseTheme(Theme::Light);
    QCOMPARE(QApplication::palette().color(QPalette::Base),
        pinax::ui::themePalette(Theme::Light).color(QPalette::Base));
    QCOMPARE(settings.value(QStringLiteral("appearance/theme")).toString(), QStringLiteral("light"));
    pinax::ui::applyTheme(Theme::System);
}

void TestMainWindow::mutedTextFollowsTheTheme()
{
    // A role, not a baked colour: the label's muted text changes with the
    // theme, without anyone setting it again (F-029).
    pinax::app::MainWindow window;
    auto* shown = window.findChild<QLabel*>(QStringLiteral("filter.shown"));
    QVERIFY(shown);
    QCOMPARE(shown->foregroundRole(), QPalette::PlaceholderText);

    pinax::ui::SeriesView series;
    auto* progress = series.findChild<QProgressBar*>(QStringLiteral("seriesView.progress"));
    QVERIFY(progress);

    for (Theme theme : {Theme::Dark, Theme::Light}) {
        pinax::ui::applyTheme(theme);
        const QPalette expected = pinax::ui::themePalette(theme);
        QCOMPARE(shown->palette().color(QPalette::PlaceholderText), expected.color(QPalette::PlaceholderText));
        // The progress bar's stylesheet is baked, and redone on the change.
        QVERIFY(progress->styleSheet().contains(expected.color(QPalette::Mid).name()));
    }
    pinax::ui::applyTheme(Theme::System);
}

void TestMainWindow::systemPutsTheDesktopBack()
{
    pinax::ui::applyTheme(Theme::System); // the desktop's look, kept from the first call
    const QPalette desktop = QApplication::palette();
    const QString desktopStyle = QApplication::style()->name();

    pinax::ui::applyTheme(Theme::Dark);
    QVERIFY(QApplication::palette().color(QPalette::Window) != desktop.color(QPalette::Window)
        || desktopStyle.toLower() == QStringLiteral("fusion"));
    pinax::ui::applyTheme(Theme::System);
    QCOMPARE(QApplication::palette().color(QPalette::Window), desktop.color(QPalette::Window));
    QCOMPARE(QApplication::palette().color(QPalette::Text), desktop.color(QPalette::Text));
    QCOMPARE(QApplication::style()->name(), desktopStyle);
}

QTEST_MAIN(TestMainWindow)
#include "test_main_window.moc"
