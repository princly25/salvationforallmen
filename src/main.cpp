#include <QApplication>

#include "ui/MainWindow.hpp"

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("AntiDetectBrowser"));
    QApplication::setOrganizationName(QStringLiteral("AntiDetectBrowser"));

    MainWindow window;
    window.show();
    return application.exec();
}
