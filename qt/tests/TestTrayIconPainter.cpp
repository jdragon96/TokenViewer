#include "UI/TrayIconPainter.h"

#include <QtTest>

#include <cstdlib>

namespace
{
const QColor kWarnLight(0xc9, 0x85, 0x00);
const QColor kCriticalLight(0xd0, 0x3b, 0x3b);
const QColor kWarnDark(0xfa, 0xb2, 0x19);
const QColor kCriticalDark(0xff, 0x6b, 0x6b);
constexpr int kOpaqueAlpha = 200;
constexpr int kColorTolerance = 12;
constexpr int kAllSizes[] = {16, 20, 24, 32, 40, 48, 64};

TrayIconState MakeState(double fiveHourPercent, double sevenDayPercent)
{
    TrayIconState state;
    state.m_bFiveHourValid = true;
    state.m_dFiveHourPercent = fiveHourPercent;
    state.m_bSevenDayValid = true;
    state.m_dSevenDayPercent = sevenDayPercent;
    return state;
}

bool IsClose(QRgb rgb, const QColor& color)
{
    // Pixels are premultiplied; compare the straight colour.
    const QColor clrPixel = QColor::fromRgba(qUnpremultiply(rgb));
    return std::abs(clrPixel.red() - color.red()) <= kColorTolerance
        && std::abs(clrPixel.green() - color.green()) <= kColorTolerance
        && std::abs(clrPixel.blue() - color.blue()) <= kColorTolerance;
}

// Pixels on the border's stroke, named by where they sit on the box.
struct Border
{
    explicit Border(int pixelSize)
        : m_iSize(pixelSize)
        , m_rectBox(TrayIconPainter::GetBorderRect(pixelSize))
        , m_dRadius(pixelSize / 4.0)
        , m_dLineWidth(TrayIconPainter::GetLineWidth(pixelSize))
    {
    }

    QPoint GetLeftBelowStart() const { return ConvertToPixel(m_rectBox.left(), m_rectBox.center().y() + m_iSize / 8.0); }
    QPoint GetLeftAboveStart() const { return ConvertToPixel(m_rectBox.left(), m_rectBox.center().y() - m_iSize / 8.0 - 1.0); }
    QPoint GetBottomLeft() const { return ConvertToPixel(m_rectBox.left() + m_dRadius + m_iSize / 16.0, m_rectBox.bottom()); }
    // Where the bottom-right corner starts.
    QPoint GetBottomRight() const { return ConvertToPixel(m_rectBox.right() - m_dRadius, m_rectBox.bottom()); }
    QPoint GetRightMiddle() const { return ConvertToPixel(m_rectBox.right(), m_rectBox.center().y()); }
    QPoint GetTopMiddle() const { return ConvertToPixel(m_rectBox.center().x(), m_rectBox.top()); }
    // Inside the stroke's inner edge, leaving out the rounded corners the stroke cuts across.
    QRegion GetInterior() const
    {
        const int iInset = qCeil(m_dLineWidth);
        const int iCorner = qCeil(m_dRadius);
        return QRegion(iInset, iCorner, m_iSize - 2 * iInset, m_iSize - 2 * iCorner)
            | QRegion(iCorner, iInset, m_iSize - 2 * iCorner, m_iSize - 2 * iInset);
    }

    static QPoint ConvertToPixel(double x, double y)
    {
        return QPoint(qFloor(x), qFloor(y));
    }

