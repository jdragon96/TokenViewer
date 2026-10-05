#include "Core/Application.h"

#include "Analysis/PriceTable.h"
#include "Core/Observers.h"
#include "Core/Settings.h"
#include "Core/SystemStatus.h"
#include "Service/LoginItem.h"
#include "Service/UsageApiClient.h"
#include "Thread/LogScanThread.h"
#include "Thread/UsageFetchThread.h"
#include "UI/SettingsDialog.h"
#include "UI/TrayIcon.h"
#include "UI/UsagePopup.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSettings>

namespace
{
constexpr int kMsPerMinute = 60 * 1000;
constexpr int kLogScanIntervalMs = 60 * 1000;
constexpr int kStaleCheckMs = 60 * 1000;
const QString kPricesResource = QStringLiteral(":/prices.json");
const QString kProjectsDirectory = QStringLiteral("/.claude/projects");
const QString kLogDirectory = QStringLiteral("/Library/Logs/TokenViewer");
const QString kBadResponseLogName = QStringLiteral("/usage-response.log");
}

Application::Application()
    : QObject(nullptr)
    , m_upStoredSettings(std::make_unique<QSettings>())
    , m_upSettings(std::make_unique<Settings>(m_upStoredSettings.get()))
    , m_upSystemStatus(std::make_unique<SystemStatus>())
    , m_upObservers(std::make_unique<Observers>())
    , m_upPriceTable(std::make_unique<PriceTable>())
    , m_upTrayIcon(std::make_unique<TrayIcon>())
    , m_upPopup(std::make_unique<UsagePopup>(*m_upObservers, m_upSystemStatus.get(), m_upSettings.get(), m_upPriceTable.get()))
    , m_upFetchThread(std::make_unique<UsageFetchThread>(
          []() { return UsageApiClient::FetchWithStoredCredential(QDateTime::currentDateTimeUtc()); },
          m_upSettings->GetRefreshIntervalMinutes() * kMsPerMinute))
    , m_upLogScanThread(std::make_unique<LogScanThread>(QDir::homePath() + kProjectsDirectory, kLogScanIntervalMs))
    , m_upSettingsDialog(nullptr)
    , m_timerStaleCheck()
    , m_lastTrayState()
    , m_bHasTrayState(false)
{
}

Application::~Application()
{
    // Stop the workers first; their queued signals target objects destroyed below.
    m_upFetchThread->Stop();
    m_upLogScanThread->Stop();
}

void Application::Start()
{
    QFile filePrices(kPricesResource);
    if (filePrices.open(QIODevice::ReadOnly))
    {
        m_upPriceTable->LoadFromJson(filePrices.readAll());
    }

    ConnectSignals();
    ApplySettings();
    m_upTrayIcon->Show();
    m_upFetchThread->Start();
    m_upLogScanThread->Start();
    m_timerStaleCheck.start(kStaleCheckMs);
}

void Application::ConnectSignals()
{
    connect(m_upFetchThread.get(), &UsageFetchThread::FetchCompleted, this, &Application::HandleFetchResult);
    connect(m_upLogScanThread.get(), &LogScanThread::SnapshotReady, this, [this](const LogSnapshot& snapshot)
    {
        m_upSystemStatus->ApplyLogSnapshot(snapshot);
        emit m_upObservers->LogSnapshotChanged();
    });
    connect(m_upTrayIcon.get(), &TrayIcon::Clicked, this, &Application::TogglePopup);
    connect(m_upObservers.get(), &Observers::LimitsChanged, this, &Application::UpdateTrayIcon);
    connect(m_upObservers.get(), &Observers::SettingsChanged, this, &Application::ApplySettings);
    connect(m_upObservers.get(), &Observers::RefreshRequested, this, [this]()
    {
        m_upFetchThread->RequestRefresh();
        m_upLogScanThread->RequestRescan();
    });
    connect(m_upObservers.get(), &Observers::QuitRequested, qApp, &QCoreApplication::quit);
    // Staleness depends on the clock, so re-check it even when nothing new arrives.
    connect(&m_timerStaleCheck, &QTimer::timeout, this, &Application::UpdateTrayIcon);
    connect(m_upObservers.get(), &Observers::SettingsWindowRequested, this, &Application::ShowSettingsDialog);
}

