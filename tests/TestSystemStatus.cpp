#include "Core/SystemStatus.h"

#include <QtTest>

namespace
{
const QDateTime kNow(QDate(2026, 10, 5), QTime(12, 0), Qt::UTC);

FetchResult MakeResult(EFetchStatus status)
{
    FetchResult result;
    result.m_eStatus = status;
    result.m_strDetail = QStringLiteral("detail");
    if (status == EFetchStatus::OK)
    {
        result.m_limits.m_fiveHour.m_bValid = true;
        result.m_limits.m_fiveHour.m_dPercent = 42.0;
        result.m_limits.m_sevenDay.m_bValid = true;
        result.m_limits.m_sevenDay.m_dPercent = 18.0;
        result.m_strPlanLabel = QStringLiteral("Max 5x");
    }
    return result;
}
}

class TestSystemStatus : public QObject
{
    Q_OBJECT

private slots:
    void TestEmptyBeforeFirstFetch();
    void TestOkStoresLimitsAndPlan();
    void TestFailureKeepsValuesThenTurnsStale();
    void TestStaleAfterSleepEvenIfLastStatusOk();
    void TestKeychainDeniedClearsLimits();
    void TestRefreshDueOnOpenAfter30Seconds();
    void TestNoOpenRefreshAfterKeychainDenied();
};

void TestSystemStatus::TestEmptyBeforeFirstFetch()
{
    SystemStatus status;
    QCOMPARE(status.GetLimitDisplayState(kNow), ELimitDisplayState::EMPTY);
    QVERIFY(!status.GetLastAttemptAt().isValid());
    QVERIFY(status.IsRefreshDueOnOpen(kNow));
}

void TestSystemStatus::TestOkStoresLimitsAndPlan()
{
    SystemStatus status;
    status.ApplyFetchResult(MakeResult(EFetchStatus::OK), kNow);
    QCOMPARE(status.GetLimitDisplayState(kNow), ELimitDisplayState::NORMAL);
    QCOMPARE(status.GetLimits().m_fiveHour.m_dPercent, 42.0);
    QCOMPARE(status.GetPlanLabel(), QStringLiteral("Max 5x"));
    QCOMPARE(status.GetLastSuccessAt(), kNow);
}

void TestSystemStatus::TestFailureKeepsValuesThenTurnsStale()
{
    SystemStatus status;
    status.ApplyFetchResult(MakeResult(EFetchStatus::OK), kNow);
    status.ApplyFetchResult(MakeResult(EFetchStatus::NETWORK_ERROR), kNow.addSecs(5 * 60));
    QCOMPARE(status.GetLimitDisplayState(kNow.addSecs(5 * 60)), ELimitDisplayState::NORMAL);
    QCOMPARE(status.GetLimitDisplayState(kNow.addSecs(16 * 60)), ELimitDisplayState::STALE);
    QCOMPARE(status.GetLimits().m_fiveHour.m_dPercent, 42.0);
    QCOMPARE(status.GetLastStatus(), EFetchStatus::NETWORK_ERROR);
    QCOMPARE(status.GetPlanLabel(), QStringLiteral("Max 5x"));
}

void TestSystemStatus::TestStaleAfterSleepEvenIfLastStatusOk()
{
    SystemStatus status;
    status.ApplyFetchResult(MakeResult(EFetchStatus::OK), kNow);
    QCOMPARE(status.GetLimitDisplayState(kNow.addSecs(20 * 60)), ELimitDisplayState::STALE);
}

void TestSystemStatus::TestKeychainDeniedClearsLimits()
{
    SystemStatus status;
    status.ApplyFetchResult(MakeResult(EFetchStatus::OK), kNow);
    status.ApplyFetchResult(MakeResult(EFetchStatus::KEYCHAIN_DENIED), kNow.addSecs(60));
    QCOMPARE(status.GetLimitDisplayState(kNow.addSecs(60)), ELimitDisplayState::EMPTY);
    QVERIFY(!status.HasLimits());
}

void TestSystemStatus::TestRefreshDueOnOpenAfter30Seconds()
{
    SystemStatus status;
    status.ApplyFetchResult(MakeResult(EFetchStatus::OK), kNow);
    QVERIFY(!status.IsRefreshDueOnOpen(kNow.addSecs(10)));
    QVERIFY(status.IsRefreshDueOnOpen(kNow.addSecs(31)));
}

void TestSystemStatus::TestNoOpenRefreshAfterKeychainDenied()
{
    SystemStatus status;
    status.ApplyFetchResult(MakeResult(EFetchStatus::KEYCHAIN_DENIED), kNow);
    QVERIFY(!status.IsRefreshDueOnOpen(kNow.addSecs(60)));
    status.ApplyFetchResult(MakeResult(EFetchStatus::OK), kNow.addSecs(120));
    QVERIFY(status.IsRefreshDueOnOpen(kNow.addSecs(200)));
}

QTEST_GUILESS_MAIN(TestSystemStatus)
#include "TestSystemStatus.moc"
