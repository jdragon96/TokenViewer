#include "Analysis/TokenAggregator.h"

#include "TestHelpers.h"

#include <QtTest>

#include <ctime>

namespace
{
TokenRecord MakeRecord(const QString& key, const QString& timestamp, const QString& sessionId,
    const QString& projectPath, const QString& model, qint64 output)
{
    TokenRecord record;
    record.m_strKey = key;
    record.m_dtTimestamp = QDateTime::fromString(timestamp, Qt::ISODate);
    record.m_strSessionId = sessionId;
    record.m_strProjectPath = projectPath;
    record.m_strModel = model;
    record.m_llOutput = output;
    return record;
}

AggregateQuery MakeQuery(EBreakdownDimension dimension)
{
    AggregateQuery query;
    query.m_eDimension = dimension;
    return query;
}
}

class TestTokenAggregator : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void TestGroupsByProjectAndSortsByCost();
    void TestDisambiguatesSameProjectNames();
    void TestGroupsByModelWithDisplayNames();
    void TestSessionUsesTitleOrFallback();
    void TestFiltersByPeriodStart();
    void TestFoldsBeyondTopCountIntoOther();
    void TestMarksApproximateAndUnpriced();
    void TestCalculatesFiveHourWindowStart();
    void TestCalculatesTodayFromLocalMidnight();
    void TestFormatsModelNames();

private:
    PriceTable m_priceTable;
};

void TestTokenAggregator::initTestCase()
{
    qputenv("TZ", "Asia/Seoul");
    tzset();
    QVERIFY(m_priceTable.LoadFromJson(ReadSourceFile(QStringLiteral("resources/prices.json"))));
}

void TestTokenAggregator::TestGroupsByProjectAndSortsByCost()
{
    // Output-only records: Opus 5.5 is $20 per 1M output tokens, Sonnet 5.5 is $10.
    LogSnapshot snapshot;
    snapshot.m_vecRecords = {
        MakeRecord("k1", "2026-10-05T01:00:00Z", "s1", "/Users/me/A", "claude-opus-5-5", 100000),
        MakeRecord("k2", "2026-10-05T01:10:00Z", "s1", "/Users/me/A", "claude-sonnet-5-5", 100000),
        MakeRecord("k3", "2026-10-05T01:20:00Z", "s2", "/Users/me/B", "claude-opus-5-5", 300000),
    };
    const Breakdown breakdown = TokenAggregator::Aggregate(snapshot, m_priceTable, MakeQuery(EBreakdownDimension::PROJECT));
    QCOMPARE(breakdown.m_vecRows.size(), 2);
    QCOMPARE(breakdown.m_vecRows[0].m_strLabel, QStringLiteral("B"));
    QCOMPARE(breakdown.m_vecRows[0].m_strDetail, QStringLiteral("/Users/me/B"));
    QVERIFY(qFuzzyCompare(breakdown.m_vecRows[0].m_dCostUsd, 6.0));
    QCOMPARE(breakdown.m_vecRows[1].m_strLabel, QStringLiteral("A"));
    QVERIFY(qFuzzyCompare(breakdown.m_vecRows[1].m_dCostUsd, 3.0));
    QVERIFY(qFuzzyCompare(breakdown.m_dTotalUsd, 9.0));
    QCOMPARE(breakdown.m_iOtherCount, 0);
}

void TestTokenAggregator::TestDisambiguatesSameProjectNames()
{
    LogSnapshot snapshot;
    snapshot.m_vecRecords = {
        MakeRecord("k1", "2026-10-05T01:00:00Z", "s1", "/Users/me/x/App", "claude-opus-5-5", 200000),
        MakeRecord("k2", "2026-10-05T01:00:00Z", "s2", "/Users/me/y/App", "claude-opus-5-5", 100000),
    };
    const Breakdown breakdown = TokenAggregator::Aggregate(snapshot, m_priceTable, MakeQuery(EBreakdownDimension::PROJECT));
    QCOMPARE(breakdown.m_vecRows[0].m_strLabel, QStringLiteral("x/App"));
    QCOMPARE(breakdown.m_vecRows[1].m_strLabel, QStringLiteral("y/App"));
}

