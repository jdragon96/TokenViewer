#pragma once

#include "UI/TrayIconPainter.h"

#include <QObject>
#include <QTimer>

#include <memory>

class LogScanThread;
class Observers;
class PriceTable;
class QSettings;
class Settings;
class SystemStatus;
class TrayIcon;
class UsageFetchThread;
class UsagePopup;
struct FetchResult;

class Application : public QObject
{
    Q_OBJECT

public:
    Application();
    ~Application();

public:
    void Start();

private:
    void ConnectSignals();
    void HandleFetchResult(const FetchResult& result);
    void UpdateTrayIcon();
    void TogglePopup();
    void ApplySettings();
    TrayIconState BuildTrayIconState() const;
    void WriteBadResponseLog(const FetchResult& result) const;

private:
    std::unique_ptr<QSettings> m_upStoredSettings;
    std::unique_ptr<Settings> m_upSettings;
    std::unique_ptr<SystemStatus> m_upSystemStatus;
    std::unique_ptr<Observers> m_upObservers;
    std::unique_ptr<PriceTable> m_upPriceTable;
    std::unique_ptr<TrayIcon> m_upTrayIcon;
    std::unique_ptr<UsagePopup> m_upPopup;
    std::unique_ptr<UsageFetchThread> m_upFetchThread;
    std::unique_ptr<LogScanThread> m_upLogScanThread;
    QTimer m_timerStaleCheck;
    TrayIconState m_lastTrayState;
    bool m_bHasTrayState;
};
