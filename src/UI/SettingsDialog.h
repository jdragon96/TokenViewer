#pragma once

#include <QDialog>

class Observers;
class QCheckBox;
class QComboBox;
class QPushButton;
class Settings;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    SettingsDialog(Observers& observers, Settings* settings, QWidget* parent = nullptr);
    ~SettingsDialog();

public:
    void LoadFromSettings();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void BuildUi();
    void FillCriticalChoices();

private:
    Observers& m_observers;
    Settings* m_kpSettings;
    QComboBox* m_pIntervalCombo;
    QComboBox* m_pWarnCombo;
    QComboBox* m_pCriticalCombo;
    QCheckBox* m_pLaunchCheck;
    QPushButton* m_pLicenseButton;
    QPushButton* m_pAboutQtButton;
    bool m_bLoading;
};
