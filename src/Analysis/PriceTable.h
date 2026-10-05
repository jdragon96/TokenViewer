#pragma once

#include "Model/TokenRecord.h"

#include <QByteArray>
#include <QString>
#include <QVector>

struct ModelPrice
{
    QString m_strId;
    QString m_strFamily;
    double m_dInput = 0.0;
    double m_dOutput = 0.0;
    double m_dCacheWrite5m = 0.0;
    double m_dCacheWrite1h = 0.0;
    double m_dCacheRead = 0.0;
    double m_dFastMultiplier = 1.0;
};

enum class EPriceMatch
{
    EXACT,
    FAMILY,
    NONE,
};

struct PriceQuote
{
    EPriceMatch m_eMatch = EPriceMatch::NONE;
    ModelPrice m_price;
};

struct CostResult
{
    double m_dUsd = 0.0;
    bool m_bApproximate = false;
    bool m_bUnpriced = false;
};

class PriceTable
{
public:
    PriceTable();
    ~PriceTable();

public:
    bool LoadFromJson(const QByteArray& json);
    PriceQuote FindPrice(const QString& modelId) const;
    CostResult CalculateCost(const TokenRecord& record) const;

public:
    QString GetAsOf() const;
    int GetModelCount() const;

private:
    QVector<ModelPrice> m_vecModels;
    QString m_strAsOf;
};
