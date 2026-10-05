import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct LogStoreTests {
    private let now = TestSupport.date("2026-10-05T12:00:00Z")

    @Test func readsMainAndSubagentFiles() throws {
        let dir = try TemporaryDirectory()
        try dir.write("-Users-me-app/s1.jsonl", [LogLines.assistant(id: "a", sessionId: nil)])
        try dir.write("-Users-me-app/s1/subagents/agent-1.jsonl", [LogLines.assistant(id: "b", sessionId: nil)])
        let store = LogStore()
        #expect(store.scan(root: dir.url, now: now))
        #expect(store.recordCount == 2)
        #expect(Set(store.snapshot.records.map(\.sessionId)) == ["s1"])
    }

    @Test func appendsOnlyNewLines() throws {
        let dir = try TemporaryDirectory()
        try dir.write("p/s.jsonl", [LogLines.assistant(id: "a")])
        let store = LogStore()
        store.scan(root: dir.url, now: now)
        try dir.append("p/s.jsonl", LogLines.assistant(id: "b") + "\n")
        #expect(store.scan(root: dir.url, now: now))
        #expect(store.recordCount == 2)
        #expect(!store.scan(root: dir.url, now: now))
    }

    @Test func keepsLastCopyOfDuplicates() throws {
        let dir = try TemporaryDirectory()
        try dir.write("p/s.jsonl", [LogLines.assistant(id: "a", output: 1), LogLines.assistant(id: "a", output: 99)])
        let store = LogStore()
        store.scan(root: dir.url, now: now)
        #expect(store.recordCount == 1)
        #expect(store.snapshot.records.first?.output == 99)
    }

    @Test func waitsForPartialLine() throws {
        let dir = try TemporaryDirectory()
        try dir.write("p/s.jsonl", [LogLines.assistant(id: "a"), LogLines.assistant(id: "b")], terminated: false)
        let store = LogStore()
        store.scan(root: dir.url, now: now)
        #expect(store.recordCount == 1)
        try dir.append("p/s.jsonl", "\n")
        store.scan(root: dir.url, now: now)
        #expect(store.recordCount == 2)
    }

    @Test func readsLinesAcrossChunkBoundaries() throws {
        let dir = try TemporaryDirectory()
        try dir.write("p/s.jsonl", [LogLines.assistant(id: "a"), LogLines.assistant(id: "b"), LogLines.assistant(id: "c")])
        let store = LogStore(chunkBytes: 16)
        store.scan(root: dir.url, now: now)
        #expect(Set(store.snapshot.records.map(\.key)) == ["a|req-1", "b|req-1", "c|req-1"])
    }

    @Test func rereadsTruncatedFile() throws {
        let dir = try TemporaryDirectory()
        try dir.write("p/s.jsonl", [LogLines.assistant(id: "a"), LogLines.assistant(id: "b")])
        let store = LogStore()
        store.scan(root: dir.url, now: now)
        try dir.write("p/s.jsonl", [LogLines.assistant(id: "c")])
        #expect(store.scan(root: dir.url, now: now))
        #expect(store.snapshot.records.map(\.key) == ["c|req-1"])
    }

    @Test func dropsDeletedFile() throws {
        let dir = try TemporaryDirectory()
        try dir.write("p/one.jsonl", [LogLines.assistant(id: "a")])
        let deleted = try dir.write("p/two.jsonl", [LogLines.assistant(id: "b")])
        let store = LogStore()
        store.scan(root: dir.url, now: now)
        try FileManager.default.removeItem(at: deleted)
        #expect(store.scan(root: dir.url, now: now))
        #expect(store.snapshot.records.map(\.key) == ["a|req-1"])
    }

    @Test func skipsFilesOlderThanRetention() throws {
        let dir = try TemporaryDirectory()
        let file = try dir.write("p/old.jsonl", [LogLines.assistant(id: "a")])
        try FileManager.default.setAttributes([.modificationDate: now.addingTimeInterval(-40 * 86_400)], ofItemAtPath: file.path)
        let store = LogStore()
        #expect(!store.scan(root: dir.url, now: now))
        #expect(store.recordCount == 0)
    }

    @Test func prunesOldRecords() throws {
        let dir = try TemporaryDirectory()
        let old = TestSupport.isoString(now.addingTimeInterval(-32 * 86_400))
        try dir.write("p/s.jsonl", [LogLines.assistant(id: "old", timestamp: old), LogLines.assistant(id: "new")])
        let store = LogStore()
        store.scan(root: dir.url, now: now)
        #expect(store.snapshot.records.map(\.key) == ["new|req-1"])
    }

    @Test func prunesTitlesWithTheirRecords() throws {
        let dir = try TemporaryDirectory()
        let old = TestSupport.isoString(now.addingTimeInterval(-32 * 86_400))
        try dir.write("p/old.jsonl", [LogLines.title(sessionId: "s-old", title: "지난달"), LogLines.assistant(id: "a", sessionId: "s-old", timestamp: old)])
        try dir.write("p/new.jsonl", [LogLines.title(sessionId: "s-new", title: "오늘"), LogLines.assistant(id: "b", sessionId: "s-new")])
        let store = LogStore()
        store.scan(root: dir.url, now: now)
        #expect(store.snapshot.sessionTitles == ["s-new": "오늘"])
    }

    @Test func forgetsTitlesOfDeletedFiles() throws {
        let dir = try TemporaryDirectory()
        let deleted = try dir.write("p/gone.jsonl", [LogLines.title(sessionId: "s-gone", title: "지운 세션"), LogLines.assistant(id: "a", sessionId: "s-gone")])
        try dir.write("p/kept.jsonl", [LogLines.title(sessionId: "s-kept", title: "남은 세션"), LogLines.assistant(id: "b", sessionId: "s-kept")])
        let store = LogStore()
        store.scan(root: dir.url, now: now)
        try FileManager.default.removeItem(at: deleted)
        store.scan(root: dir.url, now: now)
        #expect(store.snapshot.sessionTitles == ["s-kept": "남은 세션"])
    }

    @Test func missingRootIsEmpty() throws {
        let dir = try TemporaryDirectory()
        let store = LogStore()
        #expect(!store.scan(root: dir.url.appendingPathComponent("missing"), now: now))
        #expect(store.recordCount == 0)
    }

    @Test func keepsSessionTitles() throws {
        let dir = try TemporaryDirectory()
        try dir.write("p/s1.jsonl", [LogLines.title(sessionId: "s1", title: "로그 파서")])
        let store = LogStore()
        store.scan(root: dir.url, now: now)
        #expect(store.snapshot.sessionTitles == ["s1": "로그 파서"])
    }

    @Test func stopsWhenAskedTo() throws {
        let dir = try TemporaryDirectory()
        try dir.write("p/s.jsonl", [LogLines.assistant(id: "a")])
        let store = LogStore()
        #expect(!store.scan(root: dir.url, now: now, shouldContinue: { false }))
        #expect(store.recordCount == 0)
    }
}
