#include "Service/ClaudePaths.h"

#include <QDir>
#include <QtTest>

namespace
{
constexpr const char* kConfigDirEnv = "CLAUDE_CONFIG_DIR";
}

class TestClaudePaths : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void TestUsesConfigDirEnvironment();
    void TestNormalizesConfigDirSeparators();
    void TestFallsBackToHomeClaude();
    void TestEmptyEnvironmentFallsBackToHome();

private:
    bool m_bHadEnv = false;
    QByteArray m_baOriginalEnv;
};

void TestClaudePaths::init()
{
    m_bHadEnv = qEnvironmentVariableIsSet(kConfigDirEnv);
    m_baOriginalEnv = qgetenv(kConfigDirEnv);
}

void TestClaudePaths::cleanup()
{
    if (m_bHadEnv)
    {
        qputenv(kConfigDirEnv, m_baOriginalEnv);
    }
    else
    {
        qunsetenv(kConfigDirEnv);
    }
}

void TestClaudePaths::TestUsesConfigDirEnvironment()
{
    qputenv(kConfigDirEnv, "D:/claude-config");
    QCOMPARE(ClaudePaths::GetConfigDirectory(), QStringLiteral("D:/claude-config"));
    QCOMPARE(ClaudePaths::GetCredentialsPath(), QStringLiteral("D:/claude-config/.credentials.json"));
    QCOMPARE(ClaudePaths::GetProjectsPath(), QStringLiteral("D:/claude-config/projects"));
}

void TestClaudePaths::TestNormalizesConfigDirSeparators()
{
    qputenv(kConfigDirEnv, "D:\\claude-config\\");
    QCOMPARE(ClaudePaths::GetConfigDirectory(), QStringLiteral("D:/claude-config"));
    QCOMPARE(ClaudePaths::GetCredentialsPath(), QStringLiteral("D:/claude-config/.credentials.json"));
}

void TestClaudePaths::TestFallsBackToHomeClaude()
{
    qunsetenv(kConfigDirEnv);
    // Only the path string is compared; nothing under the real home is opened.
    const QString strExpected = QDir::homePath() + QStringLiteral("/.claude");
    QCOMPARE(ClaudePaths::GetConfigDirectory(), strExpected);
    QCOMPARE(ClaudePaths::GetCredentialsPath(), strExpected + QStringLiteral("/.credentials.json"));
    QCOMPARE(ClaudePaths::GetProjectsPath(), strExpected + QStringLiteral("/projects"));
}

void TestClaudePaths::TestEmptyEnvironmentFallsBackToHome()
{
    qputenv(kConfigDirEnv, QByteArray());
    QCOMPARE(ClaudePaths::GetConfigDirectory(), QDir::homePath() + QStringLiteral("/.claude"));
}

QTEST_GUILESS_MAIN(TestClaudePaths)
#include "TestClaudePaths.moc"
