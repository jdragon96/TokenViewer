#pragma once

#include "Model/UsageLimits.h"

#include <QObject>

#include <functional>
#include <memory>

using FetchFunction = std::function<FetchResult()>;

class UsageFetchThreadWorker : public QObject
{
    Q_OBJECT

public:
    explicit UsageFetchThreadWorker(QObject* parent = nullptr);
    ~UsageFetchThreadWorker();

public:
    void SetContext(const FetchFunction& fetch, int intervalMs);
    void SetIntervalMs(int intervalMs);
    void RequestRefresh();
    void Stop();

public slots:
    void DoWork();

signals:
    void FetchCompleted(const FetchResult& result);
    void WorkFinished(bool success);

private:
    void Run();
    bool IsThreadAlive() const;

private:
    class UsageFetchThreadWorkerImpl;
    std::unique_ptr<UsageFetchThreadWorkerImpl> m_pimpl;
};

class UsageFetchThread : public QObject
{
    Q_OBJECT

public:
    UsageFetchThread(const FetchFunction& fetch, int intervalMs);
    ~UsageFetchThread();

public:
    void Start();
    void Stop();
    void RequestRefresh();
    void SetIntervalMs(int intervalMs);

signals:
    void FetchCompleted(const FetchResult& result);
    void Finished(bool success);

private:
    class UsageFetchThreadImpl;
    std::unique_ptr<UsageFetchThreadImpl> m_pimpl;
};