    int m_iSize;
    QRectF m_rectBox;
    double m_dRadius;
    double m_dLineWidth;
};

QPoint Clamp(const QImage& image, const QPoint& point)
{
    return QPoint(qBound(0, point.x(), image.width() - 1), qBound(0, point.y(), image.height() - 1));
}

int GetAlphaAt(const QImage& image, const QPoint& point)
{
    return qAlpha(image.pixel(Clamp(image, point)));
}

bool IsFilledWith(const QImage& image, const QPoint& point, const QColor& color)
{
    const QRgb rgb = image.pixel(Clamp(image, point));
    return qAlpha(rgb) > kOpaqueAlpha && IsClose(rgb, color);
}

bool IsTrack(const QImage& image, const QPoint& point)
{
    // Ink at 30% alpha: about 77 when the pixel is fully covered.
    const int iAlpha = GetAlphaAt(image, point);
    return iAlpha > 50 && iAlpha < 110;
}

bool Contains(const QImage& image, const QRegion& region, const QColor& color)
{
    for (const QRect& rect : region)
    {
        for (int iY = rect.top(); iY <= rect.bottom(); ++iY)
        {
            for (int iX = rect.left(); iX <= rect.right(); ++iX)
            {
                const QRgb rgb = image.pixel(iX, iY);
                if (qAlpha(rgb) > kOpaqueAlpha && IsClose(rgb, color))
                {
                    return true;
                }
            }
        }
    }
    return false;
}

// The bounding box of pixels inside `region` more opaque than `alpha`.
QRect GetInkBounds(const QImage& image, const QRegion& region, int alpha)
{
    QRect rectBounds;
    for (const QRect& rect : region)
    {
        for (int iY = rect.top(); iY <= rect.bottom(); ++iY)
        {
            for (int iX = rect.left(); iX <= rect.right(); ++iX)
            {
                if (qAlpha(image.pixel(iX, iY)) > alpha)
                {
                    rectBounds |= QRect(iX, iY, 1, 1);
                }
            }
        }
    }
    return rectBounds;
}

QString Describe(const QImage& image, const QPoint& point)
{
    const QRgb rgb = image.pixel(Clamp(image, point));
    return QStringLiteral("(%1,%2) argb=%3").arg(point.x()).arg(point.y()).arg(rgb, 8, 16, QLatin1Char('0'));
}
}

class TestTrayIconPainter : public QObject
{
    Q_OBJECT

private slots:
    void TestClassifyLevelThresholds();
    void TestFormatLabelRoundsAndClamps();
    void TestFormatToolTip();
    void TestRendersEverySizeNatively();
    void TestFillsTheBorderFromNineOClockDownward_data();
    void TestFillsTheBorderFromNineOClockDownward();
    void TestLeavesTheRightSideAsTrackBelowHalf_data();
    void TestLeavesTheRightSideAsTrackBelowHalf();
    void TestContinuesUpTheRightThenAcrossTheTop_data();
    void TestContinuesUpTheRightThenAcrossTheTop();
    void TestFullWeekClosesTheBorder_data();
    void TestFullWeekClosesTheBorder();
    void TestEmptyWeekLeavesOnlyTrack_data();
    void TestEmptyWeekLeavesOnlyTrack();
    void TestColorsTheNumberByTheFiveHourLevel();
    void TestColorsTheBorderByTheWeeklyLevel();
    void TestInkFollowsTaskbarAppearance();
    void TestDimsStaleStateAsOneLayer();
    void TestDimsEmptyState();
    void TestNumberStaysInsideTheBox_data();
    void TestNumberStaysInsideTheBox();
    void TestNumberFillsTheBox();
    void TestOneAndTwoDigitNumbersShareOneSize();
};

void TestTrayIconPainter::TestClassifyLevelThresholds()
{
    QCOMPARE(TrayIconPainter::ClassifyLevel(69.9, 70, 90), EBarLevel::NORMAL);
    QCOMPARE(TrayIconPainter::ClassifyLevel(70.0, 70, 90), EBarLevel::WARNING);
    QCOMPARE(TrayIconPainter::ClassifyLevel(89.9, 70, 90), EBarLevel::WARNING);
    QCOMPARE(TrayIconPainter::ClassifyLevel(90.0, 70, 90), EBarLevel::CRITICAL);
    QCOMPARE(TrayIconPainter::ClassifyLevel(100.0, 70, 90), EBarLevel::CRITICAL);
}

