#include "UI/TrayIcon.h"
#include "UI/TrayIconPainter.h"

#include <QGuiApplication>
#include <QIcon>
#include <QScreen>
#include <QSettings>
#include <QSignalSpy>
#include <QSystemTrayIcon>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <memory>

namespace
{
const QString kTaskbarValue = QStringLiteral("SystemUsesLightTheme");
const QList<int> kExpectedSizes = {16, 20, 24, 32, 40, 48, 64};

TrayIconState MakeState(double fiveHourPercent, double sevenDayPercent)
{
    TrayIconState state;
    state.m_bFiveHourValid = true;
    state.m_dFiveHourPercent = fiveHourPercent;
    state.m_bSevenDayValid = true;
    state.m_dSevenDayPercent = sevenDayPercent;
    return state;
}

QImage ConvertToImage(const QIcon& icon, int pixelSize)
{
    return icon.pixmap(QSize(pixelSize, pixelSize)).toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

QImage RenderPremultiplied(const TrayIconState& state, EAppearance appearance, int pixelSize)
{
    return TrayIconPainter::Render(state, appearance, pixelSize).convertToFormat(QImage::Format_ARGB32_Premultiplied);
}
}

class TestTrayIcon : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void TestBuildIconDrawsEverySizeNatively();
    void TestSetStateUpdatesIconAndToolTip();
    void TestRedrawsOnlyWhenTaskbarAppearanceChanges();
    void TestLeftAndRightClicksEmitClicked();
    void TestResetsIconWhenDisplayScaleChanges();

private:
    QSystemTrayIcon* FindSystemTrayIcon(TrayIcon& trayIcon) const;

private:
    std::unique_ptr<QTemporaryDir> m_upDir;
    std::unique_ptr<QSettings> m_upPersonalize;
};

void TestTrayIcon::init()
{
    m_upDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_upDir->isValid());
    m_upPersonalize = std::make_unique<QSettings>(m_upDir->filePath(QStringLiteral("personalize.ini")), QSettings::IniFormat);
}

void TestTrayIcon::cleanup()
{
    m_upPersonalize.reset();
    m_upDir.reset();
}

QSystemTrayIcon* TestTrayIcon::FindSystemTrayIcon(TrayIcon& trayIcon) const
{
    return trayIcon.findChild<QSystemTrayIcon*>();
}

void TestTrayIcon::TestBuildIconDrawsEverySizeNatively()
{
    const TrayIconState state = MakeState(42.0, 18.0);
    const QIcon icon = TrayIcon::BuildIcon(state, EAppearance::DARK);
    QList<int> lstSizes;
    for (const QSize& size : icon.availableSizes())
    {
        QCOMPARE(size.width(), size.height());
        lstSizes.append(size.width());
    }
    std::sort(lstSizes.begin(), lstSizes.end());
    QCOMPARE(lstSizes, kExpectedSizes);

    // Each size is its own drawing, not a scaled copy.
    for (const int iSize : kExpectedSizes)
    {
        QCOMPARE(ConvertToImage(icon, iSize), RenderPremultiplied(state, EAppearance::DARK, iSize));
    }
}

void TestTrayIcon::TestSetStateUpdatesIconAndToolTip()
{
    TrayIcon trayIcon(m_upPersonalize.get());
    QSystemTrayIcon* pSystemTrayIcon = FindSystemTrayIcon(trayIcon);
    QVERIFY(pSystemTrayIcon != nullptr);
    QCOMPARE(pSystemTrayIcon->toolTip(), QStringLiteral("Claude 사용량\n한도 정보 없음"));
    QVERIFY(!pSystemTrayIcon->icon().isNull());

    const TrayIconState state = MakeState(42.0, 18.0);
    trayIcon.SetState(state);
    QCOMPARE(pSystemTrayIcon->toolTip(), QStringLiteral("Claude 사용량\n5시간 42% · 주간 18%"));
    // No SystemUsesLightTheme value means a dark taskbar.
    QCOMPARE(ConvertToImage(pSystemTrayIcon->icon(), 32), RenderPremultiplied(state, EAppearance::DARK, 32));
}

