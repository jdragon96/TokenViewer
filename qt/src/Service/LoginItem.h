#pragma once

#include <QString>

class QSettings;

class LoginItem
{
public:
    static constexpr const char* kValueName = "TokenViewer";
    static constexpr const char* kQtRuntimeFile = "Qt5Core.dll";

public:
    static QString BuildCommand(const QString& executablePath);
    static bool HasQtRuntime(const QString& executablePath);
    static bool Apply(QSettings* runKey, bool enabled, const QString& executablePath);
};