void TestTrayIconPainter::TestFormatLabelRoundsAndClamps()
{
    QCOMPARE(TrayIconPainter::FormatLabel(MakeState(42.4, 0.0)), QStringLiteral("42"));
    QCOMPARE(TrayIconPainter::FormatLabel(MakeState(41.6, 0.0)), QStringLiteral("42"));
    QCOMPARE(TrayIconPainter::FormatLabel(MakeState(99.6, 0.0)), QStringLiteral("100"));
    QCOMPARE(TrayIconPainter::FormatLabel(MakeState(130.0, 0.0)), QStringLiteral("100"));
    QCOMPARE(TrayIconPainter::FormatLabel(MakeState(-3.0, 0.0)), QStringLiteral("0"));
    QCOMPARE(TrayIconPainter::FormatLabel(TrayIconState()), QStringLiteral("—"));

    TrayIconState stateWeeklyOnly;
    stateWeeklyOnly.m_bSevenDayValid = true;
    stateWeeklyOnly.m_dSevenDayPercent = 50.0;
    QCOMPARE(TrayIconPainter::FormatLabel(stateWeeklyOnly), QStringLiteral("—"));
}

void TestTrayIconPainter::TestFormatToolTip()
{
    QCOMPARE(TrayIconPainter::FormatToolTip(MakeState(42.0, 18.0)), QStringLiteral("Claude 사용량\n5시간 42% · 주간 18%"));
    QCOMPARE(TrayIconPainter::FormatToolTip(MakeState(41.6, 130.0)), QStringLiteral("Claude 사용량\n5시간 42% · 주간 100%"));
    QCOMPARE(TrayIconPainter::FormatToolTip(TrayIconState()), QStringLiteral("Claude 사용량\n한도 정보 없음"));

    TrayIconState stateFiveOnly = MakeState(42.0, 0.0);
    stateFiveOnly.m_bSevenDayValid = false;
    QCOMPARE(TrayIconPainter::FormatToolTip(stateFiveOnly), QStringLiteral("Claude 사용량\n5시간 42% · 주간 —"));

    TrayIconState stateSevenOnly = MakeState(0.0, 18.0);
    stateSevenOnly.m_bFiveHourValid = false;
    QCOMPARE(TrayIconPainter::FormatToolTip(stateSevenOnly), QStringLiteral("Claude 사용량\n5시간 — · 주간 18%"));
}

void TestTrayIconPainter::TestRendersEverySizeNatively()
{
    for (const int iSize : kAllSizes)
    {
        const QImage imgIcon = TrayIconPainter::Render(MakeState(42.0, 18.0), EAppearance::DARK, iSize);
        QCOMPARE(imgIcon.size(), QSize(iSize, iSize));
        QCOMPARE(imgIcon.devicePixelRatio(), 1.0);
        QVERIFY(imgIcon.hasAlphaChannel());
    }
    QCOMPARE(TrayIconPainter::GetLineWidth(16), 2.0);
    QCOMPARE(TrayIconPainter::GetLineWidth(32), 4.0);
    QCOMPARE(TrayIconPainter::GetBorderRect(16), QRectF(1.0, 1.0, 14.0, 14.0));
    QCOMPARE(TrayIconPainter::GetBorderRect(32), QRectF(2.0, 2.0, 28.0, 28.0));
}

void TestTrayIconPainter::TestFillsTheBorderFromNineOClockDownward_data()
{
    QTest::addColumn<int>("iSize");
    QTest::newRow("16") << 16;
    QTest::newRow("24") << 24;
    QTest::newRow("32") << 32;
}

