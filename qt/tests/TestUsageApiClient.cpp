#include "Service/UsageApiClient.h"

#include "LoopbackHttpServer.h"
#include "TestHelpers.h"

#include <QTcpServer>
#include <QtTest>

namespace
{
constexpr int kLoopbackTimeoutMs = 5000;
constexpr int kShortTimeoutMs = 300;
}

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
    void TestWindowFormatDriftIsBadResponse();
    void TestNullPercentLeavesMeterEmpty();
    void TestMapsHttpStatuses();
    void TestParsesRecordedResponseShape();
    void TestFetchSendsUsageHeadersAndParses();
    void TestFetchMapsServerStatus();
    void TestFetchRedirectIsBadResponse();
    void TestFetchTimeoutIsNetworkError();
    void TestFetchStalledBodyIsNetworkError();
    void TestFetchConnectFailureIsNetworkError();
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

void TestUsageApiClient::TestWindowFormatDriftIsBadResponse()
{
    const QByteArray baDrift = QByteArrayLiteral(R"({"five_hour":{"percent":42},"seven_day":null})");
    const FetchResult resultDrift = UsageApiClient::ParseUsageResponse(baDrift);
    QCOMPARE(resultDrift.m_eStatus, EFetchStatus::BAD_RESPONSE);
    QCOMPARE(resultDrift.m_baRawBody, baDrift);

    const FetchResult resultString = UsageApiClient::ParseUsageResponse(QByteArrayLiteral(
        R"({"five_hour":"42","seven_day":{"utilization":10}})"));
    QCOMPARE(resultString.m_eStatus, EFetchStatus::BAD_RESPONSE);
}

void TestUsageApiClient::TestNullPercentLeavesMeterEmpty()
{
    const FetchResult result = UsageApiClient::ParseUsageResponse(QByteArrayLiteral(
        R"({"five_hour":{"utilization":null},"seven_day":{"utilization":10}})"));
    QCOMPARE(result.m_eStatus, EFetchStatus::OK);
    QVERIFY(!result.m_limits.m_fiveHour.m_bValid);
    QVERIFY(result.m_limits.m_sevenDay.m_bValid);
    QCOMPARE(result.m_limits.m_sevenDay.m_dPercent, 10.0);
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

    QCOMPARE(UsageApiClient::MapHttpResult(200, true, QStringLiteral("Connection closed"), QByteArrayLiteral("{\"five_hour")).m_eStatus, EFetchStatus::NETWORK_ERROR);

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

void TestUsageApiClient::TestFetchSendsUsageHeadersAndParses()
{
    const QByteArray baBody = ReadSourceFile(QStringLiteral("tests/fixtures/usage_response.json"));
    LoopbackHttpServer server(LoopbackHttpServer::MakeResponse(200, "OK", baBody));

    const FetchResult result = UsageApiClient::FetchUsage(server.GetUrl(QStringLiteral("/api/oauth/usage")), QStringLiteral("test-token"), kLoopbackTimeoutMs);

    QCOMPARE(result.m_eStatus, EFetchStatus::OK);
    QCOMPARE(result.m_limits.m_fiveHour.m_dPercent, 42.0);
    QCOMPARE(result.m_limits.m_sevenDay.m_dPercent, 18.0);
    const QByteArray baRequest = server.TakeRequest();
    QVERIFY2(baRequest.startsWith("GET /api/oauth/usage HTTP/1.1\r\n"), baRequest.constData());
    QVERIFY2(baRequest.contains("\r\nAuthorization: Bearer test-token\r\n"), baRequest.constData());
    QVERIFY2(baRequest.contains("\r\nanthropic-beta: oauth-2025-04-20\r\n"), baRequest.constData());
    QVERIFY2(baRequest.contains("\r\nAccept: application/json\r\n"), baRequest.constData());
    QVERIFY2(baRequest.contains("\r\nUser-Agent: TokenViewer/" TV_VERSION "\r\n"), baRequest.constData());
}

void TestUsageApiClient::TestFetchMapsServerStatus()
{
    LoopbackHttpServer server(LoopbackHttpServer::MakeResponse(429, "Too Many Requests", QByteArrayLiteral("{}")));

    const FetchResult result = UsageApiClient::FetchUsage(server.GetUrl(QStringLiteral("/")), QStringLiteral("test-token"), kLoopbackTimeoutMs);

    QCOMPARE(result.m_eStatus, EFetchStatus::RATE_LIMITED);
    QCOMPARE(result.m_iHttpStatus, 429);
}

void TestUsageApiClient::TestFetchRedirectIsBadResponse()
{
    LoopbackHttpServer serverTarget(LoopbackHttpServer::MakeResponse(200, "OK", ReadSourceFile(QStringLiteral("tests/fixtures/usage_response.json"))));
    LoopbackHttpServer serverRedirect(LoopbackHttpServer::MakeRedirectResponse(serverTarget.GetUrl(QStringLiteral("/api/oauth/usage"))));

    const FetchResult result = UsageApiClient::FetchUsage(serverRedirect.GetUrl(QStringLiteral("/api/oauth/usage")), QStringLiteral("test-token"), kLoopbackTimeoutMs);

    QCOMPARE(result.m_eStatus, EFetchStatus::BAD_RESPONSE);
    QCOMPARE(result.m_iHttpStatus, 302);
    QVERIFY(serverTarget.TakeRequest().isEmpty());
}

void TestUsageApiClient::TestFetchTimeoutIsNetworkError()
{
    LoopbackHttpServer server(QByteArray(), LoopbackHttpServer::EMode::HOLD);

    const FetchResult result = UsageApiClient::FetchUsage(server.GetUrl(QStringLiteral("/")), QStringLiteral("test-token"), kShortTimeoutMs);

    QCOMPARE(result.m_eStatus, EFetchStatus::NETWORK_ERROR);
    QCOMPARE(result.m_strDetail, QStringLiteral("응답 시간 초과"));
    QCOMPARE(result.m_iHttpStatus, 0);
}

void TestUsageApiClient::TestFetchStalledBodyIsNetworkError()
{
    // A 200 whose body never finishes must not be parsed as a changed response format.
    LoopbackHttpServer server(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n{\"five_hour\""), LoopbackHttpServer::EMode::HOLD);

    const FetchResult result = UsageApiClient::FetchUsage(server.GetUrl(QStringLiteral("/")), QStringLiteral("test-token"), kShortTimeoutMs);

    QCOMPARE(result.m_eStatus, EFetchStatus::NETWORK_ERROR);
    QCOMPARE(result.m_strDetail, QStringLiteral("응답 시간 초과"));
    QVERIFY(result.m_baRawBody.isEmpty());
}

void TestUsageApiClient::TestFetchConnectFailureIsNetworkError()
{
    quint16 usPort = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
        usPort = probe.serverPort();
    }

    const FetchResult result = UsageApiClient::FetchUsage(QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(usPort)), QStringLiteral("test-token"), kLoopbackTimeoutMs);

    QCOMPARE(result.m_eStatus, EFetchStatus::NETWORK_ERROR);
    QCOMPARE(result.m_strDetail, QStringLiteral("서버에 연결할 수 없음"));
    QCOMPARE(result.m_iHttpStatus, 0);
}

QTEST_GUILESS_MAIN(TestUsageApiClient)
#include "TestUsageApiClient.moc"
