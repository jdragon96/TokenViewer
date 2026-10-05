import Foundation
import TokenViewerCore

/// GET /api/oauth/usage (v0.1.0 spec 3.1). The token lives only in this request.
enum UsageClient {
    // Ephemeral: no cookies, no cache, nothing written to disk.
    private static let session = URLSession(configuration: .ephemeral)

    static func request(accessToken: String) async -> HTTPResponse {
        var request = URLRequest(url: AppConstants.usageURL, timeoutInterval: AppConstants.requestTimeout)
        request.setValue("Bearer \(accessToken)", forHTTPHeaderField: "Authorization")
        request.setValue(AppConstants.usageBetaHeader, forHTTPHeaderField: "anthropic-beta")
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        request.setValue("TokenViewer/\(AppInfo.version)", forHTTPHeaderField: "User-Agent")
        do {
            let (data, response) = try await session.data(for: request)
            return HTTPResponse(status: (response as? HTTPURLResponse)?.statusCode ?? 0, body: data)
        } catch let error as URLError where error.code == .timedOut {
            return HTTPResponse(status: 0, errorText: "응답 시간 초과")
        } catch {
            return HTTPResponse(status: 0, errorText: error.localizedDescription)
        }
    }
}
