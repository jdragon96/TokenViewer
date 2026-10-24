#include "UI/SettingsDialog.h"

#include "Core/Observers.h"
#include "Core/Settings.h"
#include "UI/LicenseNoticesDialog.h"
#include "UI/WindowsTheme.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <functional>

namespace
{
class ApplicationThemeGuard
{
public:
    ApplicationThemeGuard()
        : m_palOriginal(QApplication::palette())
        , m_fontOriginal(QApplication::font())
    {
    }

    ~ApplicationThemeGuard()
    {
        QApplication::setPalette(m_palOriginal);
        QApplication::setFont(m_fontOriginal);
    }

private:
    QPalette m_palOriginal;
    QFont m_fontOriginal;
};
}

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
    void TestStyleFollowsAppearance();

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

void TestSettingsDialog::TestStyleFollowsAppearance()
{
    const ApplicationThemeGuard guardTheme;
    QApplication::setPalette(WindowsTheme::BuildLightPalette());
    QApplication::setFont(WindowsTheme::BuildAppFont());
    QTemporaryDir dirTemp;
    QSettings storedSettings(dirTemp.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    Settings settings(&storedSettings);
    Observers observers;
    SettingsDialog dialog(observers, &settings);
    QVERIFY(dialog.styleSheet().contains(WindowsTheme::BuildControlStyleSheet(false)));
    // The hint used to be 11 px; it now uses the app font, which Hangul needs at 100% scale.
    QLabel* pHint = dialog.findChild<QLabel*>(QStringLiteral("hintLabel"));
    QVERIFY(pHint != nullptr);
    QCOMPARE(pHint->font().pointSize(), WindowsTheme::kUiFontPointSize);

    QApplication::setPalette(WindowsTheme::BuildDarkPalette());
    QVERIFY(dialog.styleSheet().contains(WindowsTheme::BuildControlStyleSheet(true)));
    QVERIFY(dialog.styleSheet().contains(WindowsTheme::GetSecondaryTextColor(true).name()));
}

QTEST_MAIN(TestSettingsDialog)
#include "TestSettingsDialog.moc"
