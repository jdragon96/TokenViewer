#include "Core/SystemStatus.h"

SystemStatus::SystemStatus()
    : m_bHasLimits(false)
    , m_limits()
    , m_strPlanLabel()
    , m_eLastStatus(EFetchStatus::OK)
    , m_strLastDetail()
    , m_dtLastAttemptAt()
    , m_dtLastSuccessAt()
    , m_logSnapshot()
{
}

SystemStatus::~SystemStatus() = default;

void SystemStatus::ApplyFetchResult(const FetchResult& result, const QDateTime& now)
{
    m_dtLastAttemptAt = now;
    m_eLastStatus = result.m_eStatus;
    m_strLastDetail = result.m_strDetail;
    if (!result.m_strPlanLabel.isEmpty())
    {
        m_strPlanLabel = result.m_strPlanLabel;
    }

    switch (result.m_eStatus)
    {
    case EFetchStatus::OK:
        m_bHasLimits = true;
        m_limits = result.m_limits;
        m_dtLastSuccessAt = now;
        break;
    case EFetchStatus::NOT_LOGGED_IN:
    case EFetchStatus::KEYCHAIN_DENIED:
    case EFetchStatus::BAD_RESPONSE:
        m_bHasLimits = false;
        m_limits = UsageLimits();
        break;
    case EFetchStatus::TOKEN_EXPIRED:
    case EFetchStatus::RATE_LIMITED:
    case EFetchStatus::NETWORK_ERROR:
    case EFetchStatus::SERVER_ERROR:
        // Keep the last values; GetLimitDisplayState dims them once they are old.
        break;
    }
}

void SystemStatus::ApplyLogSnapshot(const LogSnapshot& snapshot)
{
    m_logSnapshot = snapshot;
}

ELimitDisplayState SystemStatus::GetLimitDisplayState(const QDateTime& now) const
{
    if (!m_bHasLimits)
    {
        return ELimitDisplayState::EMPTY;
    }
    // Age alone decides, so values also dim after the Mac wakes from sleep.
    if (m_dtLastSuccessAt.secsTo(now) > kStaleAfterSecs)
    {
        return ELimitDisplayState::STALE;
    }
    return ELimitDisplayState::NORMAL;
}

bool SystemStatus::IsRefreshDueOnOpen(const QDateTime& now) const
{
    return !m_dtLastAttemptAt.isValid() || m_dtLastAttemptAt.secsTo(now) > kRefreshOnOpenAfterSecs;
}

bool SystemStatus::HasLimits() const
{
    return m_bHasLimits;
}

const UsageLimits& SystemStatus::GetLimits() const
{
    return m_limits;
}

QString SystemStatus::GetPlanLabel() const
{
    return m_strPlanLabel;
}

EFetchStatus SystemStatus::GetLastStatus() const
{
    return m_eLastStatus;
}

QString SystemStatus::GetLastDetail() const
{
    return m_strLastDetail;
}

QDateTime SystemStatus::GetLastAttemptAt() const
{
    return m_dtLastAttemptAt;
}

QDateTime SystemStatus::GetLastSuccessAt() const
{
    return m_dtLastSuccessAt;
}

const LogSnapshot& SystemStatus::GetLogSnapshot() const
{
    return m_logSnapshot;
}
