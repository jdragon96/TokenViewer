#include "Service/CredentialStore.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

namespace
{
constexpr double kMillisecondEpochThreshold = 1e12;
constexpr double kMillisecondsPerSecond = 1000.0;
const QString kCredentialsFile = QStringLiteral("/.claude/.credentials.json");
}

CredentialResult CredentialStore::Load(const QDateTime& now)
{
    QByteArray baSecret;
    const ECredentialStatus eKeychainStatus = ReadKeychain(&baSecret);
    if (eKeychainStatus == ECredentialStatus::OK)
    {
        const CredentialResult result = ParseCredentialJson(baSecret, now);
        baSecret.fill('\0');
        return result;
    }
    if (eKeychainStatus == ECredentialStatus::ACCESS_DENIED)
    {
        CredentialResult result;
        result.m_eStatus = ECredentialStatus::ACCESS_DENIED;
        return result;
    }

    // Claude Code falls back to this file when the Keychain is unavailable.
    QFile file(QDir::homePath() + kCredentialsFile);
    if (!file.open(QIODevice::ReadOnly))
    {
        CredentialResult result;
        result.m_eStatus = ECredentialStatus::NOT_FOUND;
        return result;
    }
    QByteArray baFile = file.readAll();
    const CredentialResult result = ParseCredentialJson(baFile, now);
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

ECredentialStatus CredentialStore::ReadKeychain(QByteArray* secret)
{
    CFStringRef cfService = CFStringCreateWithCString(kCFAllocatorDefault, kKeychainService, kCFStringEncodingUTF8);
    const void* arrKeys[] = { kSecClass, kSecAttrService, kSecReturnData, kSecMatchLimit };
    const void* arrValues[] = { kSecClassGenericPassword, cfService, kCFBooleanTrue, kSecMatchLimitOne };
    CFDictionaryRef cfQuery = CFDictionaryCreate(kCFAllocatorDefault, arrKeys, arrValues, 4,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

    // The first call may show the macOS "allow access" prompt; this blocks until the user answers.
    CFTypeRef cfResult = nullptr;
    const OSStatus iStatus = SecItemCopyMatching(cfQuery, &cfResult);
    CFRelease(cfQuery);
    CFRelease(cfService);

    if (iStatus == errSecSuccess && cfResult != nullptr)
    {
        const CFDataRef cfData = static_cast<CFDataRef>(cfResult);
        *secret = QByteArray(reinterpret_cast<const char*>(CFDataGetBytePtr(cfData)), static_cast<int>(CFDataGetLength(cfData)));
        CFRelease(cfResult);
        return ECredentialStatus::OK;
    }
    if (cfResult != nullptr)
    {
        CFRelease(cfResult);
    }
    return iStatus == errSecItemNotFound ? ECredentialStatus::NOT_FOUND : ECredentialStatus::ACCESS_DENIED;
}

QDateTime CredentialStore::ParseEpoch(double value)
{
    const double dMilliseconds = value > kMillisecondEpochThreshold ? value : value * kMillisecondsPerSecond;
    return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(dMilliseconds), Qt::UTC);
}
