#include "Analysis/TokenAggregator.h"

#include "TestHelpers.h"

#include <QtTest>

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
    void TestFoldsBeyondMaxRowsIntoOther_data();
    void TestFoldsBeyondMaxRowsIntoOther();
    void TestMaxRowsIncludesTheOtherRow();
    void TestMarksApproximateAndUnpriced();
    void TestCalculatesFiveHourWindowStart();
    void TestCalculatesTodayFromLocalMidnight();
    void TestFormatsModelNames();
    void TestGroupsWindowsPathsIgnoringCase();
    void TestGroupsMixedSeparatorsAndTrailingSeparator();
    void TestLabelUsesMostRecentSpelling();
    void TestDisambiguatesWindowsSameNames();
    void TestDisambiguatesAfterMergingCaseVariants();
    void TestDisambiguatesNamesDifferingOnlyInCase();
    void TestSessionFallbackUsesWindowsProjectName();

private:
    PriceTable m_priceTable;
};

void TestTokenAggregator::initTestCase()
{
    UseSeoulTimeZone();
    QVERIFY(m_priceTable.LoadFromJson(ReadSourceFile(QStringLiteral("../shared/prices.json"))));
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

void TestTokenAggregator::TestFoldsBeyondMaxRowsIntoOther_data()
{
    QTest::addColumn<int>("iGroupCount");
    QTest::addColumn<int>("iNamedCount");
    QTest::addColumn<int>("iOtherCount");
    QTest::addColumn<double>("dOtherUsd");
    QTest::addColumn<double>("dTotalUsd");

    // P1 costs $14, then $2 less per project: P2 $12, P3 $10, P4 $8, P5 $6, P6 $4, P7 $2.
    QTest::newRow("one group") << 1 << 1 << 0 << 0.0 << 14.0;
    QTest::newRow("four groups: all named, no other row") << 4 << 4 << 0 << 0.0 << 44.0;
    // Four slots including the other row, so never a lone "기타 1개".
    QTest::newRow("five groups: three named + 기타 2개 (P4 + P5)") << 5 << 3 << 2 << 14.0 << 50.0;
    QTest::newRow("seven groups: three named + 기타 4개 (P4 to P7)") << 7 << 3 << 4 << 20.0 << 56.0;
}

void TestTokenAggregator::TestFoldsBeyondMaxRowsIntoOther()
{
    QFETCH(int, iGroupCount);
    QFETCH(int, iNamedCount);
    QFETCH(int, iOtherCount);
    QFETCH(double, dOtherUsd);
    QFETCH(double, dTotalUsd);

    LogSnapshot snapshot;
    for (int iIndex = 1; iIndex <= iGroupCount; ++iIndex)
    {
        snapshot.m_vecRecords.append(MakeRecord(QStringLiteral("k%1").arg(iIndex), "2026-10-05T01:00:00Z", "s1",
            QStringLiteral("/Users/me/P%1").arg(iIndex), "claude-opus-5-5", (8 - iIndex) * 100000));
    }
    const Breakdown breakdown = TokenAggregator::Aggregate(snapshot, m_priceTable, MakeQuery(EBreakdownDimension::PROJECT));
    QCOMPARE(breakdown.m_vecRows.size(), iNamedCount);
    for (int iIndex = 0; iIndex < iNamedCount; ++iIndex)
    {
        QCOMPARE(breakdown.m_vecRows[iIndex].m_strLabel, QStringLiteral("P%1").arg(iIndex + 1));
    }
    QCOMPARE(breakdown.m_iOtherCount, iOtherCount);
    if (iOtherCount > 0)
    {
        QCOMPARE(breakdown.m_otherRow.m_strLabel, QStringLiteral("기타 %1개").arg(iOtherCount));
        QVERIFY(qFuzzyCompare(breakdown.m_otherRow.m_dCostUsd, dOtherUsd));
    }
    else
    {
        QVERIFY(breakdown.m_otherRow.m_strLabel.isEmpty());
    }
    QVERIFY(qFuzzyCompare(breakdown.m_dTotalUsd, dTotalUsd));
}

void TestTokenAggregator::TestMaxRowsIncludesTheOtherRow()
{
    // The popup shows four rows, the "기타" row included.
    QCOMPARE(AggregateQuery().m_iMaxRows, 4);

    LogSnapshot snapshot;
    for (int iIndex = 1; iIndex <= 3; ++iIndex)
    {
        snapshot.m_vecRecords.append(MakeRecord(QStringLiteral("k%1").arg(iIndex), "2026-10-05T01:00:00Z", "s1",
            QStringLiteral("/Users/me/P%1").arg(iIndex), "claude-opus-5-5", (8 - iIndex) * 100000));
    }
    AggregateQuery query = MakeQuery(EBreakdownDimension::PROJECT);
    query.m_iMaxRows = 3;
    Breakdown breakdown = TokenAggregator::Aggregate(snapshot, m_priceTable, query);
    QCOMPARE(breakdown.m_vecRows.size(), 3);
    QCOMPARE(breakdown.m_iOtherCount, 0);

    query.m_iMaxRows = 2;
    breakdown = TokenAggregator::Aggregate(snapshot, m_priceTable, query);
    QCOMPARE(breakdown.m_vecRows.size(), 1);
    QCOMPARE(breakdown.m_iOtherCount, 2);
    QCOMPARE(breakdown.m_otherRow.m_strLabel, QStringLiteral("기타 2개"));
    // P2 ($12) + P3 ($10)
    QVERIFY(qFuzzyCompare(breakdown.m_otherRow.m_dCostUsd, 22.0));
    QVERIFY(qFuzzyCompare(breakdown.m_dTotalUsd, 36.0));
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
    // A reset time already in the past is stale; fall back to now - 5h.
    QCOMPARE(TokenAggregator::CalculatePeriodStart(EBreakdownPeriod::FIVE_HOUR_WINDOW, dtNow, dtNow.addSecs(-3600)),
        QDateTime(QDate(2026, 10, 5), QTime(0, 0), Qt::UTC));
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

void TestTokenAggregator::TestGroupsWindowsPathsIgnoringCase()
{
    // VS Code alternates the drive letter case for the same folder.
    LogSnapshot snapshot;
    snapshot.m_vecRecords = {
        MakeRecord("k1", "2026-10-05T01:00:00Z", "s1", "c:\\Users\\me\\Proj", "claude-opus-5-5", 100000),
        MakeRecord("k2", "2026-10-05T02:00:00Z", "s2", "C:\\Users\\me\\Proj", "claude-opus-5-5", 200000),
    };
    const Breakdown breakdown = TokenAggregator::Aggregate(snapshot, m_priceTable, MakeQuery(EBreakdownDimension::PROJECT));
    QCOMPARE(breakdown.m_vecRows.size(), 1);
    QCOMPARE(breakdown.m_vecRows[0].m_strLabel, QStringLiteral("Proj"));
    QCOMPARE(breakdown.m_vecRows[0].m_strDetail, QStringLiteral("C:\\Users\\me\\Proj"));
    QCOMPARE(breakdown.m_vecRows[0].m_llOutput, 300000LL);
    QVERIFY(qFuzzyCompare(breakdown.m_vecRows[0].m_dCostUsd, 6.0));
}

void TestTokenAggregator::TestGroupsMixedSeparatorsAndTrailingSeparator()
{
    LogSnapshot snapshot;
    snapshot.m_vecRecords = {
        MakeRecord("k1", "2026-10-05T01:00:00Z", "s1", "C:\\Users\\me\\Proj\\", "claude-opus-5-5", 100000),
        MakeRecord("k2", "2026-10-05T02:00:00Z", "s1", "C:/Users/me/Proj", "claude-opus-5-5", 100000),
        MakeRecord("k3", "2026-10-05T03:00:00Z", "s1", "C:\\Users\\me\\Proj\\", "claude-opus-5-5", 100000),
    };
    const Breakdown breakdown = TokenAggregator::Aggregate(snapshot, m_priceTable, MakeQuery(EBreakdownDimension::PROJECT));
    QCOMPARE(breakdown.m_vecRows.size(), 1);
    QCOMPARE(breakdown.m_vecRows[0].m_strLabel, QStringLiteral("Proj"));
    QCOMPARE(breakdown.m_vecRows[0].m_llOutput, 300000LL);
}

void TestTokenAggregator::TestLabelUsesMostRecentSpelling()
{
    LogSnapshot snapshot;
    snapshot.m_vecRecords = {
        MakeRecord("k1", "2026-10-05T03:00:00Z", "s1", "c:\\users\\me\\proj", "claude-opus-5-5", 100000),
        MakeRecord("k2", "2026-10-05T01:00:00Z", "s1", "C:\\Users\\me\\Proj", "claude-opus-5-5", 100000),
    };
    const Breakdown breakdown = TokenAggregator::Aggregate(snapshot, m_priceTable, MakeQuery(EBreakdownDimension::PROJECT));
    QCOMPARE(breakdown.m_vecRows.size(), 1);
    QCOMPARE(breakdown.m_vecRows[0].m_strLabel, QStringLiteral("proj"));
    QCOMPARE(breakdown.m_vecRows[0].m_strDetail, QStringLiteral("c:\\users\\me\\proj"));
}

void TestTokenAggregator::TestDisambiguatesWindowsSameNames()
{
    LogSnapshot snapshot;
    snapshot.m_vecRecords = {
        MakeRecord("k1", "2026-10-05T01:00:00Z", "s1", "C:\\a\\App", "claude-opus-5-5", 200000),
        MakeRecord("k2", "2026-10-05T01:00:00Z", "s2", "D:\\b\\App", "claude-opus-5-5", 100000),
    };
    const Breakdown breakdown = TokenAggregator::Aggregate(snapshot, m_priceTable, MakeQuery(EBreakdownDimension::PROJECT));
    QCOMPARE(breakdown.m_vecRows.size(), 2);
    QCOMPARE(breakdown.m_vecRows[0].m_strLabel, QStringLiteral("a/App"));
    QCOMPARE(breakdown.m_vecRows[1].m_strLabel, QStringLiteral("b/App"));
}

void TestTokenAggregator::TestDisambiguatesAfterMergingCaseVariants()
{
    // Case variants of one folder are not "two projects with the same name".
    LogSnapshot snapshot;
    snapshot.m_vecRecords = {
        MakeRecord("k1", "2026-10-05T01:00:00Z", "s1", "c:\\x\\App", "claude-opus-5-5", 100000),
        MakeRecord("k2", "2026-10-05T02:00:00Z", "s1", "C:\\x\\App\\", "claude-opus-5-5", 200000),
        MakeRecord("k3", "2026-10-05T01:00:00Z", "s2", "D:\\y\\App", "claude-opus-5-5", 100000),
        MakeRecord("k4", "2026-10-05T01:00:00Z", "s3", "C:\\Users\\me\\Solo", "claude-opus-5-5", 50000),
        MakeRecord("k5", "2026-10-05T02:00:00Z", "s3", "c:\\users\\me\\solo", "claude-opus-5-5", 10000),
    };
    const Breakdown breakdown = TokenAggregator::Aggregate(snapshot, m_priceTable, MakeQuery(EBreakdownDimension::PROJECT));
    QCOMPARE(breakdown.m_vecRows.size(), 3);
    QCOMPARE(breakdown.m_vecRows[0].m_strLabel, QStringLiteral("x/App"));
    QCOMPARE(breakdown.m_vecRows[1].m_strLabel, QStringLiteral("y/App"));
    QCOMPARE(breakdown.m_vecRows[2].m_strLabel, QStringLiteral("solo"));
}

void TestTokenAggregator::TestDisambiguatesNamesDifferingOnlyInCase()
{
    // Windows folder names ignore case, so "App" and "app" in different places clash.
    LogSnapshot snapshot;
    snapshot.m_vecRecords = {
        MakeRecord("k1", "2026-10-05T01:00:00Z", "s1", "C:\\a\\App", "claude-opus-5-5", 200000),
        MakeRecord("k2", "2026-10-05T01:00:00Z", "s2", "D:\\b\\app", "claude-opus-5-5", 100000),
    };
    const Breakdown breakdown = TokenAggregator::Aggregate(snapshot, m_priceTable, MakeQuery(EBreakdownDimension::PROJECT));
    QCOMPARE(breakdown.m_vecRows.size(), 2);
    QCOMPARE(breakdown.m_vecRows[0].m_strLabel, QStringLiteral("a/App"));
    QCOMPARE(breakdown.m_vecRows[1].m_strLabel, QStringLiteral("b/app"));
}

void TestTokenAggregator::TestSessionFallbackUsesWindowsProjectName()
{
    LogSnapshot snapshot;
    snapshot.m_vecRecords = {
        MakeRecord("k1", "2026-10-05T05:27:00Z", "s1", "c:\\Users\\me\\Proj\\", "claude-opus-5-5", 100000),
        MakeRecord("k2", "2026-10-05T06:00:00Z", "s2", "C:\\Users\\me\\Proj", "claude-opus-5-5", 100000),
    };
    const Breakdown breakdown = TokenAggregator::Aggregate(snapshot, m_priceTable, MakeQuery(EBreakdownDimension::SESSION));
    QCOMPARE(breakdown.m_vecRows.size(), 2);
    QCOMPARE(breakdown.m_vecRows[0].m_strLabel, QStringLiteral("Proj · 10/5 14:27"));
    QCOMPARE(breakdown.m_vecRows[1].m_strLabel, QStringLiteral("Proj · 10/5 15:00"));
}

QTEST_GUILESS_MAIN(TestTokenAggregator)
#include "TestTokenAggregator.moc"
