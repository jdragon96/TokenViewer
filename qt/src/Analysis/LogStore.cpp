#include "Analysis/LogStore.h"

#include "Analysis/LogParser.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

namespace
{
const QString kLogFilePattern = QStringLiteral("*.jsonl");
}

LogStore::LogStore()
    : m_hashCursors()
    , m_hashRecords()
    , m_hashSessionTitles()
{
}

LogStore::~LogStore() = default;

bool LogStore::ScanDirectory(const QString& rootPath, const QDateTime& now, const std::function<bool()>& isAlive)
{
    bool bChanged = false;
    const QDateTime dtCutoff = now.addDays(-kRetentionDays);
    QSet<QString> setSeen;

    QDirIterator itFile(rootPath, QStringList{ kLogFilePattern }, QDir::Files, QDirIterator::Subdirectories);
    while (itFile.hasNext())
    {
        if (isAlive && !isAlive())
        {
            return bChanged;
        }
        const QString strPath = itFile.next();
        const QFileInfo fiFile = itFile.fileInfo();
        setSeen.insert(strPath);

        auto itCursor = m_hashCursors.find(strPath);
        if (itCursor == m_hashCursors.end())
        {
            // Claude Code deletes transcripts after 30 days; skip anything older on first sight.
            if (fiFile.lastModified() < dtCutoff)
            {
                continue;
            }
            itCursor = m_hashCursors.insert(strPath, FileCursor());
        }

        FileCursor& cursor = itCursor.value();
        if (fiFile.size() < cursor.m_llOffset)
        {
            // Truncated or replaced: drop what this file contributed and read it again.
            for (const QString& strKey : cursor.m_setKeys)
            {
                m_hashRecords.remove(strKey);
            }
            cursor = FileCursor();
            bChanged = true;
        }
        if (fiFile.size() > cursor.m_llOffset)
        {
            bChanged = ReadFile(strPath, cursor) || bChanged;
        }
    }

    const QStringList lstTracked = m_hashCursors.keys();
    for (const QString& strPath : lstTracked)
    {
        if (!setSeen.contains(strPath))
        {
            ForgetFile(strPath);
            bChanged = true;
        }
    }

    return PruneOldRecords(now) || bChanged;
}

LogSnapshot LogStore::CreateSnapshot() const
{
    LogSnapshot snapshot;
    snapshot.m_vecRecords.reserve(m_hashRecords.size());
    for (const TokenRecord& record : m_hashRecords)
    {
        snapshot.m_vecRecords.append(record);
    }
    snapshot.m_hashSessionTitles = m_hashSessionTitles;
    return snapshot;
}

int LogStore::GetRecordCount() const
{
    return m_hashRecords.size();
}

bool LogStore::ReadFile(const QString& filePath, FileCursor& cursor)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly) || !file.seek(cursor.m_llOffset))
    {
        return false;
    }

    const QString strFallbackSessionId = LogParser::FindSessionIdFromPath(filePath);
    bool bChanged = false;
    QByteArray baPending;
    while (true)
    {
        const QByteArray baChunk = file.read(kChunkBytes);
        if (baChunk.isEmpty())
        {
            break;
        }
        baPending += baChunk;

        int iLineStart = 0;
        int iNewline = baPending.indexOf('\n');
        while (iNewline >= 0)
        {
            const ParsedLine parsed = LogParser::ParseLine(baPending.mid(iLineStart, iNewline - iLineStart), strFallbackSessionId);
            if (parsed.m_eKind == ELineKind::USAGE)
            {
                // Later copies of the same response replace earlier ones.
                m_hashRecords.insert(parsed.m_record.m_strKey, parsed.m_record);
                cursor.m_setKeys.insert(parsed.m_record.m_strKey);
                bChanged = true;
            }
            else if (parsed.m_eKind == ELineKind::TITLE)
            {
                m_hashSessionTitles.insert(parsed.m_strSessionId, parsed.m_strTitle);
                bChanged = true;
            }
            cursor.m_llOffset += (iNewline - iLineStart) + 1;
            iLineStart = iNewline + 1;
            iNewline = baPending.indexOf('\n', iLineStart);
        }
        // Keep the unfinished last line; the offset stays before it until Claude Code finishes writing it.
        baPending = baPending.mid(iLineStart);
    }
    return bChanged;
}

void LogStore::ForgetFile(const QString& filePath)
{
    const FileCursor cursor = m_hashCursors.take(filePath);
    for (const QString& strKey : cursor.m_setKeys)
    {
        m_hashRecords.remove(strKey);
    }
}

bool LogStore::PruneOldRecords(const QDateTime& now)
{
    const QDateTime dtCutoff = now.addDays(-kRetentionDays);
    bool bChanged = false;
    for (auto it = m_hashRecords.begin(); it != m_hashRecords.end();)
    {
        if (it.value().m_dtTimestamp < dtCutoff)
        {
            it = m_hashRecords.erase(it);
            bChanged = true;
        }
        else
        {
            ++it;
        }
    }
    return bChanged;
}
