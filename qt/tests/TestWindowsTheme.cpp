#include "UI/WindowsTheme.h"

#include <QApplication>
#include <QOperatingSystemVersion>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QtTest>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <cstdlib>
#include <memory>

namespace
{
const QString kTaskbarValue = QStringLiteral("SystemUsesLightTheme");
const QString kAppsValue = QStringLiteral("AppsUseLightTheme");
const QString kFusionStyleName = QStringLiteral("Fusion");
constexpr const char* kFusionStyleClassName = "QFusionStyle";
const QString kFluentIcons = QStringLiteral("Segoe Fluent Icons");
const QString kMdl2Assets = QStringLiteral("Segoe MDL2 Assets");

QPalette BuildFusionPalette()
{
    const std::unique_ptr<QStyle> upFusion(QStyleFactory::create(kFusionStyleName));
    return upFusion->standardPalette();
}

void VerifyDisabledTextBetween(const QPalette& palette)
{
    // Disabled text sits between the background and the normal text.
    const int iWindow = palette.color(QPalette::Window).lightness();
    for (const QPalette::ColorRole eRole : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
    {
        const int iActive = palette.color(QPalette::Active, eRole).lightness();
        const int iDisabled = palette.color(QPalette::Disabled, eRole).lightness();
        QVERIFY(std::abs(iDisabled - iActive) > 60);
        QVERIFY(std::abs(iDisabled - iWindow) > 40);
        QVERIFY((iDisabled - iActive) * (iDisabled - iWindow) < 0);
    }
}

class ApplicationThemeGuard
{
public:
    ApplicationThemeGuard()
        : m_palOriginal(QApplication::palette())
        , m_fontOriginal(QApplication::font())
    {
    }

    ~ApplicationThemeGuard()
    {
        QApplication::setPalette(m_palOriginal);
        QApplication::setFont(m_fontOriginal);
    }

private:
    QPalette m_palOriginal;
    QFont m_fontOriginal;
};
}

class TestWindowsTheme : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void TestPersonalizeKeyPath();
    void TestTaskbarIsLightOnlyWhenSet();
    void TestAppsAreDarkOnlyWhenCleared();
    void TestMissingKeyMeansDefaults();
    void TestDarkPaletteIsDark();
    void TestLightPaletteIsWindowsLight();
    void TestAppPaletteFollowsAppsMode();
    void TestLightPaletteIsBuiltOnce();
    void TestApplyUsesFusionAndReappliesOnlyOnChange();
    void TestAppFontIsSegoeUiWithMalgunGothic();
    void TestSemiboldFontKeepsHangulFallback();
    void TestChoosesIconFont_data();
    void TestChoosesIconFont();
    void TestControlStyleSheetFollowsMode();
    void TestRoundedCornersNeedWindows11();

private:
    std::unique_ptr<QTemporaryDir> m_upDir;
    std::unique_ptr<QSettings> m_upPersonalize;
};

void TestWindowsTheme::init()
{
    m_upDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_upDir->isValid());
    m_upPersonalize = std::make_unique<QSettings>(m_upDir->filePath(QStringLiteral("personalize.ini")), QSettings::IniFormat);
}

void TestWindowsTheme::cleanup()
{
    m_upPersonalize.reset();
    m_upDir.reset();
}

