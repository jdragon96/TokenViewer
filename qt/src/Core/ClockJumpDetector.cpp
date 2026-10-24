#include "Core/ClockJumpDetector.h"

ClockJumpDetector::ClockJumpDetector(qint64 thresholdMs)
    : m_llThresholdMs(thresholdMs)
    , m_dtLastTick()
{
}

bool ClockJumpDetector::CheckTick(const QDateTime& now)
{
    const bool bJumped = m_dtLastTick.isValid() && m_dtLastTick.msecsTo(now) > m_llThresholdMs;
    m_dtLastTick = now;
    return bJumped;
}
