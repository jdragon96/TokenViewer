#include "UI/TrayIconPainter.h"

#include "UI/Formatters.h"

#include <QFont>
#include <QFontMetrics>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPair>
#include <QVector>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr double kLineWidthRatio = 1.0 / 8.0;
constexpr double kCornerRadiusRatio = 1.0 / 4.0;
constexpr double kQuarterTurnDegrees = 90.0;
// Where each corner's arc starts on its own circle, in Qt degrees.
constexpr double kRightDegrees = 0.0;
constexpr double kTopDegrees = 90.0;
constexpr double kLeftDegrees = 180.0;
constexpr double kBottomDegrees = 270.0;
constexpr int kPaddedFromSize = 24;
constexpr int kPaddingDivisor = 16;
constexpr int kLetterSpacingDivisor = 32;
constexpr double kMaxFontToSpaceRatio = 1.5;
constexpr int kInkFringeAlpha = 64;
constexpr int kTwoDigits = 2;
const QString kEmptyLabel = QStringLiteral("—");
const QString kFontFamily = QStringLiteral("Segoe UI");
const QString kToolTipTitle = QStringLiteral("Claude 사용량");
const QString kToolTipNoLimits = QStringLiteral("한도 정보 없음");

struct BorderSegment
{
    bool m_bArc = false;
    QPointF m_ptStart;
    QPointF m_ptEnd;
    QRectF m_rectArc;
    double m_dStartAngle = 0.0;
    double m_dLength = 0.0;
};

BorderSegment MakeLine(const QPointF& start, const QPointF& end)
{
    BorderSegment segment;
    segment.m_ptStart = start;
    segment.m_ptEnd = end;
    segment.m_dLength = QLineF(start, end).length();
    return segment;
}

BorderSegment MakeArc(const QRectF& rect, double startAngle)
{
    BorderSegment segment;
    segment.m_bArc = true;
    segment.m_rectArc = rect;
    segment.m_dStartAngle = startAngle;
    segment.m_dLength = M_PI * rect.width() / 4.0;
    return segment;
}

// The rounded box traced from nine o'clock downward. Qt angles grow counter-clockwise on screen.
QVector<BorderSegment> BuildSegments(const QRectF& box, double radius)
{
    const double dLeft = box.left();
    const double dRight = box.right();
    const double dTop = box.top();
    const double dBottom = box.bottom();
    const double dMiddle = box.center().y();
    const double dDiameter = radius * 2.0;
    return {
        MakeLine(QPointF(dLeft, dMiddle), QPointF(dLeft, dBottom - radius)),
        MakeArc(QRectF(dLeft, dBottom - dDiameter, dDiameter, dDiameter), kLeftDegrees),
        MakeLine(QPointF(dLeft + radius, dBottom), QPointF(dRight - radius, dBottom)),
        MakeArc(QRectF(dRight - dDiameter, dBottom - dDiameter, dDiameter, dDiameter), kBottomDegrees),
        MakeLine(QPointF(dRight, dBottom - radius), QPointF(dRight, dTop + radius)),
        MakeArc(QRectF(dRight - dDiameter, dTop, dDiameter, dDiameter), kRightDegrees),
        MakeLine(QPointF(dRight - radius, dTop), QPointF(dLeft + radius, dTop)),
        MakeArc(QRectF(dLeft, dTop, dDiameter, dDiameter), kTopDegrees),
        MakeLine(QPointF(dLeft, dTop + radius), QPointF(dLeft, dMiddle)),
    };
}

QPointF GetPointOnArc(const QRectF& rect, double angle)
{
    const double dRadians = qDegreesToRadians(angle);
    return QPointF(rect.center().x() + rect.width() / 2.0 * std::cos(dRadians),
        rect.center().y() - rect.height() / 2.0 * std::sin(dRadians));
}

