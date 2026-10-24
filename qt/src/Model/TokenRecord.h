#pragma once

#include <QDateTime>
#include <QHash>
#include <QMetaType>
#include <QString>
#include <QVector>

struct TokenRecord
{
    QString m_strKey;
    QDateTime m_dtTimestamp;
    QString m_strSessionId;
    QString m_strProjectPath;
    QString m_strModel;
    bool m_bFast = false;
    qint64 m_llInput = 0;
    qint64 m_llOutput = 0;
    qint64 m_llCacheWrite5m = 0;
    qint64 m_llCacheWrite1h = 0;
    qint64 m_llCacheRead = 0;
};

struct LogSnapshot
{
    QVector<TokenRecord> m_vecRecords;
    QHash<QString, QString> m_hashSessionTitles;
};

Q_DECLARE_METATYPE(LogSnapshot)
