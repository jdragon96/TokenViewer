#include "UI/TrayIconPainter.h"

#include <QtTest>

namespace
{
TrayIconState MakeState(double fiveHourPercent, double sevenDayPercent)
{
    TrayIconState state;
    state.m_bFiveHourValid = true;
    state.m_dFiveHourPercent = fiveHourPercent;
    state.m_bSevenDayValid = true;
    state.m_dSevenDayPercent = sevenDayPercent;
    return state;
}
}

class TestTrayIconPainter : public QObject
{
    Q_OBJECT

private slots:
    void TestClassifyLevelThresholds();
    void TestIsTemplateOnlyWhenBothNormal();
    void TestFormatLabelRoundsAndClamps();
    void TestRenderFillsFiveHourBarProportionally();
    void TestRenderUsesLevelColorWhenWarning();
    void TestRenderColorsSevenDayBarIndependently();
    void TestRenderDimsStaleState();
    void TestRenderEmptyStateHasNoFill();
    void TestRenderScalesWithDevicePixelRatio();
};

void TestTrayIconPainter::TestClassifyLevelThresholds()
{
    QCOMPARE(TrayIconPainter::ClassifyLevel(69.9, 70, 90), EBarLevel::NORMAL);
    QCOMPARE(TrayIconPainter::ClassifyLevel(70.0, 70, 90), EBarLevel::WARNING);
    QCOMPARE(TrayIconPainter::ClassifyLevel(89.9, 70, 90), EBarLevel::WARNING);
    QCOMPARE(TrayIconPainter::ClassifyLevel(90.0, 70, 90), EBarLevel::CRITICAL);
    QCOMPARE(TrayIconPainter::ClassifyLevel(100.0, 70, 90), EBarLevel::CRITICAL);
}

void TestTrayIconPainter::TestIsTemplateOnlyWhenBothNormal()
{
    QVERIFY(TrayIconPainter::IsTemplate(MakeState(42.0, 18.0)));
    QVERIFY(!TrayIconPainter::IsTemplate(MakeState(78.0, 18.0)));
    QVERIFY(!TrayIconPainter::IsTemplate(MakeState(12.0, 93.0)));
    QVERIFY(TrayIconPainter::IsTemplate(TrayIconState()));
}

void TestTrayIconPainter::TestFormatLabelRoundsAndClamps()
{
    QCOMPARE(TrayIconPainter::FormatLabel(MakeState(42.4, 0.0)), QStringLiteral("42%"));
    QCOMPARE(TrayIconPainter::FormatLabel(MakeState(99.6, 0.0)), QStringLiteral("100%"));
    QCOMPARE(TrayIconPainter::FormatLabel(MakeState(130.0, 0.0)), QStringLiteral("100%"));
    QCOMPARE(TrayIconPainter::FormatLabel(TrayIconState()), QStringLiteral("—"));
}

void TestTrayIconPainter::TestRenderFillsFiveHourBarProportionally()
{
    const TrayIconImage image = TrayIconPainter::Render(MakeState(42.0, 18.0), EMenuBarAppearance::LIGHT, 1.0);
    QVERIFY(image.m_bTemplate);
    const QRgb rgbInside = image.m_imgIcon.pixel(4, 6);
    QCOMPARE(qAlpha(rgbInside), 255);
    QCOMPARE(QColor(rgbInside), QColor(Qt::black));
    // 42% of the 22pt bar ends near x=9, so x=16 is empty bar interior.
    QCOMPARE(qAlpha(image.m_imgIcon.pixel(16, 6)), 0);
}

void TestTrayIconPainter::TestRenderUsesLevelColorWhenWarning()
{
    const TrayIconImage image = TrayIconPainter::Render(MakeState(78.0, 31.0), EMenuBarAppearance::DARK, 1.0);
    QVERIFY(!image.m_bTemplate);
    QCOMPARE(QColor(image.m_imgIcon.pixel(4, 6)), QColor(0xfa, 0xb2, 0x19));
    QCOMPARE(QColor(image.m_imgIcon.pixel(4, 12)), QColor(Qt::white));
}

void TestTrayIconPainter::TestRenderColorsSevenDayBarIndependently()
{
    const TrayIconImage image = TrayIconPainter::Render(MakeState(12.0, 93.0), EMenuBarAppearance::LIGHT, 1.0);
    QVERIFY(!image.m_bTemplate);
    QCOMPARE(QColor(image.m_imgIcon.pixel(4, 12)), QColor(0xd0, 0x3b, 0x3b));
    // 12% of the 22pt bar ends near x=3.
    QCOMPARE(qAlpha(image.m_imgIcon.pixel(8, 6)), 0);
}

void TestTrayIconPainter::TestRenderDimsStaleState()
{
    TrayIconState state = MakeState(42.0, 18.0);
    state.m_bStale = true;
    const TrayIconImage image = TrayIconPainter::Render(state, EMenuBarAppearance::LIGHT, 1.0);
    const int iAlpha = qAlpha(image.m_imgIcon.pixel(4, 6));
    QVERIFY2(iAlpha > 115 && iAlpha < 140, qPrintable(QString::number(iAlpha)));
}

void TestTrayIconPainter::TestRenderEmptyStateHasNoFill()
{
    const TrayIconImage image = TrayIconPainter::Render(TrayIconState(), EMenuBarAppearance::LIGHT, 1.0);
    QVERIFY(image.m_bTemplate);
    QCOMPARE(qAlpha(image.m_imgIcon.pixel(4, 6)), 0);
    QCOMPARE(qAlpha(image.m_imgIcon.pixel(4, 12)), 0);
}

void TestTrayIconPainter::TestRenderScalesWithDevicePixelRatio()
{
    const TrayIconImage image = TrayIconPainter::Render(MakeState(42.0, 18.0), EMenuBarAppearance::LIGHT, 2.0);
    QCOMPARE(image.m_imgIcon.height(), 36);
    QCOMPARE(image.m_imgIcon.devicePixelRatio(), 2.0);
    QVERIFY(image.m_imgIcon.width() >= qRound((TrayIconPainter::kBarWidth + TrayIconPainter::kTextGap) * 2.0));
}

QTEST_MAIN(TestTrayIconPainter)
#include "TestTrayIconPainter.moc"
