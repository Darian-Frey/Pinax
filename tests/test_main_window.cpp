#include "app/main_window.h"

#include <QSplitter>
#include <QTest>

class TestMainWindow : public QObject {
    Q_OBJECT

private slots:
    void opensWithThreePanels();
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
    QCOMPARE(splitter->widget(1)->objectName(), QStringLiteral("list"));
    QCOMPARE(splitter->widget(2)->objectName(), QStringLiteral("detail"));
}

QTEST_MAIN(TestMainWindow)
#include "test_main_window.moc"
