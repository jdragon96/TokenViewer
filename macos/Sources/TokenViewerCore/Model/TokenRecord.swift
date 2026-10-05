import Foundation

public struct TokenRecord: Sendable, Equatable {
    public var key: String
    public var timestamp: Date
    public var sessionId: String
    public var projectPath: String
    public var model: String
    public var isFast: Bool
    public var input: Int
    public var output: Int
    public var cacheWrite5m: Int
    public var cacheWrite1h: Int
    public var cacheRead: Int

    public init(
        key: String,
        timestamp: Date,
        sessionId: String = "",
        projectPath: String = "",
        model: String = "",
        isFast: Bool = false,
        input: Int = 0,
        output: Int = 0,
        cacheWrite5m: Int = 0,
        cacheWrite1h: Int = 0,
        cacheRead: Int = 0
    ) {
        self.key = key
        self.timestamp = timestamp
        self.sessionId = sessionId
        self.projectPath = projectPath
        self.model = model
        self.isFast = isFast
        self.input = input
        self.output = output
        self.cacheWrite5m = cacheWrite5m
        self.cacheWrite1h = cacheWrite1h
        self.cacheRead = cacheRead
    }
}

public struct LogSnapshot: Sendable, Equatable {
    public var records: [TokenRecord]
    public var sessionTitles: [String: String]

    public init(records: [TokenRecord] = [], sessionTitles: [String: String] = [:]) {
        self.records = records
        self.sessionTitles = sessionTitles
    }
}
