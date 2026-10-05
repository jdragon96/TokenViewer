#pragma once

#include <QByteArray>
#include <QString>

class LoginItem
{
public:
    static constexpr const char* kLabel = "com.tokenviewer.TokenViewer";

public:
    static QString GetPlistPath();
    static QByteArray BuildPlist(const QString& executablePath);
    static bool IsBundledExecutable(const QString& executablePath);
    static bool Apply(bool enabled, const QString& executablePath);
};
