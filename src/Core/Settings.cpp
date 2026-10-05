#include "Core/Settings.h"

#include <QSettings>
#include <QString>

#include <algorithm>

namespace
{
const QString kKeyInterval = QStringLiteral("refresh/intervalMinutes");
const QString kKeyWarn = QStringLiteral("alert/warnPercent");
const QString kKeyCritical = QStringLiteral("alert/criticalPercent");
const QString kKeyLaunchAtLogin = QStringLiteral("app/launchAtLogin");
const QString kKeyDimension = QStringLiteral("popup/dimension");
const QString kKeyPeriod = QStringLiteral("popup/period");
}

Settings::Settings(QSettings* settings)
    : m_kpSettings(settings)
{
}

Settings::~Settings() = default;

QVector<int> Settings::GetAllowedIntervalMinutes()
{
    return { 1, 3, 5, 10 };
}

int Settings::GetRefreshIntervalMinutes() const
{
    const int iValue = m_kpSettings->value(kKeyInterval, kDefaultIntervalMinutes).toInt();
    return GetAllowedIntervalMinutes().contains(iValue) ? iValue : kDefaultIntervalMinutes;
}

void Settings::SetRefreshIntervalMinutes(int minutes)
{
    m_kpSettings->setValue(kKeyInterval, minutes);
}

int Settings::GetWarnPercent() const
{
    const int iValue = m_kpSettings->value(kKeyWarn, kDefaultWarnPercent).toInt();
    const bool bValid = iValue >= kMinWarnPercent && iValue <= kMaxWarnPercent && iValue % kPercentStep == 0;
    return bValid ? iValue : kDefaultWarnPercent;
}

void Settings::SetWarnPercent(int percent)
{
    m_kpSettings->setValue(kKeyWarn, percent);
}

int Settings::GetCriticalPercent() const
{
    const int iWarn = GetWarnPercent();
    const int iValue = m_kpSettings->value(kKeyCritical, kDefaultCriticalPercent).toInt();
    if (iValue > iWarn && iValue <= kMaxPercent && iValue % kPercentStep == 0)
    {
        return iValue;
    }
    return kDefaultCriticalPercent > iWarn ? kDefaultCriticalPercent : std::min(iWarn + kPercentStep, kMaxPercent);
}

void Settings::SetCriticalPercent(int percent)
{
    m_kpSettings->setValue(kKeyCritical, percent);
}

bool Settings::GetLaunchAtLogin() const
{
    return m_kpSettings->value(kKeyLaunchAtLogin, true).toBool();
}

void Settings::SetLaunchAtLogin(bool enabled)
{
    m_kpSettings->setValue(kKeyLaunchAtLogin, enabled);
}

EBreakdownDimension Settings::GetDimension() const
{
    const int iValue = m_kpSettings->value(kKeyDimension, static_cast<int>(EBreakdownDimension::PROJECT)).toInt();
    if (iValue < static_cast<int>(EBreakdownDimension::PROJECT) || iValue > static_cast<int>(EBreakdownDimension::SESSION))
    {
        return EBreakdownDimension::PROJECT;
    }
    return static_cast<EBreakdownDimension>(iValue);
}

void Settings::SetDimension(EBreakdownDimension dimension)
{
    m_kpSettings->setValue(kKeyDimension, static_cast<int>(dimension));
}

EBreakdownPeriod Settings::GetPeriod() const
{
    const int iValue = m_kpSettings->value(kKeyPeriod, static_cast<int>(EBreakdownPeriod::FIVE_HOUR_WINDOW)).toInt();
    if (iValue < static_cast<int>(EBreakdownPeriod::FIVE_HOUR_WINDOW) || iValue > static_cast<int>(EBreakdownPeriod::THIRTY_DAYS))
    {
        return EBreakdownPeriod::FIVE_HOUR_WINDOW;
    }
    return static_cast<EBreakdownPeriod>(iValue);
}

void Settings::SetPeriod(EBreakdownPeriod period)
{
    m_kpSettings->setValue(kKeyPeriod, static_cast<int>(period));
}
