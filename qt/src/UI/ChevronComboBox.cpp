#include "UI/ChevronComboBox.h"

#include <QPainter>
#include <QPolygonF>
#include <QStyledItemDelegate>

namespace
{
constexpr double kChevronRightInset = 13.0;
constexpr double kChevronHalfWidth = 4.0;
constexpr double kChevronHalfHeight = 2.0;
constexpr double kChevronPenWidth = 1.2;
constexpr double kChevronAlpha = 0.8;
}

ChevronComboBox::ChevronComboBox(QWidget* parent)
    : QComboBox(parent)
{
    // The default menu-style delegate ignores the style sheet's item rules.
    setItemDelegate(new QStyledItemDelegate(this));
}

ChevronComboBox::~ChevronComboBox() = default;

void ChevronComboBox::paintEvent(QPaintEvent* event)
{
    QComboBox::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QColor clrChevron = palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::ButtonText);
    clrChevron.setAlphaF(kChevronAlpha * clrChevron.alphaF());
    QPen pen(clrChevron, kChevronPenWidth);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    const QPointF ptCenter(width() - kChevronRightInset, height() / 2.0);
    const QPolygonF polyChevron({ ptCenter + QPointF(-kChevronHalfWidth, -kChevronHalfHeight), ptCenter + QPointF(0.0, kChevronHalfHeight),
        ptCenter + QPointF(kChevronHalfWidth, -kChevronHalfHeight) });
    painter.drawPolyline(polyChevron);
}