void TestTrayIconPainter::TestFillsTheBorderFromNineOClockDownward()
{
    QFETCH(int, iSize);
    // 30%: down the left side, round the corner, part of the bottom.
    const QImage imgIcon = TrayIconPainter::Render(MakeState(10.0, 30.0), EAppearance::LIGHT, iSize);
    const Border border(iSize);
    QVERIFY2(IsFilledWith(imgIcon, border.GetLeftBelowStart(), Qt::black), qPrintable(Describe(imgIcon, border.GetLeftBelowStart())));
    QVERIFY2(IsFilledWith(imgIcon, border.GetBottomLeft(), Qt::black), qPrintable(Describe(imgIcon, border.GetBottomLeft())));
    QVERIFY2(IsTrack(imgIcon, border.GetBottomRight()), qPrintable(Describe(imgIcon, border.GetBottomRight())));
    QVERIFY2(IsTrack(imgIcon, border.GetRightMiddle()), qPrintable(Describe(imgIcon, border.GetRightMiddle())));
    QVERIFY2(IsTrack(imgIcon, border.GetTopMiddle()), qPrintable(Describe(imgIcon, border.GetTopMiddle())));
    QVERIFY2(IsTrack(imgIcon, border.GetLeftAboveStart()), qPrintable(Describe(imgIcon, border.GetLeftAboveStart())));
}

void TestTrayIconPainter::TestLeavesTheRightSideAsTrackBelowHalf_data()
{
    QTest::addColumn<int>("iSize");
    QTest::newRow("16") << 16;
    QTest::newRow("32") << 32;
}

void TestTrayIconPainter::TestLeavesTheRightSideAsTrackBelowHalf()
{
    QFETCH(int, iSize);
    const QImage imgIcon = TrayIconPainter::Render(MakeState(10.0, 40.0), EAppearance::DARK, iSize);
    const Border border(iSize);
    QVERIFY2(IsFilledWith(imgIcon, border.GetBottomLeft(), Qt::white), qPrintable(Describe(imgIcon, border.GetBottomLeft())));
    QVERIFY2(IsTrack(imgIcon, border.GetRightMiddle()), qPrintable(Describe(imgIcon, border.GetRightMiddle())));
    QVERIFY(IsClose(imgIcon.pixel(border.GetRightMiddle()), Qt::white));
}

void TestTrayIconPainter::TestContinuesUpTheRightThenAcrossTheTop_data()
{
    QTest::addColumn<int>("iSize");
    QTest::newRow("16") << 16;
    QTest::newRow("32") << 32;
}

void TestTrayIconPainter::TestContinuesUpTheRightThenAcrossTheTop()
{
    QFETCH(int, iSize);
    const Border border(iSize);
    // 60%: past the right side's middle, short of the top's middle.
    const QImage imgSixty = TrayIconPainter::Render(MakeState(10.0, 60.0), EAppearance::LIGHT, iSize);
    QVERIFY2(IsFilledWith(imgSixty, border.GetBottomRight(), Qt::black), qPrintable(Describe(imgSixty, border.GetBottomRight())));
    QVERIFY2(IsFilledWith(imgSixty, border.GetRightMiddle(), Qt::black), qPrintable(Describe(imgSixty, border.GetRightMiddle())));
    QVERIFY2(IsTrack(imgSixty, border.GetTopMiddle()), qPrintable(Describe(imgSixty, border.GetTopMiddle())));
    // 80%: past the top's middle, not yet back down the left.
    const QImage imgEighty = TrayIconPainter::Render(MakeState(10.0, 80.0), EAppearance::LIGHT, iSize);
    const QColor clrWarn = TrayIconPainter::GetLevelColor(EBarLevel::WARNING, EAppearance::LIGHT);
    QVERIFY2(IsFilledWith(imgEighty, border.GetTopMiddle(), clrWarn), qPrintable(Describe(imgEighty, border.GetTopMiddle())));
    QVERIFY2(IsTrack(imgEighty, border.GetLeftAboveStart()), qPrintable(Describe(imgEighty, border.GetLeftAboveStart())));
}

