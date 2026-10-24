#pragma once

#include <QDateTime>

// Detects a wall-clock gap between ticks, which means the Mac slept in between.
class ClockJumpDetector
{
public:
    explicit ClockJumpDetector(qint64 thresholdMs);

public:
    bool CheckTick(const QDateTime& now);

private:
    qint64 m_llThresholdMs;
    QDateTime m_dtLastTick;
};
