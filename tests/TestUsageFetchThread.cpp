#include "Thread/UsageFetchThread.h"

#include <QSignalSpy>
#include <QtTest>

#include <atomic>

namespace
{
constexpr int kShortIntervalMs = 300;
constexpr int kLongIntervalMs = 60000;
constexpr int kSignalWaitMs = 2000;
constexpr int kQuietWaitMs = 800;
constexpr int kSlowFetchMs = 300;
constexpr int kFetchInProgressWaitMs = 100;

FetchFunction MakeFetch(std::atomic<int>* calls, EFetchStatus status)
{
    return [calls, status]()
    {
        ++(*calls);
        FetchResult result;
        result.m_eStatus = status;
        return result;
    };
}
}

class TestUsageFetchThread : public QObject
{
    Q_OBJECT

private slots:
    void TestFetchesImmediatelyThenOnInterval();
    void TestKeychainDeniedStopsAutoRetry();
    void TestRefreshRequestFetchesNow();
    void TestRefreshDuringFetchDoesNotRefetch();
};

void TestUsageFetchThread::TestFetchesImmediatelyThenOnInterval()
{
    std::atomic<int> iCalls{ 0 };
    UsageFetchThread thread(MakeFetch(&iCalls, EFetchStatus::OK), kShortIntervalMs);
    QSignalSpy spy(&thread, &UsageFetchThread::FetchCompleted);
    thread.Start();
    QVERIFY(spy.wait(kSignalWaitMs));
    QVERIFY(spy.wait(kSignalWaitMs));
    QVERIFY(iCalls.load() >= 2);
    thread.Stop();
}

void TestUsageFetchThread::TestKeychainDeniedStopsAutoRetry()
{
    std::atomic<int> iCalls{ 0 };
    UsageFetchThread thread(MakeFetch(&iCalls, EFetchStatus::KEYCHAIN_DENIED), kShortIntervalMs);
    QSignalSpy spy(&thread, &UsageFetchThread::FetchCompleted);
    thread.Start();
    QVERIFY(spy.wait(kSignalWaitMs));
    QTest::qWait(kQuietWaitMs);
    QCOMPARE(iCalls.load(), 1);

    thread.RequestRefresh();
    QVERIFY(spy.wait(kSignalWaitMs));
    QCOMPARE(iCalls.load(), 2);
    thread.Stop();
}

void TestUsageFetchThread::TestRefreshRequestFetchesNow()
{
    std::atomic<int> iCalls{ 0 };
    UsageFetchThread thread(MakeFetch(&iCalls, EFetchStatus::OK), kLongIntervalMs);
    QSignalSpy spy(&thread, &UsageFetchThread::FetchCompleted);
    thread.Start();
    QVERIFY(spy.wait(kSignalWaitMs));
    thread.RequestRefresh();
    QVERIFY(spy.wait(kSignalWaitMs));
    QCOMPARE(iCalls.load(), 2);
    QCOMPARE(spy.last().at(0).value<FetchResult>().m_eStatus, EFetchStatus::OK);
    thread.Stop();
}

void TestUsageFetchThread::TestRefreshDuringFetchDoesNotRefetch()
{
    std::atomic<int> iCalls{ 0 };
    UsageFetchThread thread([&iCalls]()
    {
        ++iCalls;
        QThread::msleep(kSlowFetchMs);
        FetchResult result;
        result.m_eStatus = EFetchStatus::KEYCHAIN_DENIED;
        return result;
    }, kShortIntervalMs);
    QSignalSpy spy(&thread, &UsageFetchThread::FetchCompleted);
    thread.Start();
    QTest::qWait(kFetchInProgressWaitMs);
    thread.RequestRefresh();
    QVERIFY(spy.wait(kSignalWaitMs));
    QTest::qWait(kQuietWaitMs);
    QCOMPARE(iCalls.load(), 1);
    thread.Stop();
}

QTEST_GUILESS_MAIN(TestUsageFetchThread)
#include "TestUsageFetchThread.moc"
