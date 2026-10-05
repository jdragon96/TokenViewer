#pragma once

#include "UI/TrayIconPainter.h"

#include <QObject>
#include <QRect>

#include <memory>

class TrayIcon : public QObject
{
    Q_OBJECT

public:
    explicit TrayIcon(QObject* parent = nullptr);
    ~TrayIcon();

public:
    void Show();
    void SetState(const TrayIconState& state);

public:
    QRect GetAnchorGeometry() const;

signals:
    void Clicked();

private:
    class TrayIconImpl;
    std::unique_ptr<TrayIconImpl> m_pimpl;
};
