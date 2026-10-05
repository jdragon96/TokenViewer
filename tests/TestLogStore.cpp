#include "Analysis/LogStore.h"

#include "TestHelpers.h"

#include <QTemporaryDir>
#include <QtTest>

namespace
{
const QDateTime kNow(QDate(2026, 10, 5), QTime(12, 0), Qt::UTC);
const QByteArray kTitleLine = QByteArrayLiteral(R"({"type":"ai-title","aiTitle":"Title A","sessionId":"sessA"})") + '\n';

bool SetModifiedTime(const QString& filePath, const QDateTime& time)
{
    QFile file(filePath);
    return file.open(QIODevice::ReadWrite) && file.setFileTime(time, QFileDevice::FileModificationTime);
}
}

class TestLogStore : public QObject
{
    Q_OBJECT

private slots:
    void TestReadsMainAndSubagentFiles();
    void TestAppendsOnlyNewLines();
    void TestWaitsForPartialLine();
    void TestDropsDeletedFile();
    void TestRereadsTruncatedFile();
    void TestSkipsFilesOlderThanRetention();
    void TestPrunesOldRecords();
    void TestMissingRootIsEmpty();
    void TestStopsWhenNotAlive();
};

void TestLogStore::TestReadsMainAndSubagentFiles()
{
    QTemporaryDir dirRoot;
    const QString strMain = dirRoot.filePath(QStringLiteral("proj/sessA.jsonl"));
    const QString strAgent = dirRoot.filePath(QStringLiteral("proj/sessA/subagents/agent-1.jsonl"));
    const QByteArray baFirst = MakeUsageLine("msg_1", "sessA", "2026-10-05T10:00:00.000Z");
    // The same response is written twice, as streaming does.
    QVERIFY(AppendText(strMain, baFirst + baFirst + MakeUsageLine("msg_2", "sessA", "2026-10-05T10:01:00.000Z") + kTitleLine));
    QVERIFY(AppendText(strAgent, MakeUsageLine("msg_3", "sessA", "2026-10-05T10:02:00.000Z")));

    LogStore store;
    QVERIFY(store.ScanDirectory(dirRoot.path(), kNow));
    QCOMPARE(store.GetRecordCount(), 3);
    QCOMPARE(store.CreateSnapshot().m_hashSessionTitles.value(QStringLiteral("sessA")), QStringLiteral("Title A"));
    QVERIFY(!store.ScanDirectory(dirRoot.path(), kNow));
}

void TestLogStore::TestAppendsOnlyNewLines()
{
    QTemporaryDir dirRoot;
    const QString strMain = dirRoot.filePath(QStringLiteral("proj/sessA.jsonl"));
    QVERIFY(AppendText(strMain, MakeUsageLine("msg_1", "sessA", "2026-10-05T10:00:00.000Z")));
    LogStore store;
    QVERIFY(store.ScanDirectory(dirRoot.path(), kNow));

    QVERIFY(AppendText(strMain, MakeUsageLine("msg_4", "sessA", "2026-10-05T10:04:00.000Z")));
    QVERIFY(store.ScanDirectory(dirRoot.path(), kNow));
    QCOMPARE(store.GetRecordCount(), 2);
}

void TestLogStore::TestWaitsForPartialLine()
{
    QTemporaryDir dirRoot;
    const QString strMain = dirRoot.filePath(QStringLiteral("proj/sessA.jsonl"));
    QVERIFY(AppendText(strMain, MakeUsageLine("msg_1", "sessA", "2026-10-05T10:00:00.000Z")));
    LogStore store;
    QVERIFY(store.ScanDirectory(dirRoot.path(), kNow));

    const QByteArray baLine = MakeUsageLine("msg_5", "sessA", "2026-10-05T10:05:00.000Z");
    QVERIFY(AppendText(strMain, baLine.left(40)));
    QVERIFY(!store.ScanDirectory(dirRoot.path(), kNow));
    QCOMPARE(store.GetRecordCount(), 1);

    QVERIFY(AppendText(strMain, baLine.mid(40)));
    QVERIFY(store.ScanDirectory(dirRoot.path(), kNow));
    QCOMPARE(store.GetRecordCount(), 2);
}

