#pragma once

#include "Model/Breakdown.h"

#include <QColor>
#include <QList>
#include <QWidget>

class QEvent;
class QLabel;
class QVBoxLayout;

// Always as tall as slotCount rows, so the popup keeps its size whatever the tab, period or group count.
class BreakdownListWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BreakdownListWidget(int slotCount, QWidget* parent = nullptr);
    ~BreakdownListWidget();

public:
    static int CalculateRowHeight(const QFont& baseFont);
    static int CalculateSlotAreaHeight(const QFont& baseFont, int slotCount);

public:
    void SetBreakdown(const Breakdown& breakdown, const QColor& fillColor);

public:
    int GetRowCount() const;
    int GetSlotCount() const;

protected:
    void changeEvent(QEvent* event) override;

private:
    void UpdateSlotHeights();

private:
    int m_iSlotCount;
    QVBoxLayout* m_pLayout;
    QLabel* m_pEmptyLabel;
    QList<QWidget*> m_lstRows;
};
