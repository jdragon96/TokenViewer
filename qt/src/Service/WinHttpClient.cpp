#include "Service/WinHttpClient.h"

#include <QDeadlineTimer>
#include <QtGlobal>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <atomic>

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
// Windows 8.1+. MinGW 8.1's winhttp.h predates it.
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

namespace
{
constexpr DWORD kReadChunkBytes = 64 * 1024;
// Bounded so a stuck cancellation can never hang the fetch thread; the state then outlives us.
constexpr DWORD kCloseWaitMs = 5000;

// Shared with WinHTTP's callback thread. Freed by whichever side lets go last, because
// a cancelled read may still land in m_baBuffer after Get() has given up waiting.
struct AsyncState
{
    HANDLE m_hStepDone = nullptr;
    HANDLE m_hClosed = nullptr;
    DWORD m_dwCompletion = 0;
    DWORD m_dwBytes = 0;
    DWORD m_dwError = 0;
    QByteArray m_baBuffer;
    std::atomic<int> m_iRefs{ 2 };
};

void ReleaseState(AsyncState* state)
{
    if (--state->m_iRefs == 0)
    {
        CloseHandle(state->m_hStepDone);
        CloseHandle(state->m_hClosed);
        delete state;
    }
}

void CALLBACK OnWinHttpStatus(HINTERNET, DWORD_PTR context, DWORD status, LPVOID info, DWORD infoLength)
{
    AsyncState* pState = reinterpret_cast<AsyncState*>(context);
    if (pState == nullptr)
    {
        return;
    }
    switch (status)
    {
    case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
    case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
        pState->m_dwCompletion = status;
        SetEvent(pState->m_hStepDone);
        break;
    case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE:
        pState->m_dwCompletion = status;
        pState->m_dwBytes = *static_cast<DWORD*>(info);
        SetEvent(pState->m_hStepDone);
        break;
    case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
        pState->m_dwCompletion = status;
        pState->m_dwBytes = infoLength;
        SetEvent(pState->m_hStepDone);
        break;
    case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
        pState->m_dwCompletion = status;
        pState->m_dwError = static_cast<WINHTTP_ASYNC_RESULT*>(info)->dwError;
        SetEvent(pState->m_hStepDone);
        break;
    case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
        // Always the last callback for the handle.
        SetEvent(pState->m_hClosed);
        ReleaseState(pState);
        break;
    default:
        break;
    }
}

class ScopedInternet
{
public:
    explicit ScopedInternet(HINTERNET handle = nullptr)
        : m_hInternet(handle)
    {
    }

    ~ScopedInternet()
    {
        if (m_hInternet != nullptr)
        {
            WinHttpCloseHandle(m_hInternet);
        }
    }

    ScopedInternet(const ScopedInternet&) = delete;
    ScopedInternet& operator=(const ScopedInternet&) = delete;

public:
    HINTERNET GetHandle() const
    {
        return m_hInternet;
    }

    void SetHandle(HINTERNET handle)
    {
        m_hInternet = handle;
    }

private:
    HINTERNET m_hInternet;
};

// Owns the request handle and the async state; closing cancels any pending step.
class ScopedRequest
{
public:
    ScopedRequest()
        : m_hRequest(nullptr)
        , m_pState(new AsyncState)
        , m_bCallbackArmed(false)
    {
        m_pState->m_hStepDone = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        m_pState->m_hClosed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    }

    ~ScopedRequest()
    {
        if (m_bCallbackArmed)
        {
            WinHttpCloseHandle(m_hRequest);
            WaitForSingleObject(m_pState->m_hClosed, kCloseWaitMs);
            ReleaseState(m_pState);
            return;
        }
        if (m_hRequest != nullptr)
        {
            WinHttpCloseHandle(m_hRequest);
        }
        // No callback will ever release the second reference.
        ReleaseState(m_pState);
        ReleaseState(m_pState);
    }

    ScopedRequest(const ScopedRequest&) = delete;
    ScopedRequest& operator=(const ScopedRequest&) = delete;

public:
    bool Arm()
    {
        if (m_pState->m_hStepDone == nullptr || m_pState->m_hClosed == nullptr)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return false;
        }
        DWORD_PTR context = reinterpret_cast<DWORD_PTR>(m_pState);
        if (!WinHttpSetOption(m_hRequest, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context)))
        {
            return false;
        }
        const WINHTTP_STATUS_CALLBACK pPrevious = WinHttpSetStatusCallback(m_hRequest, OnWinHttpStatus,
            WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES, 0);
        m_bCallbackArmed = pPrevious != WINHTTP_INVALID_STATUS_CALLBACK;
        return m_bCallbackArmed;
    }

