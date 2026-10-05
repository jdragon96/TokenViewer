#pragma once

#include <QObject>

class Observers : public QObject
{
    Q_OBJECT

public:
    explicit Observers(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

signals:
    void LimitsChanged();
    void LogSnapshotChanged();
    void SettingsChanged();
    void RefreshRequested();
    void SettingsWindowRequested();
    void QuitRequested();
};