// The part of the border between `from` and `to` (fractions of the perimeter from nine o'clock).
QPainterPath BuildBorderPath(const QRectF& box, double radius, double from, double to)
{
    const QVector<BorderSegment> vecSegments = BuildSegments(box, radius);
    double dPerimeter = 0.0;
    for (const BorderSegment& segment : vecSegments)
    {
        dPerimeter += segment.m_dLength;
    }
    const double dFrom = from * dPerimeter;
    const double dTo = to * dPerimeter;

    QPainterPath path;
    bool bStarted = false;
    double dOffset = 0.0;
    for (const BorderSegment& segment : vecSegments)
    {
        const double dSegmentStart = dOffset;
        dOffset += segment.m_dLength;
        if (segment.m_dLength <= 0.0 || dOffset <= dFrom || dSegmentStart >= dTo)
        {
            continue;
        }
        const double dT0 = std::max(dFrom - dSegmentStart, 0.0) / segment.m_dLength;
        const double dT1 = std::min(dTo - dSegmentStart, segment.m_dLength) / segment.m_dLength;
        if (segment.m_bArc)
        {
            const double dStartAngle = segment.m_dStartAngle + kQuarterTurnDegrees * dT0;
            if (!bStarted)
            {
                path.moveTo(GetPointOnArc(segment.m_rectArc, dStartAngle));
                bStarted = true;
            }
            path.arcTo(segment.m_rectArc, dStartAngle, kQuarterTurnDegrees * (dT1 - dT0));
        }
        else
        {
            const QPointF ptDelta = segment.m_ptEnd - segment.m_ptStart;
            if (!bStarted)
            {
                path.moveTo(segment.m_ptStart + ptDelta * dT0);
                bStarted = true;
            }
            path.lineTo(segment.m_ptStart + ptDelta * dT1);
        }
    }
    if (from <= 0.0 && to >= 1.0)
    {
        path.closeSubpath();
    }
    return path;
}

QFont MakeLabelFont(int pixelSize)
{
    QFont font(kFontFamily);
    font.setWeight(QFont::Bold);
    font.setPixelSize(std::max(pixelSize, 1));
    // Full hinting snaps the digits to the pixel grid; Qt would otherwise switch engines with the display scale.
    font.setHintingPreference(QFont::PreferFullHinting);
    return font;
}

// The pixels the label actually covers when drawn at an integer origin, relative to that origin.
// Hinted glyphs can reach a pixel past the font's tight bounding rect, so measure the drawing itself.
// The faint anti-aliasing fringe is left out: counting it costs a font size at 16 px, where GDI then
// thins the stems to 1 px. The fringe is clipped at the border instead.
QRect MeasureInk(const QFont& font, const QString& label)
{
    const QFontMetrics metrics(font);
    const int iMargin = metrics.height();
    const QPoint ptOrigin(iMargin, iMargin + metrics.ascent());
    QImage imgScratch(metrics.horizontalAdvance(label) + 2 * iMargin, metrics.height() + 2 * iMargin, QImage::Format_ARGB32_Premultiplied);
    imgScratch.fill(Qt::transparent);
    QPainter painter(&imgScratch);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setFont(font);
    painter.setPen(Qt::black);
    painter.drawText(ptOrigin, label);
    painter.end();

    QRect rectInk;
    for (int iY = 0; iY < imgScratch.height(); ++iY)
    {
        const QRgb* pLine = reinterpret_cast<const QRgb*>(imgScratch.constScanLine(iY));
        for (int iX = 0; iX < imgScratch.width(); ++iX)
        {
            if (qAlpha(pLine[iX]) > kInkFringeAlpha)
            {
                rectInk |= QRect(iX, iY, 1, 1);
            }
        }
    }
    return rectInk.translated(-ptOrigin);
}

bool Fits(const QFont& font, const QString& label, const QSize& space)
{
    const QRect rectInk = MeasureInk(font, label);
    return rectInk.width() <= space.width() && rectInk.height() <= space.height();
}

// The widest two-digit number in this font: the digit reaching furthest left of its advance, then the one
// reaching furthest right. Sizing for it keeps 0-99 at one size; "88" is not the widest once hinted.
QString BuildWidestTwoDigitLabel(const QFont& font)
{
    const QFontMetrics metrics(font);
    QChar chFirst = QLatin1Char('0');
    QChar chSecond = QLatin1Char('0');
    int iFirstWidth = std::numeric_limits<int>::min();
    int iSecondRight = std::numeric_limits<int>::min();
    for (char cDigit = '0'; cDigit <= '9'; ++cDigit)
    {
        const QChar chDigit = QLatin1Char(cDigit);
        const QRect rectInk = MeasureInk(font, QString(chDigit));
        const int iWidthToNext = metrics.horizontalAdvance(chDigit) - rectInk.left();
        if (iWidthToNext > iFirstWidth)
        {
            iFirstWidth = iWidthToNext;
            chFirst = chDigit;
        }
        if (rectInk.right() > iSecondRight)
        {
            iSecondRight = rectInk.right();
            chSecond = chDigit;
        }
    }
    return QString(chFirst) + chSecond;
}

