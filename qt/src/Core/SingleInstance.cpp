#include "Core/SingleInstance.h"

#include <QDir>
#include <QElapsedTimer>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QThread>

#include <algorithm>

namespace
{
constexpr int kRetryIntervalMs = 50;
constexpr int kAckWriteTimeoutMs = 1000;
constexpr qint64 kMaxCommandBytes = 64;
const QByteArray kActivateLine = QByteArrayLiteral("activate");
const QByteArray kQuitLine = QByteArrayLiteral("quit");
const QByteArray kAckLine = QByteArrayLiteral("ok");
const QString kServerNamePrefix = QStringLiteral("TokenViewer-");
const QString kFileNameUnsafeCharacters = QStringLiteral("\\/:*?\"<>|");
const QString kLockFileSuffix = QStringLiteral(".lock");

QByteArray ConvertToLine(EInstanceCommand command)
{
    return command == EInstanceCommand::QUIT ? kQuitLine : kActivateLine;
}

QString BuildLockFilePath(const QString& directory, const QString& serverName)
{
    return QDir(directory).filePath(serverName + kLockFileSuffix);
}
}

SingleInstance::SingleInstance(const QString& serverName, QObject* parent)
    : SingleInstance(serverName, GetLockDirectory(), parent)
{
}

SingleInstance::SingleInstance(const QString& serverName, const QString& lockDirectory, QObject* parent)
    : QObject(parent)
    , m_strServerName(serverName)
    , m_strLockDirectory(lockDirectory)
    , m_upLockFile()
    , m_server()
{
    connect(&m_server, &QLocalServer::newConnection, this, &SingleInstance::HandleNewConnection);
}

SingleInstance::~SingleInstance()
{
    m_server.close();
}

QString SingleInstance::BuildServerName()
{
    // Named pipes are shared by every user on the PC, so the name carries the (domain and) user name.
    QString strUser = qEnvironmentVariable("USERNAME");
    if (strUser.isEmpty())
    {
        strUser = qEnvironmentVariable("USER");
    }
    const QString strDomain = qEnvironmentVariable("USERDOMAIN");
    QString strName = kServerNamePrefix + (strDomain.isEmpty() ? strUser : strDomain + QLatin1Char('-') + strUser);
    // The name is also a file name (the lock) and a pipe name, where '\' is a separator.
    for (const QChar chUnsafe : kFileNameUnsafeCharacters)
    {
        strName.replace(chUnsafe, QLatin1Char('_'));
    }
    return strName;
}

QString SingleInstance::GetLockDirectory()
{
    // A fixed per-user folder: TEMP can differ between two launches, and each would then win its own lock.
    const QString strDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return strDirectory.isEmpty() ? QDir::tempPath() : strDirectory;
}

QString SingleInstance::GetLockFilePath(const QString& serverName)
{
    return BuildLockFilePath(GetLockDirectory(), serverName);
}

bool SingleInstance::SendCommand(const QString& serverName, EInstanceCommand command, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    const auto funcRemainingMs = [&]() { return static_cast<int>(std::max<qint64>(0, timeoutMs - timer.elapsed())); };
    forever
    {
        QLocalSocket socket;
        socket.connectToServer(serverName);
        if (socket.waitForConnected(funcRemainingMs()))
        {
            socket.write(ConvertToLine(command) + '\n');
            if (!socket.waitForBytesWritten(funcRemainingMs()))
            {
                return false;
            }
            // Wait for the reply before closing: the running instance has read the command by then.
            while (!socket.canReadLine())
            {
                if (funcRemainingMs() == 0 || !socket.waitForReadyRead(funcRemainingMs()))
                {
                    return false;
                }
            }
            const bool bAcknowledged = socket.readLine(kMaxCommandBytes).trimmed() == kAckLine;
            socket.disconnectFromServer();
            return bAcknowledged;
        }
        // The running instance takes the lock before it listens, so it may not be listening yet.
        if (timer.elapsed() + kRetryIntervalMs >= timeoutMs)
        {
            return false;
        }
        QThread::msleep(kRetryIntervalMs);
    }
}

bool SingleInstance::Listen()
{
    // Windows lets several servers listen on one pipe name, so the lock file decides who owns it.
    // Only a lock held by another live process means "already running". A folder that cannot hold the lock
    // must not keep the app from starting: try TEMP, and failing that run unlocked.
    QLockFile::LockError eError = TryLock(m_strLockDirectory);
    if (eError != QLockFile::NoError && eError != QLockFile::LockFailedError)
    {
        qWarning("TokenViewer: cannot lock in %s, trying the temp folder", qPrintable(m_strLockDirectory));
        eError = TryLock(QDir::tempPath());
    }
    if (eError == QLockFile::LockFailedError)
    {
        return false;
    }
    if (eError != QLockFile::NoError)
    {
        // A second launch may then run too, which beats never starting.
        qWarning("TokenViewer: cannot take the single-instance lock; running without it");
    }
    // On Unix a socket file left by a crash would block the name; holding the lock means it is ours.
    QLocalServer::removeServer(m_strServerName);
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server.listen(m_strServerName))
    {
        // Still the only instance; later launches just cannot hand over their command.
        qWarning("TokenViewer: cannot listen on %s (%s)", qPrintable(m_strServerName), qPrintable(m_server.errorString()));
    }
    return true;
}

QLockFile::LockError SingleInstance::TryLock(const QString& directory)
{
    // The per-user folder does not exist before the first run.
    QDir().mkpath(directory);
    m_upLockFile = std::make_unique<QLockFile>(BuildLockFilePath(directory, m_strServerName));
    // A live owner keeps the name however long it runs; only a lock whose process is gone counts as stale.
    m_upLockFile->setStaleLockTime(0);
    if (m_upLockFile->tryLock(0))
    {
        return QLockFile::NoError;
    }
    const QLockFile::LockError eError = m_upLockFile->error();
    m_upLockFile.reset();
    return eError;
}

void SingleInstance::HandleNewConnection()
{
    while (QLocalSocket* pSocket = m_server.nextPendingConnection())
    {
        connect(pSocket, &QLocalSocket::disconnected, pSocket, &QObject::deleteLater);
        connect(pSocket, &QLocalSocket::readyRead, this, [this, pSocket]() { HandleReadyRead(pSocket); });
        if (pSocket->bytesAvailable() > 0)
        {
            HandleReadyRead(pSocket);
        }
    }
}

void SingleInstance::HandleReadyRead(QLocalSocket* socket)
{
    if (!socket->canReadLine())
    {
        if (socket->bytesAvailable() > kMaxCommandBytes)
        {
            socket->abort();
        }
        return;
    }
    const QByteArray baLine = socket->readLine(kMaxCommandBytes).trimmed();
    const bool bActivate = baLine == kActivateLine;
    const bool bQuit = baLine == kQuitLine;
    if (!bActivate && !bQuit)
    {
        socket->abort();
        return;
    }
    // The sender closes its end after reading this; closing ours first could drop the reply.
    socket->write(kAckLine + '\n');
    socket->waitForBytesWritten(kAckWriteTimeoutMs);
    if (bActivate)
    {
        emit ActivateRequested();
    }
    else
    {
        emit QuitRequested();
    }
}
