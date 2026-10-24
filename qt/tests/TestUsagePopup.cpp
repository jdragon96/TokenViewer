#include "UI/UsagePopup.h"

#include "Analysis/PriceTable.h"
#include "Core/AppPaths.h"
#include "Core/Observers.h"
#include "Core/Settings.h"
#include "Core/SystemStatus.h"
#include "TestHelpers.h"
#include "UI/BreakdownListWidget.h"
#include "UI/LimitMeterWidget.h"
#include "UI/TrayIconPainter.h"
#include "UI/WindowsTheme.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
#include <QWindow>
#include <QtTest>

#include <algorithm>
#include <memory>

namespace
{
constexpr ushort kZeroWidthSpace = 0x200B;
constexpr ushort kRefreshGlyph = 0xE72C;
constexpr ushort kSettingsGlyph = 0xE713;
const QSize kPopupSize(300, 400);
const QColor kBlueLight(0x2a, 0x78, 0xd6);
const QColor kBlueDark(0x39, 0x87, 0xe5);

TokenRecord MakeRecentRecord(const QString& key, const QString& projectPath)
{
    TokenRecord record;
    record.m_strKey = key;
    record.m_dtTimestamp = QDateTime::currentDateTimeUtc().addSecs(-3600);
    record.m_strSessionId = QStringLiteral("s1");
    record.m_strProjectPath = projectPath;
    record.m_strModel = QStringLiteral("claude-opus-5-5");
    record.m_llOutput = 100000;
    return record;
}

LogSnapshot MakeProjectsSnapshot(int projectCount)
{
    LogSnapshot snapshot;
    for (int iIndex = 0; iIndex < projectCount; ++iIndex)
    {
        snapshot.m_vecRecords.append(MakeRecentRecord(QStringLiteral("k%1").arg(iIndex), QStringLiteral("/Users/me/P%1").arg(iIndex)));
    }
    return snapshot;
}

FetchResult MakeOkResult()
{
    FetchResult result;
    result.m_eStatus = EFetchStatus::OK;
    result.m_limits.m_fiveHour.m_bValid = true;
    result.m_limits.m_fiveHour.m_dPercent = 42.0;
    result.m_limits.m_sevenDay.m_bValid = true;
    result.m_limits.m_sevenDay.m_dPercent = 18.0;
    return result;
}

class ApplicationNamesGuard
{
public:
    ApplicationNamesGuard()
        : m_strApplicationName(QCoreApplication::applicationName())
        , m_strOrganizationName(QCoreApplication::organizationName())
    {
    }

    ~ApplicationNamesGuard()
    {
        QCoreApplication::setApplicationName(m_strApplicationName);
        QCoreApplication::setOrganizationName(m_strOrganizationName);
    }

private:
    QString m_strApplicationName;
    QString m_strOrganizationName;
};

class ApplicationPaletteGuard
{
public:
    ApplicationPaletteGuard()
        : m_palOriginal(QApplication::palette())
    {
    }

    ~ApplicationPaletteGuard()
    {
        QApplication::setPalette(m_palOriginal);
    }

private:
    QPalette m_palOriginal;
};

class ApplicationFontGuard
{
public:
    ApplicationFontGuard()
        : m_fontOriginal(QApplication::font())
    {
    }

    ~ApplicationFontGuard()
    {
        QApplication::setFont(m_fontOriginal);
    }

private:
    QFont m_fontOriginal;
};
}

class TestUsagePopup : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();
    void TestShowsMetersWhenLimitsArrive();
    void TestShowsLoginNoticeWhenNotLoggedIn();
    void TestBadResponseNoticeShowsLogDirectory();
    void TestListsBreakdownRowsAndEmptyState();
    void TestSegmentClickChangesDimension();
    void TestHeightIsFixedWhateverTheGroupCount();
    void TestIgnoresRightClick();
    void TestCalculatePosition_data();
    void TestCalculatePosition();
    void TestUnknownAnchorUsesFallbackPoint();
    void TestKeepsBottomEdgeWhenShownAbove();
    void TestRecolorsWhenPaletteChanges();
    void TestWindowIsOpaqueWithOwnBorder();
    void TestFooterIconButtonsKeepTheirNames();
    void TestTextIsAtLeastTwelvePixels();
    void TestStyleFollowsAppearance();

