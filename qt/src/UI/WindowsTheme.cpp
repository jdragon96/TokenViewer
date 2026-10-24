#include "UI/WindowsTheme.h"

#include <QApplication>
#include <QColor>
#include <QSettings>
#include <QStyle>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

namespace
{
constexpr const char* kFusionStyleClassName = "QFusionStyle";
const QString kFusionStyleName = QStringLiteral("Fusion");

// Dark: Windows 11 dark flyout surface and text.
const QColor kWindow(0x2c, 0x2c, 0x2c);
const QColor kBase(0x38, 0x38, 0x38);
const QColor kAlternateBase(0x32, 0x32, 0x32);
const QColor kButton(0x37, 0x37, 0x37);
const QColor kText(0xff, 0xff, 0xff);
const QColor kDisabledText(0x78, 0x78, 0x78);
const QColor kPlaceholderText(0x9d, 0x9d, 0x9d);
const QColor kLight(0x45, 0x45, 0x45);
const QColor kMidlight(0x3d, 0x3d, 0x3d);
const QColor kMid(0x26, 0x26, 0x26);
const QColor kDark(0x1a, 0x1a, 0x1a);
const QColor kShadow(0x00, 0x00, 0x00);
const QColor kHighlight(0x39, 0x87, 0xe5);
const QColor kDisabledHighlight(0x4a, 0x4a, 0x4a);
const QColor kBrightText(0xff, 0x6b, 0x6b);
const QColor kLink(0x6c, 0xb4, 0xff);
const QColor kLinkVisited(0xb4, 0x9c, 0xff);
const QColor kToolTipBase(0x2c, 0x2c, 0x2c);

// Light: Windows 11 light flyout surface and text, instead of Fusion's beige grey.
const QColor kLightWindow(0xf9, 0xf9, 0xf9);
const QColor kLightBase(0xff, 0xff, 0xff);
const QColor kLightAlternateBase(0xf3, 0xf3, 0xf3);
const QColor kLightButton(0xfd, 0xfd, 0xfd);
const QColor kLightText(0x1a, 0x1a, 0x1a);
const QColor kLightDisabledText(0xa0, 0xa0, 0xa0);
const QColor kLightPlaceholderText(0x8a, 0x8a, 0x8a);
const QColor kLightLight(0xff, 0xff, 0xff);
const QColor kLightMidlight(0xf0, 0xf0, 0xf0);
const QColor kLightMid(0xd1, 0xd1, 0xd1);
const QColor kLightDark(0xa0, 0xa0, 0xa0);
const QColor kLightShadow(0x60, 0x60, 0x60);
const QColor kLightHighlight(0x2a, 0x78, 0xd6);
const QColor kLightDisabledHighlight(0xd0, 0xd0, 0xd0);
const QColor kLightHighlightedText(0xff, 0xff, 0xff);
const QColor kLightBrightText(0xc4, 0x2b, 0x1c);
const QColor kLightLink(0x00, 0x67, 0xc0);
const QColor kLightLinkVisited(0x7a, 0x3f, 0xb5);

const QColor kSecondaryTextLight(0x5f, 0x5f, 0x5f);
const QColor kSecondaryTextDark(0xc8, 0xc8, 0xc8);
const QColor kFrameBorderLight(0xd4, 0xd4, 0xd4);
const QColor kFrameBorderDark(0x48, 0x48, 0x48);

// Windows 11 control fills and strokes, already blended onto the flyout surface.
struct ControlColors
{
    const char* m_pszFill;
    const char* m_pszHover;
    const char* m_pszPressed;
    const char* m_pszStroke;
    const char* m_pszStrokeBottom;
    const char* m_pszPressedText;
    const char* m_pszListBackground;
    const char* m_pszListHover;
    const char* m_pszListBorder;
};

constexpr ControlColors kLightControls = { "#fdfdfd", "#f5f5f5", "#f0f0f0", "#e3e3e3", "#c9c9c9", "#5f5f5f", "#fbfbfb", "#ececec", "#d6d6d6" };
constexpr ControlColors kDarkControls = { "#373737", "#3d3d3d", "#323232", "#404040", "#363636", "#c8c8c8", "#2c2c2c", "#3a3a3a", "#454545" };

const char* const kControlStyleSheet = R"(
QPushButton { background: %1; border: 1px solid %4; border-bottom-color: %5; border-radius: 4px; padding: 4px 12px; min-height: 18px; }
QPushButton:hover { background: %2; }
QPushButton:pressed { background: %3; color: %6; border-bottom-color: %4; }
QComboBox { background: %1; border: 1px solid %4; border-bottom-color: %5; border-radius: 4px; padding: 3px 8px 3px 10px; min-height: 18px; }
QComboBox:hover { background: %2; }
QComboBox:on { background: %3; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView { background: %7; border: 1px solid %9; padding: 4px; outline: 0; }
QComboBox QAbstractItemView::item { min-height: 26px; padding: 0px 8px; border-radius: 4px; color: palette(text); }
QComboBox QAbstractItemView::item:selected { background: %8; color: palette(text); }
)";

#ifdef Q_OS_WIN
// Windows 11 SDK values, missing from MinGW 8.1's dwmapi.h.
constexpr DWORD kDwmUseImmersiveDarkMode = 20;
constexpr DWORD kDwmUseImmersiveDarkModeBefore20H1 = 19;
constexpr DWORD kDwmWindowCornerPreference = 33;
constexpr DWORD kDwmBorderColor = 34;
constexpr int kDwmCornerRound = 2;
#endif
}

