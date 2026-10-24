#pragma once

#include "Model/TokenRecord.h"
#include "Model/UsageLimits.h"

#include <QDateTime>
#include <QString>

enum class ELimitDisplayState
{
    EMPTY,
    NORMAL,
    STALE,
};

class SystemStatus
{
public:
    static constexpr qint64 kStaleAfterSecs = 15 * 60;
    static constexpr qint64 kRefreshOnOpenAfterSecs = 30;

public:
    SystemStatus();
    ~SystemStatus();

public:
    void ApplyFetchResult(const FetchResult& result, const QDateTime& now);
    void ApplyLogSnapshot(const LogSnapshot& snapshot);
    ELimitDisplayState GetLimitDisplayState(const QDateTime& now) const;
    bool IsRefreshDueOnOpen(const QDateTime& now) const;

public:
    bool HasLimits() const;
    const UsageLimits& GetLimits() const;
    QString GetPlanLabel() const;
    EFetchStatus GetLastStatus() const;
    QString GetLastDetail() const;
    QDateTime GetLastAttemptAt() const;
    QDateTime GetLastSuccessAt() const;
    const LogSnapshot& GetLogSnapshot() const;

private:
    bool m_bHasLimits;
    UsageLimits m_limits;
    QString m_strPlanLabel;
    EFetchStatus m_eLastStatus;
    QString m_strLastDetail;
    QDateTime m_dtLastAttemptAt;
    QDateTime m_dtLastSuccessAt;
    LogSnapshot m_logSnapshot;
};