void TestTrayIcon::TestRedrawsOnlyWhenTaskbarAppearanceChanges()
{
    m_upPersonalize->setValue(kTaskbarValue, 0);
    TrayIcon trayIcon(m_upPersonalize.get());
    QSystemTrayIcon* pSystemTrayIcon = FindSystemTrayIcon(trayIcon);
    QVERIFY(pSystemTrayIcon != nullptr);
    const TrayIconState state = MakeState(42.0, 18.0);
    trayIcon.SetState(state);
    QCOMPARE(ConvertToImage(pSystemTrayIcon->icon(), 16), RenderPremultiplied(state, EAppearance::DARK, 16));

    QVERIFY(!trayIcon.RefreshTaskbarAppearance());

    m_upPersonalize->setValue(kTaskbarValue, 1);
    QVERIFY(trayIcon.RefreshTaskbarAppearance());
    QCOMPARE(ConvertToImage(pSystemTrayIcon->icon(), 16), RenderPremultiplied(state, EAppearance::LIGHT, 16));
    QCOMPARE(ConvertToImage(pSystemTrayIcon->icon(), 32), RenderPremultiplied(state, EAppearance::LIGHT, 32));
    QVERIFY(!trayIcon.RefreshTaskbarAppearance());

    // A new state keeps the light ink.
    const TrayIconState stateNext = MakeState(75.0, 18.0);
    trayIcon.SetState(stateNext);
    QCOMPARE(ConvertToImage(pSystemTrayIcon->icon(), 24), RenderPremultiplied(stateNext, EAppearance::LIGHT, 24));

    m_upPersonalize->remove(kTaskbarValue);
    QVERIFY(trayIcon.RefreshTaskbarAppearance());
    QCOMPARE(ConvertToImage(pSystemTrayIcon->icon(), 24), RenderPremultiplied(stateNext, EAppearance::DARK, 24));
}

void TestTrayIcon::TestLeftAndRightClicksEmitClicked()
{
    TrayIcon trayIcon(m_upPersonalize.get());
    QSystemTrayIcon* pSystemTrayIcon = FindSystemTrayIcon(trayIcon);
    QVERIFY(pSystemTrayIcon != nullptr);
    QVERIFY(pSystemTrayIcon->contextMenu() == nullptr);
    QSignalSpy spyClicked(&trayIcon, &TrayIcon::Clicked);

    emit pSystemTrayIcon->activated(QSystemTrayIcon::Trigger);
    QCOMPARE(spyClicked.count(), 1);
    emit pSystemTrayIcon->activated(QSystemTrayIcon::Context);
    QCOMPARE(spyClicked.count(), 2);
    // A double click already sent Trigger for its first click.
    emit pSystemTrayIcon->activated(QSystemTrayIcon::DoubleClick);
    emit pSystemTrayIcon->activated(QSystemTrayIcon::MiddleClick);
    QCOMPARE(spyClicked.count(), 2);
}

void TestTrayIcon::TestResetsIconWhenDisplayScaleChanges()
{
    TrayIcon trayIcon(m_upPersonalize.get());
    QSystemTrayIcon* pSystemTrayIcon = FindSystemTrayIcon(trayIcon);
    QVERIFY(pSystemTrayIcon != nullptr);
    const TrayIconState state = MakeState(42.0, 18.0);
    trayIcon.SetState(state);
    QScreen* pScreen = QGuiApplication::primaryScreen();
    QVERIFY(pScreen != nullptr);

    // Qt picks the pixmap size when the icon is set, so a new scale needs the icon set again.
    qint64 llKey = pSystemTrayIcon->icon().cacheKey();
    emit pScreen->logicalDotsPerInchChanged(144.0);
    QVERIFY(pSystemTrayIcon->icon().cacheKey() != llKey);
    QCOMPARE(ConvertToImage(pSystemTrayIcon->icon(), 24), RenderPremultiplied(state, EAppearance::DARK, 24));

    llKey = pSystemTrayIcon->icon().cacheKey();
    emit qGuiApp->primaryScreenChanged(pScreen);
    QVERIFY(pSystemTrayIcon->icon().cacheKey() != llKey);

    llKey = pSystemTrayIcon->icon().cacheKey();
    emit qGuiApp->screenAdded(pScreen);
    QVERIFY(pSystemTrayIcon->icon().cacheKey() != llKey);

    // Watching an added screen keeps the existing watch working.
    llKey = pSystemTrayIcon->icon().cacheKey();
    emit pScreen->logicalDotsPerInchChanged(96.0);
    QVERIFY(pSystemTrayIcon->icon().cacheKey() != llKey);
    QCOMPARE(ConvertToImage(pSystemTrayIcon->icon(), 24), RenderPremultiplied(state, EAppearance::DARK, 24));
}

QTEST_MAIN(TestTrayIcon)
#include "TestTrayIcon.moc"
