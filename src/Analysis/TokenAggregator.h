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
    int m_iTopCount = 5;
};

class TokenAggregator
{
public:
    static Breakdown Aggregate(const LogSnapshot& snapshot, const PriceTable& priceTable, const AggregateQuery& query);
    static QDateTime CalculatePeriodStart(EBreakdownPeriod period, const QDateTime& now, const QDateTime& fiveHourResetsAt);
    static QString FormatModelName(const QString& modelId);

private:
    static QHash<QString, QString> BuildProjectLabels(const QVector<const TokenRecord*>& records);
};
