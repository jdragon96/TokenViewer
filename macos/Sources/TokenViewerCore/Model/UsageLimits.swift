import Foundation

/// One usage-limit window from the usage API. A window the API did not report is nil, not a zero.
public struct LimitWindow: Sendable, Equatable {
    public var percent: Double
    public var resetsAt: Date?

    public init(percent: Double, resetsAt: Date?) {
        self.percent = percent
        self.resetsAt = resetsAt
    }
}

public struct UsageLimits: Sendable, Equatable {
    public var fiveHour: LimitWindow?
    public var sevenDay: LimitWindow?

    public init(fiveHour: LimitWindow? = nil, sevenDay: LimitWindow? = nil) {
        self.fiveHour = fiveHour
        self.sevenDay = sevenDay
    }
}
