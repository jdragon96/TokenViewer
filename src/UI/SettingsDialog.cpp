#include "UI/SettingsDialog.h"

#include "Core/Observers.h"
#include "Core/Settings.h"
#include "UI/LicenseNoticesDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
constexpr int kHintPixelSize = 11;
}

SettingsDialog::SettingsDialog(Observers& observers, Settings* settings, QWidget* parent)
    : QDialog(parent)
    , m_observers(observers)
    , m_kpSettings(settings)
    , m_pIntervalCombo(nullptr)
    , m_pWarnCombo(nullptr)
    , m_pCriticalCombo(nullptr)
    , m_pLaunchCheck(nullptr)
    , m_pLicenseButton(nullptr)
    , m_pAboutQtButton(nullptr)
    , m_bLoading(false)
{
    BuildUi();
}

SettingsDialog::~SettingsDialog() = default;

void SettingsDialog::LoadFromSettings()
{
    m_bLoading = true;
    m_pIntervalCombo->setCurrentIndex(m_pIntervalCombo->findData(m_kpSettings->GetRefreshIntervalMinutes()));
    m_pWarnCombo->setCurrentIndex(m_pWarnCombo->findData(m_kpSettings->GetWarnPercent()));
    FillCriticalChoices();
    m_pLaunchCheck->setChecked(m_kpSettings->GetLaunchAtLogin());
    m_bLoading = false;
}

bool SettingsDialog::eventFilter(QObject* watched, QEvent* event)
{
    QWidget* pWidget = qobject_cast<QWidget*>(watched);
    if (!pWidget || (watched != m_pLicenseButton && watched != m_pAboutQtButton))
    {
        return QDialog::eventFilter(watched, event);
    }

    bool bActivated = false;
    if (event->type() == QEvent::MouseButtonRelease)
    {
        QMouseEvent* pMouse = static_cast<QMouseEvent*>(event);
        bActivated = pMouse->button() == Qt::LeftButton && pWidget->rect().contains(pMouse->pos());
    }
    else if (event->type() == QEvent::KeyRelease)
    {
        const int iKey = static_cast<QKeyEvent*>(event)->key();
        bActivated = iKey == Qt::Key_Space || iKey == Qt::Key_Return || iKey == Qt::Key_Enter;
    }

    if (bActivated)
    {
        // Defer the modal dialogs so they do not run inside the event being delivered.
        if (watched == m_pLicenseButton)
        {
            QTimer::singleShot(0, this, [this]()
            {
                LicenseNoticesDialog dialog(this);
                dialog.exec();
            });
        }
        else
        {
            QTimer::singleShot(0, this, [this]() { QMessageBox::aboutQt(this); });
        }
    }
    return QDialog::eventFilter(watched, event);
}

void SettingsDialog::BuildUi()
{
    setWindowTitle(QStringLiteral("TokenViewer 설정"));
    QVBoxLayout* pRoot = new QVBoxLayout(this);
    QFormLayout* pForm = new QFormLayout();

    m_pIntervalCombo = new QComboBox(this);
    m_pIntervalCombo->setObjectName(QStringLiteral("intervalCombo"));
    for (const int iMinutes : Settings::GetAllowedIntervalMinutes())
    {
        m_pIntervalCombo->addItem(QStringLiteral("%1분").arg(iMinutes), iMinutes);
    }
    QLabel* pIntervalHint = new QLabel(QStringLiteral("드롭다운을 열 때 30초가 지났으면 바로 갱신합니다."), this);
    QFont fontHint = pIntervalHint->font();
    fontHint.setPixelSize(kHintPixelSize);
    pIntervalHint->setFont(fontHint);

    m_pWarnCombo = new QComboBox(this);
    m_pWarnCombo->setObjectName(QStringLiteral("warnCombo"));
    for (int iPercent = Settings::kMinWarnPercent; iPercent <= Settings::kMaxWarnPercent; iPercent += Settings::kPercentStep)
    {
        m_pWarnCombo->addItem(QStringLiteral("%1%").arg(iPercent), iPercent);
    }
    m_pCriticalCombo = new QComboBox(this);
    m_pCriticalCombo->setObjectName(QStringLiteral("criticalCombo"));
    m_pLaunchCheck = new QCheckBox(QStringLiteral("로그인 시 자동 실행"), this);
    m_pLaunchCheck->setObjectName(QStringLiteral("launchCheck"));

    pForm->addRow(QStringLiteral("한도 갱신 주기"), m_pIntervalCombo);
    pForm->addRow(QString(), pIntervalHint);
    pForm->addRow(QStringLiteral("주황 경고"), m_pWarnCombo);
    pForm->addRow(QStringLiteral("빨강 경고"), m_pCriticalCombo);
    pForm->addRow(QStringLiteral("시작"), m_pLaunchCheck);
    pRoot->addLayout(pForm);

    QHBoxLayout* pBottom = new QHBoxLayout();
    m_pLicenseButton = new QPushButton(QStringLiteral("오픈소스 라이선스"), this);
    m_pLicenseButton->setObjectName(QStringLiteral("licenseButton"));
    m_pAboutQtButton = new QPushButton(QStringLiteral("Qt 정보"), this);
    m_pLicenseButton->installEventFilter(this);
    m_pAboutQtButton->installEventFilter(this);
    QLabel* pVersion = new QLabel(QStringLiteral("v%1").arg(QCoreApplication::applicationVersion()), this);
    pBottom->addWidget(m_pLicenseButton);
    pBottom->addWidget(m_pAboutQtButton);
    pBottom->addStretch(1);
    pBottom->addWidget(pVersion);
    pRoot->addLayout(pBottom);

    connect(m_pIntervalCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]()
    {
        if (m_bLoading)
        {
            return;
        }
        m_kpSettings->SetRefreshIntervalMinutes(m_pIntervalCombo->currentData().toInt());
        emit m_observers.SettingsChanged();
    });
    connect(m_pWarnCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]()
    {
        if (m_bLoading)
        {
            return;
        }
        m_kpSettings->SetWarnPercent(m_pWarnCombo->currentData().toInt());
        FillCriticalChoices();
        emit m_observers.SettingsChanged();
    });
    connect(m_pCriticalCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]()
    {
        if (m_bLoading)
        {
            return;
        }
        m_kpSettings->SetCriticalPercent(m_pCriticalCombo->currentData().toInt());
        emit m_observers.SettingsChanged();
    });
    connect(m_pLaunchCheck, &QCheckBox::toggled, this, [this](bool checked)
    {
        if (m_bLoading)
        {
            return;
        }
        m_kpSettings->SetLaunchAtLogin(checked);
        emit m_observers.SettingsChanged();
    });
}

void SettingsDialog::FillCriticalChoices()
{
    const bool bWasLoading = m_bLoading;
    m_bLoading = true;
    m_pCriticalCombo->clear();
    for (int iPercent = m_kpSettings->GetWarnPercent() + Settings::kPercentStep; iPercent <= Settings::kMaxPercent; iPercent += Settings::kPercentStep)
    {
        m_pCriticalCombo->addItem(QStringLiteral("%1%").arg(iPercent), iPercent);
    }
    m_pCriticalCombo->setCurrentIndex(m_pCriticalCombo->findData(m_kpSettings->GetCriticalPercent()));
    m_bLoading = bWasLoading;
}