public:
    HINTERNET GetHandle() const
    {
        return m_hRequest;
    }

    void SetHandle(HINTERNET handle)
    {
        m_hRequest = handle;
    }

    AsyncState* GetState() const
    {
        return m_pState;
    }

private:
    HINTERNET m_hRequest;
    AsyncState* m_pState;
    bool m_bCallbackArmed;
};

void SetFailure(WinHttpResponse* response, DWORD errorCode)
{
    response->m_bNetworkError = true;
    response->m_bTimedOut = errorCode == ERROR_WINHTTP_TIMEOUT;
    response->m_ulErrorCode = errorCode;
    response->m_strError = WinHttpClient::DescribeError(errorCode);
}

QString BuildPathAndQuery(const QUrl& url)
{
    QString strPath = url.path(QUrl::FullyEncoded);
    if (strPath.isEmpty())
    {
        strPath = QStringLiteral("/");
    }
    if (url.hasQuery())
    {
        strPath += QLatin1Char('?') + url.query(QUrl::FullyEncoded);
    }
    return strPath;
}

// Borrows text's UTF-16 buffer, so text must outlive the returned pointer.
LPCWSTR GetWideString(const QString& text)
{
    return reinterpret_cast<LPCWSTR>(text.utf16());
}
}

WinHttpResponse WinHttpClient::Get(const QUrl& url, const HttpHeaders& headers, int timeoutMs)
{
    WinHttpResponse response;
    const QDeadlineTimer timerDeadline(timeoutMs);
    const bool bSecure = url.scheme() == QLatin1String("https");
    if (!url.isValid() || (!bSecure && url.scheme() != QLatin1String("http")) || url.host().isEmpty())
    {
        SetFailure(&response, ERROR_WINHTTP_UNRECOGNIZED_SCHEME);
        return response;
    }

    // WinHTTP sends its agent string as User-Agent; passing the header as well would send it twice.
    QString strAgent;
    QString strHeaders;
    for (const QPair<QByteArray, QByteArray>& pairHeader : headers)
    {
        if (pairHeader.first.compare("User-Agent", Qt::CaseInsensitive) == 0)
        {
            strAgent = QString::fromLatin1(pairHeader.second);
            continue;
        }
        strHeaders += QString::fromLatin1(pairHeader.first) + QStringLiteral(": ") + QString::fromLatin1(pairHeader.second) + QStringLiteral("\r\n");
    }
    const LPCWSTR pszAgent = strAgent.isEmpty() ? nullptr : GetWideString(strAgent);

    // Automatic proxy follows the system settings including PAC; it needs Windows 8.1.
    ScopedInternet session(WinHttpOpen(pszAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC));
    if (session.GetHandle() == nullptr)
    {
        session.SetHandle(WinHttpOpen(pszAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC));
    }
    if (session.GetHandle() == nullptr)
    {
        SetFailure(&response, GetLastError());
        return response;
    }
    // Backstop only: the deadline below bounds the request as a whole.
    WinHttpSetTimeouts(session.GetHandle(), timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    const QString strHost = url.host(QUrl::EncodeUnicode);
    const INTERNET_PORT usPort = static_cast<INTERNET_PORT>(url.port(bSecure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT));
    ScopedInternet connection(WinHttpConnect(session.GetHandle(), GetWideString(strHost), usPort, 0));
    if (connection.GetHandle() == nullptr)
    {
        SetFailure(&response, GetLastError());
        return response;
    }

    const QString strPathAndQuery = BuildPathAndQuery(url);
    ScopedRequest request;
    request.SetHandle(WinHttpOpenRequest(connection.GetHandle(), L"GET", GetWideString(strPathAndQuery), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, bSecure ? WINHTTP_FLAG_SECURE : 0));
    if (request.GetHandle() == nullptr || !request.Arm())
    {
        SetFailure(&response, GetLastError());
        return response;
    }
    // WinHTTP follows redirects with our extra headers, which would hand the bearer token to whatever
    // host Location names. Return the 3xx instead, as QNetworkAccessManager did; fail closed if unsupported.
    DWORD dwDisabledFeatures = WINHTTP_DISABLE_REDIRECTS;
    if (!WinHttpSetOption(request.GetHandle(), WINHTTP_OPTION_DISABLE_FEATURE, &dwDisabledFeatures, sizeof(dwDisabledFeatures)))
    {
        SetFailure(&response, GetLastError());
        return response;
    }
    AsyncState* pState = request.GetState();

    // Waits for the step just started; false means the response already holds the failure.
    const auto funcWaitStep = [&](DWORD expected) -> bool
    {
        const qint64 llRemainingMs = timerDeadline.remainingTime();
        const DWORD dwWaitMs = llRemainingMs < 0 ? INFINITE : static_cast<DWORD>(llRemainingMs);
        if (WaitForSingleObject(pState->m_hStepDone, dwWaitMs) != WAIT_OBJECT_0)
        {
            SetFailure(&response, ERROR_WINHTTP_TIMEOUT);
            return false;
        }
        if (pState->m_dwCompletion == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)
        {
            SetFailure(&response, pState->m_dwError);
            return false;
        }
        if (pState->m_dwCompletion != expected)
        {
            SetFailure(&response, ERROR_WINHTTP_INTERNAL_ERROR);
            return false;
        }
        return true;
    };

    const LPCWSTR pszHeaders = strHeaders.isEmpty() ? WINHTTP_NO_ADDITIONAL_HEADERS : GetWideString(strHeaders);
    if (!WinHttpSendRequest(request.GetHandle(), pszHeaders, static_cast<DWORD>(strHeaders.size()), WINHTTP_NO_REQUEST_DATA, 0, 0,
            reinterpret_cast<DWORD_PTR>(pState)))
    {
        SetFailure(&response, GetLastError());
        return response;
    }
    if (!funcWaitStep(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE))
    {
        return response;
    }
    if (!WinHttpReceiveResponse(request.GetHandle(), nullptr))
    {
        SetFailure(&response, GetLastError());
        return response;
    }
    if (!funcWaitStep(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE))
    {
        return response;
    }

    DWORD dwStatus = 0;
    DWORD dwStatusSize = sizeof(dwStatus);
    if (!WinHttpQueryHeaders(request.GetHandle(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
            &dwStatus, &dwStatusSize, WINHTTP_NO_HEADER_INDEX))
    {
        SetFailure(&response, GetLastError());
        return response;
    }
    response.m_iHttpStatus = static_cast<int>(dwStatus);

    while (true)
    {
        // Async mode: the byte counts arrive in the callback, so the out-parameters stay null.
        if (!WinHttpQueryDataAvailable(request.GetHandle(), nullptr))
        {
            SetFailure(&response, GetLastError());
            return response;
        }
        if (!funcWaitStep(WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE))
        {
            return response;
        }
        if (pState->m_dwBytes == 0)
        {
            break;
        }
        const DWORD dwToRead = qMin(pState->m_dwBytes, kReadChunkBytes);
        pState->m_baBuffer.resize(static_cast<int>(dwToRead));
        if (!WinHttpReadData(request.GetHandle(), pState->m_baBuffer.data(), dwToRead, nullptr))
        {
            SetFailure(&response, GetLastError());
            return response;
        }
        if (!funcWaitStep(WINHTTP_CALLBACK_STATUS_READ_COMPLETE))
        {
            return response;
        }
        if (pState->m_dwBytes == 0)
        {
            break;
        }
        response.m_baBody.append(pState->m_baBuffer.constData(), static_cast<int>(pState->m_dwBytes));
    }
    return response;
}

QString WinHttpClient::DescribeError(unsigned long errorCode)
{
    switch (errorCode)
    {
    case ERROR_WINHTTP_TIMEOUT:
        return QStringLiteral("응답 시간 초과");
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        return QStringLiteral("서버 이름을 찾을 수 없음");
    case ERROR_WINHTTP_CANNOT_CONNECT:
        return QStringLiteral("서버에 연결할 수 없음");
    case ERROR_WINHTTP_CONNECTION_ERROR:
        return QStringLiteral("서버 연결이 끊어짐");
    case ERROR_WINHTTP_SECURE_FAILURE:
    case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID:
    case ERROR_WINHTTP_SECURE_CERT_CN_INVALID:
    case ERROR_WINHTTP_SECURE_INVALID_CA:
    case ERROR_WINHTTP_SECURE_CERT_REV_FAILED:
    case ERROR_WINHTTP_SECURE_CHANNEL_ERROR:
    case ERROR_WINHTTP_SECURE_INVALID_CERT:
    case ERROR_WINHTTP_SECURE_CERT_REVOKED:
    case ERROR_WINHTTP_SECURE_CERT_WRONG_USAGE:
    case ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED:
        return QStringLiteral("보안 연결(TLS) 실패");
    default:
        return QStringLiteral("네트워크 오류 (코드 %1)").arg(errorCode);
    }
}
