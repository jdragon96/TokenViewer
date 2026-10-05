#include "Thread/UsageFetchThread.h"

#include "Service/RefreshPolicy.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QMetaObject>
#include <QThread>

#include <atomic>

namespace
{
constexpr int kStopWaitMs = 4000;
constexpr int kTickMs = 100;
}

////////////////////////////////////////////////////////////////////////////////
// Worker
////////////////////////////////////////////////////////////////////////////////
class UsageFetchThreadWorker::UsageFetchThreadWorkerImpl
{
public:
    std::atomic<bool> m_bDoRunThread{ false };
    std::atomic<bool> m_bRefreshRequested{ false };
    std::atomic<int> m_iIntervalMs{ 0 };
    FetchFunction m_funcFetch;
};

UsageFetchThreadWorker::UsageFetchThreadWorker(QObject* parent)
    : QObject(parent)
    , m_pimpl(std::make_unique<UsageFetchThreadWorkerImpl>())
{
}

UsageFetchThreadWorker::~UsageFetchThreadWorker() = default;

void UsageFetchThreadWorker::SetContext(const FetchFunction& fetch, int intervalMs)
{
    m_pimpl->m_funcFetch = fetch;
    m_pimpl->m_iIntervalMs = intervalMs;
}

void UsageFetchThreadWorker::SetIntervalMs(int intervalMs)
{
    m_pimpl->m_iIntervalMs = intervalMs;
}

void UsageFetchThreadWorker::RequestRefresh()
{
    m_pimpl->m_bRefreshRequested = true;
}

void UsageFetchThreadWorker::Stop()
{
    m_pimpl->m_bDoRunThread = false;
}

void UsageFetchThreadWorker::Run()
{
    m_pimpl->m_bDoRunThread = true;
}

bool UsageFetchThreadWorker::IsThreadAlive() const
{
    return m_pimpl->m_bDoRunThread;
}

void UsageFetchThreadWorker::DoWork()
{
    Run();
    QElapsedTimer timerSinceFetch;
    bool bFirstFetch = true;
    bool bAutoRetry = true;
    int iBackoffMs = 0;
    EFetchStatus eLastStatus = EFetchStatus::OK;
    while (IsThreadAlive())
    {
        const bool bManual = m_pimpl->m_bRefreshRequested.exchange(false);
        // Outside of a rate-limit backoff, always use the latest interval from the settings.
        const int iWaitMs = eLastStatus == EFetchStatus::RATE_LIMITED ? iBackoffMs : m_pimpl->m_iIntervalMs.load();
        const bool bDue = bAutoRetry && (bFirstFetch || timerSinceFetch.elapsed() >= iWaitMs);
        if (bManual || bDue)
        {
            const FetchResult result = m_pimpl->m_funcFetch();
            emit FetchCompleted(result);
            const RefreshDecision decision = RefreshPolicy::DecideNext(result.m_eStatus, m_pimpl->m_iIntervalMs.load(), iBackoffMs);
            iBackoffMs = decision.m_iDelayMs;
            bAutoRetry = decision.m_bAutoRetry;
            eLastStatus = result.m_eStatus;
            bFirstFetch = false;
            timerSinceFetch.restart();
        }
        // QNetworkAccessManager schedules deleteLater() on this thread, whose event loop DoWork blocks; flush them here.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QThread::msleep(kTickMs);
    }
    emit WorkFinished(true);
}

////////////////////////////////////////////////////////////////////////////////
// Thread
////////////////////////////////////////////////////////////////////////////////
class UsageFetchThread::UsageFetchThreadImpl
{
public:
    QThread m_workerThread;
    std::unique_ptr<UsageFetchThreadWorker> m_upWorker;
    FetchFunction m_funcFetch;
    int m_iIntervalMs = 0;

    ~UsageFetchThreadImpl()
    {
        StopWorker();
    }

    void ConnectWorker(UsageFetchThread* owner)
    {
        QObject::connect(m_upWorker.get(), &UsageFetchThreadWorker::FetchCompleted, owner, &UsageFetchThread::FetchCompleted);
        QObject::connect(m_upWorker.get(), &UsageFetchThreadWorker::WorkFinished, owner, &UsageFetchThread::Finished);
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

UsageFetchThread::UsageFetchThread(const FetchFunction& fetch, int intervalMs)
    : QObject(nullptr)
    , m_pimpl(std::make_unique<UsageFetchThreadImpl>())
{
    qRegisterMetaType<FetchResult>("FetchResult");
    m_pimpl->m_funcFetch = fetch;
    m_pimpl->m_iIntervalMs = intervalMs;
}

UsageFetchThread::~UsageFetchThread() = default;

void UsageFetchThread::Start()
{
    if (!m_pimpl->m_upWorker)
    {
        m_pimpl->m_upWorker = std::make_unique<UsageFetchThreadWorker>();
        m_pimpl->m_upWorker->SetContext(m_pimpl->m_funcFetch, m_pimpl->m_iIntervalMs);
        m_pimpl->ConnectWorker(this);
        m_pimpl->m_upWorker->moveToThread(&m_pimpl->m_workerThread);
    }
    if (!m_pimpl->m_workerThread.isRunning())
    {
        m_pimpl->m_workerThread.start();
    }
    QMetaObject::invokeMethod(m_pimpl->m_upWorker.get(), "DoWork", Qt::QueuedConnection);
}

void UsageFetchThread::Stop()
{
    m_pimpl->StopWorker();
}

void UsageFetchThread::RequestRefresh()
{
    if (m_pimpl->m_upWorker)
    {
        m_pimpl->m_upWorker->RequestRefresh();
    }
}

void UsageFetchThread::SetIntervalMs(int intervalMs)
{
    m_pimpl->m_iIntervalMs = intervalMs;
    if (m_pimpl->m_upWorker)
    {
        m_pimpl->m_upWorker->SetIntervalMs(intervalMs);
    }
}
