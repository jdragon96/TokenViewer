import Foundation
import Testing
@testable import TokenViewerCore

enum TestSupport {
    /// The repository root, found from this file's path so tests read the real `shared/prices.json`.
    static let repoRoot = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent()
        .deletingLastPathComponent()
        .deletingLastPathComponent()
        .deletingLastPathComponent()

    static let seoul = TimeZone(identifier: "Asia/Seoul")!

    static func fixture(_ name: String) throws -> Data {
        try Data(contentsOf: repoRoot.appendingPathComponent("macos/Tests/TokenViewerCoreTests/Fixtures/\(name)"))
    }

    static func date(_ text: String) -> Date {
        ISO8601DateFormatter().date(from: text)!
    }

    static func isoString(_ date: Date) -> String {
        let formatter = ISO8601DateFormatter()
        formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        return formatter.string(from: date)
    }
}

/// Polls until `condition` holds or the timeout passes, and returns the last answer.
func waitUntil(timeout: TimeInterval = 3, _ condition: @Sendable () async -> Bool) async -> Bool {
    let deadline = Date().addingTimeInterval(timeout)
    while Date() < deadline {
        if await condition() {
            return true
        }
        try? await Task.sleep(nanoseconds: 10_000_000)
    }
    return await condition()
}

actor Recorder<Value: Sendable> {
    private(set) var values: [Value] = []

    func append(_ value: Value) {
        values.append(value)
    }
}

/// Holds an async call until `open()`; `isWaiting` tells the test the call has arrived.
actor Gate {
    private var continuation: CheckedContinuation<Void, Never>?
    private var isOpen = false

    var isWaiting: Bool { continuation != nil }

    func pass() async {
        if isOpen {
            return
        }
        await withCheckedContinuation { continuation = $0 }
    }

    func open() {
        isOpen = true
        continuation?.resume()
        continuation = nil
    }
}

final class TemporaryDirectory {
    let url: URL

    init() throws {
        url = FileManager.default.temporaryDirectory.appendingPathComponent("TokenViewerTests-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
    }

    deinit {
        try? FileManager.default.removeItem(at: url)
    }

    @discardableResult
    func write(_ relativePath: String, _ lines: [String], terminated: Bool = true) throws -> URL {
        let file = url.appendingPathComponent(relativePath)
        try FileManager.default.createDirectory(at: file.deletingLastPathComponent(), withIntermediateDirectories: true)
        let text = lines.joined(separator: "\n") + (terminated && !lines.isEmpty ? "\n" : "")
        try Data(text.utf8).write(to: file)
        return file
    }

    func append(_ relativePath: String, _ text: String) throws {
        let handle = try FileHandle(forWritingTo: url.appendingPathComponent(relativePath))
        defer { try? handle.close() }
        try handle.seekToEnd()
        try handle.write(contentsOf: Data(text.utf8))
    }
}

/// Builds Claude Code session log lines in the shape recorded on 2026-10-05.
enum LogLines {
    static func assistant(
        id: String,
        requestId: String? = "req-1",
        sessionId: String? = "session-1",
        cwd: String = "/Users/me/work/app",
        model: String = "claude-opus-5-5",
        timestamp: String = "2026-10-05T06:00:00.000Z",
        input: Int = 100,
        output: Int = 50,
        cacheRead: Int = 1_000,
        cacheCreation: Int = 200,
        split: (fiveMinutes: Int, oneHour: Int)? = nil,
        speed: String? = nil
    ) -> String {
        var usage: [String: Any] = [
            "input_tokens": input,
            "output_tokens": output,
            "cache_read_input_tokens": cacheRead,
            "cache_creation_input_tokens": cacheCreation,
        ]
        if let split {
            usage["cache_creation"] = ["ephemeral_5m_input_tokens": split.fiveMinutes, "ephemeral_1h_input_tokens": split.oneHour]
        }
        if let speed {
            usage["speed"] = speed
        }
        var line: [String: Any] = [
            "type": "assistant",
            "cwd": cwd,
            "timestamp": timestamp,
            "uuid": "uuid-\(id)",
            "message": ["id": id, "model": model, "usage": usage],
        ]
        if let requestId {
            line["requestId"] = requestId
        }
        if let sessionId {
            line["sessionId"] = sessionId
        }
        return json(line)
    }

    static func title(sessionId: String, title: String) -> String {
        json(["type": "ai-title", "sessionId": sessionId, "aiTitle": title])
    }

    static func json(_ object: [String: Any]) -> String {
        let data = try! JSONSerialization.data(withJSONObject: object, options: [.sortedKeys])
        return String(decoding: data, as: UTF8.self)
    }
}
