#include "Core/SingleInstance.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLocalSocket>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <QtTest>

#include <functional>
#include <memory>

namespace
{
constexpr int kSendTimeoutMs = 3000;
constexpr int kShortTimeoutMs = 200;
constexpr int kThreadPollMs = 10;
constexpr int kThreadWaitLimitMs = 10 * kSendTimeoutMs;
// Test mode puts app data under qttest/<app>.
constexpr int kTestDataFolderLevels = 2;
const QString kLockFileSuffix = QStringLiteral(".lock");

QString MakeUniqueServerName()
{
    // Named pipes are machine-wide; a unique name keeps parallel test runs and a real instance apart.
    return QStringLiteral("TokenViewerTest-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// The sender blocks, so it runs on its own thread while this thread's event loop serves the listener.
void RunOnThread(const std::function<void()>& work)
{
    std::unique_ptr<QThread> upThread(QThread::create(work));
    upThread->start();
    QElapsedTimer timer;
    timer.start();
    while (!upThread->isFinished() && timer.elapsed() < kThreadWaitLimitMs)
    {
        QTest::qWait(kThreadPollMs);
    }
    upThread->wait();
}

bool SendFromThread(const QString& serverName, EInstanceCommand command, int timeoutMs)
{
    bool bSent = false;
    RunOnThread([&]() { bSent = SingleInstance::SendCommand(serverName, command, timeoutMs); });
    return bSent;
}

// Removes a hand-written lock file even when the test stops early.
class LockFileRemover
{
public:
    explicit LockFileRemover(const QString& path)
        : m_strPath(path)
    {
    }

    ~LockFileRemover()
    {
        QFile::remove(m_strPath);
    }

private:
    QString m_strPath;
};

bool WriteEmptyFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly);
}

// Points TEMP and TMP somewhere else for one scope, like a second launch from a different shell.
class TempFolderOverride
{
public:
    explicit TempFolderOverride(const QString& path)
        : m_baTemp(qgetenv("TEMP"))
        , m_baTmp(qgetenv("TMP"))
    {
        qputenv("TEMP", QDir::toNativeSeparators(path).toLocal8Bit());
        qputenv("TMP", QDir::toNativeSeparators(path).toLocal8Bit());
    }

    ~TempFolderOverride()
    {
        qputenv("TEMP", m_baTemp);
        qputenv("TMP", m_baTmp);
    }

private:
    QByteArray m_baTemp;
    QByteArray m_baTmp;
};
}

class TestSingleInstance : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void TestServerNameIncludesUserName();
    void TestLockFileIsInPerUserAppDataFolder();
    void TestDifferentTempFoldersStillAllowOneInstance();
    void TestUnusableLockFolderFallsBackToTemp();
    void TestNoUsableLockFolderStillRuns();
    void TestActivateReachesRunningInstance();
    void TestQuitReachesRunningInstance();
    void TestSendFailsWithoutRunningInstance();
    void TestSecondListenOnTakenNameFails();
    void TestLockOfExitedProcessDoesNotBlock();
    void TestIgnoresUnknownCommand();
};

void TestSingleInstance::initTestCase()
{
    // Keeps the lock files out of the real per-user app data folder.
    QStandardPaths::setTestModeEnabled(true);
}