void TestTrayIconPainter::TestFullWeekClosesTheBorder_data()
{
    QTest::addColumn<int>("iSize");
    QTest::newRow("16") << 16;
    QTest::newRow("20") << 20;
    QTest::newRow("32") << 32;
    QTest::newRow("64") << 64;
}

void TestTrayIconPainter::TestFullWeekClosesTheBorder()
{
    QFETCH(int, iSize);
    // Weekly 100% is critical, so the ring is red; the number stays normal.
    const QImage imgIcon = TrayIconPainter::Render(MakeState(10.0, 100.0), EAppearance::LIGHT, iSize);
    const Border border(iSize);
    for (const QPoint& ptBorder : {border.GetLeftBelowStart(), border.GetBottomLeft(), border.GetBottomRight(), border.GetRightMiddle(),
                                   border.GetTopMiddle(), border.GetLeftAboveStart()})
    {
        QVERIFY2(IsFilledWith(imgIcon, ptBorder, kCriticalLight), qPrintable(Describe(imgIcon, ptBorder)));
    }
}

void TestTrayIconPainter::TestEmptyWeekLeavesOnlyTrack_data()
{
    QTest::addColumn<int>("iSize");
    QTest::newRow("16") << 16;
    QTest::newRow("32") << 32;
}

void TestTrayIconPainter::TestEmptyWeekLeavesOnlyTrack()
{
    QFETCH(int, iSize);
    const QImage imgIcon = TrayIconPainter::Render(MakeState(10.0, 0.0), EAppearance::LIGHT, iSize);
    const Border border(iSize);
    for (const QPoint& ptBorder : {border.GetLeftBelowStart(), border.GetBottomLeft(), border.GetBottomRight(), border.GetRightMiddle(),
                                   border.GetTopMiddle(), border.GetLeftAboveStart()})
    {
        QVERIFY2(IsTrack(imgIcon, ptBorder), qPrintable(Describe(imgIcon, ptBorder)));
        QVERIFY(IsClose(imgIcon.pixel(ptBorder), Qt::black));
    }
}

void TestTrayIconPainter::TestColorsTheNumberByTheFiveHourLevel()
{
    for (const int iSize : {16, 32})
    {
        const Border border(iSize);
        const QImage imgWarning = TrayIconPainter::Render(MakeState(75.0, 10.0), EAppearance::LIGHT, iSize);
        QVERIFY(Contains(imgWarning, border.GetInterior(), kWarnLight));
        // The border stays in the taskbar ink.
        QVERIFY(IsFilledWith(imgWarning, border.GetLeftBelowStart(), Qt::black));

        const QImage imgCritical = TrayIconPainter::Render(MakeState(95.0, 10.0), EAppearance::DARK, iSize);
        QVERIFY(Contains(imgCritical, border.GetInterior(), kCriticalDark));
        QVERIFY(IsFilledWith(imgCritical, border.GetLeftBelowStart(), Qt::white));
    }
}

void TestTrayIconPainter::TestColorsTheBorderByTheWeeklyLevel()
{
    for (const int iSize : {16, 32})
    {
        const Border border(iSize);
        const QImage imgCritical = TrayIconPainter::Render(MakeState(10.0, 95.0), EAppearance::LIGHT, iSize);
        QVERIFY(IsFilledWith(imgCritical, border.GetBottomLeft(), kCriticalLight));
        // The five-hour number stays in the taskbar ink.
        QVERIFY(!Contains(imgCritical, border.GetInterior(), kCriticalLight));
        QVERIFY(Contains(imgCritical, border.GetInterior(), Qt::black));

        const QImage imgWarning = TrayIconPainter::Render(MakeState(10.0, 75.0), EAppearance::DARK, iSize);
        QVERIFY(IsFilledWith(imgWarning, border.GetBottomLeft(), kWarnDark));
        QVERIFY(!Contains(imgWarning, border.GetInterior(), kWarnDark));
    }
}

