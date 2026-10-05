#pragma once

#include "Model/TokenRecord.h"

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QString>

#include <functional>

class LogStore
{
public:
    static constexpr qint64 kChunkBytes = 4 * 1024 * 1024;
    static constexpr int kRetentionDays = 31;

public:
    LogStore();
    ~LogStore();

public:
    bool ScanDirectory(const QString& rootPath, const QDateTime& now, const std::function<bool()>& isAlive = {});
    LogSnapshot CreateSnapshot() const;

public:
    int GetRecordCount() const;

private:
    struct FileCursor
    {
        qint64 m_llOffset = 0;
        QSet<QString> m_setKeys;
    };

private:
    bool ReadFile(const QString& filePath, FileCursor& cursor);
    void ForgetFile(const QString& filePath);
    bool PruneOldRecords(const QDateTime& now);

private:
    QHash<QString, FileCursor> m_hashCursors;
    QHash<QString, TokenRecord> m_hashRecords;
    QHash<QString, QString> m_hashSessionTitles;
};
