#include "UI/TrayIcon.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QLabel>
#include <QTimer>
#include <QVector>

namespace
{
constexpr int kStateCycleMs = 3000;
constexpr int kPopupGap = 4;
constexpr int kReopenGuardMs = 300;
constexpr int kPopupWidth = 300;
constexpr int kPopupHeight = 120;
constexpr int kPopupMargin = 16;

TrayIconState MakeSpikeState(bool valid, double fiveHourPercent, double sevenDayPercent, bool stale)
{
    TrayIconState state;
    state.m_bFiveHourValid = valid;
    state.m_dFiveHourPercent = fiveHourPercent;
    state.m_bSevenDayValid = valid;
    state.m_dSevenDayPercent = sevenDayPercent;
    state.m_bStale = stale;
    return state;
}

class HideWatcher : public QObject
{
public:
    explicit HideWatcher(QElapsedTimer* timer)
        : m_kpTimer(timer)
    {
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Hide)
        {
            m_kpTimer->start();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QElapsedTimer* m_kpTimer;
};
}

// Spike: cycles through every tray icon state so it can be checked by eye on macOS 26.
int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);

    TrayIcon trayIcon;
    QLabel popup(QStringLiteral("TokenViewer spike popup\n바깥을 누르면 닫혀야 합니다"), nullptr, Qt::Popup);
    popup.setMargin(kPopupMargin);
    popup.resize(kPopupWidth, kPopupHeight);

    QElapsedTimer timerSinceHide;
    HideWatcher hideWatcher(&timerSinceHide);
    popup.installEventFilter(&hideWatcher);

    QObject::connect(&trayIcon, &TrayIcon::Clicked, [&]()
    {
        if (popup.isVisible())
        {
            popup.hide();
            return;
        }
        // The click that closed the popup also reaches the icon; do not reopen right away.
        if (timerSinceHide.isValid() && timerSinceHide.elapsed() < kReopenGuardMs)
        {
            return;
        }
        const QRect rcAnchor = trayIcon.GetAnchorGeometry();
        popup.move(rcAnchor.center().x() - popup.width() / 2, rcAnchor.bottom() + kPopupGap);
        popup.show();
        popup.activateWindow();
    });

    const QVector<TrayIconState> vecStates = {
        MakeSpikeState(false, 0.0, 0.0, false),
        MakeSpikeState(true, 42.0, 18.0, false),
        MakeSpikeState(true, 78.0, 31.0, false),
        MakeSpikeState(true, 96.0, 40.0, false),
        MakeSpikeState(true, 12.0, 93.0, false),
        MakeSpikeState(true, 42.0, 18.0, true),
    };
    int iStateIndex = 0;
    QTimer timerCycle;
    QObject::connect(&timerCycle, &QTimer::timeout, [&]()
    {
        iStateIndex = (iStateIndex + 1) % vecStates.size();
        trayIcon.SetState(vecStates[iStateIndex]);
    });

    trayIcon.SetState(vecStates[iStateIndex]);
    trayIcon.Show();
    timerCycle.start(kStateCycleMs);
    return app.exec();
}
