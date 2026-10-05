import Foundation

public enum FetchStatus: Sendable, Equatable {
    case ok
    case notLoggedIn
    case keychainDenied
    case tokenExpired
    case rateLimited
    case networkError
    case serverError
    case badResponse
}

public struct FetchResult: Sendable, Equatable {
    public var status: FetchStatus
    public var limits: UsageLimits
    public var planLabel: String
    public var detail: String
    public var httpStatus: Int
    /// Kept only for format-drift logging; never holds the request or its token.
    public var rawBody: Data

    public init(status: FetchStatus, limits: UsageLimits = UsageLimits(), planLabel: String = "", detail: String = "", httpStatus: Int = 0, rawBody: Data = Data()) {
        self.status = status
        self.limits = limits
        self.planLabel = planLabel
        self.detail = detail
        self.httpStatus = httpStatus
        self.rawBody = rawBody
    }
}

public struct HTTPResponse: Sendable, Equatable {
    /// 0 when no HTTP response arrived (offline, timeout).
    public var status: Int
    public var body: Data
    public var errorText: String?

    public init(status: Int, body: Data = Data(), errorText: String? = nil) {
        self.status = status
        self.body = body
        self.errorText = errorText
    }
}
