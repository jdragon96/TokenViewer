#include "Analysis/TokenAggregator.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace
{
constexpr qint64 kFiveHourSecs = 5 * 60 * 60;
constexpr int kSevenDays = 7;
constexpr int kThirtyDays = 30;
const QString kUnknownProject = QStringLiteral("(알 수 없음)");
const QString kSessionTimeFormat = QStringLiteral("M/d HH:mm");

struct GroupAccumulator
{
    BreakdownRow m_row;
    int m_iPricedCount = 0;
    int m_iUnpricedCount = 0;
    QDateTime m_dtFirstSeen;
    QString m_strProjectPath;
};

qint64 SumTokens(const BreakdownRow& row)
{
    return row.m_llInput + row.m_llOutput + row.m_llCacheWrite + row.m_llCacheRead;
}

bool IsRankedBefore(const BreakdownRow& lhs, const BreakdownRow& rhs)
{
    if (lhs.m_dCostUsd != rhs.m_dCostUsd)
    {
        return lhs.m_dCostUsd > rhs.m_dCostUsd;
    }
    const qint64 llLhs = SumTokens(lhs);
    const qint64 llRhs = SumTokens(rhs);
    if (llLhs != llRhs)
    {
        return llLhs > llRhs;
    }
    return lhs.m_strLabel < rhs.m_strLabel;
}

void AddRow(BreakdownRow& target, const BreakdownRow& source)
{
    target.m_dCostUsd += source.m_dCostUsd;
    target.m_bApproximate = target.m_bApproximate || source.m_bApproximate || source.m_bUnpriced;
    target.m_llInput += source.m_llInput;
    target.m_llOutput += source.m_llOutput;
    target.m_llCacheWrite += source.m_llCacheWrite;
    target.m_llCacheRead += source.m_llCacheRead;
}

QString LabelOrUnknown(const QString& label)
{
    return label.isEmpty() ? kUnknownProject : label;
}
}

Breakdown TokenAggregator::Aggregate(const LogSnapshot& snapshot, const PriceTable& priceTable, const AggregateQuery& query)
{
    QVector<const TokenRecord*> vecRecords;
    for (const TokenRecord& record : snapshot.m_vecRecords)
    {
        if (!query.m_dtFrom.isValid() || record.m_dtTimestamp >= query.m_dtFrom)
        {
            vecRecords.append(&record);
        }
    }
    const QHash<QString, QString> hashProjectLabels = BuildProjectLabels(vecRecords);

    QHash<QString, GroupAccumulator> hashGroups;
    for (const TokenRecord* pRecord : vecRecords)
    {
        QString strGroupKey;
        switch (query.m_eDimension)
        {
        case EBreakdownDimension::PROJECT:
            strGroupKey = pRecord->m_strProjectPath;
            break;
        case EBreakdownDimension::MODEL:
            strGroupKey = pRecord->m_strModel;
            break;
        case EBreakdownDimension::SESSION:
            strGroupKey = pRecord->m_strSessionId;
            break;
        }

        GroupAccumulator& group = hashGroups[strGroupKey];
        const CostResult cost = priceTable.CalculateCost(*pRecord);
        group.m_row.m_dCostUsd += cost.m_dUsd;
        group.m_row.m_bApproximate = group.m_row.m_bApproximate || cost.m_bApproximate;
        if (cost.m_bUnpriced)
        {
            ++group.m_iUnpricedCount;
        }
        else
        {
            ++group.m_iPricedCount;
        }
        group.m_row.m_llInput += pRecord->m_llInput;
        group.m_row.m_llOutput += pRecord->m_llOutput;
        group.m_row.m_llCacheWrite += pRecord->m_llCacheWrite5m + pRecord->m_llCacheWrite1h;
        group.m_row.m_llCacheRead += pRecord->m_llCacheRead;
        if (!group.m_dtFirstSeen.isValid() || pRecord->m_dtTimestamp < group.m_dtFirstSeen)
        {
            group.m_dtFirstSeen = pRecord->m_dtTimestamp;
            group.m_strProjectPath = pRecord->m_strProjectPath;
        }
    }

    QVector<BreakdownRow> vecRows;
    for (auto it = hashGroups.begin(); it != hashGroups.end(); ++it)
    {
        GroupAccumulator& group = it.value();
        BreakdownRow& row = group.m_row;
        row.m_strDetail = it.key();
        row.m_bUnpriced = group.m_iPricedCount == 0;
        row.m_bApproximate = row.m_bApproximate || (group.m_iPricedCount > 0 && group.m_iUnpricedCount > 0);
        switch (query.m_eDimension)
        {
        case EBreakdownDimension::PROJECT:
            row.m_strLabel = LabelOrUnknown(hashProjectLabels.value(it.key()));
            break;
        case EBreakdownDimension::MODEL:
            row.m_strLabel = FormatModelName(it.key());
            break;
        case EBreakdownDimension::SESSION:
        {
            const QString strTitle = snapshot.m_hashSessionTitles.value(it.key());
            row.m_strLabel = !strTitle.isEmpty()
                ? strTitle
                : LabelOrUnknown(hashProjectLabels.value(group.m_strProjectPath)) + QStringLiteral(" · ")
                    + group.m_dtFirstSeen.toLocalTime().toString(kSessionTimeFormat);
            break;
        }
        }
        vecRows.append(row);
    }
    std::sort(vecRows.begin(), vecRows.end(), IsRankedBefore);

    Breakdown breakdown;
    for (int iIndex = 0; iIndex < vecRows.size(); ++iIndex)
    {
        breakdown.m_dTotalUsd += vecRows[iIndex].m_dCostUsd;
        if (iIndex < query.m_iTopCount)
        {
            breakdown.m_vecRows.append(vecRows[iIndex]);
        }
        else
        {
            AddRow(breakdown.m_otherRow, vecRows[iIndex]);
            ++breakdown.m_iOtherCount;
        }
    }
    if (breakdown.m_iOtherCount > 0)
    {
        breakdown.m_otherRow.m_strLabel = QStringLiteral("기타 %1개").arg(breakdown.m_iOtherCount);
    }
    return breakdown;
}

