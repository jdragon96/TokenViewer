#pragma once

#include <QColor>
#include <QImage>
#include <QRectF>
#include <QString>

class QPainter;

enum class EBarLevel
{
    NORMAL,
    WARNING,
    CRITICAL,
};

enum class EAppearance
{
    LIGHT,
    DARK,
};

struct TrayIconState
{
    bool m_bFiveHourValid = false;
    double m_dFiveHourPercent = 0.0;
    bool m_bSevenDayValid = false;
    double m_dSevenDayPercent = 0.0;
    bool m_bStale = false;
    int m_iWarnPercent = 70;
    int m_iCriticalPercent = 90;
};

bool operator==(const TrayIconState& lhs, const TrayIconState& rhs);
bool operator!=(const TrayIconState& lhs, const TrayIconState& rhs);

// Draws the square tray icon: the 5-hour % as a number inside a rounded box whose border fills with the
// weekly %, starting at nine o'clock and running counter-clockwise. A full border means the week is used up.
class TrayIconPainter
{
public:
    static constexpr double kDimmedOpacity = 0.5;
    static constexpr double kTrackAlpha = 0.3;

public:
    static EBarLevel ClassifyLevel(double percent, int warnPercent, int criticalPercent);
    static QString FormatLabel(const TrayIconState& state);
    static QString FormatToolTip(const TrayIconState& state);
    static QColor GetInkColor(EAppearance appearance);
    static QColor GetLevelColor(EBarLevel level, EAppearance appearance);
    static double GetLineWidth(int pixelSize);
    static QRectF GetBorderRect(int pixelSize);
    static QImage Render(const TrayIconState& state, EAppearance appearance, int pixelSize);

private:
    static double ClampPercent(double percent);
    static void DrawBorder(QPainter& painter, int pixelSize, double fraction, const QColor& progressColor, const QColor& trackColor);
    static void DrawLabel(QPainter& painter, int pixelSize, const QString& label, const QColor& color);
};
