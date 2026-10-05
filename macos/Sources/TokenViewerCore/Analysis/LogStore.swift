import Foundation

/// Remembers how far each session log was read so a rescan parses only the lines added since (v0.1.0 spec 7).
public final class LogStore {
    public static let defaultChunkBytes = 4 * 1024 * 1024
    public static let retentionDays = 31

    private struct FileCursor {
        var offset: UInt64 = 0
        var keys: Set<String> = []
    }

    private static let resourceKeys: [URLResourceKey] = [.isRegularFileKey, .fileSizeKey, .contentModificationDateKey]
    private static let newline = UInt8(ascii: "\n")

    private let chunkBytes: Int
    private var cursors: [String: FileCursor] = [:]
    private var records: [String: TokenRecord] = [:]
    private var sessionTitles: [String: String] = [:]

    public init(chunkBytes: Int = LogStore.defaultChunkBytes) {
        self.chunkBytes = chunkBytes
    }

    public var recordCount: Int { records.count }

    public var snapshot: LogSnapshot { LogSnapshot(records: Array(records.values), sessionTitles: sessionTitles) }

    /// Reads new lines under `root`. Returns true when the snapshot changed.
    @discardableResult
    public func scan(root: URL, now: Date, shouldContinue: () -> Bool = { true }) -> Bool {
        var changed = false
        let cutoff = now.addingTimeInterval(-Double(Self.retentionDays) * 86_400)
        var seen = Set<String>()
        let enumerator = FileManager.default.enumerator(at: root, includingPropertiesForKeys: Self.resourceKeys)
        while let url = enumerator?.nextObject() as? URL {
            guard shouldContinue() else {
                return changed
            }
            guard url.pathExtension == "jsonl",
                  let values = try? url.resourceValues(forKeys: Set(Self.resourceKeys)),
                  values.isRegularFile == true else {
                continue
            }
            let path = url.path
            seen.insert(path)
            let size = UInt64(values.fileSize ?? 0)

            var cursor: FileCursor
            if let existing = cursors[path] {
                cursor = existing
            } else {
                // Claude Code deletes transcripts after 30 days; skip anything older on first sight.
                if let modified = values.contentModificationDate, modified < cutoff {
                    continue
                }
                cursor = FileCursor()
            }
            if size < cursor.offset {
                // Truncated or replaced: drop what this file contributed and read it again.
                for key in cursor.keys {
                    records.removeValue(forKey: key)
                }
                cursor = FileCursor()
                changed = true
            }
            if size > cursor.offset {
                changed = read(path: path, cursor: &cursor) || changed
            }
            cursors[path] = cursor
        }

        for path in cursors.keys where !seen.contains(path) {
            forget(path)
            changed = true
        }
        return prune(cutoff: cutoff) || changed
    }

    private func read(path: String, cursor: inout FileCursor) -> Bool {
        guard let handle = FileHandle(forReadingAtPath: path) else {
            return false
        }
        defer { try? handle.close() }
        do {
            try handle.seek(toOffset: cursor.offset)
        } catch {
            return false
        }

        let fallbackSessionId = LogParser.sessionId(fromPath: path)
        var changed = false
        var pending = Data()
        while let chunk = try? handle.read(upToCount: chunkBytes), !chunk.isEmpty {
            pending.append(chunk)
            var lineStart = pending.startIndex
            while let newline = pending[lineStart...].firstIndex(of: Self.newline) {
                switch LogParser.parse(pending[lineStart..<newline], fallbackSessionId: fallbackSessionId) {
                case .usage(let record):
                    // Later copies of the same response replace earlier ones.
                    records[record.key] = record
                    cursor.keys.insert(record.key)
                    changed = true
                case .title(let sessionId, let title):
                    sessionTitles[sessionId] = title
                    changed = true
                case .ignored:
                    break
                }
                cursor.offset += UInt64(newline - lineStart + 1)
                lineStart = pending.index(after: newline)
            }
            // Keep the unfinished last line; the offset stays before it until Claude Code finishes writing it.
            pending = Data(pending[lineStart...])
        }
        return changed
    }

    private func forget(_ path: String) {
        guard let cursor = cursors.removeValue(forKey: path) else {
            return
        }
        for key in cursor.keys {
            records.removeValue(forKey: key)
        }
    }

    private func prune(cutoff: Date) -> Bool {
        let before = records.count
        records = records.filter { $0.value.timestamp >= cutoff }
        return records.count != before
    }
}