bool WindowsTheme::IsTaskbarLight(QSettings* personalizeKey)
{
    // Missing before Windows 10 1903, when the taskbar was always dark.
    return personalizeKey != nullptr && personalizeKey->value(QLatin1String(kTaskbarValueName), 0).toInt() != 0;
}

bool WindowsTheme::IsAppsDark(QSettings* personalizeKey)
{
    return personalizeKey != nullptr && personalizeKey->value(QLatin1String(kAppsValueName), 1).toInt() == 0;
}

QPalette WindowsTheme::BuildDarkPalette()
{
    QPalette palette;
    palette.setColor(QPalette::Window, kWindow);
    palette.setColor(QPalette::WindowText, kText);
    palette.setColor(QPalette::Base, kBase);
    palette.setColor(QPalette::AlternateBase, kAlternateBase);
    palette.setColor(QPalette::ToolTipBase, kToolTipBase);
    palette.setColor(QPalette::ToolTipText, kText);
    palette.setColor(QPalette::PlaceholderText, kPlaceholderText);
    palette.setColor(QPalette::Text, kText);
    palette.setColor(QPalette::Button, kButton);
    palette.setColor(QPalette::ButtonText, kText);
    palette.setColor(QPalette::BrightText, kBrightText);
    palette.setColor(QPalette::Light, kLight);
    palette.setColor(QPalette::Midlight, kMidlight);
    palette.setColor(QPalette::Mid, kMid);
    palette.setColor(QPalette::Dark, kDark);
    palette.setColor(QPalette::Shadow, kShadow);
    palette.setColor(QPalette::Highlight, kHighlight);
    palette.setColor(QPalette::HighlightedText, kText);
    palette.setColor(QPalette::Link, kLink);
    palette.setColor(QPalette::LinkVisited, kLinkVisited);

    palette.setColor(QPalette::Disabled, QPalette::WindowText, kDisabledText);
    palette.setColor(QPalette::Disabled, QPalette::Text, kDisabledText);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, kDisabledText);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, kDisabledHighlight);
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, kDisabledText);
    return palette;
}

QPalette WindowsTheme::BuildLightPalette()
{
    QPalette palette;
    palette.setColor(QPalette::Window, kLightWindow);
    palette.setColor(QPalette::WindowText, kLightText);
    palette.setColor(QPalette::Base, kLightBase);
    palette.setColor(QPalette::AlternateBase, kLightAlternateBase);
    palette.setColor(QPalette::ToolTipBase, kLightWindow);
    palette.setColor(QPalette::ToolTipText, kLightText);
    palette.setColor(QPalette::PlaceholderText, kLightPlaceholderText);
    palette.setColor(QPalette::Text, kLightText);
    palette.setColor(QPalette::Button, kLightButton);
    palette.setColor(QPalette::ButtonText, kLightText);
    palette.setColor(QPalette::BrightText, kLightBrightText);
    palette.setColor(QPalette::Light, kLightLight);
    palette.setColor(QPalette::Midlight, kLightMidlight);
    palette.setColor(QPalette::Mid, kLightMid);
    palette.setColor(QPalette::Dark, kLightDark);
    palette.setColor(QPalette::Shadow, kLightShadow);
    palette.setColor(QPalette::Highlight, kLightHighlight);
    palette.setColor(QPalette::HighlightedText, kLightHighlightedText);
    palette.setColor(QPalette::Link, kLightLink);
    palette.setColor(QPalette::LinkVisited, kLightLinkVisited);

    palette.setColor(QPalette::Disabled, QPalette::WindowText, kLightDisabledText);
    palette.setColor(QPalette::Disabled, QPalette::Text, kLightDisabledText);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, kLightDisabledText);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, kLightDisabledHighlight);
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, kLightWindow);
    return palette;
}

QPalette WindowsTheme::BuildAppPalette(QSettings* personalizeKey)
{
    if (IsAppsDark(personalizeKey))
    {
        return BuildDarkPalette();
    }
    // Built once: the app asks every 5 s, and the same palette object keeps its cache key.
    static const QPalette palLight = BuildLightPalette();
    return palLight;
}

