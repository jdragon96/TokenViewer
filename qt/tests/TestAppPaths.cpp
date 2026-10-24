#include "Core/AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QtTest>

class TestAppPaths : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void TestLogDirectoryIsUnderAppLocalData();
    void TestLogDirectoryMatchesLocalAppData();
    void TestBadResponseLogPath();
};

void TestAppPaths::initTestCase()
{
    QCoreApplication::setApplicationName(QStringLiteral("TokenViewer"));
    QCoreApplication::setOrganizationName(QStringLiteral("TokenViewer"));
}

void TestAppPaths::TestLogDirectoryIsUnderAppLocalData()
{
    QCOMPARE(AppPaths::GetLogDirectory(),
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/logs"));
}

void TestAppPaths::TestLogDirectoryMatchesLocalAppData()
{
    const QString strLocalAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (strLocalAppData.isEmpty())
    {
        QSKIP("LOCALAPPDATA is not set");
    }
    const QString strExpected = QDir::fromNativeSeparators(strLocalAppData) + QStringLiteral("/TokenViewer/TokenViewer/logs");
    QVERIFY2(AppPaths::GetLogDirectory().compare(strExpected, Qt::CaseInsensitive) == 0, qPrintable(AppPaths::GetLogDirectory()));
}

void TestAppPaths::TestBadResponseLogPath()
{
    QCOMPARE(AppPaths::GetBadResponseLogPath(), AppPaths::GetLogDirectory() + QStringLiteral("/usage-response.log"));
}

QTEST_GUILESS_MAIN(TestAppPaths)
#include "TestAppPaths.moc"
