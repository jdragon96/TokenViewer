#pragma once

#include "Model/Breakdown.h"

#include <QColor>
#include <QList>
#include <QWidget>

class QLabel;
class QVBoxLayout;

class BreakdownListWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BreakdownListWidget(QWidget* parent = nullptr);
    ~BreakdownListWidget();

public:
    void SetBreakdown(const Breakdown& breakdown, const QColor& fillColor);

public:
    int GetRowCount() const;

private:
    QVBoxLayout* m_pLayout;
    QLabel* m_pEmptyLabel;
    QList<QWidget*> m_lstRows;
};
