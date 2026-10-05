#include "Analysis/PriceTable.h"

#include "TestHelpers.h"

#include <QtTest>

namespace
{
TokenRecord MakeRecord(const QString& model, bool fast)
{
    TokenRecord record;
    record.m_strModel = model;
    record.m_bFast = fast;
    record.m_llInput = 1000000;
    record.m_llOutput = 100000;
    record.m_llCacheWrite5m = 200000;
    record.m_llCacheWrite1h = 50000;
    record.m_llCacheRead = 2000000;
    return record;
}
}

class TestPriceTable : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void TestLoadsBundledPrices();
    void TestFindsExactModel();
    void TestFindsDatedModel();
    void TestFallsBackToFamily();
    void TestUnknownFamilyIsUnpriced();
    void TestCalculatesCost();
    void TestDoublesFastOpus();
    void TestIgnoresFastWithoutMultiplier();
    void TestRejectsBrokenJson();

private:
    PriceTable m_priceTable;
};

void TestPriceTable::init()
{
    QVERIFY(m_priceTable.LoadFromJson(ReadSourceFile(QStringLiteral("resources/prices.json"))));
}

void TestPriceTable::TestLoadsBundledPrices()
{
    QCOMPARE(m_priceTable.GetModelCount(), 7);
    QCOMPARE(m_priceTable.GetAsOf(), QStringLiteral("2026-09-25"));
}

void TestPriceTable::TestFindsExactModel()
{
    const PriceQuote quote = m_priceTable.FindPrice(QStringLiteral("claude-opus-5-5"));
    QCOMPARE(quote.m_eMatch, EPriceMatch::EXACT);
    QCOMPARE(quote.m_price.m_dInput, 4.0);
    QCOMPARE(quote.m_price.m_dCacheRead, 0.2);
}

void TestPriceTable::TestFindsDatedModel()
{
    const PriceQuote quote = m_priceTable.FindPrice(QStringLiteral("claude-haiku-4-5-20251001"));
    QCOMPARE(quote.m_eMatch, EPriceMatch::EXACT);
    QCOMPARE(quote.m_price.m_dOutput, 5.0);
}

void TestPriceTable::TestFallsBackToFamily()
{
    const PriceQuote quoteNew = m_priceTable.FindPrice(QStringLiteral("claude-opus-6"));
    QCOMPARE(quoteNew.m_eMatch, EPriceMatch::FAMILY);
    QCOMPARE(quoteNew.m_price.m_strId, QStringLiteral("claude-opus-5-5"));

    // "claude-opus-5" is a prefix of this id, but the rest is not a date suffix.
    const PriceQuote quotePoint = m_priceTable.FindPrice(QStringLiteral("claude-opus-5-7"));
    QCOMPARE(quotePoint.m_eMatch, EPriceMatch::FAMILY);
}

void TestPriceTable::TestUnknownFamilyIsUnpriced()
{
    QCOMPARE(m_priceTable.FindPrice(QStringLiteral("<synthetic>")).m_eMatch, EPriceMatch::NONE);
    const CostResult cost = m_priceTable.CalculateCost(MakeRecord(QStringLiteral("gpt-oss"), false));
    QVERIFY(cost.m_bUnpriced);
    QCOMPARE(cost.m_dUsd, 0.0);
}

void TestPriceTable::TestCalculatesCost()
{
    // Opus 5.5: 4 + 2 + 1 + 0.4 + 0.4 dollars for the token mix in MakeRecord.
    const CostResult cost = m_priceTable.CalculateCost(MakeRecord(QStringLiteral("claude-opus-5-5"), false));
    QVERIFY(qFuzzyCompare(cost.m_dUsd, 7.8));
    QVERIFY(!cost.m_bApproximate);
    QVERIFY(!cost.m_bUnpriced);

    const CostResult costFamily = m_priceTable.CalculateCost(MakeRecord(QStringLiteral("claude-opus-6"), false));
    QVERIFY(costFamily.m_bApproximate);
}

void TestPriceTable::TestDoublesFastOpus()
{
    const CostResult cost = m_priceTable.CalculateCost(MakeRecord(QStringLiteral("claude-opus-5-5"), true));
    QVERIFY(qFuzzyCompare(cost.m_dUsd, 15.6));
}

void TestPriceTable::TestIgnoresFastWithoutMultiplier()
{
    // Sonnet 5.5: 2 + 1 + 0.5 + 0.2 + 0.4 dollars, fast or not.
    const CostResult cost = m_priceTable.CalculateCost(MakeRecord(QStringLiteral("claude-sonnet-5-5"), true));
    QVERIFY(qFuzzyCompare(cost.m_dUsd, 4.1));
}

void TestPriceTable::TestRejectsBrokenJson()
{
    PriceTable table;
    QVERIFY(!table.LoadFromJson(QByteArrayLiteral("{")));
    QVERIFY(!table.LoadFromJson(QByteArrayLiteral("{\"models\":[]}")));
    QCOMPARE(table.GetModelCount(), 0);
}

QTEST_GUILESS_MAIN(TestPriceTable)
#include "TestPriceTable.moc"