void Application::HandleFetchResult(const FetchResult& result)
{
    m_upSystemStatus->ApplyFetchResult(result, QDateTime::currentDateTimeUtc());
    if (result.m_eStatus == EFetchStatus::BAD_RESPONSE)
    {
        WriteBadResponseLog(result);
    }
    emit m_upObservers->LimitsChanged();
}

void Application::UpdateTrayIcon()
{
    const TrayIconState state = BuildTrayIconState();
    if (m_bHasTrayState && state == m_lastTrayState)
    {
        return;
    }
    m_upTrayIcon->SetState(state);
    m_lastTrayState = state;
    m_bHasTrayState = true;
}

void Application::TogglePopup()
{
    if (m_upPopup->isVisible())
    {
        m_upPopup->hide();
        return;
    }
    if (m_upPopup->WasJustHidden())
    {
        return;
    }
    if (m_upSystemStatus->IsRefreshDueOnOpen(QDateTime::currentDateTimeUtc()))
    {
        m_upFetchThread->RequestRefresh();
    }
    m_upLogScanThread->RequestRescan();
    m_upPopup->Refresh();
    m_upPopup->ShowBelow(m_upTrayIcon->GetAnchorGeometry());
}

void Application::ApplySettings()
{
    m_upFetchThread->SetIntervalMs(m_upSettings->GetRefreshIntervalMinutes() * kMsPerMinute);
    UpdateTrayIcon();
    if (m_upPopup->isVisible())
    {
        m_upPopup->Refresh();
    }
    LoginItem::Apply(m_upSettings->GetLaunchAtLogin(), QCoreApplication::applicationFilePath());
}

void Application::ShowSettingsDialog()
{
    if (!m_upSettingsDialog)
    {
        m_upSettingsDialog = std::make_unique<SettingsDialog>(*m_upObservers, m_upSettings.get());
    }
    m_upSettingsDialog->LoadFromSettings();
    m_upSettingsDialog->show();
    m_upSettingsDialog->raise();
    m_upSettingsDialog->activateWindow();
}

TrayIconState Application::BuildTrayIconState() const
{
    TrayIconState state;
    state.m_iWarnPercent = m_upSettings->GetWarnPercent();
    state.m_iCriticalPercent = m_upSettings->GetCriticalPercent();
    const ELimitDisplayState eDisplay = m_upSystemStatus->GetLimitDisplayState(QDateTime::currentDateTimeUtc());
    if (eDisplay == ELimitDisplayState::EMPTY)
    {
        return state;
    }
    const UsageLimits& limits = m_upSystemStatus->GetLimits();
    state.m_bFiveHourValid = limits.m_fiveHour.m_bValid;
    state.m_dFiveHourPercent = limits.m_fiveHour.m_dPercent;
    state.m_bSevenDayValid = limits.m_sevenDay.m_bValid;
    state.m_dSevenDayPercent = limits.m_sevenDay.m_dPercent;
    state.m_bStale = eDisplay == ELimitDisplayState::STALE;
    return state;
}

void Application::WriteBadResponseLog(const FetchResult& result) const
{
    // Only the response body is written; the request (and its token) never is.
    const QString strDirectory = QDir::homePath() + kLogDirectory;
    QDir().mkpath(strDirectory);
    QFile file(strDirectory + kBadResponseLogName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        return;
    }
    file.write(QStringLiteral("%1 HTTP %2 %3\n")
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate))
        .arg(result.m_iHttpStatus)
        .arg(result.m_strDetail)
        .toUtf8());
    file.write(result.m_baRawBody);
}
