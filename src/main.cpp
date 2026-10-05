#include "Core/Application.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("TokenViewer"));
    QApplication::setOrganizationName(QStringLiteral("TokenViewer"));
    QApplication::setOrganizationDomain(QStringLiteral("tokenviewer.com"));
    QApplication::setApplicationVersion(QStringLiteral(TV_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);

    Application application;
    application.Start();
    return app.exec();
}
