#pragma once

#include <QColor>
#include <QFont>
#include <QPalette>
#include <QString>
#include <QStringList>
#include <qwindowdefs.h>

class QSettings;

// Reads the taskbar and app light/dark modes from the registry's Personalize key (injected so tests use an ini file).
class WindowsTheme
{
public:
    static constexpr const char* kPersonalizeKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
    static constexpr const char* kTaskbarValueName = "SystemUsesLightTheme";
    static constexpr const char* kAppsValueName = "AppsUseLightTheme";
    static constexpr const char* kUiFontFamily = "Segoe UI";
    static constexpr const char* kUiSemiboldFontFamily = "Segoe UI Semibold";
    static constexpr const char* kHangulFontFamily = "Malgun Gothic";
    static constexpr int kUiFontPointSize = 9;
    static constexpr const char* kFluentIconFontFamily = "Segoe Fluent Icons";
    static constexpr const char* kMdl2IconFontFamily = "Segoe MDL2 Assets";

public:
    static bool IsTaskbarLight(QSettings* personalizeKey);
    static bool IsAppsDark(QSettings* personalizeKey);
    static QPalette BuildDarkPalette();
    static QPalette BuildLightPalette();
    static QPalette BuildAppPalette(QSettings* personalizeKey);
    static QFont BuildAppFont();
    static QFont BuildSemiboldFont(int pixelSize);
    static bool ApplyAppTheme(QSettings* personalizeKey);
    static QString ChooseIconFontFamily(const QStringList& installedFamilies);
    static QString BuildControlStyleSheet(bool dark);
    static QColor GetSecondaryTextColor(bool dark);
    static QColor GetFrameBorderColor(bool dark);
    static bool ApplyRoundedCorners(WId window, const QColor& borderColor);
    static bool ApplyDarkTitleBar(WId window, bool dark);
};
