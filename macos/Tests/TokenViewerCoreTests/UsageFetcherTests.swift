import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct UsageFetcherTests {
    static let okBody = Data(#"{"five_hour": {"utilization": 42}, "seven_day": {"utilization": 18}}"#.utf8)
    static let validCredential = CredentialResult(status: .ok, credential: Credential(
        accessToken: "token", expiresAt: nil, subscriptionType: "max", rateLimitTier: "default_claude_max_5x"))

    private func environment(
        credential: @escaping @Sendable () async -> CredentialResult = { UsageFetcherTests.validCredential },
        response: @escaping @Sendable (String) async -> HTTPResponse = { _ in HTTPResponse(status: 200, body: UsageFetcherTests.okBody) }
    ) -> UsageFetchEnvironment {
        UsageFetchEnvironment(loadCredential: credential, requestUsage: response)
    }

    @Test func fetchesImmediatelyThenOnInterval() async {
        let results = Recorder<FetchResult>()
        let fetcher = UsageFetcher(environment: environment(), interval: 0.1, onResult: { await results.append($0) })
        await fetcher.start()
        #expect(await waitUntil { await results.values.count >= 3 })
        await fetcher.stop()
        #expect(await results.values.first?.status == .ok)
        #expect(await results.values.first?.planLabel == "Max 5x")
    }

    @Test func refreshRequestFetchesNow() async {
        let results = Recorder<FetchResult>()
        let fetcher = UsageFetcher(environment: environment(), interval: 60, onResult: { await results.append($0) })
        await fetcher.start()
        #expect(await waitUntil { await results.values.count == 1 })
        await fetcher.requestRefresh()
        #expect(await waitUntil { await results.values.count == 2 })
        await fetcher.stop()
    }

    @Test func refreshWhileCredentialPendingDoesNotStack() async {
        let gate = Gate()
        let lookups = Recorder<Int>()
        let results = Recorder<FetchResult>()
        let fetcher = UsageFetcher(
            environment: environment(credential: {
                await lookups.append(1)
                await gate.pass()
                return UsageFetcherTests.validCredential
            }),
            interval: 60,
            onResult: { await results.append($0) })
        await fetcher.start()
        #expect(await waitUntil { await gate.isWaiting })
        await fetcher.requestRefresh()
        await fetcher.requestRefresh()
        await gate.open()
        #expect(await waitUntil { await results.values.count == 1 })
        try? await Task.sleep(nanoseconds: 300_000_000)
        #expect(await lookups.values.count == 1)
        #expect(await results.values.count == 1)
        await fetcher.stop()
    }

    @Test func keychainDeniedStopsAutoRetry() async {
        let results = Recorder<FetchResult>()
        let fetcher = UsageFetcher(environment: environment(credential: { CredentialResult(status: .accessDenied) }),
                                   interval: 0.05, onResult: { await results.append($0) })
        await fetcher.start()
        #expect(await waitUntil { await results.values.count == 1 })
        try? await Task.sleep(nanoseconds: 300_000_000)
        #expect(await results.values.count == 1)
        #expect(await results.values.first?.status == .keychainDenied)
        await fetcher.requestRefresh()
        #expect(await waitUntil { await results.values.count == 2 })
        await fetcher.stop()
    }

    @Test func automaticRefreshDoesNotRetryKeychainDenial() async {
        let lookups = Recorder<Int>()
        let results = Recorder<FetchResult>()
        let fetcher = UsageFetcher(environment: environment(credential: {
            await lookups.append(1)
            return CredentialResult(status: .accessDenied)
        }), interval: 60, onResult: { await results.append($0) })
        await fetcher.start()
        #expect(await waitUntil { await results.values.count == 1 })
        // Waking from sleep must not bring the Keychain prompt back on its own.
        await fetcher.requestRefresh(userInitiated: false)
        try? await Task.sleep(nanoseconds: 300_000_000)
        #expect(await lookups.values.count == 1)
        await fetcher.requestRefresh(userInitiated: true)
        #expect(await waitUntil { await lookups.values.count == 2 })
        await fetcher.stop()
    }

    @Test func rateLimitBacksOff() async {
        let results = Recorder<FetchResult>()
        let fetcher = UsageFetcher(environment: environment(response: { _ in HTTPResponse(status: 429) }),
                                   interval: 0.05, onResult: { await results.append($0) })
        await fetcher.start()
        try? await Task.sleep(nanoseconds: 500_000_000)
        await fetcher.stop()
        // Fetches at 0, 0.1 and 0.3 s; without backoff a 0.05 s interval would give about ten.
        let count = await results.values.count
        #expect(count >= 2 && count <= 4)
    }

    @Test func mapsCredentialProblems() async {
        let requests = Recorder<String>()
        func fetch(_ credential: CredentialResult) async -> FetchResult {
            let fetcher = UsageFetcher(environment: environment(credential: { credential }, response: { token in
                await requests.append(token)
                return HTTPResponse(status: 200, body: UsageFetcherTests.okBody)
            }), interval: 60, onResult: { _ in })
            return await fetcher.fetchOnce()
        }
        #expect(await fetch(CredentialResult(status: .notFound)).status == .notLoggedIn)
        #expect(await fetch(CredentialResult(status: .malformed)).status == .notLoggedIn)
        #expect(await fetch(CredentialResult(status: .accessDenied)).status == .keychainDenied)
        var expired = UsageFetcherTests.validCredential
        expired.status = .expired
        let expiredResult = await fetch(expired)
        #expect(expiredResult.status == .tokenExpired)
        #expect(expiredResult.planLabel == "Max 5x")
        #expect(await requests.values.isEmpty)
    }

    @Test func passesTokenAndPlanToResult() async {
        let requests = Recorder<String>()
        let fetcher = UsageFetcher(environment: environment(response: { token in
            await requests.append(token)
            return HTTPResponse(status: 200, body: UsageFetcherTests.okBody)
        }), interval: 60, onResult: { _ in })
        let result = await fetcher.fetchOnce()
        #expect(await requests.values == ["token"])
        #expect(result.planLabel == "Max 5x")
        #expect(result.limits.fiveHour?.percent == 42)
    }
}
