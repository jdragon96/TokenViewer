#include "Thread/LogScanThread.h"

#include "Analysis/LogStore.h"

#include <QDateTime>
#include <QMetaObject>
#include <QThread>

#include <atomic>
#include <functional>

namespace
{
constexpr int kStopWaitMs = 4000;
constexpr int kTickMs = 100;
}

////////////////////////////////////////////////////////////////////////////////
// Worker
////////////////////////////////////////////////////////////////////////////////
class LogScanThreadWorker::LogScanThreadWorkerImpl
{
public:
    std::atomic<bool> m_bDoRunThread{ false };
    std::atomic<bool> m_bRescanRequested{ false };
    QString m_strRootPath;
    int m_iScanIntervalMs = 0;
};

LogScanThreadWorker::LogScanThreadWorker(QObject* parent)
    : QObject(parent)
    , m_pimpl(std::make_unique<LogScanThreadWorkerImpl>())
{
}

LogScanThreadWorker::~LogScanThreadWorker() = default;

void LogScanThreadWorker::SetContext(const QString& rootPath, int scanIntervalMs)
{
    m_pimpl->m_strRootPath = rootPath;
    m_pimpl->m_iScanIntervalMs = scanIntervalMs;
}

void LogScanThreadWorker::Stop()
{
    m_pimpl->m_bDoRunThread = false;
}

void LogScanThreadWorker::RequestRescan()
{
    m_pimpl->m_bRescanRequested = true;
}

void LogScanThreadWorker::Run()
{
    m_pimpl->m_bDoRunThread = true;
}

bool LogScanThreadWorker::IsThreadAlive() const
{
    return m_pimpl->m_bDoRunThread;
}

void LogScanThreadWorker::DoWork()
{
    Run();
    LogStore store;
    const std::function<bool()> funcIsAlive = [this]() { return IsThreadAlive(); };
    while (IsThreadAlive())
    {
        m_pimpl->m_bRescanRequested = false;
        if (store.ScanDirectory(m_pimpl->m_strRootPath, QDateTime::currentDateTimeUtc(), funcIsAlive))
        {
            emit SnapshotReady(store.CreateSnapshot());
        }
        for (int iWaitedMs = 0; iWaitedMs < m_pimpl->m_iScanIntervalMs && IsThreadAlive() && !m_pimpl->m_bRescanRequested; iWaitedMs += kTickMs)
        {
            QThread::msleep(kTickMs);
        }
    }
    emit WorkFinished(true);
}

////////////////////////////////////////////////////////////////////////////////
// Thread
////////////////////////////////////////////////////////////////////////////////
class LogScanThread::LogScanThreadImpl
{
public:
    QThread m_workerThread;
    std::unique_ptr<LogScanThreadWorker> m_upWorker;
    QString m_strRootPath;
    int m_iScanIntervalMs = 0;

    ~LogScanThreadImpl()
    {
        StopWorker();
    }

    void ConnectWorker(LogScanThread* owner)
    {
        QObject::connect(m_upWorker.get(), &LogScanThreadWorker::SnapshotReady, owner, &LogScanThread::SnapshotReady);
        QObject::connect(m_upWorker.get(), &LogScanThreadWorker::WorkFinished, owner, &LogScanThread::Finished);
    }

    void StopWorker()
    {
        if (m_upWorker)
        {
            m_upWorker->Stop();
        }
        if (m_workerThread.isRunning())
        {
            m_workerThread.quit();
            if (!m_workerThread.wait(kStopWaitMs))
            {
                m_workerThread.terminate();
                m_workerThread.wait();
            }
        }
        m_upWorker.reset();
    }
};

LogScanThread::LogScanThread(const QString& rootPath, int scanIntervalMs)
    : QObject(nullptr)
    , m_pimpl(std::make_unique<LogScanThreadImpl>())
{
    qRegisterMetaType<LogSnapshot>("LogSnapshot");
    m_pimpl->m_strRootPath = rootPath;
    m_pimpl->m_iScanIntervalMs = scanIntervalMs;
}

LogScanThread::~LogScanThread() = default;

void LogScanThread::Start()
{
    if (!m_pimpl->m_upWorker)
    {
        m_pimpl->m_upWorker = std::make_unique<LogScanThreadWorker>();
        m_pimpl->m_upWorker->SetContext(m_pimpl->m_strRootPath, m_pimpl->m_iScanIntervalMs);
        m_pimpl->ConnectWorker(this);
        m_pimpl->m_upWorker->moveToThread(&m_pimpl->m_workerThread);
    }
    if (!m_pimpl->m_workerThread.isRunning())
    {
        m_pimpl->m_workerThread.start();
    }
    QMetaObject::invokeMethod(m_pimpl->m_upWorker.get(), "DoWork", Qt::QueuedConnection);
}

void LogScanThread::Stop()
{
    m_pimpl->StopWorker();
}

void LogScanThread::RequestRescan()
{
    if (m_pimpl->m_upWorker)
    {
        m_pimpl->m_upWorker->RequestRescan();
    }
}
