#include "Core/ClockJumpDetector.h"

#include <QtTest>

namespace
{
constexpr qint64 kThresholdMs = 30 * 1000;
const QDateTime kStart(QDate(2026, 10, 5), QTime(12, 0), Qt::UTC);
}

class TestClockJumpDetector : public QObject
{
    Q_OBJECT

private slots:
    void TestFirstTickIsNotAJump();
    void TestRegularTicksAreNotAJump();
    void TestLongGapIsAJump();
    void TestTickAfterJumpComparesAgainstJumpTick();
};

void TestClockJumpDetector::TestFirstTickIsNotAJump()
{
    ClockJumpDetector detector(kThresholdMs);
    QVERIFY(!detector.CheckTick(kStart));
}

void TestClockJumpDetector::TestRegularTicksAreNotAJump()
{
    ClockJumpDetector detector(kThresholdMs);
    QVERIFY(!detector.CheckTick(kStart));
    QVERIFY(!detector.CheckTick(kStart.addSecs(5)));
    QVERIFY(!detector.CheckTick(kStart.addSecs(10)));
}

void TestClockJumpDetector::TestLongGapIsAJump()
{
    ClockJumpDetector detector(kThresholdMs);
    QVERIFY(!detector.CheckTick(kStart));
    QVERIFY(detector.CheckTick(kStart.addSecs(10 * 60)));
}

void TestClockJumpDetector::TestTickAfterJumpComparesAgainstJumpTick()
{
    ClockJumpDetector detector(kThresholdMs);
    QVERIFY(!detector.CheckTick(kStart));
    QVERIFY(detector.CheckTick(kStart.addSecs(10 * 60)));
    QVERIFY(!detector.CheckTick(kStart.addSecs(10 * 60 + 5)));
}

QTEST_GUILESS_MAIN(TestClockJumpDetector)
#include "TestClockJumpDetector.moc"
