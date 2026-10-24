#include "Service/WinHttpClient.h"

#include "LoopbackHttpServer.h"

#include <QElapsedTimer>
#include <QTcpServer>
#include <QtTest>

namespace
{
constexpr int kGenerousTimeoutMs = 5000;
constexpr unsigned long kErrorTimeout = 12002;
constexpr unsigned long kErrorNameNotResolved = 12007;
constexpr unsigned long kErrorCannotConnect = 12029;
constexpr unsigned long kErrorConnectionReset = 12030;
constexpr unsigned long kErrorSecureFailure = 12175;
constexpr unsigned long kErrorSecureInvalidCa = 12045;
}

class TestWinHttpClient : public QObject
{
    Q_OBJECT

private slots:
    void TestReturnsStatusBodyAndSendsHeaders();
    void TestPassesErrorStatusThrough();
    void TestDoesNotFollowRedirects();
    void TestTimesOutWhenServerNeverAnswers();
    void TestTimesOutWhenBodyStalls();
    void TestClosedPortIsNetworkError();
    void TestReadsBodyLargerThanOneChunk();
    void TestUnsupportedSchemeIsNetworkError();
    void TestDescribesErrorsInKorean();
};

void TestWinHttpClient::TestReturnsStatusBodyAndSendsHeaders()
{
    const QByteArray baBody = QByteArrayLiteral(R"({"five_hour":{"utilization":42}})");
    LoopbackHttpServer server(LoopbackHttpServer::MakeResponse(200, "OK", baBody));
    const HttpHeaders vecHeaders = {
        { "Authorization", "Bearer test-token" },
        { "anthropic-beta", "oauth-2025-04-20" },
        { "Accept", "application/json" },
        { "User-Agent", "TokenViewer/9.9.9" },
    };

    const WinHttpResponse response = WinHttpClient::Get(server.GetUrl(QStringLiteral("/api/oauth/usage?a=1")), vecHeaders, kGenerousTimeoutMs);

    QVERIFY2(!response.m_bNetworkError, qPrintable(response.m_strError));
    QVERIFY(!response.m_bTimedOut);
    QCOMPARE(response.m_iHttpStatus, 200);
    QCOMPARE(response.m_baBody, baBody);
    QVERIFY(response.m_strError.isEmpty());

    const QByteArray baRequest = server.TakeRequest();
    QVERIFY2(baRequest.startsWith("GET /api/oauth/usage?a=1 HTTP/1.1\r\n"), baRequest.constData());
    QVERIFY2(baRequest.contains("\r\nAuthorization: Bearer test-token\r\n"), baRequest.constData());
    QVERIFY2(baRequest.contains("\r\nanthropic-beta: oauth-2025-04-20\r\n"), baRequest.constData());
    QVERIFY2(baRequest.contains("\r\nAccept: application/json\r\n"), baRequest.constData());
    QVERIFY2(baRequest.contains("\r\nUser-Agent: TokenViewer/9.9.9\r\n"), baRequest.constData());
    QCOMPARE(baRequest.toLower().count("user-agent:"), 1);
    QVERIFY2(baRequest.contains("\r\nHost: 127.0.0.1:" + QByteArray::number(server.GetPort()) + "\r\n"), baRequest.constData());
}

void TestWinHttpClient::TestPassesErrorStatusThrough()
{
    const QByteArray baBody = QByteArrayLiteral(R"({"error":{"type":"rate_limit_error"}})");
    LoopbackHttpServer server(LoopbackHttpServer::MakeResponse(429, "Too Many Requests", baBody));

    const WinHttpResponse response = WinHttpClient::Get(server.GetUrl(QStringLiteral("/")), HttpHeaders(), kGenerousTimeoutMs);

    QVERIFY2(!response.m_bNetworkError, qPrintable(response.m_strError));
    QCOMPARE(response.m_iHttpStatus, 429);
    QCOMPARE(response.m_baBody, baBody);
}

void TestWinHttpClient::TestDoesNotFollowRedirects()
{
    // Following a redirect would hand the bearer token to whatever host Location names.
    LoopbackHttpServer serverTarget(LoopbackHttpServer::MakeResponse(200, "OK", QByteArrayLiteral("{}")));
    LoopbackHttpServer serverRedirect(LoopbackHttpServer::MakeRedirectResponse(serverTarget.GetUrl(QStringLiteral("/elsewhere"))));
    const HttpHeaders vecHeaders = { { "Authorization", "Bearer test-token" } };

    const WinHttpResponse response = WinHttpClient::Get(serverRedirect.GetUrl(QStringLiteral("/")), vecHeaders, kGenerousTimeoutMs);

    QVERIFY2(!response.m_bNetworkError, qPrintable(response.m_strError));
    QCOMPARE(response.m_iHttpStatus, 302);
    QVERIFY(serverRedirect.TakeRequest().contains("\r\nAuthorization: Bearer test-token\r\n"));
    const QByteArray baTargetRequest = serverTarget.TakeRequest();
    QVERIFY2(baTargetRequest.isEmpty(), baTargetRequest.constData());
}

