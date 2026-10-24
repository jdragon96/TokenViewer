#pragma once

#include "UI/TrayIconPainter.h"

#include <QObject>
#include <QRect>

#include <array>
#include <memory>

class QIcon;
class QScreen;
class QSettings;

class TrayIcon : public QObject
{
    Q_OBJECT

public:
    // Windows asks for 16 px at 100% up to 32 px at 200%; the larger ones serve the overflow flyout and settings.
    static constexpr std::array<int, 7> kIconPixelSizes = {16, 20, 24, 32, 40, 48, 64};

public:
    explicit TrayIcon(QSettings* personalizeKey, QObject* parent = nullptr);
    ~TrayIcon();

public:
    static QIcon BuildIcon(const TrayIconState& state, EAppearance appearance);

public:
    void Show();
    void SetState(const TrayIconState& state);
    bool RefreshTaskbarAppearance();

public:
    QRect GetAnchorGeometry() const;

signals:
    void Clicked();

private:
    void WatchScreen(QScreen* screen);
    void HandleDisplayChanged();

private:
    class TrayIconImpl;
    std::unique_ptr<TrayIconImpl> m_pimpl;
};
