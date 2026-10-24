#pragma once

#include "Model/TokenRecord.h"

#include <QByteArray>
#include <QString>

enum class ELineKind
{
    IGNORED,
    USAGE,
    TITLE,
};

struct ParsedLine
{
    ELineKind m_eKind = ELineKind::IGNORED;
    TokenRecord m_record;
    QString m_strSessionId;
    QString m_strTitle;
};

class LogParser
{
public:
    static ParsedLine ParseLine(const QByteArray& line, const QString& fallbackSessionId);
    static QString FindSessionIdFromPath(const QString& filePath);
};