void TestTokenAggregator::TestGroupsByModelWithDisplayNames()
{
    LogSnapshot snapshot;
    snapshot.m_vecRecords = {
        MakeRecord("k1", "2026-10-05T01:00:00Z", "s1", "/Users/me/A", "claude-opus-5-5", 100000),
        MakeRecord("k2", "2026-10-05T01:00:00Z", "s1", "/Users/me/A", "claude-sonnet-5-5", 100000),
    };
    const Breakdown breakdown = TokenAggregator::Aggregate(snapshot, m_priceTable, MakeQuery(EBreakdownDimension::MODEL));
    QCOMPARE(breakdown.m_vecRows[0].m_strLabel, QStringLiteral("Opus 5.5"));
    QCOMPARE(breakdown.m_vecRows[0].m_strDetail, QStringLiteral("claude-opus-5-5"));
    QCOMPARE(breakdown.m_vecRows[1].m_strLabel, QStringLiteral("Sonnet 5.5"));
}

void TestTokenAggregator::TestSessionUsesTitleOrFallback()
{
    LogSnapshot snapshot;
    snapshot.m_vecRecords = {
        MakeRecord("k1", "2026-10-05T05:27:00Z", "s1", "/Users/me/A", "claude-opus-5-5", 100000),
        MakeRecord("k2", "2026-10-05T05:27:00Z", "s2", "/Users/me/A", "claude-opus-5-5", 200000),
    };
    snapshot.m_hashSessionTitles.insert(QStringLiteral("s1"), QStringLiteral("논문 정리"));
    const Breakdown breakdown = TokenAggregator::Aggregate(snapshot, m_priceTable, MakeQuery(EBreakdownDimension::SESSION));
    // 05:27 UTC is 14:27 in Seoul.
    QCOMPARE(breakdown.m_vecRows[0].m_strLabel, QStringLiteral("A · 10/5 14:27"));
    QCOMPARE(breakdown.m_vecRows[0].m_strDetail, QStringLiteral("s2"));
    QCOMPARE(breakdown.m_vecRows[1].m_strLabel, QStringLiteral("논문 정리"));
}

void TestTokenAggregator::TestFiltersByPeriodStart()
{
    LogSnapshot snapshot;
    snapshot.m_vecRecords = {
        MakeRecord("k1", "2026-10-04T23:59:59Z", "s1", "/Users/me/Old", "claude-opus-5-5", 100000),
        MakeRecord("k2", "2026-10-05T00:00:00Z", "s1", "/Users/me/New", "claude-opus-5-5", 100000),
    };
    AggregateQuery query = MakeQuery(EBreakdownDimension::PROJECT);
    query.m_dtFrom = QDateTime(QDate(2026, 10, 5), QTime(0, 0), Qt::UTC);
    const Breakdown breakdown = TokenAggregator::Aggregate(snapshot, m_priceTable, query);
    QCOMPARE(breakdown.m_vecRows.size(), 1);
    QCOMPARE(breakdown.m_vecRows[0].m_strLabel, QStringLiteral("New"));
}

void TestTokenAggregator::TestFoldsBeyondTopCountIntoOther()
{
    LogSnapshot snapshot;
    for (int iIndex = 1; iIndex <= 7; ++iIndex)
    {
        snapshot.m_vecRecords.append(MakeRecord(QStringLiteral("k%1").arg(iIndex), "2026-10-05T01:00:00Z", "s1",
            QStringLiteral("/Users/me/P%1").arg(iIndex), "claude-opus-5-5", (8 - iIndex) * 100000));
    }
    const Breakdown breakdown = TokenAggregator::Aggregate(snapshot, m_priceTable, MakeQuery(EBreakdownDimension::PROJECT));
    QCOMPARE(breakdown.m_vecRows.size(), 5);
    QCOMPARE(breakdown.m_vecRows[0].m_strLabel, QStringLiteral("P1"));
    QCOMPARE(breakdown.m_iOtherCount, 2);
    QCOMPARE(breakdown.m_otherRow.m_strLabel, QStringLiteral("기타 2개"));
    // P6 ($4) + P7 ($2)
    QVERIFY(qFuzzyCompare(breakdown.m_otherRow.m_dCostUsd, 6.0));
    QVERIFY(qFuzzyCompare(breakdown.m_dTotalUsd, 56.0));
}

