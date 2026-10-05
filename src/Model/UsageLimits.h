#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QMetaType>
#include <QString>

struct LimitWindow
{
    bool m_bValid = false;
    double m_dPercent = 0.0;
    QDateTime m_dtResetsAt;
};

struct UsageLimits
{
    LimitWindow m_fiveHour;
    LimitWindow m_sevenDay;
};

struct OAuthCredential
{
    QString m_strAccessToken;
    QDateTime m_dtExpiresAt;
    QString m_strSubscriptionType;
    QString m_strRateLimitTier;
};

enum class EFetchStatus
{
    OK,
    NOT_LOGGED_IN,
    KEYCHAIN_DENIED,
    TOKEN_EXPIRED,
    RATE_LIMITED,
    NETWORK_ERROR,
    SERVER_ERROR,
    BAD_RESPONSE,
};

struct FetchResult
{
    EFetchStatus m_eStatus = EFetchStatus::NETWORK_ERROR;
    UsageLimits m_limits;
    QString m_strPlanLabel;
    QString m_strDetail;
    int m_iHttpStatus = 0;
    QByteArray m_baRawBody;
};

Q_DECLARE_METATYPE(FetchResult)
