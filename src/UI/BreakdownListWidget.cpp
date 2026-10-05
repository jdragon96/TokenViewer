#include "UI/BreakdownListWidget.h"

#include "UI/Formatters.h"

#include <QFontMetricsF>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
constexpr int kRowHeight = 28;
constexpr int kRowPixelSize = 12;
constexpr int kRowSpacing = 2;
constexpr double kRowBarHeight = 4.0;
constexpr double kRowBarRadius = 2.0;
constexpr double kRowBarBottomGap = 3.0;
constexpr double kTextBottomGap = 8.0;
constexpr double kAmountGap = 8.0;
constexpr double kSecondaryAlpha = 0.7;
constexpr double kTrackAlpha = 0.12;

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
        setFixedHeight(kRowHeight);
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

        QFont fontRow = font();
        fontRow.setPixelSize(kRowPixelSize);
        painter.setFont(fontRow);
        const QFontMetricsF metrics(fontRow);
        const double dAmountWidth = metrics.horizontalAdvance(m_strAmount) + kAmountGap;
        const QRectF rcText(0.0, 0.0, width(), kRowHeight - kTextBottomGap);
        painter.setPen(clrText);
        painter.drawText(rcText.adjusted(0.0, 0.0, -dAmountWidth, 0.0), Qt::AlignLeft | Qt::AlignVCenter,
            metrics.elidedText(m_strLabel, Qt::ElideMiddle, width() - dAmountWidth));
        painter.setPen(clrSecondary);
        painter.drawText(rcText, Qt::AlignRight | Qt::AlignVCenter, m_strAmount);

        const double dBarTop = kRowHeight - kRowBarHeight - kRowBarBottomGap;
        painter.setPen(Qt::NoPen);
        painter.setBrush(clrTrack);
        painter.drawRoundedRect(QRectF(0.0, dBarTop, width(), kRowBarHeight), kRowBarRadius, kRowBarRadius);
        if (m_dRatio > 0.0)
        {
            painter.setBrush(m_clrFill);
            painter.drawRoundedRect(QRectF(0.0, dBarTop, std::max(width() * m_dRatio, kRowBarHeight), kRowBarHeight), kRowBarRadius, kRowBarRadius);
        }
    }

private:
    QString m_strLabel;
    QString m_strAmount;
    double m_dRatio;
    QColor m_clrFill;
};
}

BreakdownListWidget::BreakdownListWidget(QWidget* parent)
    : QWidget(parent)
    , m_pLayout(new QVBoxLayout(this))
    , m_pEmptyLabel(new QLabel(QStringLiteral("이 기간에 기록이 없습니다"), this))
    , m_lstRows()
{
    m_pLayout->setContentsMargins(0, 0, 0, 0);
    m_pLayout->setSpacing(kRowSpacing);
    m_pEmptyLabel->setAlignment(Qt::AlignCenter);
    m_pLayout->addWidget(m_pEmptyLabel);
}

BreakdownListWidget::~BreakdownListWidget() = default;

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
    for (const BreakdownRow& row : vecRows)
    {
        const double dRatio = dMaxCost > 0.0 ? std::min(row.m_dCostUsd / dMaxCost, 1.0) : 0.0;
        QWidget* pRow = new BreakdownRowWidget(row, dRatio, fillColor, this);
        m_pLayout->addWidget(pRow);
        pRow->show();
        m_lstRows.append(pRow);
    }
    m_pEmptyLabel->setVisible(m_lstRows.isEmpty());
    updateGeometry();
}

int BreakdownListWidget::GetRowCount() const
{
    return m_lstRows.size();
}
