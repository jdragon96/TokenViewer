#include "UI/TrayIcon.h"

#include <QGuiApplication>
#include <QIcon>
#include <QPalette>
#include <QPixmap>
#include <QSystemTrayIcon>

namespace
{
constexpr int kDarkTextLightness = 128;
}

class TrayIcon::TrayIconImpl
{
public:
    QSystemTrayIcon m_trayIcon;
};

TrayIcon::TrayIcon(QObject* parent)
    : QObject(parent)
    , m_pimpl(std::make_unique<TrayIconImpl>())
{
    connect(&m_pimpl->m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason)
    {
        if (reason == QSystemTrayIcon::Trigger)
        {
            emit Clicked();
        }
    });
    SetState(TrayIconState());
}

TrayIcon::~TrayIcon() = default;

void TrayIcon::Show()
{
    m_pimpl->m_trayIcon.show();
}

void TrayIcon::SetState(const TrayIconState& state)
{
    // Qt cannot see the menu bar's own appearance; the app palette is the closest guess.
    const bool bDark = QGuiApplication::palette().color(QPalette::WindowText).lightness() > kDarkTextLightness;
    const EMenuBarAppearance eAppearance = bDark ? EMenuBarAppearance::DARK : EMenuBarAppearance::LIGHT;
    const qreal dDevicePixelRatio = qGuiApp->devicePixelRatio();
    const TrayIconImage image = TrayIconPainter::Render(state, eAppearance, dDevicePixelRatio);

    QPixmap pixmap = QPixmap::fromImage(image.m_imgIcon);
    pixmap.setDevicePixelRatio(dDevicePixelRatio);
    QIcon icon(pixmap);
    icon.setIsMask(image.m_bTemplate);
    m_pimpl->m_trayIcon.setIcon(icon);
}

QRect TrayIcon::GetAnchorGeometry() const
{
    return m_pimpl->m_trayIcon.geometry();
}
