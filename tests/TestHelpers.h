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
