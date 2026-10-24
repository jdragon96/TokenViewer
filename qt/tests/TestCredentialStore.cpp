#include "Service/CredentialStore.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace
{
constexpr const char* kConfigDirEnv = "CLAUDE_CONFIG_DIR";
const QDateTime kNow(QDate(2026, 10, 5), QTime(12, 0), Qt::UTC);
const QString kCredentialsFileName = QStringLiteral(".credentials.json");

QByteArray MakeCredentialJson(const QString& expiresAt)
{
    return QStringLiteral(R"({"claudeAiOauth":{"accessToken":"sk-test","refreshToken":"rt-test","expiresAt":%1,"scopes":["user:inference"],"subscriptionType":"max","rateLimitTier":"default_claude_max_5x"}})")
        .arg(expiresAt).toUtf8();
}

bool WriteFile(const QString& filePath, const QByteArray& content)
{
    QFile file(filePath);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
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
    void TestMissingFileIsNotFound();
    void TestLoadsCredentialFile();
    void TestMalformedFileIsMalformed();
    void TestExpiredFileIsExpired();
    void TestLoadReadsConfigDirFile();
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

void TestCredentialStore::TestMissingFileIsNotFound()
{
    QTemporaryDir dirTemp;
    QVERIFY(dirTemp.isValid());
    const CredentialResult result = CredentialStore::LoadFromFile(dirTemp.filePath(kCredentialsFileName), kNow);
    QCOMPARE(result.m_eStatus, ECredentialStatus::NOT_FOUND);
    QVERIFY(result.m_credential.m_strAccessToken.isEmpty());
}

void TestCredentialStore::TestLoadsCredentialFile()
{
    QTemporaryDir dirTemp;
    QVERIFY(dirTemp.isValid());
    const QString strPath = dirTemp.filePath(kCredentialsFileName);
    const qint64 llExpiresMs = kNow.addSecs(3600).toMSecsSinceEpoch();
    QVERIFY(WriteFile(strPath, MakeCredentialJson(QString::number(llExpiresMs))));

    const CredentialResult result = CredentialStore::LoadFromFile(strPath, kNow);
    QCOMPARE(result.m_eStatus, ECredentialStatus::OK);
    QCOMPARE(result.m_credential.m_strAccessToken, QStringLiteral("sk-test"));
    QCOMPARE(result.m_credential.m_strSubscriptionType, QStringLiteral("max"));
    QCOMPARE(result.m_credential.m_dtExpiresAt, QDateTime::fromMSecsSinceEpoch(llExpiresMs, Qt::UTC));
}

void TestCredentialStore::TestMalformedFileIsMalformed()
{
    QTemporaryDir dirTemp;
    QVERIFY(dirTemp.isValid());
    const QString strPath = dirTemp.filePath(kCredentialsFileName);
    QVERIFY(WriteFile(strPath, QByteArrayLiteral(R"({"claudeAiOauth":{}})")));
    QCOMPARE(CredentialStore::LoadFromFile(strPath, kNow).m_eStatus, ECredentialStatus::MALFORMED);
}

void TestCredentialStore::TestExpiredFileIsExpired()
{
    QTemporaryDir dirTemp;
    QVERIFY(dirTemp.isValid());
    const QString strPath = dirTemp.filePath(kCredentialsFileName);
    QVERIFY(WriteFile(strPath, MakeCredentialJson(QString::number(kNow.addSecs(-1).toMSecsSinceEpoch()))));
    QCOMPARE(CredentialStore::LoadFromFile(strPath, kNow).m_eStatus, ECredentialStatus::EXPIRED);
}

void TestCredentialStore::TestLoadReadsConfigDirFile()
{
    QTemporaryDir dirTemp;
    QVERIFY(dirTemp.isValid());
    QVERIFY(WriteFile(dirTemp.filePath(kCredentialsFileName), MakeCredentialJson(QString::number(kNow.addSecs(3600).toMSecsSinceEpoch()))));

    // CLAUDE_CONFIG_DIR keeps Load() away from the real home folder.
    const bool bHadEnv = qEnvironmentVariableIsSet(kConfigDirEnv);
    const QByteArray baOriginal = qgetenv(kConfigDirEnv);
    qputenv(kConfigDirEnv, dirTemp.path().toUtf8());
    const CredentialResult result = CredentialStore::Load(kNow);
    if (bHadEnv)
    {
        qputenv(kConfigDirEnv, baOriginal);
    }
    else
    {
        qunsetenv(kConfigDirEnv);
    }
    QCOMPARE(result.m_eStatus, ECredentialStatus::OK);
    QCOMPARE(result.m_credential.m_strAccessToken, QStringLiteral("sk-test"));
}

QTEST_GUILESS_MAIN(TestCredentialStore)
#include "TestCredentialStore.moc"
