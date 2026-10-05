#include "UI/SettingsDialog.h"

#include "Core/Observers.h"
#include "Core/Settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class TestSettingsDialog : public QObject
{
    Q_OBJECT

private slots:
    void TestLoadsCurrentValues();
    void TestWarnChangeUpdatesSettingsAndCriticalChoices();
};

void TestSettingsDialog::TestLoadsCurrentValues()
{
    QTemporaryDir dirTemp;
    QSettings storedSettings(dirTemp.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    Settings settings(&storedSettings);
    settings.SetRefreshIntervalMinutes(5);
    Observers observers;
    SettingsDialog dialog(observers, &settings);
    dialog.LoadFromSettings();

    QCOMPARE(dialog.findChild<QComboBox*>(QStringLiteral("intervalCombo"))->currentData().toInt(), 5);
    QCOMPARE(dialog.findChild<QComboBox*>(QStringLiteral("warnCombo"))->currentData().toInt(), 70);
    QCOMPARE(dialog.findChild<QComboBox*>(QStringLiteral("criticalCombo"))->currentData().toInt(), 90);
    QVERIFY(dialog.findChild<QCheckBox*>(QStringLiteral("launchCheck"))->isChecked());
}

void TestSettingsDialog::TestWarnChangeUpdatesSettingsAndCriticalChoices()
{
    QTemporaryDir dirTemp;
    QSettings storedSettings(dirTemp.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    Settings settings(&storedSettings);
    Observers observers;
    SettingsDialog dialog(observers, &settings);
    dialog.LoadFromSettings();
    QSignalSpy spy(&observers, &Observers::SettingsChanged);

    QComboBox* pWarn = dialog.findChild<QComboBox*>(QStringLiteral("warnCombo"));
    pWarn->setCurrentIndex(pWarn->findData(90));
    QCOMPARE(settings.GetWarnPercent(), 90);
    QCOMPARE(spy.count(), 1);

    QComboBox* pCritical = dialog.findChild<QComboBox*>(QStringLiteral("criticalCombo"));
    QCOMPARE(pCritical->itemData(0).toInt(), 95);
    QCOMPARE(pCritical->currentData().toInt(), settings.GetCriticalPercent());
}

QTEST_MAIN(TestSettingsDialog)
#include "TestSettingsDialog.moc"
