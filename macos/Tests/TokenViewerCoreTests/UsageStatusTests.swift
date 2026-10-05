import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct UsageStatusTests {
    private let now = TestSupport.date("2026-10-05T12:00:00Z")
    private let limits = UsageLimits(fiveHour: LimitWindow(percent: 42, resetsAt: nil), sevenDay: LimitWindow(percent: 18, resetsAt: nil))

    private func status(after results: [(FetchResult, TimeInterval)]) -> UsageStatus {
        var status = UsageStatus()
        for (result, offset) in results {
            status.apply(result, now: now.addingTimeInterval(offset))
        }
        return status
    }

    @Test func emptyBeforeFirstFetch() {
        let status = UsageStatus()
        #expect(status.displayState(now: now) == .empty)
        #expect(status.isRefreshDueOnOpen(now: now))
        #expect(!status.needsRetryButton)
    }

    @Test func okStoresLimitsAndPlan() {
        let status = status(after: [(FetchResult(status: .ok, limits: limits, planLabel: "Max 5x"), 0)])
        #expect(status.limits == limits)
        #expect(status.planLabel == "Max 5x")
        #expect(status.lastSuccessAt == now)
        #expect(status.displayState(now: now) == .normal)
    }

    @Test func failureKeepsValuesThenTurnsStale() {
        let status = status(after: [(FetchResult(status: .ok, limits: limits), 0), (FetchResult(status: .networkError, detail: "오프라인"), 60)])
        #expect(status.limits == limits)
        #expect(status.lastStatus == .networkError)
        #expect(status.displayState(now: now.addingTimeInterval(60)) == .normal)
        #expect(status.displayState(now: now.addingTimeInterval(16 * 60)) == .stale)
    }

    @Test func staleAfterSleepEvenIfLastStatusOk() {
        let status = status(after: [(FetchResult(status: .ok, limits: limits), 0)])
        #expect(status.displayState(now: now.addingTimeInterval(20 * 60)) == .stale)
    }

    @Test func keychainDeniedClearsLimits() {
        let status = status(after: [(FetchResult(status: .ok, limits: limits, planLabel: "Max 5x"), 0), (FetchResult(status: .keychainDenied), 60)])
        #expect(status.limits == nil)
        #expect(status.planLabel == "")
        #expect(status.displayState(now: now.addingTimeInterval(60)) == .empty)
        #expect(status.needsRetryButton)
    }

    @Test func loggedOutClearsPlanLabel() {
        let status = status(after: [(FetchResult(status: .ok, planLabel: "Pro"), 0), (FetchResult(status: .notLoggedIn), 60)])
        #expect(status.planLabel == "")
        #expect(status.needsRetryButton)
    }

    @Test func badResponseClearsLimits() {
        let status = status(after: [(FetchResult(status: .ok, limits: limits), 0), (FetchResult(status: .badResponse), 60)])
        #expect(status.limits == nil)
    }

    @Test func expiredTokenKeepsPlanLabel() {
        let status = status(after: [(FetchResult(status: .tokenExpired, planLabel: "Max 5x"), 0)])
        #expect(status.planLabel == "Max 5x")
    }

    @Test func refreshDueOnOpenAfter30Seconds() {
        let status = status(after: [(FetchResult(status: .ok, limits: limits), 0)])
        #expect(!status.isRefreshDueOnOpen(now: now.addingTimeInterval(10)))
        #expect(status.isRefreshDueOnOpen(now: now.addingTimeInterval(31)))
    }

    @Test func noOpenRefreshAfterKeychainDenied() {
        let status = status(after: [(FetchResult(status: .keychainDenied), 0)])
        #expect(!status.isRefreshDueOnOpen(now: now.addingTimeInterval(3600)))
    }
}
