import Foundation

public enum ParsedLine: Sendable, Equatable {
    case ignored
    case usage(TokenRecord)
    case title(sessionId: String, title: String)
}

/// Reads one Claude Code session log line (v0.1.0 spec 5.1, 5.2).
public enum LogParser {
    private static let usageMarker = Data("\"usage\"".utf8)
    private static let titleMarker = Data("\"ai-title\"".utf8)
    private static let syntheticModel = "<synthetic>"
    private static let subagentDirectory = "subagents"

    public static func parse(_ line: Data, fallbackSessionId: String) -> ParsedLine {
        // Most lines carry neither field; skip them before paying for a JSON parse.
        guard line.range(of: usageMarker) != nil || line.range(of: titleMarker) != nil,
              let object = (try? JSONSerialization.jsonObject(with: line)) as? [String: Any] else {
            return .ignored
        }
        let type = object["type"] as? String ?? ""
        var sessionId = object["sessionId"] as? String ?? ""
        if sessionId.isEmpty {
            sessionId = fallbackSessionId
        }

        if type == "ai-title" {
            let title = (object["aiTitle"] as? String ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
            guard !title.isEmpty, !sessionId.isEmpty else {
                return .ignored
            }
            return .title(sessionId: sessionId, title: title)
        }

        guard type == "assistant",
              let message = object["message"] as? [String: Any],
              let usage = message["usage"] as? [String: Any], !usage.isEmpty,
              let model = message["model"] as? String, !model.isEmpty, model != syntheticModel,
              let timestamp = (object["timestamp"] as? String).flatMap(ISODate.parse) else {
            return .ignored
        }

        // Streaming writes the same response 2-5 times; message id + request id identifies it.
        var key = message["id"] as? String ?? ""
        if key.isEmpty {
            key = object["uuid"] as? String ?? ""
        }
        guard !key.isEmpty else {
            return .ignored
        }
        if let requestId = object["requestId"] as? String, !requestId.isEmpty {
            key += "|" + requestId
        }

        var record = TokenRecord(key: key, timestamp: timestamp, sessionId: sessionId,
                                 projectPath: object["cwd"] as? String ?? "", model: model)
        record.isFast = usage["speed"] as? String == "fast"
        record.input = count(usage, "input_tokens")
        record.output = count(usage, "output_tokens")
        record.cacheRead = count(usage, "cache_read_input_tokens")
        let creation = usage["cache_creation"] as? [String: Any] ?? [:]
        if creation["ephemeral_5m_input_tokens"] != nil || creation["ephemeral_1h_input_tokens"] != nil {
            record.cacheWrite5m = count(creation, "ephemeral_5m_input_tokens")
            record.cacheWrite1h = count(creation, "ephemeral_1h_input_tokens")
        } else {
            record.cacheWrite5m = count(usage, "cache_creation_input_tokens")
        }
        return .usage(record)
    }

    /// Subagent logs live in `<sessionId>/subagents/agent-*.jsonl` and count toward that parent session.
    public static func sessionId(fromPath path: String) -> String {
        let file = URL(fileURLWithPath: path)
        let parent = file.deletingLastPathComponent()
        if parent.lastPathComponent == subagentDirectory {
            return parent.deletingLastPathComponent().lastPathComponent
        }
        return file.deletingPathExtension().lastPathComponent
    }

    private static func count(_ object: [String: Any], _ key: String) -> Int {
        Int(JSONValues.number(object[key]) ?? 0)
    }
}
