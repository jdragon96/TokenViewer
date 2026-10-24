#pragma once

#include <QString>

class AppPaths
{
public:
    static QString GetLogDirectory();
    static QString GetBadResponseLogPath();
};
