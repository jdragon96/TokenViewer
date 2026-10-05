#pragma once

#include "Model/UsageLimits.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonValue>
#include <QString>

class UsageApiClient
{
public:
    static constexpr const char* kUsageUrl = "https://api.anthropic.com/api/oauth/usage";
    static constexpr const char* kBetaHeader = "oauth-2025-04-20";
    static constexpr int kTimeoutMs = 10000;

public:
    static FetchResult FetchWithStoredCredential(const QDateTime& now);
    static FetchResult MapHttpResult(int httpStatus, bool networkError, const QString& errorText, const QByteArray& body);
    static FetchResult ParseUsageResponse(const QByteArray& body);

private:
    static FetchResult FetchBlocking(const QString& accessToken);
    static LimitWindow ParseWindow(const QJsonValue& value);
    static QDateTime ParseTimestamp(const QJsonValue& value);
};
