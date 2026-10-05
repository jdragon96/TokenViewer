#pragma once

#include <QDateTime>
#include <QString>

class Formatters
{
public:
    static QString FormatUsd(double usd, bool approximate);
    static QString FormatTokens(qint64 tokens);
    static QString FormatResetCountdown(const QDateTime& resetsAt, const QDateTime& now);
    static QString FormatResetDay(const QDateTime& resetsAt, const QDateTime& now);
    static QString FormatAge(const QDateTime& past, const QDateTime& now);
    static QString FormatPercent(double percent);
};
