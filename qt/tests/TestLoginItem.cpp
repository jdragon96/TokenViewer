#include "Service/LoginItem.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

namespace
{
const QString kValueName = QStringLiteral("TokenViewer");
const QDateTime kOldTime(QDate(2020, 1, 1), QTime(0, 0), Qt::UTC);

bool TouchFile(const QString& filePath)
{
    QFile file(filePath);
    return file.open(QIODevice::WriteOnly);
}

QString Quote(const QString& executablePath)
{
    return QLatin1Char('"') + QDir::toNativeSeparators(executablePath) + QLatin1Char('"');
}
}

class TestLoginItem : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void TestBuildsQuotedNativeCommand();
    void TestDetectsQtRuntimeBesideExecutable();
    void TestApplyWritesRunValue();
    void TestApplyKeepsSameValue();
    void TestApplyOverwritesDifferentPath();
    void TestDisableRemovesValue();
    void TestDisableWithoutValueSucceeds();
    void TestRefusesWithoutQtRuntime();

private:
    std::unique_ptr<QTemporaryDir> m_upDir;
    std::unique_ptr<QSettings> m_upRunKey;
    QString m_strIniPath;
    QString m_strDeployedExe;
    QString m_strBareExe;
};

void TestLoginItem::init()
{
    m_upDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_upDir->isValid());
    m_strIniPath = m_upDir->filePath(QStringLiteral("run.ini"));
    m_upRunKey = std::make_unique<QSettings>(m_strIniPath, QSettings::IniFormat);

    // A deployed folder has Qt5Core.dll next to the executable; a build folder does not.
    QVERIFY(QDir(m_upDir->path()).mkpath(QStringLiteral("deployed")));
    QVERIFY(QDir(m_upDir->path()).mkpath(QStringLiteral("build")));
    m_strDeployedExe = m_upDir->filePath(QStringLiteral("deployed/TokenViewer.exe"));
    m_strBareExe = m_upDir->filePath(QStringLiteral("build/TokenViewer.exe"));
    QVERIFY(TouchFile(m_strDeployedExe));
    QVERIFY(TouchFile(m_upDir->filePath(QStringLiteral("deployed/Qt5Core.dll"))));
    QVERIFY(TouchFile(m_strBareExe));
}

void TestLoginItem::cleanup()
{
    m_upRunKey.reset();
    m_upDir.reset();
}

void TestLoginItem::TestBuildsQuotedNativeCommand()
{
    QCOMPARE(LoginItem::BuildCommand(QStringLiteral("C:/Program Files/TokenViewer/TokenViewer.exe")),
        QStringLiteral("\"C:\\Program Files\\TokenViewer\\TokenViewer.exe\""));
}

void TestLoginItem::TestDetectsQtRuntimeBesideExecutable()
{
    QVERIFY(LoginItem::HasQtRuntime(m_strDeployedExe));
    QVERIFY(!LoginItem::HasQtRuntime(m_strBareExe));
}

void TestLoginItem::TestApplyWritesRunValue()
{
    QVERIFY(LoginItem::Apply(m_upRunKey.get(), true, m_strDeployedExe));
    QCOMPARE(m_upRunKey->value(kValueName).toString(), Quote(m_strDeployedExe));

    // The value must reach the backing store, not just the in-memory cache.
    m_upRunKey.reset();
    QSettings settingsReopened(m_strIniPath, QSettings::IniFormat);
    QCOMPARE(settingsReopened.value(kValueName).toString(), Quote(m_strDeployedExe));
}

void TestLoginItem::TestApplyKeepsSameValue()
{
    m_upRunKey->setValue(kValueName, Quote(m_strDeployedExe));
    m_upRunKey->sync();
    {
        QFile file(m_strIniPath);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(kOldTime, QFileDevice::FileModificationTime));
    }

    QVERIFY(LoginItem::Apply(m_upRunKey.get(), true, m_strDeployedExe));
    m_upRunKey->sync();
    QCOMPARE(QFileInfo(m_strIniPath).lastModified().toUTC(), kOldTime);
    QCOMPARE(m_upRunKey->value(kValueName).toString(), Quote(m_strDeployedExe));
}

void TestLoginItem::TestApplyOverwritesDifferentPath()
{
    m_upRunKey->setValue(kValueName, QStringLiteral("\"C:\\Old\\TokenViewer.exe\""));
    m_upRunKey->sync();

    QVERIFY(LoginItem::Apply(m_upRunKey.get(), true, m_strDeployedExe));
    QCOMPARE(m_upRunKey->value(kValueName).toString(), Quote(m_strDeployedExe));
}

void TestLoginItem::TestDisableRemovesValue()
{
    m_upRunKey->setValue(kValueName, Quote(m_strDeployedExe));
    m_upRunKey->setValue(QStringLiteral("Other"), QStringLiteral("\"C:\\Other.exe\""));
    m_upRunKey->sync();

    QVERIFY(LoginItem::Apply(m_upRunKey.get(), false, m_strDeployedExe));
    QVERIFY(!m_upRunKey->contains(kValueName));
    QCOMPARE(m_upRunKey->value(QStringLiteral("Other")).toString(), QStringLiteral("\"C:\\Other.exe\""));

    m_upRunKey.reset();
    QSettings settingsReopened(m_strIniPath, QSettings::IniFormat);
    QVERIFY(!settingsReopened.contains(kValueName));
}

void TestLoginItem::TestDisableWithoutValueSucceeds()
{
    QVERIFY(LoginItem::Apply(m_upRunKey.get(), false, m_strBareExe));
    QVERIFY(!m_upRunKey->contains(kValueName));
}

void TestLoginItem::TestRefusesWithoutQtRuntime()
{
    m_upRunKey->setValue(QStringLiteral("Other"), QStringLiteral("\"C:\\Other.exe\""));
    m_upRunKey->sync();

    QVERIFY(!LoginItem::Apply(m_upRunKey.get(), true, m_strBareExe));
    QVERIFY(!m_upRunKey->contains(kValueName));
    m_upRunKey->sync();
    QSettings settingsReopened(m_strIniPath, QSettings::IniFormat);
    QVERIFY(!settingsReopened.contains(kValueName));
    QCOMPARE(settingsReopened.allKeys(), QStringList{ QStringLiteral("Other") });
}

QTEST_GUILESS_MAIN(TestLoginItem)
#include "TestLoginItem.moc"
