import Foundation

/// The system calls UsageFetcher needs; the app passes the Keychain and URLSession, tests pass fakes.
public struct UsageFetchEnvironment: Sendable {
    public var loadCredential: @Sendable () async -> CredentialResult
    public var requestUsage: @Sendable (_ accessToken: String) async -> HTTPResponse

    public init(loadCredential: @escaping @Sendable () async -> CredentialResult,
                requestUsage: @escaping @Sendable (_ accessToken: String) async -> HTTPResponse) {
        self.loadCredential = loadCredential
        self.requestUsage = requestUsage
    }
}

/// Polls the usage API on its own schedule and hands each result to `onResult` (spec 4.1).
public actor UsageFetcher {
    private let environment: UsageFetchEnvironment
    private let onResult: @Sendable (FetchResult) async -> Void
    private let signal = WakeSignal()
    private var interval: TimeInterval
    private var loop: Task<Void, Never>?
    private var isFetching = false
    private var lastStatus: FetchStatus?

    public init(environment: UsageFetchEnvironment, interval: TimeInterval, onResult: @escaping @Sendable (FetchResult) async -> Void) {
        self.environment = environment
        self.interval = interval
        self.onResult = onResult
    }

    public func start() {
        guard loop == nil else {
            return
        }
        loop = Task { await self.run() }
    }

    public func stop() async {
        loop?.cancel()
        loop = nil
        await signal.wake()
    }

    /// A request made while a fetch is running is answered by that fetch, so prompts and calls never stack up.
    /// After a Keychain denial only the user may ask again; an automatic request (waking from sleep) would bring the prompt back.
    public func requestRefresh(userInitiated: Bool = true) async {
        guard !isFetching, userInitiated || lastStatus != .keychainDenied else {
            return
        }
        await signal.wake()
    }

    /// Takes effect from the next wait.
    public func setInterval(_ seconds: TimeInterval) {
        interval = seconds
    }

    func fetchOnce() async -> FetchResult {
        let credential = await environment.loadCredential()
        let planLabel = credential.credential.map {
            CredentialParser.planLabel(subscriptionType: $0.subscriptionType, rateLimitTier: $0.rateLimitTier)
        } ?? ""
        switch credential.status {
        case .notFound, .malformed:
            return FetchResult(status: .notLoggedIn, detail: "Claude Code 로그인 정보를 찾지 못했습니다")
        case .accessDenied:
            return FetchResult(status: .keychainDenied, detail: "Keychain 접근이 거부되었습니다")
        case .expired:
            return FetchResult(status: .tokenExpired, planLabel: planLabel, detail: "로그인 토큰이 만료되었습니다")
        case .ok:
            break
        }
        guard let accessToken = credential.credential?.accessToken else {
            return FetchResult(status: .notLoggedIn, detail: "Claude Code 로그인 정보를 찾지 못했습니다")
        }
        var result = UsageResponseParser.map(await environment.requestUsage(accessToken))
        result.planLabel = planLabel
        return result
    }

    private func run() async {
        var backoff: TimeInterval = 0
        while !Task.isCancelled {
            isFetching = true
            let result = await fetchOnce()
            isFetching = false
            lastStatus = result.status
            let decision = RefreshPolicy.next(after: result.status, baseInterval: interval, previousDelay: backoff)
            backoff = decision.delay
            await onResult(result)
            if Task.isCancelled {
                break
            }
            // Outside of a rate-limit backoff, always use the latest interval from the settings.
            let delay = result.status == .rateLimited ? backoff : interval
            await signal.wait(timeout: decision.autoRetry ? delay : nil)
        }
    }
}
