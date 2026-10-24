#include "UI/LimitMeterWidget.h"

#include "UI/WindowsTheme.h"

#include <QFontMetricsF>
#include <QPainter>

#include <algorithm>

namespace
{
constexpr int kWidgetWidth = 128;
constexpr int kWidgetHeight = 54;
constexpr int kNamePixelSize = 12;
constexpr int kValuePixelSize = 18;
constexpr int kSubPixelSize = 12;
constexpr double kHeaderHeight = 24.0;
constexpr double kBarTop = 29.0;
constexpr double kBarHeight = 4.0;
constexpr double kBarRadius = 2.0;
constexpr double kSubGap = 5.0;
constexpr double kSubHeight = 16.0;
constexpr double kSecondaryAlpha = 0.7;
constexpr double kMutedAlpha = 0.62;
constexpr double kTrackAlpha = 0.12;
constexpr double kDimmedFillAlpha = 0.45;
}

LimitMeterWidget::LimitMeterWidget(const QString& name, QWidget* parent)
    : QWidget(parent)
    , m_strName(name)
    , m_strValueText(QStringLiteral("—"))
    , m_dPercent(0.0)
    , m_strSubText()
    , m_clrFill()
    , m_bDimmed(false)
{
    setMinimumHeight(kWidgetHeight);
}

LimitMeterWidget::~LimitMeterWidget() = default;

void LimitMeterWidget::SetValue(const QString& valueText, double percent, const QString& subText, const QColor& fillColor, bool dimmed)
{
    m_strValueText = valueText;
    m_dPercent = percent;
    m_strSubText = subText;
    m_clrFill = fillColor;
    m_bDimmed = dimmed;
    update();
}

QSize LimitMeterWidget::sizeHint() const
{
    return QSize(kWidgetWidth, kWidgetHeight);
}

QString LimitMeterWidget::GetValueText() const
{
    return m_strValueText;
}

QString LimitMeterWidget::GetSubText() const
{
    return m_strSubText;
}

QColor LimitMeterWidget::GetFillColor() const
{
    return m_clrFill;
}

void LimitMeterWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor clrText = palette().color(QPalette::WindowText);
    QColor clrSecondary = clrText;
    clrSecondary.setAlphaF(kSecondaryAlpha);
    QColor clrMuted = clrText;
    clrMuted.setAlphaF(kMutedAlpha);
    QColor clrTrack = clrText;
    clrTrack.setAlphaF(kTrackAlpha);
    const double dWidth = width();

    // Name and value share one baseline at the bottom of the header, whatever their sizes.
    QFont fontName = font();
    fontName.setPixelSize(kNamePixelSize);
    const QFont fontValue = WindowsTheme::BuildSemiboldFont(kValuePixelSize);
    const QFontMetricsF metricsValue(fontValue);
    const double dBaseline = kHeaderHeight - metricsValue.descent();

    painter.setFont(fontName);
    painter.setPen(clrSecondary);
    painter.drawText(QPointF(0.0, dBaseline), m_strName);

    painter.setFont(fontValue);
    painter.setPen(m_bDimmed ? clrSecondary : clrText);
    painter.drawText(QPointF(dWidth - metricsValue.horizontalAdvance(m_strValueText), dBaseline), m_strValueText);

    painter.setPen(Qt::NoPen);
    painter.setBrush(clrTrack);
    painter.drawRoundedRect(QRectF(0.0, kBarTop, dWidth, kBarHeight), kBarRadius, kBarRadius);
    const double dFillWidth = dWidth * std::clamp(m_dPercent, 0.0, 100.0) / 100.0;
    if (dFillWidth > 0.0)
    {
        QColor clrFill = m_clrFill;
        if (m_bDimmed)
        {
            clrFill.setAlphaF(kDimmedFillAlpha);
        }
        painter.setBrush(clrFill);
        painter.drawRoundedRect(QRectF(0.0, kBarTop, std::max(dFillWidth, kBarHeight), kBarHeight), kBarRadius, kBarRadius);
    }

    QFont fontSub = font();
    fontSub.setPixelSize(kSubPixelSize);
    painter.setFont(fontSub);
    painter.setPen(clrMuted);
    painter.drawText(QRectF(0.0, kBarTop + kBarHeight + kSubGap, dWidth, kSubHeight), Qt::AlignLeft | Qt::AlignVCenter, m_strSubText);
}
