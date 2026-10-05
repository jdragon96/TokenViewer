import Testing
@testable import TokenViewerCore

@Suite struct RefreshPolicyTests {
    @Test func okUsesBaseInterval() {
        #expect(RefreshPolicy.next(after: .ok, baseInterval: 180, previousDelay: 999) == RefreshDecision(delay: 180, autoRetry: true))
    }

    @Test func rateLimitDoublesDelay() {
        #expect(RefreshPolicy.next(after: .rateLimited, baseInterval: 180, previousDelay: 0).delay == 360)
        #expect(RefreshPolicy.next(after: .rateLimited, baseInterval: 180, previousDelay: 360).delay == 720)
    }

    @Test func rateLimitIsCapped() {
        #expect(RefreshPolicy.next(after: .rateLimited, baseInterval: 180, previousDelay: 1500).delay == 1800)
        #expect(RefreshPolicy.next(after: .rateLimited, baseInterval: 600, previousDelay: 1800).delay == 1800)
    }

    @Test func keychainDeniedStopsAutoRetry() {
        #expect(!RefreshPolicy.next(after: .keychainDenied, baseInterval: 180, previousDelay: 0).autoRetry)
    }

    @Test func otherFailuresKeepBaseInterval() {
        for status: FetchStatus in [.networkError, .serverError, .tokenExpired, .badResponse, .notLoggedIn] {
            #expect(RefreshPolicy.next(after: status, baseInterval: 180, previousDelay: 720) == RefreshDecision(delay: 180, autoRetry: true))
        }
    }
}