private:
    std::unique_ptr<QTemporaryDir> m_upDir;
    std::unique_ptr<QSettings> m_upStoredSettings;
    std::unique_ptr<Settings> m_upSettings;
    std::unique_ptr<SystemStatus> m_upStatus;
    std::unique_ptr<Observers> m_upObservers;
    PriceTable m_priceTable;
    QString m_strOriginalApplicationName;
    QString m_strOriginalOrganizationName;
};

void TestUsagePopup::initTestCase()
{
    m_strOriginalApplicationName = QCoreApplication::applicationName();
    m_strOriginalOrganizationName = QCoreApplication::organizationName();
}

void TestUsagePopup::init()
{
    m_upDir = std::make_unique<QTemporaryDir>();
    m_upStoredSettings = std::make_unique<QSettings>(m_upDir->filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    m_upSettings = std::make_unique<Settings>(m_upStoredSettings.get());
    m_upStatus = std::make_unique<SystemStatus>();
    m_upObservers = std::make_unique<Observers>();
    QVERIFY(m_priceTable.LoadFromJson(ReadSourceFile(QStringLiteral("../shared/prices.json"))));
}

void TestUsagePopup::cleanup()
{
    m_upObservers.reset();
    m_upStatus.reset();
    m_upSettings.reset();
    m_upStoredSettings.reset();
    m_upDir.reset();
    // A leaked "TokenViewer" name would send a later default QSettings() to the real registry key.
    QCOMPARE(QCoreApplication::applicationName(), m_strOriginalApplicationName);
    QCOMPARE(QCoreApplication::organizationName(), m_strOriginalOrganizationName);
}

void TestUsagePopup::TestShowsMetersWhenLimitsArrive()
{
    const QDateTime dtNow = QDateTime::currentDateTimeUtc();
    FetchResult result;
    result.m_eStatus = EFetchStatus::OK;
    result.m_limits.m_fiveHour.m_bValid = true;
    result.m_limits.m_fiveHour.m_dPercent = 42.0;
    result.m_limits.m_fiveHour.m_dtResetsAt = dtNow.addSecs(2 * 3600 + 13 * 60 + 30);
    result.m_limits.m_sevenDay.m_bValid = true;
    result.m_limits.m_sevenDay.m_dPercent = 18.0;
    result.m_strPlanLabel = QStringLiteral("Max 5x");
    m_upStatus->ApplyFetchResult(result, dtNow);

    UsagePopup popup(*m_upObservers, m_upStatus.get(), m_upSettings.get(), &m_priceTable);
    popup.Refresh();

    LimitMeterWidget* pMeter = popup.findChild<LimitMeterWidget*>(QStringLiteral("fiveHourMeter"));
    QVERIFY(pMeter != nullptr);
    QCOMPARE(pMeter->GetValueText(), QStringLiteral("42%"));
    QCOMPARE(pMeter->GetSubText(), QStringLiteral("2:13 후 리셋"));
    QVERIFY(pMeter->isVisibleTo(&popup));
    QVERIFY(popup.findChild<QLabel*>(QStringLiteral("noticeLabel"))->isHidden());
    QCOMPARE(popup.findChild<QLabel*>(QStringLiteral("planBadge"))->text(), QStringLiteral("Max 5x"));
}

void TestUsagePopup::TestShowsLoginNoticeWhenNotLoggedIn()
{
    FetchResult result;
    result.m_eStatus = EFetchStatus::NOT_LOGGED_IN;
    m_upStatus->ApplyFetchResult(result, QDateTime::currentDateTimeUtc());

    UsagePopup popup(*m_upObservers, m_upStatus.get(), m_upSettings.get(), &m_priceTable);
    popup.Refresh();

    QLabel* pNotice = popup.findChild<QLabel*>(QStringLiteral("noticeLabel"));
    QVERIFY(!pNotice->isHidden());
    QVERIFY(pNotice->text().contains(QStringLiteral("/login")));
    QVERIFY(!popup.findChild<QPushButton*>(QStringLiteral("retryButton"))->isHidden());
    QVERIFY(!popup.findChild<LimitMeterWidget*>(QStringLiteral("fiveHourMeter"))->isVisibleTo(&popup));
}

void TestUsagePopup::TestBadResponseNoticeShowsLogDirectory()
{
    FetchResult result;
    result.m_eStatus = EFetchStatus::BAD_RESPONSE;
    m_upStatus->ApplyFetchResult(result, QDateTime::currentDateTimeUtc());

    // The real names give the real (long) path length; the guard puts the test names back even on failure.
    const ApplicationNamesGuard guardNames;
    QCoreApplication::setApplicationName(QStringLiteral("TokenViewer"));
    QCoreApplication::setOrganizationName(QStringLiteral("TokenViewer"));
    UsagePopup popup(*m_upObservers, m_upStatus.get(), m_upSettings.get(), &m_priceTable);
    popup.Refresh();
    popup.show();
    QVERIFY(QTest::qWaitForWindowExposed(&popup));

    QLabel* pNotice = popup.findChild<QLabel*>(QStringLiteral("noticeLabel"));
    const QString strNotice = pNotice->text().remove(QChar(kZeroWidthSpace));
    const QString strNativeDirectory = QDir::toNativeSeparators(AppPaths::GetLogDirectory());
    QVERIFY2(strNotice.contains(strNativeDirectory), qPrintable(strNotice));
    QVERIFY(strNativeDirectory.contains(QLatin1Char('\\')));
    QVERIFY(!strNotice.contains(QStringLiteral("Library")));
    // The path must wrap inside the fixed-width popup instead of being clipped.
    QVERIFY2(pNotice->minimumSizeHint().width() <= pNotice->width(),
        qPrintable(QStringLiteral("%1 > %2").arg(pNotice->minimumSizeHint().width()).arg(pNotice->width())));
}

void TestUsagePopup::TestListsBreakdownRowsAndEmptyState()
{
    UsagePopup popup(*m_upObservers, m_upStatus.get(), m_upSettings.get(), &m_priceTable);
    popup.Refresh();
    BreakdownListWidget* pList = popup.findChild<BreakdownListWidget*>(QStringLiteral("breakdownList"));
    QCOMPARE(pList->GetRowCount(), 0);

    LogSnapshot snapshot;
    snapshot.m_vecRecords = { MakeRecentRecord(QStringLiteral("k1"), QStringLiteral("/Users/me/A")),
        MakeRecentRecord(QStringLiteral("k2"), QStringLiteral("/Users/me/B")) };
    m_upStatus->ApplyLogSnapshot(snapshot);
    popup.Refresh();
    QCOMPARE(pList->GetRowCount(), 2);
}

void TestUsagePopup::TestSegmentClickChangesDimension()
{
    UsagePopup popup(*m_upObservers, m_upStatus.get(), m_upSettings.get(), &m_priceTable);
    QPushButton* pModelButton = popup.findChild<QPushButton*>(QStringLiteral("modelButton"));
    QTest::mouseClick(pModelButton, Qt::LeftButton);
    QCOMPARE(m_upSettings->GetDimension(), EBreakdownDimension::MODEL);
    QVERIFY(pModelButton->isChecked());
}

void TestUsagePopup::TestHeightIsFixedWhateverTheGroupCount()
{
    UsagePopup popup(*m_upObservers, m_upStatus.get(), m_upSettings.get(), &m_priceTable);
    popup.Refresh();
    popup.show();
    QVERIFY(QTest::qWaitForWindowExposed(&popup));
    BreakdownListWidget* pList = popup.findChild<BreakdownListWidget*>(QStringLiteral("breakdownList"));

    // No records: the empty message sits in the same four-slot area.
    QCOMPARE(pList->GetRowCount(), 0);
    const int iHeight = popup.height();

    for (const int iProjectCount : { 1, 4, 7, 0 })
    {
        m_upStatus->ApplyLogSnapshot(MakeProjectsSnapshot(iProjectCount));
        popup.Refresh();
        QCOMPARE(pList->GetRowCount(), std::min(iProjectCount, 4));
        QCOMPARE(popup.height(), iHeight);
    }

    // Seven projects but only two models: switching tabs keeps the height.
    LogSnapshot snapshot = MakeProjectsSnapshot(7);
    for (int iIndex = 0; iIndex < snapshot.m_vecRecords.size(); iIndex += 2)
    {
        snapshot.m_vecRecords[iIndex].m_strModel = QStringLiteral("claude-sonnet-5-5");
    }
    m_upStatus->ApplyLogSnapshot(snapshot);
    popup.Refresh();
    QCOMPARE(pList->GetRowCount(), 4);
    QCOMPARE(popup.height(), iHeight);
    QTest::mouseClick(popup.findChild<QPushButton*>(QStringLiteral("modelButton")), Qt::LeftButton);
    QCOMPARE(pList->GetRowCount(), 2);
    QCOMPARE(popup.height(), iHeight);
    QTest::mouseClick(popup.findChild<QPushButton*>(QStringLiteral("projectButton")), Qt::LeftButton);
    QCOMPARE(pList->GetRowCount(), 4);
    QCOMPARE(popup.height(), iHeight);
}

void TestUsagePopup::TestIgnoresRightClick()
{
    UsagePopup popup(*m_upObservers, m_upStatus.get(), m_upSettings.get(), &m_priceTable);
    popup.show();
    QVERIFY(QTest::qWaitForWindowExposed(&popup));
    QPushButton* pSessionButton = popup.findChild<QPushButton*>(QStringLiteral("sessionButton"));
    QTest::mouseClick(pSessionButton, Qt::RightButton);
    QVERIFY(m_upSettings->GetDimension() != EBreakdownDimension::SESSION);
}

void TestUsagePopup::TestCalculatePosition_data()
{
    QTest::addColumn<QRect>("rcAnchor");
    QTest::addColumn<QSize>("sizePopup");
    QTest::addColumn<QRect>("rcAvailable");
    QTest::addColumn<QPoint>("ptExpected");

    // 1920x1080 with the taskbar (48 px) at the bottom; tray icons sit inside the taskbar, outside the available area.
    const QRect rcBottomTaskbar(0, 0, 1920, 1032);
    QTest::newRow("bottom taskbar: above, kept inside the area")
        << QRect(1000, 1044, 24, 24) << kPopupSize << rcBottomTaskbar << QPoint(862, 624);
    QTest::newRow("lower half: bottom edge 4 px above the icon")
        << QRect(1000, 900, 24, 24) << kPopupSize << rcBottomTaskbar << QPoint(862, 496);
    QTest::newRow("icon straddling the area's bottom edge")
        << QRect(1000, 1020, 24, 24) << kPopupSize << rcBottomTaskbar << QPoint(862, 616);
    QTest::newRow("near the right screen edge")
        << QRect(1900, 1044, 24, 24) << kPopupSize << rcBottomTaskbar << QPoint(1612, 624);
    QTest::newRow("near the left screen edge")
        << QRect(0, 1044, 24, 24) << kPopupSize << rcBottomTaskbar << QPoint(8, 624);

    const QRect rcTopTaskbar(0, 48, 1920, 1032);
    QTest::newRow("top taskbar: below, kept inside the area")
        << QRect(1000, 12, 24, 24) << kPopupSize << rcTopTaskbar << QPoint(862, 56);
    QTest::newRow("upper half: top edge 4 px below the icon")
        << QRect(1000, 100, 24, 24) << kPopupSize << rcTopTaskbar << QPoint(862, 128);

    QTest::newRow("right vertical taskbar")
        << QRect(1884, 1040, 24, 24) << kPopupSize << QRect(0, 0, 1872, 1080) << QPoint(1564, 636);
    QTest::newRow("left vertical taskbar")
        << QRect(12, 1040, 24, 24) << kPopupSize << QRect(48, 0, 1872, 1080) << QPoint(56, 636);

    QTest::newRow("second screen to the right")
        << QRect(4400, 1404, 24, 24) << kPopupSize << QRect(1920, 0, 2560, 1392) << QPoint(4172, 984);
    QTest::newRow("screen left of the primary")
        << QRect(-500, 1044, 24, 24) << kPopupSize << QRect(-1920, 0, 1920, 1032) << QPoint(-638, 624);
    QTest::newRow("taller than the area: top margin wins")
        << QRect(400, 320, 24, 24) << kPopupSize << QRect(0, 0, 800, 300) << QPoint(262, 8);
}

void TestUsagePopup::TestCalculatePosition()
{
    QFETCH(QRect, rcAnchor);
    QFETCH(QSize, sizePopup);
    QFETCH(QRect, rcAvailable);
    QFETCH(QPoint, ptExpected);
    QCOMPARE(UsagePopup::CalculatePosition(rcAnchor, sizePopup, rcAvailable), ptExpected);
}

void TestUsagePopup::TestUnknownAnchorUsesFallbackPoint()
{
    const QRect rcIcon(1000, 1044, 24, 24);
    QCOMPARE(UsagePopup::ResolveAnchor(rcIcon, QPoint(5, 5)), rcIcon);

    // A re-launch with no icon geometry uses the corner where the notification area usually is.
    const QRect rcAvailable(0, 0, 1920, 1032);
    const QRect rcCorner = UsagePopup::ResolveAnchor(QRect(), rcAvailable.bottomRight());
    QVERIFY(!rcCorner.isEmpty());
    QCOMPARE(UsagePopup::CalculatePosition(rcCorner, kPopupSize, rcAvailable), QPoint(1612, 624));

    // A click with no icon geometry uses the cursor.
    const QRect rcCursor = UsagePopup::ResolveAnchor(QRect(), QPoint(1000, 100));
    QCOMPARE(UsagePopup::CalculatePosition(rcCursor, kPopupSize, rcAvailable), QPoint(851, 105));
}

void TestUsagePopup::TestKeepsBottomEdgeWhenShownAbove()
{
    // The offscreen screen is 800x600, so this icon is in its lower half.
    const QRect rcAnchor(700, 570, 24, 24);
    const int iExpectedBottom = rcAnchor.top() - 4;

    // The breakdown list keeps its height, so a refresh failure notice is what makes the popup taller here.
    m_upStatus->ApplyFetchResult(MakeOkResult(), QDateTime::currentDateTimeUtc());
    UsagePopup popup(*m_upObservers, m_upStatus.get(), m_upSettings.get(), &m_priceTable);
    popup.Refresh();
    popup.ShowNear(rcAnchor);
    QVERIFY(QTest::qWaitForWindowExposed(&popup));
    QCOMPARE(popup.y() + popup.height(), iExpectedBottom);
    QCOMPARE(popup.x(), 800 - 8 - popup.width());
    const int iShortHeight = popup.height();

    FetchResult resultFailed;
    resultFailed.m_eStatus = EFetchStatus::NETWORK_ERROR;
    resultFailed.m_strDetail = QStringLiteral("network unreachable");
    m_upStatus->ApplyFetchResult(resultFailed, QDateTime::currentDateTimeUtc());
    popup.Refresh();
    QVERIFY(popup.height() > iShortHeight);
    QCOMPARE(popup.y() + popup.height(), iExpectedBottom);
    QTRY_COMPARE(popup.windowHandle()->geometry().bottom() + 1, iExpectedBottom);

    m_upStatus->ApplyFetchResult(MakeOkResult(), QDateTime::currentDateTimeUtc());
    popup.Refresh();
    QCOMPARE(popup.height(), iShortHeight);
    QCOMPARE(popup.y() + popup.height(), iExpectedBottom);
    QTRY_COMPARE(popup.windowHandle()->geometry().bottom() + 1, iExpectedBottom);
}

void TestUsagePopup::TestRecolorsWhenPaletteChanges()
{
    const QDateTime dtNow = QDateTime::currentDateTimeUtc();
    FetchResult resultOk;
    resultOk.m_eStatus = EFetchStatus::OK;
    resultOk.m_limits.m_fiveHour.m_bValid = true;
    resultOk.m_limits.m_fiveHour.m_dPercent = 42.0;
    resultOk.m_limits.m_sevenDay.m_bValid = true;
    resultOk.m_limits.m_sevenDay.m_dPercent = 18.0;
    m_upStatus->ApplyFetchResult(resultOk, dtNow);
    FetchResult resultFailed;
    resultFailed.m_eStatus = EFetchStatus::NETWORK_ERROR;
    m_upStatus->ApplyFetchResult(resultFailed, dtNow);

    const ApplicationPaletteGuard guardPalette;
    QVERIFY(QApplication::palette().color(QPalette::Window).lightness() >= 128);
    UsagePopup popup(*m_upObservers, m_upStatus.get(), m_upSettings.get(), &m_priceTable);
    popup.Refresh();
    popup.show();
    QVERIFY(QTest::qWaitForWindowExposed(&popup));
    LimitMeterWidget* pMeter = popup.findChild<LimitMeterWidget*>(QStringLiteral("fiveHourMeter"));
    QLabel* pFooter = popup.findChild<QLabel*>(QStringLiteral("footerLabel"));
    QCOMPARE(pMeter->GetFillColor(), kBlueLight);
    QCOMPARE(pFooter->palette().color(QPalette::WindowText), TrayIconPainter::GetLevelColor(EBarLevel::CRITICAL, EAppearance::LIGHT));

    // The app switches to dark mode while the popup is open.
    QApplication::setPalette(WindowsTheme::BuildDarkPalette());
    QCOMPARE(pMeter->GetFillColor(), kBlueDark);
    QCOMPARE(pFooter->palette().color(QPalette::WindowText), TrayIconPainter::GetLevelColor(EBarLevel::CRITICAL, EAppearance::DARK));
}

void TestUsagePopup::TestWindowIsOpaqueWithOwnBorder()
{
    const ApplicationPaletteGuard guardPalette;
    QApplication::setPalette(WindowsTheme::BuildLightPalette());
    UsagePopup popup(*m_upObservers, m_upStatus.get(), m_upSettings.get(), &m_priceTable);
    // A translucent window is layered, and Windows draws layered windows' text without ClearType.
    QVERIFY(!popup.testAttribute(Qt::WA_TranslucentBackground));
    popup.Refresh();
    popup.show();
    QVERIFY(QTest::qWaitForWindowExposed(&popup));

    // Offscreen has no DWM, the same as Windows 10: a square popup with a 1 px border of its own.
    QVERIFY(QGuiApplication::platformName() != QLatin1String("windows"));
    QVERIFY(!popup.HasSystemCorners());
    const QImage imgPopup = popup.grab().toImage();
    QCOMPARE(imgPopup.pixelColor(0, 0), WindowsTheme::GetFrameBorderColor(false));
    QCOMPARE(imgPopup.pixelColor(imgPopup.width() - 1, imgPopup.height() - 1), WindowsTheme::GetFrameBorderColor(false));
    // Inside the margin the background is painted solid.
    QCOMPARE(imgPopup.pixelColor(4, imgPopup.height() / 2), QColor(0xf9, 0xf9, 0xf9));
}

void TestUsagePopup::TestFooterIconButtonsKeepTheirNames()
{
    UsagePopup popup(*m_upObservers, m_upStatus.get(), m_upSettings.get(), &m_priceTable);
    QToolButton* pRefresh = popup.findChild<QToolButton*>(QStringLiteral("refreshButton"));
    QToolButton* pSettings = popup.findChild<QToolButton*>(QStringLiteral("settingsButton"));
    QVERIFY(pRefresh != nullptr);
    QVERIFY(pSettings != nullptr);
    // The glyphs are private-use characters, so tooltips and screen readers get the words.
    QCOMPARE(pRefresh->toolTip(), QStringLiteral("새로고침"));
    QCOMPARE(pRefresh->accessibleName(), QStringLiteral("새로고침"));
    QCOMPARE(pSettings->toolTip(), QStringLiteral("설정"));
    QCOMPARE(pSettings->accessibleName(), QStringLiteral("설정"));

    const QString strIconFamily = WindowsTheme::ChooseIconFontFamily(QFontDatabase().families());
    if (strIconFamily.isEmpty())
    {
        QCOMPARE(pRefresh->text(), QStringLiteral("↻"));
        QCOMPARE(pSettings->text(), QStringLiteral("⚙"));
    }
    else
    {
        QCOMPARE(pRefresh->text(), QString(QChar(kRefreshGlyph)));
        QCOMPARE(pSettings->text(), QString(QChar(kSettingsGlyph)));
        QCOMPARE(pRefresh->font().family(), strIconFamily);
        QCOMPARE(pSettings->font().family(), strIconFamily);
    }
}

void TestUsagePopup::TestTextIsAtLeastTwelvePixels()
{
    const ApplicationFontGuard guardFont;
    QApplication::setFont(WindowsTheme::BuildAppFont());
    FetchResult result = MakeOkResult();
    result.m_strPlanLabel = QStringLiteral("Max 20x");
    m_upStatus->ApplyFetchResult(result, QDateTime::currentDateTimeUtc());
    m_upStatus->ApplyLogSnapshot(MakeProjectsSnapshot(2));
    UsagePopup popup(*m_upObservers, m_upStatus.get(), m_upSettings.get(), &m_priceTable);
    popup.Refresh();
    popup.show();
    QVERIFY(QTest::qWaitForWindowExposed(&popup));

    int iChecked = 0;
    for (QWidget* pWidget : popup.findChildren<QWidget*>())
    {
        if (!qobject_cast<QLabel*>(pWidget) && !qobject_cast<QAbstractButton*>(pWidget) && !qobject_cast<QComboBox*>(pWidget))
        {
            continue;
        }
        const QFont font = pWidget->font();
        const double dPixels = font.pixelSize() > 0 ? font.pixelSize() : font.pointSizeF() * pWidget->logicalDpiY() / 72.0;
        // Hangul below 12 px is hard to read on a 100% Windows screen.
        QVERIFY2(dPixels >= 12.0, qPrintable(QStringLiteral("%1 %2: %3 px").arg(pWidget->metaObject()->className(), pWidget->objectName()).arg(dPixels)));
        ++iChecked;
    }
    QVERIFY(iChecked >= 10);
}

void TestUsagePopup::TestStyleFollowsAppearance()
{
    const ApplicationPaletteGuard guardPalette;
    QApplication::setPalette(WindowsTheme::BuildLightPalette());
    m_upStatus->ApplyFetchResult(MakeOkResult(), QDateTime::currentDateTimeUtc());
    UsagePopup popup(*m_upObservers, m_upStatus.get(), m_upSettings.get(), &m_priceTable);
    popup.Refresh();
    popup.show();
    QVERIFY(QTest::qWaitForWindowExposed(&popup));
    const QString strLightStyleSheet = popup.styleSheet();
    QVERIFY(strLightStyleSheet.contains(WindowsTheme::BuildControlStyleSheet(false)));
    QLabel* pFooter = popup.findChild<QLabel*>(QStringLiteral("footerLabel"));
    QCOMPARE(pFooter->palette().color(QPalette::WindowText), WindowsTheme::GetSecondaryTextColor(false));

    QApplication::setPalette(WindowsTheme::BuildDarkPalette());
    QVERIFY(popup.styleSheet() != strLightStyleSheet);
    QVERIFY(popup.styleSheet().contains(WindowsTheme::BuildControlStyleSheet(true)));
    QCOMPARE(pFooter->palette().color(QPalette::WindowText), WindowsTheme::GetSecondaryTextColor(true));

    // Switching back gives the light style sheet again.
    QApplication::setPalette(WindowsTheme::BuildLightPalette());
    QCOMPARE(popup.styleSheet(), strLightStyleSheet);
}

QTEST_MAIN(TestUsagePopup)
#include "TestUsagePopup.moc"
