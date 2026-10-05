#include "Analysis/LogParser.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{
const QString kSyntheticModel = QStringLiteral("<synthetic>");
const QString kSubagentDirName = QStringLiteral("subagents");

qint64 ReadCount(const QJsonObject& object, const QString& key)
{
    return static_cast<qint64>(object.value(key).toDouble());
}
}

ParsedLine LogParser::ParseLine(const QByteArray& line, const QString& fallbackSessionId)
{
    ParsedLine parsed;
    // Most lines carry neither field; skip them before paying for a JSON parse.
    if (!line.contains("\"usage\"") && !line.contains("\"ai-title\""))
    {
        return parsed;
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
    {
        return parsed;
    }

    const QJsonObject objLine = doc.object();
    const QString strType = objLine.value(QStringLiteral("type")).toString();
    QString strSessionId = objLine.value(QStringLiteral("sessionId")).toString();
    if (strSessionId.isEmpty())
    {
        strSessionId = fallbackSessionId;
    }

    if (strType == QStringLiteral("ai-title"))
    {
        const QString strTitle = objLine.value(QStringLiteral("aiTitle")).toString().trimmed();
        if (!strTitle.isEmpty() && !strSessionId.isEmpty())
        {
            parsed.m_eKind = ELineKind::TITLE;
            parsed.m_strSessionId = strSessionId;
            parsed.m_strTitle = strTitle;
        }
        return parsed;
    }
    if (strType != QStringLiteral("assistant"))
    {
        return parsed;
    }

    const QJsonObject objMessage = objLine.value(QStringLiteral("message")).toObject();
    const QJsonObject objUsage = objMessage.value(QStringLiteral("usage")).toObject();
    const QString strModel = objMessage.value(QStringLiteral("model")).toString();
    if (objUsage.isEmpty() || strModel.isEmpty() || strModel == kSyntheticModel)
    {
        return parsed;
    }

    const QDateTime dtTimestamp = QDateTime::fromString(objLine.value(QStringLiteral("timestamp")).toString(), Qt::ISODateWithMs);
    if (!dtTimestamp.isValid())
    {
        return parsed;
    }

    // Streaming writes the same response 2-5 times; message id + request id identifies it.
    QString strKey = objMessage.value(QStringLiteral("id")).toString();
    if (strKey.isEmpty())
    {
        strKey = objLine.value(QStringLiteral("uuid")).toString();
    }
    if (strKey.isEmpty())
    {
        return parsed;
    }
    const QString strRequestId = objLine.value(QStringLiteral("requestId")).toString();
    if (!strRequestId.isEmpty())
    {
        strKey += QLatin1Char('|') + strRequestId;
    }

    TokenRecord& record = parsed.m_record;
    record.m_strKey = strKey;
    record.m_dtTimestamp = dtTimestamp.toUTC();
    record.m_strSessionId = strSessionId;
    record.m_strProjectPath = objLine.value(QStringLiteral("cwd")).toString();
    record.m_strModel = strModel;
    record.m_bFast = objUsage.value(QStringLiteral("speed")).toString() == QStringLiteral("fast");
    record.m_llInput = ReadCount(objUsage, QStringLiteral("input_tokens"));
    record.m_llOutput = ReadCount(objUsage, QStringLiteral("output_tokens"));
    record.m_llCacheRead = ReadCount(objUsage, QStringLiteral("cache_read_input_tokens"));

    const QJsonObject objCacheCreation = objUsage.value(QStringLiteral("cache_creation")).toObject();
    const bool bHasSplit = objCacheCreation.contains(QStringLiteral("ephemeral_5m_input_tokens"))
        || objCacheCreation.contains(QStringLiteral("ephemeral_1h_input_tokens"));
    if (bHasSplit)
    {
        record.m_llCacheWrite5m = ReadCount(objCacheCreation, QStringLiteral("ephemeral_5m_input_tokens"));
        record.m_llCacheWrite1h = ReadCount(objCacheCreation, QStringLiteral("ephemeral_1h_input_tokens"));
    }
    else
    {
        record.m_llCacheWrite5m = ReadCount(objUsage, QStringLiteral("cache_creation_input_tokens"));
    }

    parsed.m_eKind = ELineKind::USAGE;
    return parsed;
}

QString LogParser::FindSessionIdFromPath(const QString& filePath)
{
    const QFileInfo fiFile(filePath);
    const QFileInfo fiParent(fiFile.path());
    if (fiParent.fileName() == kSubagentDirName)
    {
        return QFileInfo(fiParent.path()).fileName();
    }
    return fiFile.completeBaseName();
}
