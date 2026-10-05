import AppKit
import Observation
import TokenViewerCore

/// Owns the workers, the settings and the menu bar item, and connects them (spec 4).
@MainActor
final class AppController {
    let state: AppState

    private let store: SettingsStore
    private let statusItem = StatusItemController()
    private lazy var popup = PopupController(content: PopupView(controller: self))
    private let settingsWindow = SettingsWindowController()
    private var appliedSettings: SettingsValues
    private var fetcher: UsageFetcher?
    private var scanner: LogScanner?
    private var queryRevision = 0
    private var clockTimer: Timer?
    private var wakeObserver: NSObjectProtocol?

    init(store: SettingsStore = SettingsStore()) {
        self.store = store
        state = AppState(settings: store.values)
        appliedSettings = store.values
    }

    func start() {
        let prices = AppInfo.pricesURL.flatMap { try? Data(contentsOf: $0) }.flatMap { PriceTable(json: $0) }
        let fetcher = UsageFetcher(
            environment: UsageFetchEnvironment(
                loadCredential: { await KeychainReader.loadCredential(now: Date()) },
                requestUsage: { await UsageClient.request(accessToken: $0) }),
            interval: Self.seconds(minutes: state.settings.intervalMinutes),
            onResult: { [weak self] result in await self?.receive(result) })
        let scanner = LogScanner(
            root: FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".claude/projects"),
            prices: prices,
            interval: AppConstants.logScanInterval,
            query: state.breakdownQuery,
            onUpdate: { [weak self] breakdown in await self?.receive(breakdown) })
        self.fetcher = fetcher
        self.scanner = scanner

        statusItem.onClick = { [weak self] in self?.handleStatusItemClick() }
        statusItem.update(state.iconState)
        LoginItem.apply(enabled: state.settings.launchAtLogin)
        observeSettings()
        startClock()
        Task {
            await fetcher.start()
            await scanner.start()
        }
    }

    /// The ↻ and '다시 확인' buttons: fetch limits and rescan logs now.
    func refreshNow() {
        Task { [fetcher, scanner] in
            await fetcher?.requestRefresh()
            await scanner?.requestRescan()
        }
    }

    func openSettings() {
        popup.close()
        settingsWindow.show(controller: self)
    }

    func quit() {
        NSApp.terminate(nil)
    }

    private func handleStatusItemClick() {
        if popup.isShown {
            popup.close()
            return
        }
        if popup.wasJustClosed {
            return
        }
        state.tick()
        if state.usage.isRefreshDueOnOpen(now: state.now) {
            Task { [fetcher] in await fetcher?.requestRefresh(userInitiated: false) }
        }
        Task { [scanner] in await scanner?.requestRescan() }
        popup.show(below: statusItem.anchorFrame)
    }

    private func receive(_ result: FetchResult) {
        let resetsBefore = state.usage.limits?.fiveHour?.resetsAt
        state.apply(result)
        if result.status == .badResponse {
            ErrorLog.writeBadResponse(result)
        }
        // The 5-hour period starts from the API's reset time.
        if state.usage.limits?.fiveHour?.resetsAt != resetsBefore {
            pushQuery()
        }
        statusItem.update(state.iconState)
    }

    private func receive(_ breakdown: Breakdown) {
        state.apply(breakdown)
    }

    private func pushQuery() {
        queryRevision += 1
        let query = state.breakdownQuery
        let revision = queryRevision
        Task { [scanner] in await scanner?.setQuery(query, revision: revision) }
    }

    private func observeSettings() {
        withObservationTracking {
            _ = state.settings
        } onChange: { [weak self] in
            Task { @MainActor in self?.settingsDidChange() }
        }
    }

    /// Saves what the views changed, corrects it (red must stay above orange) and applies each part that moved.
    private func settingsDidChange() {
        let previous = appliedSettings
        store.save(state.settings)
        let saved = store.values
        if state.settings != saved {
            state.settings = saved
        }
        appliedSettings = saved

        if saved.intervalMinutes != previous.intervalMinutes {
            let seconds = Self.seconds(minutes: saved.intervalMinutes)
            Task { [fetcher] in await fetcher?.setInterval(seconds) }
        }
        if saved.launchAtLogin != previous.launchAtLogin {
            LoginItem.apply(enabled: saved.launchAtLogin)
        }
        if saved.dimension != previous.dimension || saved.period != previous.period {
            pushQuery()
        }
        statusItem.update(state.iconState)
        observeSettings()
    }

    private func startClock() {
        clockTimer = Timer.scheduledTimer(withTimeInterval: AppConstants.clockTickInterval, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.tick() }
        }
        wakeObserver = NSWorkspace.shared.notificationCenter.addObserver(
            forName: NSWorkspace.didWakeNotification, object: nil, queue: .main
        ) { [weak self] _ in
            MainActor.assumeIsolated { self?.scheduleWakeRefresh() }
        }
    }

    /// Staleness and "n분 전" depend on the clock, so re-check them even when nothing new arrives.
    private func tick() {
        state.tick()
        statusItem.update(state.iconState)
    }

    private func scheduleWakeRefresh() {
        // The delay lets the network come back before refreshing.
        Task { [fetcher, scanner] in
            try? await Task.sleep(nanoseconds: UInt64(AppConstants.wakeRefreshDelay * 1_000_000_000))
            await fetcher?.requestRefresh(userInitiated: false)
            await scanner?.requestRescan()
        }
    }

    private static func seconds(minutes: Int) -> TimeInterval {
        TimeInterval(minutes * 60)
    }
}
