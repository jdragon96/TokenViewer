#include "Service/UsageApiClient.h"

#include "Service/CredentialStore.h"
#include "Service/WinHttpClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

namespace
{
constexpr int kHttpOk = 200;
constexpr int kHttpUnauthorized = 401;
constexpr int kHttpForbidden = 403;
constexpr int kHttpTooManyRequests = 429;
constexpr int kHttpServerErrorMin = 500;
constexpr double kMillisecondEpochThreshold = 1e12;
constexpr double kMillisecondsPerSecond = 1000.0;
const QString kFiveHourKey = QStringLiteral("five_hour");
const QString kSevenDayKey = QStringLiteral("seven_day");
}

FetchResult UsageApiClient::FetchWithStoredCredential(const QDateTime& now)
{
    const CredentialResult credential = CredentialStore::Load(now);
    const QString strPlanLabel = CredentialStore::FormatPlanLabel(credential.m_credential.m_strSubscriptionType, credential.m_credential.m_strRateLimitTier);
    FetchResult result;
    switch (credential.m_eStatus)
    {
    case ECredentialStatus::NOT_FOUND:
    case ECredentialStatus::MALFORMED:
        result.m_eStatus = EFetchStatus::NOT_LOGGED_IN;
        result.m_strDetail = QStringLiteral("Claude Code 로그인 정보를 찾지 못했습니다");
        return result;
    case ECredentialStatus::ACCESS_DENIED:
        result.m_eStatus = EFetchStatus::KEYCHAIN_DENIED;
        result.m_strDetail = QStringLiteral("Keychain 접근이 거부되었습니다");
        return result;
    case ECredentialStatus::EXPIRED:
        result.m_eStatus = EFetchStatus::TOKEN_EXPIRED;
        result.m_strDetail = QStringLiteral("로그인 토큰이 만료되었습니다");
        result.m_strPlanLabel = strPlanLabel;
        return result;
    case ECredentialStatus::OK:
        break;
    }

    result = FetchBlocking(credential.m_credential.m_strAccessToken);
    result.m_strPlanLabel = strPlanLabel;
    return result;
}

FetchResult UsageApiClient::MapHttpResult(int httpStatus, bool networkError, const QString& errorText, const QByteArray& body)
{
    if (httpStatus == kHttpOk && !networkError)
    {
        return ParseUsageResponse(body);
    }

    FetchResult result;
    result.m_iHttpStatus = httpStatus;
    if (httpStatus == kHttpUnauthorized || httpStatus == kHttpForbidden)
    {
        result.m_eStatus = EFetchStatus::TOKEN_EXPIRED;
        result.m_strDetail = QStringLiteral("인증이 만료되었습니다 (HTTP %1)").arg(httpStatus);
    }
    else if (httpStatus == kHttpTooManyRequests)
    {
        result.m_eStatus = EFetchStatus::RATE_LIMITED;
        result.m_strDetail = QStringLiteral("요청이 너무 많습니다 (HTTP 429)");
    }
    else if (httpStatus >= kHttpServerErrorMin)
    {
        result.m_eStatus = EFetchStatus::SERVER_ERROR;
        result.m_strDetail = QStringLiteral("서버 오류 (HTTP %1)").arg(httpStatus);
    }
    else if (httpStatus == 0 || httpStatus == kHttpOk)
    {
        result.m_eStatus = EFetchStatus::NETWORK_ERROR;
        result.m_strDetail = errorText.isEmpty() ? QStringLiteral("네트워크 연결 없음") : errorText;
    }
    else
    {
        result.m_eStatus = EFetchStatus::BAD_RESPONSE;
        result.m_strDetail = QStringLiteral("예상하지 못한 응답 (HTTP %1)").arg(httpStatus);
        result.m_baRawBody = body;
    }
    return result;
}

FetchResult UsageApiClient::ParseUsageResponse(const QByteArray& body)
{
    FetchResult result;
    result.m_iHttpStatus = kHttpOk;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    const QJsonObject objRoot = doc.object();
    if (!doc.isObject() || (!objRoot.contains(kFiveHourKey) && !objRoot.contains(kSevenDayKey)))
    {
        result.m_eStatus = EFetchStatus::BAD_RESPONSE;
        result.m_strDetail = QStringLiteral("five_hour / seven_day 필드가 없습니다");
        result.m_baRawBody = body;
        return result;
    }
    result.m_eStatus = EFetchStatus::OK;
    bool bMalformed = false;
    result.m_limits.m_fiveHour = ParseWindow(objRoot.value(kFiveHourKey), &bMalformed);
    result.m_limits.m_sevenDay = ParseWindow(objRoot.value(kSevenDayKey), &bMalformed);
    if (bMalformed)
    {
        result.m_limits = UsageLimits();
        result.m_eStatus = EFetchStatus::BAD_RESPONSE;
        result.m_strDetail = QStringLiteral("five_hour / seven_day 형식이 바뀌었습니다");
        result.m_baRawBody = body;
    }
    return result;
}