QDateTime TokenAggregator::CalculatePeriodStart(EBreakdownPeriod period, const QDateTime& now, const QDateTime& fiveHourResetsAt)
{
    switch (period)
    {
    case EBreakdownPeriod::FIVE_HOUR_WINDOW:
        return fiveHourResetsAt.isValid() ? fiveHourResetsAt.addSecs(-kFiveHourSecs).toUTC() : now.addSecs(-kFiveHourSecs).toUTC();
    case EBreakdownPeriod::TODAY:
        return QDateTime(now.toLocalTime().date(), QTime(0, 0), Qt::LocalTime).toUTC();
    case EBreakdownPeriod::SEVEN_DAYS:
        return now.addDays(-kSevenDays).toUTC();
    case EBreakdownPeriod::THIRTY_DAYS:
        break;
    }
    return now.addDays(-kThirtyDays).toUTC();
}

QString TokenAggregator::FormatModelName(const QString& modelId)
{
    static const QRegularExpression reModel(QStringLiteral("^claude-([a-z]+)((?:-\\d{1,2})+)(?:-\\d{8})?$"));
    const QRegularExpressionMatch match = reModel.match(modelId);
    if (!match.hasMatch())
    {
        return modelId;
    }
    QString strFamily = match.captured(1);
    strFamily[0] = strFamily[0].toUpper();
    QString strVersion = match.captured(2).mid(1);
    strVersion.replace(QLatin1Char('-'), QLatin1Char('.'));
    return strFamily + QLatin1Char(' ') + strVersion;
}

QHash<QString, QString> TokenAggregator::BuildProjectLabels(const QVector<const TokenRecord*>& records)
{
    QHash<QString, QSet<QString>> hashPathsByName;
    for (const TokenRecord* pRecord : records)
    {
        hashPathsByName[QFileInfo(pRecord->m_strProjectPath).fileName()].insert(pRecord->m_strProjectPath);
    }

    QHash<QString, QString> hashLabels;
    for (auto it = hashPathsByName.cbegin(); it != hashPathsByName.cend(); ++it)
    {
        for (const QString& strPath : it.value())
        {
            if (it.value().size() == 1)
            {
                hashLabels.insert(strPath, it.key());
                continue;
            }
            // Two projects share a folder name: prefix the parent folder to tell them apart.
            const QFileInfo fiPath(strPath);
            hashLabels.insert(strPath, QFileInfo(fiPath.path()).fileName() + QLatin1Char('/') + fiPath.fileName());
        }
    }
    return hashLabels;
}
