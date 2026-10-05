import Foundation

public struct SettingsValues: Sendable, Equatable {
    public var intervalMinutes: Int
    public var warnPercent: Int
    public var criticalPercent: Int
    public var launchAtLogin: Bool
    public var dimension: BreakdownDimension
    public var period: BreakdownPeriod

    public init(intervalMinutes: Int, warnPercent: Int, criticalPercent: Int, launchAtLogin: Bool,
                dimension: BreakdownDimension, period: BreakdownPeriod) {
        self.intervalMinutes = intervalMinutes
        self.warnPercent = warnPercent
        self.criticalPercent = criticalPercent
        self.launchAtLogin = launchAtLogin
        self.dimension = dimension
        self.period = period
    }
}

/// UserDefaults with the same keys the Qt build's QSettings wrote, so existing choices carry over (spec 4.3).
public struct SettingsStore {
    public static let allowedIntervals = [1, 3, 5, 10]
    public static let minWarn = 50
    public static let maxWarn = 95
    public static let maxPercent = 100
    public static let percentStep = 5
    private static let defaultInterval = 3
    private static let defaultWarn = 70
    private static let defaultCritical = 90

    private enum Key {
        static let interval = "refresh.intervalMinutes"
        static let warn = "alert.warnPercent"
        static let critical = "alert.criticalPercent"
        static let launchAtLogin = "app.launchAtLogin"
        static let dimension = "popup.dimension"
        static let period = "popup.period"
    }

    private let defaults: UserDefaults

    public init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
    }

    /// The stored values, with anything out of range replaced by its default.
    public var values: SettingsValues {
        let interval = int(Key.interval) ?? Self.defaultInterval
        let warn = validWarn(int(Key.warn))
        return SettingsValues(
            intervalMinutes: Self.allowedIntervals.contains(interval) ? interval : Self.defaultInterval,
            warnPercent: warn,
            criticalPercent: validCritical(int(Key.critical), warn: warn),
            launchAtLogin: bool(Key.launchAtLogin) ?? true,
            dimension: int(Key.dimension).flatMap(BreakdownDimension.init(rawValue:)) ?? .project,
            period: int(Key.period).flatMap(BreakdownPeriod.init(rawValue:)) ?? .fiveHourWindow)
    }

    public func save(_ values: SettingsValues) {
        defaults.set(values.intervalMinutes, forKey: Key.interval)
        defaults.set(values.warnPercent, forKey: Key.warn)
        defaults.set(values.criticalPercent, forKey: Key.critical)
        defaults.set(values.launchAtLogin, forKey: Key.launchAtLogin)
        defaults.set(values.dimension.rawValue, forKey: Key.dimension)
        defaults.set(values.period.rawValue, forKey: Key.period)
    }

    private func validWarn(_ value: Int?) -> Int {
        guard let value, value >= Self.minWarn, value <= Self.maxWarn, value % Self.percentStep == 0 else {
            return Self.defaultWarn
        }
        return value
    }

    private func validCritical(_ value: Int?, warn: Int) -> Int {
        if let value, value > warn, value <= Self.maxPercent, value % Self.percentStep == 0 {
            return value
        }
        return Self.defaultCritical > warn ? Self.defaultCritical : min(warn + Self.percentStep, Self.maxPercent)
    }

    // QSettings may have written numbers and booleans as strings; accept both.
    private func int(_ key: String) -> Int? {
        switch defaults.object(forKey: key) {
        case let number as NSNumber:
            return number.intValue
        case let text as String:
            return Int(text)
        default:
            return nil
        }
    }

    private func bool(_ key: String) -> Bool? {
        switch defaults.object(forKey: key) {
        case let number as NSNumber:
            return number.boolValue
        case let text as String:
            return ["true": true, "false": false][text.lowercased()]
        default:
            return nil
        }
    }
}
