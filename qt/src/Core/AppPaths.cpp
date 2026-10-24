#include "Core/AppPaths.h"

#include <QStandardPaths>

namespace
{
const QString kLogDirectoryName = QStringLiteral("/logs");
const QString kBadResponseLogName = QStringLiteral("/usage-response.log");
}

QString AppPaths::GetLogDirectory()
{
    // %LOCALAPPDATA%/<organization>/<application> on Windows.
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + kLogDirectoryName;
}

QString AppPaths::GetBadResponseLogPath()
{
    return GetLogDirectory() + kBadResponseLogName;
}