void TestTrayIconPainter::TestInkFollowsTaskbarAppearance()
{
    const Border border(32);
    const QImage imgDark = TrayIconPainter::Render(MakeState(42.0, 18.0), EAppearance::DARK, 32);
    QVERIFY(IsFilledWith(imgDark, border.GetLeftBelowStart(), Qt::white));
    QVERIFY(Contains(imgDark, border.GetInterior(), Qt::white));
    QVERIFY(IsClose(imgDark.pixel(border.GetTopMiddle()), Qt::white));

    const QImage imgLight = TrayIconPainter::Render(MakeState(42.0, 18.0), EAppearance::LIGHT, 32);
    QVERIFY(IsFilledWith(imgLight, border.GetLeftBelowStart(), Qt::black));
    QVERIFY(Contains(imgLight, border.GetInterior(), Qt::black));
    QVERIFY(IsClose(imgLight.pixel(border.GetTopMiddle()), Qt::black));

    QCOMPARE(TrayIconPainter::GetLevelColor(EBarLevel::WARNING, EAppearance::LIGHT), kWarnLight);
    QCOMPARE(TrayIconPainter::GetLevelColor(EBarLevel::WARNING, EAppearance::DARK), kWarnDark);
    QCOMPARE(TrayIconPainter::GetLevelColor(EBarLevel::CRITICAL, EAppearance::LIGHT), kCriticalLight);
    QCOMPARE(TrayIconPainter::GetLevelColor(EBarLevel::CRITICAL, EAppearance::DARK), kCriticalDark);
    QCOMPARE(TrayIconPainter::GetLevelColor(EBarLevel::NORMAL, EAppearance::DARK), QColor(Qt::white));
    QCOMPARE(TrayIconPainter::GetLevelColor(EBarLevel::NORMAL, EAppearance::LIGHT), QColor(Qt::black));
}

void TestTrayIconPainter::TestDimsStaleStateAsOneLayer()
{
    for (const int iSize : {16, 32})
    {
        TrayIconState state = MakeState(42.0, 30.0);
        state.m_bStale = true;
        const QImage imgIcon = TrayIconPainter::Render(state, EAppearance::LIGHT, iSize);
        const Border border(iSize);
        // Progress at half opacity, with no track showing through it.
        const int iProgressAlpha = GetAlphaAt(imgIcon, border.GetLeftBelowStart());
        QVERIFY2(iProgressAlpha >= 118 && iProgressAlpha <= 137, qPrintable(Describe(imgIcon, border.GetLeftBelowStart())));
        QVERIFY(IsClose(imgIcon.pixel(border.GetLeftBelowStart()), Qt::black));
        const int iTrackAlpha = GetAlphaAt(imgIcon, border.GetRightMiddle());
        QVERIFY2(iTrackAlpha >= 30 && iTrackAlpha <= 48, qPrintable(Describe(imgIcon, border.GetRightMiddle())));
        // Nothing in the image is more than half opaque.
        QVERIFY(GetInkBounds(imgIcon, imgIcon.rect(), 132).isEmpty());
    }
}

void TestTrayIconPainter::TestDimsEmptyState()
{
    for (const int iSize : {16, 32})
    {
        const QImage imgIcon = TrayIconPainter::Render(TrayIconState(), EAppearance::DARK, iSize);
        const Border border(iSize);
        for (const QPoint& ptBorder : {border.GetLeftBelowStart(), border.GetBottomLeft(), border.GetRightMiddle(), border.GetTopMiddle()})
        {
            const int iAlpha = GetAlphaAt(imgIcon, ptBorder);
            QVERIFY2(iAlpha >= 30 && iAlpha <= 48, qPrintable(Describe(imgIcon, ptBorder)));
        }
        // The dash is drawn, at half opacity.
        QVERIFY(!GetInkBounds(imgIcon, border.GetInterior(), 60).isEmpty());
        QVERIFY(GetInkBounds(imgIcon, imgIcon.rect(), 132).isEmpty());
    }
}

