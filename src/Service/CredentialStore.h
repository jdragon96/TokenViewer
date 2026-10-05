#pragma once

#include "Model/UsageLimits.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>

enum class ECredentialStatus
{
    OK,
    NOT_FOUND,
    ACCESS_DENIED,
    MALFORMED,
    EXPIRED,
};

struct CredentialResult
{
    ECredentialStatus m_eStatus = ECredentialStatus::NOT_FOUND;
    OAuthCredential m_credential;
};

class CredentialStore
{
public:
    static constexpr const char* kKeychainService = "Claude Code-credentials";

public:
    static CredentialResult Load(const QDateTime& now);
    static CredentialResult ParseCredentialJson(const QByteArray& json, const QDateTime& now);
    static QString FormatPlanLabel(const QString& subscriptionType, const QString& rateLimitTier);

private:
    static ECredentialStatus ReadKeychain(QByteArray* secret);
    static QDateTime ParseEpoch(double value);
};
