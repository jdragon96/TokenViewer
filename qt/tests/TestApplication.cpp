#include "Core/Application.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QtTest>

class TestApplication : public QObject
{
    Q_OBJECT

public:
    // QTEST_MAIN calls this before it creates the QApplication, as main() does.
    static void initMain()
    {
        Application::ConfigureHighDpi();
    }

private slots:
    void TestHighDpiSettingsAreSetBeforeStartup();
};

void TestApplication::TestHighDpiSettingsAreSetBeforeStartup()
{
    QVERIFY(QCoreApplication::testAttribute(Qt::AA_EnableHighDpiScaling));
    QVERIFY(QCoreApplication::testAttribute(Qt::AA_UseHighDpiPixmaps));
    // 125% and 150% stay as they are instead of rounding to 100% or 200%.
    QCOMPARE(QGuiApplication::highDpiScaleFactorRoundingPolicy(), Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
}

QTEST_MAIN(TestApplication)
#include "TestApplication.moc"
