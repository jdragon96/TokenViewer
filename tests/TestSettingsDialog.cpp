#include "UI/SettingsDialog.h"

#include "Core/Observers.h"
#include "Core/Settings.h"
#include "UI/LicenseNoticesDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QTimer>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <functional>

class TestSettingsDialog : public QObject
{
    Q_OBJECT

private slots:
    void TestLoadsCurrentValues();
    void TestWarnChangeUpdatesSettingsAndCriticalChoices();
    void TestLicenseButtonIgnoresRightClick();
    void TestLicenseButtonOpensOnLeftClick();
    void TestLicenseButtonOpensOnSpaceKey();
    void TestLicenseButtonIgnoresOrphanKeyRelease();

private:
    // Runs the action, lets a deferred dialog open (and closes it), and reports whether one appeared.
    static bool OpensLicenseDialog(SettingsDialog& dialog, const std::function<void(QPushButton*)>& action);
};

bool TestSettingsDialog::OpensLicenseDialog(SettingsDialog& dialog, const std::function<void(QPushButton*)>& action)
{
    constexpr int kProbeDelayMs = 100;
    constexpr int kWaitMs = 300;
    bool bOpened = false;
    QTimer::singleShot(kProbeDelayMs, &dialog, [&dialog, &bOpened]()
    {
        LicenseNoticesDialog* pNotices = dialog.findChild<LicenseNoticesDialog*>();
        if (pNotices)
        {
            bOpened = true;
            pNotices->reject();
        }
    });
    action(dialog.findChild<QPushButton*>(QStringLiteral("licenseButton")));
    QTest::qWait(kWaitMs);
    return bOpened;
}

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

void TestSettingsDialog::TestLicenseButtonIgnoresRightClick()
{
    QTemporaryDir dirTemp;
    QSettings storedSettings(dirTemp.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    Settings settings(&storedSettings);
    Observers observers;
    SettingsDialog dialog(observers, &settings);
    QVERIFY(!OpensLicenseDialog(dialog, [](QPushButton* pButton)
    {
        QTest::mouseClick(pButton, Qt::RightButton);
    }));
}

void TestSettingsDialog::TestLicenseButtonOpensOnLeftClick()
{
    QTemporaryDir dirTemp;
    QSettings storedSettings(dirTemp.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    Settings settings(&storedSettings);
    Observers observers;
    SettingsDialog dialog(observers, &settings);
    QVERIFY(OpensLicenseDialog(dialog, [](QPushButton* pButton)
    {
        QTest::mouseClick(pButton, Qt::LeftButton);
    }));
}

void TestSettingsDialog::TestLicenseButtonOpensOnSpaceKey()
{
    QTemporaryDir dirTemp;
    QSettings storedSettings(dirTemp.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    Settings settings(&storedSettings);
    Observers observers;
    SettingsDialog dialog(observers, &settings);
    QVERIFY(OpensLicenseDialog(dialog, [](QPushButton* pButton)
    {
        QTest::keyClick(pButton, Qt::Key_Space);
    }));
}

void TestSettingsDialog::TestLicenseButtonIgnoresOrphanKeyRelease()
{
    QTemporaryDir dirTemp;
    QSettings storedSettings(dirTemp.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    Settings settings(&storedSettings);
    Observers observers;
    SettingsDialog dialog(observers, &settings);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    QVERIFY(!OpensLicenseDialog(dialog, [](QPushButton* pButton)
    {
        pButton->setFocus();
        QTest::keyRelease(pButton, Qt::Key_Return);
    }));
}

QTEST_MAIN(TestSettingsDialog)
#include "TestSettingsDialog.moc"
