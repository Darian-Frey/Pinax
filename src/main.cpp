#include "app/main_window.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Pinax"));
    QApplication::setOrganizationName(QStringLiteral("Pinax"));
    QApplication::setApplicationVersion(QStringLiteral(PINAX_VERSION));

    pinax::app::MainWindow window;
    window.show();

    return QApplication::exec();
}
