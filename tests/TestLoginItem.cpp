#include "Service/LoginItem.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace
{
const QString kBundledPath = QStringLiteral("/Applications/TokenViewer.app/Contents/MacOS/TokenViewer");
}

class TestLoginItem : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void TestBuildPlistEscapesPath();
    void TestApplyCreatesAndRemovesPlist();
    void TestRefusesBareExecutable();

private:
    QTemporaryDir m_dirHome;
    QByteArray m_baOriginalHome;
};

void TestLoginItem::init()
{
    m_baOriginalHome = qgetenv("HOME");
    qputenv("HOME", m_dirHome.path().toUtf8());
}

void TestLoginItem::cleanup()
{
    qputenv("HOME", m_baOriginalHome);
}

void TestLoginItem::TestBuildPlistEscapesPath()
{
    const QByteArray baPlist = LoginItem::BuildPlist(QStringLiteral("/Applications/A&B.app/Contents/MacOS/TokenViewer"));
    QVERIFY(baPlist.contains("<string>com.tokenviewer.TokenViewer</string>"));
    QVERIFY(baPlist.contains("/Applications/A&amp;B.app/Contents/MacOS/TokenViewer"));
    QVERIFY(baPlist.contains("<key>RunAtLoad</key>"));
}

void TestLoginItem::TestApplyCreatesAndRemovesPlist()
{
    QVERIFY(LoginItem::GetPlistPath().startsWith(m_dirHome.path()));
    QVERIFY(LoginItem::Apply(true, kBundledPath));
    QFile file(LoginItem::GetPlistPath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), LoginItem::BuildPlist(kBundledPath));
    file.close();

    QVERIFY(LoginItem::Apply(false, kBundledPath));
    QVERIFY(!QFile::exists(LoginItem::GetPlistPath()));
}

void TestLoginItem::TestRefusesBareExecutable()
{
    QVERIFY(!LoginItem::Apply(true, QStringLiteral("/tmp/build/TokenViewer")));
    QVERIFY(!QFile::exists(LoginItem::GetPlistPath()));
}

QTEST_GUILESS_MAIN(TestLoginItem)
#include "TestLoginItem.moc"
