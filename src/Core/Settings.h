#pragma once

#include "Model/Breakdown.h"

#include <QVector>

class QSettings;

class Settings
{
public:
    static constexpr int kDefaultIntervalMinutes = 3;
    static constexpr int kDefaultWarnPercent = 70;
    static constexpr int kDefaultCriticalPercent = 90;
    static constexpr int kMinWarnPercent = 50;
    static constexpr int kMaxWarnPercent = 95;
    static constexpr int kMaxPercent = 100;
    static constexpr int kPercentStep = 5;

public:
    explicit Settings(QSettings* settings);
    ~Settings();

public:
    static QVector<int> GetAllowedIntervalMinutes();

public:
    int GetRefreshIntervalMinutes() const;
    void SetRefreshIntervalMinutes(int minutes);
    int GetWarnPercent() const;
    void SetWarnPercent(int percent);
    int GetCriticalPercent() const;
    void SetCriticalPercent(int percent);
    bool GetLaunchAtLogin() const;
    void SetLaunchAtLogin(bool enabled);
    EBreakdownDimension GetDimension() const;
    void SetDimension(EBreakdownDimension dimension);
    EBreakdownPeriod GetPeriod() const;
    void SetPeriod(EBreakdownPeriod period);

private:
    QSettings* m_kpSettings;
};