void TestTokenAggregator::TestMarksApproximateAndUnpriced()
{
    LogSnapshot snapshot;
    snapshot.m_vecRecords = {
        MakeRecord("k1", "2026-10-05T01:00:00Z", "s1", "/Users/me/A", "claude-opus-6", 300000),
        MakeRecord("k2", "2026-10-05T01:00:00Z", "s1", "/Users/me/B", "gpt-x", 100000),
        MakeRecord("k3", "2026-10-05T01:00:00Z", "s1", "/Users/me/C", "claude-opus-5-5", 200000),
        MakeRecord("k4", "2026-10-05T01:00:00Z", "s1", "/Users/me/C", "gpt-x", 100000),
    };
    const Breakdown breakdown = TokenAggregator::Aggregate(snapshot, m_priceTable, MakeQuery(EBreakdownDimension::PROJECT));
    QCOMPARE(breakdown.m_vecRows.size(), 3);
    QCOMPARE(breakdown.m_vecRows[0].m_strLabel, QStringLiteral("A"));
    QVERIFY(breakdown.m_vecRows[0].m_bApproximate);
    QVERIFY(!breakdown.m_vecRows[0].m_bUnpriced);
    QCOMPARE(breakdown.m_vecRows[1].m_strLabel, QStringLiteral("C"));
    QVERIFY(breakdown.m_vecRows[1].m_bApproximate);
    QVERIFY(!breakdown.m_vecRows[1].m_bUnpriced);
    QCOMPARE(breakdown.m_vecRows[2].m_strLabel, QStringLiteral("B"));
    QVERIFY(breakdown.m_vecRows[2].m_bUnpriced);
}

void TestTokenAggregator::TestCalculatesFiveHourWindowStart()
{
    const QDateTime dtNow(QDate(2026, 10, 5), QTime(5, 0), Qt::UTC);
    const QDateTime dtResetsAt(QDate(2026, 10, 5), QTime(7, 40), Qt::UTC);
    QCOMPARE(TokenAggregator::CalculatePeriodStart(EBreakdownPeriod::FIVE_HOUR_WINDOW, dtNow, dtResetsAt),
        QDateTime(QDate(2026, 10, 5), QTime(2, 40), Qt::UTC));
    QCOMPARE(TokenAggregator::CalculatePeriodStart(EBreakdownPeriod::FIVE_HOUR_WINDOW, dtNow, QDateTime()),
        QDateTime(QDate(2026, 10, 5), QTime(0, 0), Qt::UTC));
    QCOMPARE(TokenAggregator::CalculatePeriodStart(EBreakdownPeriod::SEVEN_DAYS, dtNow, QDateTime()),
        QDateTime(QDate(2026, 9, 28), QTime(5, 0), Qt::UTC));
}

void TestTokenAggregator::TestCalculatesTodayFromLocalMidnight()
{
    // 00:30 UTC is 09:30 on 10/5 in Seoul, so "today" starts at 10/5 00:00 KST = 10/4 15:00 UTC.
    QCOMPARE(TokenAggregator::CalculatePeriodStart(EBreakdownPeriod::TODAY, QDateTime(QDate(2026, 10, 5), QTime(0, 30), Qt::UTC), QDateTime()),
        QDateTime(QDate(2026, 10, 4), QTime(15, 0), Qt::UTC));
    // 14:59 UTC is 23:59 on 10/4 in Seoul.
    QCOMPARE(TokenAggregator::CalculatePeriodStart(EBreakdownPeriod::TODAY, QDateTime(QDate(2026, 10, 4), QTime(14, 59), Qt::UTC), QDateTime()),
        QDateTime(QDate(2026, 10, 3), QTime(15, 0), Qt::UTC));
}

void TestTokenAggregator::TestFormatsModelNames()
{
    QCOMPARE(TokenAggregator::FormatModelName(QStringLiteral("claude-opus-5-5")), QStringLiteral("Opus 5.5"));
    QCOMPARE(TokenAggregator::FormatModelName(QStringLiteral("claude-haiku-4-5-20251001")), QStringLiteral("Haiku 4.5"));
    QCOMPARE(TokenAggregator::FormatModelName(QStringLiteral("claude-sonnet-5")), QStringLiteral("Sonnet 5"));
    QCOMPARE(TokenAggregator::FormatModelName(QStringLiteral("claude-fable-5-1")), QStringLiteral("Fable 5.1"));
    QCOMPARE(TokenAggregator::FormatModelName(QStringLiteral("gpt-x")), QStringLiteral("gpt-x"));
}

QTEST_GUILESS_MAIN(TestTokenAggregator)
#include "TestTokenAggregator.moc"
