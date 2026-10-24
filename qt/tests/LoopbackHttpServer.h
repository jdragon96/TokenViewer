#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QSemaphore>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QUrl>

#include <memory>

// Serves one HTTP connection on its own thread, because the client under test blocks the test thread.
class LoopbackHttpServer
{
public:
    enum class EMode
    {
        RESPOND,
        HOLD,  // send the (possibly empty or partial) response, then keep the connection open
    };

public:
    explicit LoopbackHttpServer(const QByteArray& response, EMode mode = EMode::RESPOND)
        : m_upThread(QThread::create([this, response, mode]() { Serve(response, mode); }))
        , m_semReady()
        , m_semRelease()
        , m_usPort(0)
        , m_baRequest()
    {
        m_upThread->start();
        m_semReady.acquire();
    }

    ~LoopbackHttpServer()
    {
        Finish();
    }

public:
    // Ends the server thread and returns the raw request head it read (empty if nobody connected).
    QByteArray TakeRequest()
    {
        Finish();
        return m_baRequest;
    }

    static QByteArray MakeResponse(int status, const QByteArray& reason, const QByteArray& body)
    {
        return "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason + "\r\n"
            + "Content-Type: application/json\r\n"
            + "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
            + "Connection: close\r\n\r\n" + body;
    }

    static QByteArray MakeRedirectResponse(const QUrl& location)
    {
        return "HTTP/1.1 302 Found\r\nLocation: " + location.toEncoded() + "\r\n"
            + "Content-Length: 0\r\nConnection: close\r\n\r\n";
    }

public:
    quint16 GetPort() const
    {
        return m_usPort;
    }

    QUrl GetUrl(const QString& pathAndQuery) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_usPort).arg(pathAndQuery));
    }

private:
    void Finish()
    {
        m_semRelease.release();
        m_upThread->wait();
    }

    void Serve(const QByteArray& response, EMode mode)
    {
        QTcpServer server;
        if (server.listen(QHostAddress::LocalHost, 0))
        {
            m_usPort = server.serverPort();
        }
        m_semReady.release();
        if (m_usPort == 0)
        {
            return;
        }
        // Short polls so a server that is never contacted stops as soon as the test finishes it.
        QElapsedTimer timerAccept;
        timerAccept.start();
        while (!server.waitForNewConnection(kAcceptPollMs))
        {
            if (m_semRelease.available() > 0 || timerAccept.elapsed() >= kWaitMs)
            {
                return;
            }
        }
        std::unique_ptr<QTcpSocket> upSocket(server.nextPendingConnection());
        QByteArray baRequest;
        while (!baRequest.contains("\r\n\r\n") && upSocket->waitForReadyRead(kWaitMs))
        {
            baRequest += upSocket->readAll();
        }
        m_baRequest = baRequest;
        upSocket->write(response);
        while (upSocket->bytesToWrite() > 0 && upSocket->waitForBytesWritten(kWaitMs))
        {
        }
        if (mode == EMode::HOLD)
        {
            m_semRelease.tryAcquire(1, kWaitMs);
            return;
        }
        upSocket->disconnectFromHost();
        if (upSocket->state() != QAbstractSocket::UnconnectedState)
        {
            upSocket->waitForDisconnected(kWaitMs);
        }
    }

private:
    static constexpr int kWaitMs = 5000;
    static constexpr int kAcceptPollMs = 20;

    std::unique_ptr<QThread> m_upThread;
    QSemaphore m_semReady;
    QSemaphore m_semRelease;
    quint16 m_usPort;
    QByteArray m_baRequest;
};