void TestWinHttpClient::TestTimesOutWhenServerNeverAnswers()
{
    constexpr int kTimeoutMs = 500;
    LoopbackHttpServer server(QByteArray(), LoopbackHttpServer::EMode::HOLD);

    QElapsedTimer timerRequest;
    timerRequest.start();
    const WinHttpResponse response = WinHttpClient::Get(server.GetUrl(QStringLiteral("/")), HttpHeaders(), kTimeoutMs);
    const qint64 llElapsedMs = timerRequest.elapsed();

    QVERIFY(response.m_bTimedOut);
    QVERIFY(response.m_bNetworkError);
    QCOMPARE(response.m_iHttpStatus, 0);
    QCOMPARE(response.m_strError, QStringLiteral("응답 시간 초과"));
    QVERIFY2(llElapsedMs >= kTimeoutMs - 50, qPrintable(QString::number(llElapsedMs)));
    QVERIFY2(llElapsedMs < kTimeoutMs + 1000, qPrintable(QString::number(llElapsedMs)));
    // The request did go out; only the answer was missing.
    QVERIFY(server.TakeRequest().startsWith("GET / HTTP/1.1\r\n"));
}

void TestWinHttpClient::TestTimesOutWhenBodyStalls()
{
    constexpr int kTimeoutMs = 500;
    const QByteArray baPartial = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n{\"five_hour\"");
    LoopbackHttpServer server(baPartial, LoopbackHttpServer::EMode::HOLD);

    QElapsedTimer timerRequest;
    timerRequest.start();
    const WinHttpResponse response = WinHttpClient::Get(server.GetUrl(QStringLiteral("/")), HttpHeaders(), kTimeoutMs);
    const qint64 llElapsedMs = timerRequest.elapsed();

    QVERIFY(response.m_bTimedOut);
    QVERIFY(response.m_bNetworkError);
    QCOMPARE(response.m_strError, QStringLiteral("응답 시간 초과"));
    QVERIFY2(llElapsedMs < kTimeoutMs + 1000, qPrintable(QString::number(llElapsedMs)));
}

void TestWinHttpClient::TestClosedPortIsNetworkError()
{
    quint16 usPort = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
        usPort = probe.serverPort();
    }

    QElapsedTimer timerRequest;
    timerRequest.start();
    const WinHttpResponse response = WinHttpClient::Get(QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(usPort)), HttpHeaders(), kGenerousTimeoutMs);

    QVERIFY(response.m_bNetworkError);
    QVERIFY(!response.m_bTimedOut);
    QVERIFY2(timerRequest.elapsed() < kGenerousTimeoutMs, qPrintable(QString::number(timerRequest.elapsed())));
    QCOMPARE(response.m_ulErrorCode, kErrorCannotConnect);
    QCOMPARE(response.m_strError, WinHttpClient::DescribeError(kErrorCannotConnect));
}

void TestWinHttpClient::TestReadsBodyLargerThanOneChunk()
{
    QByteArray baBody(200 * 1024, Qt::Uninitialized);
    for (int i = 0; i < baBody.size(); ++i)
    {
        baBody[i] = static_cast<char>('a' + i % 26);
    }
    LoopbackHttpServer server(LoopbackHttpServer::MakeResponse(200, "OK", baBody));

    const WinHttpResponse response = WinHttpClient::Get(server.GetUrl(QStringLiteral("/big")), HttpHeaders(), kGenerousTimeoutMs);

    QVERIFY2(!response.m_bNetworkError, qPrintable(response.m_strError));
    QCOMPARE(response.m_iHttpStatus, 200);
    QCOMPARE(response.m_baBody.size(), baBody.size());
    QVERIFY(response.m_baBody == baBody);
}

void TestWinHttpClient::TestUnsupportedSchemeIsNetworkError()
{
    const WinHttpResponse response = WinHttpClient::Get(QUrl(QStringLiteral("ftp://127.0.0.1/file")), HttpHeaders(), kGenerousTimeoutMs);

    QVERIFY(response.m_bNetworkError);
    QVERIFY(!response.m_bTimedOut);
    QVERIFY(!response.m_strError.isEmpty());
}

void TestWinHttpClient::TestDescribesErrorsInKorean()
{
    QCOMPARE(WinHttpClient::DescribeError(kErrorTimeout), QStringLiteral("응답 시간 초과"));
    QCOMPARE(WinHttpClient::DescribeError(kErrorNameNotResolved), QStringLiteral("서버 이름을 찾을 수 없음"));
    QCOMPARE(WinHttpClient::DescribeError(kErrorCannotConnect), QStringLiteral("서버에 연결할 수 없음"));
    QCOMPARE(WinHttpClient::DescribeError(kErrorConnectionReset), QStringLiteral("서버 연결이 끊어짐"));
    QCOMPARE(WinHttpClient::DescribeError(kErrorSecureFailure), QStringLiteral("보안 연결(TLS) 실패"));
    QCOMPARE(WinHttpClient::DescribeError(kErrorSecureInvalidCa), QStringLiteral("보안 연결(TLS) 실패"));
    QCOMPARE(WinHttpClient::DescribeError(12345), QStringLiteral("네트워크 오류 (코드 12345)"));
}

QTEST_GUILESS_MAIN(TestWinHttpClient)
#include "TestWinHttpClient.moc"
