#pragma once

#include <QColor>
#include <QImage>
#include <QString>

class QPainter;

enum class EBarLevel
{
    NORMAL,
    WARNING,
    CRITICAL,
};

enum class EMenuBarAppearance
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

struct TrayIconImage
{
    QImage m_imgIcon;
    bool m_bTemplate = true;
};

class TrayIconPainter
{
public:
    static constexpr double kHeight = 18.0;
    static constexpr double kBarWidth = 22.0;
    static constexpr double kBarHeight = 4.0;
    static constexpr double kFiveHourBarTop = 4.0;
    static constexpr double kSevenDayBarTop = 10.0;
    static constexpr double kTextGap = 5.0;
    static constexpr int kFontPixelSize = 13;
    static constexpr double kDimmedOpacity = 0.5;

public:
    static EBarLevel ClassifyLevel(double percent, int warnPercent, int criticalPercent);
    static bool IsTemplate(const TrayIconState& state);
    static QString FormatLabel(const TrayIconState& state);
    static QColor GetInkColor(EMenuBarAppearance appearance);
    static QColor GetLevelColor(EBarLevel level, EMenuBarAppearance appearance);
    static TrayIconImage Render(const TrayIconState& state, EMenuBarAppearance appearance, qreal devicePixelRatio);

private:
    static double ClampPercent(double percent);
    static void DrawBar(QPainter& painter, double top, bool valid, double percent, const QColor& fillColor, const QColor& outlineColor);
};
