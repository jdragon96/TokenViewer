#pragma once

#include <QString>
#include <QVector>

enum class EBreakdownDimension
{
    PROJECT = 0,
    MODEL = 1,
    SESSION = 2,
};

enum class EBreakdownPeriod
{
    FIVE_HOUR_WINDOW = 0,
    TODAY = 1,
    SEVEN_DAYS = 2,
    THIRTY_DAYS = 3,
};

struct BreakdownRow
{
    QString m_strLabel;
    QString m_strDetail;
    double m_dCostUsd = 0.0;
    bool m_bApproximate = false;
    bool m_bUnpriced = false;
    qint64 m_llInput = 0;
    qint64 m_llOutput = 0;
    qint64 m_llCacheWrite = 0;
    qint64 m_llCacheRead = 0;
};

struct Breakdown
{
    QVector<BreakdownRow> m_vecRows;
    int m_iOtherCount = 0;
    BreakdownRow m_otherRow;
    double m_dTotalUsd = 0.0;
};
