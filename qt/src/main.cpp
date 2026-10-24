#include "Core/Application.h"
#include "Core/SingleInstance.h"

#include <QApplication>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace
{
constexpr int kSendTimeoutMs = 3000;
const QString kQuitArgument = QStringLiteral("--quit");
}

int main(int argc, char* argv[])
{
    Application::ConfigureHighDpi();
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("TokenViewer"));
    QApplication::setOrganizationName(QStringLiteral("TokenViewer"));
    QApplication::setOrganizationDomain(QStringLiteral("tokenviewer.com"));
    QApplication::setApplicationVersion(QStringLiteral(TV_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);

    const bool bQuit = QCoreApplication::arguments().contains(kQuitArgument);
    const QString strServerName = SingleInstance::BuildServerName();
    SingleInstance singleInstance(strServerName);
    if (!singleInstance.Listen())
    {
        // Another instance is running: hand it the command and leave.
#ifdef Q_OS_WIN
        // Windows lets only the process the user just started bring a window to the front; pass that right on.
        if (!bQuit)
        {
            AllowSetForegroundWindow(ASFW_ANY);
        }
#endif
        if (!SingleInstance::SendCommand(strServerName, bQuit ? EInstanceCommand::QUIT : EInstanceCommand::ACTIVATE, kSendTimeoutMs))
        {
            qWarning("TokenViewer: the running instance did not answer");
        }
        return 0;
    }
    if (bQuit)
    {
        // Nothing was running.
        return 0;
    }

    Application application;
    QObject::connect(&singleInstance, &SingleInstance::ActivateRequested, &application, &Application::ActivatePopup);
    QObject::connect(&singleInstance, &SingleInstance::QuitRequested, &application, &Application::RequestQuit);
    application.Start();
    return app.exec();
}