// The largest font at which every two-digit number fits `space`. Measuring takes a few milliseconds per
// size and only depends on the space, so it is worked out once per space.
int FindTwoDigitFontSize(const QSize& space)
{
    static QHash<QPair<int, int>, int> hashFontSizes;
    const QPair<int, int> pairKey(space.width(), space.height());
    const auto it = hashFontSizes.constFind(pairKey);
    if (it != hashFontSizes.constEnd())
    {
        return it.value();
    }
    int iFontSize = qCeil(space.height() * kMaxFontToSpaceRatio);
    while (iFontSize > 1)
    {
        const QFont font = MakeLabelFont(iFontSize);
        if (Fits(font, BuildWidestTwoDigitLabel(font), space))
        {
            break;
        }
        --iFontSize;
    }
    hashFontSizes.insert(pairKey, iFontSize);
    return iFontSize;
}
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

QString TrayIconPainter::FormatLabel(const TrayIconState& state)
{
    if (!state.m_bFiveHourValid)
    {
        return kEmptyLabel;
    }
    return QString::number(qRound(ClampPercent(state.m_dFiveHourPercent)));
}

QString TrayIconPainter::FormatToolTip(const TrayIconState& state)
{
    if (!state.m_bFiveHourValid && !state.m_bSevenDayValid)
    {
        return kToolTipTitle + QLatin1Char('\n') + kToolTipNoLimits;
    }
    const QString strFive = state.m_bFiveHourValid ? Formatters::FormatPercent(state.m_dFiveHourPercent) : kEmptyLabel;
    const QString strSeven = state.m_bSevenDayValid ? Formatters::FormatPercent(state.m_dSevenDayPercent) : kEmptyLabel;
    return QStringLiteral("%1\n5시간 %2 · 주간 %3").arg(kToolTipTitle, strFive, strSeven);
}

QColor TrayIconPainter::GetInkColor(EAppearance appearance)
{
    return appearance == EAppearance::DARK ? QColor(Qt::white) : QColor(Qt::black);
}

