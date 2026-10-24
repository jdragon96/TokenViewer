#include "Service/LoginItem.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

QString LoginItem::BuildCommand(const QString& executablePath)
{
    // Quoted so a path with spaces (e.g. under "Program Files") stays one argument.
    return QLatin1Char('"') + QDir::toNativeSeparators(executablePath) + QLatin1Char('"');
}

bool LoginItem::HasQtRuntime(const QString& executablePath)
{
    return QFileInfo::exists(QFileInfo(executablePath).absolutePath() + QLatin1Char('/') + QLatin1String(kQtRuntimeFile));
}

bool LoginItem::Apply(QSettings* runKey, bool enabled, const QString& executablePath)
{
    if (runKey == nullptr)
    {
        return false;
    }
    const QString strValueName = QString::fromLatin1(kValueName);
    if (!enabled)
    {
        if (!runKey->contains(strValueName))
        {
            return true;
        }
        runKey->remove(strValueName);
        runKey->sync();
        return runKey->status() == QSettings::NoError;
    }
    // A build-tree binary has no Qt DLLs beside it and would fail to start at login.
    if (!HasQtRuntime(executablePath))
    {
        return false;
    }

    const QString strCommand = BuildCommand(executablePath);
    if (runKey->value(strValueName).toString() == strCommand)
    {
        return true;
    }
    runKey->setValue(strValueName, strCommand);
    runKey->sync();
    return runKey->status() == QSettings::NoError;
}
