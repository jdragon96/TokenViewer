import Foundation

public enum LimitDisplayState: Sendable, Equatable {
    case empty
    case normal
    case stale
}

/// The latest usage-limit state the menu bar and the dropdown show (Qt `SystemStatus`).
public struct UsageStatus: Sendable, Equatable {
    public static let staleAfter: TimeInterval = 15 * 60
    public static let refreshOnOpenAfter: TimeInterval = 30

    public private(set) var limits: UsageLimits?
    public private(set) var planLabel = ""
    public private(set) var lastStatus: FetchStatus = .ok
    public private(set) var lastDetail = ""
    public private(set) var lastAttemptAt: Date?
    public private(set) var lastSuccessAt: Date?

    public init() {}

    public mutating func apply(_ result: FetchResult, now: Date) {
        lastAttemptAt = now
        lastStatus = result.status
        lastDetail = result.detail
        switch result.status {
        case .notLoggedIn, .keychainDenied:
            // The account is unknown, so the old plan badge no longer applies.
            planLabel = ""
        default:
            if !result.planLabel.isEmpty {
                planLabel = result.planLabel
            }
        }
        switch result.status {
        case .ok:
            limits = result.limits
            lastSuccessAt = now
        case .notLoggedIn, .keychainDenied, .badResponse:
            limits = nil
        case .tokenExpired, .rateLimited, .networkError, .serverError:
            // Keep the last values; displayState dims them once they are old.
            break
        }
    }

    public func displayState(now: Date) -> LimitDisplayState {
        guard limits != nil, let lastSuccessAt else {
            return .empty
        }
        // Age alone decides, so values also dim after the Mac wakes from sleep.
        return now.timeIntervalSince(lastSuccessAt) > Self.staleAfter ? .stale : .normal
    }

    public func isRefreshDueOnOpen(now: Date) -> Bool {
        // Only the '다시 확인' button retries after a Keychain denial.
        if lastStatus == .keychainDenied {
            return false
        }
        guard let lastAttemptAt else {
            return true
        }
        return now.timeIntervalSince(lastAttemptAt) > Self.refreshOnOpenAfter
    }

    /// The retry button only helps when the credential itself was missing or refused.
    public var needsRetryButton: Bool {
        lastAttemptAt != nil && (lastStatus == .notLoggedIn || lastStatus == .keychainDenied)
    }
}
