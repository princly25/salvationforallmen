#include <QApplication>
#include <QLabel>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("AntiDetectBrowser"));
    QApplication::setOrganizationName(QStringLiteral("AntiDetectBrowser"));

    QLabel status(QStringLiteral("AntiDetectBrowser core is ready. Dashboard implementation follows in Module 6."));
    status.setMinimumSize(640, 120);
    status.setAlignment(Qt::AlignCenter);
    status.show();
    return application.exec();
}

