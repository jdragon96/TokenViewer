#include "UI/Formatters.h"

#include <QtTest>

#include <ctime>

namespace
{
QDateTime MakeUtc(int month, int day, int hour, int minute, int second = 0)
{
    return QDateTime(QDate(2026, month, day), QTime(hour, minute, second), Qt::UTC);
}
}

class TestFormatters : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void TestFormatsUsd();
    void TestFormatsTokens();
    void TestFormatsResetCountdown();
    void TestFormatsResetDay();
    void TestFormatsAge();
    void TestFormatsPercent();
};

void TestFormatters::initTestCase()
{
    qputenv("TZ", "Asia/Seoul");
    tzset();
}

void TestFormatters::TestFormatsUsd()
{
    QCOMPARE(Formatters::FormatUsd(12.4, false), QStringLiteral("$12.40"));
    QCOMPARE(Formatters::FormatUsd(0.004, false), QStringLiteral("<$0.01"));
    QCOMPARE(Formatters::FormatUsd(1234.5, true), QStringLiteral("≈$1,234.50"));
    QCOMPARE(Formatters::FormatUsd(0.0, false), QStringLiteral("$0.00"));
}

void TestFormatters::TestFormatsTokens()
{
    QCOMPARE(Formatters::FormatTokens(512), QStringLiteral("512"));
    QCOMPARE(Formatters::FormatTokens(48200), QStringLiteral("48.2K"));
    QCOMPARE(Formatters::FormatTokens(212000), QStringLiteral("212K"));
    QCOMPARE(Formatters::FormatTokens(1240000), QStringLiteral("1.24M"));
    QCOMPARE(Formatters::FormatTokens(18400000), QStringLiteral("18.4M"));
}

void TestFormatters::TestFormatsResetCountdown()
{
    const QDateTime dtNow = MakeUtc(10, 5, 2, 27);
    QCOMPARE(Formatters::FormatResetCountdown(MakeUtc(10, 5, 4, 40), dtNow), QStringLiteral("2:13 후 리셋"));
    QCOMPARE(Formatters::FormatResetCountdown(MakeUtc(10, 5, 3, 12), dtNow), QStringLiteral("45분 후 리셋"));
    QCOMPARE(Formatters::FormatResetCountdown(MakeUtc(10, 5, 2, 27, 30), dtNow), QStringLiteral("곧 리셋"));
    QCOMPARE(Formatters::FormatResetCountdown(QDateTime(), dtNow), QStringLiteral("리셋 시각 없음"));
}

void TestFormatters::TestFormatsResetDay()
{
    const QDateTime dtNow = MakeUtc(10, 5, 2, 27);
    // 10/8 00:00 UTC is Thursday 09:00 in Seoul.
    QCOMPARE(Formatters::FormatResetDay(MakeUtc(10, 8, 0, 0), dtNow), QStringLiteral("목 9:00 리셋"));
    QCOMPARE(Formatters::FormatResetDay(MakeUtc(10, 5, 4, 40), dtNow), QStringLiteral("2:13 후 리셋"));
}

void TestFormatters::TestFormatsAge()
{
    const QDateTime dtNow = MakeUtc(10, 5, 12, 0);
    QCOMPARE(Formatters::FormatAge(dtNow.addSecs(-30), dtNow), QStringLiteral("방금"));
    QCOMPARE(Formatters::FormatAge(dtNow.addSecs(-18 * 60), dtNow), QStringLiteral("18분 전"));
    QCOMPARE(Formatters::FormatAge(dtNow.addSecs(-125 * 60), dtNow), QStringLiteral("2시간 전"));
}

void TestFormatters::TestFormatsPercent()
{
    QCOMPARE(Formatters::FormatPercent(42.4), QStringLiteral("42%"));
    QCOMPARE(Formatters::FormatPercent(130.0), QStringLiteral("100%"));
}

QTEST_GUILESS_MAIN(TestFormatters)
#include "TestFormatters.moc"
