#pragma once

#include "Model/TokenRecord.h"

#include <QObject>
#include <QString>

#include <memory>

class LogScanThreadWorker : public QObject
{
    Q_OBJECT

public:
    explicit LogScanThreadWorker(QObject* parent = nullptr);
    ~LogScanThreadWorker();

public:
    void SetContext(const QString& rootPath, int scanIntervalMs);
    void Stop();
    void RequestRescan();

public slots:
    void DoWork();

signals:
    void SnapshotReady(const LogSnapshot& snapshot);
    void WorkFinished(bool success);

private:
    void Run();
    bool IsThreadAlive() const;

private:
    class LogScanThreadWorkerImpl;
    std::unique_ptr<LogScanThreadWorkerImpl> m_pimpl;
};

class LogScanThread : public QObject
{
    Q_OBJECT

public:
    LogScanThread(const QString& rootPath, int scanIntervalMs);
    ~LogScanThread();

public:
    void Start();
    void Stop();
    void RequestRescan();

signals:
    void SnapshotReady(const LogSnapshot& snapshot);
    void Finished(bool success);

private:
    class LogScanThreadImpl;
    std::unique_ptr<LogScanThreadImpl> m_pimpl;
};
