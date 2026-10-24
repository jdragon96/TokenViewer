#include "UI/TrayIcon.h"

#include "UI/WindowsTheme.h"

#include <QGuiApplication>
#include <QIcon>
#include <QPixmap>
#include <QScreen>
#include <QSystemTrayIcon>

class TrayIcon::TrayIconImpl
{
public:
    TrayIconImpl(QSettings* personalizeKey, QObject* owner)
        : m_kpPersonalizeKey(personalizeKey)
        , m_trayIcon(owner)
        , m_state()
        , m_eAppearance(ReadAppearance())
    {
    }

    EAppearance ReadAppearance() const
    {
        return WindowsTheme::IsTaskbarLight(m_kpPersonalizeKey) ? EAppearance::LIGHT : EAppearance::DARK;
    }

    void Redraw()
    {
        m_trayIcon.setIcon(TrayIcon::BuildIcon(m_state, m_eAppearance));
        m_trayIcon.setToolTip(TrayIconPainter::FormatToolTip(m_state));
    }

public:
    QSettings* m_kpPersonalizeKey;
    QSystemTrayIcon m_trayIcon;
    TrayIconState m_state;
    EAppearance m_eAppearance;
};

TrayIcon::TrayIcon(QSettings* personalizeKey, QObject* parent)
    : QObject(parent)
    , m_pimpl(std::make_unique<TrayIconImpl>(personalizeKey, this))
{
    connect(&m_pimpl->m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason)
    {
        // Left and right click both toggle the popup; there is no separate context menu.
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::Context)
        {
            emit Clicked();
        }
    });
    // Qt picks the pixmap size when the icon is set, so a new display scale needs the icon set again.
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen* screen)
    {
        WatchScreen(screen);
        HandleDisplayChanged();
    });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &TrayIcon::HandleDisplayChanged);
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, &TrayIcon::HandleDisplayChanged);
    for (QScreen* pScreen : QGuiApplication::screens())
    {
        WatchScreen(pScreen);
    }
    m_pimpl->Redraw();
}

TrayIcon::~TrayIcon() = default;

QIcon TrayIcon::BuildIcon(const TrayIconState& state, EAppearance appearance)
{
    // Each size is drawn on its own pixel grid; a scaled copy would blur the number at 16 px.
    QIcon icon;
    for (const int iSize : kIconPixelSizes)
    {
        icon.addPixmap(QPixmap::fromImage(TrayIconPainter::Render(state, appearance, iSize)));
    }
    return icon;
}

void TrayIcon::Show()
{
    m_pimpl->m_trayIcon.show();
}

void TrayIcon::SetState(const TrayIconState& state)
{
    m_pimpl->m_state = state;
    m_pimpl->Redraw();
}

bool TrayIcon::RefreshTaskbarAppearance()
{
    const EAppearance eAppearance = m_pimpl->ReadAppearance();
    if (eAppearance == m_pimpl->m_eAppearance)
    {
        return false;
    }
    m_pimpl->m_eAppearance = eAppearance;
    m_pimpl->Redraw();
    return true;
}

QRect TrayIcon::GetAnchorGeometry() const
{
    return m_pimpl->m_trayIcon.geometry();
}

void TrayIcon::WatchScreen(QScreen* screen)
{
    connect(screen, &QScreen::logicalDotsPerInchChanged, this, &TrayIcon::HandleDisplayChanged, Qt::UniqueConnection);
    connect(screen, &QScreen::physicalDotsPerInchChanged, this, &TrayIcon::HandleDisplayChanged, Qt::UniqueConnection);
}

void TrayIcon::HandleDisplayChanged()
{
    m_pimpl->Redraw();
}
