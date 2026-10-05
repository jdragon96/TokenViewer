import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct LogScannerTests {
    private let recent = TestSupport.isoString(Date().addingTimeInterval(-3600))

    private func makeScanner(root: URL, updates: Recorder<Breakdown>) throws -> LogScanner {
        LogScanner(root: root, prices: try TestSupport.prices(), interval: 60,
                   query: BreakdownQuery(dimension: .project, period: .thirtyDays),
                   onUpdate: { await updates.append($0) })
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

    @Test func emptyRootPublishesEmptyBreakdown() async throws {
        let dir = try TemporaryDirectory()
        let updates = Recorder<Breakdown>()
        let scanner = try makeScanner(root: dir.url.appendingPathComponent("missing"), updates: updates)
        await scanner.start()
        #expect(await waitUntil { await updates.values.last == Breakdown() })
        await scanner.stop()
    }
}
