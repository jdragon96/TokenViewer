#include "Service/RefreshPolicy.h"

#include <QtTest>

namespace
{
constexpr int kBaseMs = 3 * 60 * 1000;
}

class TestRefreshPolicy : public QObject
{
    Q_OBJECT

private slots:
    void TestOkUsesBaseInterval();
    void TestRateLimitDoublesDelay();
    void TestRateLimitIsCapped();
    void TestKeychainDeniedStopsAutoRetry();
    void TestOtherFailuresKeepBaseInterval();
};

void TestRefreshPolicy::TestOkUsesBaseInterval()
{
    const RefreshDecision decision = RefreshPolicy::DecideNext(EFetchStatus::OK, kBaseMs, 4 * kBaseMs);
    QCOMPARE(decision.m_iDelayMs, kBaseMs);
    QVERIFY(decision.m_bAutoRetry);
}

void TestRefreshPolicy::TestRateLimitDoublesDelay()
{
    QCOMPARE(RefreshPolicy::DecideNext(EFetchStatus::RATE_LIMITED, kBaseMs, 0).m_iDelayMs, 2 * kBaseMs);
    QCOMPARE(RefreshPolicy::DecideNext(EFetchStatus::RATE_LIMITED, kBaseMs, 2 * kBaseMs).m_iDelayMs, 4 * kBaseMs);
}

void TestRefreshPolicy::TestRateLimitIsCapped()
{
    QCOMPARE(RefreshPolicy::DecideNext(EFetchStatus::RATE_LIMITED, kBaseMs, 25 * 60 * 1000).m_iDelayMs, RefreshPolicy::kMaxBackoffMs);
}

void TestRefreshPolicy::TestKeychainDeniedStopsAutoRetry()
{
    QVERIFY(!RefreshPolicy::DecideNext(EFetchStatus::KEYCHAIN_DENIED, kBaseMs, kBaseMs).m_bAutoRetry);
}

void TestRefreshPolicy::TestOtherFailuresKeepBaseInterval()
{
    for (const EFetchStatus eStatus : { EFetchStatus::NETWORK_ERROR, EFetchStatus::SERVER_ERROR, EFetchStatus::TOKEN_EXPIRED,
             EFetchStatus::NOT_LOGGED_IN, EFetchStatus::BAD_RESPONSE })
    {
        const RefreshDecision decision = RefreshPolicy::DecideNext(eStatus, kBaseMs, 4 * kBaseMs);
        QCOMPARE(decision.m_iDelayMs, kBaseMs);
        QVERIFY(decision.m_bAutoRetry);
    }
}

QTEST_GUILESS_MAIN(TestRefreshPolicy)
#include "TestRefreshPolicy.moc"