void TestSingleInstance::cleanupTestCase()
{
    // Each instance removes its lock on exit; drop the then empty test folders (<app>, then qttest) as well.
    QDir dirFolder(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    for (int iLevel = 0; iLevel < kTestDataFolderLevels; ++iLevel)
    {
        const QString strName = dirFolder.dirName();
        if (!dirFolder.cdUp() || !dirFolder.rmdir(strName))
        {
            break;
        }
    }
}

void TestSingleInstance::TestServerNameIncludesUserName()
{
    const QString strUser = qEnvironmentVariable("USERNAME");
    QVERIFY(!strUser.isEmpty());
    const QString strName = SingleInstance::BuildServerName();
    QVERIFY(strName.startsWith(QStringLiteral("TokenViewer")));
    QVERIFY2(strName.contains(strUser), qPrintable(strName));
    QVERIFY(!strName.contains(QLatin1Char('\\')));
}

void TestSingleInstance::TestLockFileIsInPerUserAppDataFolder()
{
    const QString strName = MakeUniqueServerName();
    const QString strPath = SingleInstance::GetLockFilePath(strName);
    const QString strAppData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QVERIFY(!strAppData.isEmpty());
    QCOMPARE(QFileInfo(strPath).absolutePath(), QFileInfo(strAppData).absoluteFilePath());
    QCOMPARE(QFileInfo(strPath).fileName(), strName + kLockFileSuffix);
}

void TestSingleInstance::TestDifferentTempFoldersStillAllowOneInstance()
{
    // TEMP differs between shells and accounts' sessions; the lock must not follow it.
    QTemporaryDir dirFirstTemp;
    QTemporaryDir dirSecondTemp;
    QVERIFY(dirFirstTemp.isValid());
    QVERIFY(dirSecondTemp.isValid());
    const QString strName = MakeUniqueServerName();

    std::unique_ptr<SingleInstance> upFirst;
    {
        const TempFolderOverride overrideTemp(dirFirstTemp.path());
        QCOMPARE(QDir::cleanPath(QDir::tempPath()), QDir::cleanPath(dirFirstTemp.path()));
        upFirst = std::make_unique<SingleInstance>(strName);
        QVERIFY(upFirst->Listen());
    }
    {
        const TempFolderOverride overrideTemp(dirSecondTemp.path());
        QCOMPARE(QDir::cleanPath(QDir::tempPath()), QDir::cleanPath(dirSecondTemp.path()));
        SingleInstance second(strName);
        QVERIFY(!second.Listen());
    }
}

void TestSingleInstance::TestUnusableLockFolderFallsBackToTemp()
{
    // A file where the lock folder should be: the folder cannot be created, but nothing else is running.
    QTemporaryDir dirWork;
    QVERIFY(dirWork.isValid());
    const QString strBlocker = dirWork.filePath(QStringLiteral("blocker"));
    QVERIFY(WriteEmptyFile(strBlocker));
    QTemporaryDir dirTemp;
    QVERIFY(dirTemp.isValid());
    const TempFolderOverride overrideTemp(dirTemp.path());
    const QString strName = MakeUniqueServerName();
    const QString strTempLock = QDir(dirTemp.path()).filePath(strName + kLockFileSuffix);

    auto upFirst = std::make_unique<SingleInstance>(strName, strBlocker);
    QVERIFY2(upFirst->Listen(), "a lock folder that cannot be created must not read as \"already running\"");
    QVERIFY(QFile::exists(strTempLock));
    QSignalSpy spyActivate(upFirst.get(), &SingleInstance::ActivateRequested);
    QVERIFY(SendFromThread(strName, EInstanceCommand::ACTIVATE, kSendTimeoutMs));
    QTRY_COMPARE(spyActivate.count(), 1);

    // A second launch in the same state meets the first one's lock in TEMP.
    {
        SingleInstance second(strName, strBlocker);
        QVERIFY(!second.Listen());
    }

    upFirst.reset();
    QVERIFY(!QFile::exists(strTempLock));
}

void TestSingleInstance::TestNoUsableLockFolderStillRuns()
{
    // Neither the lock folder nor TEMP can hold a lock: run as the only instance rather than never start.
    QTemporaryDir dirWork;
    QVERIFY(dirWork.isValid());
    const QString strBlocker = dirWork.filePath(QStringLiteral("blocker"));
    QVERIFY(WriteEmptyFile(strBlocker));
    const TempFolderOverride overrideTemp(strBlocker);
    const QString strName = MakeUniqueServerName();

    SingleInstance instance(strName, strBlocker);
    QVERIFY(instance.Listen());
    QSignalSpy spyQuit(&instance, &SingleInstance::QuitRequested);
    QVERIFY(SendFromThread(strName, EInstanceCommand::QUIT, kSendTimeoutMs));
    QTRY_COMPARE(spyQuit.count(), 1);
}

void TestSingleInstance::TestActivateReachesRunningInstance()
{
    const QString strName = MakeUniqueServerName();
    SingleInstance first(strName);
    QVERIFY(first.Listen());
    QSignalSpy spyActivate(&first, &SingleInstance::ActivateRequested);
    QSignalSpy spyQuit(&first, &SingleInstance::QuitRequested);

    QVERIFY(SendFromThread(strName, EInstanceCommand::ACTIVATE, kSendTimeoutMs));
    QTRY_COMPARE(spyActivate.count(), 1);
    QCOMPARE(spyQuit.count(), 0);

    // A later launch reaches it again.
    QVERIFY(SendFromThread(strName, EInstanceCommand::ACTIVATE, kSendTimeoutMs));
    QTRY_COMPARE(spyActivate.count(), 2);
}

void TestSingleInstance::TestQuitReachesRunningInstance()
{
    const QString strName = MakeUniqueServerName();
    SingleInstance first(strName);
    QVERIFY(first.Listen());
    QSignalSpy spyActivate(&first, &SingleInstance::ActivateRequested);
    QSignalSpy spyQuit(&first, &SingleInstance::QuitRequested);

    QVERIFY(SendFromThread(strName, EInstanceCommand::QUIT, kSendTimeoutMs));
    QTRY_COMPARE(spyQuit.count(), 1);
    QCOMPARE(spyActivate.count(), 0);
}

void TestSingleInstance::TestSendFailsWithoutRunningInstance()
{
    QElapsedTimer timer;
    timer.start();
    QVERIFY(!SingleInstance::SendCommand(MakeUniqueServerName(), EInstanceCommand::ACTIVATE, kShortTimeoutMs));
    QVERIFY(!SingleInstance::SendCommand(MakeUniqueServerName(), EInstanceCommand::QUIT, kShortTimeoutMs));
    // It retries for a while (the other instance may be starting) but gives up at the deadline.
    QVERIFY2(timer.elapsed() < 10 * kShortTimeoutMs, qPrintable(QString::number(timer.elapsed())));
}

void TestSingleInstance::TestSecondListenOnTakenNameFails()
{
    const QString strName = MakeUniqueServerName();
    auto upFirst = std::make_unique<SingleInstance>(strName);
    QVERIFY(upFirst->Listen());
    QSignalSpy spyFirst(upFirst.get(), &SingleInstance::ActivateRequested);

    // Windows lets two pipe servers share a name, so a second launch must still be turned away.
    SingleInstance second(strName);
    QVERIFY(!second.Listen());
    QSignalSpy spySecond(&second, &SingleInstance::ActivateRequested);
    QVERIFY(SendFromThread(strName, EInstanceCommand::ACTIVATE, kSendTimeoutMs));
    QTRY_COMPARE(spyFirst.count(), 1);
    QCOMPARE(spySecond.count(), 0);

    // Once the first one exits the name is free again.
    upFirst.reset();
    SingleInstance third(strName);
    QVERIFY(third.Listen());
}

void TestSingleInstance::TestLockOfExitedProcessDoesNotBlock()
{
    // A crashed instance leaves its lock file behind; a process that is gone must not keep the name.
    QProcess process;
    process.start(QStringLiteral("cmd.exe"), { QStringLiteral("/c"), QStringLiteral("exit") });
    QVERIFY(process.waitForStarted());
    const qint64 llDeadPid = process.processId();
    QVERIFY(process.waitForFinished());
    QVERIFY(llDeadPid > 0);

    const QString strName = MakeUniqueServerName();
    const QString strLockPath = SingleInstance::GetLockFilePath(strName);
    const LockFileRemover removerLock(strLockPath);
    QVERIFY(QDir().mkpath(QFileInfo(strLockPath).absolutePath()));
    // QLockFile's format: pid, application name, host name (empty counts as this host).
    QFile fileLock(strLockPath);
    QVERIFY(fileLock.open(QIODevice::WriteOnly));
    fileLock.write(QStringLiteral("%1\n%2\n\n").arg(llDeadPid).arg(QCoreApplication::applicationName()).toUtf8());
    fileLock.close();

    {
        SingleInstance instance(strName);
        QVERIFY(instance.Listen());
        QSignalSpy spyActivate(&instance, &SingleInstance::ActivateRequested);
        QVERIFY(SendFromThread(strName, EInstanceCommand::ACTIVATE, kSendTimeoutMs));
        QTRY_COMPARE(spyActivate.count(), 1);
    }
    // A normal exit leaves no lock behind.
    QVERIFY(!QFile::exists(strLockPath));
}

void TestSingleInstance::TestIgnoresUnknownCommand()
{
    const QString strName = MakeUniqueServerName();
    SingleInstance first(strName);
    QVERIFY(first.Listen());
    QSignalSpy spyActivate(&first, &SingleInstance::ActivateRequested);
    QSignalSpy spyQuit(&first, &SingleInstance::QuitRequested);

    bool bAcknowledged = true;
    RunOnThread([&]()
    {
        QLocalSocket socket;
        socket.connectToServer(strName);
        if (!socket.waitForConnected(kSendTimeoutMs))
        {
            return;
        }
        socket.write("hello\n");
        socket.waitForBytesWritten(kSendTimeoutMs);
        bAcknowledged = socket.waitForReadyRead(kShortTimeoutMs * 2);
    });
    QVERIFY(!bAcknowledged);
    QTest::qWait(50);
    QCOMPARE(spyActivate.count(), 0);
    QCOMPARE(spyQuit.count(), 0);

    // The listener still works afterwards.
    QVERIFY(SendFromThread(strName, EInstanceCommand::QUIT, kSendTimeoutMs));
    QTRY_COMPARE(spyQuit.count(), 1);
}

QTEST_MAIN(TestSingleInstance)
#include "TestSingleInstance.moc"
