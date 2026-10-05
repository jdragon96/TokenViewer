#pragma once

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

inline QByteArray ReadSourceFile(const QString& relativePath)
{
    QFile file(QString::fromUtf8(TV_SOURCE_DIR) + QLatin1Char('/') + relativePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        return QByteArray();
    }
    return file.readAll();
}

inline bool AppendText(const QString& filePath, const QByteArray& text)
{
    QDir().mkpath(QFileInfo(filePath).path());
    QFile file(filePath);
    if (!file.open(QIODevice::Append))
    {
        return false;
    }
    return file.write(text) == text.size();
}

inline QByteArray MakeUsageLine(const QString& messageId, const QString& sessionId, const QString& timestamp)
{
    const QString strLine = QStringLiteral(R"({"type":"assistant","sessionId":"%1","cwd":"/Users/me/Proj","timestamp":"%2","requestId":"req_%3","message":{"id":"%3","model":"claude-opus-5-5","usage":{"input_tokens":1,"output_tokens":10,"cache_creation_input_tokens":0,"cache_read_input_tokens":0}}})");
    return strLine.arg(sessionId, timestamp, messageId).toUtf8() + '\n';
}
