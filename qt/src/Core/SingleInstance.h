#pragma once

#include <QLocalServer>
#include <QLockFile>
#include <QObject>
#include <QString>

#include <memory>

class QLocalSocket;

enum class EInstanceCommand
{
    ACTIVATE,
    QUIT,
};

// Keeps one running app per user: a later launch hands its command to the running one and exits.
class SingleInstance : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstance(const QString& serverName, QObject* parent = nullptr);
    SingleInstance(const QString& serverName, const QString& lockDirectory, QObject* parent = nullptr);
    ~SingleInstance();

public:
    static QString BuildServerName();
    static QString GetLockDirectory();
    static QString GetLockFilePath(const QString& serverName);
    static bool SendCommand(const QString& serverName, EInstanceCommand command, int timeoutMs);

public:
    bool Listen();

signals:
    void ActivateRequested();
    void QuitRequested();

private:
    QLockFile::LockError TryLock(const QString& directory);
    void HandleNewConnection();
    void HandleReadyRead(QLocalSocket* socket);

private:
    QString m_strServerName;
    QString m_strLockDirectory;
    std::unique_ptr<QLockFile> m_upLockFile;
    QLocalServer m_server;
};