void TestLogStore::TestDropsDeletedFile()
{
    QTemporaryDir dirRoot;
    const QString strMain = dirRoot.filePath(QStringLiteral("proj/sessA.jsonl"));
    QVERIFY(AppendText(strMain, MakeUsageLine("msg_1", "sessA", "2026-10-05T10:00:00.000Z")));
    LogStore store;
    QVERIFY(store.ScanDirectory(dirRoot.path(), kNow));

    QVERIFY(QFile::remove(strMain));
    QVERIFY(store.ScanDirectory(dirRoot.path(), kNow));
    QCOMPARE(store.GetRecordCount(), 0);
}

void TestLogStore::TestRereadsTruncatedFile()
{
    QTemporaryDir dirRoot;
    const QString strMain = dirRoot.filePath(QStringLiteral("proj/sessA.jsonl"));
    QVERIFY(AppendText(strMain, MakeUsageLine("msg_1", "sessA", "2026-10-05T10:00:00.000Z")
        + MakeUsageLine("msg_2", "sessA", "2026-10-05T10:01:00.000Z")));
    LogStore store;
    QVERIFY(store.ScanDirectory(dirRoot.path(), kNow));

    QFile file(strMain);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(MakeUsageLine("msg_9", "sessA", "2026-10-05T10:09:00.000Z"));
    file.close();

    QVERIFY(store.ScanDirectory(dirRoot.path(), kNow));
    const LogSnapshot snapshot = store.CreateSnapshot();
    QCOMPARE(snapshot.m_vecRecords.size(), 1);
    QCOMPARE(snapshot.m_vecRecords.first().m_strKey, QStringLiteral("msg_9|req_msg_9"));
}

void TestLogStore::TestSkipsFilesOlderThanRetention()
{
    QTemporaryDir dirRoot;
    const QString strMain = dirRoot.filePath(QStringLiteral("proj/old.jsonl"));
    QVERIFY(AppendText(strMain, MakeUsageLine("msg_1", "old", "2026-08-20T10:00:00.000Z")));
    QVERIFY(SetModifiedTime(strMain, QDateTime(QDate(2026, 8, 20), QTime(10, 0), Qt::UTC)));

    LogStore store;
    QVERIFY(!store.ScanDirectory(dirRoot.path(), kNow));
    QCOMPARE(store.GetRecordCount(), 0);
}

void TestLogStore::TestPrunesOldRecords()
{
    QTemporaryDir dirRoot;
    QVERIFY(AppendText(dirRoot.filePath(QStringLiteral("proj/sessA.jsonl")), MakeUsageLine("msg_1", "sessA", "2026-08-20T10:00:00.000Z")));
    LogStore store;
    store.ScanDirectory(dirRoot.path(), kNow);
    QCOMPARE(store.GetRecordCount(), 0);
}

void TestLogStore::TestMissingRootIsEmpty()
{
    LogStore store;
    QVERIFY(!store.ScanDirectory(QStringLiteral("/nonexistent/tokenviewer/projects"), kNow));
    QCOMPARE(store.GetRecordCount(), 0);
}

void TestLogStore::TestStopsWhenNotAlive()
{
    QTemporaryDir dirRoot;
    QVERIFY(AppendText(dirRoot.filePath(QStringLiteral("proj/sessA.jsonl")), MakeUsageLine("msg_1", "sessA", "2026-10-05T10:00:00.000Z")));
    LogStore store;
    store.ScanDirectory(dirRoot.path(), kNow, []() { return false; });
    QCOMPARE(store.GetRecordCount(), 0);
}

QTEST_GUILESS_MAIN(TestLogStore)
#include "TestLogStore.moc"
