import Foundation
import Observation
import TokenViewerCore

/// Everything the menu bar item, the dropdown and the settings window show. Views bind to `settings` directly;
/// AppController saves and applies each change.
@MainActor
@Observable
final class AppState {
    var settings: SettingsValues
    private(set) var usage = UsageStatus()
    private(set) var breakdown = Breakdown()
    /// False until the first log scan finishes, which takes several seconds on a large history.
    private(set) var isBreakdownLoaded = false
    /// Advanced by a clock so ages and staleness update without new data.
    private(set) var now = Date()

    init(settings: SettingsValues) {
        self.settings = settings
    }

    func apply(_ result: FetchResult) {
        now = Date()
        usage.apply(result, now: now)
    }

    func apply(_ breakdown: Breakdown) {
        self.breakdown = breakdown
        isBreakdownLoaded = true
    }

    func tick() {
        now = Date()
    }

    var displayState: LimitDisplayState { usage.displayState(now: now) }

    var iconState: StatusIconState {
        StatusIconState(usage: usage, warnPercent: settings.warnPercent, criticalPercent: settings.criticalPercent, now: now)
    }

    var breakdownQuery: BreakdownQuery {
        BreakdownQuery(dimension: settings.dimension, period: settings.period, fiveHourResetsAt: usage.limits?.fiveHour?.resetsAt)
    }
}
