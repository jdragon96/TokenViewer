#include "UI/UsagePopup.h"

#include "Analysis/PriceTable.h"
#include "Core/Observers.h"
#include "Core/Settings.h"
#include "Core/SystemStatus.h"
#include "TestHelpers.h"
#include "UI/BreakdownListWidget.h"
#include "UI/LimitMeterWidget.h"

#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

namespace
{
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
}

class TestUsagePopup : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void TestShowsMetersWhenLimitsArrive();
    void TestShowsLoginNoticeWhenNotLoggedIn();
    void TestListsBreakdownRowsAndEmptyState();
    void TestSegmentClickChangesDimension();
    void TestHeightFollowsRowCount();
    void TestIgnoresRightClick();

private:
    std::unique_ptr<QTemporaryDir> m_upDir;
    std::unique_ptr<QSettings> m_upStoredSettings;
    std::unique_ptr<Settings> m_upSettings;
    std::unique_ptr<SystemStatus> m_upStatus;
    std::unique_ptr<Observers> m_upObservers;
    PriceTable m_priceTable;
};

void TestUsagePopup::init()
{
    m_upDir = std::make_unique<QTemporaryDir>();
    m_upStoredSettings = std::make_unique<QSettings>(m_upDir->filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    m_upSettings = std::make_unique<Settings>(m_upStoredSettings.get());
    m_upStatus = std::make_unique<SystemStatus>();
    m_upObservers = std::make_unique<Observers>();
    QVERIFY(m_priceTable.LoadFromJson(ReadSourceFile(QStringLiteral("shared/prices.json"))));
}

void TestUsagePopup::cleanup()
{
    m_upObservers.reset();
    m_upStatus.reset();
    m_upSettings.reset();
    m_upStoredSettings.reset();
    m_upDir.reset();
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

void TestUsagePopup::TestHeightFollowsRowCount()
{
    UsagePopup popup(*m_upObservers, m_upStatus.get(), m_upSettings.get(), &m_priceTable);
    popup.show();
    QVERIFY(QTest::qWaitForWindowExposed(&popup));

    LogSnapshot snapshot;
    for (int i = 0; i < 5; ++i)
    {
        snapshot.m_vecRecords.append(MakeRecentRecord(QStringLiteral("k%1").arg(i), QStringLiteral("/Users/me/P%1").arg(i)));
    }
    m_upStatus->ApplyLogSnapshot(snapshot);
    popup.Refresh();
    const int iTallHeight = popup.height();

    LogSnapshot single;
    single.m_vecRecords = { MakeRecentRecord(QStringLiteral("k0"), QStringLiteral("/Users/me/P0")) };
    m_upStatus->ApplyLogSnapshot(single);
    popup.Refresh();
    QVERIFY(popup.height() < iTallHeight);
    const int iShortHeight = popup.height();

    m_upStatus->ApplyLogSnapshot(snapshot);
    popup.Refresh();
    QCOMPARE(popup.height(), iTallHeight);
    QVERIFY(popup.height() > iShortHeight);
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

QTEST_MAIN(TestUsagePopup)
#include "TestUsagePopup.moc"
