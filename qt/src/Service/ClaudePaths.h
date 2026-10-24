#pragma once

#include <QString>

class ClaudePaths
{
public:
    static QString GetConfigDirectory();
    static QString GetCredentialsPath();
    static QString GetProjectsPath();
};
