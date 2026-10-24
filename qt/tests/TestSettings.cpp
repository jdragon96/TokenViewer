#include "Core/Settings.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

class TestSettings : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void TestDefaultsWhenEmpty();
    void TestRejectsUnknownInterval();
    void TestRejectsInvalidWarnPercent();
    void TestKeepsCriticalAboveWarn();
    void TestRoundTripsDimensionAndPeriod();

private:
    std::unique_ptr<QTemporaryDir> m_upDir;
    std::unique_ptr<QSettings> m_upStoredSettings;
    std::unique_ptr<Settings> m_upSettings;
};

void TestSettings::init()
{
    m_upDir = std::make_unique<QTemporaryDir>();
    m_upStoredSettings = std::make_unique<QSettings>(m_upDir->filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    m_upSettings = std::make_unique<Settings>(m_upStoredSettings.get());
}

void TestSettings::cleanup()
{
    m_upSettings.reset();
    m_upStoredSettings.reset();
    m_upDir.reset();
}

void TestSettings::TestDefaultsWhenEmpty()
{
    QCOMPARE(m_upSettings->GetRefreshIntervalMinutes(), 3);
    QCOMPARE(m_upSettings->GetWarnPercent(), 70);
    QCOMPARE(m_upSettings->GetCriticalPercent(), 90);
    QVERIFY(m_upSettings->GetLaunchAtLogin());
    QCOMPARE(m_upSettings->GetDimension(), EBreakdownDimension::PROJECT);
    QCOMPARE(m_upSettings->GetPeriod(), EBreakdownPeriod::FIVE_HOUR_WINDOW);
}

void TestSettings::TestRejectsUnknownInterval()
{
    m_upSettings->SetRefreshIntervalMinutes(10);
    QCOMPARE(m_upSettings->GetRefreshIntervalMinutes(), 10);
    m_upSettings->SetRefreshIntervalMinutes(7);
    QCOMPARE(m_upSettings->GetRefreshIntervalMinutes(), 3);
}

void TestSettings::TestRejectsInvalidWarnPercent()
{
    m_upSettings->SetWarnPercent(45);
    QCOMPARE(m_upSettings->GetWarnPercent(), 70);
    m_upSettings->SetWarnPercent(72);
    QCOMPARE(m_upSettings->GetWarnPercent(), 70);
    m_upSettings->SetWarnPercent(80);
    QCOMPARE(m_upSettings->GetWarnPercent(), 80);
}

void TestSettings::TestKeepsCriticalAboveWarn()
{
    m_upSettings->SetWarnPercent(90);
    m_upSettings->SetCriticalPercent(90);
    QCOMPARE(m_upSettings->GetCriticalPercent(), 95);
    m_upSettings->SetWarnPercent(95);
    m_upSettings->SetCriticalPercent(50);
    QCOMPARE(m_upSettings->GetCriticalPercent(), 100);
}

void TestSettings::TestRoundTripsDimensionAndPeriod()
{
    m_upSettings->SetDimension(EBreakdownDimension::SESSION);
    m_upSettings->SetPeriod(EBreakdownPeriod::SEVEN_DAYS);
    QCOMPARE(m_upSettings->GetDimension(), EBreakdownDimension::SESSION);
    QCOMPARE(m_upSettings->GetPeriod(), EBreakdownPeriod::SEVEN_DAYS);
    m_upStoredSettings->setValue(QStringLiteral("popup/dimension"), 9);
    QCOMPARE(m_upSettings->GetDimension(), EBreakdownDimension::PROJECT);
}

QTEST_GUILESS_MAIN(TestSettings)
#include "TestSettings.moc"
