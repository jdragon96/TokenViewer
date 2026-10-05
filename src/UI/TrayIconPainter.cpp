#include "UI/TrayIconPainter.h"

#include <QFont>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kMinVisibleFill = 2.0;
constexpr double kOutlineAlpha = 0.45;
constexpr double kOutlineRadius = 1.5;
constexpr double kFillRadius = 2.0;
const QString kEmptyLabel = QStringLiteral("—");
}

bool operator==(const TrayIconState& lhs, const TrayIconState& rhs)
{
    return lhs.m_bFiveHourValid == rhs.m_bFiveHourValid
        && lhs.m_dFiveHourPercent == rhs.m_dFiveHourPercent
        && lhs.m_bSevenDayValid == rhs.m_bSevenDayValid
        && lhs.m_dSevenDayPercent == rhs.m_dSevenDayPercent
        && lhs.m_bStale == rhs.m_bStale
        && lhs.m_iWarnPercent == rhs.m_iWarnPercent
        && lhs.m_iCriticalPercent == rhs.m_iCriticalPercent;
}

bool operator!=(const TrayIconState& lhs, const TrayIconState& rhs)
{
    return !(lhs == rhs);
}

EBarLevel TrayIconPainter::ClassifyLevel(double percent, int warnPercent, int criticalPercent)
{
    if (percent >= criticalPercent)
    {
        return EBarLevel::CRITICAL;
    }
    if (percent >= warnPercent)
    {
        return EBarLevel::WARNING;
    }
    return EBarLevel::NORMAL;
}

bool TrayIconPainter::IsTemplate(const TrayIconState& state)
{
    const bool bFiveNormal = !state.m_bFiveHourValid
        || ClassifyLevel(state.m_dFiveHourPercent, state.m_iWarnPercent, state.m_iCriticalPercent) == EBarLevel::NORMAL;
    const bool bSevenNormal = !state.m_bSevenDayValid
        || ClassifyLevel(state.m_dSevenDayPercent, state.m_iWarnPercent, state.m_iCriticalPercent) == EBarLevel::NORMAL;
    return bFiveNormal && bSevenNormal;
}

QString TrayIconPainter::FormatLabel(const TrayIconState& state)
{
    if (!state.m_bFiveHourValid)
    {
        return kEmptyLabel;
    }
    return QString::number(qRound(ClampPercent(state.m_dFiveHourPercent))) + QLatin1Char('%');
}

QColor TrayIconPainter::GetInkColor(EMenuBarAppearance appearance)
{
    return appearance == EMenuBarAppearance::DARK ? QColor(Qt::white) : QColor(Qt::black);
}

QColor TrayIconPainter::GetLevelColor(EBarLevel level, EMenuBarAppearance appearance)
{
    const bool bDark = appearance == EMenuBarAppearance::DARK;
    switch (level)
    {
    case EBarLevel::WARNING:
        return bDark ? QColor(0xfa, 0xb2, 0x19) : QColor(0xc9, 0x85, 0x00);
    case EBarLevel::CRITICAL:
        return bDark ? QColor(0xff, 0x6b, 0x6b) : QColor(0xd0, 0x3b, 0x3b);
    case EBarLevel::NORMAL:
        break;
    }
    return GetInkColor(appearance);
}

TrayIconImage TrayIconPainter::Render(const TrayIconState& state, EMenuBarAppearance appearance, qreal devicePixelRatio)
{
    TrayIconImage result;
    result.m_bTemplate = IsTemplate(state);

    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setPixelSize(kFontPixelSize);
    const QString strLabel = FormatLabel(state);
    const double dTextWidth = QFontMetricsF(font).horizontalAdvance(strLabel);
    const double dWidth = std::ceil(kBarWidth + kTextGap + dTextWidth + 1.0);

    QImage imgIcon(qRound(dWidth * devicePixelRatio), qRound(kHeight * devicePixelRatio), QImage::Format_ARGB32_Premultiplied);
    imgIcon.setDevicePixelRatio(devicePixelRatio);
    imgIcon.fill(Qt::transparent);

    // A template image only carries alpha and macOS picks the color, so draw it in plain black.
    const EMenuBarAppearance eInkAppearance = result.m_bTemplate ? EMenuBarAppearance::LIGHT : appearance;
    const QColor clrInk = GetInkColor(eInkAppearance);
    QColor clrOutline = clrInk;
    clrOutline.setAlphaF(kOutlineAlpha);

    const EBarLevel eFiveLevel = state.m_bFiveHourValid
        ? ClassifyLevel(state.m_dFiveHourPercent, state.m_iWarnPercent, state.m_iCriticalPercent)
        : EBarLevel::NORMAL;
    const EBarLevel eSevenLevel = state.m_bSevenDayValid
        ? ClassifyLevel(state.m_dSevenDayPercent, state.m_iWarnPercent, state.m_iCriticalPercent)
        : EBarLevel::NORMAL;
    const QColor clrFive = eFiveLevel == EBarLevel::NORMAL ? clrInk : GetLevelColor(eFiveLevel, appearance);
    const QColor clrSeven = eSevenLevel == EBarLevel::NORMAL ? clrInk : GetLevelColor(eSevenLevel, appearance);
    const bool bEmpty = !state.m_bFiveHourValid && !state.m_bSevenDayValid;

    QPainter painter(&imgIcon);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setOpacity((state.m_bStale || bEmpty) ? kDimmedOpacity : 1.0);

    DrawBar(painter, kFiveHourBarTop, state.m_bFiveHourValid, state.m_dFiveHourPercent, clrFive, clrOutline);
    DrawBar(painter, kSevenDayBarTop, state.m_bSevenDayValid, state.m_dSevenDayPercent, clrSeven, clrOutline);

    painter.setFont(font);
    painter.setPen(clrFive);
    painter.drawText(QRectF(kBarWidth + kTextGap, 0.0, dTextWidth + 1.0, kHeight), Qt::AlignLeft | Qt::AlignVCenter, strLabel);
    painter.end();

    result.m_imgIcon = imgIcon;
    return result;
}

double TrayIconPainter::ClampPercent(double percent)
{
    return std::clamp(percent, 0.0, 100.0);
}

void TrayIconPainter::DrawBar(QPainter& painter, double top, bool valid, double percent, const QColor& fillColor, const QColor& outlineColor)
{
    painter.setPen(QPen(outlineColor, 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(0.5, top + 0.5, kBarWidth - 1.0, kBarHeight - 1.0), kOutlineRadius, kOutlineRadius);

    if (!valid)
    {
        return;
    }
    double dFillWidth = kBarWidth * ClampPercent(percent) / 100.0;
    if (dFillWidth <= 0.0)
    {
        return;
    }
    dFillWidth = std::max(dFillWidth, kMinVisibleFill);
    painter.setPen(Qt::NoPen);
    painter.setBrush(fillColor);
    painter.drawRoundedRect(QRectF(0.0, top, dFillWidth, kBarHeight), kFillRadius, kFillRadius);
}
