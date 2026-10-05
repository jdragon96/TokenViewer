import Foundation
import Testing
@testable import TokenViewerCore

@Suite struct LogParserTests {
    private func parse(_ line: String, fallback: String = "fallback") -> ParsedLine {
        LogParser.parse(Data(line.utf8), fallbackSessionId: fallback)
    }

    private func usage(_ line: String, fallback: String = "fallback") throws -> TokenRecord {
        guard case .usage(let record) = parse(line, fallback: fallback) else {
            Issue.record("expected a usage line: \(line)")
            throw CancellationError()
        }
        return record
    }

    @Test func parsesAssistantUsage() throws {
        let record = try usage(LogLines.assistant(id: "msg-1", requestId: "req-9", input: 10, output: 20, cacheRead: 30, cacheCreation: 40))
        #expect(record.key == "msg-1|req-9")
        #expect(record.timestamp == TestSupport.date("2026-10-05T06:00:00Z"))
        #expect(record.sessionId == "session-1")
        #expect(record.projectPath == "/Users/me/work/app")
        #expect(record.model == "claude-opus-5-5")
        #expect(record.input == 10)
        #expect(record.output == 20)
        #expect(record.cacheRead == 30)
        #expect(record.cacheWrite5m == 40)
        #expect(record.cacheWrite1h == 0)
        #expect(!record.isFast)
    }

    @Test func splitsCacheWritesByLifetime() throws {
        let record = try usage(LogLines.assistant(id: "m", cacheCreation: 1_000, split: (fiveMinutes: 300, oneHour: 700)))
        #expect(record.cacheWrite5m == 300)
        #expect(record.cacheWrite1h == 700)
    }

    @Test func marksFastSpeed() throws {
        #expect(try usage(LogLines.assistant(id: "m", speed: "fast")).isFast)
    }

    @Test func keyWithoutRequestId() throws {
        #expect(try usage(LogLines.assistant(id: "msg-1", requestId: nil)).key == "msg-1")
    }

    @Test func usesFallbackSessionId() throws {
        #expect(try usage(LogLines.assistant(id: "m", sessionId: nil), fallback: "from-path").sessionId == "from-path")
    }

    @Test func ignoresSyntheticModel() {
        #expect(parse(LogLines.assistant(id: "m", model: "<synthetic>")) == .ignored)
    }

    @Test func ignoresNonAssistantLines() {
        #expect(parse(LogLines.json(["type": "user", "message": ["usage": ["input_tokens": 1]]])) == .ignored)
        #expect(parse(LogLines.json(["type": "assistant", "message": ["model": "claude-opus-5-5"]])) == .ignored)
    }

    @Test func ignoresBrokenJson() {
        #expect(parse(#"{"type": "assistant", "usage": "#) == .ignored)
        #expect(parse("") == .ignored)
    }

    @Test func parsesAiTitle() {
        #expect(parse(LogLines.title(sessionId: "s-7", title: "  로그 파서 고치기 ")) == .title(sessionId: "s-7", title: "로그 파서 고치기"))
        #expect(parse(LogLines.title(sessionId: "s-7", title: "   ")) == .ignored)
    }

    @Test func keepsNonAsciiNames() throws {
        let record = try usage(LogLines.assistant(id: "m", cwd: "/Users/me/작업/앱 🚀"))
        #expect(record.projectPath == "/Users/me/작업/앱 🚀")
    }

    @Test func findsSessionIdFromPath() {
        #expect(LogParser.sessionId(fromPath: "/p/-Users-me-app/abc-123.jsonl") == "abc-123")
        #expect(LogParser.sessionId(fromPath: "/p/-Users-me-app/abc-123/subagents/agent-1.jsonl") == "abc-123")
    }
}
