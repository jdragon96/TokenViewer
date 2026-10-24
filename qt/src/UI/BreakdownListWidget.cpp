#include "UI/BreakdownListWidget.h"

#include "UI/Formatters.h"

#include <QEvent>
#include <QFontMetricsF>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>
#include <QtMath>

#include <algorithm>

namespace
{
constexpr int kRowPixelSize = 13;
constexpr int kRowSpacing = 1;
constexpr int kTextBarGap = 3;
constexpr int kRowBarHeight = 4;
constexpr int kRowBottomGap = 5;
constexpr double kRowBarRadius = 2.0;
constexpr double kAmountGap = 8.0;
constexpr double kSecondaryAlpha = 0.7;
constexpr double kTrackAlpha = 0.12;

QFont MakeRowFont(const QFont& baseFont)
{
    QFont fontRow = baseFont;
    fontRow.setPixelSize(kRowPixelSize);
    return fontRow;
}

// The label line's height; the row adds the bar and its gaps below it.
int CalculateTextHeight(const QFont& rowFont)
{
    return qCeil(QFontMetricsF(rowFont).height());
}

QString FormatAmount(const BreakdownRow& row)
{
    if (row.m_bUnpriced)
    {
        const qint64 llTotal = row.m_llInput + row.m_llOutput + row.m_llCacheWrite + row.m_llCacheRead;
        return Formatters::FormatTokens(llTotal) + QStringLiteral(" 토큰");
    }
    return Formatters::FormatUsd(row.m_dCostUsd, row.m_bApproximate);
}

QString BuildToolTip(const BreakdownRow& row)
{
    return QStringLiteral("%1\n입력 %2 · 출력 %3\n캐시 쓰기 %4 · 캐시 읽기 %5")
        .arg(row.m_strDetail, Formatters::FormatTokens(row.m_llInput), Formatters::FormatTokens(row.m_llOutput),
            Formatters::FormatTokens(row.m_llCacheWrite), Formatters::FormatTokens(row.m_llCacheRead));
}

class BreakdownRowWidget : public QWidget
{
public:
    BreakdownRowWidget(const BreakdownRow& row, double ratio, const QColor& fillColor, QWidget* parent)
        : QWidget(parent)
        , m_strLabel(row.m_strLabel)
        , m_strAmount(FormatAmount(row))
        , m_dRatio(ratio)
        , m_clrFill(fillColor)
    {
        setToolTip(BuildToolTip(row));
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QColor clrText = palette().color(QPalette::WindowText);
        QColor clrSecondary = clrText;
        clrSecondary.setAlphaF(kSecondaryAlpha);
        QColor clrTrack = clrText;
        clrTrack.setAlphaF(kTrackAlpha);

        const QFont fontRow = MakeRowFont(font());
        painter.setFont(fontRow);
        const QFontMetricsF metrics(fontRow);
        const int iTextHeight = CalculateTextHeight(fontRow);
        const double dAmountWidth = metrics.horizontalAdvance(m_strAmount) + kAmountGap;
        const QRectF rcText(0.0, 0.0, width(), iTextHeight);
        painter.setPen(clrText);
        painter.drawText(rcText.adjusted(0.0, 0.0, -dAmountWidth, 0.0), Qt::AlignLeft | Qt::AlignVCenter,
            metrics.elidedText(m_strLabel, Qt::ElideMiddle, width() - dAmountWidth));
        painter.setPen(clrSecondary);
        painter.drawText(rcText, Qt::AlignRight | Qt::AlignVCenter, m_strAmount);

        const double dBarTop = iTextHeight + kTextBarGap;
        painter.setPen(Qt::NoPen);
        painter.setBrush(clrTrack);
        painter.drawRoundedRect(QRectF(0.0, dBarTop, width(), kRowBarHeight), kRowBarRadius, kRowBarRadius);
        if (m_dRatio > 0.0)
        {
            painter.setBrush(m_clrFill);
            painter.drawRoundedRect(QRectF(0.0, dBarTop, std::max(width() * m_dRatio, static_cast<double>(kRowBarHeight)), kRowBarHeight),
                kRowBarRadius, kRowBarRadius);
        }
    }

private:
    QString m_strLabel;
    QString m_strAmount;
    double m_dRatio;
    QColor m_clrFill;
};
}

