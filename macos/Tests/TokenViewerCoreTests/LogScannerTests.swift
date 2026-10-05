import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct LogScannerTests {
    private let recent = TestSupport.isoString(Date().addingTimeInterval(-3600))

    private func makeScanner(root: URL, updates: Recorder<Breakdown>, revisions: Recorder<Int> = Recorder()) throws -> LogScanner {
        LogScanner(root: root, prices: try TestSupport.prices(), interval: 60,
                   query: BreakdownQuery(dimension: .project, period: .thirtyDays),
                   onUpdate: { update in
                       await revisions.append(update.revision)
                       await updates.append(update.breakdown)
                   })
    }

    @Test func updatesCarryTheirQueryRevision() async throws {
        let dir = try TemporaryDirectory()
        try dir.write("-w-app/s1.jsonl", [LogLines.assistant(id: "a", cwd: "/w/app", timestamp: recent)])
        let updates = Recorder<Breakdown>()
        let revisions = Recorder<Int>()
        let scanner = try makeScanner(root: dir.url, updates: updates, revisions: revisions)
        await scanner.start()
        #expect(await waitUntil { await revisions.values == [0] })
        await scanner.setQuery(BreakdownQuery(dimension: .model, period: .thirtyDays), revision: 3)
        await scanner.requestRescan()
        // The periodic publish after the new query carries the same revision, so a sink can drop older ones.
        #expect(await waitUntil { await revisions.values == [0, 3, 3] })
        await scanner.stop()
    }

    @Test func publishesOnStartAndRescan() async throws {
        let dir = try TemporaryDirectory()
        try dir.write("-w-app/s1.jsonl", [LogLines.assistant(id: "a", cwd: "/w/app", timestamp: recent)])
        let updates = Recorder<Breakdown>()
        let scanner = try makeScanner(root: dir.url, updates: updates)
        await scanner.start()
        #expect(await waitUntil { await updates.values.last?.rows.map(\.label) == ["app"] })
        try dir.write("-w-web/s2.jsonl", [LogLines.assistant(id: "b", cwd: "/w/web", timestamp: recent)])
        await scanner.requestRescan()
        #expect(await waitUntil { await updates.values.last?.rows.count == 2 })
        await scanner.stop()
    }

    @Test func setQueryRegroupsRightAway() async throws {
        let dir = try TemporaryDirectory()
        try dir.write("-w-app/s1.jsonl", [LogLines.assistant(id: "a", cwd: "/w/app", timestamp: recent)])
        let updates = Recorder<Breakdown>()
        let scanner = try makeScanner(root: dir.url, updates: updates)
        await scanner.start()
        #expect(await waitUntil { await !updates.values.isEmpty })
        await scanner.setQuery(BreakdownQuery(dimension: .model, period: .thirtyDays), revision: 1)
        #expect(await updates.values.last?.rows.map(\.label) == ["Opus 5.5"])
        await scanner.stop()
    }

    @Test func ignoresOlderQueryRevisions() async throws {
        let dir = try TemporaryDirectory()
        try dir.write("-w-app/s1.jsonl", [LogLines.assistant(id: "a", cwd: "/w/app", timestamp: recent)])
        let updates = Recorder<Breakdown>()
        let scanner = try makeScanner(root: dir.url, updates: updates)
        await scanner.start()
        #expect(await waitUntil { await !updates.values.isEmpty })
        await scanner.setQuery(BreakdownQuery(dimension: .model, period: .thirtyDays), revision: 2)
        await scanner.setQuery(BreakdownQuery(dimension: .project, period: .thirtyDays), revision: 1)
        #expect(await updates.values.last?.rows.map(\.label) == ["Opus 5.5"])
        await scanner.stop()
    }

    @Test func scansOnItsOwnQueue() async throws {
        let dir = try TemporaryDirectory()
        let scanner = try makeScanner(root: dir.url, updates: Recorder())
        // A first scan of a long history takes seconds; it must not hold a Swift concurrency pool thread.
        #expect(await scanner.isOnScanQueue())
    }

    @Test func emptyRootPublishesEmptyBreakdown() async throws {
        let dir = try TemporaryDirectory()
        let updates = Recorder<Breakdown>()
        let scanner = try makeScanner(root: dir.url.appendingPathComponent("missing"), updates: updates)
        await scanner.start()
        #expect(await waitUntil { await updates.values.last == Breakdown() })
        await scanner.stop()
    }
}
