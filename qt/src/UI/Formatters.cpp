#include "UI/Formatters.h"

#include <QLocale>

#include <algorithm>

namespace
{
constexpr qint64 kSecsPerMinute = 60;
constexpr qint64 kSecsPerHour = 60 * 60;
constexpr qint64 kSecsPerDay = 24 * 60 * 60;
constexpr qint64 kThousand = 1000;
constexpr qint64 kHundredThousand = 100000;
constexpr qint64 kMillion = 1000000;
constexpr qint64 kTenMillion = 10000000;
constexpr double kSmallestCent = 0.005;
const QString kNoReset = QStringLiteral("리셋 시각 없음");
}

QString Formatters::FormatUsd(double usd, bool approximate)
{
    const QString strPrefix = approximate ? QStringLiteral("≈") : QString();
    if (usd > 0.0 && usd < kSmallestCent)
    {
        return strPrefix + QStringLiteral("<$0.01");
    }
    return strPrefix + QLatin1Char('$') + QLocale(QLocale::English).toString(usd, 'f', 2);
}

QString Formatters::FormatTokens(qint64 tokens)
{
    if (tokens < kThousand)
    {
        return QString::number(tokens);
    }
    if (tokens < kMillion)
    {
        return QString::number(static_cast<double>(tokens) / kThousand, 'f', tokens < kHundredThousand ? 1 : 0) + QLatin1Char('K');
    }
    return QString::number(static_cast<double>(tokens) / kMillion, 'f', tokens < kTenMillion ? 2 : 1) + QLatin1Char('M');
}

QString Formatters::FormatResetCountdown(const QDateTime& resetsAt, const QDateTime& now)
{
    if (!resetsAt.isValid())
    {
        return kNoReset;
    }
    const qint64 llSecs = now.secsTo(resetsAt);
    if (llSecs <= kSecsPerMinute)
    {
        return QStringLiteral("곧 리셋");
    }
    const qint64 llHours = llSecs / kSecsPerHour;
    const qint64 llMinutes = (llSecs % kSecsPerHour) / kSecsPerMinute;
    if (llHours > 0)
    {
        return QStringLiteral("%1:%2 후 리셋").arg(llHours).arg(llMinutes, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1분 후 리셋").arg(llMinutes);
}

QString Formatters::FormatResetDay(const QDateTime& resetsAt, const QDateTime& now)
{
    if (!resetsAt.isValid())
    {
        return kNoReset;
    }
    if (now.secsTo(resetsAt) < kSecsPerDay)
    {
        return FormatResetCountdown(resetsAt, now);
    }
    return QLocale(QLocale::Korean).toString(resetsAt.toLocalTime(), QStringLiteral("ddd H:mm")) + QStringLiteral(" 리셋");
}

QString Formatters::FormatAge(const QDateTime& past, const QDateTime& now)
{
    if (!past.isValid())
    {
        return QString();
    }
    const qint64 llSecs = std::max<qint64>(0, past.secsTo(now));
    if (llSecs < kSecsPerMinute)
    {
        return QStringLiteral("방금");
    }
    if (llSecs < kSecsPerHour)
    {
        return QStringLiteral("%1분 전").arg(llSecs / kSecsPerMinute);
    }
    return QStringLiteral("%1시간 전").arg(llSecs / kSecsPerHour);
}

QString Formatters::FormatPercent(double percent)
{
    return QString::number(qRound(std::clamp(percent, 0.0, 100.0))) + QLatin1Char('%');
}
