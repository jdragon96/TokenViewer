#include "Service/CredentialStore.h"

#include <QtTest>

namespace
{
const QDateTime kNow(QDate(2026, 10, 5), QTime(12, 0), Qt::UTC);

QByteArray MakeCredentialJson(const QString& expiresAt)
{
    return QStringLiteral(R"({"claudeAiOauth":{"accessToken":"sk-test","refreshToken":"rt-test","expiresAt":%1,"scopes":["user:inference"],"subscriptionType":"max","rateLimitTier":"default_claude_max_5x"}})")
        .arg(expiresAt).toUtf8();
}
}

class TestCredentialStore : public QObject
{
    Q_OBJECT

private slots:
    void TestParsesTokenAndPlan();
    void TestReportsExpiredToken();
    void TestAcceptsEpochSeconds();
    void TestMissingTokenIsMalformed();
    void TestNotJsonIsMalformed();
    void TestFormatsPlanLabel();
};

void TestCredentialStore::TestParsesTokenAndPlan()
{
    const qint64 llExpiresMs = kNow.addSecs(3600).toMSecsSinceEpoch();
    const CredentialResult result = CredentialStore::ParseCredentialJson(MakeCredentialJson(QString::number(llExpiresMs)), kNow);
    QCOMPARE(result.m_eStatus, ECredentialStatus::OK);
    QCOMPARE(result.m_credential.m_strAccessToken, QStringLiteral("sk-test"));
    QCOMPARE(result.m_credential.m_dtExpiresAt, QDateTime::fromMSecsSinceEpoch(llExpiresMs, Qt::UTC));
    QCOMPARE(result.m_credential.m_strSubscriptionType, QStringLiteral("max"));
    QCOMPARE(result.m_credential.m_strRateLimitTier, QStringLiteral("default_claude_max_5x"));
}

void TestCredentialStore::TestReportsExpiredToken()
{
    const qint64 llExpiresMs = kNow.addSecs(-1).toMSecsSinceEpoch();
    const CredentialResult result = CredentialStore::ParseCredentialJson(MakeCredentialJson(QString::number(llExpiresMs)), kNow);
    QCOMPARE(result.m_eStatus, ECredentialStatus::EXPIRED);
    QCOMPARE(result.m_credential.m_strSubscriptionType, QStringLiteral("max"));
}

void TestCredentialStore::TestAcceptsEpochSeconds()
{
    const qint64 llExpiresSecs = kNow.addSecs(600).toSecsSinceEpoch();
    const CredentialResult result = CredentialStore::ParseCredentialJson(MakeCredentialJson(QString::number(llExpiresSecs)), kNow);
    QCOMPARE(result.m_eStatus, ECredentialStatus::OK);
    QCOMPARE(result.m_credential.m_dtExpiresAt, QDateTime::fromSecsSinceEpoch(llExpiresSecs, Qt::UTC));
}

void TestCredentialStore::TestMissingTokenIsMalformed()
{
    const CredentialResult result = CredentialStore::ParseCredentialJson(QByteArrayLiteral(R"({"claudeAiOauth":{"subscriptionType":"max"}})"), kNow);
    QCOMPARE(result.m_eStatus, ECredentialStatus::MALFORMED);
}

void TestCredentialStore::TestNotJsonIsMalformed()
{
    QCOMPARE(CredentialStore::ParseCredentialJson(QByteArrayLiteral("not json"), kNow).m_eStatus, ECredentialStatus::MALFORMED);
}

void TestCredentialStore::TestFormatsPlanLabel()
{
    QCOMPARE(CredentialStore::FormatPlanLabel(QStringLiteral("max"), QStringLiteral("default_claude_max_5x")), QStringLiteral("Max 5x"));
    QCOMPARE(CredentialStore::FormatPlanLabel(QStringLiteral("max"), QStringLiteral("default_claude_max_20x")), QStringLiteral("Max 20x"));
    QCOMPARE(CredentialStore::FormatPlanLabel(QStringLiteral("max"), QString()), QStringLiteral("Max"));
    QCOMPARE(CredentialStore::FormatPlanLabel(QStringLiteral("pro"), QString()), QStringLiteral("Pro"));
    QCOMPARE(CredentialStore::FormatPlanLabel(QString(), QString()), QString());
}

QTEST_GUILESS_MAIN(TestCredentialStore)
#include "TestCredentialStore.moc"