QColor TrayIconPainter::GetLevelColor(EBarLevel level, EAppearance appearance)
{
    const bool bDark = appearance == EAppearance::DARK;
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

double TrayIconPainter::GetLineWidth(int pixelSize)
{
    // Whole pixels keep the straight sides crisp (20 px: 2.5 → 3).
    return std::max(1.0, std::round(pixelSize * kLineWidthRatio));
}

QRectF TrayIconPainter::GetBorderRect(int pixelSize)
{
    const double dLineWidth = GetLineWidth(pixelSize);
    return QRectF(dLineWidth / 2.0, dLineWidth / 2.0, pixelSize - dLineWidth, pixelSize - dLineWidth);
}

QImage TrayIconPainter::Render(const TrayIconState& state, EAppearance appearance, int pixelSize)
{
    const QColor clrInk = GetInkColor(appearance);
    QColor clrTrack = clrInk;
    clrTrack.setAlphaF(kTrackAlpha);
    const EBarLevel eFiveLevel = state.m_bFiveHourValid
        ? ClassifyLevel(state.m_dFiveHourPercent, state.m_iWarnPercent, state.m_iCriticalPercent)
        : EBarLevel::NORMAL;
    const EBarLevel eSevenLevel = state.m_bSevenDayValid
        ? ClassifyLevel(state.m_dSevenDayPercent, state.m_iWarnPercent, state.m_iCriticalPercent)
        : EBarLevel::NORMAL;
    const double dFraction = state.m_bSevenDayValid ? ClampPercent(state.m_dSevenDayPercent) / 100.0 : 0.0;

    QImage imgIcon(pixelSize, pixelSize, QImage::Format_ARGB32_Premultiplied);
    imgIcon.fill(Qt::transparent);
    {
        QPainter painter(&imgIcon);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        DrawBorder(painter, pixelSize, dFraction, GetLevelColor(eSevenLevel, appearance), clrTrack);
        DrawLabel(painter, pixelSize, FormatLabel(state), GetLevelColor(eFiveLevel, appearance));
    }

    const bool bEmpty = !state.m_bFiveHourValid && !state.m_bSevenDayValid;
    if (!state.m_bStale && !bEmpty)
    {
        return imgIcon;
    }
    // Dim the finished drawing as one layer; dimming each stroke would let the track show through the progress.
    QImage imgDimmed(pixelSize, pixelSize, QImage::Format_ARGB32_Premultiplied);
    imgDimmed.fill(Qt::transparent);
    QPainter painter(&imgDimmed);
    painter.setOpacity(kDimmedOpacity);
    painter.drawImage(0, 0, imgIcon);
    painter.end();
    return imgDimmed;
}

double TrayIconPainter::ClampPercent(double percent)
{
    return std::clamp(percent, 0.0, 100.0);
}

void TrayIconPainter::DrawBorder(QPainter& painter, int pixelSize, double fraction, const QColor& progressColor, const QColor& trackColor)
{
    const QRectF rectBox = GetBorderRect(pixelSize);
    const double dRadius = std::min(pixelSize * kCornerRadiusRatio, rectBox.width() / 2.0);
    QPen pen(trackColor, GetLineWidth(pixelSize), Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin);
    painter.setBrush(Qt::NoBrush);

    // The track only covers what progress does not, so the two never overlap.
    if (fraction < 1.0)
    {
        painter.setPen(pen);
        painter.drawPath(BuildBorderPath(rectBox, dRadius, fraction, 1.0));
    }
    if (fraction > 0.0)
    {
        pen.setColor(progressColor);
        painter.setPen(pen);
        painter.drawPath(BuildBorderPath(rectBox, dRadius, 0.0, fraction));
    }
}

void TrayIconPainter::DrawLabel(QPainter& painter, int pixelSize, const QString& label, const QColor& color)
{
    // From 24 px up a gap between number and border costs little; below that it would thin the digits to 1 px stems.
    const int iPadding = pixelSize >= kPaddedFromSize ? pixelSize / kPaddingDivisor : 0;
    const int iInset = qCeil(GetLineWidth(pixelSize)) + iPadding;
    const QRect rectSpace(iInset, iInset, pixelSize - 2 * iInset, pixelSize - 2 * iInset);

    // Size for the widest two-digit number so 0-99 all look alike; only 100 shrinks further.
    int iFontSize = FindTwoDigitFontSize(rectSpace.size());
    // 100 has the narrow 1 on tabular-width digits; closing up its spacing buys a size at 16 px.
    const double dLetterSpacing = label.size() > kTwoDigits ? -std::max(1, pixelSize / kLetterSpacingDivisor) : 0.0;
    const auto funcMakeFont = [dLetterSpacing](int fontSize)
    {
        QFont font = MakeLabelFont(fontSize);
        font.setLetterSpacing(QFont::AbsoluteSpacing, dLetterSpacing);
        return font;
    };
    while (iFontSize > 1 && !Fits(funcMakeFont(iFontSize), label, rectSpace.size()))
    {
        --iFontSize;
    }

    const QFont font = funcMakeFont(iFontSize);
    const QRect rectInk = MeasureInk(font, label);
    // A whole-pixel origin draws the glyphs exactly as measured.
    const QPoint ptOrigin(rectSpace.left() + (rectSpace.width() - rectInk.width()) / 2 - rectInk.left(),
        rectSpace.top() + (rectSpace.height() - rectInk.height()) / 2 - rectInk.top());

    // Keep the unmeasured fringe off the border: clip to the stroke's inner edge.
    const double dLineWidth = GetLineWidth(pixelSize);
    const double dInnerRadius = std::max(0.0, pixelSize * kCornerRadiusRatio - dLineWidth / 2.0);
    QPainterPath pathInterior;
    pathInterior.addRoundedRect(QRectF(dLineWidth, dLineWidth, pixelSize - 2.0 * dLineWidth, pixelSize - 2.0 * dLineWidth),
        dInnerRadius, dInnerRadius);
    painter.save();
    painter.setClipPath(pathInterior);
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(ptOrigin, label);
    painter.restore();
}
