import Foundation

public struct RefreshDecision: Sendable, Equatable {
    public var delay: TimeInterval
    public var autoRetry: Bool

    public init(delay: TimeInterval, autoRetry: Bool) {
        self.delay = delay
        self.autoRetry = autoRetry
    }
}

/// When to call the usage API next (v0.1.0 spec 6, 7).
public enum RefreshPolicy {
    public static let maxBackoff: TimeInterval = 30 * 60

    public static func next(after status: FetchStatus, baseInterval: TimeInterval, previousDelay: TimeInterval) -> RefreshDecision {
        switch status {
        case .rateLimited:
            return RefreshDecision(delay: min(max(previousDelay, baseInterval) * 2, maxBackoff), autoRetry: true)
        case .keychainDenied:
            // Retrying would pop the Keychain prompt every few minutes; wait for the user to ask.
            return RefreshDecision(delay: baseInterval, autoRetry: false)
        default:
            return RefreshDecision(delay: baseInterval, autoRetry: true)
        }
    }
}