FetchResult UsageApiClient::FetchBlocking(const QString& accessToken)
{
    return FetchUsage(QUrl(QString::fromLatin1(kUsageUrl)), accessToken, kTimeoutMs);
}

FetchResult UsageApiClient::FetchUsage(const QUrl& url, const QString& accessToken, int timeoutMs)
{
    // WinHTTP rather than QNetworkAccessManager: official Qt 5.15 Windows builds need OpenSSL 1.1.1 DLLs for HTTPS.
    const HttpHeaders vecHeaders = {
        { "Authorization", QByteArrayLiteral("Bearer ") + accessToken.toUtf8() },
        { "anthropic-beta", kBetaHeader },
        { "Accept", "application/json" },
        { "User-Agent", "TokenViewer/" TV_VERSION },
    };
    const WinHttpResponse response = WinHttpClient::Get(url, vecHeaders, timeoutMs);
    if (response.m_bTimedOut)
    {
        FetchResult result;
        result.m_eStatus = EFetchStatus::NETWORK_ERROR;
        result.m_strDetail = QStringLiteral("응답 시간 초과");
        return result;
    }
    if (response.m_bNetworkError)
    {
        // A transport failure after the status line still leaves a partial body; never parse it.
        return MapHttpResult(0, true, response.m_strError, QByteArray());
    }
    return MapHttpResult(response.m_iHttpStatus, false, QString(), response.m_baBody);
}

LimitWindow UsageApiClient::ParseWindow(const QJsonValue& value, bool* malformed)
{
    LimitWindow window;
    if (value.isNull() || value.isUndefined())
    {
        return window;
    }
    if (!value.isObject())
    {
        *malformed = true;
        return window;
    }
    const QJsonObject objWindow = value.toObject();
    const QString strUtilization = QStringLiteral("utilization");
    const QString strUsedPercentage = QStringLiteral("used_percentage");
    if (!objWindow.contains(strUtilization) && !objWindow.contains(strUsedPercentage))
    {
        *malformed = true;
        return window;
    }
    QJsonValue jvPercent = objWindow.value(strUtilization);
    if (!jvPercent.isDouble())
    {
        jvPercent = objWindow.value(strUsedPercentage);
    }
    if (!jvPercent.isDouble())
    {
        // A percent key that is null leaves the meter empty; any other type is format drift.
        const QJsonValue jvPresent = objWindow.contains(strUtilization) ? objWindow.value(strUtilization) : objWindow.value(strUsedPercentage);
        *malformed = *malformed || !jvPresent.isNull();
        return window;
    }
    window.m_bValid = true;
    window.m_dPercent = std::clamp(jvPercent.toDouble(), 0.0, 100.0);
    window.m_dtResetsAt = ParseTimestamp(objWindow.value(QStringLiteral("resets_at")));
    return window;
}

QDateTime UsageApiClient::ParseTimestamp(const QJsonValue& value)
{
    if (value.isDouble())
    {
        const double dValue = value.toDouble();
        const double dMilliseconds = dValue > kMillisecondEpochThreshold ? dValue : dValue * kMillisecondsPerSecond;
        return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(dMilliseconds), Qt::UTC);
    }
    if (!value.isString())
    {
        return QDateTime();
    }
    QString strValue = value.toString();
    QDateTime dtValue = QDateTime::fromString(strValue, Qt::ISODateWithMs);
    if (!dtValue.isValid())
    {
        // Qt 5 reads at most milliseconds; drop longer fractions such as microseconds.
        static const QRegularExpression reFraction(QStringLiteral("\\.(\\d{3})\\d+"));
        strValue.replace(reFraction, QStringLiteral(".\\1"));
        dtValue = QDateTime::fromString(strValue, Qt::ISODateWithMs);
    }
    return dtValue.isValid() ? dtValue.toUTC() : QDateTime();
}
