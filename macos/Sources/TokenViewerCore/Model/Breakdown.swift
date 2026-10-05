import Foundation

public enum BreakdownDimension: Int, Sendable, CaseIterable {
    case project = 0
    case model = 1
    case session = 2

    public var title: String {
        switch self {
        case .project: "프로젝트"
        case .model: "모델"
        case .session: "세션"
        }
    }
}

public enum BreakdownPeriod: Int, Sendable, CaseIterable {
    case fiveHourWindow = 0
    case today = 1
    case sevenDays = 2
    case thirtyDays = 3

    public var title: String {
        switch self {
        case .fiveHourWindow: "이번 5시간 창"
        case .today: "오늘"
        case .sevenDays: "7일"
        case .thirtyDays: "30일"
        }
    }
}

public struct BreakdownRow: Sendable, Equatable {
    public var label = ""
    /// The raw group key: project path, model id or session id. Shown in the tooltip.
    public var detail = ""
    public var costUsd = 0.0
    public var isApproximate = false
    public var isUnpriced = false
    public var input = 0
    public var output = 0
    public var cacheWrite = 0
    public var cacheRead = 0

    public init() {}

    public var totalTokens: Int { input + output + cacheWrite + cacheRead }
}

public struct Breakdown: Sendable, Equatable {
    public var rows: [BreakdownRow] = []
    public var otherCount = 0
    public var otherRow = BreakdownRow()
    public var totalUsd = 0.0

    public init() {}

    /// The top rows followed by the folded "기타 N개" row when there is one.
    public var displayRows: [BreakdownRow] { otherCount > 0 ? rows + [otherRow] : rows }
}
