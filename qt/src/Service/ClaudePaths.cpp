#include "Service/ClaudePaths.h"

#include <QDir>

namespace
{
constexpr const char* kConfigDirEnv = "CLAUDE_CONFIG_DIR";
const QString kDefaultDirectoryName = QStringLiteral("/.claude");
const QString kCredentialsFileName = QStringLiteral("/.credentials.json");
const QString kProjectsDirectoryName = QStringLiteral("/projects");
}

QString ClaudePaths::GetConfigDirectory()
{
    // Claude Code reads the same variable, so both apps agree on one folder.
    const QString strConfigDir = qEnvironmentVariable(kConfigDirEnv);
    if (strConfigDir.isEmpty())
    {
        return QDir::homePath() + kDefaultDirectoryName;
    }
    return QDir::cleanPath(QDir::fromNativeSeparators(strConfigDir));
}

QString ClaudePaths::GetCredentialsPath()
{
    return GetConfigDirectory() + kCredentialsFileName;
}

QString ClaudePaths::GetProjectsPath()
{
    return GetConfigDirectory() + kProjectsDirectoryName;
}
