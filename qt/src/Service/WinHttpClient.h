#pragma once

#include <QByteArray>
#include <QPair>
#include <QString>
#include <QUrl>
#include <QVector>

using HttpHeaders = QVector<QPair<QByteArray, QByteArray>>;

struct WinHttpResponse
{
    int m_iHttpStatus = 0;
    QByteArray m_baBody;
    bool m_bNetworkError = false;
    bool m_bTimedOut = false;
    QString m_strError;
    unsigned long m_ulErrorCode = 0;
};

// Blocking HTTP(S) GET through the Windows WinHTTP stack: TLS and proxy come from the OS,
// so no OpenSSL DLLs are needed.
class WinHttpClient
{
public:
    static WinHttpResponse Get(const QUrl& url, const HttpHeaders& headers, int timeoutMs);
    static QString DescribeError(unsigned long errorCode);
};