QFont WindowsTheme::BuildAppFont()
{
    // Qt 5 starts from DEFAULT_GUI_FONT, which is Gulim on Korean Windows; Hangul falls back to Malgun Gothic.
    QFont font(QString::fromLatin1(kUiFontFamily));
    font.setFamilies({ QString::fromLatin1(kUiFontFamily), QString::fromLatin1(kHangulFontFamily) });
    font.setPointSize(kUiFontPointSize);
    return font;
}

QFont WindowsTheme::BuildSemiboldFont(int pixelSize)
{
    // GDI keeps Segoe UI's semibold face in its own family; plain "Segoe UI" at 600 would give bold.
    QFont font(QString::fromLatin1(kUiSemiboldFontFamily));
    font.setFamilies({ QString::fromLatin1(kUiSemiboldFontFamily), QString::fromLatin1(kUiFontFamily), QString::fromLatin1(kHangulFontFamily) });
    font.setPixelSize(pixelSize);
    font.setWeight(QFont::DemiBold);
    return font;
}

bool WindowsTheme::ApplyAppTheme(QSettings* personalizeKey)
{
    // Fusion follows the palette; the native windowsvista style ignores it and stays light.
    if (!QApplication::style()->inherits(kFusionStyleClassName))
    {
        QApplication::setStyle(kFusionStyleName);
    }
    bool bChanged = false;
    const QFont fontApp = BuildAppFont();
    if (QApplication::font() != fontApp)
    {
        QApplication::setFont(fontApp);
        bChanged = true;
    }
    const QPalette palApp = BuildAppPalette(personalizeKey);
    if (QApplication::palette() != palApp)
    {
        QApplication::setPalette(palApp);
        bChanged = true;
    }
    return bChanged;
}

QString WindowsTheme::ChooseIconFontFamily(const QStringList& installedFamilies)
{
    // Windows 11 ships Segoe Fluent Icons, Windows 10 Segoe MDL2 Assets; both use the same code points.
    for (const char* pszFamily : { kFluentIconFontFamily, kMdl2IconFontFamily })
    {
        const QString strFamily = QString::fromLatin1(pszFamily);
        if (installedFamilies.contains(strFamily, Qt::CaseInsensitive))
        {
            return strFamily;
        }
    }
    return QString();
}

QString WindowsTheme::BuildControlStyleSheet(bool dark)
{
    const ControlColors& colors = dark ? kDarkControls : kLightControls;
    return QString::fromLatin1(kControlStyleSheet)
        .arg(QLatin1String(colors.m_pszFill), QLatin1String(colors.m_pszHover), QLatin1String(colors.m_pszPressed),
            QLatin1String(colors.m_pszStroke), QLatin1String(colors.m_pszStrokeBottom), QLatin1String(colors.m_pszPressedText),
            QLatin1String(colors.m_pszListBackground), QLatin1String(colors.m_pszListHover), QLatin1String(colors.m_pszListBorder));
}

QColor WindowsTheme::GetSecondaryTextColor(bool dark)
{
    return dark ? kSecondaryTextDark : kSecondaryTextLight;
}

QColor WindowsTheme::GetFrameBorderColor(bool dark)
{
    return dark ? kFrameBorderDark : kFrameBorderLight;
}

bool WindowsTheme::ApplyRoundedCorners(WId window, const QColor& borderColor)
{
#ifdef Q_OS_WIN
    const HWND hWindow = reinterpret_cast<HWND>(window);
    const int iPreference = kDwmCornerRound;
    // Fails before Windows 11 (build 22000); the caller then draws a square border itself.
    if (FAILED(DwmSetWindowAttribute(hWindow, kDwmWindowCornerPreference, &iPreference, sizeof(iPreference))))
    {
        return false;
    }
    // Without this the border takes the accent colour when "show accent colour on borders" is on.
    const COLORREF clrBorder = RGB(borderColor.red(), borderColor.green(), borderColor.blue());
    DwmSetWindowAttribute(hWindow, kDwmBorderColor, &clrBorder, sizeof(clrBorder));
    return true;
#else
    Q_UNUSED(window);
    Q_UNUSED(borderColor);
    return false;
#endif
}

bool WindowsTheme::ApplyDarkTitleBar(WId window, bool dark)
{
#ifdef Q_OS_WIN
    const HWND hWindow = reinterpret_cast<HWND>(window);
    const BOOL bDark = dark ? TRUE : FALSE;
    if (SUCCEEDED(DwmSetWindowAttribute(hWindow, kDwmUseImmersiveDarkMode, &bDark, sizeof(bDark))))
    {
        return true;
    }
    // Windows 10 1809-1909 used an undocumented number for the same attribute.
    return SUCCEEDED(DwmSetWindowAttribute(hWindow, kDwmUseImmersiveDarkModeBefore20H1, &bDark, sizeof(bDark)));
#else
    Q_UNUSED(window);
    Q_UNUSED(dark);
    return false;
#endif
}
