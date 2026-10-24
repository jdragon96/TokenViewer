#include "Thread/LogScanThread.h"

#include "TestHelpers.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace
{
constexpr int kLongIntervalMs = 60000;
constexpr int kSignalWaitMs = 5000;

QString CurrentTimestamp()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}
}

class TestLogScanThread : public QObject
{
    Q_OBJECT

private slots:
    void TestEmitsSnapshotAndRescansOnRequest();
};

void TestLogScanThread::TestEmitsSnapshotAndRescansOnRequest()
{
    QTemporaryDir dirRoot;
    const QString strFile = dirRoot.filePath(QStringLiteral("proj/sessA.jsonl"));
    QVERIFY(AppendText(strFile, MakeUsageLine("msg_1", "sessA", CurrentTimestamp())));

    LogScanThread thread(dirRoot.path(), kLongIntervalMs);
    QSignalSpy spy(&thread, &LogScanThread::SnapshotReady);
    thread.Start();
    QVERIFY(spy.wait(kSignalWaitMs));
    QCOMPARE(spy.last().at(0).value<LogSnapshot>().m_vecRecords.size(), 1);

    QVERIFY(AppendText(strFile, MakeUsageLine("msg_2", "sessA", CurrentTimestamp())));
    thread.RequestRescan();
    QVERIFY(spy.wait(kSignalWaitMs));
    QCOMPARE(spy.last().at(0).value<LogSnapshot>().m_vecRecords.size(), 2);

    thread.Stop();
}

QTEST_GUILESS_MAIN(TestLogScanThread)
#include "TestLogScanThread.moc"
