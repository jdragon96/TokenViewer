#include "Analysis/PriceTable.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>

namespace
{
constexpr double kTokensPerPriceUnit = 1000000.0;
}

PriceTable::PriceTable()
    : m_vecModels()
    , m_strAsOf()
{
}

PriceTable::~PriceTable() = default;

bool PriceTable::LoadFromJson(const QByteArray& json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
    {
        return false;
    }
    const QJsonObject objRoot = doc.object();
    QVector<ModelPrice> vecModels;
    const QJsonArray arrModels = objRoot.value(QStringLiteral("models")).toArray();
    for (const QJsonValue& jvModel : arrModels)
    {
        const QJsonObject objModel = jvModel.toObject();
        ModelPrice price;
        price.m_strId = objModel.value(QStringLiteral("id")).toString();
        price.m_strFamily = objModel.value(QStringLiteral("family")).toString();
        price.m_dInput = objModel.value(QStringLiteral("input")).toDouble();
        price.m_dOutput = objModel.value(QStringLiteral("output")).toDouble();
        price.m_dCacheWrite5m = objModel.value(QStringLiteral("cacheWrite5m")).toDouble();
        price.m_dCacheWrite1h = objModel.value(QStringLiteral("cacheWrite1h")).toDouble();
        price.m_dCacheRead = objModel.value(QStringLiteral("cacheRead")).toDouble();
        price.m_dFastMultiplier = objModel.value(QStringLiteral("fastMultiplier")).toDouble(1.0);
        if (!price.m_strId.isEmpty())
        {
            vecModels.append(price);
        }
    }
    if (vecModels.isEmpty())
    {
        return false;
    }
    m_vecModels = vecModels;
    m_strAsOf = objRoot.value(QStringLiteral("asOf")).toString();
    return true;
}

PriceQuote PriceTable::FindPrice(const QString& modelId) const
{
    static const QRegularExpression reDateSuffix(QStringLiteral("^-\\d{8}$"));
    PriceQuote quote;
    for (const ModelPrice& price : m_vecModels)
    {
        const bool bSame = modelId == price.m_strId;
        const bool bDated = modelId.startsWith(price.m_strId) && reDateSuffix.match(modelId.mid(price.m_strId.size())).hasMatch();
        if (bSame || bDated)
        {
            quote.m_eMatch = EPriceMatch::EXACT;
            quote.m_price = price;
            return quote;
        }
    }

    const QStringList lstParts = modelId.split(QLatin1Char('-'));
    for (const ModelPrice& price : m_vecModels)
    {
        if (!price.m_strFamily.isEmpty() && lstParts.contains(price.m_strFamily))
        {
            quote.m_eMatch = EPriceMatch::FAMILY;
            quote.m_price = price;
            return quote;
        }
    }
    return quote;
}

CostResult PriceTable::CalculateCost(const TokenRecord& record) const
{
    CostResult result;
    const PriceQuote quote = FindPrice(record.m_strModel);
    if (quote.m_eMatch == EPriceMatch::NONE)
    {
        result.m_bUnpriced = true;
        return result;
    }

    const ModelPrice& price = quote.m_price;
    double dUsd = (record.m_llInput * price.m_dInput
        + record.m_llOutput * price.m_dOutput
        + record.m_llCacheWrite5m * price.m_dCacheWrite5m
        + record.m_llCacheWrite1h * price.m_dCacheWrite1h
        + record.m_llCacheRead * price.m_dCacheRead) / kTokensPerPriceUnit;
    if (record.m_bFast)
    {
        dUsd *= price.m_dFastMultiplier;
    }
    result.m_dUsd = dUsd;
    result.m_bApproximate = quote.m_eMatch == EPriceMatch::FAMILY;
    return result;
}

QString PriceTable::GetAsOf() const
{
    return m_strAsOf;
}

int PriceTable::GetModelCount() const
{
    return m_vecModels.size();
}
