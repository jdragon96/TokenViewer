import Foundation
import TokenViewerCore

enum ErrorLog {
    private static let directory = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Logs/TokenViewer")

    /// Keeps the last format-drift response for diagnosis. Only the response body is written; the request and its token never are.
    static func writeBadResponse(_ result: FetchResult) {
        try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        var data = Data("\(ISO8601DateFormatter().string(from: Date())) HTTP \(result.httpStatus) \(result.detail)\n".utf8)
        data.append(result.rawBody)
        try? data.write(to: directory.appendingPathComponent("usage-response.log"), options: .atomic)
    }
}
