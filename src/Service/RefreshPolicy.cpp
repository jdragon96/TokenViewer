#include "Service/RefreshPolicy.h"

#include <algorithm>

RefreshDecision RefreshPolicy::DecideNext(EFetchStatus status, int baseIntervalMs, int previousDelayMs)
{
    RefreshDecision decision;
    decision.m_iDelayMs = baseIntervalMs;
    switch (status)
    {
    case EFetchStatus::RATE_LIMITED:
        decision.m_iDelayMs = std::min(std::max(previousDelayMs, baseIntervalMs) * 2, kMaxBackoffMs);
        break;
    case EFetchStatus::KEYCHAIN_DENIED:
        // Retrying would pop the Keychain prompt every few minutes; wait for the user to ask.
        decision.m_bAutoRetry = false;
        break;
    default:
        break;
    }
    return decision;
}
