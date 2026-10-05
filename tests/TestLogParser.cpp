#include "Analysis/LogParser.h"

#include <QtTest>

namespace
{
const QByteArray kAssistantLine = QByteArrayLiteral(R"({"parentUuid":"p1","isSidechain":false,"message":{"model":"claude-opus-5-5","id":"msg_A","type":"message","role":"assistant","content":[],"usage":{"input_tokens":2,"cache_creation_input_tokens":18829,"cache_read_input_tokens":20738,"output_tokens":558,"cache_creation":{"ephemeral_1h_input_tokens":18000,"ephemeral_5m_input_tokens":829},"speed":"standard"}},"requestId":"req_A","type":"assistant","uuid":"u1","timestamp":"2026-09-27T00:10:21.996Z","cwd":"/Users/me/Desktop/Games/CaptureGame","sessionId":"sess-1","version":"2.1.282"})");
}

class TestLogParser : public QObject
{
    Q_OBJECT

private slots:
    void TestParsesAssistantUsage();
    void TestFallsBackToTotalCacheCreation();
    void TestMarksFastSpeed();
    void TestIgnoresSyntheticModel();
    void TestIgnoresNonAssistantLines();
    void TestIgnoresBrokenJson();
    void TestParsesAiTitle();
    void TestUsesFallbackSessionId();
    void TestKeyWithoutRequestId();
    void TestFindsSessionIdFromPath();
};

void TestLogParser::TestParsesAssistantUsage()
{
    const ParsedLine parsed = LogParser::ParseLine(kAssistantLine, QStringLiteral("unused"));
    QCOMPARE(parsed.m_eKind, ELineKind::USAGE);
    const TokenRecord& record = parsed.m_record;
    QCOMPARE(record.m_strKey, QStringLiteral("msg_A|req_A"));
    QCOMPARE(record.m_dtTimestamp, QDateTime(QDate(2026, 9, 27), QTime(0, 10, 21, 996), Qt::UTC));
    QCOMPARE(record.m_strSessionId, QStringLiteral("sess-1"));
    QCOMPARE(record.m_strProjectPath, QStringLiteral("/Users/me/Desktop/Games/CaptureGame"));
    QCOMPARE(record.m_strModel, QStringLiteral("claude-opus-5-5"));
    QCOMPARE(record.m_llInput, qint64(2));
    QCOMPARE(record.m_llOutput, qint64(558));
    QCOMPARE(record.m_llCacheWrite5m, qint64(829));
    QCOMPARE(record.m_llCacheWrite1h, qint64(18000));
    QCOMPARE(record.m_llCacheRead, qint64(20738));
    QVERIFY(!record.m_bFast);
}

void TestLogParser::TestFallsBackToTotalCacheCreation()
{
    const QByteArray baLine = QByteArrayLiteral(R"({"type":"assistant","sessionId":"s","timestamp":"2026-09-27T00:00:00.000Z","requestId":"r","message":{"id":"m","model":"claude-sonnet-5-5","usage":{"input_tokens":1,"output_tokens":2,"cache_creation_input_tokens":500,"cache_read_input_tokens":3}}})");
    const ParsedLine parsed = LogParser::ParseLine(baLine, QString());
    QCOMPARE(parsed.m_eKind, ELineKind::USAGE);
    QCOMPARE(parsed.m_record.m_llCacheWrite5m, qint64(500));
    QCOMPARE(parsed.m_record.m_llCacheWrite1h, qint64(0));
}

void TestLogParser::TestMarksFastSpeed()
{
    QByteArray baLine = kAssistantLine;
    baLine.replace("\"speed\":\"standard\"", "\"speed\":\"fast\"");
    QVERIFY(LogParser::ParseLine(baLine, QString()).m_record.m_bFast);
}

void TestLogParser::TestIgnoresSyntheticModel()
{
    QByteArray baLine = kAssistantLine;
    baLine.replace("\"model\":\"claude-opus-5-5\"", "\"model\":\"<synthetic>\"");
    QCOMPARE(LogParser::ParseLine(baLine, QString()).m_eKind, ELineKind::IGNORED);
}

void TestLogParser::TestIgnoresNonAssistantLines()
{
    const QByteArray baLine = QByteArrayLiteral(R"({"type":"user","message":{"role":"user","content":"hi"},"toolUseResult":{"usage":{"output_tokens":5}},"timestamp":"2026-09-27T00:00:00.000Z","sessionId":"s"})");
    QCOMPARE(LogParser::ParseLine(baLine, QString()).m_eKind, ELineKind::IGNORED);
}

void TestLogParser::TestIgnoresBrokenJson()
{
    // Cut inside the usage object so the line passes the "usage" prefilter but is not valid JSON.
    const QByteArray baBroken = kAssistantLine.left(kAssistantLine.indexOf("\"output_tokens\""));
    QVERIFY(baBroken.contains("\"usage\""));
    QCOMPARE(LogParser::ParseLine(baBroken, QString()).m_eKind, ELineKind::IGNORED);
}

void TestLogParser::TestParsesAiTitle()
{
    const QByteArray baLine = QByteArrayLiteral(R"({"type":"ai-title","aiTitle":"BundleFusion 논문 정리","sessionId":"sess-1"})");
    const ParsedLine parsed = LogParser::ParseLine(baLine, QString());
    QCOMPARE(parsed.m_eKind, ELineKind::TITLE);
    QCOMPARE(parsed.m_strSessionId, QStringLiteral("sess-1"));
    QCOMPARE(parsed.m_strTitle, QStringLiteral("BundleFusion 논문 정리"));
}

void TestLogParser::TestUsesFallbackSessionId()
{
    QByteArray baLine = kAssistantLine;
    baLine.replace(",\"sessionId\":\"sess-1\"", "");
    const ParsedLine parsed = LogParser::ParseLine(baLine, QStringLiteral("from-path"));
    QCOMPARE(parsed.m_record.m_strSessionId, QStringLiteral("from-path"));
}

void TestLogParser::TestKeyWithoutRequestId()
{
    QByteArray baLine = kAssistantLine;
    baLine.replace("\"requestId\":\"req_A\",", "");
    QCOMPARE(LogParser::ParseLine(baLine, QString()).m_record.m_strKey, QStringLiteral("msg_A"));
}

void TestLogParser::TestFindsSessionIdFromPath()
{
    QCOMPARE(LogParser::FindSessionIdFromPath(QStringLiteral("/h/.claude/projects/-Users-me-A/abc-123.jsonl")), QStringLiteral("abc-123"));
    QCOMPARE(LogParser::FindSessionIdFromPath(QStringLiteral("/h/.claude/projects/-Users-me-A/abc-123/subagents/agent-x.jsonl")), QStringLiteral("abc-123"));
}

QTEST_GUILESS_MAIN(TestLogParser)
#include "TestLogParser.moc"