BreakdownListWidget::BreakdownListWidget(int slotCount, QWidget* parent)
    : QWidget(parent)
    , m_iSlotCount(slotCount)
    , m_pLayout(new QVBoxLayout(this))
    , m_pEmptyLabel(new QLabel(QStringLiteral("이 기간에 기록이 없습니다"), this))
    , m_lstRows()
{
    m_pLayout->setContentsMargins(0, 0, 0, 0);
    m_pLayout->setSpacing(kRowSpacing);
    m_pEmptyLabel->setObjectName(QStringLiteral("emptyLabel"));
    m_pEmptyLabel->setAlignment(Qt::AlignCenter);
    m_pLayout->addWidget(m_pEmptyLabel);
    // Rows go above this, so unused slots stay blank at the bottom.
    m_pLayout->addStretch(1);
    UpdateSlotHeights();
}

BreakdownListWidget::~BreakdownListWidget() = default;

int BreakdownListWidget::CalculateRowHeight(const QFont& baseFont)
{
    return CalculateTextHeight(MakeRowFont(baseFont)) + kTextBarGap + kRowBarHeight + kRowBottomGap;
}

int BreakdownListWidget::CalculateSlotAreaHeight(const QFont& baseFont, int slotCount)
{
    return slotCount * CalculateRowHeight(baseFont) + std::max(0, slotCount - 1) * kRowSpacing;
}

void BreakdownListWidget::SetBreakdown(const Breakdown& breakdown, const QColor& fillColor)
{
    for (QWidget* pRow : m_lstRows)
    {
        m_pLayout->removeWidget(pRow);
        pRow->hide();
        pRow->deleteLater();
    }
    m_lstRows.clear();

    QVector<BreakdownRow> vecRows = breakdown.m_vecRows;
    if (breakdown.m_iOtherCount > 0)
    {
        vecRows.append(breakdown.m_otherRow);
    }
    const double dMaxCost = breakdown.m_vecRows.isEmpty() ? 0.0 : breakdown.m_vecRows.first().m_dCostUsd;
    const int iRowHeight = CalculateRowHeight(font());
    for (const BreakdownRow& row : vecRows)
    {
        const double dRatio = dMaxCost > 0.0 ? std::min(row.m_dCostUsd / dMaxCost, 1.0) : 0.0;
        QWidget* pRow = new BreakdownRowWidget(row, dRatio, fillColor, this);
        pRow->setFixedHeight(iRowHeight);
        // Before the trailing stretch.
        m_pLayout->insertWidget(m_pLayout->count() - 1, pRow);
        pRow->show();
        m_lstRows.append(pRow);
    }
    m_pEmptyLabel->setVisible(m_lstRows.isEmpty());
}

int BreakdownListWidget::GetRowCount() const
{
    return m_lstRows.size();
}

int BreakdownListWidget::GetSlotCount() const
{
    return m_iSlotCount;
}

void BreakdownListWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    // The app font arrives after the popup is built, and the slot heights follow the font.
    if (event->type() == QEvent::FontChange)
    {
        UpdateSlotHeights();
    }
}

void BreakdownListWidget::UpdateSlotHeights()
{
    const int iAreaHeight = CalculateSlotAreaHeight(font(), m_iSlotCount);
    setFixedHeight(iAreaHeight);
    // The empty message fills the whole area so it sits in the middle of it.
    m_pEmptyLabel->setFixedHeight(iAreaHeight);
    const int iRowHeight = CalculateRowHeight(font());
    for (QWidget* pRow : m_lstRows)
    {
        pRow->setFixedHeight(iRowHeight);
    }
}
