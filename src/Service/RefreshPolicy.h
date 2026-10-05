#pragma once

#include "Model/UsageLimits.h"

struct RefreshDecision
{
    int m_iDelayMs = 0;
    bool m_bAutoRetry = true;
};

class RefreshPolicy
{
public:
    static constexpr int kMaxBackoffMs = 30 * 60 * 1000;

public:
    static RefreshDecision DecideNext(EFetchStatus status, int baseIntervalMs, int previousDelayMs);
};