void TestWindowsTheme::TestPersonalizeKeyPath()
{
    QCOMPARE(QString::fromLatin1(WindowsTheme::kPersonalizeKey),
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"));
}

void TestWindowsTheme::TestTaskbarIsLightOnlyWhenSet()
{
    // Before Windows 10 1903 the value does not exist and the taskbar is dark.
    QVERIFY(!WindowsTheme::IsTaskbarLight(m_upPersonalize.get()));
    m_upPersonalize->setValue(kTaskbarValue, 1);
    QVERIFY(WindowsTheme::IsTaskbarLight(m_upPersonalize.get()));
    m_upPersonalize->setValue(kTaskbarValue, 0);
    QVERIFY(!WindowsTheme::IsTaskbarLight(m_upPersonalize.get()));
    // The apps value does not affect the taskbar.
    m_upPersonalize->setValue(kAppsValue, 1);
    QVERIFY(!WindowsTheme::IsTaskbarLight(m_upPersonalize.get()));
}

void TestWindowsTheme::TestAppsAreDarkOnlyWhenCleared()
{
    QVERIFY(!WindowsTheme::IsAppsDark(m_upPersonalize.get()));
    m_upPersonalize->setValue(kAppsValue, 0);
    QVERIFY(WindowsTheme::IsAppsDark(m_upPersonalize.get()));
    m_upPersonalize->setValue(kAppsValue, 1);
    QVERIFY(!WindowsTheme::IsAppsDark(m_upPersonalize.get()));
    m_upPersonalize->setValue(kTaskbarValue, 0);
    QVERIFY(!WindowsTheme::IsAppsDark(m_upPersonalize.get()));
}

void TestWindowsTheme::TestMissingKeyMeansDefaults()
{
    QVERIFY(!WindowsTheme::IsTaskbarLight(nullptr));
    QVERIFY(!WindowsTheme::IsAppsDark(nullptr));
}

void TestWindowsTheme::TestDarkPaletteIsDark()
{
    const QPalette palette = WindowsTheme::BuildDarkPalette();
    // The Windows 11 dark flyout surface.
    QCOMPARE(palette.color(QPalette::Window), QColor(0x2c, 0x2c, 0x2c));
    QVERIFY(palette.color(QPalette::Window).lightness() < 64);
    QVERIFY(palette.color(QPalette::Base).lightness() < 64);
    QVERIFY(palette.color(QPalette::Button).lightness() < 80);
    QVERIFY(palette.color(QPalette::WindowText).lightness() > 200);
    QVERIFY(palette.color(QPalette::Text).lightness() > 200);
    QVERIFY(palette.color(QPalette::ButtonText).lightness() > 200);
    QCOMPARE(palette.color(QPalette::Highlight), QColor(0x39, 0x87, 0xe5));
    QVERIFY(palette.color(QPalette::HighlightedText).lightness() > 200);
    // Fusion outlines check boxes with a darker Window, so Base must differ from that to show the box.
    QVERIFY(palette.color(QPalette::Base).lightness() > palette.color(QPalette::Window).darker(140).lightness() + 10);
    VerifyDisabledTextBetween(palette);
}

void TestWindowsTheme::TestLightPaletteIsWindowsLight()
{
    const QPalette palette = WindowsTheme::BuildLightPalette();
    // The Windows 11 light flyout surface instead of Fusion's beige grey.
    QCOMPARE(palette.color(QPalette::Window), QColor(0xf9, 0xf9, 0xf9));
    QCOMPARE(palette.color(QPalette::WindowText), QColor(0x1a, 0x1a, 0x1a));
    QCOMPARE(palette.color(QPalette::Text), QColor(0x1a, 0x1a, 0x1a));
    QCOMPARE(palette.color(QPalette::ButtonText), QColor(0x1a, 0x1a, 0x1a));
    QCOMPARE(palette.color(QPalette::Base), QColor(0xff, 0xff, 0xff));
    QCOMPARE(palette.color(QPalette::Highlight), QColor(0x2a, 0x78, 0xd6));
    QCOMPARE(palette.color(QPalette::HighlightedText), QColor(0xff, 0xff, 0xff));
    QVERIFY(palette.color(QPalette::Button).lightness() >= palette.color(QPalette::Window).lightness());
    QVERIFY(palette.color(QPalette::Window) != BuildFusionPalette().color(QPalette::Window));
    VerifyDisabledTextBetween(palette);
}

void TestWindowsTheme::TestAppPaletteFollowsAppsMode()
{
    const QPalette palLight = WindowsTheme::BuildLightPalette();
    const QPalette palDark = WindowsTheme::BuildDarkPalette();
    QVERIFY(palLight != palDark);

    // No AppsUseLightTheme value means light apps.
    QCOMPARE(WindowsTheme::BuildAppPalette(m_upPersonalize.get()), palLight);
    m_upPersonalize->setValue(kAppsValue, 0);
    QCOMPARE(WindowsTheme::BuildAppPalette(m_upPersonalize.get()), palDark);
    m_upPersonalize->setValue(kAppsValue, 1);
    QCOMPARE(WindowsTheme::BuildAppPalette(m_upPersonalize.get()), palLight);
}

void TestWindowsTheme::TestLightPaletteIsBuiltOnce()
{
    // The app asks every 5 s; the same palette object each time keeps one cache key.
    const QPalette palFirst = WindowsTheme::BuildAppPalette(m_upPersonalize.get());
    const QPalette palSecond = WindowsTheme::BuildAppPalette(m_upPersonalize.get());
    QCOMPARE(palFirst, WindowsTheme::BuildLightPalette());
    QCOMPARE(palSecond.cacheKey(), palFirst.cacheKey());
}

void TestWindowsTheme::TestApplyUsesFusionAndReappliesOnlyOnChange()
{
    const ApplicationThemeGuard guardTheme;

    m_upPersonalize->setValue(kAppsValue, 0);
    QVERIFY(WindowsTheme::ApplyAppTheme(m_upPersonalize.get()));
    QVERIFY(QApplication::style()->inherits(kFusionStyleClassName));
    QCOMPARE(QApplication::palette(), WindowsTheme::BuildDarkPalette());
    // Unchanged mode: nothing is re-applied (a new palette would re-polish every widget every tick).
    QVERIFY(!WindowsTheme::ApplyAppTheme(m_upPersonalize.get()));

    m_upPersonalize->setValue(kAppsValue, 1);
    QVERIFY(WindowsTheme::ApplyAppTheme(m_upPersonalize.get()));
    QVERIFY(QApplication::style()->inherits(kFusionStyleClassName));
    QCOMPARE(QApplication::palette(), WindowsTheme::BuildLightPalette());
    QVERIFY(!WindowsTheme::ApplyAppTheme(m_upPersonalize.get()));

    m_upPersonalize->remove(kAppsValue);
    QVERIFY(!WindowsTheme::ApplyAppTheme(m_upPersonalize.get()));
}

void TestWindowsTheme::TestAppFontIsSegoeUiWithMalgunGothic()
{
    const QFont font = WindowsTheme::BuildAppFont();
    // Qt 5 would otherwise start from DEFAULT_GUI_FONT, which is Gulim on Korean Windows.
    QCOMPARE(font.family(), QStringLiteral("Segoe UI"));
    QCOMPARE(font.families(), QStringList({ QStringLiteral("Segoe UI"), QStringLiteral("Malgun Gothic") }));
    QCOMPARE(font.pointSize(), 9);
    QCOMPARE(font.weight(), static_cast<int>(QFont::Normal));

    // Applied with the theme, so dialogs get it too, and only once.
    const ApplicationThemeGuard guardTheme;
    QApplication::setFont(QFont(QStringLiteral("Gulim"), 9));
    QVERIFY(WindowsTheme::ApplyAppTheme(m_upPersonalize.get()));
    QCOMPARE(QApplication::font().families(), font.families());
    QCOMPARE(QApplication::font().pointSize(), 9);
    QVERIFY(!WindowsTheme::ApplyAppTheme(m_upPersonalize.get()));
}

void TestWindowsTheme::TestSemiboldFontKeepsHangulFallback()
{
    const QFont font = WindowsTheme::BuildSemiboldFont(14);
    QCOMPARE(font.pixelSize(), 14);
    QCOMPARE(font.weight(), static_cast<int>(QFont::DemiBold));
    QCOMPARE(font.families().value(0), QStringLiteral("Segoe UI Semibold"));
    QVERIFY(font.families().contains(QStringLiteral("Segoe UI")));
    QCOMPARE(font.families().value(font.families().size() - 1), QStringLiteral("Malgun Gothic"));
}

void TestWindowsTheme::TestChoosesIconFont_data()
{
    QTest::addColumn<QStringList>("lstInstalled");
    QTest::addColumn<QString>("strExpected");

    QTest::newRow("Windows 11: Fluent first") << QStringList({ QStringLiteral("Segoe UI"), kMdl2Assets, kFluentIcons }) << kFluentIcons;
    QTest::newRow("Windows 10: MDL2") << QStringList({ QStringLiteral("Segoe UI"), kMdl2Assets }) << kMdl2Assets;
    QTest::newRow("case does not matter") << QStringList({ QStringLiteral("segoe mdl2 assets") }) << kMdl2Assets;
    QTest::newRow("neither: text fallback") << QStringList({ QStringLiteral("Segoe UI"), QStringLiteral("Malgun Gothic") }) << QString();
    QTest::newRow("no fonts at all") << QStringList() << QString();
}

void TestWindowsTheme::TestChoosesIconFont()
{
    QFETCH(QStringList, lstInstalled);
    QFETCH(QString, strExpected);
    QCOMPARE(WindowsTheme::ChooseIconFontFamily(lstInstalled), strExpected);
}

void TestWindowsTheme::TestControlStyleSheetFollowsMode()
{
    const QString strLight = WindowsTheme::BuildControlStyleSheet(false);
    const QString strDark = WindowsTheme::BuildControlStyleSheet(true);
    QVERIFY(strLight != strDark);
    for (const QString& strStyleSheet : { strLight, strDark })
    {
        QVERIFY(strStyleSheet.contains(QStringLiteral("QComboBox")));
        QVERIFY(strStyleSheet.contains(QStringLiteral("QPushButton")));
        QVERIFY(strStyleSheet.contains(QStringLiteral("border-radius: 4px")));
        // A font rule here would replace the app font's families or size for every control.
        QVERIFY(!strStyleSheet.contains(QStringLiteral("font")));
        QVERIFY(!strStyleSheet.contains(QLatin1Char('%')));
    }
}

void TestWindowsTheme::TestRoundedCornersNeedWindows11()
{
#ifdef Q_OS_WIN
    const HWND hWindow = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"", WS_POPUP, 0, 0, 16, 16, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    QVERIFY(hWindow != nullptr);
    // DWM rounds corners from Windows 11 (build 22000); before that the popup draws a square border itself.
    const bool bWindows11 = QOperatingSystemVersion::current().microVersion() >= 22000;
    const bool bRounded = WindowsTheme::ApplyRoundedCorners(reinterpret_cast<WId>(hWindow), QColor(0x80, 0x80, 0x80));
    DestroyWindow(hWindow);
    QCOMPARE(bRounded, bWindows11);
#else
    QSKIP("Windows only");
#endif
}

QTEST_MAIN(TestWindowsTheme)
#include "TestWindowsTheme.moc"
