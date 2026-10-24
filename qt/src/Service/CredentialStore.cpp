#include "Service/CredentialStore.h"

#include "Service/ClaudePaths.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

namespace
{
constexpr double kMillisecondEpochThreshold = 1e12;
constexpr double kMillisecondsPerSecond = 1000.0;
}

CredentialResult CredentialStore::Load(const QDateTime& now)
{
    // Claude Code on Windows keeps its login only in this file; there is no Keychain.
    return LoadFromFile(ClaudePaths::GetCredentialsPath(), now);
}

CredentialResult CredentialStore::LoadFromFile(const QString& filePath, const QDateTime& now)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        CredentialResult result;
        result.m_eStatus = ECredentialStatus::NOT_FOUND;
        return result;
    }
    QByteArray baFile = file.readAll();
    const CredentialResult result = ParseCredentialJson(baFile, now);
    // Don't leave the token lying around in freed heap memory.
    baFile.fill('\0');
    return result;
}

CredentialResult CredentialStore::ParseCredentialJson(const QByteArray& json, const QDateTime& now)
{
    CredentialResult result;
    result.m_eStatus = ECredentialStatus::MALFORMED;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
    {
        return result;
    }
    // Only these fields are read; the refresh token is never touched.
    const QJsonObject objOauth = doc.object().value(QStringLiteral("claudeAiOauth")).toObject();
    const QString strToken = objOauth.value(QStringLiteral("accessToken")).toString();
    if (strToken.isEmpty())
    {
        return result;
    }

    OAuthCredential& credential = result.m_credential;
    credential.m_strAccessToken = strToken;
    credential.m_strSubscriptionType = objOauth.value(QStringLiteral("subscriptionType")).toString();
    credential.m_strRateLimitTier = objOauth.value(QStringLiteral("rateLimitTier")).toString();
    const QJsonValue jvExpiresAt = objOauth.value(QStringLiteral("expiresAt"));
    if (jvExpiresAt.isDouble())
    {
        credential.m_dtExpiresAt = ParseEpoch(jvExpiresAt.toDouble());
    }
    else if (jvExpiresAt.isString())
    {
        credential.m_dtExpiresAt = QDateTime::fromString(jvExpiresAt.toString(), Qt::ISODateWithMs).toUTC();
    }

    const bool bExpired = credential.m_dtExpiresAt.isValid() && credential.m_dtExpiresAt <= now;
    result.m_eStatus = bExpired ? ECredentialStatus::EXPIRED : ECredentialStatus::OK;
    return result;
}

QString CredentialStore::FormatPlanLabel(const QString& subscriptionType, const QString& rateLimitTier)
{
    static const QRegularExpression reMultiplier(QStringLiteral("(\\d+)x"));
    const QString strType = subscriptionType.toLower();
    QString strName;
    if (strType == QStringLiteral("max"))
    {
        strName = QStringLiteral("Max");
    }
    else if (strType == QStringLiteral("pro"))
    {
        strName = QStringLiteral("Pro");
    }
    else if (strType == QStringLiteral("team"))
    {
        strName = QStringLiteral("Team");
    }
    else if (strType == QStringLiteral("enterprise"))
    {
        strName = QStringLiteral("Enterprise");
    }
    else
    {
        return QString();
    }

    const QRegularExpressionMatch match = reMultiplier.match(rateLimitTier.toLower());
    if (strName == QStringLiteral("Max") && match.hasMatch())
    {
        return strName + QLatin1Char(' ') + match.captured(1) + QLatin1Char('x');
    }
    return strName;
}

QDateTime CredentialStore::ParseEpoch(double value)
{
    const double dMilliseconds = value > kMillisecondEpochThreshold ? value : value * kMillisecondsPerSecond;
    return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(dMilliseconds), Qt::UTC);
}
