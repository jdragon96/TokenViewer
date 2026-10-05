#include "Service/UsageApiClient.h"

#include "TestHelpers.h"

#include <QtTest>

class TestUsageApiClient : public QObject
{
    Q_OBJECT

private slots:
    void TestParsesUtilizationAndIsoReset();
    void TestAcceptsUsedPercentageAndEpoch();
    void TestClampsOutOfRange();
    void TestNullWindowLeavesMeterEmpty();
    void TestMissingWindowsIsBadResponse();
    void TestNotJsonIsBadResponse();
    void TestMapsHttpStatuses();
    void TestParsesRecordedResponseShape();
};

void TestUsageApiClient::TestParsesUtilizationAndIsoReset()
{
    const FetchResult result = UsageApiClient::ParseUsageResponse(QByteArrayLiteral(
        R"({"five_hour":{"utilization":42.0,"resets_at":"2026-10-05T07:40:00.123456+00:00"},"seven_day":{"utilization":18,"resets_at":"2026-10-08T00:00:00Z"}})"));
    QCOMPARE(result.m_eStatus, EFetchStatus::OK);
    QVERIFY(result.m_limits.m_fiveHour.m_bValid);
    QCOMPARE(result.m_limits.m_fiveHour.m_dPercent, 42.0);
    QCOMPARE(result.m_limits.m_fiveHour.m_dtResetsAt, QDateTime(QDate(2026, 10, 5), QTime(7, 40, 0, 123), Qt::UTC));
    QCOMPARE(result.m_limits.m_sevenDay.m_dPercent, 18.0);
    QCOMPARE(result.m_limits.m_sevenDay.m_dtResetsAt, QDateTime(QDate(2026, 10, 8), QTime(0, 0), Qt::UTC));
}

void TestUsageApiClient::TestAcceptsUsedPercentageAndEpoch()
{
    const FetchResult result = UsageApiClient::ParseUsageResponse(QByteArrayLiteral(
        R"({"five_hour":{"used_percentage":55,"resets_at":1791160800},"seven_day":{"used_percentage":20.5,"resets_at":1791500000000}})"));
    QCOMPARE(result.m_eStatus, EFetchStatus::OK);
    QCOMPARE(result.m_limits.m_fiveHour.m_dPercent, 55.0);
    QCOMPARE(result.m_limits.m_fiveHour.m_dtResetsAt, QDateTime::fromSecsSinceEpoch(1791160800, Qt::UTC));
    QCOMPARE(result.m_limits.m_sevenDay.m_dtResetsAt, QDateTime::fromMSecsSinceEpoch(1791500000000, Qt::UTC));
}

void TestUsageApiClient::TestClampsOutOfRange()
{
    const FetchResult result = UsageApiClient::ParseUsageResponse(QByteArrayLiteral(
        R"({"five_hour":{"utilization":130},"seven_day":{"utilization":-5}})"));
    QCOMPARE(result.m_limits.m_fiveHour.m_dPercent, 100.0);
    QCOMPARE(result.m_limits.m_sevenDay.m_dPercent, 0.0);
    QVERIFY(!result.m_limits.m_fiveHour.m_dtResetsAt.isValid());
}

void TestUsageApiClient::TestNullWindowLeavesMeterEmpty()
{
    const FetchResult result = UsageApiClient::ParseUsageResponse(QByteArrayLiteral(
        R"({"five_hour":null,"seven_day":{"utilization":18,"resets_at":null}})"));
    QCOMPARE(result.m_eStatus, EFetchStatus::OK);
    QVERIFY(!result.m_limits.m_fiveHour.m_bValid);
    QVERIFY(result.m_limits.m_sevenDay.m_bValid);
    QVERIFY(!result.m_limits.m_sevenDay.m_dtResetsAt.isValid());
}

void TestUsageApiClient::TestMissingWindowsIsBadResponse()
{
    const FetchResult result = UsageApiClient::ParseUsageResponse(QByteArrayLiteral(R"({"other":1})"));
    QCOMPARE(result.m_eStatus, EFetchStatus::BAD_RESPONSE);
    QCOMPARE(result.m_baRawBody, QByteArrayLiteral(R"({"other":1})"));
}

void TestUsageApiClient::TestNotJsonIsBadResponse()
{
    QCOMPARE(UsageApiClient::ParseUsageResponse(QByteArrayLiteral("<html>")).m_eStatus, EFetchStatus::BAD_RESPONSE);
}

void TestUsageApiClient::TestMapsHttpStatuses()
{
    QCOMPARE(UsageApiClient::MapHttpResult(401, true, QString(), QByteArray()).m_eStatus, EFetchStatus::TOKEN_EXPIRED);
    QCOMPARE(UsageApiClient::MapHttpResult(403, true, QString(), QByteArray()).m_eStatus, EFetchStatus::TOKEN_EXPIRED);
    QCOMPARE(UsageApiClient::MapHttpResult(429, true, QString(), QByteArray()).m_eStatus, EFetchStatus::RATE_LIMITED);
    QCOMPARE(UsageApiClient::MapHttpResult(503, true, QString(), QByteArray()).m_eStatus, EFetchStatus::SERVER_ERROR);
    QCOMPARE(UsageApiClient::MapHttpResult(404, true, QString(), QByteArray()).m_eStatus, EFetchStatus::BAD_RESPONSE);

    const FetchResult resultNetwork = UsageApiClient::MapHttpResult(0, true, QStringLiteral("Host not found"), QByteArray());
    QCOMPARE(resultNetwork.m_eStatus, EFetchStatus::NETWORK_ERROR);
    QVERIFY(resultNetwork.m_strDetail.contains(QStringLiteral("Host not found")));

    const FetchResult resultOk = UsageApiClient::MapHttpResult(200, false, QString(), QByteArrayLiteral(R"({"five_hour":{"utilization":1}})"));
    QCOMPARE(resultOk.m_eStatus, EFetchStatus::OK);
}

void TestUsageApiClient::TestParsesRecordedResponseShape()
{
    const FetchResult result = UsageApiClient::ParseUsageResponse(ReadSourceFile(QStringLiteral("tests/fixtures/usage_response.json")));
    QCOMPARE(result.m_eStatus, EFetchStatus::OK);
    QVERIFY(result.m_limits.m_fiveHour.m_bValid);
    QVERIFY(result.m_limits.m_fiveHour.m_dtResetsAt.isValid());
    QVERIFY(result.m_limits.m_sevenDay.m_bValid);
    QVERIFY(result.m_limits.m_sevenDay.m_dtResetsAt.isValid());
}

QTEST_GUILESS_MAIN(TestUsageApiClient)
#include "TestUsageApiClient.moc"
