import Foundation

public struct BreakdownQuery: Sendable, Equatable {
    public var dimension: BreakdownDimension
    public var period: BreakdownPeriod
    public var fiveHourResetsAt: Date?

    public init(dimension: BreakdownDimension, period: BreakdownPeriod, fiveHourResetsAt: Date? = nil) {
        self.dimension = dimension
        self.period = period
        self.fiveHourResetsAt = fiveHourResetsAt
    }
}

/// A breakdown and the revision of the query it was grouped by.
public struct BreakdownUpdate: Sendable, Equatable {
    public var breakdown: Breakdown
    public var revision: Int

    public init(breakdown: Breakdown, revision: Int) {
        self.breakdown = breakdown
        self.revision = revision
    }
}

/// Scans `~/.claude/projects` off the main thread and publishes the breakdown for the current tab and period (spec 4.2).
public actor LogScanner {
    private let root: URL
    private let prices: PriceTable?
    private let interval: TimeInterval
    private let onUpdate: @Sendable (BreakdownUpdate) async -> Void
    private let store = LogStore()
    private let signal = WakeSignal()
    private var query: BreakdownQuery
    private var queryRevision = 0
    private var loop: Task<Void, Never>?

    public init(root: URL, prices: PriceTable?, interval: TimeInterval, query: BreakdownQuery,
                onUpdate: @escaping @Sendable (BreakdownUpdate) async -> Void) {
        self.root = root
        self.prices = prices
        self.interval = interval
        self.query = query
        self.onUpdate = onUpdate
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

    public func requestRescan() async {
        await signal.wake()
    }

    /// Re-groups right away. Queries come from separate tasks and can arrive out of order, so older revisions are dropped.
    public func setQuery(_ query: BreakdownQuery, revision: Int) async {
        guard revision > queryRevision else {
            return
        }
        self.query = query
        queryRevision = revision
        await publish()
    }

    private func run() async {
        while !Task.isCancelled {
            store.scan(root: root, now: Date(), shouldContinue: { !Task.isCancelled })
            // Periods move with the clock, so publish every pass even when no file changed.
            await publish()
            await signal.wait(timeout: interval)
        }
    }

    private func publish() async {
        let now = Date()
        let start = TokenAggregator.periodStart(query.period, now: now, fiveHourResetsAt: query.fiveHourResetsAt)
        let breakdown = TokenAggregator.aggregate(store.snapshot, prices: prices, dimension: query.dimension, from: start)
        // Updates from the loop and from setQuery can reach the sink in either order; the revision lets it keep the newest.
        await onUpdate(BreakdownUpdate(breakdown: breakdown, revision: queryRevision))
    }
}
