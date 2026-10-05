#pragma once

#include "Model/Breakdown.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

class BreakdownListWidget;
class LimitMeterWidget;
class Observers;
class PriceTable;
class QButtonGroup;
class QComboBox;
class QLabel;
class QPushButton;
class QToolButton;
class Settings;
class SystemStatus;

class UsagePopup : public QWidget
{
    Q_OBJECT

public:
    UsagePopup(Observers& observers, SystemStatus* systemStatus, Settings* settings, const PriceTable* priceTable, QWidget* parent = nullptr);
    ~UsagePopup();

public:
    void ShowBelow(const QRect& anchor);
    void Refresh();
    bool WasJustHidden() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void BuildUi();
    QPushButton* CreateSegmentButton(const QString& text, const QString& objectName);
    void SelectDimension(EBreakdownDimension dimension);
    void RefreshLimits(const QDateTime& now);
    void RefreshBreakdown(const QDateTime& now);
    void RefreshFooter(const QDateTime& now);
    QString BuildNoticeText(const QDateTime& now) const;
    bool IsDarkAppearance() const;

private:
    Observers& m_observers;
    SystemStatus* m_kpSystemStatus;
    Settings* m_kpSettings;
    const PriceTable* m_kpPriceTable;
    QLabel* m_pPlanBadge;
    QWidget* m_pMetersRow;
    LimitMeterWidget* m_pFiveHourMeter;
    LimitMeterWidget* m_pSevenDayMeter;
    QLabel* m_pNoticeLabel;
    QPushButton* m_pRetryButton;
    QButtonGroup* m_pSegmentGroup;
    QPushButton* m_pProjectButton;
    QPushButton* m_pModelButton;
    QPushButton* m_pSessionButton;
    BreakdownListWidget* m_pBreakdownList;
    QComboBox* m_pPeriodCombo;
    QLabel* m_pFooterLabel;
    QToolButton* m_pRefreshButton;
    QToolButton* m_pSettingsButton;
    QToolButton* m_pQuitButton;
    QTimer m_timerAge;
    QElapsedTimer m_timerSinceHide;
};