void TestTrayIconPainter::TestNumberStaysInsideTheBox_data()
{
    QTest::addColumn<double>("dFiveHour");
    QTest::newRow("100") << 100.0;
    QTest::newRow("88") << 88.0;
    QTest::newRow("8") << 8.0;
    QTest::newRow("dash") << -1.0;
}

void TestTrayIconPainter::TestNumberStaysInsideTheBox()
{
    QFETCH(double, dFiveHour);
    TrayIconState state = MakeState(dFiveHour, 0.0);
    state.m_bFiveHourValid = dFiveHour >= 0.0;
    for (const int iSize : kAllSizes)
    {
        // With an empty week the ring is pure track, so anything stronger outside the interior is spilt number.
        const QImage imgIcon = TrayIconPainter::Render(state, EAppearance::LIGHT, iSize);
        const QRegion regionInterior = Border(iSize).GetInterior();
        for (int iY = 0; iY < iSize; ++iY)
        {
            for (int iX = 0; iX < iSize; ++iX)
            {
                if (regionInterior.contains(QPoint(iX, iY)))
                {
                    continue;
                }
                QVERIFY2(qAlpha(imgIcon.pixel(iX, iY)) <= 80,
                    qPrintable(QStringLiteral("size %1: %2").arg(iSize).arg(Describe(imgIcon, QPoint(iX, iY)))));
            }
        }
        QVERIFY(!GetInkBounds(imgIcon, regionInterior, 128).isEmpty());
    }
}

void TestTrayIconPainter::TestNumberFillsTheBox()
{
    for (const int iSize : {16, 32, 64})
    {
        const QRegion regionInterior = Border(iSize).GetInterior();
        const QRect rectInterior = regionInterior.boundingRect();
        const QRect rectTwoDigits = GetInkBounds(TrayIconPainter::Render(MakeState(88.0, 0.0), EAppearance::LIGHT, iSize), regionInterior, 128);
        const QRect rectThreeDigits = GetInkBounds(TrayIconPainter::Render(MakeState(100.0, 0.0), EAppearance::LIGHT, iSize), regionInterior, 128);
        // Two digits use most of the box width; 100 is shrunk to fit.
        QVERIFY2(rectTwoDigits.width() >= rectInterior.width() * 0.7,
            qPrintable(QStringLiteral("size %1: %2 of %3").arg(iSize).arg(rectTwoDigits.width()).arg(rectInterior.width())));
        QVERIFY(rectThreeDigits.height() < rectTwoDigits.height());
        // Both are centred.
        QVERIFY(qAbs(rectTwoDigits.center().y() - rectInterior.center().y()) <= qMax(1, iSize / 16));
        QVERIFY(qAbs(rectTwoDigits.center().x() - rectInterior.center().x()) <= qMax(1, iSize / 16));
    }
}

void TestTrayIconPainter::TestOneAndTwoDigitNumbersShareOneSize()
{
    for (const int iSize : kAllSizes)
    {
        const QRegion regionInterior = Border(iSize).GetInterior();
        const int iReferenceHeight = GetInkBounds(TrayIconPainter::Render(MakeState(88.0, 0.0), EAppearance::LIGHT, iSize), regionInterior, 128).height();
        for (int iValue = 0; iValue < 100; ++iValue)
        {
            const QImage imgIcon = TrayIconPainter::Render(MakeState(iValue, 0.0), EAppearance::LIGHT, iSize);
            const int iHeight = GetInkBounds(imgIcon, regionInterior, 128).height();
            QVERIFY2(iHeight == iReferenceHeight,
                qPrintable(QStringLiteral("size %1, value %2: height %3, expected %4").arg(iSize).arg(iValue).arg(iHeight).arg(iReferenceHeight)));
        }
    }
}

QTEST_MAIN(TestTrayIconPainter)
#include "TestTrayIconPainter.moc"
