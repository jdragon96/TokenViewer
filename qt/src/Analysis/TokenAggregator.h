#pragma once

#include "Analysis/PriceTable.h"
#include "Model/Breakdown.h"
#include "Model/TokenRecord.h"

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QVector>

struct AggregateQuery
{
    EBreakdownDimension m_eDimension = EBreakdownDimension::PROJECT;
    QDateTime m_dtFrom;
    // Rows shown, the "기타 N개" row included: more groups than this keep m_iMaxRows - 1 by name and fold the rest.
    int m_iMaxRows = 4;
};

class TokenAggregator
{
public:
    static Breakdown Aggregate(const LogSnapshot& snapshot, const PriceTable& priceTable, const AggregateQuery& query);
    static QDateTime CalculatePeriodStart(EBreakdownPeriod period, const QDateTime& now, const QDateTime& fiveHourResetsAt);
    static QString FormatModelName(const QString& modelId);
    static QString NormalizeProjectPath(const QString& projectPath);

private:
    struct ProjectInfo
    {
        QString m_strLabel;
        QString m_strDisplayPath;
    };

private:
    static QHash<QString, ProjectInfo> BuildProjectInfos(const QVector<const TokenRecord*>& records);
};
