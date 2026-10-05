#include "Service/LoginItem.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace
{
const QString kLaunchAgentsDirectory = QStringLiteral("/Library/LaunchAgents/");
const QString kBundleExecutableMarker = QStringLiteral(".app/Contents/MacOS/");
const QString kPlistTemplate = QStringLiteral(R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>%1</string>
    <key>ProgramArguments</key>
    <array>
        <string>%2</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>LimitLoadToSessionType</key>
    <string>Aqua</string>
    <key>ProcessType</key>
    <string>Interactive</string>
</dict>
</plist>
)");
}

QString LoginItem::GetPlistPath()
{
    return QDir::homePath() + kLaunchAgentsDirectory + QString::fromLatin1(kLabel) + QStringLiteral(".plist");
}

QByteArray LoginItem::BuildPlist(const QString& executablePath)
{
    return kPlistTemplate.arg(QString::fromLatin1(kLabel), executablePath.toHtmlEscaped()).toUtf8();
}

bool LoginItem::IsBundledExecutable(const QString& executablePath)
{
    return executablePath.contains(kBundleExecutableMarker);
}

bool LoginItem::Apply(bool enabled, const QString& executablePath)
{
    const QString strPlistPath = GetPlistPath();
    if (!enabled)
    {
        return !QFile::exists(strPlistPath) || QFile::remove(strPlistPath);
    }
    // Never register a bare build-tree binary; launchd would start it without its bundle.
    if (!IsBundledExecutable(executablePath))
    {
        return false;
    }

    const QByteArray baPlist = BuildPlist(executablePath);
    QFile fileExisting(strPlistPath);
    if (fileExisting.open(QIODevice::ReadOnly) && fileExisting.readAll() == baPlist)
    {
        return true;
    }
    fileExisting.close();

    QDir().mkpath(QFileInfo(strPlistPath).path());
    QSaveFile fileNew(strPlistPath);
    if (!fileNew.open(QIODevice::WriteOnly))
    {
        return false;
    }
    fileNew.write(baPlist);
    return fileNew.commit();
}
